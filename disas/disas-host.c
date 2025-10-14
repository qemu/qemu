/*
 * Routines for host instruction disassembly.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "disas/disas.h"
#include "disas/capstone.h"
#include "disas-internal.h"


/*
 * Get LENGTH bytes from info's buffer, at host address memaddr.
 * Transfer them to myaddr.
 */
static int host_read_memory(bfd_vma memaddr, bfd_byte *myaddr, int length,
                            struct disassemble_info *info)
{
    if (memaddr < info->buffer_vma
        || memaddr + length > info->buffer_vma + info->buffer_length) {
        /* Out of bounds.  Use EIO because GDB uses it.  */
        return EIO;
    }
    memcpy (myaddr, info->buffer + (memaddr - info->buffer_vma), length);
    return 0;
}

/* Print address in hex, truncated to the width of a host virtual address. */
static void host_print_address(bfd_vma addr, struct disassemble_info *info)
{
    info->fprintf_func(info->stream, "0x%" PRIxPTR, (uintptr_t)addr);
}

static void initialize_debug_host(CPUDebug *s)
{
    disas_initialize_debug(s);

    s->info.read_memory_func = host_read_memory;
    s->info.print_address_func = host_print_address;
#if HOST_BIG_ENDIAN
    s->info.endian = BFD_ENDIAN_BIG;
#else
    s->info.endian = BFD_ENDIAN_LITTLE;
#endif
#if defined(CONFIG_TCG_INTERPRETER)
    s->info.print_insn = print_insn_tci;
#elif defined(__i386__)
    s->info.mach = bfd_mach_i386_i386;
    s->info.cap_arch = CS_ARCH_X86;
    s->info.cap_mode = CS_MODE_32;
    s->info.cap_insn_unit = 1;
    s->info.cap_insn_split = 8;
#elif defined(__x86_64__)
    s->info.mach = bfd_mach_x86_64;
    s->info.cap_arch = CS_ARCH_X86;
    s->info.cap_mode = CS_MODE_64;
    s->info.cap_insn_unit = 1;
    s->info.cap_insn_split = 8;
#elif defined(_ARCH_PPC64)
    s->info.cap_arch = CS_ARCH_PPC;
    s->info.cap_mode = CS_MODE_64;
#elif defined(__riscv)
#if defined(_ILP32) || (__riscv_xlen == 32)
    s->info.print_insn = print_insn_riscv32;
#elif defined(_LP64)
    s->info.print_insn = print_insn_riscv64;
#else
#error unsupported RISC-V ABI
#endif
#elif defined(__aarch64__)
    s->info.cap_arch = CS_ARCH_ARM64;
#elif defined(__alpha__)
    s->info.print_insn = print_insn_alpha;
#elif defined(__sparc__)
    s->info.print_insn = print_insn_sparc;
    s->info.mach = bfd_mach_sparc_v9b;
#elif defined(__arm__)
    /* TCG only generates code for arm mode.  */
    s->info.cap_arch = CS_ARCH_ARM;
#elif defined(__MIPSEB__)
    s->info.print_insn = print_insn_big_mips;
#elif defined(__MIPSEL__)
    s->info.print_insn = print_insn_little_mips;
#elif defined(__m68k__)
    s->info.print_insn = print_insn_m68k;
#elif defined(__s390__)
    s->info.cap_arch = CS_ARCH_SYSZ;
    s->info.cap_insn_unit = 2;
    s->info.cap_insn_split = 6;
#elif defined(__hppa__)
    s->info.print_insn = print_insn_hppa;
#elif defined(__loongarch__)
    s->info.print_insn = print_insn_loongarch;
#endif
}

/* Disassemble this for me please... (debugging). */
void disas(FILE *out, const void *code, size_t size)
{
    uintptr_t pc;
    int count;
    CPUDebug s;

    initialize_debug_host(&s);
    s.info.fprintf_func = fprintf;
    s.info.stream = out;
    s.info.buffer = code;
    s.info.buffer_vma = (uintptr_t)code;
    s.info.buffer_length = size;
    s.info.show_opcodes = true;

    if (s.info.cap_arch >= 0 && cap_disas_host(&s.info, code, size)) {
        return;
    }

    if (s.info.print_insn == NULL) {
        s.info.print_insn = print_insn_od_host;
    }
    for (pc = (uintptr_t)code; size > 0; pc += count, size -= count) {
        fprintf(out, "0x%08" PRIxPTR ":  ", pc);
        count = s.info.print_insn(pc, &s.info);
        fprintf(out, "\n");
        if (count < 0) {
            break;
        }
    }
}
