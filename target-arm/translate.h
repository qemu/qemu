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
    bool cpacr_fpen; /* FP enabled via CPACR.FPEN */
    bool vfp_enabled; /* FP enabled via FPSCR.EN */
    int vec_len;
    int vec_stride;
    /* Immediate value in AArch32 SVC insn; must be set if is_jmp == DISAS_SWI
     * so that top level loop can generate correct syndrome information.
     */
    uint32_t svc_imm;
    int aarch64;
    int current_el;
    GHashTable *cp_regs;
    uint64_t features; /* CPU features bits */
    /* Because unallocated encodings generate different exception syndrome
     * information from traps due to FP being disabled, we can't do a single
     * "is fp access disabled" check at a high level in the decode tree.
     * To help in catching bugs where the access check was forgotten in some
     * code path, we set this flag when the access check is done, and assert
     * that it is set at the point where we actually touch the FP regs.
     */
    bool fp_access_checked;
    /* ARMv8 single-step state (this is distinct from the QEMU gdbstub
     * single-step support).
     */
    bool ss_active;
    bool pstate_ss;
    /* True if the insn just emitted was a load-exclusive instruction
     * (necessary for syndrome information for single step exceptions),
     * ie A64 LDX*, LDAX*, A32/T32 LDREX*, LDAEX*.
     */
    bool is_ldex;
    /* True if a single-step exception will be taken to the current EL */
    bool ss_same_el;
    /* Bottom two bits of XScale c15_cpar coprocessor access control reg */
    int c15_cpar;
#define TMP_A64_MAX 16
    int tmp_a64_count;
    TCGv_i64 tmp_a64[TMP_A64_MAX];
} DisasContext;

extern TCGv_ptr cpu_env;

static inline int arm_dc_feature(DisasContext *dc, int feature)
{
    return (dc->features & (1ULL << feature)) != 0;
}

static inline int get_mem_index(DisasContext *s)
{
    return s->current_el;
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
#define DISAS_HVC 8
#define DISAS_SMC 9

#ifdef TARGET_AARCH64
void a64_translate_init(void);
void gen_intermediate_code_internal_a64(ARMCPU *cpu,
                                        TranslationBlock *tb,
                                        bool search_pc);
void gen_a64_set_pc_im(uint64_t val);
void aarch64_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags);
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

static inline void aarch64_cpu_dump_state(CPUState *cs, FILE *f,
                                          fprintf_function cpu_fprintf,
                                          int flags)
{
}
#endif

void arm_gen_test_cc(int cc, int label);

#endif /* TARGET_ARM_TRANSLATE_H */
