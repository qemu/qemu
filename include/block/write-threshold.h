/*
 * QEMU System Emulator block write threshold notification
 *
 * Copyright Red Hat, Inc. 2014
 *
 * Authors:
 *  Francesco Romani <fromani@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2 or later.
 * See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_WRITE_THRESHOLD_H
#define BLOCK_WRITE_THRESHOLD_H

/*
 * bdrv_write_threshold_set:
 *
 * Set the write threshold for block devices, in bytes.
 * Notify when a write exceeds the threshold, meaning the device
 * is becoming full, so it can be transparently resized.
 * To be used with thin-provisioned block devices.
 *
 * Use threshold_bytes == 0 to disable.
 */
void bdrv_write_threshold_set(BlockDriverState *bs, uint64_t threshold_bytes);

/*
 * bdrv_write_threshold_get
 *
 * Get the configured write threshold, in bytes.
 * Zero means no threshold configured.
 */
uint64_t bdrv_write_threshold_get(const BlockDriverState *bs);

/*
 * bdrv_write_threshold_check_write
 *
 * Check whether the specified request exceeds the write threshold.
 * If so, send a corresponding event and disable write threshold checking.
 */
void bdrv_write_threshold_check_write(BlockDriverState *bs, int64_t offset,
                                      int64_t bytes);

#endif
