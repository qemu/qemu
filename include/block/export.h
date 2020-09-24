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

    /* Creates and starts a new block export */
    BlockExport *(*create)(BlockExportOptions *, Error **);
} BlockExportDriver;

struct BlockExport {
    const BlockExportDriver *drv;
};

#endif
