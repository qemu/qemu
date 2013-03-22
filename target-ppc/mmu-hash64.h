#if !defined (__MMU_HASH64_H__)
#define __MMU_HASH64_H__

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64
void dump_slb(FILE *f, fprintf_function cpu_fprintf, CPUPPCState *env);
int ppc_store_slb (CPUPPCState *env, target_ulong rb, target_ulong rs);
hwaddr ppc_hash64_get_phys_page_debug(CPUPPCState *env, target_ulong addr);
int ppc_hash64_handle_mmu_fault(CPUPPCState *env, target_ulong address, int rw,
                                int mmu_idx);
#endif

/*
 * SLB definitions
 */

/* Bits in the SLB ESID word */
#define SLB_ESID_ESID           0xFFFFFFFFF0000000ULL
#define SLB_ESID_V              0x0000000008000000ULL /* valid */

/* Bits in the SLB VSID word */
#define SLB_VSID_SHIFT          12
#define SLB_VSID_SHIFT_1T       24
#define SLB_VSID_SSIZE_SHIFT    62
#define SLB_VSID_B              0xc000000000000000ULL
#define SLB_VSID_B_256M         0x0000000000000000ULL
#define SLB_VSID_B_1T           0x4000000000000000ULL
#define SLB_VSID_VSID           0x3FFFFFFFFFFFF000ULL
#define SLB_VSID_PTEM           (SLB_VSID_B | SLB_VSID_VSID)
#define SLB_VSID_KS             0x0000000000000800ULL
#define SLB_VSID_KP             0x0000000000000400ULL
#define SLB_VSID_N              0x0000000000000200ULL /* no-execute */
#define SLB_VSID_L              0x0000000000000100ULL
#define SLB_VSID_C              0x0000000000000080ULL /* class */
#define SLB_VSID_LP             0x0000000000000030ULL
#define SLB_VSID_ATTR           0x0000000000000FFFULL

/*
 * Hash page table definitions
 */

#define HPTES_PER_GROUP         8
#define HASH_PTE_SIZE_64        16
#define HASH_PTEG_SIZE_64       (HASH_PTE_SIZE_64 * HPTES_PER_GROUP)

#define HPTE64_V_SSIZE_SHIFT    62
#define HPTE64_V_AVPN_SHIFT     7
#define HPTE64_V_AVPN           0x3fffffffffffff80ULL
#define HPTE64_V_AVPN_VAL(x)    (((x) & HPTE64_V_AVPN) >> HPTE64_V_AVPN_SHIFT)
#define HPTE64_V_COMPARE(x, y)  (!(((x) ^ (y)) & 0xffffffffffffff80ULL))
#define HPTE64_V_LARGE          0x0000000000000004ULL
#define HPTE64_V_SECONDARY      0x0000000000000002ULL
#define HPTE64_V_VALID          0x0000000000000001ULL

#define HPTE64_R_PP0            0x8000000000000000ULL
#define HPTE64_R_TS             0x4000000000000000ULL
#define HPTE64_R_KEY_HI         0x3000000000000000ULL
#define HPTE64_R_RPN_SHIFT      12
#define HPTE64_R_RPN            0x0ffffffffffff000ULL
#define HPTE64_R_FLAGS          0x00000000000003ffULL
#define HPTE64_R_PP             0x0000000000000003ULL
#define HPTE64_R_N              0x0000000000000004ULL
#define HPTE64_R_G              0x0000000000000008ULL
#define HPTE64_R_M              0x0000000000000010ULL
#define HPTE64_R_I              0x0000000000000020ULL
#define HPTE64_R_W              0x0000000000000040ULL
#define HPTE64_R_WIMG           0x0000000000000078ULL
#define HPTE64_R_C              0x0000000000000080ULL
#define HPTE64_R_R              0x0000000000000100ULL
#define HPTE64_R_KEY_LO         0x0000000000000e00ULL
#define HPTE64_R_KEY(x)         ((((x) & HPTE64_R_KEY_HI) >> 60) | \
                                 (((x) & HPTE64_R_KEY_LO) >> 9))

#define HPTE64_V_1TB_SEG        0x4000000000000000ULL
#define HPTE64_V_VRMA_MASK      0x4001ffffff000000ULL

static inline target_ulong ppc_hash64_load_hpte0(CPUPPCState *env,
                                                 hwaddr pte_offset)
{
    if (env->external_htab) {
        return  ldq_p(env->external_htab + pte_offset);
    } else {
        return ldq_phys(env->htab_base + pte_offset);
    }
}

static inline target_ulong ppc_hash64_load_hpte1(CPUPPCState *env,
                                                 hwaddr pte_offset)
{
    if (env->external_htab) {
        return ldq_p(env->external_htab + pte_offset + HASH_PTE_SIZE_64/2);
    } else {
        return ldq_phys(env->htab_base + pte_offset + HASH_PTE_SIZE_64/2);
    }
}

static inline void ppc_hash64_store_hpte0(CPUPPCState *env,
                                          hwaddr pte_offset, target_ulong pte0)
{
    if (env->external_htab) {
        stq_p(env->external_htab + pte_offset, pte0);
    } else {
        stq_phys(env->htab_base + pte_offset, pte0);
    }
}

static inline void ppc_hash64_store_hpte1(CPUPPCState *env,
                                          hwaddr pte_offset, target_ulong pte1)
{
    if (env->external_htab) {
        stq_p(env->external_htab + pte_offset + HASH_PTE_SIZE_64/2, pte1);
    } else {
        stq_phys(env->htab_base + pte_offset + HASH_PTE_SIZE_64/2, pte1);
    }
}

typedef struct {
    uint64_t pte0, pte1;
} ppc_hash_pte64_t;

#endif /* CONFIG_USER_ONLY */

#endif /* !defined (__MMU_HASH64_H__) */
