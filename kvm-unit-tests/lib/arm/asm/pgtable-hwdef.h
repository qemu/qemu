#ifndef _ASMARM_PGTABLE_HWDEF_H_
#define _ASMARM_PGTABLE_HWDEF_H_
/*
 * From arch/arm/include/asm/pgtable-3level.h
 *      arch/arm/include/asm/pgtable-3level-hwdef.h
 */

#define PTRS_PER_PGD		4
#define PGDIR_SHIFT		30
#define PGDIR_SIZE		(_AC(1,UL) << PGDIR_SHIFT)
#define PGDIR_MASK		(~((1 << PGDIR_SHIFT) - 1))

#define PTRS_PER_PTE		512
#define PTRS_PER_PMD		512

#define PMD_SHIFT		21
#define PMD_SIZE		(_AC(1,UL) << PMD_SHIFT)
#define PMD_MASK		(~((1 << PMD_SHIFT) - 1))

#define L_PMD_SECT_VALID	(_AT(pmdval_t, 1) << 0)

#define L_PTE_VALID		(_AT(pteval_t, 1) << 0)		/* Valid */
#define L_PTE_PRESENT		(_AT(pteval_t, 3) << 0)		/* Present */
#define L_PTE_USER		(_AT(pteval_t, 1) << 6)		/* AP[1] */
#define L_PTE_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define L_PTE_YOUNG		(_AT(pteval_t, 1) << 10)	/* AF */
#define L_PTE_XN		(_AT(pteval_t, 1) << 54)	/* XN */

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define L_PTE_MT_UNCACHED	(_AT(pteval_t, 0) << 2)	/* strongly ordered */
#define L_PTE_MT_BUFFERABLE	(_AT(pteval_t, 1) << 2)	/* normal non-cacheable */
#define L_PTE_MT_WRITETHROUGH	(_AT(pteval_t, 2) << 2)	/* normal inner write-through */
#define L_PTE_MT_WRITEBACK	(_AT(pteval_t, 3) << 2)	/* normal inner write-back */
#define L_PTE_MT_WRITEALLOC	(_AT(pteval_t, 7) << 2)	/* normal inner write-alloc */
#define L_PTE_MT_DEV_SHARED	(_AT(pteval_t, 4) << 2)	/* device */
#define L_PTE_MT_DEV_NONSHARED	(_AT(pteval_t, 4) << 2)	/* device */
#define L_PTE_MT_DEV_WC		(_AT(pteval_t, 1) << 2)	/* normal non-cacheable */
#define L_PTE_MT_DEV_CACHED	(_AT(pteval_t, 3) << 2)	/* normal inner write-back */
#define L_PTE_MT_MASK		(_AT(pteval_t, 7) << 2)

/*
 * Hardware page table definitions.
 *
 * + Level 1/2 descriptor
 *   - common
 */
#define PMD_TYPE_MASK		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_FAULT		(_AT(pmdval_t, 0) << 0)
#define PMD_TYPE_TABLE		(_AT(pmdval_t, 3) << 0)
#define PMD_TYPE_SECT		(_AT(pmdval_t, 1) << 0)
#define PMD_TABLE_BIT		(_AT(pmdval_t, 1) << 1)
#define PMD_BIT4		(_AT(pmdval_t, 0))
#define PMD_DOMAIN(x)		(_AT(pmdval_t, 0))
#define PMD_APTABLE_SHIFT	(61)
#define PMD_APTABLE		(_AT(pgdval_t, 3) << PGD_APTABLE_SHIFT)
#define PMD_PXNTABLE		(_AT(pgdval_t, 1) << 59)

/*
 *   - section
 */
#define PMD_SECT_BUFFERABLE	(_AT(pmdval_t, 1) << 2)
#define PMD_SECT_CACHEABLE	(_AT(pmdval_t, 1) << 3)
#define PMD_SECT_USER		(_AT(pmdval_t, 1) << 6)		/* AP[1] */
#define PMD_SECT_AP2		(_AT(pmdval_t, 1) << 7)		/* read only */
#define PMD_SECT_S		(_AT(pmdval_t, 3) << 8)
#define PMD_SECT_AF		(_AT(pmdval_t, 1) << 10)
#define PMD_SECT_nG		(_AT(pmdval_t, 1) << 11)
#define PMD_SECT_PXN		(_AT(pmdval_t, 1) << 53)
#define PMD_SECT_XN		(_AT(pmdval_t, 1) << 54)
#define PMD_SECT_AP_WRITE	(_AT(pmdval_t, 0))
#define PMD_SECT_AP_READ	(_AT(pmdval_t, 0))
#define PMD_SECT_AP1		(_AT(pmdval_t, 1) << 6)
#define PMD_SECT_TEX(x)		(_AT(pmdval_t, 0))

/*
 * AttrIndx[2:0] encoding (mapping attributes defined in the MAIR* registers).
 */
#define PMD_SECT_UNCACHED	(_AT(pmdval_t, 0) << 2)	/* strongly ordered */
#define PMD_SECT_BUFFERED	(_AT(pmdval_t, 1) << 2)	/* normal non-cacheable */
#define PMD_SECT_WT		(_AT(pmdval_t, 2) << 2)	/* normal inner write-through */
#define PMD_SECT_WB		(_AT(pmdval_t, 3) << 2)	/* normal inner write-back */
#define PMD_SECT_WBWA		(_AT(pmdval_t, 7) << 2)	/* normal inner write-alloc */

/*
 * + Level 3 descriptor (PTE)
 */
#define PTE_TYPE_MASK		(_AT(pteval_t, 3) << 0)
#define PTE_TYPE_FAULT		(_AT(pteval_t, 0) << 0)
#define PTE_TYPE_PAGE		(_AT(pteval_t, 3) << 0)
#define PTE_TABLE_BIT		(_AT(pteval_t, 1) << 1)
#define PTE_BUFFERABLE		(_AT(pteval_t, 1) << 2)		/* AttrIndx[0] */
#define PTE_CACHEABLE		(_AT(pteval_t, 1) << 3)		/* AttrIndx[1] */
#define PTE_AP2			(_AT(pteval_t, 1) << 7)		/* AP[2] */
#define PTE_EXT_SHARED		(_AT(pteval_t, 3) << 8)		/* SH[1:0], inner shareable */
#define PTE_EXT_AF		(_AT(pteval_t, 1) << 10)	/* Access Flag */
#define PTE_EXT_NG		(_AT(pteval_t, 1) << 11)	/* nG */
#define PTE_EXT_XN		(_AT(pteval_t, 1) << 54)	/* XN */

/*
 * 40-bit physical address supported.
 */
#define PHYS_MASK_SHIFT		(40)
#define PHYS_MASK		((_AC(1, ULL) << PHYS_MASK_SHIFT) - 1)

#endif /* _ASMARM_PGTABLE_HWDEF_H_ */
