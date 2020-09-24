/*
 * Declarations for block exports
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

#ifndef BLOCK_EXPORT_H
#define BLOCK_EXPORT_H

#include "qapi/qapi-types-block-export.h"

typedef struct BlockExport BlockExport;

typedef struct BlockExportDriver {
    /* The export type that this driver services */
    BlockExportType type;

    /*
     * The size of the driver-specific state that contains BlockExport as its
     * first field.
     */
    size_t instance_size;

    /* Creates and starts a new block export */
    int (*create)(BlockExport *, BlockExportOptions *, Error **);

    /*
     * Frees a removed block export. This function is only called after all
     * references have been dropped.
     */
    void (*delete)(BlockExport *);
} BlockExportDriver;

struct BlockExport {
    const BlockExportDriver *drv;

    /*
     * Reference count for this block export. This includes strong references
     * both from the owner (qemu-nbd or the monitor) and clients connected to
     * the export.
     */
    int refcount;

    /* The AioContext whose lock protects this BlockExport object. */
    AioContext *ctx;
};

BlockExport *blk_exp_add(BlockExportOptions *export, Error **errp);
void blk_exp_ref(BlockExport *exp);
void blk_exp_unref(BlockExport *exp);

#endif
