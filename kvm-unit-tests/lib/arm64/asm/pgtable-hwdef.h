#ifndef _ASMARM64_PGTABLE_HWDEF_H_
#define _ASMARM64_PGTABLE_HWDEF_H_
/*
 * From arch/arm64/include/asm/pgtable-hwdef.h
 *      arch/arm64/include/asm/memory.h
 */
#define UL(x) _AC(x, UL)

#define PTRS_PER_PTE		(1 << (PAGE_SHIFT - 3))

/*
 * PGDIR_SHIFT determines the size a top-level page table entry can map
 * (depending on the configuration, this level can be 0, 1 or 2).
 */
#define PGDIR_SHIFT		((PAGE_SHIFT - 3) * PGTABLE_LEVELS + 3)
#define PGDIR_SIZE		(_AC(1, UL) << PGDIR_SHIFT)
#define PGDIR_MASK		(~(PGDIR_SIZE-1))
#define PTRS_PER_PGD		(1 << (VA_BITS - PGDIR_SHIFT))

/* From include/asm-generic/pgtable-nopud.h */
#define PUD_SHIFT		PGDIR_SHIFT
#define PTRS_PER_PUD		1
#define PUD_SIZE		(UL(1) << PUD_SHIFT)
#define PUD_MASK		(~(PUD_SIZE-1))
/* From include/asm-generic/pgtable-nopmd.h */
#define PMD_SHIFT		PUD_SHIFT
#define PTRS_PER_PMD		1
#define PMD_SIZE		(UL(1) << PMD_SHIFT)
#define PMD_MASK		(~(PMD_SIZE-1))

/*
 * Section address mask and size definitions.
 */
#define SECTION_SHIFT		PMD_SHIFT
#define SECTION_SIZE		(_AC(1, UL) << SECTION_SHIFT)
#define SECTION_MASK		(~(SECTION_SIZE-1))

/*
 * Hardware page table definitions.
 *
 * Level 1 descriptor (PUD).
 */
#define PUD_TYPE_TABLE		(_AT(pudval_t, 3) << 0)
#define PUD_TABLE_BIT		(_AT(pgdval_t, 1) << 1)
#define PUD_TYPE_MASK		(_AT(pgdval_t, 3) << 0)
#define PUD_TYPE_SECT		(_AT(pgdval_t, 1) << 0)

/*
 * Level 2 descriptor (PMD).
 */
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_FAULT		(_AT(pmdval_t, 0) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_TABLE_BIT		(_AT(pmdval_t, 1) << 1)

/*
 * Section
 */
#define PMD_SECT_VALID		(_AT(pmdval_t, 1) << 0)
#define PMD_SECT_PROT_NONE	(_AT(pmdval_t, 1) << 58)
#define PMD_SECT_USER		(_AT(pmdval_t, 1) << 6)		/* AP[1] */
#define PMD_SECT_RDONLY		(_AT(pmdval_t, 1) << 7)		/* AP[2] */
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_NG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_PXN		(_AT(pmdval_t, 1) << 53)
#define PMD_SECT_UXN		(_AT(pmdval_t, 1) << 54)

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PMD_ATTRINDX(t)		(_AT(pmdval_t, (t)) << 2)
#define PMD_ATTRINDX_MASK	(_AT(pmdval_t, 7) << 2)

/*
 * Level 3 descriptor (PTE).
 */
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
#define PTE_TYPE_FAULT		(_AT(pteval_t, 0) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
#define PTE_TABLE_BIT		(_AT(pteval_t, 1) << 1)
#define PTE_USER		(_AT(pteval_t, 1) << 6)		/* AP[1] */
#define PTE_RDONLY		(_AT(pteval_t, 1) << 7)		/* AP[2] */
#define PTE_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define PTE_AF			(_AT(pteval_t, 1) << 10)	/* Access Flag */
#define PTE_NG			(_AT(pteval_t, 1) << 11)	/* nG */
#define PTE_PXN			(_AT(pteval_t, 1) << 53)	/* Privileged XN */
#define PTE_UXN			(_AT(pteval_t, 1) << 54)	/* User XN */

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PTE_ATTRINDX(t)		(_AT(pteval_t, (t)) << 2)
#define PTE_ATTRINDX_MASK	(_AT(pteval_t, 7) << 2)

/*
 * Highest possible physical address supported.
 */
#define PHYS_MASK_SHIFT		(48)
#define PHYS_MASK		((UL(1) << PHYS_MASK_SHIFT) - 1)

/*
 * TCR flags.
 */
#define TCR_TxSZ(x)		(((UL(64) - (x)) << 16) | ((UL(64) - (x)) << 0))
#define TCR_IRGN_NC		((UL(0) << 8) | (UL(0) << 24))
#define TCR_IRGN_WBWA		((UL(1) << 8) | (UL(1) << 24))
#define TCR_IRGN_WT		((UL(2) << 8) | (UL(2) << 24))
#define TCR_IRGN_WBnWA		((UL(3) << 8) | (UL(3) << 24))
#define TCR_IRGN_MASK		((UL(3) << 8) | (UL(3) << 24))
#define TCR_ORGN_NC		((UL(0) << 10) | (UL(0) << 26))
#define TCR_ORGN_WBWA		((UL(1) << 10) | (UL(1) << 26))
#define TCR_ORGN_WT		((UL(2) << 10) | (UL(2) << 26))
#define TCR_ORGN_WBnWA		((UL(3) << 10) | (UL(3) << 26))
#define TCR_ORGN_MASK		((UL(3) << 10) | (UL(3) << 26))
#define TCR_SHARED		((UL(3) << 12) | (UL(3) << 28))
#define TCR_TG0_4K		(UL(0) << 14)
#define TCR_TG0_64K		(UL(1) << 14)
#define TCR_TG0_16K		(UL(2) << 14)
#define TCR_TG1_16K		(UL(1) << 30)
#define TCR_TG1_4K		(UL(2) << 30)
#define TCR_TG1_64K		(UL(3) << 30)
#define TCR_ASID16		(UL(1) << 36)
#define TCR_TBI0		(UL(1) << 37)

/*
 * Memory types available.
 */
#define MT_DEVICE_nGnRnE	0	/* noncached */
#define MT_DEVICE_nGnRE		1	/* device */
#define MT_DEVICE_GRE		2
#define MT_NORMAL_NC		3	/* writecombine */
#define MT_NORMAL		4

#endif /* _ASMARM64_PGTABLE_HWDEF_H_ */
