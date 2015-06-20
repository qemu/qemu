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
} DisasContext;

extern TCGv_ptr cpu_env;

#ifdef TARGET_AARCH64
void a64_translate_init(void);
void disas_a64_insn(CPUARMState *env, DisasContext *s);
void gen_a64_set_pc_im(uint64_t val);
#else
static inline void a64_translate_init(void)
{
}

static inline void disas_a64_insn(CPUARMState *env, DisasContext *s)
{
}

static inline void gen_a64_set_pc_im(uint64_t val)
{
}
#endif

#endif /* TARGET_ARM_TRANSLATE_H */
