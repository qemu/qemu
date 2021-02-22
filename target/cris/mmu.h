#ifndef TARGET_CRIS_MMU_H
#define TARGET_CRIS_MMU_H

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

void cris_mmu_init(CPUCRISState *env);
void cris_mmu_flush_pid(CPUCRISState *env, uint32_t pid);
int cris_mmu_translate(struct cris_mmu_result *res,
                       CPUCRISState *env, uint32_t vaddr,
                       MMUAccessType access_type, int mmu_idx, int debug);

#endif
