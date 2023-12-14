#ifndef MMU_HASH64_H
#define MMU_HASH64_H

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64
void dump_slb(PowerPCCPU *cpu);
int ppc_store_slb(PowerPCCPU *cpu, target_ulong slot,
                  target_ulong esid, target_ulong vsid);
bool ppc_hash64_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                      hwaddr *raddrp, int *psizep, int *protp, int mmu_idx,
                      bool guest_visible);
void ppc_hash64_tlb_flush_hpte(PowerPCCPU *cpu,
                               target_ulong pte_index,
                               target_ulong pte0, target_ulong pte1);
unsigned ppc_hash64_hpte_page_shift_noslb(PowerPCCPU *cpu,
                                          uint64_t pte0, uint64_t pte1);
void ppc_hash64_init(PowerPCCPU *cpu);
void ppc_hash64_finalize(PowerPCCPU *cpu);
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
#define SLB_VSID_VRMA           (0x0001FFFFFF000000ULL | SLB_VSID_B_1T)
#define SLB_VSID_PTEM           (SLB_VSID_B | SLB_VSID_VSID)
#define SLB_VSID_KS             0x0000000000000800ULL
#define SLB_VSID_KP             0x0000000000000400ULL
#define SLB_VSID_N              0x0000000000000200ULL /* no-execute */
#define SLB_VSID_L              0x0000000000000100ULL
#define SLB_VSID_L_SHIFT        PPC_BIT_NR(55)
#define SLB_VSID_C              0x0000000000000080ULL /* class */
#define SLB_VSID_LP             0x0000000000000030ULL
#define SLB_VSID_LP_SHIFT       PPC_BIT_NR(59)
#define SLB_VSID_ATTR           0x0000000000000FFFULL
#define SLB_VSID_LLP_MASK       (SLB_VSID_L | SLB_VSID_LP)
#define SLB_VSID_4K             0x0000000000000000ULL
#define SLB_VSID_64K            0x0000000000000110ULL
#define SLB_VSID_16M            0x0000000000000100ULL
#define SLB_VSID_16G            0x0000000000000120ULL

/*
 * Hash page table definitions
 */

#define SDR_64_HTABORG         0x0FFFFFFFFFFC0000ULL
#define SDR_64_HTABSIZE        0x000000000000001FULL

#define PATE0_HTABORG           0x0FFFFFFFFFFC0000ULL
#define PATE0_PS                PPC_BITMASK(56, 58)
#define PATE0_GET_PS(dw0)       (((dw0) & PATE0_PS) >> PPC_BIT_NR(58))

#define HPTES_PER_GROUP         8
#define HASH_PTE_SIZE_64        16
#define HASH_PTEG_SIZE_64       (HASH_PTE_SIZE_64 * HPTES_PER_GROUP)

#define HPTE64_V_SSIZE          SLB_VSID_B
#define HPTE64_V_SSIZE_256M     SLB_VSID_B_256M
#define HPTE64_V_SSIZE_1T       SLB_VSID_B_1T
#define HPTE64_V_SSIZE_SHIFT    62
#define HPTE64_V_AVPN_SHIFT     7
#define HPTE64_V_AVPN           0x3fffffffffffff80ULL
#define HPTE64_V_AVPN_VAL(x)    (((x) & HPTE64_V_AVPN) >> HPTE64_V_AVPN_SHIFT)
#define HPTE64_V_COMPARE(x, y)  (!(((x) ^ (y)) & 0xffffffffffffff83ULL))
#define HPTE64_V_BOLTED         0x0000000000000010ULL
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
#define HPTE64_R_KEY(x)         ((((x) & HPTE64_R_KEY_HI) >> 57) | \
                                 (((x) & HPTE64_R_KEY_LO) >> 9))

#define HPTE64_V_1TB_SEG        0x4000000000000000ULL
#define HPTE64_V_VRMA_MASK      0x4001ffffff000000ULL

/* PTE offsets */
#define HPTE64_DW1              (HASH_PTE_SIZE_64 / 2)
#define HPTE64_DW1_R            (HPTE64_DW1 + 6)
#define HPTE64_DW1_C            (HPTE64_DW1 + 7)

/* Format changes for ARCH v3 */
#define HPTE64_V_COMMON_BITS    0x000fffffffffffffULL
#define HPTE64_R_3_0_SSIZE_SHIFT 58
#define HPTE64_R_3_0_SSIZE_MASK (3ULL << HPTE64_R_3_0_SSIZE_SHIFT)

struct ppc_hash_pte64 {
    uint64_t pte0, pte1;
};

const ppc_hash_pte64_t *ppc_hash64_map_hptes(PowerPCCPU *cpu,
                                             hwaddr ptex, int n);
void ppc_hash64_unmap_hptes(PowerPCCPU *cpu, const ppc_hash_pte64_t *hptes,
                            hwaddr ptex, int n);

static inline uint64_t ppc_hash64_hpte0(PowerPCCPU *cpu,
                                        const ppc_hash_pte64_t *hptes, int i)
{
    return ldq_p(&(hptes[i].pte0));
}

static inline uint64_t ppc_hash64_hpte1(PowerPCCPU *cpu,
                                        const ppc_hash_pte64_t *hptes, int i)
{
    return ldq_p(&(hptes[i].pte1));
}

/*
 * MMU Options
 */

struct PPCHash64PageSize {
    uint32_t page_shift;  /* Page shift (or 0) */
    uint32_t pte_enc;     /* Encoding in the HPTE (>>12) */
};
typedef struct PPCHash64PageSize PPCHash64PageSize;

struct PPCHash64SegmentPageSizes {
    uint32_t page_shift;  /* Base page shift of segment (or 0) */
    uint32_t slb_enc;     /* SLB encoding for BookS */
    PPCHash64PageSize enc[PPC_PAGE_SIZES_MAX_SZ];
};

struct PPCHash64Options {
#define PPC_HASH64_1TSEG        0x00001
#define PPC_HASH64_AMR          0x00002
#define PPC_HASH64_CI_LARGEPAGE 0x00004
    unsigned flags;
    unsigned slb_size;
    PPCHash64SegmentPageSizes sps[PPC_PAGE_SIZES_MAX_SZ];
};

extern const PPCHash64Options ppc_hash64_opts_basic;
extern const PPCHash64Options ppc_hash64_opts_POWER7;

static inline bool ppc_hash64_has(PowerPCCPU *cpu, unsigned feature)
{
    return !!(cpu->hash64_opts->flags & feature);
}

#endif /* CONFIG_USER_ONLY */

#if defined(CONFIG_USER_ONLY) || !defined(TARGET_PPC64)
static inline void ppc_hash64_init(PowerPCCPU *cpu)
{
}
static inline void ppc_hash64_finalize(PowerPCCPU *cpu)
{
}
#endif

#endif /* MMU_HASH64_H */
