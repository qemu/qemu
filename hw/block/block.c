/*
 * Common code for block device models
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "block/block_int-common.h"
#include "system/blockdev.h"
#include "system/block-backend.h"
#include "hw/block/block.h"
#include "migration/cpr.h"
#include "qapi/error.h"
#include "qapi/qapi-types-block.h"

/*
 * Read the non-zeroes parts of @blk into @buf
 * Reading all of the @blk is expensive if the zeroes parts of @blk
 * is large enough. Therefore check the block status and only write
 * the non-zeroes block into @buf.
 *
 * Return 0 on success, non-zero on error.
 */
static int blk_pread_nonzeroes(BlockBackend *blk, hwaddr size, void *buf)
{
    int ret;
    int64_t bytes, offset = 0;
    BlockDriverState *bs = blk_bs(blk);

    for (;;) {
        bytes = MIN(size - offset, BDRV_REQUEST_MAX_BYTES);
        if (bytes <= 0) {
            return 0;
        }
        ret = bdrv_block_status(bs, offset, bytes, &bytes, NULL, NULL);
        if (ret < 0) {
            return ret;
        }
        if (!(ret & BDRV_BLOCK_ZERO)) {
            ret = blk_pread(blk, offset, bytes, (uint8_t *) buf + offset, 0);
            if (ret < 0) {
                return ret;
            }
        }
        offset += bytes;
    }
}

/*
 * Read the entire contents of @blk into @buf.
 * @blk's contents must be @size bytes, and @size must be at most
 * BDRV_REQUEST_MAX_BYTES.
 * On success, return true.
 * On failure, store an error through @errp and return false.
 *
 * This function not intended for actual block devices, which read on
 * demand.  It's for things like memory devices that (ab)use a block
 * backend to provide persistence.
 */
bool blk_check_size_and_read_all(BlockBackend *blk, DeviceState *dev,
                                 void *buf, hwaddr size, Error **errp)
{
    int64_t blk_len;
    int ret;
    g_autofree char *dev_id = NULL;

    if (cpr_is_incoming()) {
        return true;
    }

    blk_len = blk_getlength(blk);
    if (blk_len < 0) {
        error_setg_errno(errp, -blk_len,
                         "can't get size of %s block backend", blk_name(blk));
        return false;
    }
    if (blk_len != size) {
        dev_id = qdev_get_human_name(dev);
        error_setg(errp, "%s device '%s' requires %" HWADDR_PRIu
                   " bytes, %s block backend provides %" PRIu64 " bytes",
                   object_get_typename(OBJECT(dev)), dev_id, size,
                   blk_name(blk), blk_len);
        return false;
    }

    /*
     * We could loop for @size > BDRV_REQUEST_MAX_BYTES, but if we
     * ever get to the point we want to read *gigabytes* here, we
     * should probably rework the device to be more like an actual
     * block device and read only on demand.
     */
    assert(size <= BDRV_REQUEST_MAX_BYTES);
    ret = blk_pread_nonzeroes(blk, size, buf);
    if (ret < 0) {
        dev_id = qdev_get_human_name(dev);
        error_setg_errno(errp, -ret, "can't read %s block backend"
                         " for %s device '%s'",
                         blk_name(blk), object_get_typename(OBJECT(dev)),
                         dev_id);
        return false;
    }
    return true;
}

bool blkconf_blocksizes(BlockConf *conf, Error **errp)
{
    BlockBackend *blk = conf->blk;
    BlockSizes blocksizes;
    BlockDriverState *bs;
    bool use_blocksizes;
    bool use_bs;

    switch (conf->backend_defaults) {
    case ON_OFF_AUTO_AUTO:
        use_blocksizes = !blk_probe_blocksizes(blk, &blocksizes);
        use_bs = false;
        break;

    case ON_OFF_AUTO_ON:
        use_blocksizes = !blk_probe_blocksizes(blk, &blocksizes);
        bs = blk_bs(blk);
        use_bs = bs;
        break;

    case ON_OFF_AUTO_OFF:
        use_blocksizes = false;
        use_bs = false;
        break;

    default:
        abort();
    }

    /* fill in detected values if they are not defined via qemu command line */
    if (!conf->physical_block_size) {
        if (use_blocksizes) {
           conf->physical_block_size = blocksizes.phys;
        } else {
            conf->physical_block_size = BDRV_SECTOR_SIZE;
        }
    }
    if (!conf->logical_block_size) {
        if (use_blocksizes) {
            conf->logical_block_size = blocksizes.log;
        } else {
            conf->logical_block_size = BDRV_SECTOR_SIZE;
        }
    }
    if (use_bs) {
        if (!conf->opt_io_size) {
            conf->opt_io_size = bs->bl.opt_transfer;
        }
        if (conf->discard_granularity == -1) {
            if (bs->bl.pdiscard_alignment) {
                conf->discard_granularity = bs->bl.pdiscard_alignment;
            } else if (bs->bl.request_alignment != 1) {
                conf->discard_granularity = bs->bl.request_alignment;
            }
        }
    }

    if (conf->logical_block_size > conf->physical_block_size) {
        error_setg(errp,
                   "logical_block_size > physical_block_size not supported");
        return false;
    }

    if (!QEMU_IS_ALIGNED(conf->min_io_size, conf->logical_block_size)) {
        error_setg(errp,
                   "min_io_size must be a multiple of logical_block_size");
        return false;
    }

    /*
     * all devices which support min_io_size (scsi and virtio-blk) expose it to
     * the guest as a uint16_t in units of logical blocks
     */
    if (conf->min_io_size / conf->logical_block_size > UINT16_MAX) {
        error_setg(errp, "min_io_size must not exceed %u logical blocks",
                   UINT16_MAX);
        return false;
    }

    if (!QEMU_IS_ALIGNED(conf->opt_io_size, conf->logical_block_size)) {
        error_setg(errp,
                   "opt_io_size must be a multiple of logical_block_size");
        return false;
    }

    if (conf->discard_granularity != -1 &&
        !QEMU_IS_ALIGNED(conf->discard_granularity,
                         conf->logical_block_size)) {
        error_setg(errp, "discard_granularity must be "
                   "a multiple of logical_block_size");
        return false;
    }

    return true;
}

uint32_t blkconf_zone_write_granularity(BlockConf *conf)
{
    return MAX(conf->logical_block_size, conf->physical_block_size);
}

bool blkconf_check_zoned_geometry(BlockConf *conf, Error **errp)
{
    BlockDriverState *bs = blk_bs(conf->blk);
    uint32_t wg;

    if (bs->bl.zoned == BLK_Z_NONE) {
        return true;
    }

    wg = blkconf_zone_write_granularity(conf);

    /*
     * A backend that requires a coarser granularity than the one that is about
     * to be reported would reject writes that the guest has been told are
     * valid, leaving it with a plain I/O error.
     */
    if (bs->bl.write_granularity > wg) {
        error_setg(errp, "the backend requires writes to sequential zones to "
                   "be a multiple of %" PRIu32 " bytes, which is coarser than "
                   "the zone write granularity %" PRIu32 " that would be "
                   "reported", bs->bl.write_granularity, wg);
        error_append_hint(errp, "Set physical_block_size=%" PRIu32 ".\n",
                          bs->bl.write_granularity);
        return false;
    }

    if (!QEMU_IS_ALIGNED(bs->bl.zone_size, wg)) {
        error_setg(errp, "zone size %" PRIu64 " is not a multiple of the zone "
                   "write granularity %" PRIu32, bs->bl.zone_size, wg);
        return false;
    }

    /*
     * A write pointer that is not a multiple of the write granularity does not
     * fall on a logical block boundary, so the guest can neither read nor write
     * at it and the zone can only be recovered by resetting it. A backend that
     * records its write pointers, rather than reading them back from a device,
     * can hand us such a pointer when the zones were written while the device
     * was configured with a smaller logical block size.
     */
    for (uint32_t i = 0; i < bs->bl.nr_zones; i++) {
        uint64_t wp;

        if (bdrv_zone_is_conv(bs, i)) {
            continue;
        }

        wp = bs->wps->wp[i];
        if (!QEMU_IS_ALIGNED(wp, wg)) {
            error_setg(errp, "write pointer 0x%" PRIx64 " of zone %" PRIu32
                       " is not a multiple of the zone write granularity %"
                       PRIu32, wp, i, wg);
            error_append_hint(errp, "The zones were written with a finer zone "
                              "write granularity. Reset them, or attach the "
                              "device with the block sizes that they were "
                              "written with.\n");
            return false;
        }
    }

    return true;
}

bool blkconf_apply_backend_options(BlockConf *conf, bool readonly,
                                   bool resizable, Error **errp)
{
    BlockBackend *blk = conf->blk;
    BlockdevOnError rerror, werror;
    uint64_t perm, shared_perm;
    bool wce;
    int ret;

    perm = BLK_PERM_CONSISTENT_READ;
    if (!readonly) {
        perm |= BLK_PERM_WRITE;
    }

    shared_perm = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED;
    if (resizable) {
        shared_perm |= BLK_PERM_RESIZE;
    }
    if (conf->share_rw) {
        shared_perm |= BLK_PERM_WRITE;
    }

    ret = blk_set_perm(blk, perm, shared_perm, errp);
    if (ret < 0) {
        return false;
    }

    switch (conf->wce) {
    case ON_OFF_AUTO_ON:    wce = true; break;
    case ON_OFF_AUTO_OFF:   wce = false; break;
    case ON_OFF_AUTO_AUTO:  wce = blk_enable_write_cache(blk); break;
    default:
        abort();
    }

    rerror = conf->rerror;
    if (rerror == BLOCKDEV_ON_ERROR_AUTO) {
        rerror = blk_get_on_error(blk, true);
    }

    werror = conf->werror;
    if (werror == BLOCKDEV_ON_ERROR_AUTO) {
        werror = blk_get_on_error(blk, false);
    }

    blk_set_enable_write_cache(blk, wce);
    blk_set_on_error(blk, rerror, werror);

    if (!block_acct_setup(blk_get_stats(blk), conf->account_invalid,
                          conf->account_failed, conf->stats_intervals,
                          conf->num_stats_intervals, errp)) {
        return false;
    }
    return true;
}

bool blkconf_geometry(BlockConf *conf, int *ptrans,
                      unsigned cyls_max, unsigned heads_max, unsigned secs_max,
                      Error **errp)
{
    if (!conf->cyls && !conf->heads && !conf->secs) {
        hd_geometry_guess(conf->blk,
                          &conf->cyls, &conf->heads, &conf->secs,
                          ptrans);
    } else if (ptrans && *ptrans == BIOS_ATA_TRANSLATION_AUTO) {
        *ptrans = hd_bios_chs_auto_trans(conf->cyls, conf->heads, conf->secs);
    }
    if (conf->cyls || conf->heads || conf->secs) {
        if (conf->cyls < 1 || conf->cyls > cyls_max) {
            error_setg(errp, "cyls must be between 1 and %u", cyls_max);
            return false;
        }
        if (conf->heads < 1 || conf->heads > heads_max) {
            error_setg(errp, "heads must be between 1 and %u", heads_max);
            return false;
        }
        if (conf->secs < 1 || conf->secs > secs_max) {
            error_setg(errp, "secs must be between 1 and %u", secs_max);
            return false;
        }
    }
    return true;
}
