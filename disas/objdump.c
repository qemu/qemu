/*
 * Dump disassembly as text, for processing by scripts/disas-objdump.pl.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas-internal.h"


static int print_insn_objdump(bfd_vma pc, disassemble_info *info,
                              const char *prefix)
{
    int i, n = info->buffer_length;
    g_autofree uint8_t *buf = g_malloc(n);

    if (info->read_memory_func(pc, buf, n, info) == 0) {
        for (i = 0; i < n; ++i) {
            if (i % 32 == 0) {
                info->fprintf_func(info->stream, "\n%s: ", prefix);
            }
            info->fprintf_func(info->stream, "%02x", buf[i]);
        }
    } else {
        info->fprintf_func(info->stream, "unable to read memory");
    }
    return n;
}

int print_insn_od_host(bfd_vma pc, disassemble_info *info)
{
    return print_insn_objdump(pc, info, "OBJD-H");
}

int print_insn_od_target(bfd_vma pc, disassemble_info *info)
{
    return print_insn_objdump(pc, info, "OBJD-T");
}
