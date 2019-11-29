/*
 * Copyright (C) 2015 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

#define _GNU_SOURCE
#include "darshan-util-config.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <inttypes.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "darshan-logutils.h"

/* integer counter name strings for the ABCXYZ module */
#define X(a) #a,
char *abcxyz_counter_names[] = {
    ABCXYZ_COUNTERS
};

/* floating point counter name strings for the ABCXYZ module */
char *abcxyz_f_counter_names[] = {
    ABCXYZ_F_COUNTERS
};
#undef X

/* prototypes for each of the ABCXYZ module's logutil functions */
static int darshan_log_get_abcxyz_record(darshan_fd fd, void** abcxyz_buf_p);
static int darshan_log_put_abcxyz_record(darshan_fd fd, void* abcxyz_buf);
static void darshan_log_print_abcxyz_record(void *file_rec,
    char *file_name, char *mnt_pt, char *fs_type);
static void darshan_log_print_abcxyz_description(int ver);
static void darshan_log_print_abcxyz_record_diff(void *file_rec1, char *file_name1,
    void *file_rec2, char *file_name2);
static void darshan_log_agg_abcxyz_records(void *rec, void *agg_rec, int init_flag);

/* structure storing each function needed for implementing the darshan
 * logutil interface. these functions are used for reading, writing, and
 * printing module data in a consistent manner.
 */
struct darshan_mod_logutil_funcs abcxyz_logutils =
{
    .log_get_record = &darshan_log_get_abcxyz_record,
    .log_put_record = &darshan_log_put_abcxyz_record,
    .log_print_record = &darshan_log_print_abcxyz_record,
    .log_print_description = &darshan_log_print_abcxyz_description,
    .log_print_diff = &darshan_log_print_abcxyz_record_diff,
    .log_agg_records = &darshan_log_agg_abcxyz_records
};

/* retrieve a ABCXYZ record from log file descriptor 'fd', storing the
 * data in the buffer address pointed to by 'abcxyz_buf_p'. Return 1 on
 * successful record read, 0 on no more data, and -1 on error.
 */
static int darshan_log_get_abcxyz_record(darshan_fd fd, void** abcxyz_buf_p)
{
    struct darshan_abcxyz_record *rec = *((struct darshan_abcxyz_record **)abcxyz_buf_p);
    int i;
    int ret;

    if(fd->mod_map[DARSHAN_ABCXYZ_MOD].len == 0)
        return(0);

    if(*abcxyz_buf_p == NULL)
    {
        rec = malloc(sizeof(*rec));
        if(!rec)
            return(-1);
    }

    /* read a ABCXYZ module record from the darshan log file */
    ret = darshan_log_get_mod(fd, DARSHAN_ABCXYZ_MOD, rec,
        sizeof(struct darshan_abcxyz_record));

    if(*abcxyz_buf_p == NULL)
    {
        if(ret == sizeof(struct darshan_abcxyz_record))
            *abcxyz_buf_p = rec;
        else
            free(rec);
    }

    if(ret < 0)
        return(-1);
    else if(ret < sizeof(struct darshan_abcxyz_record))
        return(0);
    else
    {
        /* if the read was successful, do any necessary byte-swapping */
        if(fd->swap_flag)
        {
            DARSHAN_BSWAP64(&(rec->base_rec.id));
            DARSHAN_BSWAP64(&(rec->base_rec.rank));
            for(i=0; i<ABCXYZ_NUM_INDICES; i++)
                DARSHAN_BSWAP64(&rec->counters[i]);
            for(i=0; i<ABCXYZ_F_NUM_INDICES; i++)
                DARSHAN_BSWAP64(&rec->fcounters[i]);
        }

        return(1);
    }
}

/* write the ABCXYZ record stored in 'abcxyz_buf' to log file descriptor 'fd'.
 * Return 0 on success, -1 on failure
 */
static int darshan_log_put_abcxyz_record(darshan_fd fd, void* abcxyz_buf)
{
    struct darshan_abcxyz_record *rec = (struct darshan_abcxyz_record *)abcxyz_buf;
    int ret;

    /* append ABCXYZ record to darshan log file */
    ret = darshan_log_put_mod(fd, DARSHAN_ABCXYZ_MOD, rec,
        sizeof(struct darshan_abcxyz_record), DARSHAN_ABCXYZ_VER);
    if(ret < 0)
        return(-1);

    return(0);
}

/* print all I/O data record statistics for the given ABCXYZ record */
static void darshan_log_print_abcxyz_record(void *file_rec, char *file_name,
    char *mnt_pt, char *fs_type)
{
    int i;
    struct darshan_abcxyz_record *abcxyz_rec =
        (struct darshan_abcxyz_record *)file_rec;

    /* print each of the integer and floating point counters for the ABCXYZ module */
    for(i=0; i<ABCXYZ_NUM_INDICES; i++)
    {
        /* macro defined in darshan-logutils.h */
        DARSHAN_D_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
            abcxyz_rec->base_rec.rank, abcxyz_rec->base_rec.id,
            abcxyz_counter_names[i], abcxyz_rec->counters[i],
            file_name, mnt_pt, fs_type);
    }

    for(i=0; i<ABCXYZ_F_NUM_INDICES; i++)
    {
        /* macro defined in darshan-logutils.h */
        DARSHAN_F_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
            abcxyz_rec->base_rec.rank, abcxyz_rec->base_rec.id,
            abcxyz_f_counter_names[i], abcxyz_rec->fcounters[i],
            file_name, mnt_pt, fs_type);
    }

    return;
}

/* print out a description of the ABCXYZ module record fields */
static void darshan_log_print_abcxyz_description(int ver)
{
    printf("\n# description of ABCXYZ counters:\n");
    printf("#   ABCXYZ_FOOS: number of 'foo' function calls.\n");
    printf("#   ABCXYZ_FOO_MAX_DAT: maximum data value set by calls to 'foo'.\n");
    printf("#   ABCXYZ_F_FOO_TIMESTAMP: timestamp of the first call to function 'foo'.\n");
    printf("#   ABCXYZ_F_FOO_MAX_DURATION: timer indicating duration of call to 'foo' with max ABCXYZ_FOO_MAX_DAT value.\n");

    return;
}

/* print a diff of two ABCXYZ records (with the same record id) */
static void darshan_log_print_abcxyz_record_diff(void *file_rec1, char *file_name1,
    void *file_rec2, char *file_name2)
{
    struct darshan_abcxyz_record *file1 = (struct darshan_abcxyz_record *)file_rec1;
    struct darshan_abcxyz_record *file2 = (struct darshan_abcxyz_record *)file_rec2;
    int i;

    /* NOTE: we assume that both input records are the same module format version */

    for(i=0; i<ABCXYZ_NUM_INDICES; i++)
    {
        if(!file2)
        {
            printf("- ");
            DARSHAN_D_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file1->base_rec.rank, file1->base_rec.id, abcxyz_counter_names[i],
                file1->counters[i], file_name1, "", "");

        }
        else if(!file1)
        {
            printf("+ ");
            DARSHAN_D_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file2->base_rec.rank, file2->base_rec.id, abcxyz_counter_names[i],
                file2->counters[i], file_name2, "", "");
        }
        else if(file1->counters[i] != file2->counters[i])
        {
            printf("- ");
            DARSHAN_D_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file1->base_rec.rank, file1->base_rec.id, abcxyz_counter_names[i],
                file1->counters[i], file_name1, "", "");
            printf("+ ");
            DARSHAN_D_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file2->base_rec.rank, file2->base_rec.id, abcxyz_counter_names[i],
                file2->counters[i], file_name2, "", "");
        }
    }

    for(i=0; i<ABCXYZ_F_NUM_INDICES; i++)
    {
        if(!file2)
        {
            printf("- ");
            DARSHAN_F_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file1->base_rec.rank, file1->base_rec.id, abcxyz_f_counter_names[i],
                file1->fcounters[i], file_name1, "", "");

        }
        else if(!file1)
        {
            printf("+ ");
            DARSHAN_F_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file2->base_rec.rank, file2->base_rec.id, abcxyz_f_counter_names[i],
                file2->fcounters[i], file_name2, "", "");
        }
        else if(file1->fcounters[i] != file2->fcounters[i])
        {
            printf("- ");
            DARSHAN_F_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file1->base_rec.rank, file1->base_rec.id, abcxyz_f_counter_names[i],
                file1->fcounters[i], file_name1, "", "");
            printf("+ ");
            DARSHAN_F_COUNTER_PRINT(darshan_module_names[DARSHAN_ABCXYZ_MOD],
                file2->base_rec.rank, file2->base_rec.id, abcxyz_f_counter_names[i],
                file2->fcounters[i], file_name2, "", "");
        }
    }

    return;
}

/* aggregate the input ABCXYZ record 'rec'  into the output record 'agg_rec' */
static void darshan_log_agg_abcxyz_records(void *rec, void *agg_rec, int init_flag)
{
    struct darshan_abcxyz_record *abcxyz_rec = (struct darshan_abcxyz_record *)rec;
    struct darshan_abcxyz_record *agg_abcxyz_rec = (struct darshan_abcxyz_record *)agg_rec;
    int i;

    for(i = 0; i < ABCXYZ_NUM_INDICES; i++)
    {
        switch(i)
        {
            case ABCXYZ_FOOS:
                /* sum */
                agg_abcxyz_rec->counters[i] += abcxyz_rec->counters[i];
                break;
            case ABCXYZ_FOO_MAX_DAT:
                /* max */
                if(abcxyz_rec->counters[i] > agg_abcxyz_rec->counters[i])
                {
                    agg_abcxyz_rec->counters[i] = abcxyz_rec->counters[i];
                    agg_abcxyz_rec->fcounters[ABCXYZ_F_FOO_MAX_DURATION] =
                        abcxyz_rec->fcounters[ABCXYZ_F_FOO_MAX_DURATION];
                }
                break;
            default:
                /* if we don't know how to aggregate this counter, just set to -1 */
                agg_abcxyz_rec->counters[i] = -1;
                break;
        }
    }

    for(i = 0; i < ABCXYZ_F_NUM_INDICES; i++)
    {
        switch(i)
        {
            case ABCXYZ_F_FOO_TIMESTAMP:
                /* min non-zero */
                if((abcxyz_rec->fcounters[i] > 0)  &&
                    ((agg_abcxyz_rec->fcounters[i] == 0) ||
                    (abcxyz_rec->fcounters[i] < agg_abcxyz_rec->fcounters[i])))
                {
                    agg_abcxyz_rec->fcounters[i] = abcxyz_rec->fcounters[i];
                }
                break;
            default:
                /* if we don't know how to aggregate this counter, just set to -1 */
                agg_abcxyz_rec->fcounters[i] = -1;
                break;
        }
    }

    return;
}

/*
 * Local variables:
 *  c-indent-level: 4
 *  c-basic-offset: 4
 * End:
 *
 * vim: ts=8 sts=4 sw=4 expandtab
 */