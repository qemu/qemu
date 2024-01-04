/*
 * MMU enable and page table manipulation functions
 *
 * Copyright (C) 2014, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#include <asm/setup.h>
#include <asm/thread_info.h>
#include <asm/cpumask.h>
#include <asm/mmu.h>

extern unsigned long etext;

pgd_t *mmu_idmap;

/* CPU 0 starts with disabled MMU */
static cpumask_t mmu_disabled_cpumask = { {1} };
unsigned int mmu_disabled_cpu_count = 1;

bool __mmu_enabled(void)
{
	int cpu = current_thread_info()->cpu;

	return !cpumask_test_cpu(cpu, &mmu_disabled_cpumask);
}

extern void asm_mmu_enable(phys_addr_t pgtable);
void mmu_enable(pgd_t *pgtable)
{
	int cpu = current_thread_info()->cpu;

	asm_mmu_enable(__pa(pgtable));
	flush_tlb_all();

	if (cpumask_test_and_clear_cpu(cpu, &mmu_disabled_cpumask))
		--mmu_disabled_cpu_count;
}

void mmu_mark_disabled(int cpu)
{
	if (!cpumask_test_and_set_cpu(cpu, &mmu_disabled_cpumask))
		++mmu_disabled_cpu_count;
}

extern void asm_mmu_disable(void);
void mmu_disable(void)
{
	int cpu = current_thread_info()->cpu;

	mmu_mark_disabled(cpu);

	asm_mmu_disable();
}

void mmu_set_range_ptes(pgd_t *pgtable, unsigned long virt_offset,
			unsigned long phys_start, unsigned long phys_end,
			pgprot_t prot)
{
	unsigned long vaddr = virt_offset & PAGE_MASK;
	unsigned long paddr = phys_start & PAGE_MASK;
	unsigned long virt_end = phys_end - paddr + vaddr;

	for (; vaddr < virt_end; vaddr += PAGE_SIZE, paddr += PAGE_SIZE) {
		pgd_t *pgd = pgd_offset(pgtable, vaddr);
		pud_t *pud = pud_alloc(pgd, vaddr);
		pmd_t *pmd = pmd_alloc(pud, vaddr);
		pte_t *pte = pte_alloc(pmd, vaddr);

		pte_val(*pte) = paddr;
		pte_val(*pte) |= PTE_TYPE_PAGE | PTE_AF | PTE_SHARED;
		pte_val(*pte) |= pgprot_val(prot);
	}
}

void mmu_set_range_sect(pgd_t *pgtable, unsigned long virt_offset,
			unsigned long phys_start, unsigned long phys_end,
			pgprot_t prot)
{
	unsigned long vaddr = virt_offset & PGDIR_MASK;
	unsigned long paddr = phys_start & PGDIR_MASK;
	unsigned long virt_end = phys_end - paddr + vaddr;

	for (; vaddr < virt_end; vaddr += PGDIR_SIZE, paddr += PGDIR_SIZE) {
		pgd_t *pgd = pgd_offset(pgtable, vaddr);
		pgd_val(*pgd) = paddr;
		pgd_val(*pgd) |= PMD_TYPE_SECT | PMD_SECT_AF | PMD_SECT_S;
		pgd_val(*pgd) |= pgprot_val(prot);
	}
}


void mmu_init_io_sect(pgd_t *pgtable, unsigned long virt_offset)
{
	mmu_set_range_sect(pgtable, virt_offset,
		PHYS_IO_OFFSET, PHYS_IO_END,
		__pgprot(PMD_SECT_UNCACHED | PMD_SECT_USER));
}

void mmu_enable_idmap(void)
{
	unsigned long phys_end = sizeof(long) == 8 || !(PHYS_END >> 32)
						? PHYS_END : 0xfffff000;
	unsigned long code_end = (unsigned long)&etext;

	mmu_idmap = pgd_alloc();

	mmu_init_io_sect(mmu_idmap, PHYS_IO_OFFSET);

	/* armv8 requires code shared between EL1 and EL0 to be read-only */
	mmu_set_range_ptes(mmu_idmap, PHYS_OFFSET,
		PHYS_OFFSET, code_end,
		__pgprot(PTE_WBWA | PTE_RDONLY | PTE_USER));

	mmu_set_range_ptes(mmu_idmap, code_end,
		code_end, phys_end,
		__pgprot(PTE_WBWA | PTE_USER));

	mmu_enable(mmu_idmap);
}
