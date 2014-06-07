/*
 * Copyright (c) 2000-2005 Silicon Graphics, Inc.
 * All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it would be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef QEMU_IO_H
#define QEMU_IO_H

#include "qemu-common.h"

#define CMD_FLAG_GLOBAL ((int)0x80000000) /* don't iterate "args" */

typedef int (*cfunc_t)(BlockDriverState *bs, int argc, char **argv);
typedef void (*helpfunc_t)(void);

typedef struct cmdinfo {
    const char* name;
    const char* altname;
    cfunc_t     cfunc;
    int         argmin;
    int         argmax;
    int         canpush;
    int         flags;
    const char  *args;
    const char  *oneline;
    helpfunc_t  help;
} cmdinfo_t;

extern bool qemuio_misalign;

bool qemuio_command(BlockDriverState *bs, const char *cmd);

void qemuio_add_command(const cmdinfo_t *ci);
int qemuio_command_usage(const cmdinfo_t *ci);
void qemuio_complete_command(const char *input,
                             void (*fn)(const char *cmd, void *opaque),
                             void *opaque);

#endif /* QEMU_IO_H */
