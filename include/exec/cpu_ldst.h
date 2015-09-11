/*
 *  Software MMU support
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

/*
 * Generate inline load/store functions for all MMU modes (typically
 * at least _user and _kernel) as well as _data versions, for all data
 * sizes.
 *
 * Used by target op helpers.
 *
 * The syntax for the accessors is:
 *
 * load: cpu_ld{sign}{size}_{mmusuffix}(env, ptr)
 *
 * store: cpu_st{sign}{size}_{mmusuffix}(env, ptr, val)
 *
 * sign is:
 * (empty): for 32 and 64 bit sizes
 *   u    : unsigned
 *   s    : signed
 *
 * size is:
 *   b: 8 bits
 *   w: 16 bits
 *   l: 32 bits
 *   q: 64 bits
 *
 * mmusuffix is one of the generic suffixes "data" or "code", or
 * (for softmmu configs)  a target-specific MMU mode suffix as defined
 * in target cpu.h.
 */
#ifndef CPU_LDST_H
#define CPU_LDST_H

#if defined(CONFIG_USER_ONLY)
/* All direct uses of g2h and h2g need to go away for usermode softmmu.  */
#define g2h(x) ((void *)((unsigned long)(target_ulong)(x) + guest_base))

#if HOST_LONG_BITS <= TARGET_VIRT_ADDR_SPACE_BITS
#define h2g_valid(x) 1
#else
#define h2g_valid(x) ({ \
    unsigned long __guest = (unsigned long)(x) - guest_base; \
    (__guest < (1ul << TARGET_VIRT_ADDR_SPACE_BITS)) && \
    (!reserved_va || (__guest < reserved_va)); \
})
#endif

#define h2g_nocheck(x) ({ \
    unsigned long __ret = (unsigned long)(x) - guest_base; \
    (abi_ulong)__ret; \
})

#define h2g(x) ({ \
    /* Check if given address fits target address space */ \
    assert(h2g_valid(x)); \
    h2g_nocheck(x); \
})

#endif

#if defined(CONFIG_USER_ONLY)

/* In user-only mode we provide only the _code and _data accessors. */

#define MEMSUFFIX _data
#define DATA_SIZE 1
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_useronly_template.h"
#undef MEMSUFFIX

#define MEMSUFFIX _code
#define CODE_ACCESS
#define DATA_SIZE 1
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_useronly_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_useronly_template.h"
#undef MEMSUFFIX
#undef CODE_ACCESS

#else

/* The memory helpers for tcg-generated code need tcg_target_long etc.  */
#include "tcg.h"

#ifdef MMU_MODE0_SUFFIX
#define CPU_MMU_INDEX 0
#define MEMSUFFIX MMU_MODE0_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif

#if (NB_MMU_MODES >= 2) && defined(MMU_MODE1_SUFFIX)
#define CPU_MMU_INDEX 1
#define MEMSUFFIX MMU_MODE1_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif

#if (NB_MMU_MODES >= 3) && defined(MMU_MODE2_SUFFIX)

#define CPU_MMU_INDEX 2
#define MEMSUFFIX MMU_MODE2_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 3) */

#if (NB_MMU_MODES >= 4) && defined(MMU_MODE3_SUFFIX)

#define CPU_MMU_INDEX 3
#define MEMSUFFIX MMU_MODE3_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 4) */

#if (NB_MMU_MODES >= 5) && defined(MMU_MODE4_SUFFIX)

#define CPU_MMU_INDEX 4
#define MEMSUFFIX MMU_MODE4_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 5) */

#if (NB_MMU_MODES >= 6) && defined(MMU_MODE5_SUFFIX)

#define CPU_MMU_INDEX 5
#define MEMSUFFIX MMU_MODE5_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 6) */

#if (NB_MMU_MODES >= 7) && defined(MMU_MODE6_SUFFIX)

#define CPU_MMU_INDEX 6
#define MEMSUFFIX MMU_MODE6_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 7) */

#if (NB_MMU_MODES >= 8) && defined(MMU_MODE7_SUFFIX)

#define CPU_MMU_INDEX 7
#define MEMSUFFIX MMU_MODE7_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 8) */

#if (NB_MMU_MODES >= 9) && defined(MMU_MODE8_SUFFIX)

#define CPU_MMU_INDEX 8
#define MEMSUFFIX MMU_MODE8_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 9) */

#if (NB_MMU_MODES >= 10) && defined(MMU_MODE9_SUFFIX)

#define CPU_MMU_INDEX 9
#define MEMSUFFIX MMU_MODE9_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 10) */

#if (NB_MMU_MODES >= 11) && defined(MMU_MODE10_SUFFIX)

#define CPU_MMU_INDEX 10
#define MEMSUFFIX MMU_MODE10_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 11) */

#if (NB_MMU_MODES >= 12) && defined(MMU_MODE11_SUFFIX)

#define CPU_MMU_INDEX 11
#define MEMSUFFIX MMU_MODE11_SUFFIX
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 12) */

#if (NB_MMU_MODES > 12)
#error "NB_MMU_MODES > 12 is not supported for now"
#endif /* (NB_MMU_MODES > 12) */

/* these access are slower, they must be as rare as possible */
#define CPU_MMU_INDEX (cpu_mmu_index(env, false))
#define MEMSUFFIX _data
#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX

#define CPU_MMU_INDEX (cpu_mmu_index(env, true))
#define MEMSUFFIX _code
#define SOFTMMU_CODE_ACCESS

#define DATA_SIZE 1
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 2
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 4
#include "exec/cpu_ldst_template.h"

#define DATA_SIZE 8
#include "exec/cpu_ldst_template.h"

#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#undef SOFTMMU_CODE_ACCESS

#endif /* defined(CONFIG_USER_ONLY) */

/**
 * tlb_vaddr_to_host:
 * @env: CPUArchState
 * @addr: guest virtual address to look up
 * @access_type: 0 for read, 1 for write, 2 for execute
 * @mmu_idx: MMU index to use for lookup
 *
 * Look up the specified guest virtual index in the TCG softmmu TLB.
 * If the TLB contains a host virtual address suitable for direct RAM
 * access, then return it. Otherwise (TLB miss, TLB entry is for an
 * I/O access, etc) return NULL.
 *
 * This is the equivalent of the initial fast-path code used by
 * TCG backends for guest load and store accesses.
 */
static inline void *tlb_vaddr_to_host(CPUArchState *env, target_ulong addr,
                                      int access_type, int mmu_idx)
{
#if defined(CONFIG_USER_ONLY)
    return g2h(vaddr);
#else
    int index = (addr >> TARGET_PAGE_BITS) & (CPU_TLB_SIZE - 1);
    CPUTLBEntry *tlbentry = &env->tlb_table[mmu_idx][index];
    target_ulong tlb_addr;
    uintptr_t haddr;

    switch (access_type) {
    case 0:
        tlb_addr = tlbentry->addr_read;
        break;
    case 1:
        tlb_addr = tlbentry->addr_write;
        break;
    case 2:
        tlb_addr = tlbentry->addr_code;
        break;
    default:
        g_assert_not_reached();
    }

    if ((addr & TARGET_PAGE_MASK)
        != (tlb_addr & (TARGET_PAGE_MASK | TLB_INVALID_MASK))) {
        /* TLB entry is for a different page */
        return NULL;
    }

    if (tlb_addr & ~TARGET_PAGE_MASK) {
        /* IO access */
        return NULL;
    }

    haddr = addr + env->tlb_table[mmu_idx][index].addend;
    return (void *)haddr;
#endif /* defined(CONFIG_USER_ONLY) */
}

#endif /* CPU_LDST_H */
