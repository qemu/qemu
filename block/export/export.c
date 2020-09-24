/*
 * Common block export infrastructure
 *
 * Copyright (c) 2012, 2020 Red Hat, Inc.
 *
 * Authors:
 * Paolo Bonzini <pbonzini@redhat.com>
 * Kevin Wolf <kwolf@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"

#include "block/block.h"
#include "sysemu/block-backend.h"
#include "block/export.h"
#include "block/nbd.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-block-export.h"

static const BlockExportDriver *blk_exp_drivers[] = {
    &blk_exp_nbd,
};

static const BlockExportDriver *blk_exp_find_driver(BlockExportType type)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(blk_exp_drivers); i++) {
        if (blk_exp_drivers[i]->type == type) {
            return blk_exp_drivers[i];
        }
    }
    return NULL;
}

BlockExport *blk_exp_add(BlockExportOptions *export, Error **errp)
{
    const BlockExportDriver *drv;

    drv = blk_exp_find_driver(export->type);
    if (!drv) {
        error_setg(errp, "No driver found for the requested export type");
        return NULL;
    }

    return drv->create(export, errp);
}

void blk_exp_ref(BlockExport *exp)
{
    assert(exp->refcount > 0);
    exp->refcount++;
}

void blk_exp_unref(BlockExport *exp)
{
    assert(exp->refcount > 0);
    if (--exp->refcount == 0) {
        exp->drv->delete(exp);
    }
}

void qmp_block_export_add(BlockExportOptions *export, Error **errp)
{
    blk_exp_add(export, errp);
}
