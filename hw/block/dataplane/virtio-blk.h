/*
 * Dedicated thread for virtio-blk I/O processing
 *
 * Copyright 2012 IBM, Corp.
 * Copyright 2012 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Stefan Hajnoczi <stefanha@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef HW_DATAPLANE_VIRTIO_BLK_H
#define HW_DATAPLANE_VIRTIO_BLK_H

#include "hw/virtio/virtio.h"

typedef struct VirtIOBlockDataPlane VirtIOBlockDataPlane;

void virtio_blk_data_plane_create(VirtIODevice *vdev, VirtIOBlkConf *conf,
                                  VirtIOBlockDataPlane **dataplane,
                                  Error **errp);
void virtio_blk_data_plane_destroy(VirtIOBlockDataPlane *s);
void virtio_blk_data_plane_start(VirtIOBlockDataPlane *s);
void virtio_blk_data_plane_stop(VirtIOBlockDataPlane *s);
void virtio_blk_data_plane_drain(VirtIOBlockDataPlane *s);
void virtio_blk_data_plane_notify(VirtIOBlockDataPlane *s);

#endif /* HW_DATAPLANE_VIRTIO_BLK_H */
