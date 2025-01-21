/*
 * Common routines for disassembly.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/disas.h"
#include "disas/capstone.h"
#include "hw/core/cpu.h"
#include "disas-internal.h"


/* Filled in by elfload.c.  Simplistic, but will do for now. */
struct syminfo *syminfos = NULL;

/*
 * Print an error message.  We can assume that this is in response to
 * an error return from {host,target}_read_memory.
 */
static void perror_memory(int status, bfd_vma memaddr,
                          struct disassemble_info *info)
{
    if (status != EIO) {
        /* Can't happen.  */
        info->fprintf_func(info->stream, "Unknown error %d\n", status);
    } else {
        /* Address between memaddr and memaddr + len was out of bounds.  */
        info->fprintf_func(info->stream,
                           "Address 0x%" PRIx64 " is out of bounds.\n",
                           memaddr);
    }
}

/* Print address in hex. */
static void print_address(bfd_vma addr, struct disassemble_info *info)
{
    info->fprintf_func(info->stream, "0x%" PRIx64, addr);
}

/* Stub prevents some fruitless earching in optabs disassemblers. */
static int symbol_at_address(bfd_vma addr, struct disassemble_info *info)
{
    return 1;
}

void disas_initialize_debug(CPUDebug *s)
{
    memset(s, 0, sizeof(*s));
    s->info.arch = bfd_arch_unknown;
    s->info.cap_arch = -1;
    s->info.cap_insn_unit = 4;
    s->info.cap_insn_split = 4;
    s->info.memory_error_func = perror_memory;
    s->info.symbol_at_address_func = symbol_at_address;
}

void disas_initialize_debug_target(CPUDebug *s, CPUState *cpu)
{
    disas_initialize_debug(s);

    s->cpu = cpu;
    s->info.print_address_func = print_address;
    s->info.endian = BFD_ENDIAN_UNKNOWN;

    if (cpu->cc->disas_set_info) {
        cpu->cc->disas_set_info(cpu, &s->info);
        g_assert(s->info.endian != BFD_ENDIAN_UNKNOWN);
    }
}

int disas_gstring_printf(FILE *stream, const char *fmt, ...)
{
    /* We abuse the FILE parameter to pass a GString. */
    GString *s = (GString *)stream;
    int initial_len = s->len;
    va_list va;

    va_start(va, fmt);
    g_string_append_vprintf(s, fmt, va);
    va_end(va);

    return s->len - initial_len;
}

/* Look up symbol for debugging purpose.  Returns "" if unknown. */
const char *lookup_symbol(uint64_t orig_addr)
{
    const char *symbol = "";
    struct syminfo *s;

    for (s = syminfos; s; s = s->next) {
        symbol = s->lookup_symbol(s, orig_addr);
        if (symbol[0] != '\0') {
            break;
        }
    }

    return symbol;
}
