/*
 * Copyright (c) 2018  Citrix Systems Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_BLOCK_DATAPLANE_XEN_BLOCK_H
#define HW_BLOCK_DATAPLANE_XEN_BLOCK_H

#include "hw/block/block.h"
#include "hw/xen/xen-bus.h"
#include "sysemu/iothread.h"

typedef struct XenBlockDataPlane XenBlockDataPlane;

XenBlockDataPlane *xen_block_dataplane_create(XenDevice *xendev,
                                              BlockBackend *blk,
                                              unsigned int sector_size,
                                              IOThread *iothread);
void xen_block_dataplane_destroy(XenBlockDataPlane *dataplane);
void xen_block_dataplane_start(XenBlockDataPlane *dataplane,
                               const unsigned int ring_ref[],
                               unsigned int nr_ring_ref,
                               unsigned int event_channel,
                               unsigned int protocol,
                               Error **errp);
void xen_block_dataplane_stop(XenBlockDataPlane *dataplane);

#endif /* HW_BLOCK_DATAPLANE_XEN_BLOCK_H */
