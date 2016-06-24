/*
 * Copyright (C) 2015 University of Chicago.
 * See COPYRIGHT notice in top-level directory.
 *
 */

/* TODO list (general) for this module:
 * - add stdio page to darshan-job-summary
 * - figure out what to do about posix module compatibility
 *   - remove stdio counters in POSIX or keep and set to -1?
 *   - affected counters in posix module:
 *     - POSIX_FOPENS
 *     - POSIX_FREADS
 *     - POSIX_FWRITES
 *     - POSIX_FSEEKS
 */

/* catalog of stdio functions instrumented by this module
 *
 * functions for opening streams
 * --------------
 * FILE    *fdopen(int, const char *);                      DONE
 * FILE    *fopen(const char *, const char *);              DONE
 * FILE    *fopen64(const char *, const char *);            DONE
 * FILE    *freopen(const char *, const char *, FILE *);    DONE
 * FILE    *freopen64(const char *, const char *, FILE *);  DONE
 *
 * functions for closing streams
 * --------------
 * int      fclose(FILE *);                                 DONE
 *
 * functions for flushing streams
 * --------------
 * int      fflush(FILE *);                                 DONE
 *
 * functions for reading data
 * --------------
 * int      fgetc(FILE *);                                  DONE
 * char    *fgets(char *, int, FILE *);                     DONE
 * size_t   fread(void *, size_t, size_t, FILE *);          DONE
 * int      fscanf(FILE *, const char *, ...);              DONE
 * int      vfscanf(FILE *, const char *, va_list);         DONE
 * int      getc(FILE *);                                   DONE
 * int      getw(FILE *);                                   DONE
 *
 * functions for writing data
 * --------------
 * int      fprintf(FILE *, const char *, ...);             DONE
 * int      vfprintf(FILE *, const char *, va_list);        DONE
 * int      fputc(int, FILE *);                             DONE
 * int      fputs(const char *, FILE *);                    DONE
 * size_t   fwrite(const void *, size_t, size_t, FILE *);   DONE
 * int      putc(int, FILE *);                              DONE
 * int      putw(int, FILE *);                              DONE
 *
 * functions for changing file position
 * --------------
 * int      fseek(FILE *, long int, int);                   DONE
 * int      fseeko(FILE *, off_t, int);                     DONE
 * int      fseeko64(FILE *, off_t, int);                   DONE
 * int      fsetpos(FILE *, const fpos_t *);                DONE
 * int      fsetpos64(FILE *, const fpos_t *);              DONE
 * void     rewind(FILE *);                                 DONE
 *
 * Omissions: 
 *   - _unlocked() variants of the various flush, read, and write
 *     functions.  There are many of these, but they are not available on all
 *     systems and the man page advises not to use them.
 *   - ungetc()
 */

#define _XOPEN_SOURCE 500
#define _GNU_SOURCE

#include "darshan-runtime-config.h"
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <search.h>
#include <assert.h>
#include <libgen.h>
#include <pthread.h>

#include "darshan.h"
#include "darshan-dynamic.h"

DARSHAN_FORWARD_DECL(fopen, FILE*, (const char *path, const char *mode));
DARSHAN_FORWARD_DECL(fopen64, FILE*, (const char *path, const char *mode));
DARSHAN_FORWARD_DECL(fdopen, FILE*, (int fd, const char *mode));
DARSHAN_FORWARD_DECL(freopen, FILE*, (const char *path, const char *mode, FILE *stream));
DARSHAN_FORWARD_DECL(freopen64, FILE*, (const char *path, const char *mode, FILE *stream));
DARSHAN_FORWARD_DECL(fclose, int, (FILE *fp));
DARSHAN_FORWARD_DECL(fflush, int, (FILE *fp));
DARSHAN_FORWARD_DECL(fwrite, size_t, (const void *ptr, size_t size, size_t nmemb, FILE *stream));
DARSHAN_FORWARD_DECL(fputc, int, (int c, FILE *stream));
DARSHAN_FORWARD_DECL(putw, int, (int w, FILE *stream));
DARSHAN_FORWARD_DECL(fputs, int, (const char *s, FILE *stream));
DARSHAN_FORWARD_DECL(fprintf, int, (FILE *stream, const char *format, ...));
DARSHAN_FORWARD_DECL(vfprintf, int, (FILE *stream, const char *format, va_list));
DARSHAN_FORWARD_DECL(fread, size_t, (void *ptr, size_t size, size_t nmemb, FILE *stream));
DARSHAN_FORWARD_DECL(fgetc, int, (FILE *stream));
DARSHAN_FORWARD_DECL(getw, int, (FILE *stream));
DARSHAN_FORWARD_DECL(_IO_getc, int, (FILE *stream));
DARSHAN_FORWARD_DECL(_IO_putc, int, (int, FILE *stream));
DARSHAN_FORWARD_DECL(fscanf, int, (FILE *stream, const char *format, ...));
DARSHAN_FORWARD_DECL(__isoc99_fscanf, int, (FILE *stream, const char *format, ...));
DARSHAN_FORWARD_DECL(vfscanf, int, (FILE *stream, const char *format, va_list ap));
DARSHAN_FORWARD_DECL(fgets, char*, (char *s, int size, FILE *stream));
DARSHAN_FORWARD_DECL(fseek, int, (FILE *stream, long offset, int whence));
DARSHAN_FORWARD_DECL(fseeko, int, (FILE *stream, off_t offset, int whence));
DARSHAN_FORWARD_DECL(fseeko64, int, (FILE *stream, off_t offset, int whence));
DARSHAN_FORWARD_DECL(fsetpos, int, (FILE *stream, const fpos_t *pos));
DARSHAN_FORWARD_DECL(fsetpos64, int, (FILE *stream, const fpos_t *pos));
DARSHAN_FORWARD_DECL(rewind, void, (FILE *stream));

/* structure to track stdio stats at runtime */
struct stdio_file_record_ref
{
    struct darshan_stdio_file* file_rec;
    int64_t offset;
    double last_meta_end;
    double last_read_end;
    double last_write_end;
};

/* The stdio_runtime structure maintains necessary state for storing
 * STDIO file records and for coordinating with darshan-core at 
 * shutdown time.
 */
struct stdio_runtime
{
    void *rec_id_hash;
    void *stream_hash;
    int file_rec_count;
};

static struct stdio_runtime *stdio_runtime = NULL;
static pthread_mutex_t stdio_runtime_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
static int instrumentation_disabled = 0;
static int darshan_mem_alignment = 1;
static int my_rank = -1;

static void stdio_runtime_initialize(void);
static void stdio_shutdown(
    MPI_Comm mod_comm,
    darshan_record_id *shared_recs,
    int shared_rec_count,
    void **stdio_buf,
    int *stdio_buf_sz);
static void stdio_record_reduction_op(void* infile_v, void* inoutfile_v,
    int *len, MPI_Datatype *datatype);
static struct stdio_file_record_ref *stdio_track_new_file_record(
    darshan_record_id rec_id, const char *path);
static void stdio_cleanup_runtime();

#define STDIO_LOCK() pthread_mutex_lock(&stdio_runtime_mutex)
#define STDIO_UNLOCK() pthread_mutex_unlock(&stdio_runtime_mutex)

#define STDIO_PRE_RECORD() do { \
    STDIO_LOCK(); \
    if(!stdio_runtime && !instrumentation_disabled) stdio_runtime_initialize(); \
    if(!stdio_runtime) { \
        STDIO_UNLOCK(); \
        return(ret); \
    } \
} while(0)

#define STDIO_POST_RECORD() do { \
    STDIO_UNLOCK(); \
} while(0)

#define STDIO_RECORD_OPEN(__ret, __path, __tm1, __tm2) do { \
    darshan_record_id rec_id; \
    struct stdio_file_record_ref* rec_ref; \
    char *newpath; \
    if(__ret == NULL) break; \
    newpath = darshan_clean_file_path(__path); \
    if(!newpath) newpath = (char*)__path; \
    if(darshan_core_excluded_path(newpath)) { \
        if(newpath != (char*)__path) free(newpath); \
        break; \
    } \
    rec_id = darshan_core_gen_record_id(newpath); \
    rec_ref = darshan_lookup_record_ref(stdio_runtime->rec_id_hash, &rec_id, sizeof(rec_id)); \
    if(!rec_ref) rec_ref = stdio_track_new_file_record(rec_id, newpath); \
    if(!rec_ref) { \
        if(newpath != (char*)__path) free(newpath); \
        break; \
    } \
    rec_ref->offset = 0; \
    rec_ref->file_rec->counters[STDIO_OPENS] += 1; \
    if(rec_ref->file_rec->fcounters[STDIO_F_OPEN_START_TIMESTAMP] == 0 || \
     rec_ref->file_rec->fcounters[STDIO_F_OPEN_START_TIMESTAMP] > __tm1) \
        rec_ref->file_rec->fcounters[STDIO_F_OPEN_START_TIMESTAMP] = __tm1; \
    rec_ref->file_rec->fcounters[STDIO_F_OPEN_END_TIMESTAMP] = __tm2; \
    DARSHAN_TIMER_INC_NO_OVERLAP(rec_ref->file_rec->fcounters[STDIO_F_META_TIME], __tm1, __tm2, rec_ref->last_meta_end); \
    darshan_add_record_ref(&(stdio_runtime->stream_hash), &(__ret), sizeof(__ret), rec_ref); \
    if(newpath != (char*)__path) free(newpath); \
} while(0)


#define STDIO_RECORD_READ(__fp, __bytes,  __tm1, __tm2) do{ \
    struct stdio_file_record_ref* rec_ref; \
    int64_t this_offset; \
    rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &(__fp), sizeof(__fp)); \
    if(!rec_ref) break; \
    this_offset = rec_ref->offset; \
    rec_ref->offset = this_offset + __bytes; \
    if(rec_ref->file_rec->counters[STDIO_MAX_BYTE_READ] < (this_offset + __bytes - 1)) \
        rec_ref->file_rec->counters[STDIO_MAX_BYTE_READ] = (this_offset + __bytes - 1); \
    rec_ref->file_rec->counters[STDIO_BYTES_READ] += __bytes; \
    rec_ref->file_rec->counters[STDIO_READS] += 1; \
    if(rec_ref->file_rec->fcounters[STDIO_F_READ_START_TIMESTAMP] == 0 || \
     rec_ref->file_rec->fcounters[STDIO_F_READ_START_TIMESTAMP] > __tm1) \
        rec_ref->file_rec->fcounters[STDIO_F_READ_START_TIMESTAMP] = __tm1; \
    rec_ref->file_rec->fcounters[STDIO_F_READ_END_TIMESTAMP] = __tm2; \
    DARSHAN_TIMER_INC_NO_OVERLAP(rec_ref->file_rec->fcounters[STDIO_F_READ_TIME], __tm1, __tm2, rec_ref->last_write_end); \
} while(0)

#define STDIO_RECORD_WRITE(__fp, __bytes,  __tm1, __tm2, __fflush_flag) do{ \
    struct stdio_file_record_ref* rec_ref; \
    int64_t this_offset; \
    rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &(__fp), sizeof(__fp)); \
    if(!rec_ref) break; \
    this_offset = rec_ref->offset; \
    rec_ref->offset = this_offset + __bytes; \
    if(rec_ref->file_rec->counters[STDIO_MAX_BYTE_WRITTEN] < (this_offset + __bytes - 1)) \
        rec_ref->file_rec->counters[STDIO_MAX_BYTE_WRITTEN] = (this_offset + __bytes - 1); \
    rec_ref->file_rec->counters[STDIO_BYTES_WRITTEN] += __bytes; \
    if(__fflush_flag) \
        rec_ref->file_rec->counters[STDIO_FLUSHES] += 1; \
    else \
        rec_ref->file_rec->counters[STDIO_WRITES] += 1; \
    if(rec_ref->file_rec->fcounters[STDIO_F_WRITE_START_TIMESTAMP] == 0 || \
     rec_ref->file_rec->fcounters[STDIO_F_WRITE_START_TIMESTAMP] > __tm1) \
        rec_ref->file_rec->fcounters[STDIO_F_WRITE_START_TIMESTAMP] = __tm1; \
    rec_ref->file_rec->fcounters[STDIO_F_WRITE_END_TIMESTAMP] = __tm2; \
    DARSHAN_TIMER_INC_NO_OVERLAP(rec_ref->file_rec->fcounters[STDIO_F_WRITE_TIME], __tm1, __tm2, rec_ref->last_write_end); \
} while(0)

FILE* DARSHAN_DECL(fopen)(const char *path, const char *mode)
{
    FILE* ret;
    double tm1, tm2;

    MAP_OR_FAIL(fopen);

    tm1 = darshan_core_wtime();
    ret = __real_fopen(path, mode);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    STDIO_RECORD_OPEN(ret, path, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

FILE* DARSHAN_DECL(fopen64)(const char *path, const char *mode)
{
    FILE* ret;
    double tm1, tm2;

    MAP_OR_FAIL(fopen);

    tm1 = darshan_core_wtime();
    ret = __real_fopen64(path, mode);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    STDIO_RECORD_OPEN(ret, path, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

FILE* DARSHAN_DECL(fdopen)(int fd, const char *mode)
{
    FILE* ret;
    double tm1, tm2;

    MAP_OR_FAIL(fdopen);

    tm1 = darshan_core_wtime();
    ret = __real_fdopen(fd, mode);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    STDIO_RECORD_OPEN(ret, "UNKNOWN-FDOPEN", tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

FILE* DARSHAN_DECL(freopen)(const char *path, const char *mode, FILE *stream)
{
    FILE* ret;
    double tm1, tm2;

    MAP_OR_FAIL(freopen);

    tm1 = darshan_core_wtime();
    ret = __real_freopen(path, mode, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    STDIO_RECORD_OPEN(ret, path, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

FILE* DARSHAN_DECL(freopen64)(const char *path, const char *mode, FILE *stream)
{
    FILE* ret;
    double tm1, tm2;

    MAP_OR_FAIL(freopen64);

    tm1 = darshan_core_wtime();
    ret = __real_freopen64(path, mode, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    STDIO_RECORD_OPEN(ret, path, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}


int DARSHAN_DECL(fflush)(FILE *fp)
{
    double tm1, tm2;
    int ret;

    MAP_OR_FAIL(fflush);

    tm1 = darshan_core_wtime();
    ret = __real_fflush(fp);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret >= 0)
        STDIO_RECORD_WRITE(fp, 0, tm1, tm2, 1);
    STDIO_POST_RECORD();

    return(ret);
}

int DARSHAN_DECL(fclose)(FILE *fp)
{
    double tm1, tm2;
    int ret;
    struct stdio_file_record_ref *rec_ref;

    MAP_OR_FAIL(fclose);

    tm1 = darshan_core_wtime();
    ret = __real_fclose(fp);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &fp, sizeof(fp));
    if(rec_ref)
    {
        if(rec_ref->file_rec->fcounters[STDIO_F_CLOSE_START_TIMESTAMP] == 0 ||
         rec_ref->file_rec->fcounters[STDIO_F_CLOSE_START_TIMESTAMP] > tm1)
           rec_ref->file_rec->fcounters[STDIO_F_CLOSE_START_TIMESTAMP] = tm1;
        rec_ref->file_rec->fcounters[STDIO_F_CLOSE_END_TIMESTAMP] = tm2;
        DARSHAN_TIMER_INC_NO_OVERLAP(
            rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
            tm1, tm2, rec_ref->last_meta_end);
        darshan_delete_record_ref(&(stdio_runtime->stream_hash), &fp, sizeof(fp));
    }
    STDIO_POST_RECORD();

    return(ret);
}

size_t DARSHAN_DECL(fwrite)(const void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t ret;
    double tm1, tm2;

    MAP_OR_FAIL(fwrite);

    tm1 = darshan_core_wtime();
    ret = __real_fwrite(ptr, size, nmemb, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret > 0)
        STDIO_RECORD_WRITE(stream, size*ret, tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}


int DARSHAN_DECL(fputc)(int c, FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(fputc);

    tm1 = darshan_core_wtime();
    ret = __real_fputc(c, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF)
        STDIO_RECORD_WRITE(stream, 1, tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}

int DARSHAN_DECL(putw)(int w, FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(putw);

    tm1 = darshan_core_wtime();
    ret = __real_putw(w, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF)
        STDIO_RECORD_WRITE(stream, sizeof(int), tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}



int DARSHAN_DECL(fputs)(const char *s, FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(fputs);

    tm1 = darshan_core_wtime();
    ret = __real_fputs(s, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF && ret > 0)
        STDIO_RECORD_WRITE(stream, strlen(s), tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}

int DARSHAN_DECL(vfprintf)(FILE *stream, const char *format, va_list ap)
{
    int ret;
    double tm1, tm2;
    long start_off, end_off;

    MAP_OR_FAIL(vfprintf);

    tm1 = darshan_core_wtime();
    start_off = ftell(stream);
    ret = __real_vfprintf(stream, format, ap);
    end_off = ftell(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret > 0)
        STDIO_RECORD_WRITE(stream, (end_off-start_off), tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}


int DARSHAN_DECL(fprintf)(FILE *stream, const char *format, ...)
{
    int ret;
    double tm1, tm2;
    va_list ap;
    long start_off, end_off;

    MAP_OR_FAIL(vfprintf);

    tm1 = darshan_core_wtime();
    /* NOTE: we intentionally switch to vfprintf here to handle the variable
     * length arguments.
     */
    start_off = ftell(stream);
    va_start(ap, format);
    ret = __real_vfprintf(stream, format, ap);
    va_end(ap);
    end_off = ftell(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret > 0)
        STDIO_RECORD_WRITE(stream, (end_off-start_off), tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}

size_t DARSHAN_DECL(fread)(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
    size_t ret;
    double tm1, tm2;

    MAP_OR_FAIL(fread);

    tm1 = darshan_core_wtime();
    ret = __real_fread(ptr, size, nmemb, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret > 0)
        STDIO_RECORD_READ(stream, size*ret, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

size_t DARSHAN_DECL(fgetc)(FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(fgetc);

    tm1 = darshan_core_wtime();
    ret = __real_fgetc(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF)
        STDIO_RECORD_READ(stream, 1, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

/* NOTE: stdio.h typically implements getc() as a macro pointing to _IO_getc */
size_t DARSHAN_DECL(_IO_getc)(FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(_IO_getc);

    tm1 = darshan_core_wtime();
    ret = __real__IO_getc(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF)
        STDIO_RECORD_READ(stream, 1, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

/* NOTE: stdio.h typically implements putc() as a macro pointing to _IO_putc */
size_t DARSHAN_DECL(_IO_putc)(int c, FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(_IO_putc);

    tm1 = darshan_core_wtime();
    ret = __real__IO_putc(c, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF)
        STDIO_RECORD_WRITE(stream, 1, tm1, tm2, 0);
    STDIO_POST_RECORD();

    return(ret);
}
size_t DARSHAN_DECL(getw)(FILE *stream)
{
    int ret;
    double tm1, tm2;

    MAP_OR_FAIL(getw);

    tm1 = darshan_core_wtime();
    ret = __real_getw(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != EOF || ferror(stream) == 0)
        STDIO_RECORD_READ(stream, sizeof(int), tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

/* NOTE: some glibc versions use __isoc99_fscanf as the underlying symbol
 * rather than fscanf
 */
int DARSHAN_DECL(__isoc99_fscanf)(FILE *stream, const char *format, ...)
{
    int ret;
    double tm1, tm2;
    va_list ap;
    long start_off, end_off;

    MAP_OR_FAIL(vfscanf);

    tm1 = darshan_core_wtime();
    /* NOTE: we intentionally switch to vfscanf here to handle the variable
     * length arguments.
     */
    start_off = ftell(stream);
    va_start(ap, format);
    ret = __real_vfscanf(stream, format, ap);
    va_end(ap);
    end_off = ftell(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != 0)
        STDIO_RECORD_READ(stream, (end_off-start_off), tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}


int DARSHAN_DECL(fscanf)(FILE *stream, const char *format, ...)
{
    int ret;
    double tm1, tm2;
    va_list ap;
    long start_off, end_off;

    MAP_OR_FAIL(vfscanf);

    tm1 = darshan_core_wtime();
    /* NOTE: we intentionally switch to vfscanf here to handle the variable
     * length arguments.
     */
    start_off = ftell(stream);
    va_start(ap, format);
    ret = __real_vfscanf(stream, format, ap);
    va_end(ap);
    end_off = ftell(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != 0)
        STDIO_RECORD_READ(stream, (end_off-start_off), tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}

int DARSHAN_DECL(vfscanf)(FILE *stream, const char *format, va_list ap)
{
    int ret;
    double tm1, tm2;
    long start_off, end_off;

    MAP_OR_FAIL(vfscanf);

    tm1 = darshan_core_wtime();
    start_off = ftell(stream);
    ret = __real_vfscanf(stream, format, ap);
    end_off = ftell(stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != 0)
        STDIO_RECORD_READ(stream, end_off-start_off, tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}


char* DARSHAN_DECL(fgets)(char *s, int size, FILE *stream)
{
    char *ret;
    double tm1, tm2;

    MAP_OR_FAIL(fgets);

    tm1 = darshan_core_wtime();
    ret = __real_fgets(s, size, stream);
    tm2 = darshan_core_wtime();

    STDIO_PRE_RECORD();
    if(ret != NULL)
        STDIO_RECORD_READ(stream, strlen(ret), tm1, tm2);
    STDIO_POST_RECORD();

    return(ret);
}


void DARSHAN_DECL(rewind)(FILE *stream)
{
    double tm1, tm2;
    struct stdio_file_record_ref *rec_ref;

    MAP_OR_FAIL(rewind);

    tm1 = darshan_core_wtime();
    __real_rewind(stream);
    tm2 = darshan_core_wtime();

    /* NOTE: we don't use STDIO_PRE_RECORD here because there is no return
     * value in this wrapper.
     */
    STDIO_LOCK();
    if(!stdio_runtime && !instrumentation_disabled) stdio_runtime_initialize();
    if(!stdio_runtime) {
        STDIO_UNLOCK();
        return;
    }

    rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &stream, sizeof(stream));

    if(rec_ref)
    {
        rec_ref->offset = 0;
        DARSHAN_TIMER_INC_NO_OVERLAP(
            rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
            tm1, tm2, rec_ref->last_meta_end);
        rec_ref->file_rec->counters[STDIO_SEEKS] += 1;
    }
    STDIO_POST_RECORD();

    return;
}

int DARSHAN_DECL(fseek)(FILE *stream, long offset, int whence)
{
    int ret;
    struct stdio_file_record_ref *rec_ref;
    double tm1, tm2;

    MAP_OR_FAIL(fseek);

    tm1 = darshan_core_wtime();
    ret = __real_fseek(stream, offset, whence);
    tm2 = darshan_core_wtime();

    if(ret >= 0)
    {
        STDIO_PRE_RECORD();
        rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &stream, sizeof(stream));
        if(rec_ref)
        {
            rec_ref->offset = ftell(stream);
            DARSHAN_TIMER_INC_NO_OVERLAP(
                rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
                tm1, tm2, rec_ref->last_meta_end);
            rec_ref->file_rec->counters[STDIO_SEEKS] += 1;
        }
        STDIO_POST_RECORD();
    }

    return(ret);
}

int DARSHAN_DECL(fseeko)(FILE *stream, off_t offset, int whence)
{
    int ret;
    struct stdio_file_record_ref *rec_ref;
    double tm1, tm2;

    MAP_OR_FAIL(fseeko);

    tm1 = darshan_core_wtime();
    ret = __real_fseeko(stream, offset, whence);
    tm2 = darshan_core_wtime();

    if(ret >= 0)
    {
        STDIO_PRE_RECORD();
        rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &stream, sizeof(stream));
        if(rec_ref)
        {
            rec_ref->offset = ftell(stream);
            DARSHAN_TIMER_INC_NO_OVERLAP(
                rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
                tm1, tm2, rec_ref->last_meta_end);
            rec_ref->file_rec->counters[STDIO_SEEKS] += 1;
        }
        STDIO_POST_RECORD();
    }

    return(ret);
}

int DARSHAN_DECL(fseeko64)(FILE *stream, off_t offset, int whence)
{
    int ret;
    struct stdio_file_record_ref *rec_ref;
    double tm1, tm2;

    MAP_OR_FAIL(fseeko64);

    tm1 = darshan_core_wtime();
    ret = __real_fseeko64(stream, offset, whence);
    tm2 = darshan_core_wtime();

    if(ret >= 0)
    {
        STDIO_PRE_RECORD();
        rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &stream, sizeof(stream));
        if(rec_ref)
        {
            rec_ref->offset = ftell(stream);
            DARSHAN_TIMER_INC_NO_OVERLAP(
                rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
                tm1, tm2, rec_ref->last_meta_end);
            rec_ref->file_rec->counters[STDIO_SEEKS] += 1;
        }
        STDIO_POST_RECORD();
    }

    return(ret);
}

int DARSHAN_DECL(fsetpos)(FILE *stream, const fpos_t *pos)
{
    int ret;
    struct stdio_file_record_ref *rec_ref;
    double tm1, tm2;

    MAP_OR_FAIL(fsetpos);

    tm1 = darshan_core_wtime();
    ret = __real_fsetpos(stream, pos);
    tm2 = darshan_core_wtime();

    if(ret >= 0)
    {
        STDIO_PRE_RECORD();
        rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &stream, sizeof(stream));
        if(rec_ref)
        {
            rec_ref->offset = ftell(stream);
            DARSHAN_TIMER_INC_NO_OVERLAP(
                rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
                tm1, tm2, rec_ref->last_meta_end);
            rec_ref->file_rec->counters[STDIO_SEEKS] += 1;
        }
        STDIO_POST_RECORD();
    }

    return(ret);
}

int DARSHAN_DECL(fsetpos64)(FILE *stream, const fpos_t *pos)
{
    int ret;
    struct stdio_file_record_ref *rec_ref;
    double tm1, tm2;

    MAP_OR_FAIL(fsetpos64);

    tm1 = darshan_core_wtime();
    ret = __real_fsetpos64(stream, pos);
    tm2 = darshan_core_wtime();

    if(ret >= 0)
    {
        STDIO_PRE_RECORD();
        rec_ref = darshan_lookup_record_ref(stdio_runtime->stream_hash, &stream, sizeof(stream));
        if(rec_ref)
        {
            rec_ref->offset = ftell(stream);
            DARSHAN_TIMER_INC_NO_OVERLAP(
                rec_ref->file_rec->fcounters[STDIO_F_META_TIME],
                tm1, tm2, rec_ref->last_meta_end);
            rec_ref->file_rec->counters[STDIO_SEEKS] += 1;
        }
        STDIO_POST_RECORD();
    }

    return(ret);
}


/**********************************************************
 * Internal functions for manipulating STDIO module state *
 **********************************************************/

/* initialize internal STDIO module data structures and register with darshan-core */
static void stdio_runtime_initialize()
{
    int stdio_buf_size;

    /* try to store default number of records for this module */
    stdio_buf_size = DARSHAN_DEF_MOD_REC_COUNT * sizeof(struct darshan_stdio_file);

    /* don't do anything if already initialized or instrumenation is disabled */
    if(stdio_runtime || instrumentation_disabled)
        return;

    /* register the stdio module with darshan core */
    darshan_core_register_module(
        DARSHAN_STDIO_MOD,
        &stdio_shutdown,
        &stdio_buf_size,
        &my_rank,
        &darshan_mem_alignment);

    /* return if darshan-core does not provide enough module memory */
    if(stdio_buf_size < sizeof(struct darshan_stdio_file))
    {
        darshan_core_unregister_module(DARSHAN_POSIX_MOD);
        return;
    }

    stdio_runtime = malloc(sizeof(*stdio_runtime));
    if(!stdio_runtime)
    {
        darshan_core_unregister_module(DARSHAN_STDIO_MOD);
        return;
    }
    memset(stdio_runtime, 0, sizeof(*stdio_runtime));
}

/************************************************************************
 * Functions exported by this module for coordinating with darshan-core *
 ************************************************************************/

static void stdio_record_reduction_op(void* infile_v, void* inoutfile_v,
    int *len, MPI_Datatype *datatype)
{
    struct darshan_stdio_file tmp_file;
    struct darshan_stdio_file *infile = infile_v;
    struct darshan_stdio_file *inoutfile = inoutfile_v;
    int i, j;

    assert(stdio_runtime);

    for(i=0; i<*len; i++)
    {
        memset(&tmp_file, 0, sizeof(struct darshan_stdio_file));
        tmp_file.base_rec.id = infile->base_rec.id;
        tmp_file.base_rec.rank = -1;

        /* sum */
        for(j=STDIO_OPENS; j<=STDIO_BYTES_READ; j++)
        {
            tmp_file.counters[j] = infile->counters[j] + inoutfile->counters[j];
        }
        
        /* max */
        for(j=STDIO_MAX_BYTE_READ; j<=STDIO_MAX_BYTE_WRITTEN; j++)
        {
            if(infile->counters[j] > inoutfile->counters[j])
                tmp_file.counters[j] = infile->counters[j];
            else
                tmp_file.counters[j] = inoutfile->counters[j];
        }

        /* sum */
        for(j=STDIO_F_META_TIME; j<=STDIO_F_READ_TIME; j++)
        {
            tmp_file.fcounters[j] = infile->fcounters[j] + inoutfile->fcounters[j];
        }

        /* min non-zero (if available) value */
        for(j=STDIO_F_OPEN_START_TIMESTAMP; j<=STDIO_F_READ_START_TIMESTAMP; j++)
        {
            if((infile->fcounters[j] < inoutfile->fcounters[j] &&
               infile->fcounters[j] > 0) || inoutfile->fcounters[j] == 0) 
                tmp_file.fcounters[j] = infile->fcounters[j];
            else
                tmp_file.fcounters[j] = inoutfile->fcounters[j];
        }

        /* max */
        for(j=STDIO_F_OPEN_END_TIMESTAMP; j<=STDIO_F_READ_END_TIMESTAMP; j++)
        {
            if(infile->fcounters[j] > inoutfile->fcounters[j])
                tmp_file.fcounters[j] = infile->fcounters[j];
            else
                tmp_file.fcounters[j] = inoutfile->fcounters[j];
        }

        /* update pointers */
        *inoutfile = tmp_file;
        inoutfile++;
        infile++;
    }

    return;
}

static void stdio_shutdown(
    MPI_Comm mod_comm,
    darshan_record_id *shared_recs,
    int shared_rec_count,
    void **stdio_buf,
    int *stdio_buf_sz)
{
    struct stdio_file_record_ref *rec_ref;
    struct darshan_stdio_file *stdio_rec_buf = *(struct darshan_stdio_file **)stdio_buf;
    int i;
    struct darshan_stdio_file *red_send_buf = NULL;
    struct darshan_stdio_file *red_recv_buf = NULL;
    MPI_Datatype red_type;
    MPI_Op red_op;
    int stdio_rec_count;

    STDIO_LOCK();
    assert(stdio_runtime);
    stdio_rec_count = stdio_runtime->file_rec_count;

    /* if there are globally shared files, do a shared file reduction */
    /* NOTE: the shared file reduction is also skipped if the 
     * DARSHAN_DISABLE_SHARED_REDUCTION environment variable is set.
     */
    if(shared_rec_count && !getenv("DARSHAN_DISABLE_SHARED_REDUCTION"))
    {
        /* necessary initialization of shared records */
        for(i = 0; i < shared_rec_count; i++)
        {
            rec_ref = darshan_lookup_record_ref(stdio_runtime->rec_id_hash,
                &shared_recs[i], sizeof(darshan_record_id));
            assert(rec_ref);

            rec_ref->file_rec->base_rec.rank = -1;
        }

        /* sort the array of files descending by rank so that we get all of the 
         * shared files (marked by rank -1) in a contiguous portion at end 
         * of the array
         */
        darshan_record_sort(stdio_rec_buf, stdio_rec_count, sizeof(struct darshan_stdio_file));

        /* make *send_buf point to the shared files at the end of sorted array */
        red_send_buf = &(stdio_rec_buf[stdio_rec_count-shared_rec_count]);

        /* allocate memory for the reduction output on rank 0 */
        if(my_rank == 0)
        {
            red_recv_buf = malloc(shared_rec_count * sizeof(struct darshan_stdio_file));
            if(!red_recv_buf)
            {
                return;
            }
        }

        /* construct a datatype for a STDIO file record.  This is serving no purpose
         * except to make sure we can do a reduction on proper boundaries
         */
        DARSHAN_MPI_CALL(PMPI_Type_contiguous)(sizeof(struct darshan_stdio_file),
            MPI_BYTE, &red_type);
        DARSHAN_MPI_CALL(PMPI_Type_commit)(&red_type);

        /* register a STDIO file record reduction operator */
        DARSHAN_MPI_CALL(PMPI_Op_create)(stdio_record_reduction_op, 1, &red_op);

        /* reduce shared STDIO file records */
        DARSHAN_MPI_CALL(PMPI_Reduce)(red_send_buf, red_recv_buf,
            shared_rec_count, red_type, red_op, 0, mod_comm);

        /* clean up reduction state */
        if(my_rank == 0)
        {
            int tmp_ndx = stdio_rec_count - shared_rec_count;
            memcpy(&(stdio_rec_buf[tmp_ndx]), red_recv_buf,
                shared_rec_count * sizeof(struct darshan_stdio_file));
            free(red_recv_buf);
        }
        else
        {
            stdio_rec_count -= shared_rec_count;
        }

        DARSHAN_MPI_CALL(PMPI_Type_free)(&red_type);
        DARSHAN_MPI_CALL(PMPI_Op_free)(&red_op);
    }

    /* update output buffer size to account for shared file reduction */
    *stdio_buf_sz = stdio_rec_count * sizeof(struct darshan_stdio_file);

    /* shutdown internal structures used for instrumenting */
    stdio_cleanup_runtime();

    /* disable further instrumentation */
    instrumentation_disabled = 1;

    STDIO_UNLOCK();
    
    return;
}

static struct stdio_file_record_ref *stdio_track_new_file_record(
    darshan_record_id rec_id, const char *path)
{
    struct darshan_stdio_file *file_rec = NULL;
    struct stdio_file_record_ref *rec_ref = NULL;
    int ret;

    rec_ref = malloc(sizeof(*rec_ref));
    if(!rec_ref)
        return(NULL);
    memset(rec_ref, 0, sizeof(*rec_ref));

    /* add a reference to this file record based on record id */
    ret = darshan_add_record_ref(&(stdio_runtime->rec_id_hash), &rec_id,
        sizeof(darshan_record_id), rec_ref);
    if(ret == 0)
    {
        free(rec_ref);
        return(NULL);
    }

    /* register the actual file record with darshan-core so it is persisted
     * in the log file
     */
    file_rec = darshan_core_register_record(
        rec_id,
        path,
        DARSHAN_STDIO_MOD,
        sizeof(struct darshan_stdio_file),
        NULL);

    if(!file_rec)
    {
        darshan_delete_record_ref(&(stdio_runtime->rec_id_hash),
            &rec_id, sizeof(darshan_record_id));
        free(rec_ref);
        return(NULL);
    }

    /* registering this file record was successful, so initialize some fields */
    file_rec->base_rec.id = rec_id;
    file_rec->base_rec.rank = my_rank;
    rec_ref->file_rec = file_rec;
    stdio_runtime->file_rec_count++;

    return(rec_ref);

}

static void stdio_cleanup_runtime()
{
    darshan_clear_record_refs(&(stdio_runtime->stream_hash), 0);
    darshan_clear_record_refs(&(stdio_runtime->rec_id_hash), 1);

    free(stdio_runtime);
    stdio_runtime = NULL;

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