/*
 * QEMU dump
 *
 * Copyright Fujitsu, Corp. 2011, 2012
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu-common.h"
#include "sysemu/dump.h"
#include "qapi/qmp/qerror.h"
#include "qmp-commands.h"

/* we need this function in hmp.c */
void qmp_dump_guest_memory(bool paging, const char *file, bool has_begin,
                           int64_t begin, bool has_length, int64_t length,
                           Error **errp)
{
    error_set(errp, QERR_UNSUPPORTED);
}

int cpu_get_dump_info(ArchDumpInfo *info)
{
    return -1;
}

ssize_t cpu_get_note_size(int class, int machine, int nr_cpus)
{
    return -1;
}

