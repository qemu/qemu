#define CRIS_MMU_ERR_EXEC  0
#define CRIS_MMU_ERR_READ  1
#define CRIS_MMU_ERR_WRITE 2
#define CRIS_MMU_ERR_FLUSH 3

struct cris_mmu_result_t
{
	uint32_t phy;
	uint32_t pfn;
	int g:1;
	int v:1;
	int k:1;
	int w:1;
	int e:1;
	int cause_op;
};

int cris_mmu_translate(struct cris_mmu_result_t *res,
		       CPUState *env, uint32_t vaddr,
		       int rw, int mmu_idx);
