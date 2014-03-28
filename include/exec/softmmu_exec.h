/*
 *  Software MMU support
 *
 * Generate inline load/store functions for all MMU modes (typically
 * at least _user and _kernel) as well as _data versions, for all data
 * sizes.
 *
 * Used by target op helpers.
 *
 * MMU mode suffixes are defined in target cpu.h.
 */

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

#define CPU_MMU_INDEX 0
#define MEMSUFFIX MMU_MODE0_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX

#define CPU_MMU_INDEX 1
#define MEMSUFFIX MMU_MODE1_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX

#if (NB_MMU_MODES >= 3)

#define CPU_MMU_INDEX 2
#define MEMSUFFIX MMU_MODE2_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 3) */

#if (NB_MMU_MODES >= 4)

#define CPU_MMU_INDEX 3
#define MEMSUFFIX MMU_MODE3_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 4) */

#if (NB_MMU_MODES >= 5)

#define CPU_MMU_INDEX 4
#define MEMSUFFIX MMU_MODE4_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
#undef CPU_MMU_INDEX
#undef MEMSUFFIX
#endif /* (NB_MMU_MODES >= 5) */

#if (NB_MMU_MODES >= 6)

#define CPU_MMU_INDEX 5
#define MEMSUFFIX MMU_MODE5_SUFFIX
#define DATA_SIZE 1
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
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
#include "exec/softmmu_header.h"

#define DATA_SIZE 2
#include "exec/softmmu_header.h"

#define DATA_SIZE 4
#include "exec/softmmu_header.h"

#define DATA_SIZE 8
#include "exec/softmmu_header.h"
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
