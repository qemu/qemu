/*
 * QEMU Block backends
 *
 * Copyright (C) 2014 Red Hat, Inc.
 *
 * Authors:
 *  Markus Armbruster <armbru@redhat.com>,
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.1
 * or later.  See the COPYING.LIB file in the top-level directory.
 */

#ifndef BLOCK_BACKEND_H
#define BLOCK_BACKEND_H

#include "qemu/typedefs.h"
#include "qapi/error.h"

BlockBackend *blk_new(const char *name, Error **errp);
void blk_ref(BlockBackend *blk);
void blk_unref(BlockBackend *blk);
const char *blk_name(BlockBackend *blk);
BlockBackend *blk_by_name(const char *name);
BlockBackend *blk_next(BlockBackend *blk);

#endif
