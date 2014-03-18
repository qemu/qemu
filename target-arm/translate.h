#ifndef TARGET_ARM_TRANSLATE_H
#define TARGET_ARM_TRANSLATE_H

/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
    uint32_t insn;
    int is_jmp;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    int condlabel;
    /* Thumb-2 conditional execution bits.  */
    int condexec_mask;
    int condexec_cond;
    struct TranslationBlock *tb;
    int singlestep_enabled;
    int thumb;
    int bswap_code;
#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
    int vfp_enabled;
    int vec_len;
    int vec_stride;
    int aarch64;
    int current_pl;
    GHashTable *cp_regs;
    uint64_t features; /* CPU features bits */
#define TMP_A64_MAX 16
    int tmp_a64_count;
    TCGv_i64 tmp_a64[TMP_A64_MAX];
} DisasContext;

extern TCGv_ptr cpu_env;

static inline int arm_dc_feature(DisasContext *dc, int feature)
{
    return (dc->features & (1ULL << feature)) != 0;
}

/* target-specific extra values for is_jmp */
/* These instructions trap after executing, so the A32/T32 decoder must
 * defer them until after the conditional execution state has been updated.
 * WFI also needs special handling when single-stepping.
 */
#define DISAS_WFI 4
#define DISAS_SWI 5
/* For instructions which unconditionally cause an exception we can skip
 * emitting unreachable code at the end of the TB in the A64 decoder
 */
#define DISAS_EXC 6
/* WFE */
#define DISAS_WFE 7

#ifdef TARGET_AARCH64
void a64_translate_init(void);
void gen_intermediate_code_internal_a64(ARMCPU *cpu,
                                        TranslationBlock *tb,
                                        bool search_pc);
void gen_a64_set_pc_im(uint64_t val);
#else
static inline void a64_translate_init(void)
{
}

static inline void gen_intermediate_code_internal_a64(ARMCPU *cpu,
                                                      TranslationBlock *tb,
                                                      bool search_pc)
{
}

static inline void gen_a64_set_pc_im(uint64_t val)
{
}
#endif

void arm_gen_test_cc(int cc, int label);

#endif /* TARGET_ARM_TRANSLATE_H */
