/*
 *  (C) 2009 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#include "darshan-runtime-config.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#ifdef HAVE_MNTENT_H
#include <mntent.h>
#endif
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <pthread.h>

#include <mpi.h>

#include "darshan-core.h"

extern char* __progname_full;

static void darshan_core_initialize(int *argc, char ***argv);
static void darshan_core_shutdown(void);

static char *darshan_get_exe_and_mounts(int rank);
static void darshan_get_exe_and_mounts_root(char* trailing_data, int space_left);

/* internal variables */
static struct darshan_core_job_runtime *darshan_global_job = NULL;
static pthread_mutex_t darshan_mutex = PTHREAD_MUTEX_INITIALIZER;

#define DARSHAN_LOCK() pthread_mutex_lock(&darshan_mutex)
#define DARSHAN_UNLOCK() pthread_mutex_unlock(&darshan_mutex)

#define DARSHAN_MAX_MNTS 64
#define DARSHAN_MAX_MNT_PATH 256
#define DARSHAN_MAX_MNT_TYPE 32
struct mnt_data
{
    int64_t hash;
    int64_t block_size;
    char path[DARSHAN_MAX_MNT_PATH];
    char type[DARSHAN_MAX_MNT_TYPE];
};
static struct mnt_data mnt_data_array[DARSHAN_MAX_MNTS];
static int mnt_data_count = 0;

/* intercept MPI initialize and finalize to initialize darshan */
int MPI_Init(int *argc, char ***argv)
{
    int ret;

    ret = DARSHAN_MPI_CALL(PMPI_Init)(argc, argv);
    if(ret != MPI_SUCCESS)
    {
        return(ret);
    }

    darshan_core_initialize(argc, argv);

    return(ret);
}

int MPI_Init_thread(int *argc, char ***argv, int required, int *provided)
{
    int ret;

    ret = DARSHAN_MPI_CALL(PMPI_Init_thread)(argc, argv, required, provided);
    if (ret != MPI_SUCCESS)
    {
        return(ret);
    }

    darshan_core_initialize(argc, argv);

    return(ret);
}

int MPI_Finalize(void)
{
    int ret;

    darshan_core_shutdown();

    ret = DARSHAN_MPI_CALL(PMPI_Finalize)();
    return(ret);
}

static void darshan_core_initialize(int *argc, char ***argv)
{
    int i;
    int nprocs;
    int rank;
    int internal_timing_flag = 0;
    double init_start, init_time, init_max;
    char* truncate_string = "<TRUNCATED>";
    int truncate_offset;
    int chars_left = 0;

    DARSHAN_MPI_CALL(PMPI_Comm_size)(MPI_COMM_WORLD, &nprocs);
    DARSHAN_MPI_CALL(PMPI_Comm_rank)(MPI_COMM_WORLD, &rank);

    if(getenv("DARSHAN_INTERNAL_TIMING"))
        internal_timing_flag = 1;

    if (internal_timing_flag)
        init_start = DARSHAN_MPI_CALL(PMPI_Wtime)();

    /* setup darshan runtime if darshan is enabled and hasn't been initialized already */
    if (!getenv("DARSHAN_DISABLE") && !darshan_global_job)
    {
        /* allocate structure to track darshan_global_job information */
        darshan_global_job = malloc(sizeof(*darshan_global_job));
        if (darshan_global_job)
        {
            memset(darshan_global_job, 0, sizeof(*darshan_global_job));

            if (getenv("DARSHAN_DISABLE_TIMING"))
            {
                darshan_global_job->flags |= CP_FLAG_NOTIMING;
            }

            strcpy(darshan_global_job->log_job.version_string, CP_VERSION);
            darshan_global_job->log_job.magic_nr = CP_MAGIC_NR;
            darshan_global_job->log_job.uid = getuid();
            darshan_global_job->log_job.start_time = time(NULL);
            darshan_global_job->log_job.nprocs = nprocs;
            darshan_global_job->wtime_offset = DARSHAN_MPI_CALL(PMPI_Wtime)();
            darshan_global_job->mod_list_head = NULL;

            /* record exe and arguments */
            for(i=0; i<(*argc); i++)
            {
                chars_left = CP_EXE_LEN-strlen(darshan_global_job->exe);
                strncat(darshan_global_job->exe, *(argv[i]), chars_left);
                if(i < ((*argc)-1))
                {
                    chars_left = CP_EXE_LEN-strlen(darshan_global_job->exe);
                    strncat(darshan_global_job->exe, " ", chars_left);
                }
            }

            /* if we don't see any arguments, then use glibc symbol to get
             * program name at least (this happens in fortran)
             */
            if(argc == 0)
            {
                chars_left = CP_EXE_LEN-strlen(darshan_global_job->exe);
                strncat(darshan_global_job->exe, __progname_full, chars_left);
                chars_left = CP_EXE_LEN-strlen(darshan_global_job->exe);
                strncat(darshan_global_job->exe, " <unknown args>", chars_left);
            }

            if(chars_left == 0)
            {
                /* we ran out of room; mark that string was truncated */
                truncate_offset = CP_EXE_LEN - strlen(truncate_string);
                sprintf(&darshan_global_job->exe[truncate_offset], "%s",
                    truncate_string);
            }

            /* collect information about command line and 
             * mounted file systems 
             */
            darshan_global_job->trailing_data = darshan_get_exe_and_mounts(rank);
        }
    }

    if(internal_timing_flag)
    {
        init_time = DARSHAN_MPI_CALL(PMPI_Wtime)() - init_start;
        DARSHAN_MPI_CALL(PMPI_Reduce)(&init_time, &init_max, 1,
            MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
        if(rank == 0)
        {
            printf("#darshan:<op>\t<nprocs>\t<time>\n");
            printf("darshan:init\t%d\t%f\n", nprocs, init_max);
        }
    }

    return;
}

static void darshan_core_shutdown()
{
    int internal_timing_flag = 0;

    if(getenv("DARSHAN_INTERNAL_TIMING"))
        internal_timing_flag = 1;

    printf("darshan SHUTDOWN\n");

    return;
}

static int mnt_data_cmp(const void* a, const void* b)
{
    const struct mnt_data *d_a = (const struct mnt_data*)a;
    const struct mnt_data *d_b = (const struct mnt_data*)b;

    if(strlen(d_a->path) > strlen(d_b->path))
        return(-1);
    else if(strlen(d_a->path) < strlen(d_b->path))
        return(1);
    else
        return(0);
}

/* adds an entry to table of mounted file systems */
static void add_entry(char* trailing_data, int* space_left, struct mntent *entry)
{
    int ret;
    char tmp_mnt[256];
    struct statfs statfsbuf;

    strncpy(mnt_data_array[mnt_data_count].path, entry->mnt_dir,
        DARSHAN_MAX_MNT_PATH-1);
    strncpy(mnt_data_array[mnt_data_count].type, entry->mnt_type,
        DARSHAN_MAX_MNT_TYPE-1);
    mnt_data_array[mnt_data_count].hash =
        darshan_hash((void*)mnt_data_array[mnt_data_count].path,
        strlen(mnt_data_array[mnt_data_count].path), 0);
    /* NOTE: we now try to detect the preferred block size for each file 
     * system using fstatfs().  On Lustre we assume a size of 1 MiB 
     * because fstatfs() reports 4 KiB. 
     */
#ifndef LL_SUPER_MAGIC
#define LL_SUPER_MAGIC 0x0BD00BD0
#endif
    ret = statfs(entry->mnt_dir, &statfsbuf);
    if(ret == 0 && statfsbuf.f_type != LL_SUPER_MAGIC)
        mnt_data_array[mnt_data_count].block_size = statfsbuf.f_bsize;
    else if(ret == 0 && statfsbuf.f_type == LL_SUPER_MAGIC)
        mnt_data_array[mnt_data_count].block_size = 1024*1024;
    else
        mnt_data_array[mnt_data_count].block_size = 4096;

    /* store mount information for use in header of darshan log */
    ret = snprintf(tmp_mnt, 256, "\n%" PRId64 "\t%s\t%s",
        mnt_data_array[mnt_data_count].hash,
        entry->mnt_type, entry->mnt_dir);
    if(ret < 256 && strlen(tmp_mnt) <= (*space_left))
    {
        strcat(trailing_data, tmp_mnt);
        (*space_left) -= strlen(tmp_mnt);
    }

    mnt_data_count++;
    return;
}

/* darshan_get_exe_and_mounts_root()
 *
 * collects command line and list of mounted file systems into a string that
 * will be stored with the job header
 */
static void darshan_get_exe_and_mounts_root(char* trailing_data, int space_left)
{
    FILE* tab;
    struct mntent *entry;
    char* exclude;
    int tmp_index = 0;
    int skip = 0;

    /* skip these fs types */
    static char* fs_exclusions[] = {
        "tmpfs",
        "proc",
        "sysfs",
        "devpts",
        "binfmt_misc",
        "fusectl",
        "debugfs",
        "securityfs",
        "nfsd",
        "none",
        "rpc_pipefs",
        "hugetlbfs",
        "cgroup",
        NULL
    };

    /* length of exe has already been safety checked in darshan-posix.c */
    strcat(trailing_data, darshan_global_job->exe);
    space_left = CP_EXE_LEN - strlen(trailing_data);

    /* we make two passes through mounted file systems; in the first pass we
     * grab any non-nfs mount points, then on the second pass we grab nfs
     * mount points
     */

    tab = setmntent("/etc/mtab", "r");
    if(!tab)
        return;
    /* loop through list of mounted file systems */
    while(mnt_data_count<DARSHAN_MAX_MNTS && (entry = getmntent(tab)) != NULL)
    {
        /* filter out excluded fs types */
        tmp_index = 0;
        skip = 0;
        while((exclude = fs_exclusions[tmp_index]))
        {
            if(!(strcmp(exclude, entry->mnt_type)))
            {
                skip =1;
                break;
            }
            tmp_index++;
        }

        if(skip || (strcmp(entry->mnt_type, "nfs") == 0))
            continue;

        add_entry(trailing_data, &space_left, entry);
    }
    endmntent(tab);

    tab = setmntent("/etc/mtab", "r");
    if(!tab)
        return;
    /* loop through list of mounted file systems */
    while(mnt_data_count<DARSHAN_MAX_MNTS && (entry = getmntent(tab)) != NULL)
    {
        if(strcmp(entry->mnt_type, "nfs") != 0)
            continue;

        add_entry(trailing_data, &space_left, entry);
    }
    endmntent(tab);

    /* Sort mount points in order of longest path to shortest path.  This is
     * necessary so that if we try to match file paths to mount points later
     * we don't match on "/" every time.
     */
    qsort(mnt_data_array, mnt_data_count, sizeof(mnt_data_array[0]), mnt_data_cmp);
    return;
}

/* darshan_get_exe_and_mounts()
 *
 * collects command line and list of mounted file systems into a string that
 * will be stored with the job header
 */
static char *darshan_get_exe_and_mounts(int rank)
{
    char* trailing_data;
    int space_left;

    space_left = CP_EXE_LEN + 1;
    trailing_data = malloc(space_left);
    if(!trailing_data)
    {
        return(NULL);
    }
    memset(trailing_data, 0, space_left);

    if(rank == 0)
    {
        darshan_get_exe_and_mounts_root(trailing_data, space_left);
    }

    /* broadcast trailing data to all nodes */
    DARSHAN_MPI_CALL(PMPI_Bcast)(trailing_data, space_left, MPI_CHAR, 0,
        MPI_COMM_WORLD);
    /* broadcast mount count to all nodes */
    DARSHAN_MPI_CALL(PMPI_Bcast)(&mnt_data_count, 1, MPI_INT, 0,
        MPI_COMM_WORLD);
    /* broadcast mount data to all nodes */
    DARSHAN_MPI_CALL(PMPI_Bcast)(mnt_data_array,
        mnt_data_count*sizeof(mnt_data_array[0]), MPI_BYTE, 0, MPI_COMM_WORLD);

    return(trailing_data);
}

void darshan_core_register_module(
    char *name,
    struct darshan_module_funcs *funcs,
    int *runtime_mem_limit)
{
    struct darshan_core_module *tmp;
    struct darshan_core_module *new_mod;

    *runtime_mem_limit = 0;
    if (!darshan_global_job)
        return;

    DARSHAN_LOCK();
    tmp = darshan_global_job->mod_list_head;
    while(tmp)
    {
        /* silently return if this module is already registered */
        if (strcmp(tmp->name, name) == 0)
        {
            DARSHAN_UNLOCK();
            return;
        }
        tmp = tmp->next_mod;
    }

    /* allocate new module and add to the head of the linked list of darshan modules */
    new_mod = malloc(sizeof(*new_mod));
    if (!new_mod)
    {
        DARSHAN_UNLOCK();
        return;
    }

    memset(new_mod, 0, sizeof(*new_mod));
    strncpy(new_mod->name, name, DARSHAN_MOD_NAME_LEN);
    new_mod->mod_funcs = *funcs;
    new_mod->next_mod = darshan_global_job->mod_list_head;
    darshan_global_job->mod_list_head = new_mod;
    DARSHAN_UNLOCK();

    /* TODO: something smarter than just 2 MiB per module */
    *runtime_mem_limit = 2 * 1024 * 1024;

    return;
}

void darshan_core_lookup_id(
    void *name,
    int len,
    int printable_flag,
    darshan_file_id *id)
{
    darshan_file_id tmp_id;

    if (!darshan_global_job)
        return;

    /* TODO: what do you do with printable flag? */

    /* hash the input name to get a unique id for this record */
    tmp_id = darshan_hash(name, len, 0);
    
    /* TODO: how to store the filename to hash mapping? */

    *id = tmp_id;
    return;
}

double darshan_core_wtime()
{
    if(!darshan_global_job || darshan_global_job->flags & CP_FLAG_NOTIMING)
    {
        return(0);
    }

    return DARSHAN_MPI_CALL(PMPI_Wtime)();
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */