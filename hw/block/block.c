/*
 * Common code for block device models
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "sysemu/blockdev.h"
#include "sysemu/block-backend.h"
#include "hw/block/block.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

void blkconf_serial(BlockConf *conf, char **serial)
{
    DriveInfo *dinfo;

    if (!*serial) {
        /* try to fall back to value set with legacy -drive serial=... */
        dinfo = blk_legacy_dinfo(conf->blk);
        if (dinfo) {
            *serial = g_strdup(dinfo->serial);
        }
    }
}

void blkconf_blocksizes(BlockConf *conf)
{
    BlockBackend *blk = conf->blk;
    BlockSizes blocksizes;
    int backend_ret;

    backend_ret = blk_probe_blocksizes(blk, &blocksizes);
    /* fill in detected values if they are not defined via qemu command line */
    if (!conf->physical_block_size) {
        if (!backend_ret) {
           conf->physical_block_size = blocksizes.phys;
        } else {
            conf->physical_block_size = BDRV_SECTOR_SIZE;
        }
    }
    if (!conf->logical_block_size) {
        if (!backend_ret) {
            conf->logical_block_size = blocksizes.log;
        } else {
            conf->logical_block_size = BDRV_SECTOR_SIZE;
        }
    }
}

void blkconf_apply_backend_options(BlockConf *conf, bool readonly,
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

    shared_perm = BLK_PERM_CONSISTENT_READ | BLK_PERM_WRITE_UNCHANGED |
                  BLK_PERM_GRAPH_MOD;
    if (resizable) {
        shared_perm |= BLK_PERM_RESIZE;
    }
    if (conf->share_rw) {
        shared_perm |= BLK_PERM_WRITE;
    }

    ret = blk_set_perm(blk, perm, shared_perm, errp);
    if (ret < 0) {
        return;
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
}

void blkconf_geometry(BlockConf *conf, int *ptrans,
                      unsigned cyls_max, unsigned heads_max, unsigned secs_max,
                      Error **errp)
{
    DriveInfo *dinfo;

    if (!conf->cyls && !conf->heads && !conf->secs) {
        /* try to fall back to value set with legacy -drive cyls=... */
        dinfo = blk_legacy_dinfo(conf->blk);
        if (dinfo) {
            conf->cyls  = dinfo->cyls;
            conf->heads = dinfo->heads;
            conf->secs  = dinfo->secs;
            if (ptrans) {
                *ptrans = dinfo->trans;
            }
        }
    }
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
            return;
        }
        if (conf->heads < 1 || conf->heads > heads_max) {
            error_setg(errp, "heads must be between 1 and %u", heads_max);
            return;
        }
        if (conf->secs < 1 || conf->secs > secs_max) {
            error_setg(errp, "secs must be between 1 and %u", secs_max);
            return;
        }
    }
}
