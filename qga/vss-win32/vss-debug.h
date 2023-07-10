/*
 * QEMU Guest Agent VSS debug declarations
 *
 * Copyright (C) 2023 Red Hat Inc
 *
 * Authors:
 *  Konstantin Kostiuk <kkostiuk@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <vss-handles.h>

#ifndef VSS_DEBUG_H
#define VSS_DEBUG_H

void qga_debug_internal(const char *funcname, const char *fmt, ...) G_GNUC_PRINTF(2, 3);

#define qga_debug(fmt, ...) qga_debug_internal(__func__, fmt, ## __VA_ARGS__)
#define qga_debug_begin qga_debug("begin")
#define qga_debug_end qga_debug("end")

#endif
