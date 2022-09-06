/*
 * Virtio Block Device common helpers
 *
 * Copyright IBM, Corp. 2007
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "standard-headers/linux/virtio_blk.h"
#include "hw/virtio/virtio.h"
#include "hw/virtio/virtio-blk-common.h"

/* Config size before the discard support (hide associated config fields) */
#define VIRTIO_BLK_CFG_SIZE offsetof(struct virtio_blk_config, \
                                     max_discard_sectors)

/*
 * Starting from the discard feature, we can use this array to properly
 * set the config size depending on the features enabled.
 */
static const VirtIOFeature feature_sizes[] = {
    {.flags = 1ULL << VIRTIO_BLK_F_DISCARD,
     .end = endof(struct virtio_blk_config, discard_sector_alignment)},
    {.flags = 1ULL << VIRTIO_BLK_F_WRITE_ZEROES,
     .end = endof(struct virtio_blk_config, write_zeroes_may_unmap)},
    {}
};

const VirtIOConfigSizeParams virtio_blk_cfg_size_params = {
    .min_size = VIRTIO_BLK_CFG_SIZE,
    .max_size = sizeof(struct virtio_blk_config),
    .feature_sizes = feature_sizes
};
