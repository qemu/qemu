/*
 * Common code for block device models
 *
 * Copyright (C) 2012 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "blockdev.h"
#include "hw/block-common.h"

void blkconf_serial(BlockConf *conf, char **serial)
{
    DriveInfo *dinfo;

    if (!*serial) {
        /* try to fall back to value set with legacy -drive serial=... */
        dinfo = drive_get_by_blockdev(conf->bs);
        if (dinfo->serial) {
            *serial = g_strdup(dinfo->serial);
        }
    }
}
