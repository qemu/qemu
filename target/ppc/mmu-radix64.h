#ifndef MMU_RADIX64_H
#define MMU_RADIX64_H

#ifndef CONFIG_USER_ONLY

#ifdef TARGET_PPC64

/* Radix Quadrants */
#define R_EADDR_MASK            0x3FFFFFFFFFFFFFFF
#define R_EADDR_VALID_MASK      0xC00FFFFFFFFFFFFF
#define R_EADDR_QUADRANT        0xC000000000000000
#define R_EADDR_QUADRANT0       0x0000000000000000
#define R_EADDR_QUADRANT1       0x4000000000000000
#define R_EADDR_QUADRANT2       0x8000000000000000
#define R_EADDR_QUADRANT3       0xC000000000000000

bool ppc_radix64_xlate(PowerPCCPU *cpu, vaddr eaddr, MMUAccessType access_type,
                       hwaddr *raddr, int *psizep, int *protp, int mmu_idx,
                       bool guest_visible);

#endif /* TARGET_PPC64 */

#endif /* CONFIG_USER_ONLY */

#endif /* MMU_RADIX64_H */
