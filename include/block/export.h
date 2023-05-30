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
#include "qemu/queue.h"

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

    /*
     * Start to disconnect all clients and drop other references held
     * internally by the export driver. When the function returns, there may
     * still be active references while the export is in the process of
     * shutting down.
     */
    void (*request_shutdown)(BlockExport *);
} BlockExportDriver;

struct BlockExport {
    const BlockExportDriver *drv;

    /* Unique identifier for the export */
    char *id;

    /*
     * Reference count for this block export. This includes strong references
     * both from the owner (qemu-nbd or the monitor) and clients connected to
     * the export.
     *
     * Use atomics to access this field.
     */
    int refcount;

    /*
     * True if one of the references in refcount belongs to the user. After the
     * user has dropped their reference, they may not e.g. remove the same
     * export a second time (which would decrease the refcount without having
     * it incremented first).
     */
    bool user_owned;

    /* The AioContext whose lock protects this BlockExport object. */
    AioContext *ctx;

    /* The block device to export */
    BlockBackend *blk;

    /* List entry for block_exports */
    QLIST_ENTRY(BlockExport) next;
};

BlockExport *blk_exp_add(BlockExportOptions *export, Error **errp);
BlockExport *blk_exp_find(const char *id);
void blk_exp_ref(BlockExport *exp);
void blk_exp_unref(BlockExport *exp);
void blk_exp_request_shutdown(BlockExport *exp);
void blk_exp_close_all(void);
void blk_exp_close_all_type(BlockExportType type);

#endif
