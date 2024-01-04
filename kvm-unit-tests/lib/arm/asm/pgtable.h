#ifndef _ASMARM_PGTABLE_H_
#define _ASMARM_PGTABLE_H_
/*
 * Adapted from arch/arm/include/asm/pgtable.h
 *              arch/arm/include/asm/pgtable-3level.h
 *              arch/arm/include/asm/pgalloc.h
 *              include/asm-generic/pgtable-nopud.h
 *
 * Note: some Linux function APIs have been modified. Nothing crazy,
 *       but if a function took, for example, an mm_struct, then
 *       that was either removed or replaced.
 */
#include <alloc.h>
#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable-hwdef.h>

#define pgd_none(pgd)		(!pgd_val(pgd))
#define pud_none(pud)		(!pud_val(pud))
#define pmd_none(pmd)		(!pmd_val(pmd))
#define pte_none(pte)		(!pte_val(pte))

#define pgd_index(addr) \
	(((addr) >> PGDIR_SHIFT) & (PTRS_PER_PGD - 1))
#define pgd_offset(pgtable, addr) ((pgtable) + pgd_index(addr))

#define pgd_free(pgd) free(pgd)
static inline pgd_t *pgd_alloc(void)
{
	pgd_t *pgd = memalign(L1_CACHE_BYTES, PTRS_PER_PGD * sizeof(pgd_t));
	memset(pgd, 0, PTRS_PER_PGD * sizeof(pgd_t));
	return pgd;
}

#define pud_offset(pgd, addr)	((pud_t *)pgd)
#define pud_free(pud)
#define pud_alloc(pgd, addr)	pud_offset(pgd, addr)

static inline pmd_t *pud_page_vaddr(pud_t pud)
{
	return __va(pud_val(pud) & PHYS_MASK & (s32)PAGE_MASK);
}

#define pmd_index(addr) \
	(((addr) >> PMD_SHIFT) & (PTRS_PER_PMD - 1))
#define pmd_offset(pud, addr) \
	(pud_page_vaddr(*(pud)) + pmd_index(addr))

#define pmd_free(pmd) free(pmd)
static inline pmd_t *pmd_alloc_one(void)
{
	pmd_t *pmd = memalign(PAGE_SIZE, PTRS_PER_PMD * sizeof(pmd_t));
	memset(pmd, 0, PTRS_PER_PMD * sizeof(pmd_t));
	return pmd;
}
static inline pmd_t *pmd_alloc(pud_t *pud, unsigned long addr)
{
	if (pud_none(*pud)) {
		pmd_t *pmd = pmd_alloc_one();
		pud_val(*pud) = __pa(pmd) | PMD_TYPE_TABLE;
	}
	return pmd_offset(pud, addr);
}

static inline pte_t *pmd_page_vaddr(pmd_t pmd)
{
	return __va(pmd_val(pmd) & PHYS_MASK & (s32)PAGE_MASK);
}

#define pte_index(addr) \
	(((addr) >> PAGE_SHIFT) & (PTRS_PER_PTE - 1))
#define pte_offset(pmd, addr) \
	(pmd_page_vaddr(*(pmd)) + pte_index(addr))

#define pte_free(pte) free(pte)
static inline pte_t *pte_alloc_one(void)
{
	pte_t *pte = memalign(PAGE_SIZE, PTRS_PER_PTE * sizeof(pte_t));
	memset(pte, 0, PTRS_PER_PTE * sizeof(pte_t));
	return pte;
}
static inline pte_t *pte_alloc(pmd_t *pmd, unsigned long addr)
{
	if (pmd_none(*pmd)) {
		pte_t *pte = pte_alloc_one();
		pmd_val(*pmd) = __pa(pte) | PMD_TYPE_TABLE;
	}
	return pte_offset(pmd, addr);
}

#endif /* _ASMARM_PGTABLE_H_ */
