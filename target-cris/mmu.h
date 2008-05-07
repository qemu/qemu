#define CRIS_MMU_ERR_EXEC  0
#define CRIS_MMU_ERR_READ  1
#define CRIS_MMU_ERR_WRITE 2
#define CRIS_MMU_ERR_FLUSH 3

struct cris_mmu_result_t
{
	uint32_t phy;
	uint32_t pfn;
	int prot;
	int bf_vec;
};

target_ulong cris_mmu_tlb_latest_update(CPUState *env);
int cris_mmu_translate(struct cris_mmu_result_t *res,
		       CPUState *env, uint32_t vaddr,
		       int rw, int mmu_idx);
