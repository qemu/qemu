#define MOXIE_MMU_ERR_EXEC  0
#define MOXIE_MMU_ERR_READ  1
#define MOXIE_MMU_ERR_WRITE 2
#define MOXIE_MMU_ERR_FLUSH 3

typedef struct {
    uint32_t phy;
    uint32_t pfn;
    int cause_op;
} MoxieMMUResult;

int moxie_mmu_translate(MoxieMMUResult *res,
                        CPUMoxieState *env, uint32_t vaddr,
                        int rw, int mmu_idx);
