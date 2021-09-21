#ifndef TARGET_ARM_TRANSLATE_H
#define TARGET_ARM_TRANSLATE_H

#include "exec/translator.h"
#include "internals.h"


/* internal defines */
typedef struct DisasContext {
    DisasContextBase base;
    const ARMISARegisters *isar;

    /* The address of the current instruction being translated. */
    target_ulong pc_curr;
    target_ulong page_start;
    uint32_t insn;
    /* Nonzero if this instruction has been conditionally skipped.  */
    int condjmp;
    /* The label that will be jumped to when the instruction is skipped.  */
    TCGLabel *condlabel;
    /* Thumb-2 conditional execution bits.  */
    int condexec_mask;
    int condexec_cond;
    /* M-profile ECI/ICI exception-continuable instruction state */
    int eci;
    /*
     * trans_ functions for insns which are continuable should set this true
     * after decode (ie after any UNDEF checks)
     */
    bool eci_handled;
    /* TCG op to rewind to if this turns out to be an invalid ECI state */
    TCGOp *insn_eci_rewind;
    int thumb;
    int sctlr_b;
    MemOp be_data;
#if !defined(CONFIG_USER_ONLY)
    int user;
#endif
    ARMMMUIdx mmu_idx; /* MMU index to use for normal loads/stores */
    uint8_t tbii;      /* TBI1|TBI0 for insns */
    uint8_t tbid;      /* TBI1|TBI0 for data */
    uint8_t tcma;      /* TCMA1|TCMA0 for MTE */
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
    bool v8m_fpccr_s_wrong; /* true if v8M FPCCR.S != v8m_secure */
    bool v7m_new_fp_ctxt_needed; /* ASPEN set but no active FP context */
    bool v7m_lspact; /* FPCCR.LSPACT set */
    /* Immediate value in AArch32 SVC insn; must be set if is_jmp == DISAS_SWI
     * so that top level loop can generate correct syndrome information.
     */
    uint32_t svc_imm;
    int aarch64;
    int current_el;
    /* Debug target exception level for single-step exceptions */
    int debug_target_el;
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
    bool sve_access_checked;
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
    /* True if AccType_UNPRIV should be used for LDTR et al */
    bool unpriv;
    /* True if v8.3-PAuth is active.  */
    bool pauth_active;
    /* True if v8.5-MTE access to tags is enabled.  */
    bool ata;
    /* True if v8.5-MTE tag checks affect the PE; index with is_unpriv.  */
    bool mte_active[2];
    /* True with v8.5-BTI and SCTLR_ELx.BT* set.  */
    bool bt;
    /* True if any CP15 access is trapped by HSTR_EL2 */
    bool hstr_active;
    /* True if memory operations require alignment */
    bool align_mem;
    /* True if PSTATE.IL is set */
    bool pstate_il;
    /* True if MVE insns are definitely not predicated by VPR or LTPSIZE */
    bool mve_no_pred;
    /*
     * >= 0, a copy of PSTATE.BTYPE, which will be 0 without v8.5-BTI.
     *  < 0, set by the current instruction.
     */
    int8_t btype;
    /* A copy of cpu->dcz_blocksize. */
    uint8_t dcz_blocksize;
    /* True if this page is guarded.  */
    bool guarded_page;
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

/*
 * Constant expanders for the decoders.
 */

static inline int negate(DisasContext *s, int x)
{
    return -x;
}

static inline int plus_1(DisasContext *s, int x)
{
    return x + 1;
}

static inline int plus_2(DisasContext *s, int x)
{
    return x + 2;
}

static inline int times_2(DisasContext *s, int x)
{
    return x * 2;
}

static inline int times_4(DisasContext *s, int x)
{
    return x * 4;
}

static inline int times_2_plus_1(DisasContext *s, int x)
{
    return x * 2 + 1;
}

static inline int rsub_64(DisasContext *s, int x)
{
    return 64 - x;
}

static inline int rsub_32(DisasContext *s, int x)
{
    return 32 - x;
}

static inline int rsub_16(DisasContext *s, int x)
{
    return 16 - x;
}

static inline int rsub_8(DisasContext *s, int x)
{
    return 8 - x;
}

static inline int neon_3same_fp_size(DisasContext *s, int x)
{
    /* Convert 0==fp32, 1==fp16 into a MO_* value */
    return MO_32 - x;
}

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
    return (s->mmu_idx == ARMMMUIdx_SE10_0 && s->secure_routed_to_el3)
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
/* CPU state was modified dynamically; exit to main loop for interrupts. */
#define DISAS_UPDATE_EXIT  DISAS_TARGET_1
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
/*
 * For instructions which want an immediate exit to the main loop, as opposed
 * to attempting to use lookup_and_goto_ptr.  Unlike DISAS_UPDATE_EXIT, this
 * doesn't write the PC on exiting the translation loop so you need to ensure
 * something (gen_a64_set_pc_im or runtime helper) has done so before we reach
 * return from cpu_tb_exec.
 */
#define DISAS_EXIT      DISAS_TARGET_9
/* CPU state was modified dynamically; no need to exit, but do not chain. */
#define DISAS_UPDATE_NOCHAIN  DISAS_TARGET_10

#ifdef TARGET_AARCH64
void a64_translate_init(void);
void gen_a64_set_pc_im(uint64_t val);
extern const TranslatorOps aarch64_translator_ops;
#else
static inline void a64_translate_init(void)
{
}

static inline void gen_a64_set_pc_im(uint64_t val)
{
}
#endif

void arm_test_cc(DisasCompare *cmp, int cc);
void arm_free_cc(DisasCompare *cmp);
void arm_jump_cc(DisasCompare *cmp, TCGLabel *label);
void arm_gen_test_cc(int cc, TCGLabel *label);
MemOp pow2_align(unsigned i);
void unallocated_encoding(DisasContext *s);
void gen_exception_insn(DisasContext *s, uint64_t pc, int excp,
                        uint32_t syn, uint32_t target_el);

/* Return state of Alternate Half-precision flag, caller frees result */
static inline TCGv_i32 get_ahp_flag(void)
{
    TCGv_i32 ret = tcg_temp_new_i32();

    tcg_gen_ld_i32(ret, cpu_env,
                   offsetof(CPUARMState, vfp.xregs[ARM_VFP_FPSCR]));
    tcg_gen_extract_i32(ret, ret, 26, 1);

    return ret;
}

/* Set bits within PSTATE.  */
static inline void set_pstate_bits(uint32_t bits)
{
    TCGv_i32 p = tcg_temp_new_i32();

    tcg_debug_assert(!(bits & CACHED_PSTATE_BITS));

    tcg_gen_ld_i32(p, cpu_env, offsetof(CPUARMState, pstate));
    tcg_gen_ori_i32(p, p, bits);
    tcg_gen_st_i32(p, cpu_env, offsetof(CPUARMState, pstate));
    tcg_temp_free_i32(p);
}

/* Clear bits within PSTATE.  */
static inline void clear_pstate_bits(uint32_t bits)
{
    TCGv_i32 p = tcg_temp_new_i32();

    tcg_debug_assert(!(bits & CACHED_PSTATE_BITS));

    tcg_gen_ld_i32(p, cpu_env, offsetof(CPUARMState, pstate));
    tcg_gen_andi_i32(p, p, ~bits);
    tcg_gen_st_i32(p, cpu_env, offsetof(CPUARMState, pstate));
    tcg_temp_free_i32(p);
}

/* If the singlestep state is Active-not-pending, advance to Active-pending. */
static inline void gen_ss_advance(DisasContext *s)
{
    if (s->ss_active) {
        s->pstate_ss = 0;
        clear_pstate_bits(PSTATE_SS);
    }
}

static inline void gen_exception(int excp, uint32_t syndrome,
                                 uint32_t target_el)
{
    TCGv_i32 tcg_excp = tcg_const_i32(excp);
    TCGv_i32 tcg_syn = tcg_const_i32(syndrome);
    TCGv_i32 tcg_el = tcg_const_i32(target_el);

    gen_helper_exception_with_syndrome(cpu_env, tcg_excp,
                                       tcg_syn, tcg_el);

    tcg_temp_free_i32(tcg_el);
    tcg_temp_free_i32(tcg_syn);
    tcg_temp_free_i32(tcg_excp);
}

/* Generate an architectural singlestep exception */
static inline void gen_swstep_exception(DisasContext *s, int isv, int ex)
{
    bool same_el = (s->debug_target_el == s->current_el);

    /*
     * If singlestep is targeting a lower EL than the current one,
     * then s->ss_active must be false and we can never get here.
     */
    assert(s->debug_target_el >= s->current_el);

    gen_exception(EXCP_UDEF, syn_swstep(same_el, isv, ex), s->debug_target_el);
}

/*
 * Given a VFP floating point constant encoded into an 8 bit immediate in an
 * instruction, expand it to the actual constant value of the specified
 * size, as per the VFPExpandImm() pseudocode in the Arm ARM.
 */
uint64_t vfp_expand_imm(int size, uint8_t imm8);

/* Vector operations shared between ARM and AArch64.  */
void gen_gvec_ceq0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_clt0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cgt0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cle0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_cge0(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_mla(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_mls(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                  uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_cmtst(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                    uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sshl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_ushl(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_cmtst_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void gen_ushl_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b);
void gen_sshl_i32(TCGv_i32 d, TCGv_i32 a, TCGv_i32 b);
void gen_ushl_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);
void gen_sshl_i64(TCGv_i64 d, TCGv_i64 a, TCGv_i64 b);

void gen_gvec_uqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sqadd_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sqsub_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                       uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_ssra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_usra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                   int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_srshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_urshr(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_srsra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_ursra(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                    int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sri(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sli(unsigned vece, uint32_t rd_ofs, uint32_t rm_ofs,
                  int64_t shift, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sqrdmlah_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_sqrdmlsh_qc(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                          uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_sabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uabd(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

void gen_gvec_saba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);
void gen_gvec_uaba(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz);

/*
 * Forward to the isar_feature_* tests given a DisasContext pointer.
 */
#define dc_isar_feature(name, ctx) \
    ({ DisasContext *ctx_ = (ctx); isar_feature_##name(ctx_->isar); })

/* Note that the gvec expanders operate on offsets + sizes.  */
typedef void GVecGen2Fn(unsigned, uint32_t, uint32_t, uint32_t, uint32_t);
typedef void GVecGen2iFn(unsigned, uint32_t, uint32_t, int64_t,
                         uint32_t, uint32_t);
typedef void GVecGen3Fn(unsigned, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t);
typedef void GVecGen4Fn(unsigned, uint32_t, uint32_t, uint32_t,
                        uint32_t, uint32_t, uint32_t);

/* Function prototype for gen_ functions for calling Neon helpers */
typedef void NeonGenOneOpFn(TCGv_i32, TCGv_i32);
typedef void NeonGenOneOpEnvFn(TCGv_i32, TCGv_ptr, TCGv_i32);
typedef void NeonGenTwoOpFn(TCGv_i32, TCGv_i32, TCGv_i32);
typedef void NeonGenTwoOpEnvFn(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32);
typedef void NeonGenThreeOpEnvFn(TCGv_i32, TCGv_env, TCGv_i32,
                                 TCGv_i32, TCGv_i32);
typedef void NeonGenTwo64OpFn(TCGv_i64, TCGv_i64, TCGv_i64);
typedef void NeonGenTwo64OpEnvFn(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i64);
typedef void NeonGenNarrowFn(TCGv_i32, TCGv_i64);
typedef void NeonGenNarrowEnvFn(TCGv_i32, TCGv_ptr, TCGv_i64);
typedef void NeonGenWidenFn(TCGv_i64, TCGv_i32);
typedef void NeonGenTwoOpWidenFn(TCGv_i64, TCGv_i32, TCGv_i32);
typedef void NeonGenOneSingleOpFn(TCGv_i32, TCGv_i32, TCGv_ptr);
typedef void NeonGenTwoSingleOpFn(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_ptr);
typedef void NeonGenTwoDoubleOpFn(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_ptr);
typedef void NeonGenOne64OpFn(TCGv_i64, TCGv_i64);
typedef void CryptoTwoOpFn(TCGv_ptr, TCGv_ptr);
typedef void CryptoThreeOpIntFn(TCGv_ptr, TCGv_ptr, TCGv_i32);
typedef void CryptoThreeOpFn(TCGv_ptr, TCGv_ptr, TCGv_ptr);
typedef void AtomicThreeOpFn(TCGv_i64, TCGv_i64, TCGv_i64, TCGArg, MemOp);
typedef void WideShiftImmFn(TCGv_i64, TCGv_i64, int64_t shift);
typedef void WideShiftFn(TCGv_i64, TCGv_ptr, TCGv_i64, TCGv_i32);
typedef void ShiftImmFn(TCGv_i32, TCGv_i32, int32_t shift);
typedef void ShiftFn(TCGv_i32, TCGv_ptr, TCGv_i32, TCGv_i32);

/**
 * arm_tbflags_from_tb:
 * @tb: the TranslationBlock
 *
 * Extract the flag values from @tb.
 */
static inline CPUARMTBFlags arm_tbflags_from_tb(const TranslationBlock *tb)
{
    return (CPUARMTBFlags){ tb->flags, tb->cs_base };
}

/*
 * Enum for argument to fpstatus_ptr().
 */
typedef enum ARMFPStatusFlavour {
    FPST_FPCR,
    FPST_FPCR_F16,
    FPST_STD,
    FPST_STD_F16,
} ARMFPStatusFlavour;

/**
 * fpstatus_ptr: return TCGv_ptr to the specified fp_status field
 *
 * We have multiple softfloat float_status fields in the Arm CPU state struct
 * (see the comment in cpu.h for details). Return a TCGv_ptr which has
 * been set up to point to the requested field in the CPU state struct.
 * The options are:
 *
 * FPST_FPCR
 *   for non-FP16 operations controlled by the FPCR
 * FPST_FPCR_F16
 *   for operations controlled by the FPCR where FPCR.FZ16 is to be used
 * FPST_STD
 *   for A32/T32 Neon operations using the "standard FPSCR value"
 * FPST_STD_F16
 *   as FPST_STD, but where FPCR.FZ16 is to be used
 */
static inline TCGv_ptr fpstatus_ptr(ARMFPStatusFlavour flavour)
{
    TCGv_ptr statusptr = tcg_temp_new_ptr();
    int offset;

    switch (flavour) {
    case FPST_FPCR:
        offset = offsetof(CPUARMState, vfp.fp_status);
        break;
    case FPST_FPCR_F16:
        offset = offsetof(CPUARMState, vfp.fp_status_f16);
        break;
    case FPST_STD:
        offset = offsetof(CPUARMState, vfp.standard_fp_status);
        break;
    case FPST_STD_F16:
        offset = offsetof(CPUARMState, vfp.standard_fp_status_f16);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_gen_addi_ptr(statusptr, cpu_env, offset);
    return statusptr;
}

/**
 * finalize_memop:
 * @s: DisasContext
 * @opc: size+sign+align of the memory operation
 *
 * Build the complete MemOp for a memory operation, including alignment
 * and endianness.
 *
 * If (op & MO_AMASK) then the operation already contains the required
 * alignment, e.g. for AccType_ATOMIC.  Otherwise, this an optionally
 * unaligned operation, e.g. for AccType_NORMAL.
 *
 * In the latter case, there are configuration bits that require alignment,
 * and this is applied here.  Note that there is no way to indicate that
 * no alignment should ever be enforced; this must be handled manually.
 */
static inline MemOp finalize_memop(DisasContext *s, MemOp opc)
{
    if (s->align_mem && !(opc & MO_AMASK)) {
        opc |= MO_ALIGN;
    }
    return opc | s->be_data;
}

/**
 * asimd_imm_const: Expand an encoded SIMD constant value
 *
 * Expand a SIMD constant value. This is essentially the pseudocode
 * AdvSIMDExpandImm, except that we also perform the boolean NOT needed for
 * VMVN and VBIC (when cmode < 14 && op == 1).
 *
 * The combination cmode == 15 op == 1 is a reserved encoding for AArch32;
 * callers must catch this; we return the 64-bit constant value defined
 * for AArch64.
 *
 * cmode = 2,3,4,5,6,7,10,11,12,13 imm=0 was UNPREDICTABLE in v7A but
 * is either not unpredictable or merely CONSTRAINED UNPREDICTABLE in v8A;
 * we produce an immediate constant value of 0 in these cases.
 */
uint64_t asimd_imm_const(uint32_t imm, int cmode, int op);

#endif /* TARGET_ARM_TRANSLATE_H */
