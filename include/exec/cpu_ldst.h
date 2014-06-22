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
 * MMU mode suffixes are defined in target cpu.h.
 */
#ifndef CPU_LDST_H
#define CPU_LDST_H

#if defined(CONFIG_USER_ONLY)
/* All direct uses of g2h and h2g need to go away for usermode softmmu.  */
#define g2h(x) ((void *)((unsigned long)(target_ulong)(x) + GUEST_BASE))

#if HOST_LONG_BITS <= TARGET_VIRT_ADDR_SPACE_BITS
#define h2g_valid(x) 1
#else
#define h2g_valid(x) ({ \
    unsigned long __guest = (unsigned long)(x) - GUEST_BASE; \
    (__guest < (1ul << TARGET_VIRT_ADDR_SPACE_BITS)) && \
    (!RESERVED_VA || (__guest < RESERVED_VA)); \
})
#endif

#define h2g_nocheck(x) ({ \
    unsigned long __ret = (unsigned long)(x) - GUEST_BASE; \
    (abi_ulong)__ret; \
})

#define h2g(x) ({ \
    /* Check if given address fits target address space */ \
    assert(h2g_valid(x)); \
    h2g_nocheck(x); \
})

#define saddr(x) g2h(x)
#define laddr(x) g2h(x)

#else /* !CONFIG_USER_ONLY */
/* NOTE: we use double casts if pointers and target_ulong have
   different sizes */
#define saddr(x) (uint8_t *)(intptr_t)(x)
#define laddr(x) (uint8_t *)(intptr_t)(x)
#endif

#define ldub_raw(p) ldub_p(laddr((p)))
#define ldsb_raw(p) ldsb_p(laddr((p)))
#define lduw_raw(p) lduw_p(laddr((p)))
#define ldsw_raw(p) ldsw_p(laddr((p)))
#define ldl_raw(p) ldl_p(laddr((p)))
#define ldq_raw(p) ldq_p(laddr((p)))
#define ldfl_raw(p) ldfl_p(laddr((p)))
#define ldfq_raw(p) ldfq_p(laddr((p)))
#define stb_raw(p, v) stb_p(saddr((p)), v)
#define stw_raw(p, v) stw_p(saddr((p)), v)
#define stl_raw(p, v) stl_p(saddr((p)), v)
#define stq_raw(p, v) stq_p(saddr((p)), v)
#define stfl_raw(p, v) stfl_p(saddr((p)), v)
#define stfq_raw(p, v) stfq_p(saddr((p)), v)


#if defined(CONFIG_USER_ONLY)

/* if user mode, no other memory access functions */
#define ldub(p) ldub_raw(p)
#define ldsb(p) ldsb_raw(p)
#define lduw(p) lduw_raw(p)
#define ldsw(p) ldsw_raw(p)
#define ldl(p) ldl_raw(p)
#define ldq(p) ldq_raw(p)
#define ldfl(p) ldfl_raw(p)
#define ldfq(p) ldfq_raw(p)
#define stb(p, v) stb_raw(p, v)
#define stw(p, v) stw_raw(p, v)
#define stl(p, v) stl_raw(p, v)
#define stq(p, v) stq_raw(p, v)
#define stfl(p, v) stfl_raw(p, v)
#define stfq(p, v) stfq_raw(p, v)

#define cpu_ldub_code(env1, p) ldub_raw(p)
#define cpu_ldsb_code(env1, p) ldsb_raw(p)
#define cpu_lduw_code(env1, p) lduw_raw(p)
#define cpu_ldsw_code(env1, p) ldsw_raw(p)
#define cpu_ldl_code(env1, p) ldl_raw(p)
#define cpu_ldq_code(env1, p) ldq_raw(p)

#define cpu_ldub_data(env, addr) ldub_raw(addr)
#define cpu_lduw_data(env, addr) lduw_raw(addr)
#define cpu_ldsw_data(env, addr) ldsw_raw(addr)
#define cpu_ldl_data(env, addr) ldl_raw(addr)
#define cpu_ldq_data(env, addr) ldq_raw(addr)

#define cpu_stb_data(env, addr, data) stb_raw(addr, data)
#define cpu_stw_data(env, addr, data) stw_raw(addr, data)
#define cpu_stl_data(env, addr, data) stl_raw(addr, data)
#define cpu_stq_data(env, addr, data) stq_raw(addr, data)

#define cpu_ldub_kernel(env, addr) ldub_raw(addr)
#define cpu_lduw_kernel(env, addr) lduw_raw(addr)
#define cpu_ldsw_kernel(env, addr) ldsw_raw(addr)
#define cpu_ldl_kernel(env, addr) ldl_raw(addr)
#define cpu_ldq_kernel(env, addr) ldq_raw(addr)

#define cpu_stb_kernel(env, addr, data) stb_raw(addr, data)
#define cpu_stw_kernel(env, addr, data) stw_raw(addr, data)
#define cpu_stl_kernel(env, addr, data) stl_raw(addr, data)
#define cpu_stq_kernel(env, addr, data) stq_raw(addr, data)

#define ldub_kernel(p) ldub_raw(p)
#define ldsb_kernel(p) ldsb_raw(p)
#define lduw_kernel(p) lduw_raw(p)
#define ldsw_kernel(p) ldsw_raw(p)
#define ldl_kernel(p) ldl_raw(p)
#define ldq_kernel(p) ldq_raw(p)
#define ldfl_kernel(p) ldfl_raw(p)
#define ldfq_kernel(p) ldfq_raw(p)
#define stb_kernel(p, v) stb_raw(p, v)
#define stw_kernel(p, v) stw_raw(p, v)
#define stl_kernel(p, v) stl_raw(p, v)
#define stq_kernel(p, v) stq_raw(p, v)
#define stfl_kernel(p, v) stfl_raw(p, v)
#define stfq_kernel(p, vt) stfq_raw(p, v)

#define cpu_ldub_data(env, addr) ldub_raw(addr)
#define cpu_lduw_data(env, addr) lduw_raw(addr)
#define cpu_ldl_data(env, addr) ldl_raw(addr)

#define cpu_stb_data(env, addr, data) stb_raw(addr, data)
#define cpu_stw_data(env, addr, data) stw_raw(addr, data)
#define cpu_stl_data(env, addr, data) stl_raw(addr, data)

#else

/* XXX: find something cleaner.
 * Furthermore, this is false for 64 bits targets
 */
#define ldul_user       ldl_user
#define ldul_kernel     ldl_kernel
#define ldul_hypv       ldl_hypv
#define ldul_executive  ldl_executive
#define ldul_supervisor ldl_supervisor

/* The memory helpers for tcg-generated code need tcg_target_long etc.  */
#include "tcg.h"

uint8_t helper_ldb_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint16_t helper_ldw_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint32_t helper_ldl_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint64_t helper_ldq_mmu(CPUArchState *env, target_ulong addr, int mmu_idx);

void helper_stb_mmu(CPUArchState *env, target_ulong addr,
                    uint8_t val, int mmu_idx);
void helper_stw_mmu(CPUArchState *env, target_ulong addr,
                    uint16_t val, int mmu_idx);
void helper_stl_mmu(CPUArchState *env, target_ulong addr,
                    uint32_t val, int mmu_idx);
void helper_stq_mmu(CPUArchState *env, target_ulong addr,
                    uint64_t val, int mmu_idx);

uint8_t helper_ldb_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint16_t helper_ldw_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint32_t helper_ldl_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);
uint64_t helper_ldq_cmmu(CPUArchState *env, target_ulong addr, int mmu_idx);

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

#if (NB_MMU_MODES >= 3)

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

#if (NB_MMU_MODES >= 4)

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

#if (NB_MMU_MODES >= 5)

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

#if (NB_MMU_MODES >= 6)

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

#if (NB_MMU_MODES > 6)
#error "NB_MMU_MODES > 6 is not supported for now"
#endif /* (NB_MMU_MODES > 6) */

/* these access are slower, they must be as rare as possible */
#define CPU_MMU_INDEX (cpu_mmu_index(env))
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

#define ldub(p) ldub_data(p)
#define ldsb(p) ldsb_data(p)
#define lduw(p) lduw_data(p)
#define ldsw(p) ldsw_data(p)
#define ldl(p) ldl_data(p)
#define ldq(p) ldq_data(p)

#define stb(p, v) stb_data(p, v)
#define stw(p, v) stw_data(p, v)
#define stl(p, v) stl_data(p, v)
#define stq(p, v) stq_data(p, v)

#define CPU_MMU_INDEX (cpu_mmu_index(env))
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
}

#endif /* defined(CONFIG_USER_ONLY) */

#endif /* CPU_LDST_H */
