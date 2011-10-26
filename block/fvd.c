/*
 * Copyright (c) 2010-2011 IBM
 *
 * Authors:
 *         Chunqiang Tang <ctang@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 * See the COPYING file in the top-level directory.
 */

/*=============================================================================
 *  A short description: this module implements the QEMU block device driver
 *  for the Fast Virtual Disk (FVD) format.  See the following companion
 *  papers for a detailed description of FVD:
 *  1. The so-called "FVD-cow paper":
 *          "FVD: a High-Performance Virtual Machine Image Format for Cloud",
 *      by Chunqiang Tang, 2010.
 *  2. The so-called "FVD-compact paper":
 *          "FVD: a High-Performance Virtual Machine Image Format for Cloud
 *           with Sparse Image Capability", by Chunqiang Tang, 2010.
 *============================================================================*/

#include "block/fvd.h"

//#define ENABLE_TRACE_IO
//#define DEBUG_MEMORY_LEAK
//#define SIMULATED_TEST_WITH_QEMU_IO

#ifndef FVD_DEBUG
#undef DEBUG_MEMORY_LEAK
#undef ENABLE_TRACE_IO
#undef SIMULATED_TEST_WITH_QEMU_IO
#endif

/* Use include to avoid exposing too many FVD symbols, and to allow inline
 * function optimization. */
#include "block/fvd-utils.c"
#include "block/fvd-debug.c"
#include "block/fvd-misc.c"
#include "block/fvd-create.c"
#include "block/fvd-open.c"
#include "block/fvd-read.c"
#include "block/fvd-write.c"
#include "block/fvd-load.c"
#include "block/fvd-store.c"
#include "block/fvd-journal.c"
#include "block/fvd-prefetch.c"

static AIOPool fvd_aio_pool = {
    .aiocb_size = sizeof (FvdAIOCB),
    .cancel = fvd_aio_cancel,
};

static BlockDriver bdrv_fvd = {
    .format_name = "fvd",
    .instance_size = sizeof (BDRVFvdState),
    .bdrv_create = fvd_create,
    .bdrv_probe = fvd_probe,
    .bdrv_file_open = fvd_open,
    .bdrv_close = fvd_close,
    .bdrv_is_allocated = fvd_is_allocated,
    .bdrv_co_flush = fvd_flush,
    .bdrv_aio_readv = fvd_aio_readv,
    .bdrv_aio_writev = fvd_aio_writev,
    .bdrv_aio_flush = fvd_aio_flush,
    .create_options = fvd_create_options,
    .bdrv_get_info = fvd_get_info,
    .bdrv_update = fvd_update,
    .bdrv_has_zero_init = fvd_has_zero_init
};

static void bdrv_fvd_init (void)
{
    bdrv_register (&bdrv_fvd);
}

block_init (bdrv_fvd_init);

/*
 * Since bdrv_close may not be properly invoked on a VM shutdown, we
 * use a destructor to flush metadata to disk. This only affects
 * performance and does not affect correctness.
 * See Section 3.3.4 of the FVD-cow paper for the rationale.
 */
extern QTAILQ_HEAD (, BlockDriverState) bdrv_states;
static void __attribute__ ((destructor)) flush_fvd_bitmap_to_disk (void)
{
    BlockDriverState *bs;
    QTAILQ_FOREACH (bs, &bdrv_states, list) {
        if (bs->drv == &bdrv_fvd) {
            flush_metadata_to_disk_on_exit (bs);

#ifdef FVD_DEBUG
            dump_resource_summary (bs->opaque);
#endif
        }
    }
}

/*
 * TODOs: Below are some potential enhancements for future development:
 * 1. Handle storage leak on failure.
 *
 * 2. Profile-directed prefetch. See Section 3.4.1 of the FVD-cow paper.
 * Related metadata are FvdHeader.prefetch_profile_offset and
 * FvdHeader.prefetch_profile_entries,
 * FvdHeader.profile_directed_prefetch_start_delay,
 * FvdHeader.generate_prefetch_profile.
 *
 * 3.  Cap the prefetch throughput at the upper limit. See Section 3.4.2 of
 * the FVD-cow paper.  Related metadata are
 * FvdHeader.prefetch_max_read_throughput and
 * FvdHeader.prefetch_max_write_throughput.
 *
 * 4. Support write through to the base image. When a VM issues a write
 * request, in addition to saving the data in the FVD data file, also save the
 * data in the base image if the address of write request is not beyond the
 * size of the base image (this of course requires the base image NOT to be
 * 'read_only'. This feature changes the semantics of copy-on-write, but it
 * suits a different use case, where the base image is stored on a remote
 * storage server, and the FVD image is stored on a local disk and acts as a
 * write-through cache of the base image. This can be used to cache and
 * improve the performance of persistent storage on network-attached storage,
 * e.g., Amazon EBS.  This feature is not described in the FVD-cow paper as it
 * would complicate the discussion.  Related metadata are
 * FvdHeader.write_updates_base_img.
 */
