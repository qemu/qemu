#ifndef MMU_RADIX64_H
#define MMU_RADIX64_H

#ifndef CONFIG_USER_ONLY

/* Radix Quadrants */
#define R_EADDR_MASK            0x3FFFFFFFFFFFFFFF
#define R_EADDR_QUADRANT        0xC000000000000000
#define R_EADDR_QUADRANT0       0x0000000000000000
#define R_EADDR_QUADRANT1       0x4000000000000000
#define R_EADDR_QUADRANT2       0x8000000000000000
#define R_EADDR_QUADRANT3       0xC000000000000000

/* Radix Partition Table Entry Fields */
#define PATE1_R_PRTB           0x0FFFFFFFFFFFF000
#define PATE1_R_PRTS           0x000000000000001F

/* Radix Process Table Entry Fields */
#define PRTBE_R_GET_RTS(rts) \
    ((((rts >> 58) & 0x18) | ((rts >> 5) & 0x7)) + 31)
#define PRTBE_R_RPDB            0x0FFFFFFFFFFFFF00
#define PRTBE_R_RPDS            0x000000000000001F

/* Radix Page Directory/Table Entry Fields */
#define R_PTE_VALID             0x8000000000000000
#define R_PTE_LEAF              0x4000000000000000
#define R_PTE_SW0               0x2000000000000000
#define R_PTE_RPN               0x01FFFFFFFFFFF000
#define R_PTE_SW1               0x0000000000000E00
#define R_GET_SW(sw)            (((sw >> 58) & 0x8) | ((sw >> 9) & 0x7))
#define R_PTE_R                 0x0000000000000100
#define R_PTE_C                 0x0000000000000080
#define R_PTE_ATT               0x0000000000000030
#define R_PTE_ATT_NORMAL        0x0000000000000000
#define R_PTE_ATT_SAO           0x0000000000000010
#define R_PTE_ATT_NI_IO         0x0000000000000020
#define R_PTE_ATT_TOLERANT_IO   0x0000000000000030
#define R_PTE_EAA_PRIV          0x0000000000000008
#define R_PTE_EAA_R             0x0000000000000004
#define R_PTE_EAA_RW            0x0000000000000002
#define R_PTE_EAA_X             0x0000000000000001
#define R_PDE_NLB               PRTBE_R_RPDB
#define R_PDE_NLS               PRTBE_R_RPDS

#ifdef TARGET_PPC64

int ppc_radix64_handle_mmu_fault(PowerPCCPU *cpu, vaddr eaddr, int rwx,
                                 int mmu_idx);
hwaddr ppc_radix64_get_phys_page_debug(PowerPCCPU *cpu, target_ulong addr);

static inline int ppc_radix64_get_prot_eaa(uint64_t pte)
{
    return (pte & R_PTE_EAA_R ? PAGE_READ : 0) |
           (pte & R_PTE_EAA_RW ? PAGE_READ | PAGE_WRITE : 0) |
           (pte & R_PTE_EAA_X ? PAGE_EXEC : 0);
}

static inline int ppc_radix64_get_prot_amr(const PowerPCCPU *cpu)
{
    const CPUPPCState *env = &cpu->env;
    int amr = env->spr[SPR_AMR] >> 62; /* We only care about key0 AMR63:62 */
    int iamr = env->spr[SPR_IAMR] >> 62; /* We only care about key0 IAMR63:62 */

    return (amr & 0x2 ? 0 : PAGE_WRITE) | /* Access denied if bit is set */
           (amr & 0x1 ? 0 : PAGE_READ) |
           (iamr & 0x1 ? 0 : PAGE_EXEC);
}

#endif /* TARGET_PPC64 */

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_RADIX64_H */
