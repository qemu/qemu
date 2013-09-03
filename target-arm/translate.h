#ifndef TARGET_ARM_TRANSLATE_H
#define TARGET_ARM_TRANSLATE_H

/* internal defines */
typedef struct DisasContext {
    target_ulong pc;
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

#endif /* TARGET_ARM_TRANSLATE_H */
