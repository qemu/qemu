#define CRIS_MMU_ERR_EXEC  0
#define CRIS_MMU_ERR_READ  1
#define CRIS_MMU_ERR_WRITE 2
#define CRIS_MMU_ERR_FLUSH 3

struct cris_mmu_result
{
	uint32_t phy;
	int prot;
	int bf_vec;
};

void cris_mmu_init(CPUState *env);
void cris_mmu_flush_pid(CPUState *env, uint32_t pid);
int cris_mmu_translate(struct cris_mmu_result *res,
		       CPUState *env, uint32_t vaddr,
		       int rw, int mmu_idx);
