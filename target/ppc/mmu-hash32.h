#ifndef MMU_HASH32_H
#define MMU_HASH32_H

#ifndef CONFIG_USER_ONLY

#include "system/memory.h"

bool ppc_hash32_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                      bool guest_visible);

/*
 * Segment register definitions
 */

#define SR32_T                  0x80000000
#define SR32_KS                 0x40000000
#define SR32_KP                 0x20000000
#define SR32_NX                 0x10000000
#define SR32_VSID               0x00ffffff

/*
 * Block Address Translation (BAT) definitions
 */

#define BATU32_BEPIU            0xf0000000
#define BATU32_BEPIL            0x0ffe0000
#define BATU32_BEPI             0xfffe0000
#define BATU32_BL               0x00001ffc
#define BATU32_VS               0x00000002
#define BATU32_VP               0x00000001


#define BATL32_BRPN             0xfffe0000
#define BATL32_WIMG             0x00000078
#define BATL32_PP               0x00000003

/*
 * Hash page table definitions
 */
#define SDR_32_HTABORG         0xFFFF0000UL
#define SDR_32_HTABMASK        0x000001FFUL

#define HPTES_PER_GROUP         8
#define HASH_PTE_SIZE_32        8
#define HASH_PTEG_SIZE_32       (HASH_PTE_SIZE_32 * HPTES_PER_GROUP)

#define HPTE32_V_VALID          0x80000000
#define HPTE32_V_VSID           0x7fffff80
#define HPTE32_V_SECONDARY      0x00000040
#define HPTE32_V_API            0x0000003f
#define HPTE32_V_COMPARE(x, y)  (!(((x) ^ (y)) & 0x7fffffbf))

#define HPTE32_R_RPN            0xfffff000
#define HPTE32_R_R              0x00000100
#define HPTE32_R_C              0x00000080
#define HPTE32_R_W              0x00000040
#define HPTE32_R_I              0x00000020
#define HPTE32_R_M              0x00000010
#define HPTE32_R_G              0x00000008
#define HPTE32_R_WIMG           0x00000078
#define HPTE32_R_PP             0x00000003

static inline hwaddr ppc_hash32_hpt_base(PowerPCCPU *cpu)
{
    return cpu->env.spr[SPR_SDR1] & SDR_32_HTABORG;
}

static inline hwaddr ppc_hash32_hpt_mask(PowerPCCPU *cpu)
{
    return ((cpu->env.spr[SPR_SDR1] & SDR_32_HTABMASK) << 16) | 0xFFFF;
}

static inline target_ulong ppc_hash32_load_hpte0(PowerPCCPU *cpu,
                                                 hwaddr pte_offset)
{
    target_ulong base = ppc_hash32_hpt_base(cpu);

    return ldl_phys(CPU(cpu)->as, base + pte_offset);
}

static inline target_ulong ppc_hash32_load_hpte1(PowerPCCPU *cpu,
                                                 hwaddr pte_offset)
{
    target_ulong base = ppc_hash32_hpt_base(cpu);

    return ldl_phys(CPU(cpu)->as, base + pte_offset + HASH_PTE_SIZE_32 / 2);
}

static inline void ppc_hash32_store_hpte0(PowerPCCPU *cpu,
                                          hwaddr pte_offset, target_ulong pte0)
{
    target_ulong base = ppc_hash32_hpt_base(cpu);

    stl_phys(CPU(cpu)->as, base + pte_offset, pte0);
}

static inline void ppc_hash32_store_hpte1(PowerPCCPU *cpu,
                                          hwaddr pte_offset, target_ulong pte1)
{
    target_ulong base = ppc_hash32_hpt_base(cpu);

    stl_phys(CPU(cpu)->as, base + pte_offset + HASH_PTE_SIZE_32 / 2, pte1);
}

static inline hwaddr get_pteg_offset32(PowerPCCPU *cpu, hwaddr hash)
{
    return (hash * HASH_PTEG_SIZE_32) & ppc_hash32_hpt_mask(cpu);
}

static inline bool ppc_hash32_key(bool pr, target_ulong sr)
{
    return pr ? (sr & SR32_KP) : (sr & SR32_KS);
}

static inline int ppc_hash32_prot(bool key, int pp, bool nx)
{
    int prot;

    if (key) {
        switch (pp) {
        case 0x0:
            prot = 0;
            break;
        case 0x1:
        case 0x3:
            prot = PAGE_READ;
            break;
        case 0x2:
            prot = PAGE_READ | PAGE_WRITE;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        switch (pp) {
        case 0x0:
        case 0x1:
        case 0x2:
            prot = PAGE_READ | PAGE_WRITE;
            break;
        case 0x3:
            prot = PAGE_READ;
            break;
        default:
            g_assert_not_reached();
        }
    }
    return nx ? prot : prot | PAGE_EXEC;
}

static inline int ppc_hash32_bat_prot(target_ulong batu, target_ulong batl)
{
    int prot = 0;
    int pp = batl & BATL32_PP;

    if (pp) {
        prot = PAGE_READ | PAGE_EXEC;
        if (pp == 0x2) {
            prot |= PAGE_WRITE;
        }
    }
    return prot;
}

typedef struct {
    uint32_t pte0, pte1;
} ppc_hash_pte32_t;

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_HASH32_H */
