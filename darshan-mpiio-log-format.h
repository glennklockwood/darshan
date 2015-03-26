/*
 *  (C) 2015 by Argonne National Laboratory.
 *      See COPYRIGHT in top-level directory.
 */

#ifndef __DARSHAN_MPIIO_LOG_FORMAT_H
#define __DARSHAN_MPIIO_LOG_FORMAT_H

#include "darshan-log-format.h"

enum darshan_mpiio_indices
{
    DARSHAN_MPIIO_INDEP_OPENS,   /* independent opens */
    DARSHAN_MPIIO_COLL_OPENS,    /* collective opens */

    DARSHAN_MPIIO_NUM_INDICES,
};

struct darshan_mpiio_file
{
    darshan_record_id f_id;
    int64_t rank;
    int64_t counters[DARSHAN_MPIIO_NUM_INDICES];
};

#endif /* __DARSHAN_MPIIO_LOG_FORMAT_H */