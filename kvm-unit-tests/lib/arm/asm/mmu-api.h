#ifndef __ASMARM_MMU_API_H_
#define __ASMARM_MMU_API_H_
extern pgd_t *mmu_idmap;
extern unsigned int mmu_disabled_cpu_count;
extern bool __mmu_enabled(void);
static inline bool mmu_enabled(void)
{
	return mmu_disabled_cpu_count == 0 || __mmu_enabled();
}
extern void mmu_enable(pgd_t *pgtable);
extern void mmu_mark_disabled(int cpu);
extern void mmu_disable(void);
extern void mmu_enable_idmap(void);
extern void mmu_init_io_sect(pgd_t *pgtable, unsigned long virt_offset);
extern void mmu_set_range_sect(pgd_t *pgtable, unsigned long virt_offset,
			       unsigned long phys_start, unsigned long phys_end,
			       pgprot_t prot);
extern void mmu_set_range_ptes(pgd_t *pgtable, unsigned long virt_offset,
			       unsigned long phys_start, unsigned long phys_end,
			       pgprot_t prot);
#endif
