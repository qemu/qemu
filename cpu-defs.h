/*
 * common defines for all CPUs
 *
 * Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#ifndef CPU_DEFS_H
#define CPU_DEFS_H

#ifndef NEED_CPU_H
#error cpu.h included from common code
#endif

#include "config.h"
#include <setjmp.h>
#include <inttypes.h>
#include "osdep.h"

#ifndef TARGET_LONG_BITS
#error TARGET_LONG_BITS must be defined before including this header
#endif

#ifndef TARGET_PHYS_ADDR_BITS
#if TARGET_LONG_BITS >= HOST_LONG_BITS
#define TARGET_PHYS_ADDR_BITS TARGET_LONG_BITS
#else
#define TARGET_PHYS_ADDR_BITS HOST_LONG_BITS
#endif
#endif

#define TARGET_LONG_SIZE (TARGET_LONG_BITS / 8)

/* target_ulong is the type of a virtual address */
#if TARGET_LONG_SIZE == 4
typedef int32_t target_long;
typedef uint32_t target_ulong;
#define TARGET_FMT_lx "%08x"
#define TARGET_FMT_ld "%d"
#define TARGET_FMT_lu "%u"
#elif TARGET_LONG_SIZE == 8
typedef int64_t target_long;
typedef uint64_t target_ulong;
#define TARGET_FMT_lx "%016" PRIx64
#define TARGET_FMT_ld "%" PRId64
#define TARGET_FMT_lu "%" PRIu64
#else
#error TARGET_LONG_SIZE undefined
#endif

/* target_phys_addr_t is the type of a physical address (its size can
   be different from 'target_ulong'). We have sizeof(target_phys_addr)
   = max(sizeof(unsigned long),
   sizeof(size_of_target_physical_address)) because we must pass a
   host pointer to memory operations in some cases */

#if TARGET_PHYS_ADDR_BITS == 32
typedef uint32_t target_phys_addr_t;
#define TARGET_FMT_plx "%08x"
#elif TARGET_PHYS_ADDR_BITS == 64
typedef uint64_t target_phys_addr_t;
#define TARGET_FMT_plx "%016" PRIx64
#else
#error TARGET_PHYS_ADDR_BITS undefined
#endif

/* address in the RAM (different from a physical address) */
typedef unsigned long ram_addr_t;

#define HOST_LONG_SIZE (HOST_LONG_BITS / 8)

#define EXCP_INTERRUPT 	0x10000 /* async interruption */
#define EXCP_HLT        0x10001 /* hlt instruction reached */
#define EXCP_DEBUG      0x10002 /* cpu stopped after a breakpoint or singlestep */
#define EXCP_HALTED     0x10003 /* cpu is halted (waiting for external event) */
#define MAX_BREAKPOINTS 32
#define MAX_WATCHPOINTS 32

#define TB_JMP_CACHE_BITS 12
#define TB_JMP_CACHE_SIZE (1 << TB_JMP_CACHE_BITS)

/* Only the bottom TB_JMP_PAGE_BITS of the jump cache hash bits vary for
   addresses on the same page.  The top bits are the same.  This allows
   TLB invalidation to quickly clear a subset of the hash table.  */
#define TB_JMP_PAGE_BITS (TB_JMP_CACHE_BITS / 2)
#define TB_JMP_PAGE_SIZE (1 << TB_JMP_PAGE_BITS)
#define TB_JMP_ADDR_MASK (TB_JMP_PAGE_SIZE - 1)
#define TB_JMP_PAGE_MASK (TB_JMP_CACHE_SIZE - TB_JMP_PAGE_SIZE)

#define CPU_TLB_BITS 8
#define CPU_TLB_SIZE (1 << CPU_TLB_BITS)

typedef struct CPUTLBEntry {
    /* bit 31 to TARGET_PAGE_BITS : virtual address
       bit TARGET_PAGE_BITS-1..IO_MEM_SHIFT : if non zero, memory io
                                              zone number
       bit 3                      : indicates that the entry is invalid
       bit 2..0                   : zero
    */
    target_ulong addr_read;
    target_ulong addr_write;
    target_ulong addr_code;
    /* addend to virtual address to get physical address */
    target_phys_addr_t addend;
} CPUTLBEntry;

#define CPU_COMMON                                                      \
    struct TranslationBlock *current_tb; /* currently executing TB  */  \
    /* soft mmu support */                                              \
    /* in order to avoid passing too many arguments to the memory       \
       write helpers, we store some rarely used information in the CPU  \
       context) */                                                      \
    unsigned long mem_write_pc; /* host pc at which the memory was      \
                                   written */                           \
    target_ulong mem_write_vaddr; /* target virtual addr at which the   \
                                     memory was written */              \
    /* The meaning of the MMU modes is defined in the target code. */   \
    CPUTLBEntry tlb_table[NB_MMU_MODES][CPU_TLB_SIZE];                  \
    struct TranslationBlock *tb_jmp_cache[TB_JMP_CACHE_SIZE];           \
                                                                        \
    /* from this point: preserved by CPU reset */                       \
    /* ice debug support */                                             \
    target_ulong breakpoints[MAX_BREAKPOINTS];                          \
    int nb_breakpoints;                                                 \
    int singlestep_enabled;                                             \
                                                                        \
    struct {                                                            \
        target_ulong vaddr;                                             \
        target_phys_addr_t addend;                                      \
    } watchpoint[MAX_WATCHPOINTS];                                      \
    int nb_watchpoints;                                                 \
    int watchpoint_hit;                                                 \
                                                                        \
    void *next_cpu; /* next CPU sharing TB cache */                     \
    int cpu_index; /* CPU index (informative) */                        \
    /* user data */                                                     \
    void *opaque;                                                       \
                                                                        \
    const char *cpu_model_str;

#endif
