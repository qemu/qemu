#ifndef TARGET_ARM_TRANSLATE_H
#define TARGET_ARM_TRANSLATE_H

#include "exec/translator.h"


/* internal defines */
typedef struct DisasContext {
    DisasContextBase base;
    const ARMISARegisters *isar;

    target_ulong pc;
    target_ulong page_start;
    uint32_t insn;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    TCGLabel *condlabel;
    /* Thumb-2 conditional execution bits.  */
    int condexec_mask;
    int condexec_cond;
    int thumb;
    int sctlr_b;
    TCGMemOp be_data;
#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
    ARMMMUIdx mmu_idx; /* MMU index to use for normal loads/stores */
    bool tbi0;         /* TBI0 for EL0/1 or TBI for EL2/3 */
    bool tbi1;         /* TBI1 for EL0/1, not used for EL2/3 */
    bool ns;        /* Use non-secure CPREG bank on access */
    int fp_excp_el; /* FP exception EL or 0 if enabled */
    int sve_excp_el; /* SVE exception EL or 0 if enabled */
    int sve_len;     /* SVE vector length in bytes */
    /* Flag indicating that exceptions from secure mode are routed to EL3. */
    bool secure_routed_to_el3;
    bool vfp_enabled; /* FP enabled via FPSCR.EN */
    int vec_len;
    int vec_stride;
    bool v7m_handler_mode;
    bool v8m_secure; /* true if v8M and we're in Secure mode */
    bool v8m_stackcheck; /* true if we need to perform v8M stack limit checks */
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
    /* TCG op of the current insn_start.  */
    TCGOp *insn_start;
#define TMP_A64_MAX 16
    int tmp_a64_count;
    TCGv_i64 tmp_a64[TMP_A64_MAX];
} DisasContext;

typedef struct DisasCompare {
    TCGCond cond;
    TCGv_i32 value;
    bool value_global;
} DisasCompare;

/* Share the TCG temporaries common between 32 and 64 bit modes.  */
extern TCGv_i32 cpu_NF, cpu_ZF, cpu_CF, cpu_VF;
extern TCGv_i64 cpu_exclusive_addr;
extern TCGv_i64 cpu_exclusive_val;

static inline int arm_dc_feature(DisasContext *dc, int feature)
{
    return (dc->features & (1ULL << feature)) != 0;
}

static inline int get_mem_index(DisasContext *s)
{
    return arm_to_core_mmu_idx(s->mmu_idx);
}

/* Function used to determine the target exception EL when otherwise not known
 * or default.
 */
static inline int default_exception_el(DisasContext *s)
{
    /* If we are coming from secure EL0 in a system with a 32-bit EL3, then
     * there is no secure EL1, so we route exceptions to EL3.  Otherwise,
     * exceptions can only be routed to ELs above 1, so we target the higher of
     * 1 or the current EL.
     */
    return (s->mmu_idx == ARMMMUIdx_S1SE0 && s->secure_routed_to_el3)
            ? 3 : MAX(1, s->current_el);
}

static inline void disas_set_insn_syndrome(DisasContext *s, uint32_t syn)
{
    /* We don't need to save all of the syndrome so we mask and shift
     * out unneeded bits to help the sleb128 encoder do a better job.
     */
    syn &= ARM_INSN_START_WORD2_MASK;
    syn >>= ARM_INSN_START_WORD2_SHIFT;

    /* We check and clear insn_start_idx to catch multiple updates.  */
    assert(s->insn_start != NULL);
    tcg_set_insn_start_param(s->insn_start, 2, syn);
    s->insn_start = NULL;
}

/* is_jmp field values */
#define DISAS_JUMP      DISAS_TARGET_0 /* only pc was modified dynamically */
#define DISAS_UPDATE    DISAS_TARGET_1 /* cpu state was modified dynamically */
/* These instructions trap after executing, so the A32/T32 decoder must
 * defer them until after the conditional execution state has been updated.
 * WFI also needs special handling when single-stepping.
 */
#define DISAS_WFI       DISAS_TARGET_2
#define DISAS_SWI       DISAS_TARGET_3
/* WFE */
#define DISAS_WFE       DISAS_TARGET_4
#define DISAS_HVC       DISAS_TARGET_5
#define DISAS_SMC       DISAS_TARGET_6
#define DISAS_YIELD     DISAS_TARGET_7
/* M profile branch which might be an exception return (and so needs
 * custom end-of-TB code)
 */
#define DISAS_BX_EXCRET DISAS_TARGET_8
/* For instructions which want an immediate exit to the main loop,
 * as opposed to attempting to use lookup_and_goto_ptr. Unlike
 * DISAS_UPDATE this doesn't write the PC on exiting the translation
 * loop so you need to ensure something (gen_a64_set_pc_im or runtime
 * helper) has done so before we reach return from cpu_tb_exec.
 */
#define DISAS_EXIT      DISAS_TARGET_9

#ifdef TARGET_AARCH64
void a64_translate_init(void);
void gen_a64_set_pc_im(uint64_t val);
void aarch64_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags);
extern const TranslatorOps aarch64_translator_ops;
#else
static inline void a64_translate_init(void)
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

void arm_test_cc(DisasCompare *cmp, int cc);
void arm_free_cc(DisasCompare *cmp);
void arm_jump_cc(DisasCompare *cmp, TCGLabel *label);
void arm_gen_test_cc(int cc, TCGLabel *label);

/* Return state of Alternate Half-precision flag, caller frees result */
static inline TCGv_i32 get_ahp_flag(void)
{
    TCGv_i32 ret = tcg_temp_new_i32();

    tcg_gen_ld_i32(ret, cpu_env,
                   offsetof(CPUARMState, vfp.xregs[ARM_VFP_FPSCR]));
    tcg_gen_extract_i32(ret, ret, 26, 1);

    return ret;
}


/* Vector operations shared between ARM and AArch64.  */
extern const GVecGen3 bsl_op;
extern const GVecGen3 bit_op;
extern const GVecGen3 bif_op;
extern const GVecGen3 mla_op[4];
extern const GVecGen3 mls_op[4];
extern const GVecGen3 cmtst_op[4];
extern const GVecGen2i ssra_op[4];
extern const GVecGen2i usra_op[4];
extern const GVecGen2i sri_op[4];
extern const GVecGen2i sli_op[4];
void gen_cmtst_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);

/*
 * Forward to the isar_feature_* tests given a DisasContext pointer.
 */
#define dc_isar_feature(name, ctx) \
    ({ DisasContext *ctx_ = (ctx); isar_feature_##name(ctx_->isar); })

#endif /* TARGET_ARM_TRANSLATE_H */
