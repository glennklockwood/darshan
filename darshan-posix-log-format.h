/*
 *  (C) 2009 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef __DARSHAN_POSIX_LOG_FORMAT_H
#define __DARSHAN_POSIX_LOG_FORMAT_H

#include "darshan-log-format.h"

/* integer statistics for POSIX file records */
enum darshan_posix_indices
{
    POSIX_OPENS,              /* count of posix opens */
    POSIX_READS,              /* count of posix reads */
    POSIX_WRITES,             /* count of posix writes */
    POSIX_SEEKS,              /* count of posix seeks */
    POSIX_STATS,              /* count of posix stat/lstat/fstats */
    POSIX_MMAPS,              /* count of posix mmaps */
    POSIX_FOPENS,             /* count of posix fopens */
    POSIX_FREADS,             /* count of posix freads */
    POSIX_FWRITES,            /* count of posix fwrites */
    POSIX_FSEEKS,             /* count of posix fseeks */
    POSIX_FSYNCS,             /* count of posix fsyncs */
    POSIX_FDSYNCS,            /* count of posix fdatasyncs */
    POSIX_MODE,               /* mode of file */
    POSIX_BYTES_READ,         /* total bytes read */
    POSIX_BYTES_WRITTEN,      /* total bytes written */
    POSIX_MAX_BYTE_READ,      /* highest offset byte read */
    POSIX_MAX_BYTE_WRITTEN,   /* highest offset byte written */
    POSIX_CONSEC_READS,       /* count of consecutive reads */
    POSIX_CONSEC_WRITES,      /* count of consecutive writes */ 
    POSIX_SEQ_READS,          /* count of sequential reads */
    POSIX_SEQ_WRITES,         /* count of sequential writes */
    POSIX_RW_SWITCHES,        /* number of times switched between read and write */
    POSIX_MEM_NOT_ALIGNED,    /* count of accesses not mem aligned */
    POSIX_MEM_ALIGNMENT,      /* mem alignment in bytes */
    POSIX_FILE_NOT_ALIGNED,   /* count of accesses not file aligned */
    POSIX_FILE_ALIGNMENT,     /* file alignment in bytes */
    POSIX_MAX_READ_TIME_SIZE,
    POSIX_MAX_WRITE_TIME_SIZE,
    /* buckets */
    POSIX_SIZE_READ_0_100,    /* count of posix read size ranges */
    POSIX_SIZE_READ_100_1K,
    POSIX_SIZE_READ_1K_10K,
    POSIX_SIZE_READ_10K_100K,
    POSIX_SIZE_READ_100K_1M,
    POSIX_SIZE_READ_1M_4M,
    POSIX_SIZE_READ_4M_10M,
    POSIX_SIZE_READ_10M_100M,
    POSIX_SIZE_READ_100M_1G,
    POSIX_SIZE_READ_1G_PLUS,
    /* buckets */
    POSIX_SIZE_WRITE_0_100,   /* count of posix write size ranges */
    POSIX_SIZE_WRITE_100_1K,
    POSIX_SIZE_WRITE_1K_10K,
    POSIX_SIZE_WRITE_10K_100K,
    POSIX_SIZE_WRITE_100K_1M,
    POSIX_SIZE_WRITE_1M_4M,
    POSIX_SIZE_WRITE_4M_10M,
    POSIX_SIZE_WRITE_10M_100M,
    POSIX_SIZE_WRITE_100M_1G,
    POSIX_SIZE_WRITE_1G_PLUS,
    /* stride/access counters */
    POSIX_STRIDE1_STRIDE,     /* the four most frequently appearing strides */
    POSIX_STRIDE2_STRIDE,
    POSIX_STRIDE3_STRIDE,
    POSIX_STRIDE4_STRIDE,
    POSIX_STRIDE1_COUNT,      /* count of each of the most frequent strides */
    POSIX_STRIDE2_COUNT,
    POSIX_STRIDE3_COUNT,
    POSIX_STRIDE4_COUNT,
    POSIX_ACCESS1_ACCESS,     /* the four most frequently appearing access sizes */
    POSIX_ACCESS2_ACCESS,
    POSIX_ACCESS3_ACCESS,
    POSIX_ACCESS4_ACCESS,
    POSIX_ACCESS1_COUNT,      /* count of each of the most frequent access sizes */
    POSIX_ACCESS2_COUNT,
    POSIX_ACCESS3_COUNT,
    POSIX_ACCESS4_COUNT,
    POSIX_FASTEST_RANK,
    POSIX_FASTEST_RANK_BYTES,
    POSIX_SLOWEST_RANK,
    POSIX_SLOWEST_RANK_BYTES,

    POSIX_NUM_INDICES,
};

/* floating point statistics for POSIX file records */
enum darshan_posix_f_indices
{
    POSIX_F_OPEN_TIMESTAMP = 0,    /* timestamp of first open */
    POSIX_F_READ_START_TIMESTAMP,  /* timestamp of first read */
    POSIX_F_WRITE_START_TIMESTAMP, /* timestamp of first write */
    POSIX_F_READ_END_TIMESTAMP,    /* timestamp of last read */
    POSIX_F_WRITE_END_TIMESTAMP,   /* timestamp of last write */
    POSIX_F_CLOSE_TIMESTAMP,       /* timestamp of last close */
    POSIX_F_READ_TIME,             /* cumulative posix read time */
    POSIX_F_WRITE_TIME,            /* cumulative posix write time */
    POSIX_F_META_TIME,             /* cumulative posix meta time */
    POSIX_F_MAX_READ_TIME,
    POSIX_F_MAX_WRITE_TIME,
    /* Total I/O and meta time consumed by fastest and slowest ranks */ 
    POSIX_F_FASTEST_RANK_TIME,
    POSIX_F_SLOWEST_RANK_TIME,
#if 0
    F_VARIANCE_RANK_TIME,
    F_VARIANCE_RANK_BYTES,
#endif

    POSIX_F_NUM_INDICES,
};

/* file record structure for POSIX files. a record is created and stored for
 * every POSIX file opened by the original application. For the POSIX module,
 * the record includes:
 *      - a corresponding record identifier (created by hashing the file path)
 *      - the rank of the process which opened the file (-1 for shared files)
 *      - integer file I/O statistics (open, read/write counts, etc)
 *      - floating point I/O statistics (timestamps, cumulative timers, etc.)
 */
struct darshan_posix_file
{
    darshan_record_id f_id;
    int64_t rank;
    int64_t counters[POSIX_NUM_INDICES];
    double fcounters[POSIX_F_NUM_INDICES];
};

#endif /* __DARSHAN_POSIX_LOG_FORMAT_H */
