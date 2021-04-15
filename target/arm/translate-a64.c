/*
 *  AArch64 translation
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "translate.h"
#include "internals.h"
#include "qemu/host-utils.h"

#include "hw/semihosting/semihost.h"
#include "exec/gen-icount.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"

#include "trace-tcg.h"
#include "translate-a64.h"
#include "qemu/atomic128.h"

#include "qemuafl/cpu-translate.h"
#include "qemuafl/qasan-qemu.h"

// SP = 31, LINK = 30

#define AFL_QEMU_TARGET_ARM64_SNIPPET                                         \
  if (is_persistent) {                                                        \
                                                                              \
    if (s->pc_curr == afl_persistent_addr) {                                  \
                                                                              \
      gen_helper_afl_persistent_routine(cpu_env);                             \
                                                                              \
      if (afl_persistent_ret_addr == 0 && !persistent_exits) {                \
                                                                              \
        tcg_gen_movi_tl(cpu_X[30], afl_persistent_addr);                      \
                                                                              \
      }                                                                       \
                                                                              \
      if (!persistent_save_gpr) afl_gen_tcg_plain_call(&afl_persistent_loop); \
                                                                              \
    } else if (afl_persistent_ret_addr &&                                     \
               s->pc_curr == afl_persistent_ret_addr) {                       \
                                                                              \
      gen_goto_tb(s, 0, afl_persistent_addr);                                 \
                                                                              \
    }                                                                         \
                                                                              \
  }

static TCGv_i64 cpu_X[32];
static TCGv_i64 cpu_pc;

/* Load/store exclusive handling */
static TCGv_i64 cpu_exclusive_high;

static const char *regnames[] = {
    "x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7",
    "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15",
    "x16", "x17", "x18", "x19", "x20", "x21", "x22", "x23",
    "x24", "x25", "x26", "x27", "x28", "x29", "lr", "sp"
};

enum a64_shift_type {
    A64_SHIFT_TYPE_LSL = 0,
    A64_SHIFT_TYPE_LSR = 1,
    A64_SHIFT_TYPE_ASR = 2,
    A64_SHIFT_TYPE_ROR = 3
};

/* Table based decoder typedefs - used when the relevant bits for decode
 * are too awkwardly scattered across the instruction (eg SIMD).
 */
typedef void AArch64DecodeFn(DisasContext *s, uint32_t insn);

typedef struct AArch64DecodeTable {
    uint32_t pattern;
    uint32_t mask;
    AArch64DecodeFn *disas_fn;
} AArch64DecodeTable;

/* initialize TCG globals.  */
void a64_translate_init(void)
{
    int i;

    cpu_pc = tcg_global_mem_new_i64(cpu_env,
                                    offsetof(CPUARMState, pc),
                                    "pc");
    for (i = 0; i < 32; i++) {
        cpu_X[i] = tcg_global_mem_new_i64(cpu_env,
                                          offsetof(CPUARMState, xregs[i]),
                                          regnames[i]);
    }

    cpu_exclusive_high = tcg_global_mem_new_i64(cpu_env,
        offsetof(CPUARMState, exclusive_high), "exclusive_high");
}

/*
 * Return the core mmu_idx to use for A64 "unprivileged load/store" insns
 */
static int get_a64_user_mem_index(DisasContext *s)
{
    /*
     * If AccType_UNPRIV is not used, the insn uses AccType_NORMAL,
     * which is the usual mmu_idx for this cpu state.
     */
    ARMMMUIdx useridx = s->mmu_idx;

    if (s->unpriv) {
        /*
         * We have pre-computed the condition for AccType_UNPRIV.
         * Therefore we should never get here with a mmu_idx for
         * which we do not know the corresponding user mmu_idx.
         */
        switch (useridx) {
        case ARMMMUIdx_E10_1:
        case ARMMMUIdx_E10_1_PAN:
            useridx = ARMMMUIdx_E10_0;
            break;
        case ARMMMUIdx_E20_2:
        case ARMMMUIdx_E20_2_PAN:
            useridx = ARMMMUIdx_E20_0;
            break;
        case ARMMMUIdx_SE10_1:
        case ARMMMUIdx_SE10_1_PAN:
            useridx = ARMMMUIdx_SE10_0;
            break;
        case ARMMMUIdx_SE20_2:
        case ARMMMUIdx_SE20_2_PAN:
            useridx = ARMMMUIdx_SE20_0;
            break;
        default:
            g_assert_not_reached();
        }
    }
    return arm_to_core_mmu_idx(useridx);
}

static void reset_btype(DisasContext *s)
{
    if (s->btype != 0) {
        TCGv_i32 zero = tcg_const_i32(0);
        tcg_gen_st_i32(zero, cpu_env, offsetof(CPUARMState, btype));
        tcg_temp_free_i32(zero);
        s->btype = 0;
    }
}

static void set_btype(DisasContext *s, int val)
{
    TCGv_i32 tcg_val;

    /* BTYPE is a 2-bit field, and 0 should be done with reset_btype.  */
    tcg_debug_assert(val >= 1 && val <= 3);

    tcg_val = tcg_const_i32(val);
    tcg_gen_st_i32(tcg_val, cpu_env, offsetof(CPUARMState, btype));
    tcg_temp_free_i32(tcg_val);
    s->btype = -1;
}

void gen_a64_set_pc_im(uint64_t val)
{
    tcg_gen_movi_i64(cpu_pc, val);
}

/*
 * Handle Top Byte Ignore (TBI) bits.
 *
 * If address tagging is enabled via the TCR TBI bits:
 *  + for EL2 and EL3 there is only one TBI bit, and if it is set
 *    then the address is zero-extended, clearing bits [63:56]
 *  + for EL0 and EL1, TBI0 controls addresses with bit 55 == 0
 *    and TBI1 controls addressses with bit 55 == 1.
 *    If the appropriate TBI bit is set for the address then
 *    the address is sign-extended from bit 55 into bits [63:56]
 *
 * Here We have concatenated TBI{1,0} into tbi.
 */
static void gen_top_byte_ignore(DisasContext *s, TCGv_i64 dst,
                                TCGv_i64 src, int tbi)
{
    if (tbi == 0) {
        /* Load unmodified address */
        tcg_gen_mov_i64(dst, src);
    } else if (!regime_has_2_ranges(s->mmu_idx)) {
        /* Force tag byte to all zero */
        tcg_gen_extract_i64(dst, src, 0, 56);
    } else {
        /* Sign-extend from bit 55.  */
        tcg_gen_sextract_i64(dst, src, 0, 56);

        switch (tbi) {
        case 1:
            /* tbi0 but !tbi1: only use the extension if positive */
            tcg_gen_and_i64(dst, dst, src);
            break;
        case 2:
            /* !tbi0 but tbi1: only use the extension if negative */
            tcg_gen_or_i64(dst, dst, src);
            break;
        case 3:
            /* tbi0 and tbi1: always use the extension */
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static void gen_a64_set_pc(DisasContext *s, TCGv_i64 src)
{
    /*
     * If address tagging is enabled for instructions via the TCR TBI bits,
     * then loading an address into the PC will clear out any tag.
     */
    gen_top_byte_ignore(s, cpu_pc, src, s->tbii);
}

/*
 * Handle MTE and/or TBI.
 *
 * For TBI, ideally, we would do nothing.  Proper behaviour on fault is
 * for the tag to be present in the FAR_ELx register.  But for user-only
 * mode we do not have a TLB with which to implement this, so we must
 * remove the top byte now.
 *
 * Always return a fresh temporary that we can increment independently
 * of the write-back address.
 */

TCGv_i64 clean_data_tbi(DisasContext *s, TCGv_i64 addr)
{
    TCGv_i64 clean = new_tmp_a64(s);
#ifdef CONFIG_USER_ONLY
    gen_top_byte_ignore(s, clean, addr, s->tbid);
#else
    tcg_gen_mov_i64(clean, addr);
#endif
    return clean;
}

/* Insert a zero tag into src, with the result at dst. */
static void gen_address_with_allocation_tag0(TCGv_i64 dst, TCGv_i64 src)
{
    tcg_gen_andi_i64(dst, src, ~MAKE_64BIT_MASK(56, 4));
}

static void gen_probe_access(DisasContext *s, TCGv_i64 ptr,
                             MMUAccessType acc, int log2_size)
{
    TCGv_i32 t_acc = tcg_const_i32(acc);
    TCGv_i32 t_idx = tcg_const_i32(get_mem_index(s));
    TCGv_i32 t_size = tcg_const_i32(1 << log2_size);

    gen_helper_probe_access(cpu_env, ptr, t_acc, t_idx, t_size);
    tcg_temp_free_i32(t_acc);
    tcg_temp_free_i32(t_idx);
    tcg_temp_free_i32(t_size);
}

/*
 * For MTE, check a single logical or atomic access.  This probes a single
 * address, the exact one specified.  The size and alignment of the access
 * is not relevant to MTE, per se, but watchpoints do require the size,
 * and we want to recognize those before making any other changes to state.
 */
static TCGv_i64 gen_mte_check1_mmuidx(DisasContext *s, TCGv_i64 addr,
                                      bool is_write, bool tag_checked,
                                      int log2_size, bool is_unpriv,
                                      int core_idx)
{
    if (tag_checked && s->mte_active[is_unpriv]) {
        TCGv_i32 tcg_desc;
        TCGv_i64 ret;
        int desc = 0;

        desc = FIELD_DP32(desc, MTEDESC, MIDX, core_idx);
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, ESIZE, 1 << log2_size);
        tcg_desc = tcg_const_i32(desc);

        ret = new_tmp_a64(s);
        gen_helper_mte_check1(ret, cpu_env, tcg_desc, addr);
        tcg_temp_free_i32(tcg_desc);

        return ret;
    }
    return clean_data_tbi(s, addr);
}

TCGv_i64 gen_mte_check1(DisasContext *s, TCGv_i64 addr, bool is_write,
                        bool tag_checked, int log2_size)
{
    return gen_mte_check1_mmuidx(s, addr, is_write, tag_checked, log2_size,
                                 false, get_mem_index(s));
}

/*
 * For MTE, check multiple logical sequential accesses.
 */
TCGv_i64 gen_mte_checkN(DisasContext *s, TCGv_i64 addr, bool is_write,
                        bool tag_checked, int log2_esize, int total_size)
{
    if (tag_checked && s->mte_active[0] && total_size != (1 << log2_esize)) {
        TCGv_i32 tcg_desc;
        TCGv_i64 ret;
        int desc = 0;

        desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, ESIZE, 1 << log2_esize);
        desc = FIELD_DP32(desc, MTEDESC, TSIZE, total_size);
        tcg_desc = tcg_const_i32(desc);

        ret = new_tmp_a64(s);
        gen_helper_mte_checkN(ret, cpu_env, tcg_desc, addr);
        tcg_temp_free_i32(tcg_desc);

        return ret;
    }
    return gen_mte_check1(s, addr, is_write, tag_checked, log2_esize);
}

typedef struct DisasCompare64 {
    TCGCond cond;
    TCGv_i64 value;
} DisasCompare64;

static void a64_test_cc(DisasCompare64 *c64, int cc)
{
    DisasCompare c32;

    arm_test_cc(&c32, cc);

    /* Sign-extend the 32-bit value so that the GE/LT comparisons work
       * properly.  The NE/EQ comparisons are also fine with this choice.  */
    c64->cond = c32.cond;
    c64->value = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(c64->value, c32.value);

    arm_free_cc(&c32);
}

static void a64_free_cc(DisasCompare64 *c64)
{
    tcg_temp_free_i64(c64->value);
}

static void gen_exception_internal(int excp)
{
    TCGv_i32 tcg_excp = tcg_const_i32(excp);

    assert(excp_is_internal(excp));
    gen_helper_exception_internal(cpu_env, tcg_excp);
    tcg_temp_free_i32(tcg_excp);
}

static void gen_exception_internal_insn(DisasContext *s, uint64_t pc, int excp)
{
    gen_a64_set_pc_im(pc);
    gen_exception_internal(excp);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_insn(DisasContext *s, uint64_t pc, int excp,
                               uint32_t syndrome, uint32_t target_el)
{
    gen_a64_set_pc_im(pc);
    gen_exception(excp, syndrome, target_el);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_bkpt_insn(DisasContext *s, uint32_t syndrome)
{
    TCGv_i32 tcg_syn;

    gen_a64_set_pc_im(s->pc_curr);
    tcg_syn = tcg_const_i32(syndrome);
    gen_helper_exception_bkpt_insn(cpu_env, tcg_syn);
    tcg_temp_free_i32(tcg_syn);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_step_complete_exception(DisasContext *s)
{
    /* We just completed step of an insn. Move from Active-not-pending
     * to Active-pending, and then also take the swstep exception.
     * This corresponds to making the (IMPDEF) choice to prioritize
     * swstep exceptions over asynchronous exceptions taken to an exception
     * level where debug is disabled. This choice has the advantage that
     * we do not need to maintain internal state corresponding to the
     * ISV/EX syndrome bits between completion of the step and generation
     * of the exception, and our syndrome information is always correct.
     */
    gen_ss_advance(s);
    gen_swstep_exception(s, 1, s->is_ldex);
    s->base.is_jmp = DISAS_NORETURN;
}

static inline bool use_goto_tb(DisasContext *s, int n, uint64_t dest)
{
    /* No direct tb linking with singlestep (either QEMU's or the ARM
     * debug architecture kind) or deterministic io
     */
    if (s->base.singlestep_enabled || s->ss_active ||
        (tb_cflags(s->base.tb) & CF_LAST_IO)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    /* Only link tbs from inside the same guest page */
    if ((s->base.tb->pc & TARGET_PAGE_MASK) != (dest & TARGET_PAGE_MASK)) {
        return false;
    }
#endif

    return true;
}

static inline void gen_goto_tb(DisasContext *s, int n, uint64_t dest)
{
    const TranslationBlock *tb;

    tb = s->base.tb;
    if (use_goto_tb(s, n, dest)) {
        tcg_gen_goto_tb(n);
        gen_a64_set_pc_im(dest);
        tcg_gen_exit_tb(tb, n);
        s->base.is_jmp = DISAS_NORETURN;
    } else {
        gen_a64_set_pc_im(dest);
        if (s->ss_active) {
            gen_step_complete_exception(s);
        } else if (s->base.singlestep_enabled) {
            gen_exception_internal(EXCP_DEBUG);
        } else {
            tcg_gen_lookup_and_goto_ptr();
            s->base.is_jmp = DISAS_NORETURN;
        }
    }
}

void unallocated_encoding(DisasContext *s)
{
    /* Unallocated and reserved encodings are uncategorized */
    gen_exception_insn(s, s->pc_curr, EXCP_UDEF, syn_uncategorized(),
                       default_exception_el(s));
}

static void init_tmp_a64_array(DisasContext *s)
{
#ifdef CONFIG_DEBUG_TCG
    memset(s->tmp_a64, 0, sizeof(s->tmp_a64));
#endif
    s->tmp_a64_count = 0;
}

static void free_tmp_a64(DisasContext *s)
{
    int i;
    for (i = 0; i < s->tmp_a64_count; i++) {
        tcg_temp_free_i64(s->tmp_a64[i]);
    }
    init_tmp_a64_array(s);
}

TCGv_i64 new_tmp_a64(DisasContext *s)
{
    assert(s->tmp_a64_count < TMP_A64_MAX);
    return s->tmp_a64[s->tmp_a64_count++] = tcg_temp_new_i64();
}

TCGv_i64 new_tmp_a64_local(DisasContext *s)
{
    assert(s->tmp_a64_count < TMP_A64_MAX);
    return s->tmp_a64[s->tmp_a64_count++] = tcg_temp_local_new_i64();
}

TCGv_i64 new_tmp_a64_zero(DisasContext *s)
{
    TCGv_i64 t = new_tmp_a64(s);
    tcg_gen_movi_i64(t, 0);
    return t;
}

/*
 * Register access functions
 *
 * These functions are used for directly accessing a register in where
 * changes to the final register value are likely to be made. If you
 * need to use a register for temporary calculation (e.g. index type
 * operations) use the read_* form.
 *
 * B1.2.1 Register mappings
 *
 * In instruction register encoding 31 can refer to ZR (zero register) or
 * the SP (stack pointer) depending on context. In QEMU's case we map SP
 * to cpu_X[31] and ZR accesses to a temporary which can be discarded.
 * This is the point of the _sp forms.
 */
TCGv_i64 cpu_reg(DisasContext *s, int reg)
{
    if (reg == 31) {
        return new_tmp_a64_zero(s);
    } else {
        return cpu_X[reg];
    }
}

/* register access for when 31 == SP */
TCGv_i64 cpu_reg_sp(DisasContext *s, int reg)
{
    return cpu_X[reg];
}

/* read a cpu register in 32bit/64bit mode. Returns a TCGv_i64
 * representing the register contents. This TCGv is an auto-freed
 * temporary so it need not be explicitly freed, and may be modified.
 */
TCGv_i64 read_cpu_reg(DisasContext *s, int reg, int sf)
{
    TCGv_i64 v = new_tmp_a64(s);
    if (reg != 31) {
        if (sf) {
            tcg_gen_mov_i64(v, cpu_X[reg]);
        } else {
            tcg_gen_ext32u_i64(v, cpu_X[reg]);
        }
    } else {
        tcg_gen_movi_i64(v, 0);
    }
    return v;
}

TCGv_i64 read_cpu_reg_sp(DisasContext *s, int reg, int sf)
{
    TCGv_i64 v = new_tmp_a64(s);
    if (sf) {
        tcg_gen_mov_i64(v, cpu_X[reg]);
    } else {
        tcg_gen_ext32u_i64(v, cpu_X[reg]);
    }
    return v;
}

/* Return the offset into CPUARMState of a slice (from
 * the least significant end) of FP register Qn (ie
 * Dn, Sn, Hn or Bn).
 * (Note that this is not the same mapping as for A32; see cpu.h)
 */
static inline int fp_reg_offset(DisasContext *s, int regno, MemOp size)
{
    return vec_reg_offset(s, regno, 0, size);
}

/* Offset of the high half of the 128 bit vector Qn */
static inline int fp_reg_hi_offset(DisasContext *s, int regno)
{
    return vec_reg_offset(s, regno, 1, MO_64);
}

/* Convenience accessors for reading and writing single and double
 * FP registers. Writing clears the upper parts of the associated
 * 128 bit vector register, as required by the architecture.
 * Note that unlike the GP register accessors, the values returned
 * by the read functions must be manually freed.
 */
static TCGv_i64 read_fp_dreg(DisasContext *s, int reg)
{
    TCGv_i64 v = tcg_temp_new_i64();

    tcg_gen_ld_i64(v, cpu_env, fp_reg_offset(s, reg, MO_64));
    return v;
}

static TCGv_i32 read_fp_sreg(DisasContext *s, int reg)
{
    TCGv_i32 v = tcg_temp_new_i32();

    tcg_gen_ld_i32(v, cpu_env, fp_reg_offset(s, reg, MO_32));
    return v;
}

static TCGv_i32 read_fp_hreg(DisasContext *s, int reg)
{
    TCGv_i32 v = tcg_temp_new_i32();

    tcg_gen_ld16u_i32(v, cpu_env, fp_reg_offset(s, reg, MO_16));
    return v;
}

/* Clear the bits above an N-bit vector, for N = (is_q ? 128 : 64).
 * If SVE is not enabled, then there are only 128 bits in the vector.
 */
static void clear_vec_high(DisasContext *s, bool is_q, int rd)
{
    unsigned ofs = fp_reg_offset(s, rd, MO_64);
    unsigned vsz = vec_full_reg_size(s);

    /* Nop move, with side effect of clearing the tail. */
    tcg_gen_gvec_mov(MO_64, ofs, ofs, is_q ? 16 : 8, vsz);
}

void write_fp_dreg(DisasContext *s, int reg, TCGv_i64 v)
{
    unsigned ofs = fp_reg_offset(s, reg, MO_64);

    tcg_gen_st_i64(v, cpu_env, ofs);
    clear_vec_high(s, false, reg);
}

static void write_fp_sreg(DisasContext *s, int reg, TCGv_i32 v)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(tmp, v);
    write_fp_dreg(s, reg, tmp);
    tcg_temp_free_i64(tmp);
}

/* Expand a 2-operand AdvSIMD vector operation using an expander function.  */
static void gen_gvec_fn2(DisasContext *s, bool is_q, int rd, int rn,
                         GVecGen2Fn *gvec_fn, int vece)
{
    gvec_fn(vece, vec_full_reg_offset(s, rd), vec_full_reg_offset(s, rn),
            is_q ? 16 : 8, vec_full_reg_size(s));
}

/* Expand a 2-operand + immediate AdvSIMD vector operation using
 * an expander function.
 */
static void gen_gvec_fn2i(DisasContext *s, bool is_q, int rd, int rn,
                          int64_t imm, GVecGen2iFn *gvec_fn, int vece)
{
    gvec_fn(vece, vec_full_reg_offset(s, rd), vec_full_reg_offset(s, rn),
            imm, is_q ? 16 : 8, vec_full_reg_size(s));
}

/* Expand a 3-operand AdvSIMD vector operation using an expander function.  */
static void gen_gvec_fn3(DisasContext *s, bool is_q, int rd, int rn, int rm,
                         GVecGen3Fn *gvec_fn, int vece)
{
    gvec_fn(vece, vec_full_reg_offset(s, rd), vec_full_reg_offset(s, rn),
            vec_full_reg_offset(s, rm), is_q ? 16 : 8, vec_full_reg_size(s));
}

/* Expand a 4-operand AdvSIMD vector operation using an expander function.  */
static void gen_gvec_fn4(DisasContext *s, bool is_q, int rd, int rn, int rm,
                         int rx, GVecGen4Fn *gvec_fn, int vece)
{
    gvec_fn(vece, vec_full_reg_offset(s, rd), vec_full_reg_offset(s, rn),
            vec_full_reg_offset(s, rm), vec_full_reg_offset(s, rx),
            is_q ? 16 : 8, vec_full_reg_size(s));
}

/* Expand a 2-operand operation using an out-of-line helper.  */
static void gen_gvec_op2_ool(DisasContext *s, bool is_q, int rd,
                             int rn, int data, gen_helper_gvec_2 *fn)
{
    tcg_gen_gvec_2_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
}

/* Expand a 3-operand operation using an out-of-line helper.  */
static void gen_gvec_op3_ool(DisasContext *s, bool is_q, int rd,
                             int rn, int rm, int data, gen_helper_gvec_3 *fn)
{
    tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
}

/* Expand a 3-operand + fpstatus pointer + simd data value operation using
 * an out-of-line helper.
 */
static void gen_gvec_op3_fpst(DisasContext *s, bool is_q, int rd, int rn,
                              int rm, bool is_fp16, int data,
                              gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr fpst = fpstatus_ptr(is_fp16 ? FPST_FPCR_F16 : FPST_FPCR);
    tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm), fpst,
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
    tcg_temp_free_ptr(fpst);
}

/* Expand a 3-operand + qc + operation using an out-of-line helper.  */
static void gen_gvec_op3_qc(DisasContext *s, bool is_q, int rd, int rn,
                            int rm, gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr qc_ptr = tcg_temp_new_ptr();

    tcg_gen_addi_ptr(qc_ptr, cpu_env, offsetof(CPUARMState, vfp.qc));
    tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm), qc_ptr,
                       is_q ? 16 : 8, vec_full_reg_size(s), 0, fn);
    tcg_temp_free_ptr(qc_ptr);
}

/* Set ZF and NF based on a 64 bit result. This is alas fiddlier
 * than the 32 bit equivalent.
 */
static inline void gen_set_NZ64(TCGv_i64 result)
{
    tcg_gen_extr_i64_i32(cpu_ZF, cpu_NF, result);
    tcg_gen_or_i32(cpu_ZF, cpu_ZF, cpu_NF);
}

/* Set NZCV as for a logical operation: NZ as per result, CV cleared. */
static inline void gen_logic_CC(int sf, TCGv_i64 result)
{
    if (sf) {
        gen_set_NZ64(result);
    } else {
        tcg_gen_extrl_i64_i32(cpu_ZF, result);
        tcg_gen_mov_i32(cpu_NF, cpu_ZF);
    }
    tcg_gen_movi_i32(cpu_CF, 0);
    tcg_gen_movi_i32(cpu_VF, 0);
}

/* dest = T0 + T1; compute C, N, V and Z flags */
static void gen_add_CC(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    if (sf) {
        TCGv_i64 result, flag, tmp;
        result = tcg_temp_new_i64();
        flag = tcg_temp_new_i64();
        tmp = tcg_temp_new_i64();

        tcg_gen_movi_i64(tmp, 0);
        tcg_gen_add2_i64(result, flag, t0, tmp, t1, tmp);

        tcg_gen_extrl_i64_i32(cpu_CF, flag);

        gen_set_NZ64(result);

        tcg_gen_xor_i64(flag, result, t0);
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_andc_i64(flag, flag, tmp);
        tcg_temp_free_i64(tmp);
        tcg_gen_extrh_i64_i32(cpu_VF, flag);

        tcg_gen_mov_i64(dest, result);
        tcg_temp_free_i64(result);
        tcg_temp_free_i64(flag);
    } else {
        /* 32 bit arithmetic */
        TCGv_i32 t0_32 = tcg_temp_new_i32();
        TCGv_i32 t1_32 = tcg_temp_new_i32();
        TCGv_i32 tmp = tcg_temp_new_i32();

        tcg_gen_movi_i32(tmp, 0);
        tcg_gen_extrl_i64_i32(t0_32, t0);
        tcg_gen_extrl_i64_i32(t1_32, t1);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, t0_32, tmp, t1_32, tmp);
        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
        tcg_gen_xor_i32(cpu_VF, cpu_NF, t0_32);
        tcg_gen_xor_i32(tmp, t0_32, t1_32);
        tcg_gen_andc_i32(cpu_VF, cpu_VF, tmp);
        tcg_gen_extu_i32_i64(dest, cpu_NF);

        tcg_temp_free_i32(tmp);
        tcg_temp_free_i32(t0_32);
        tcg_temp_free_i32(t1_32);
    }
}

/* dest = T0 - T1; compute C, N, V and Z flags */
static void gen_sub_CC(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    if (sf) {
        /* 64 bit arithmetic */
        TCGv_i64 result, flag, tmp;

        result = tcg_temp_new_i64();
        flag = tcg_temp_new_i64();
        tcg_gen_sub_i64(result, t0, t1);

        gen_set_NZ64(result);

        tcg_gen_setcond_i64(TCG_COND_GEU, flag, t0, t1);
        tcg_gen_extrl_i64_i32(cpu_CF, flag);

        tcg_gen_xor_i64(flag, result, t0);
        tmp = tcg_temp_new_i64();
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_and_i64(flag, flag, tmp);
        tcg_temp_free_i64(tmp);
        tcg_gen_extrh_i64_i32(cpu_VF, flag);
        tcg_gen_mov_i64(dest, result);
        tcg_temp_free_i64(flag);
        tcg_temp_free_i64(result);
    } else {
        /* 32 bit arithmetic */
        TCGv_i32 t0_32 = tcg_temp_new_i32();
        TCGv_i32 t1_32 = tcg_temp_new_i32();
        TCGv_i32 tmp;

        tcg_gen_extrl_i64_i32(t0_32, t0);
        tcg_gen_extrl_i64_i32(t1_32, t1);
        tcg_gen_sub_i32(cpu_NF, t0_32, t1_32);
        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
        tcg_gen_setcond_i32(TCG_COND_GEU, cpu_CF, t0_32, t1_32);
        tcg_gen_xor_i32(cpu_VF, cpu_NF, t0_32);
        tmp = tcg_temp_new_i32();
        tcg_gen_xor_i32(tmp, t0_32, t1_32);
        tcg_temp_free_i32(t0_32);
        tcg_temp_free_i32(t1_32);
        tcg_gen_and_i32(cpu_VF, cpu_VF, tmp);
        tcg_temp_free_i32(tmp);
        tcg_gen_extu_i32_i64(dest, cpu_NF);
    }
}

/* dest = T0 + T1 + CF; do not compute flags. */
static void gen_adc(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    TCGv_i64 flag = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(flag, cpu_CF);
    tcg_gen_add_i64(dest, t0, t1);
    tcg_gen_add_i64(dest, dest, flag);
    tcg_temp_free_i64(flag);

    if (!sf) {
        tcg_gen_ext32u_i64(dest, dest);
    }
}

/* dest = T0 + T1 + CF; compute C, N, V and Z flags. */
static void gen_adc_CC(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    if (sf) {
        TCGv_i64 result, cf_64, vf_64, tmp;
        result = tcg_temp_new_i64();
        cf_64 = tcg_temp_new_i64();
        vf_64 = tcg_temp_new_i64();
        tmp = tcg_const_i64(0);

        tcg_gen_extu_i32_i64(cf_64, cpu_CF);
        tcg_gen_add2_i64(result, cf_64, t0, tmp, cf_64, tmp);
        tcg_gen_add2_i64(result, cf_64, result, cf_64, t1, tmp);
        tcg_gen_extrl_i64_i32(cpu_CF, cf_64);
        gen_set_NZ64(result);

        tcg_gen_xor_i64(vf_64, result, t0);
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_andc_i64(vf_64, vf_64, tmp);
        tcg_gen_extrh_i64_i32(cpu_VF, vf_64);

        tcg_gen_mov_i64(dest, result);

        tcg_temp_free_i64(tmp);
        tcg_temp_free_i64(vf_64);
        tcg_temp_free_i64(cf_64);
        tcg_temp_free_i64(result);
    } else {
        TCGv_i32 t0_32, t1_32, tmp;
        t0_32 = tcg_temp_new_i32();
        t1_32 = tcg_temp_new_i32();
        tmp = tcg_const_i32(0);

        tcg_gen_extrl_i64_i32(t0_32, t0);
        tcg_gen_extrl_i64_i32(t1_32, t1);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, t0_32, tmp, cpu_CF, tmp);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, cpu_NF, cpu_CF, t1_32, tmp);

        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
        tcg_gen_xor_i32(cpu_VF, cpu_NF, t0_32);
        tcg_gen_xor_i32(tmp, t0_32, t1_32);
        tcg_gen_andc_i32(cpu_VF, cpu_VF, tmp);
        tcg_gen_extu_i32_i64(dest, cpu_NF);

        tcg_temp_free_i32(tmp);
        tcg_temp_free_i32(t1_32);
        tcg_temp_free_i32(t0_32);
    }
}

/*
 * Load/Store generators
 */

/*
 * Store from GPR register to memory.
 */
static void do_gpr_st_memidx(DisasContext *s, TCGv_i64 source,
                             TCGv_i64 tcg_addr, int size, int memidx,
                             bool iss_valid,
                             unsigned int iss_srt,
                             bool iss_sf, bool iss_ar)
{
    g_assert(size <= 3);
    tcg_gen_qemu_st_i64(source, tcg_addr, memidx, s->be_data + size);

    if (iss_valid) {
        uint32_t syn;

        syn = syn_data_abort_with_iss(0,
                                      size,
                                      false,
                                      iss_srt,
                                      iss_sf,
                                      iss_ar,
                                      0, 0, 0, 0, 0, false);
        disas_set_insn_syndrome(s, syn);
    }
}

static void do_gpr_st(DisasContext *s, TCGv_i64 source,
                      TCGv_i64 tcg_addr, int size,
                      bool iss_valid,
                      unsigned int iss_srt,
                      bool iss_sf, bool iss_ar)
{
    do_gpr_st_memidx(s, source, tcg_addr, size, get_mem_index(s),
                     iss_valid, iss_srt, iss_sf, iss_ar);
}

/*
 * Load from memory to GPR register
 */
static void do_gpr_ld_memidx(DisasContext *s,
                             TCGv_i64 dest, TCGv_i64 tcg_addr,
                             int size, bool is_signed,
                             bool extend, int memidx,
                             bool iss_valid, unsigned int iss_srt,
                             bool iss_sf, bool iss_ar)
{
    MemOp memop = s->be_data + size;

    g_assert(size <= 3);

    if (is_signed) {
        memop += MO_SIGN;
    }

    tcg_gen_qemu_ld_i64(dest, tcg_addr, memidx, memop);

    if (extend && is_signed) {
        g_assert(size < 3);
        tcg_gen_ext32u_i64(dest, dest);
    }

    if (iss_valid) {
        uint32_t syn;

        syn = syn_data_abort_with_iss(0,
                                      size,
                                      is_signed,
                                      iss_srt,
                                      iss_sf,
                                      iss_ar,
                                      0, 0, 0, 0, 0, false);
        disas_set_insn_syndrome(s, syn);
    }
}

static void do_gpr_ld(DisasContext *s,
                      TCGv_i64 dest, TCGv_i64 tcg_addr,
                      int size, bool is_signed, bool extend,
                      bool iss_valid, unsigned int iss_srt,
                      bool iss_sf, bool iss_ar)
{
    do_gpr_ld_memidx(s, dest, tcg_addr, size, is_signed, extend,
                     get_mem_index(s),
                     iss_valid, iss_srt, iss_sf, iss_ar);
}

/*
 * Store from FP register to memory
 */
static void do_fp_st(DisasContext *s, int srcidx, TCGv_i64 tcg_addr, int size)
{
    /* This writes the bottom N bits of a 128 bit wide vector to memory */
    TCGv_i64 tmp = tcg_temp_new_i64();
    tcg_gen_ld_i64(tmp, cpu_env, fp_reg_offset(s, srcidx, MO_64));
    if (size < 4) {
        tcg_gen_qemu_st_i64(tmp, tcg_addr, get_mem_index(s),
                            s->be_data + size);
    } else {
        bool be = s->be_data == MO_BE;
        TCGv_i64 tcg_hiaddr = tcg_temp_new_i64();

        tcg_gen_addi_i64(tcg_hiaddr, tcg_addr, 8);
        tcg_gen_qemu_st_i64(tmp, be ? tcg_hiaddr : tcg_addr, get_mem_index(s),
                            s->be_data | MO_Q);
        tcg_gen_ld_i64(tmp, cpu_env, fp_reg_hi_offset(s, srcidx));
        tcg_gen_qemu_st_i64(tmp, be ? tcg_addr : tcg_hiaddr, get_mem_index(s),
                            s->be_data | MO_Q);
        tcg_temp_free_i64(tcg_hiaddr);
    }

    tcg_temp_free_i64(tmp);
}

/*
 * Load from memory to FP register
 */
static void do_fp_ld(DisasContext *s, int destidx, TCGv_i64 tcg_addr, int size)
{
    /* This always zero-extends and writes to a full 128 bit wide vector */
    TCGv_i64 tmplo = tcg_temp_new_i64();
    TCGv_i64 tmphi = NULL;

    if (size < 4) {
        MemOp memop = s->be_data + size;
        tcg_gen_qemu_ld_i64(tmplo, tcg_addr, get_mem_index(s), memop);
    } else {
        bool be = s->be_data == MO_BE;
        TCGv_i64 tcg_hiaddr;

        tmphi = tcg_temp_new_i64();
        tcg_hiaddr = tcg_temp_new_i64();

        tcg_gen_addi_i64(tcg_hiaddr, tcg_addr, 8);
        tcg_gen_qemu_ld_i64(tmplo, be ? tcg_hiaddr : tcg_addr, get_mem_index(s),
                            s->be_data | MO_Q);
        tcg_gen_qemu_ld_i64(tmphi, be ? tcg_addr : tcg_hiaddr, get_mem_index(s),
                            s->be_data | MO_Q);
        tcg_temp_free_i64(tcg_hiaddr);
    }

    tcg_gen_st_i64(tmplo, cpu_env, fp_reg_offset(s, destidx, MO_64));
    tcg_temp_free_i64(tmplo);

    if (tmphi) {
        tcg_gen_st_i64(tmphi, cpu_env, fp_reg_hi_offset(s, destidx));
        tcg_temp_free_i64(tmphi);
    }
    clear_vec_high(s, tmphi != NULL, destidx);
}

/*
 * Vector load/store helpers.
 *
 * The principal difference between this and a FP load is that we don't
 * zero extend as we are filling a partial chunk of the vector register.
 * These functions don't support 128 bit loads/stores, which would be
 * normal load/store operations.
 *
 * The _i32 versions are useful when operating on 32 bit quantities
 * (eg for floating point single or using Neon helper functions).
 */

/* Get value of an element within a vector register */
static void read_vec_element(DisasContext *s, TCGv_i64 tcg_dest, int srcidx,
                             int element, MemOp memop)
{
    int vect_off = vec_reg_offset(s, srcidx, element, memop & MO_SIZE);
    switch (memop) {
    case MO_8:
        tcg_gen_ld8u_i64(tcg_dest, cpu_env, vect_off);
        break;
    case MO_16:
        tcg_gen_ld16u_i64(tcg_dest, cpu_env, vect_off);
        break;
    case MO_32:
        tcg_gen_ld32u_i64(tcg_dest, cpu_env, vect_off);
        break;
    case MO_8|MO_SIGN:
        tcg_gen_ld8s_i64(tcg_dest, cpu_env, vect_off);
        break;
    case MO_16|MO_SIGN:
        tcg_gen_ld16s_i64(tcg_dest, cpu_env, vect_off);
        break;
    case MO_32|MO_SIGN:
        tcg_gen_ld32s_i64(tcg_dest, cpu_env, vect_off);
        break;
    case MO_64:
    case MO_64|MO_SIGN:
        tcg_gen_ld_i64(tcg_dest, cpu_env, vect_off);
        break;
    default:
        g_assert_not_reached();
    }
}

static void read_vec_element_i32(DisasContext *s, TCGv_i32 tcg_dest, int srcidx,
                                 int element, MemOp memop)
{
    int vect_off = vec_reg_offset(s, srcidx, element, memop & MO_SIZE);
    switch (memop) {
    case MO_8:
        tcg_gen_ld8u_i32(tcg_dest, cpu_env, vect_off);
        break;
    case MO_16:
        tcg_gen_ld16u_i32(tcg_dest, cpu_env, vect_off);
        break;
    case MO_8|MO_SIGN:
        tcg_gen_ld8s_i32(tcg_dest, cpu_env, vect_off);
        break;
    case MO_16|MO_SIGN:
        tcg_gen_ld16s_i32(tcg_dest, cpu_env, vect_off);
        break;
    case MO_32:
    case MO_32|MO_SIGN:
        tcg_gen_ld_i32(tcg_dest, cpu_env, vect_off);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Set value of an element within a vector register */
static void write_vec_element(DisasContext *s, TCGv_i64 tcg_src, int destidx,
                              int element, MemOp memop)
{
    int vect_off = vec_reg_offset(s, destidx, element, memop & MO_SIZE);
    switch (memop) {
    case MO_8:
        tcg_gen_st8_i64(tcg_src, cpu_env, vect_off);
        break;
    case MO_16:
        tcg_gen_st16_i64(tcg_src, cpu_env, vect_off);
        break;
    case MO_32:
        tcg_gen_st32_i64(tcg_src, cpu_env, vect_off);
        break;
    case MO_64:
        tcg_gen_st_i64(tcg_src, cpu_env, vect_off);
        break;
    default:
        g_assert_not_reached();
    }
}

static void write_vec_element_i32(DisasContext *s, TCGv_i32 tcg_src,
                                  int destidx, int element, MemOp memop)
{
    int vect_off = vec_reg_offset(s, destidx, element, memop & MO_SIZE);
    switch (memop) {
    case MO_8:
        tcg_gen_st8_i32(tcg_src, cpu_env, vect_off);
        break;
    case MO_16:
        tcg_gen_st16_i32(tcg_src, cpu_env, vect_off);
        break;
    case MO_32:
        tcg_gen_st_i32(tcg_src, cpu_env, vect_off);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Store from vector register to memory */
static void do_vec_st(DisasContext *s, int srcidx, int element,
                      TCGv_i64 tcg_addr, int size, MemOp endian)
{
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();

    read_vec_element(s, tcg_tmp, srcidx, element, size);
    tcg_gen_qemu_st_i64(tcg_tmp, tcg_addr, get_mem_index(s), endian | size);

    tcg_temp_free_i64(tcg_tmp);
}

/* Load from memory to vector register */
static void do_vec_ld(DisasContext *s, int destidx, int element,
                      TCGv_i64 tcg_addr, int size, MemOp endian)
{
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i64(tcg_tmp, tcg_addr, get_mem_index(s), endian | size);
    write_vec_element(s, tcg_tmp, destidx, element, size);

    tcg_temp_free_i64(tcg_tmp);
}

/* Check that FP/Neon access is enabled. If it is, return
 * true. If not, emit code to generate an appropriate exception,
 * and return false; the caller should not emit any code for
 * the instruction. Note that this check must happen after all
 * unallocated-encoding checks (otherwise the syndrome information
 * for the resulting exception will be incorrect).
 */
static bool fp_access_check(DisasContext *s)
{
    if (s->fp_excp_el) {
        assert(!s->fp_access_checked);
        s->fp_access_checked = true;

        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_fp_access_trap(1, 0xe, false), s->fp_excp_el);
        return false;
    }
    s->fp_access_checked = true;
    return true;
}

/* Check that SVE access is enabled.  If it is, return true.
 * If not, emit code to generate an appropriate exception and return false.
 */
bool sve_access_check(DisasContext *s)
{
    if (s->sve_excp_el) {
        assert(!s->sve_access_checked);
        s->sve_access_checked = true;

        gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                           syn_sve_access_trap(), s->sve_excp_el);
        return false;
    }
    s->sve_access_checked = true;
    return fp_access_check(s);
}

/*
 * This utility function is for doing register extension with an
 * optional shift. You will likely want to pass a temporary for the
 * destination register. See DecodeRegExtend() in the ARM ARM.
 */
static void ext_and_shift_reg(TCGv_i64 tcg_out, TCGv_i64 tcg_in,
                              int option, unsigned int shift)
{
    int extsize = extract32(option, 0, 2);
    bool is_signed = extract32(option, 2, 1);

    if (is_signed) {
        switch (extsize) {
        case 0:
            tcg_gen_ext8s_i64(tcg_out, tcg_in);
            break;
        case 1:
            tcg_gen_ext16s_i64(tcg_out, tcg_in);
            break;
        case 2:
            tcg_gen_ext32s_i64(tcg_out, tcg_in);
            break;
        case 3:
            tcg_gen_mov_i64(tcg_out, tcg_in);
            break;
        }
    } else {
        switch (extsize) {
        case 0:
            tcg_gen_ext8u_i64(tcg_out, tcg_in);
            break;
        case 1:
            tcg_gen_ext16u_i64(tcg_out, tcg_in);
            break;
        case 2:
            tcg_gen_ext32u_i64(tcg_out, tcg_in);
            break;
        case 3:
            tcg_gen_mov_i64(tcg_out, tcg_in);
            break;
        }
    }

    if (shift) {
        tcg_gen_shli_i64(tcg_out, tcg_out, shift);
    }
}

static inline void gen_check_sp_alignment(DisasContext *s)
{
    /* The AArch64 architecture mandates that (if enabled via PSTATE
     * or SCTLR bits) there is a check that SP is 16-aligned on every
     * SP-relative load or store (with an exception generated if it is not).
     * In line with general QEMU practice regarding misaligned accesses,
     * we omit these checks for the sake of guest program performance.
     * This function is provided as a hook so we can more easily add these
     * checks in future (possibly as a "favour catching guest program bugs
     * over speed" user selectable option).
     */
}

/*
 * This provides a simple table based table lookup decoder. It is
 * intended to be used when the relevant bits for decode are too
 * awkwardly placed and switch/if based logic would be confusing and
 * deeply nested. Since it's a linear search through the table, tables
 * should be kept small.
 *
 * It returns the first handler where insn & mask == pattern, or
 * NULL if there is no match.
 * The table is terminated by an empty mask (i.e. 0)
 */
static inline AArch64DecodeFn *lookup_disas_fn(const AArch64DecodeTable *table,
                                               uint32_t insn)
{
    const AArch64DecodeTable *tptr = table;

    while (tptr->mask) {
        if ((insn & tptr->mask) == tptr->pattern) {
            return tptr->disas_fn;
        }
        tptr++;
    }
    return NULL;
}

/*
 * The instruction disassembly implemented here matches
 * the instruction encoding classifications in chapter C4
 * of the ARM Architecture Reference Manual (DDI0487B_a);
 * classification names and decode diagrams here should generally
 * match up with those in the manual.
 */

/* Unconditional branch (immediate)
 *   31  30       26 25                                  0
 * +----+-----------+-------------------------------------+
 * | op | 0 0 1 0 1 |                 imm26               |
 * +----+-----------+-------------------------------------+
 */
static void disas_uncond_b_imm(DisasContext *s, uint32_t insn)
{
    uint64_t addr = s->pc_curr + sextract32(insn, 0, 26) * 4;

    if (insn & (1U << 31)) {
        /* BL Branch with link */
        if (use_qasan && qasan_max_call_stack)
          gen_helper_qasan_shadow_stack_push(tcg_const_tl(s->pc_curr));
        tcg_gen_movi_i64(cpu_reg(s, 30), s->base.pc_next);
    }

    /* B Branch / BL Branch with link */
    reset_btype(s);
    gen_goto_tb(s, 0, addr);
}

/* Compare and branch (immediate)
 *   31  30         25  24  23                  5 4      0
 * +----+-------------+----+---------------------+--------+
 * | sf | 0 1 1 0 1 0 | op |         imm19       |   Rt   |
 * +----+-------------+----+---------------------+--------+
 */
static void disas_comp_b_imm(DisasContext *s, uint32_t insn)
{
    unsigned int sf, op, rt;
    uint64_t addr;
    TCGLabel *label_match;
    TCGv_i64 tcg_cmp;

    sf = extract32(insn, 31, 1);
    op = extract32(insn, 24, 1); /* 0: CBZ; 1: CBNZ */
    rt = extract32(insn, 0, 5);
    addr = s->pc_curr + sextract32(insn, 5, 19) * 4;

    tcg_cmp = read_cpu_reg(s, rt, sf);
    label_match = gen_new_label();

    reset_btype(s);
    tcg_gen_brcondi_i64(op ? TCG_COND_NE : TCG_COND_EQ,
                        tcg_cmp, 0, label_match);

    gen_goto_tb(s, 0, s->base.pc_next);
    gen_set_label(label_match);
    gen_goto_tb(s, 1, addr);
}

/* Test and branch (immediate)
 *   31  30         25  24  23   19 18          5 4    0
 * +----+-------------+----+-------+-------------+------+
 * | b5 | 0 1 1 0 1 1 | op |  b40  |    imm14    |  Rt  |
 * +----+-------------+----+-------+-------------+------+
 */
static void disas_test_b_imm(DisasContext *s, uint32_t insn)
{
    unsigned int bit_pos, op, rt;
    uint64_t addr;
    TCGLabel *label_match;
    TCGv_i64 tcg_cmp;

    bit_pos = (extract32(insn, 31, 1) << 5) | extract32(insn, 19, 5);
    op = extract32(insn, 24, 1); /* 0: TBZ; 1: TBNZ */
    addr = s->pc_curr + sextract32(insn, 5, 14) * 4;
    rt = extract32(insn, 0, 5);

    tcg_cmp = tcg_temp_new_i64();
    tcg_gen_andi_i64(tcg_cmp, cpu_reg(s, rt), (1ULL << bit_pos));
    label_match = gen_new_label();

    reset_btype(s);
    tcg_gen_brcondi_i64(op ? TCG_COND_NE : TCG_COND_EQ,
                        tcg_cmp, 0, label_match);
    tcg_temp_free_i64(tcg_cmp);
    gen_goto_tb(s, 0, s->base.pc_next);
    gen_set_label(label_match);
    gen_goto_tb(s, 1, addr);
}

/* Conditional branch (immediate)
 *  31           25  24  23                  5   4  3    0
 * +---------------+----+---------------------+----+------+
 * | 0 1 0 1 0 1 0 | o1 |         imm19       | o0 | cond |
 * +---------------+----+---------------------+----+------+
 */
static void disas_cond_b_imm(DisasContext *s, uint32_t insn)
{
    unsigned int cond;
    uint64_t addr;

    if ((insn & (1 << 4)) || (insn & (1 << 24))) {
        unallocated_encoding(s);
        return;
    }
    addr = s->pc_curr + sextract32(insn, 5, 19) * 4;
    cond = extract32(insn, 0, 4);

    reset_btype(s);
    if (cond < 0x0e) {
        /* genuinely conditional branches */
        TCGLabel *label_match = gen_new_label();
        arm_gen_test_cc(cond, label_match);
        gen_goto_tb(s, 0, s->base.pc_next);
        gen_set_label(label_match);
        gen_goto_tb(s, 1, addr);
    } else {
        /* 0xe and 0xf are both "always" conditions */
        gen_goto_tb(s, 0, addr);
    }
}

/* HINT instruction group, including various allocated HINTs */
static void handle_hint(DisasContext *s, uint32_t insn,
                        unsigned int op1, unsigned int op2, unsigned int crm)
{
    unsigned int selector = crm << 3 | op2;

    if (op1 != 3) {
        unallocated_encoding(s);
        return;
    }

    switch (selector) {
    case 0b00000: /* NOP */
        break;
    case 0b00011: /* WFI */
        s->base.is_jmp = DISAS_WFI;
        break;
    case 0b00001: /* YIELD */
        /* When running in MTTCG we don't generate jumps to the yield and
         * WFE helpers as it won't affect the scheduling of other vCPUs.
         * If we wanted to more completely model WFE/SEV so we don't busy
         * spin unnecessarily we would need to do something more involved.
         */
        if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
            s->base.is_jmp = DISAS_YIELD;
        }
        break;
    case 0b00010: /* WFE */
        if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
            s->base.is_jmp = DISAS_WFE;
        }
        break;
    case 0b00100: /* SEV */
    case 0b00101: /* SEVL */
        /* we treat all as NOP at least for now */
        break;
    case 0b00111: /* XPACLRI */
        if (s->pauth_active) {
            gen_helper_xpaci(cpu_X[30], cpu_env, cpu_X[30]);
        }
        break;
    case 0b01000: /* PACIA1716 */
        if (s->pauth_active) {
            gen_helper_pacia(cpu_X[17], cpu_env, cpu_X[17], cpu_X[16]);
        }
        break;
    case 0b01010: /* PACIB1716 */
        if (s->pauth_active) {
            gen_helper_pacib(cpu_X[17], cpu_env, cpu_X[17], cpu_X[16]);
        }
        break;
    case 0b01100: /* AUTIA1716 */
        if (s->pauth_active) {
            gen_helper_autia(cpu_X[17], cpu_env, cpu_X[17], cpu_X[16]);
        }
        break;
    case 0b01110: /* AUTIB1716 */
        if (s->pauth_active) {
            gen_helper_autib(cpu_X[17], cpu_env, cpu_X[17], cpu_X[16]);
        }
        break;
    case 0b11000: /* PACIAZ */
        if (s->pauth_active) {
            gen_helper_pacia(cpu_X[30], cpu_env, cpu_X[30],
                                new_tmp_a64_zero(s));
        }
        break;
    case 0b11001: /* PACIASP */
        if (s->pauth_active) {
            gen_helper_pacia(cpu_X[30], cpu_env, cpu_X[30], cpu_X[31]);
        }
        break;
    case 0b11010: /* PACIBZ */
        if (s->pauth_active) {
            gen_helper_pacib(cpu_X[30], cpu_env, cpu_X[30],
                                new_tmp_a64_zero(s));
        }
        break;
    case 0b11011: /* PACIBSP */
        if (s->pauth_active) {
            gen_helper_pacib(cpu_X[30], cpu_env, cpu_X[30], cpu_X[31]);
        }
        break;
    case 0b11100: /* AUTIAZ */
        if (s->pauth_active) {
            gen_helper_autia(cpu_X[30], cpu_env, cpu_X[30],
                              new_tmp_a64_zero(s));
        }
        break;
    case 0b11101: /* AUTIASP */
        if (s->pauth_active) {
            gen_helper_autia(cpu_X[30], cpu_env, cpu_X[30], cpu_X[31]);
        }
        break;
    case 0b11110: /* AUTIBZ */
        if (s->pauth_active) {
            gen_helper_autib(cpu_X[30], cpu_env, cpu_X[30],
                              new_tmp_a64_zero(s));
        }
        break;
    case 0b11111: /* AUTIBSP */
        if (s->pauth_active) {
            gen_helper_autib(cpu_X[30], cpu_env, cpu_X[30], cpu_X[31]);
        }
        break;
    default:
        /* default specified as NOP equivalent */
        break;
    }
}

static void gen_clrex(DisasContext *s, uint32_t insn)
{
    tcg_gen_movi_i64(cpu_exclusive_addr, -1);
}

/* CLREX, DSB, DMB, ISB */
static void handle_sync(DisasContext *s, uint32_t insn,
                        unsigned int op1, unsigned int op2, unsigned int crm)
{
    TCGBar bar;

    if (op1 != 3) {
        unallocated_encoding(s);
        return;
    }

    switch (op2) {
    case 2: /* CLREX */
        gen_clrex(s, insn);
        return;
    case 4: /* DSB */
    case 5: /* DMB */
        switch (crm & 3) {
        case 1: /* MBReqTypes_Reads */
            bar = TCG_BAR_SC | TCG_MO_LD_LD | TCG_MO_LD_ST;
            break;
        case 2: /* MBReqTypes_Writes */
            bar = TCG_BAR_SC | TCG_MO_ST_ST;
            break;
        default: /* MBReqTypes_All */
            bar = TCG_BAR_SC | TCG_MO_ALL;
            break;
        }
        tcg_gen_mb(bar);
        return;
    case 6: /* ISB */
        /* We need to break the TB after this insn to execute
         * a self-modified code correctly and also to take
         * any pending interrupts immediately.
         */
        reset_btype(s);
        gen_goto_tb(s, 0, s->base.pc_next);
        return;

    case 7: /* SB */
        if (crm != 0 || !dc_isar_feature(aa64_sb, s)) {
            goto do_unallocated;
        }
        /*
         * TODO: There is no speculation barrier opcode for TCG;
         * MB and end the TB instead.
         */
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
        gen_goto_tb(s, 0, s->base.pc_next);
        return;

    default:
    do_unallocated:
        unallocated_encoding(s);
        return;
    }
}

static void gen_xaflag(void)
{
    TCGv_i32 z = tcg_temp_new_i32();

    tcg_gen_setcondi_i32(TCG_COND_EQ, z, cpu_ZF, 0);

    /*
     * (!C & !Z) << 31
     * (!(C | Z)) << 31
     * ~((C | Z) << 31)
     * ~-(C | Z)
     * (C | Z) - 1
     */
    tcg_gen_or_i32(cpu_NF, cpu_CF, z);
    tcg_gen_subi_i32(cpu_NF, cpu_NF, 1);

    /* !(Z & C) */
    tcg_gen_and_i32(cpu_ZF, z, cpu_CF);
    tcg_gen_xori_i32(cpu_ZF, cpu_ZF, 1);

    /* (!C & Z) << 31 -> -(Z & ~C) */
    tcg_gen_andc_i32(cpu_VF, z, cpu_CF);
    tcg_gen_neg_i32(cpu_VF, cpu_VF);

    /* C | Z */
    tcg_gen_or_i32(cpu_CF, cpu_CF, z);

    tcg_temp_free_i32(z);
}

static void gen_axflag(void)
{
    tcg_gen_sari_i32(cpu_VF, cpu_VF, 31);         /* V ? -1 : 0 */
    tcg_gen_andc_i32(cpu_CF, cpu_CF, cpu_VF);     /* C & !V */

    /* !(Z | V) -> !(!ZF | V) -> ZF & !V -> ZF & ~VF */
    tcg_gen_andc_i32(cpu_ZF, cpu_ZF, cpu_VF);

    tcg_gen_movi_i32(cpu_NF, 0);
    tcg_gen_movi_i32(cpu_VF, 0);
}

/* MSR (immediate) - move immediate to processor state field */
static void handle_msr_i(DisasContext *s, uint32_t insn,
                         unsigned int op1, unsigned int op2, unsigned int crm)
{
    TCGv_i32 t1;
    int op = op1 << 3 | op2;

    /* End the TB by default, chaining is ok.  */
    s->base.is_jmp = DISAS_TOO_MANY;

    switch (op) {
    case 0x00: /* CFINV */
        if (crm != 0 || !dc_isar_feature(aa64_condm_4, s)) {
            goto do_unallocated;
        }
        tcg_gen_xori_i32(cpu_CF, cpu_CF, 1);
        s->base.is_jmp = DISAS_NEXT;
        break;

    case 0x01: /* XAFlag */
        if (crm != 0 || !dc_isar_feature(aa64_condm_5, s)) {
            goto do_unallocated;
        }
        gen_xaflag();
        s->base.is_jmp = DISAS_NEXT;
        break;

    case 0x02: /* AXFlag */
        if (crm != 0 || !dc_isar_feature(aa64_condm_5, s)) {
            goto do_unallocated;
        }
        gen_axflag();
        s->base.is_jmp = DISAS_NEXT;
        break;

    case 0x03: /* UAO */
        if (!dc_isar_feature(aa64_uao, s) || s->current_el == 0) {
            goto do_unallocated;
        }
        if (crm & 1) {
            set_pstate_bits(PSTATE_UAO);
        } else {
            clear_pstate_bits(PSTATE_UAO);
        }
        t1 = tcg_const_i32(s->current_el);
        gen_helper_rebuild_hflags_a64(cpu_env, t1);
        tcg_temp_free_i32(t1);
        break;

    case 0x04: /* PAN */
        if (!dc_isar_feature(aa64_pan, s) || s->current_el == 0) {
            goto do_unallocated;
        }
        if (crm & 1) {
            set_pstate_bits(PSTATE_PAN);
        } else {
            clear_pstate_bits(PSTATE_PAN);
        }
        t1 = tcg_const_i32(s->current_el);
        gen_helper_rebuild_hflags_a64(cpu_env, t1);
        tcg_temp_free_i32(t1);
        break;

    case 0x05: /* SPSel */
        if (s->current_el == 0) {
            goto do_unallocated;
        }
        t1 = tcg_const_i32(crm & PSTATE_SP);
        gen_helper_msr_i_spsel(cpu_env, t1);
        tcg_temp_free_i32(t1);
        break;

    case 0x1a: /* DIT */
        if (!dc_isar_feature(aa64_dit, s)) {
            goto do_unallocated;
        }
        if (crm & 1) {
            set_pstate_bits(PSTATE_DIT);
        } else {
            clear_pstate_bits(PSTATE_DIT);
        }
        /* There's no need to rebuild hflags because DIT is a nop */
        break;

    case 0x1e: /* DAIFSet */
        t1 = tcg_const_i32(crm);
        gen_helper_msr_i_daifset(cpu_env, t1);
        tcg_temp_free_i32(t1);
        break;

    case 0x1f: /* DAIFClear */
        t1 = tcg_const_i32(crm);
        gen_helper_msr_i_daifclear(cpu_env, t1);
        tcg_temp_free_i32(t1);
        /* For DAIFClear, exit the cpu loop to re-evaluate pending IRQs.  */
        s->base.is_jmp = DISAS_UPDATE_EXIT;
        break;

    case 0x1c: /* TCO */
        if (dc_isar_feature(aa64_mte, s)) {
            /* Full MTE is enabled -- set the TCO bit as directed. */
            if (crm & 1) {
                set_pstate_bits(PSTATE_TCO);
            } else {
                clear_pstate_bits(PSTATE_TCO);
            }
            t1 = tcg_const_i32(s->current_el);
            gen_helper_rebuild_hflags_a64(cpu_env, t1);
            tcg_temp_free_i32(t1);
            /* Many factors, including TCO, go into MTE_ACTIVE. */
            s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        } else if (dc_isar_feature(aa64_mte_insn_reg, s)) {
            /* Only "instructions accessible at EL0" -- PSTATE.TCO is WI.  */
            s->base.is_jmp = DISAS_NEXT;
        } else {
            goto do_unallocated;
        }
        break;

    default:
    do_unallocated:
        unallocated_encoding(s);
        return;
    }
}

static void gen_get_nzcv(TCGv_i64 tcg_rt)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 nzcv = tcg_temp_new_i32();

    /* build bit 31, N */
    tcg_gen_andi_i32(nzcv, cpu_NF, (1U << 31));
    /* build bit 30, Z */
    tcg_gen_setcondi_i32(TCG_COND_EQ, tmp, cpu_ZF, 0);
    tcg_gen_deposit_i32(nzcv, nzcv, tmp, 30, 1);
    /* build bit 29, C */
    tcg_gen_deposit_i32(nzcv, nzcv, cpu_CF, 29, 1);
    /* build bit 28, V */
    tcg_gen_shri_i32(tmp, cpu_VF, 31);
    tcg_gen_deposit_i32(nzcv, nzcv, tmp, 28, 1);
    /* generate result */
    tcg_gen_extu_i32_i64(tcg_rt, nzcv);

    tcg_temp_free_i32(nzcv);
    tcg_temp_free_i32(tmp);
}

static void gen_set_nzcv(TCGv_i64 tcg_rt)
{
    TCGv_i32 nzcv = tcg_temp_new_i32();

    /* take NZCV from R[t] */
    tcg_gen_extrl_i64_i32(nzcv, tcg_rt);

    /* bit 31, N */
    tcg_gen_andi_i32(cpu_NF, nzcv, (1U << 31));
    /* bit 30, Z */
    tcg_gen_andi_i32(cpu_ZF, nzcv, (1 << 30));
    tcg_gen_setcondi_i32(TCG_COND_EQ, cpu_ZF, cpu_ZF, 0);
    /* bit 29, C */
    tcg_gen_andi_i32(cpu_CF, nzcv, (1 << 29));
    tcg_gen_shri_i32(cpu_CF, cpu_CF, 29);
    /* bit 28, V */
    tcg_gen_andi_i32(cpu_VF, nzcv, (1 << 28));
    tcg_gen_shli_i32(cpu_VF, cpu_VF, 3);
    tcg_temp_free_i32(nzcv);
}

/* MRS - move from system register
 * MSR (register) - move to system register
 * SYS
 * SYSL
 * These are all essentially the same insn in 'read' and 'write'
 * versions, with varying op0 fields.
 */
static void handle_sys(DisasContext *s, uint32_t insn, bool isread,
                       unsigned int op0, unsigned int op1, unsigned int op2,
                       unsigned int crn, unsigned int crm, unsigned int rt)
{
    const ARMCPRegInfo *ri;
    TCGv_i64 tcg_rt;

    ri = get_arm_cp_reginfo(s->cp_regs,
                            ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP,
                                               crn, crm, op0, op1, op2));

    if (!ri) {
        /* Unknown register; this might be a guest error or a QEMU
         * unimplemented feature.
         */
        qemu_log_mask(LOG_UNIMP, "%s access to unsupported AArch64 "
                      "system register op0:%d op1:%d crn:%d crm:%d op2:%d\n",
                      isread ? "read" : "write", op0, op1, crn, crm, op2);
        unallocated_encoding(s);
        return;
    }

    /* Check access permissions */
    if (!cp_access_ok(s->current_el, ri, isread)) {
        unallocated_encoding(s);
        return;
    }

    if (ri->accessfn) {
        /* Emit code to perform further access permissions checks at
         * runtime; this may result in an exception.
         */
        TCGv_ptr tmpptr;
        TCGv_i32 tcg_syn, tcg_isread;
        uint32_t syndrome;

        gen_a64_set_pc_im(s->pc_curr);
        tmpptr = tcg_const_ptr(ri);
        syndrome = syn_aa64_sysregtrap(op0, op1, op2, crn, crm, rt, isread);
        tcg_syn = tcg_const_i32(syndrome);
        tcg_isread = tcg_const_i32(isread);
        gen_helper_access_check_cp_reg(cpu_env, tmpptr, tcg_syn, tcg_isread);
        tcg_temp_free_ptr(tmpptr);
        tcg_temp_free_i32(tcg_syn);
        tcg_temp_free_i32(tcg_isread);
    } else if (ri->type & ARM_CP_RAISES_EXC) {
        /*
         * The readfn or writefn might raise an exception;
         * synchronize the CPU state in case it does.
         */
        gen_a64_set_pc_im(s->pc_curr);
    }

    /* Handle special cases first */
    switch (ri->type & ~(ARM_CP_FLAG_MASK & ~ARM_CP_SPECIAL)) {
    case ARM_CP_NOP:
        return;
    case ARM_CP_NZCV:
        tcg_rt = cpu_reg(s, rt);
        if (isread) {
            gen_get_nzcv(tcg_rt);
        } else {
            gen_set_nzcv(tcg_rt);
        }
        return;
    case ARM_CP_CURRENTEL:
        /* Reads as current EL value from pstate, which is
         * guaranteed to be constant by the tb flags.
         */
        tcg_rt = cpu_reg(s, rt);
        tcg_gen_movi_i64(tcg_rt, s->current_el << 2);
        return;
    case ARM_CP_DC_ZVA:
        /* Writes clear the aligned block of memory which rt points into. */
        if (s->mte_active[0]) {
            TCGv_i32 t_desc;
            int desc = 0;

            desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
            desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
            desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
            t_desc = tcg_const_i32(desc);

            tcg_rt = new_tmp_a64(s);
            gen_helper_mte_check_zva(tcg_rt, cpu_env, t_desc, cpu_reg(s, rt));
            tcg_temp_free_i32(t_desc);
        } else {
            tcg_rt = clean_data_tbi(s, cpu_reg(s, rt));
        }
        gen_helper_dc_zva(cpu_env, tcg_rt);
        return;
    case ARM_CP_DC_GVA:
        {
            TCGv_i64 clean_addr, tag;

            /*
             * DC_GVA, like DC_ZVA, requires that we supply the original
             * pointer for an invalid page.  Probe that address first.
             */
            tcg_rt = cpu_reg(s, rt);
            clean_addr = clean_data_tbi(s, tcg_rt);
            gen_probe_access(s, clean_addr, MMU_DATA_STORE, MO_8);

            if (s->ata) {
                /* Extract the tag from the register to match STZGM.  */
                tag = tcg_temp_new_i64();
                tcg_gen_shri_i64(tag, tcg_rt, 56);
                gen_helper_stzgm_tags(cpu_env, clean_addr, tag);
                tcg_temp_free_i64(tag);
            }
        }
        return;
    case ARM_CP_DC_GZVA:
        {
            TCGv_i64 clean_addr, tag;

            /* For DC_GZVA, we can rely on DC_ZVA for the proper fault. */
            tcg_rt = cpu_reg(s, rt);
            clean_addr = clean_data_tbi(s, tcg_rt);
            gen_helper_dc_zva(cpu_env, clean_addr);

            if (s->ata) {
                /* Extract the tag from the register to match STZGM.  */
                tag = tcg_temp_new_i64();
                tcg_gen_shri_i64(tag, tcg_rt, 56);
                gen_helper_stzgm_tags(cpu_env, clean_addr, tag);
                tcg_temp_free_i64(tag);
            }
        }
        return;
    default:
        break;
    }
    if ((ri->type & ARM_CP_FPU) && !fp_access_check(s)) {
        return;
    } else if ((ri->type & ARM_CP_SVE) && !sve_access_check(s)) {
        return;
    }

    if ((tb_cflags(s->base.tb) & CF_USE_ICOUNT) && (ri->type & ARM_CP_IO)) {
        gen_io_start();
    }

    tcg_rt = cpu_reg(s, rt);

    if (isread) {
        if (ri->type & ARM_CP_CONST) {
            tcg_gen_movi_i64(tcg_rt, ri->resetvalue);
        } else if (ri->readfn) {
            TCGv_ptr tmpptr;
            tmpptr = tcg_const_ptr(ri);
            gen_helper_get_cp_reg64(tcg_rt, cpu_env, tmpptr);
            tcg_temp_free_ptr(tmpptr);
        } else {
            tcg_gen_ld_i64(tcg_rt, cpu_env, ri->fieldoffset);
        }
    } else {
        if (ri->type & ARM_CP_CONST) {
            /* If not forbidden by access permissions, treat as WI */
            return;
        } else if (ri->writefn) {
            TCGv_ptr tmpptr;
            tmpptr = tcg_const_ptr(ri);
            gen_helper_set_cp_reg64(cpu_env, tmpptr, tcg_rt);
            tcg_temp_free_ptr(tmpptr);
        } else {
            tcg_gen_st_i64(tcg_rt, cpu_env, ri->fieldoffset);
        }
    }

    if ((tb_cflags(s->base.tb) & CF_USE_ICOUNT) && (ri->type & ARM_CP_IO)) {
        /* I/O operations must end the TB here (whether read or write) */
        s->base.is_jmp = DISAS_UPDATE_EXIT;
    }
    if (!isread && !(ri->type & ARM_CP_SUPPRESS_TB_END)) {
        /*
         * A write to any coprocessor regiser that ends a TB
         * must rebuild the hflags for the next TB.
         */
        TCGv_i32 tcg_el = tcg_const_i32(s->current_el);
        gen_helper_rebuild_hflags_a64(cpu_env, tcg_el);
        tcg_temp_free_i32(tcg_el);
        /*
         * We default to ending the TB on a coprocessor register write,
         * but allow this to be suppressed by the register definition
         * (usually only necessary to work around guest bugs).
         */
        s->base.is_jmp = DISAS_UPDATE_EXIT;
    }
}

/* System
 *  31                 22 21  20 19 18 16 15   12 11    8 7   5 4    0
 * +---------------------+---+-----+-----+-------+-------+-----+------+
 * | 1 1 0 1 0 1 0 1 0 0 | L | op0 | op1 |  CRn  |  CRm  | op2 |  Rt  |
 * +---------------------+---+-----+-----+-------+-------+-----+------+
 */
static void disas_system(DisasContext *s, uint32_t insn)
{
    unsigned int l, op0, op1, crn, crm, op2, rt;
    l = extract32(insn, 21, 1);
    op0 = extract32(insn, 19, 2);
    op1 = extract32(insn, 16, 3);
    crn = extract32(insn, 12, 4);
    crm = extract32(insn, 8, 4);
    op2 = extract32(insn, 5, 3);
    rt = extract32(insn, 0, 5);

    if (op0 == 0) {
        if (l || rt != 31) {
            unallocated_encoding(s);
            return;
        }
        switch (crn) {
        case 2: /* HINT (including allocated hints like NOP, YIELD, etc) */
            handle_hint(s, insn, op1, op2, crm);
            break;
        case 3: /* CLREX, DSB, DMB, ISB */
            handle_sync(s, insn, op1, op2, crm);
            break;
        case 4: /* MSR (immediate) */
            handle_msr_i(s, insn, op1, op2, crm);
            break;
        default:
            unallocated_encoding(s);
            break;
        }
        return;
    }
    handle_sys(s, insn, l, op0, op1, op2, crn, crm, rt);
}

/* Exception generation
 *
 *  31             24 23 21 20                     5 4   2 1  0
 * +-----------------+-----+------------------------+-----+----+
 * | 1 1 0 1 0 1 0 0 | opc |          imm16         | op2 | LL |
 * +-----------------------+------------------------+----------+
 */
static void disas_exc(DisasContext *s, uint32_t insn)
{
    int opc = extract32(insn, 21, 3);
    int op2_ll = extract32(insn, 0, 5);
    int imm16 = extract32(insn, 5, 16);
    TCGv_i32 tmp;

    switch (opc) {
    case 0:
        /* For SVC, HVC and SMC we advance the single-step state
         * machine before taking the exception. This is architecturally
         * mandated, to ensure that single-stepping a system call
         * instruction works properly.
         */
        switch (op2_ll) {
        case 1:                                                     /* SVC */
            gen_ss_advance(s);
            gen_exception_insn(s, s->base.pc_next, EXCP_SWI,
                               syn_aa64_svc(imm16), default_exception_el(s));
            break;
        case 2:                                                     /* HVC */
            if (s->current_el == 0) {
                unallocated_encoding(s);
                break;
            }
            /* The pre HVC helper handles cases when HVC gets trapped
             * as an undefined insn by runtime configuration.
             */
            gen_a64_set_pc_im(s->pc_curr);
            gen_helper_pre_hvc(cpu_env);
            gen_ss_advance(s);
            gen_exception_insn(s, s->base.pc_next, EXCP_HVC,
                               syn_aa64_hvc(imm16), 2);
            break;
        case 3:                                                     /* SMC */
            if (s->current_el == 0) {
                unallocated_encoding(s);
                break;
            }
            gen_a64_set_pc_im(s->pc_curr);
            tmp = tcg_const_i32(syn_aa64_smc(imm16));
            gen_helper_pre_smc(cpu_env, tmp);
            tcg_temp_free_i32(tmp);
            gen_ss_advance(s);
            gen_exception_insn(s, s->base.pc_next, EXCP_SMC,
                               syn_aa64_smc(imm16), 3);
            break;
        default:
            unallocated_encoding(s);
            break;
        }
        break;
    case 1:
        if (op2_ll != 0) {
            unallocated_encoding(s);
            break;
        }
        /* BRK */
        gen_exception_bkpt_insn(s, syn_aa64_bkpt(imm16));
        break;
    case 2:
        if (op2_ll != 0) {
            unallocated_encoding(s);
            break;
        }
        /* HLT. This has two purposes.
         * Architecturally, it is an external halting debug instruction.
         * Since QEMU doesn't implement external debug, we treat this as
         * it is required for halting debug disabled: it will UNDEF.
         * Secondly, "HLT 0xf000" is the A64 semihosting syscall instruction.
         */
        if (semihosting_enabled() && imm16 == 0xf000) {
#ifndef CONFIG_USER_ONLY
            /* In system mode, don't allow userspace access to semihosting,
             * to provide some semblance of security (and for consistency
             * with our 32-bit semihosting).
             */
            if (s->current_el == 0) {
                unsupported_encoding(s, insn);
                break;
            }
#endif
            gen_exception_internal_insn(s, s->pc_curr, EXCP_SEMIHOST);
        } else {
            unsupported_encoding(s, insn);
        }
        break;
    case 5:
        if (op2_ll < 1 || op2_ll > 3) {
            unallocated_encoding(s);
            break;
        }
        /* DCPS1, DCPS2, DCPS3 */
        unsupported_encoding(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* Unconditional branch (register)
 *  31           25 24   21 20   16 15   10 9    5 4     0
 * +---------------+-------+-------+-------+------+-------+
 * | 1 1 0 1 0 1 1 |  opc  |  op2  |  op3  |  Rn  |  op4  |
 * +---------------+-------+-------+-------+------+-------+
 */
static void disas_uncond_b_reg(DisasContext *s, uint32_t insn)
{
    unsigned int opc, op2, op3, rn, op4;
    unsigned btype_mod = 2;   /* 0: BR, 1: BLR, 2: other */
    TCGv_i64 dst;
    TCGv_i64 modifier;

    opc = extract32(insn, 21, 4);
    op2 = extract32(insn, 16, 5);
    op3 = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    op4 = extract32(insn, 0, 5);

    if (op2 != 0x1f) {
        goto do_unallocated;
    }

    switch (opc) {
    case 0: /* BR */
    case 1: /* BLR */
    case 2: /* RET */
        btype_mod = opc;
        switch (op3) {
        case 0:
            /* BR, BLR, RET */
            if (op4 != 0) {
                goto do_unallocated;
            }
            dst = cpu_reg(s, rn);
            break;

        case 2:
        case 3:
            if (!dc_isar_feature(aa64_pauth, s)) {
                goto do_unallocated;
            }
            if (opc == 2) {
                /* RETAA, RETAB */
                if (rn != 0x1f || op4 != 0x1f) {
                    goto do_unallocated;
                }
                rn = 30;
                modifier = cpu_X[31];
            } else {
                /* BRAAZ, BRABZ, BLRAAZ, BLRABZ */
                if (op4 != 0x1f) {
                    goto do_unallocated;
                }
                modifier = new_tmp_a64_zero(s);
            }
            if (s->pauth_active) {
                dst = new_tmp_a64(s);
                if (op3 == 2) {
                    gen_helper_autia(dst, cpu_env, cpu_reg(s, rn), modifier);
                } else {
                    gen_helper_autib(dst, cpu_env, cpu_reg(s, rn), modifier);
                }
            } else {
                dst = cpu_reg(s, rn);
            }
            break;

        default:
            goto do_unallocated;
        }
        if (use_qasan && qasan_max_call_stack) {
          if (opc == 2 && rn == 30)
            gen_helper_qasan_shadow_stack_pop(cpu_reg(s, 30));
          else if (opc == 1)
            gen_helper_qasan_shadow_stack_push(tcg_const_tl(s->pc_curr));
        }
        gen_a64_set_pc(s, dst);
        /* BLR also needs to load return address */
        if (opc == 1) {
            tcg_gen_movi_i64(cpu_reg(s, 30), s->base.pc_next);
        }
        break;

    case 8: /* BRAA */
    case 9: /* BLRAA */
        if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        if ((op3 & ~1) != 2) {
            goto do_unallocated;
        }
        btype_mod = opc & 1;
        if (s->pauth_active) {
            dst = new_tmp_a64(s);
            modifier = cpu_reg_sp(s, op4);
            if (op3 == 2) {
                gen_helper_autia(dst, cpu_env, cpu_reg(s, rn), modifier);
            } else {
                gen_helper_autib(dst, cpu_env, cpu_reg(s, rn), modifier);
            }
        } else {
            dst = cpu_reg(s, rn);
        }
        gen_a64_set_pc(s, dst);
        /* BLRAA also needs to load return address */
        if (opc == 9) {
            tcg_gen_movi_i64(cpu_reg(s, 30), s->base.pc_next);
        }
        break;

    case 4: /* ERET */
        if (s->current_el == 0) {
            goto do_unallocated;
        }
        switch (op3) {
        case 0: /* ERET */
            if (op4 != 0) {
                goto do_unallocated;
            }
            dst = tcg_temp_new_i64();
            tcg_gen_ld_i64(dst, cpu_env,
                           offsetof(CPUARMState, elr_el[s->current_el]));
            break;

        case 2: /* ERETAA */
        case 3: /* ERETAB */
            if (!dc_isar_feature(aa64_pauth, s)) {
                goto do_unallocated;
            }
            if (rn != 0x1f || op4 != 0x1f) {
                goto do_unallocated;
            }
            dst = tcg_temp_new_i64();
            tcg_gen_ld_i64(dst, cpu_env,
                           offsetof(CPUARMState, elr_el[s->current_el]));
            if (s->pauth_active) {
                modifier = cpu_X[31];
                if (op3 == 2) {
                    gen_helper_autia(dst, cpu_env, dst, modifier);
                } else {
                    gen_helper_autib(dst, cpu_env, dst, modifier);
                }
            }
            break;

        default:
            goto do_unallocated;
        }
        if (tb_cflags(s->base.tb) & CF_USE_ICOUNT) {
            gen_io_start();
        }

        gen_helper_exception_return(cpu_env, dst);
        tcg_temp_free_i64(dst);
        /* Must exit loop to check un-masked IRQs */
        s->base.is_jmp = DISAS_EXIT;
        return;

    case 5: /* DRPS */
        if (op3 != 0 || op4 != 0 || rn != 0x1f) {
            goto do_unallocated;
        } else {
            unsupported_encoding(s, insn);
        }
        return;

    default:
    do_unallocated:
        unallocated_encoding(s);
        return;
    }

    switch (btype_mod) {
    case 0: /* BR */
        if (dc_isar_feature(aa64_bti, s)) {
            /* BR to {x16,x17} or !guard -> 1, else 3.  */
            set_btype(s, rn == 16 || rn == 17 || !s->guarded_page ? 1 : 3);
        }
        break;

    case 1: /* BLR */
        if (dc_isar_feature(aa64_bti, s)) {
            /* BLR sets BTYPE to 2, regardless of source guarded page.  */
            set_btype(s, 2);
        }
        break;

    default: /* RET or none of the above.  */
        /* BTYPE will be set to 0 by normal end-of-insn processing.  */
        break;
    }

    s->base.is_jmp = DISAS_JUMP;
}

/* Branches, exception generating and system instructions */
static void disas_b_exc_sys(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 25, 7)) {
    case 0x0a: case 0x0b:
    case 0x4a: case 0x4b: /* Unconditional branch (immediate) */
        disas_uncond_b_imm(s, insn);
        break;
    case 0x1a: case 0x5a: /* Compare & branch (immediate) */
        disas_comp_b_imm(s, insn);
        break;
    case 0x1b: case 0x5b: /* Test & branch (immediate) */
        disas_test_b_imm(s, insn);
        break;
    case 0x2a: /* Conditional branch (immediate) */
        disas_cond_b_imm(s, insn);
        break;
    case 0x6a: /* Exception generation / System */
        if (insn & (1 << 24)) {
            if (extract32(insn, 22, 2) == 0) {
                disas_system(s, insn);
            } else {
                unallocated_encoding(s);
            }
        } else {
            disas_exc(s, insn);
        }
        break;
    case 0x6b: /* Unconditional branch (register) */
        disas_uncond_b_reg(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/*
 * Load/Store exclusive instructions are implemented by remembering
 * the value/address loaded, and seeing if these are the same
 * when the store is performed. This is not actually the architecturally
 * mandated semantics, but it works for typical guest code sequences
 * and avoids having to monitor regular stores.
 *
 * The store exclusive uses the atomic cmpxchg primitives to avoid
 * races in multi-threaded linux-user and when MTTCG softmmu is
 * enabled.
 */
static void gen_load_exclusive(DisasContext *s, int rt, int rt2,
                               TCGv_i64 addr, int size, bool is_pair)
{
    int idx = get_mem_index(s);
    MemOp memop = s->be_data;

    g_assert(size <= 3);
    if (is_pair) {
        g_assert(size >= 2);
        if (size == 2) {
            /* The pair must be single-copy atomic for the doubleword.  */
            memop |= MO_64 | MO_ALIGN;
            tcg_gen_qemu_ld_i64(cpu_exclusive_val, addr, idx, memop);
            if (s->be_data == MO_LE) {
                tcg_gen_extract_i64(cpu_reg(s, rt), cpu_exclusive_val, 0, 32);
                tcg_gen_extract_i64(cpu_reg(s, rt2), cpu_exclusive_val, 32, 32);
            } else {
                tcg_gen_extract_i64(cpu_reg(s, rt), cpu_exclusive_val, 32, 32);
                tcg_gen_extract_i64(cpu_reg(s, rt2), cpu_exclusive_val, 0, 32);
            }
        } else {
            /* The pair must be single-copy atomic for *each* doubleword, not
               the entire quadword, however it must be quadword aligned.  */
            memop |= MO_64;
            tcg_gen_qemu_ld_i64(cpu_exclusive_val, addr, idx,
                                memop | MO_ALIGN_16);

            TCGv_i64 addr2 = tcg_temp_new_i64();
            tcg_gen_addi_i64(addr2, addr, 8);
            tcg_gen_qemu_ld_i64(cpu_exclusive_high, addr2, idx, memop);
            tcg_temp_free_i64(addr2);

            tcg_gen_mov_i64(cpu_reg(s, rt), cpu_exclusive_val);
            tcg_gen_mov_i64(cpu_reg(s, rt2), cpu_exclusive_high);
        }
    } else {
        memop |= size | MO_ALIGN;
        tcg_gen_qemu_ld_i64(cpu_exclusive_val, addr, idx, memop);
        tcg_gen_mov_i64(cpu_reg(s, rt), cpu_exclusive_val);
    }
    tcg_gen_mov_i64(cpu_exclusive_addr, addr);
}

static void gen_store_exclusive(DisasContext *s, int rd, int rt, int rt2,
                                TCGv_i64 addr, int size, int is_pair)
{
    /* if (env->exclusive_addr == addr && env->exclusive_val == [addr]
     *     && (!is_pair || env->exclusive_high == [addr + datasize])) {
     *     [addr] = {Rt};
     *     if (is_pair) {
     *         [addr + datasize] = {Rt2};
     *     }
     *     {Rd} = 0;
     * } else {
     *     {Rd} = 1;
     * }
     * env->exclusive_addr = -1;
     */
    TCGLabel *fail_label = gen_new_label();
    TCGLabel *done_label = gen_new_label();
    TCGv_i64 tmp;

    tcg_gen_brcond_i64(TCG_COND_NE, addr, cpu_exclusive_addr, fail_label);

    tmp = tcg_temp_new_i64();
    if (is_pair) {
        if (size == 2) {
            if (s->be_data == MO_LE) {
                tcg_gen_concat32_i64(tmp, cpu_reg(s, rt), cpu_reg(s, rt2));
            } else {
                tcg_gen_concat32_i64(tmp, cpu_reg(s, rt2), cpu_reg(s, rt));
            }
            tcg_gen_atomic_cmpxchg_i64(tmp, cpu_exclusive_addr,
                                       cpu_exclusive_val, tmp,
                                       get_mem_index(s),
                                       MO_64 | MO_ALIGN | s->be_data);
            tcg_gen_setcond_i64(TCG_COND_NE, tmp, tmp, cpu_exclusive_val);
        } else if (tb_cflags(s->base.tb) & CF_PARALLEL) {
            if (!HAVE_CMPXCHG128) {
                gen_helper_exit_atomic(cpu_env);
                s->base.is_jmp = DISAS_NORETURN;
            } else if (s->be_data == MO_LE) {
                gen_helper_paired_cmpxchg64_le_parallel(tmp, cpu_env,
                                                        cpu_exclusive_addr,
                                                        cpu_reg(s, rt),
                                                        cpu_reg(s, rt2));
            } else {
                gen_helper_paired_cmpxchg64_be_parallel(tmp, cpu_env,
                                                        cpu_exclusive_addr,
                                                        cpu_reg(s, rt),
                                                        cpu_reg(s, rt2));
            }
        } else if (s->be_data == MO_LE) {
            gen_helper_paired_cmpxchg64_le(tmp, cpu_env, cpu_exclusive_addr,
                                           cpu_reg(s, rt), cpu_reg(s, rt2));
        } else {
            gen_helper_paired_cmpxchg64_be(tmp, cpu_env, cpu_exclusive_addr,
                                           cpu_reg(s, rt), cpu_reg(s, rt2));
        }
    } else {
        tcg_gen_atomic_cmpxchg_i64(tmp, cpu_exclusive_addr, cpu_exclusive_val,
                                   cpu_reg(s, rt), get_mem_index(s),
                                   size | MO_ALIGN | s->be_data);
        tcg_gen_setcond_i64(TCG_COND_NE, tmp, tmp, cpu_exclusive_val);
    }
    tcg_gen_mov_i64(cpu_reg(s, rd), tmp);
    tcg_temp_free_i64(tmp);
    tcg_gen_br(done_label);

    gen_set_label(fail_label);
    tcg_gen_movi_i64(cpu_reg(s, rd), 1);
    gen_set_label(done_label);
    tcg_gen_movi_i64(cpu_exclusive_addr, -1);
}

static void gen_compare_and_swap(DisasContext *s, int rs, int rt,
                                 int rn, int size)
{
    TCGv_i64 tcg_rs = cpu_reg(s, rs);
    TCGv_i64 tcg_rt = cpu_reg(s, rt);
    int memidx = get_mem_index(s);
    TCGv_i64 clean_addr;

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn), true, rn != 31, size);
    tcg_gen_atomic_cmpxchg_i64(tcg_rs, clean_addr, tcg_rs, tcg_rt, memidx,
                               size | MO_ALIGN | s->be_data);
}

static void gen_compare_and_swap_pair(DisasContext *s, int rs, int rt,
                                      int rn, int size)
{
    TCGv_i64 s1 = cpu_reg(s, rs);
    TCGv_i64 s2 = cpu_reg(s, rs + 1);
    TCGv_i64 t1 = cpu_reg(s, rt);
    TCGv_i64 t2 = cpu_reg(s, rt + 1);
    TCGv_i64 clean_addr;
    int memidx = get_mem_index(s);

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    /* This is a single atomic access, despite the "pair". */
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn), true, rn != 31, size + 1);

    if (size == 2) {
        TCGv_i64 cmp = tcg_temp_new_i64();
        TCGv_i64 val = tcg_temp_new_i64();

        if (s->be_data == MO_LE) {
            tcg_gen_concat32_i64(val, t1, t2);
            tcg_gen_concat32_i64(cmp, s1, s2);
        } else {
            tcg_gen_concat32_i64(val, t2, t1);
            tcg_gen_concat32_i64(cmp, s2, s1);
        }

        tcg_gen_atomic_cmpxchg_i64(cmp, clean_addr, cmp, val, memidx,
                                   MO_64 | MO_ALIGN | s->be_data);
        tcg_temp_free_i64(val);

        if (s->be_data == MO_LE) {
            tcg_gen_extr32_i64(s1, s2, cmp);
        } else {
            tcg_gen_extr32_i64(s2, s1, cmp);
        }
        tcg_temp_free_i64(cmp);
    } else if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        if (HAVE_CMPXCHG128) {
            TCGv_i32 tcg_rs = tcg_const_i32(rs);
            if (s->be_data == MO_LE) {
                gen_helper_casp_le_parallel(cpu_env, tcg_rs,
                                            clean_addr, t1, t2);
            } else {
                gen_helper_casp_be_parallel(cpu_env, tcg_rs,
                                            clean_addr, t1, t2);
            }
            tcg_temp_free_i32(tcg_rs);
        } else {
            gen_helper_exit_atomic(cpu_env);
            s->base.is_jmp = DISAS_NORETURN;
        }
    } else {
        TCGv_i64 d1 = tcg_temp_new_i64();
        TCGv_i64 d2 = tcg_temp_new_i64();
        TCGv_i64 a2 = tcg_temp_new_i64();
        TCGv_i64 c1 = tcg_temp_new_i64();
        TCGv_i64 c2 = tcg_temp_new_i64();
        TCGv_i64 zero = tcg_const_i64(0);

        /* Load the two words, in memory order.  */
        tcg_gen_qemu_ld_i64(d1, clean_addr, memidx,
                            MO_64 | MO_ALIGN_16 | s->be_data);
        tcg_gen_addi_i64(a2, clean_addr, 8);
        tcg_gen_qemu_ld_i64(d2, a2, memidx, MO_64 | s->be_data);

        /* Compare the two words, also in memory order.  */
        tcg_gen_setcond_i64(TCG_COND_EQ, c1, d1, s1);
        tcg_gen_setcond_i64(TCG_COND_EQ, c2, d2, s2);
        tcg_gen_and_i64(c2, c2, c1);

        /* If compare equal, write back new data, else write back old data.  */
        tcg_gen_movcond_i64(TCG_COND_NE, c1, c2, zero, t1, d1);
        tcg_gen_movcond_i64(TCG_COND_NE, c2, c2, zero, t2, d2);
        tcg_gen_qemu_st_i64(c1, clean_addr, memidx, MO_64 | s->be_data);
        tcg_gen_qemu_st_i64(c2, a2, memidx, MO_64 | s->be_data);
        tcg_temp_free_i64(a2);
        tcg_temp_free_i64(c1);
        tcg_temp_free_i64(c2);
        tcg_temp_free_i64(zero);

        /* Write back the data from memory to Rs.  */
        tcg_gen_mov_i64(s1, d1);
        tcg_gen_mov_i64(s2, d2);
        tcg_temp_free_i64(d1);
        tcg_temp_free_i64(d2);
    }
}

/* Update the Sixty-Four bit (SF) registersize. This logic is derived
 * from the ARMv8 specs for LDR (Shared decode for all encodings).
 */
static bool disas_ldst_compute_iss_sf(int size, bool is_signed, int opc)
{
    int opc0 = extract32(opc, 0, 1);
    int regsize;

    if (is_signed) {
        regsize = opc0 ? 32 : 64;
    } else {
        regsize = size == 3 ? 64 : 32;
    }
    return regsize == 64;
}

/* Load/store exclusive
 *
 *  31 30 29         24  23  22   21  20  16  15  14   10 9    5 4    0
 * +-----+-------------+----+---+----+------+----+-------+------+------+
 * | sz  | 0 0 1 0 0 0 | o2 | L | o1 |  Rs  | o0 |  Rt2  |  Rn  | Rt   |
 * +-----+-------------+----+---+----+------+----+-------+------+------+
 *
 *  sz: 00 -> 8 bit, 01 -> 16 bit, 10 -> 32 bit, 11 -> 64 bit
 *   L: 0 -> store, 1 -> load
 *  o2: 0 -> exclusive, 1 -> not
 *  o1: 0 -> single register, 1 -> register pair
 *  o0: 1 -> load-acquire/store-release, 0 -> not
 */
static void disas_ldst_excl(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rt2 = extract32(insn, 10, 5);
    int rs = extract32(insn, 16, 5);
    int is_lasr = extract32(insn, 15, 1);
    int o2_L_o1_o0 = extract32(insn, 21, 3) * 2 | is_lasr;
    int size = extract32(insn, 30, 2);
    TCGv_i64 clean_addr;

    switch (o2_L_o1_o0) {
    case 0x0: /* STXR */
    case 0x1: /* STLXR */
        if (rn == 31) {
            gen_check_sp_alignment(s);
        }
        if (is_lasr) {
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
        }
        clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn),
                                    true, rn != 31, size);
        gen_store_exclusive(s, rs, rt, rt2, clean_addr, size, false);
        return;

    case 0x4: /* LDXR */
    case 0x5: /* LDAXR */
        if (rn == 31) {
            gen_check_sp_alignment(s);
        }
        clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn),
                                    false, rn != 31, size);
        s->is_ldex = true;
        gen_load_exclusive(s, rt, rt2, clean_addr, size, false);
        if (is_lasr) {
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
        }
        return;

    case 0x8: /* STLLR */
        if (!dc_isar_feature(aa64_lor, s)) {
            break;
        }
        /* StoreLORelease is the same as Store-Release for QEMU.  */
        /* fall through */
    case 0x9: /* STLR */
        /* Generate ISS for non-exclusive accesses including LASR.  */
        if (rn == 31) {
            gen_check_sp_alignment(s);
        }
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
        clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn),
                                    true, rn != 31, size);
        do_gpr_st(s, cpu_reg(s, rt), clean_addr, size, true, rt,
                  disas_ldst_compute_iss_sf(size, false, 0), is_lasr);
        return;

    case 0xc: /* LDLAR */
        if (!dc_isar_feature(aa64_lor, s)) {
            break;
        }
        /* LoadLOAcquire is the same as Load-Acquire for QEMU.  */
        /* fall through */
    case 0xd: /* LDAR */
        /* Generate ISS for non-exclusive accesses including LASR.  */
        if (rn == 31) {
            gen_check_sp_alignment(s);
        }
        clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn),
                                    false, rn != 31, size);
        do_gpr_ld(s, cpu_reg(s, rt), clean_addr, size, false, false, true, rt,
                  disas_ldst_compute_iss_sf(size, false, 0), is_lasr);
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
        return;

    case 0x2: case 0x3: /* CASP / STXP */
        if (size & 2) { /* STXP / STLXP */
            if (rn == 31) {
                gen_check_sp_alignment(s);
            }
            if (is_lasr) {
                tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
            }
            clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn),
                                        true, rn != 31, size);
            gen_store_exclusive(s, rs, rt, rt2, clean_addr, size, true);
            return;
        }
        if (rt2 == 31
            && ((rt | rs) & 1) == 0
            && dc_isar_feature(aa64_atomics, s)) {
            /* CASP / CASPL */
            gen_compare_and_swap_pair(s, rs, rt, rn, size | 2);
            return;
        }
        break;

    case 0x6: case 0x7: /* CASPA / LDXP */
        if (size & 2) { /* LDXP / LDAXP */
            if (rn == 31) {
                gen_check_sp_alignment(s);
            }
            clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn),
                                        false, rn != 31, size);
            s->is_ldex = true;
            gen_load_exclusive(s, rt, rt2, clean_addr, size, true);
            if (is_lasr) {
                tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
            }
            return;
        }
        if (rt2 == 31
            && ((rt | rs) & 1) == 0
            && dc_isar_feature(aa64_atomics, s)) {
            /* CASPA / CASPAL */
            gen_compare_and_swap_pair(s, rs, rt, rn, size | 2);
            return;
        }
        break;

    case 0xa: /* CAS */
    case 0xb: /* CASL */
    case 0xe: /* CASA */
    case 0xf: /* CASAL */
        if (rt2 == 31 && dc_isar_feature(aa64_atomics, s)) {
            gen_compare_and_swap(s, rs, rt, rn, size);
            return;
        }
        break;
    }
    unallocated_encoding(s);
}

/*
 * Load register (literal)
 *
 *  31 30 29   27  26 25 24 23                5 4     0
 * +-----+-------+---+-----+-------------------+-------+
 * | opc | 0 1 1 | V | 0 0 |     imm19         |  Rt   |
 * +-----+-------+---+-----+-------------------+-------+
 *
 * V: 1 -> vector (simd/fp)
 * opc (non-vector): 00 -> 32 bit, 01 -> 64 bit,
 *                   10-> 32 bit signed, 11 -> prefetch
 * opc (vector): 00 -> 32 bit, 01 -> 64 bit, 10 -> 128 bit (11 unallocated)
 */
static void disas_ld_lit(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int64_t imm = sextract32(insn, 5, 19) << 2;
    bool is_vector = extract32(insn, 26, 1);
    int opc = extract32(insn, 30, 2);
    bool is_signed = false;
    int size = 2;
    TCGv_i64 tcg_rt, clean_addr;

    if (is_vector) {
        if (opc == 3) {
            unallocated_encoding(s);
            return;
        }
        size = 2 + opc;
        if (!fp_access_check(s)) {
            return;
        }
    } else {
        if (opc == 3) {
            /* PRFM (literal) : prefetch */
            return;
        }
        size = 2 + extract32(opc, 0, 1);
        is_signed = extract32(opc, 1, 1);
    }

    tcg_rt = cpu_reg(s, rt);

    clean_addr = tcg_const_i64(s->pc_curr + imm);
    if (is_vector) {
        do_fp_ld(s, rt, clean_addr, size);
    } else {
        /* Only unsigned 32bit loads target 32bit registers.  */
        bool iss_sf = opc != 0;

        do_gpr_ld(s, tcg_rt, clean_addr, size, is_signed, false,
                  true, rt, iss_sf, false);
    }
    tcg_temp_free_i64(clean_addr);
}

/*
 * LDNP (Load Pair - non-temporal hint)
 * LDP (Load Pair - non vector)
 * LDPSW (Load Pair Signed Word - non vector)
 * STNP (Store Pair - non-temporal hint)
 * STP (Store Pair - non vector)
 * LDNP (Load Pair of SIMD&FP - non-temporal hint)
 * LDP (Load Pair of SIMD&FP)
 * STNP (Store Pair of SIMD&FP - non-temporal hint)
 * STP (Store Pair of SIMD&FP)
 *
 *  31 30 29   27  26  25 24   23  22 21   15 14   10 9    5 4    0
 * +-----+-------+---+---+-------+---+-----------------------------+
 * | opc | 1 0 1 | V | 0 | index | L |  imm7 |  Rt2  |  Rn  | Rt   |
 * +-----+-------+---+---+-------+---+-------+-------+------+------+
 *
 * opc: LDP/STP/LDNP/STNP        00 -> 32 bit, 10 -> 64 bit
 *      LDPSW/STGP               01
 *      LDP/STP/LDNP/STNP (SIMD) 00 -> 32 bit, 01 -> 64 bit, 10 -> 128 bit
 *   V: 0 -> GPR, 1 -> Vector
 * idx: 00 -> signed offset with non-temporal hint, 01 -> post-index,
 *      10 -> signed offset, 11 -> pre-index
 *   L: 0 -> Store 1 -> Load
 *
 * Rt, Rt2 = GPR or SIMD registers to be stored
 * Rn = general purpose register containing address
 * imm7 = signed offset (multiple of 4 or 8 depending on size)
 */
static void disas_ldst_pair(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rt2 = extract32(insn, 10, 5);
    uint64_t offset = sextract64(insn, 15, 7);
    int index = extract32(insn, 23, 2);
    bool is_vector = extract32(insn, 26, 1);
    bool is_load = extract32(insn, 22, 1);
    int opc = extract32(insn, 30, 2);

    bool is_signed = false;
    bool postindex = false;
    bool wback = false;
    bool set_tag = false;

    TCGv_i64 clean_addr, dirty_addr;

    int size;

    if (opc == 3) {
        unallocated_encoding(s);
        return;
    }

    if (is_vector) {
        size = 2 + opc;
    } else if (opc == 1 && !is_load) {
        /* STGP */
        if (!dc_isar_feature(aa64_mte_insn_reg, s) || index == 0) {
            unallocated_encoding(s);
            return;
        }
        size = 3;
        set_tag = true;
    } else {
        size = 2 + extract32(opc, 1, 1);
        is_signed = extract32(opc, 0, 1);
        if (!is_load && is_signed) {
            unallocated_encoding(s);
            return;
        }
    }

    switch (index) {
    case 1: /* post-index */
        postindex = true;
        wback = true;
        break;
    case 0:
        /* signed offset with "non-temporal" hint. Since we don't emulate
         * caches we don't care about hints to the cache system about
         * data access patterns, and handle this identically to plain
         * signed offset.
         */
        if (is_signed) {
            /* There is no non-temporal-hint version of LDPSW */
            unallocated_encoding(s);
            return;
        }
        postindex = false;
        break;
    case 2: /* signed offset, rn not updated */
        postindex = false;
        break;
    case 3: /* pre-index */
        postindex = false;
        wback = true;
        break;
    }

    if (is_vector && !fp_access_check(s)) {
        return;
    }

    offset <<= (set_tag ? LOG2_TAG_GRANULE : size);

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    dirty_addr = read_cpu_reg_sp(s, rn, 1);
    if (!postindex) {
        tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
    }

    if (set_tag) {
        if (!s->ata) {
            /*
             * TODO: We could rely on the stores below, at least for
             * system mode, if we arrange to add MO_ALIGN_16.
             */
            gen_helper_stg_stub(cpu_env, dirty_addr);
        } else if (tb_cflags(s->base.tb) & CF_PARALLEL) {
            gen_helper_stg_parallel(cpu_env, dirty_addr, dirty_addr);
        } else {
            gen_helper_stg(cpu_env, dirty_addr, dirty_addr);
        }
    }

    clean_addr = gen_mte_checkN(s, dirty_addr, !is_load,
                                (wback || rn != 31) && !set_tag,
                                size, 2 << size);

    if (is_vector) {
        if (is_load) {
            do_fp_ld(s, rt, clean_addr, size);
        } else {
            do_fp_st(s, rt, clean_addr, size);
        }
        tcg_gen_addi_i64(clean_addr, clean_addr, 1 << size);
        if (is_load) {
            do_fp_ld(s, rt2, clean_addr, size);
        } else {
            do_fp_st(s, rt2, clean_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        TCGv_i64 tcg_rt2 = cpu_reg(s, rt2);

        if (is_load) {
            TCGv_i64 tmp = tcg_temp_new_i64();

            /* Do not modify tcg_rt before recognizing any exception
             * from the second load.
             */
            do_gpr_ld(s, tmp, clean_addr, size, is_signed, false,
                      false, 0, false, false);
            tcg_gen_addi_i64(clean_addr, clean_addr, 1 << size);
            do_gpr_ld(s, tcg_rt2, clean_addr, size, is_signed, false,
                      false, 0, false, false);

            tcg_gen_mov_i64(tcg_rt, tmp);
            tcg_temp_free_i64(tmp);
        } else {
            do_gpr_st(s, tcg_rt, clean_addr, size,
                      false, 0, false, false);
            tcg_gen_addi_i64(clean_addr, clean_addr, 1 << size);
            do_gpr_st(s, tcg_rt2, clean_addr, size,
                      false, 0, false, false);
        }
    }

    if (wback) {
        if (postindex) {
            tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, rn), dirty_addr);
    }
}

/*
 * Load/store (immediate post-indexed)
 * Load/store (immediate pre-indexed)
 * Load/store (unscaled immediate)
 *
 * 31 30 29   27  26 25 24 23 22 21  20    12 11 10 9    5 4    0
 * +----+-------+---+-----+-----+---+--------+-----+------+------+
 * |size| 1 1 1 | V | 0 0 | opc | 0 |  imm9  | idx |  Rn  |  Rt  |
 * +----+-------+---+-----+-----+---+--------+-----+------+------+
 *
 * idx = 01 -> post-indexed, 11 pre-indexed, 00 unscaled imm. (no writeback)
         10 -> unprivileged
 * V = 0 -> non-vector
 * size: 00 -> 8 bit, 01 -> 16 bit, 10 -> 32 bit, 11 -> 64bit
 * opc: 00 -> store, 01 -> loadu, 10 -> loads 64, 11 -> loads 32
 */
static void disas_ldst_reg_imm9(DisasContext *s, uint32_t insn,
                                int opc,
                                int size,
                                int rt,
                                bool is_vector)
{
    int rn = extract32(insn, 5, 5);
    int imm9 = sextract32(insn, 12, 9);
    int idx = extract32(insn, 10, 2);
    bool is_signed = false;
    bool is_store = false;
    bool is_extended = false;
    bool is_unpriv = (idx == 2);
    bool iss_valid = !is_vector;
    bool post_index;
    bool writeback;
    int memidx;

    TCGv_i64 clean_addr, dirty_addr;

    if (is_vector) {
        size |= (opc & 2) << 1;
        if (size > 4 || is_unpriv) {
            unallocated_encoding(s);
            return;
        }
        is_store = ((opc & 1) == 0);
        if (!fp_access_check(s)) {
            return;
        }
    } else {
        if (size == 3 && opc == 2) {
            /* PRFM - prefetch */
            if (idx != 0) {
                unallocated_encoding(s);
                return;
            }
            return;
        }
        if (opc == 3 && size > 1) {
            unallocated_encoding(s);
            return;
        }
        is_store = (opc == 0);
        is_signed = extract32(opc, 1, 1);
        is_extended = (size < 3) && extract32(opc, 0, 1);
    }

    switch (idx) {
    case 0:
    case 2:
        post_index = false;
        writeback = false;
        break;
    case 1:
        post_index = true;
        writeback = true;
        break;
    case 3:
        post_index = false;
        writeback = true;
        break;
    default:
        g_assert_not_reached();
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    dirty_addr = read_cpu_reg_sp(s, rn, 1);
    if (!post_index) {
        tcg_gen_addi_i64(dirty_addr, dirty_addr, imm9);
    }

    memidx = is_unpriv ? get_a64_user_mem_index(s) : get_mem_index(s);
    clean_addr = gen_mte_check1_mmuidx(s, dirty_addr, is_store,
                                       writeback || rn != 31,
                                       size, is_unpriv, memidx);

    if (is_vector) {
        if (is_store) {
            do_fp_st(s, rt, clean_addr, size);
        } else {
            do_fp_ld(s, rt, clean_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        bool iss_sf = disas_ldst_compute_iss_sf(size, is_signed, opc);

        if (is_store) {
            do_gpr_st_memidx(s, tcg_rt, clean_addr, size, memidx,
                             iss_valid, rt, iss_sf, false);
        } else {
            do_gpr_ld_memidx(s, tcg_rt, clean_addr, size,
                             is_signed, is_extended, memidx,
                             iss_valid, rt, iss_sf, false);
        }
    }

    if (writeback) {
        TCGv_i64 tcg_rn = cpu_reg_sp(s, rn);
        if (post_index) {
            tcg_gen_addi_i64(dirty_addr, dirty_addr, imm9);
        }
        tcg_gen_mov_i64(tcg_rn, dirty_addr);
    }
}

/*
 * Load/store (register offset)
 *
 * 31 30 29   27  26 25 24 23 22 21  20  16 15 13 12 11 10 9  5 4  0
 * +----+-------+---+-----+-----+---+------+-----+--+-----+----+----+
 * |size| 1 1 1 | V | 0 0 | opc | 1 |  Rm  | opt | S| 1 0 | Rn | Rt |
 * +----+-------+---+-----+-----+---+------+-----+--+-----+----+----+
 *
 * For non-vector:
 *   size: 00-> byte, 01 -> 16 bit, 10 -> 32bit, 11 -> 64bit
 *   opc: 00 -> store, 01 -> loadu, 10 -> loads 64, 11 -> loads 32
 * For vector:
 *   size is opc<1>:size<1:0> so 100 -> 128 bit; 110 and 111 unallocated
 *   opc<0>: 0 -> store, 1 -> load
 * V: 1 -> vector/simd
 * opt: extend encoding (see DecodeRegExtend)
 * S: if S=1 then scale (essentially index by sizeof(size))
 * Rt: register to transfer into/out of
 * Rn: address register or SP for base
 * Rm: offset register or ZR for offset
 */
static void disas_ldst_reg_roffset(DisasContext *s, uint32_t insn,
                                   int opc,
                                   int size,
                                   int rt,
                                   bool is_vector)
{
    int rn = extract32(insn, 5, 5);
    int shift = extract32(insn, 12, 1);
    int rm = extract32(insn, 16, 5);
    int opt = extract32(insn, 13, 3);
    bool is_signed = false;
    bool is_store = false;
    bool is_extended = false;

    TCGv_i64 tcg_rm, clean_addr, dirty_addr;

    if (extract32(opt, 1, 1) == 0) {
        unallocated_encoding(s);
        return;
    }

    if (is_vector) {
        size |= (opc & 2) << 1;
        if (size > 4) {
            unallocated_encoding(s);
            return;
        }
        is_store = !extract32(opc, 0, 1);
        if (!fp_access_check(s)) {
            return;
        }
    } else {
        if (size == 3 && opc == 2) {
            /* PRFM - prefetch */
            return;
        }
        if (opc == 3 && size > 1) {
            unallocated_encoding(s);
            return;
        }
        is_store = (opc == 0);
        is_signed = extract32(opc, 1, 1);
        is_extended = (size < 3) && extract32(opc, 0, 1);
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    dirty_addr = read_cpu_reg_sp(s, rn, 1);

    tcg_rm = read_cpu_reg(s, rm, 1);
    ext_and_shift_reg(tcg_rm, tcg_rm, opt, shift ? size : 0);

    tcg_gen_add_i64(dirty_addr, dirty_addr, tcg_rm);
    clean_addr = gen_mte_check1(s, dirty_addr, is_store, true, size);

    if (is_vector) {
        if (is_store) {
            do_fp_st(s, rt, clean_addr, size);
        } else {
            do_fp_ld(s, rt, clean_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        bool iss_sf = disas_ldst_compute_iss_sf(size, is_signed, opc);
        if (is_store) {
            do_gpr_st(s, tcg_rt, clean_addr, size,
                      true, rt, iss_sf, false);
        } else {
            do_gpr_ld(s, tcg_rt, clean_addr, size,
                      is_signed, is_extended,
                      true, rt, iss_sf, false);
        }
    }
}

/*
 * Load/store (unsigned immediate)
 *
 * 31 30 29   27  26 25 24 23 22 21        10 9     5
 * +----+-------+---+-----+-----+------------+-------+------+
 * |size| 1 1 1 | V | 0 1 | opc |   imm12    |  Rn   |  Rt  |
 * +----+-------+---+-----+-----+------------+-------+------+
 *
 * For non-vector:
 *   size: 00-> byte, 01 -> 16 bit, 10 -> 32bit, 11 -> 64bit
 *   opc: 00 -> store, 01 -> loadu, 10 -> loads 64, 11 -> loads 32
 * For vector:
 *   size is opc<1>:size<1:0> so 100 -> 128 bit; 110 and 111 unallocated
 *   opc<0>: 0 -> store, 1 -> load
 * Rn: base address register (inc SP)
 * Rt: target register
 */
static void disas_ldst_reg_unsigned_imm(DisasContext *s, uint32_t insn,
                                        int opc,
                                        int size,
                                        int rt,
                                        bool is_vector)
{
    int rn = extract32(insn, 5, 5);
    unsigned int imm12 = extract32(insn, 10, 12);
    unsigned int offset;

    TCGv_i64 clean_addr, dirty_addr;

    bool is_store;
    bool is_signed = false;
    bool is_extended = false;

    if (is_vector) {
        size |= (opc & 2) << 1;
        if (size > 4) {
            unallocated_encoding(s);
            return;
        }
        is_store = !extract32(opc, 0, 1);
        if (!fp_access_check(s)) {
            return;
        }
    } else {
        if (size == 3 && opc == 2) {
            /* PRFM - prefetch */
            return;
        }
        if (opc == 3 && size > 1) {
            unallocated_encoding(s);
            return;
        }
        is_store = (opc == 0);
        is_signed = extract32(opc, 1, 1);
        is_extended = (size < 3) && extract32(opc, 0, 1);
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    dirty_addr = read_cpu_reg_sp(s, rn, 1);
    offset = imm12 << size;
    tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
    clean_addr = gen_mte_check1(s, dirty_addr, is_store, rn != 31, size);

    if (is_vector) {
        if (is_store) {
            do_fp_st(s, rt, clean_addr, size);
        } else {
            do_fp_ld(s, rt, clean_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        bool iss_sf = disas_ldst_compute_iss_sf(size, is_signed, opc);
        if (is_store) {
            do_gpr_st(s, tcg_rt, clean_addr, size,
                      true, rt, iss_sf, false);
        } else {
            do_gpr_ld(s, tcg_rt, clean_addr, size, is_signed, is_extended,
                      true, rt, iss_sf, false);
        }
    }
}

/* Atomic memory operations
 *
 *  31  30      27  26    24    22  21   16   15    12    10    5     0
 * +------+-------+---+-----+-----+---+----+----+-----+-----+----+-----+
 * | size | 1 1 1 | V | 0 0 | A R | 1 | Rs | o3 | opc | 0 0 | Rn |  Rt |
 * +------+-------+---+-----+-----+--------+----+-----+-----+----+-----+
 *
 * Rt: the result register
 * Rn: base address or SP
 * Rs: the source register for the operation
 * V: vector flag (always 0 as of v8.3)
 * A: acquire flag
 * R: release flag
 */
static void disas_ldst_atomic(DisasContext *s, uint32_t insn,
                              int size, int rt, bool is_vector)
{
    int rs = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int o3_opc = extract32(insn, 12, 4);
    bool r = extract32(insn, 22, 1);
    bool a = extract32(insn, 23, 1);
    TCGv_i64 tcg_rs, clean_addr;
    AtomicThreeOpFn *fn = NULL;

    if (is_vector || !dc_isar_feature(aa64_atomics, s)) {
        unallocated_encoding(s);
        return;
    }
    switch (o3_opc) {
    case 000: /* LDADD */
        fn = tcg_gen_atomic_fetch_add_i64;
        break;
    case 001: /* LDCLR */
        fn = tcg_gen_atomic_fetch_and_i64;
        break;
    case 002: /* LDEOR */
        fn = tcg_gen_atomic_fetch_xor_i64;
        break;
    case 003: /* LDSET */
        fn = tcg_gen_atomic_fetch_or_i64;
        break;
    case 004: /* LDSMAX */
        fn = tcg_gen_atomic_fetch_smax_i64;
        break;
    case 005: /* LDSMIN */
        fn = tcg_gen_atomic_fetch_smin_i64;
        break;
    case 006: /* LDUMAX */
        fn = tcg_gen_atomic_fetch_umax_i64;
        break;
    case 007: /* LDUMIN */
        fn = tcg_gen_atomic_fetch_umin_i64;
        break;
    case 010: /* SWP */
        fn = tcg_gen_atomic_xchg_i64;
        break;
    case 014: /* LDAPR, LDAPRH, LDAPRB */
        if (!dc_isar_feature(aa64_rcpc_8_3, s) ||
            rs != 31 || a != 1 || r != 0) {
            unallocated_encoding(s);
            return;
        }
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn), false, rn != 31, size);

    if (o3_opc == 014) {
        /*
         * LDAPR* are a special case because they are a simple load, not a
         * fetch-and-do-something op.
         * The architectural consistency requirements here are weaker than
         * full load-acquire (we only need "load-acquire processor consistent"),
         * but we choose to implement them as full LDAQ.
         */
        do_gpr_ld(s, cpu_reg(s, rt), clean_addr, size, false, false,
                  true, rt, disas_ldst_compute_iss_sf(size, false, 0), true);
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
        return;
    }

    tcg_rs = read_cpu_reg(s, rs, true);

    if (o3_opc == 1) { /* LDCLR */
        tcg_gen_not_i64(tcg_rs, tcg_rs);
    }

    /* The tcg atomic primitives are all full barriers.  Therefore we
     * can ignore the Acquire and Release bits of this instruction.
     */
    fn(cpu_reg(s, rt), clean_addr, tcg_rs, get_mem_index(s),
       s->be_data | size | MO_ALIGN);
}

/*
 * PAC memory operations
 *
 *  31  30      27  26    24    22  21       12  11  10    5     0
 * +------+-------+---+-----+-----+---+--------+---+---+----+-----+
 * | size | 1 1 1 | V | 0 0 | M S | 1 |  imm9  | W | 1 | Rn |  Rt |
 * +------+-------+---+-----+-----+---+--------+---+---+----+-----+
 *
 * Rt: the result register
 * Rn: base address or SP
 * V: vector flag (always 0 as of v8.3)
 * M: clear for key DA, set for key DB
 * W: pre-indexing flag
 * S: sign for imm9.
 */
static void disas_ldst_pac(DisasContext *s, uint32_t insn,
                           int size, int rt, bool is_vector)
{
    int rn = extract32(insn, 5, 5);
    bool is_wback = extract32(insn, 11, 1);
    bool use_key_a = !extract32(insn, 23, 1);
    int offset;
    TCGv_i64 clean_addr, dirty_addr, tcg_rt;

    if (size != 3 || is_vector || !dc_isar_feature(aa64_pauth, s)) {
        unallocated_encoding(s);
        return;
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    dirty_addr = read_cpu_reg_sp(s, rn, 1);

    if (s->pauth_active) {
        if (use_key_a) {
            gen_helper_autda(dirty_addr, cpu_env, dirty_addr,
                             new_tmp_a64_zero(s));
        } else {
            gen_helper_autdb(dirty_addr, cpu_env, dirty_addr,
                             new_tmp_a64_zero(s));
        }
    }

    /* Form the 10-bit signed, scaled offset.  */
    offset = (extract32(insn, 22, 1) << 9) | extract32(insn, 12, 9);
    offset = sextract32(offset << size, 0, 10 + size);
    tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);

    /* Note that "clean" and "dirty" here refer to TBI not PAC.  */
    clean_addr = gen_mte_check1(s, dirty_addr, false,
                                is_wback || rn != 31, size);

    tcg_rt = cpu_reg(s, rt);
    do_gpr_ld(s, tcg_rt, clean_addr, size, /* is_signed */ false,
              /* extend */ false, /* iss_valid */ !is_wback,
              /* iss_srt */ rt, /* iss_sf */ true, /* iss_ar */ false);

    if (is_wback) {
        tcg_gen_mov_i64(cpu_reg_sp(s, rn), dirty_addr);
    }
}

/*
 * LDAPR/STLR (unscaled immediate)
 *
 *  31  30            24    22  21       12    10    5     0
 * +------+-------------+-----+---+--------+-----+----+-----+
 * | size | 0 1 1 0 0 1 | opc | 0 |  imm9  | 0 0 | Rn |  Rt |
 * +------+-------------+-----+---+--------+-----+----+-----+
 *
 * Rt: source or destination register
 * Rn: base register
 * imm9: unscaled immediate offset
 * opc: 00: STLUR*, 01/10/11: various LDAPUR*
 * size: size of load/store
 */
static void disas_ldst_ldapr_stlr(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int offset = sextract32(insn, 12, 9);
    int opc = extract32(insn, 22, 2);
    int size = extract32(insn, 30, 2);
    TCGv_i64 clean_addr, dirty_addr;
    bool is_store = false;
    bool is_signed = false;
    bool extend = false;
    bool iss_sf;

    if (!dc_isar_feature(aa64_rcpc_8_4, s)) {
        unallocated_encoding(s);
        return;
    }

    switch (opc) {
    case 0: /* STLURB */
        is_store = true;
        break;
    case 1: /* LDAPUR* */
        break;
    case 2: /* LDAPURS* 64-bit variant */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        is_signed = true;
        break;
    case 3: /* LDAPURS* 32-bit variant */
        if (size > 1) {
            unallocated_encoding(s);
            return;
        }
        is_signed = true;
        extend = true; /* zero-extend 32->64 after signed load */
        break;
    default:
        g_assert_not_reached();
    }

    iss_sf = disas_ldst_compute_iss_sf(size, is_signed, opc);

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    dirty_addr = read_cpu_reg_sp(s, rn, 1);
    tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
    clean_addr = clean_data_tbi(s, dirty_addr);

    if (is_store) {
        /* Store-Release semantics */
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
        do_gpr_st(s, cpu_reg(s, rt), clean_addr, size, true, rt, iss_sf, true);
    } else {
        /*
         * Load-AcquirePC semantics; we implement as the slightly more
         * restrictive Load-Acquire.
         */
        do_gpr_ld(s, cpu_reg(s, rt), clean_addr, size, is_signed, extend,
                  true, rt, iss_sf, true);
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }
}

/* Load/store register (all forms) */
static void disas_ldst_reg(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int opc = extract32(insn, 22, 2);
    bool is_vector = extract32(insn, 26, 1);
    int size = extract32(insn, 30, 2);

    switch (extract32(insn, 24, 2)) {
    case 0:
        if (extract32(insn, 21, 1) == 0) {
            /* Load/store register (unscaled immediate)
             * Load/store immediate pre/post-indexed
             * Load/store register unprivileged
             */
            disas_ldst_reg_imm9(s, insn, opc, size, rt, is_vector);
            return;
        }
        switch (extract32(insn, 10, 2)) {
        case 0:
            disas_ldst_atomic(s, insn, size, rt, is_vector);
            return;
        case 2:
            disas_ldst_reg_roffset(s, insn, opc, size, rt, is_vector);
            return;
        default:
            disas_ldst_pac(s, insn, size, rt, is_vector);
            return;
        }
        break;
    case 1:
        disas_ldst_reg_unsigned_imm(s, insn, opc, size, rt, is_vector);
        return;
    }
    unallocated_encoding(s);
}

/* AdvSIMD load/store multiple structures
 *
 *  31  30  29           23 22  21         16 15    12 11  10 9    5 4    0
 * +---+---+---------------+---+-------------+--------+------+------+------+
 * | 0 | Q | 0 0 1 1 0 0 0 | L | 0 0 0 0 0 0 | opcode | size |  Rn  |  Rt  |
 * +---+---+---------------+---+-------------+--------+------+------+------+
 *
 * AdvSIMD load/store multiple structures (post-indexed)
 *
 *  31  30  29           23 22  21  20     16 15    12 11  10 9    5 4    0
 * +---+---+---------------+---+---+---------+--------+------+------+------+
 * | 0 | Q | 0 0 1 1 0 0 1 | L | 0 |   Rm    | opcode | size |  Rn  |  Rt  |
 * +---+---+---------------+---+---+---------+--------+------+------+------+
 *
 * Rt: first (or only) SIMD&FP register to be transferred
 * Rn: base address or SP
 * Rm (post-index only): post-index register (when !31) or size dependent #imm
 */
static void disas_ldst_multiple_struct(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 10, 2);
    int opcode = extract32(insn, 12, 4);
    bool is_store = !extract32(insn, 22, 1);
    bool is_postidx = extract32(insn, 23, 1);
    bool is_q = extract32(insn, 30, 1);
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;
    MemOp endian = s->be_data;

    int total;    /* total bytes */
    int elements; /* elements per vector */
    int rpt;    /* num iterations */
    int selem;  /* structure elements */
    int r;

    if (extract32(insn, 31, 1) || extract32(insn, 21, 1)) {
        unallocated_encoding(s);
        return;
    }

    if (!is_postidx && rm != 0) {
        unallocated_encoding(s);
        return;
    }

    /* From the shared decode logic */
    switch (opcode) {
    case 0x0:
        rpt = 1;
        selem = 4;
        break;
    case 0x2:
        rpt = 4;
        selem = 1;
        break;
    case 0x4:
        rpt = 1;
        selem = 3;
        break;
    case 0x6:
        rpt = 3;
        selem = 1;
        break;
    case 0x7:
        rpt = 1;
        selem = 1;
        break;
    case 0x8:
        rpt = 1;
        selem = 2;
        break;
    case 0xa:
        rpt = 2;
        selem = 1;
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (size == 3 && !is_q && selem != 1) {
        /* reserved */
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    /* For our purposes, bytes are always little-endian.  */
    if (size == 0) {
        endian = MO_LE;
    }

    total = rpt * selem * (is_q ? 16 : 8);
    tcg_rn = cpu_reg_sp(s, rn);

    /*
     * Issue the MTE check vs the logical repeat count, before we
     * promote consecutive little-endian elements below.
     */
    clean_addr = gen_mte_checkN(s, tcg_rn, is_store, is_postidx || rn != 31,
                                size, total);

    /*
     * Consecutive little-endian elements from a single register
     * can be promoted to a larger little-endian operation.
     */
    if (selem == 1 && endian == MO_LE) {
        size = 3;
    }
    elements = (is_q ? 16 : 8) >> size;

    tcg_ebytes = tcg_const_i64(1 << size);
    for (r = 0; r < rpt; r++) {
        int e;
        for (e = 0; e < elements; e++) {
            int xs;
            for (xs = 0; xs < selem; xs++) {
                int tt = (rt + r + xs) % 32;
                if (is_store) {
                    do_vec_st(s, tt, e, clean_addr, size, endian);
                } else {
                    do_vec_ld(s, tt, e, clean_addr, size, endian);
                }
                tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
            }
        }
    }
    tcg_temp_free_i64(tcg_ebytes);

    if (!is_store) {
        /* For non-quad operations, setting a slice of the low
         * 64 bits of the register clears the high 64 bits (in
         * the ARM ARM pseudocode this is implicit in the fact
         * that 'rval' is a 64 bit wide variable).
         * For quad operations, we might still need to zero the
         * high bits of SVE.
         */
        for (r = 0; r < rpt * selem; r++) {
            int tt = (rt + r) % 32;
            clear_vec_high(s, is_q, tt);
        }
    }

    if (is_postidx) {
        if (rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, rm));
        }
    }
}

/* AdvSIMD load/store single structure
 *
 *  31  30  29           23 22 21 20       16 15 13 12  11  10 9    5 4    0
 * +---+---+---------------+-----+-----------+-----+---+------+------+------+
 * | 0 | Q | 0 0 1 1 0 1 0 | L R | 0 0 0 0 0 | opc | S | size |  Rn  |  Rt  |
 * +---+---+---------------+-----+-----------+-----+---+------+------+------+
 *
 * AdvSIMD load/store single structure (post-indexed)
 *
 *  31  30  29           23 22 21 20       16 15 13 12  11  10 9    5 4    0
 * +---+---+---------------+-----+-----------+-----+---+------+------+------+
 * | 0 | Q | 0 0 1 1 0 1 1 | L R |     Rm    | opc | S | size |  Rn  |  Rt  |
 * +---+---+---------------+-----+-----------+-----+---+------+------+------+
 *
 * Rt: first (or only) SIMD&FP register to be transferred
 * Rn: base address or SP
 * Rm (post-index only): post-index register (when !31) or size dependent #imm
 * index = encoded in Q:S:size dependent on size
 *
 * lane_size = encoded in R, opc
 * transfer width = encoded in opc, S, size
 */
static void disas_ldst_single_struct(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 10, 2);
    int S = extract32(insn, 12, 1);
    int opc = extract32(insn, 13, 3);
    int R = extract32(insn, 21, 1);
    int is_load = extract32(insn, 22, 1);
    int is_postidx = extract32(insn, 23, 1);
    int is_q = extract32(insn, 30, 1);

    int scale = extract32(opc, 1, 2);
    int selem = (extract32(opc, 0, 1) << 1 | R) + 1;
    bool replicate = false;
    int index = is_q << 3 | S << 2 | size;
    int xs, total;
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;

    if (extract32(insn, 31, 1)) {
        unallocated_encoding(s);
        return;
    }
    if (!is_postidx && rm != 0) {
        unallocated_encoding(s);
        return;
    }

    switch (scale) {
    case 3:
        if (!is_load || S) {
            unallocated_encoding(s);
            return;
        }
        scale = size;
        replicate = true;
        break;
    case 0:
        break;
    case 1:
        if (extract32(size, 0, 1)) {
            unallocated_encoding(s);
            return;
        }
        index >>= 1;
        break;
    case 2:
        if (extract32(size, 1, 1)) {
            unallocated_encoding(s);
            return;
        }
        if (!extract32(size, 0, 1)) {
            index >>= 2;
        } else {
            if (S) {
                unallocated_encoding(s);
                return;
            }
            index >>= 3;
            scale = 3;
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    total = selem << scale;
    tcg_rn = cpu_reg_sp(s, rn);

    clean_addr = gen_mte_checkN(s, tcg_rn, !is_load, is_postidx || rn != 31,
                                scale, total);

    tcg_ebytes = tcg_const_i64(1 << scale);
    for (xs = 0; xs < selem; xs++) {
        if (replicate) {
            /* Load and replicate to all elements */
            TCGv_i64 tcg_tmp = tcg_temp_new_i64();

            tcg_gen_qemu_ld_i64(tcg_tmp, clean_addr,
                                get_mem_index(s), s->be_data + scale);
            tcg_gen_gvec_dup_i64(scale, vec_full_reg_offset(s, rt),
                                 (is_q + 1) * 8, vec_full_reg_size(s),
                                 tcg_tmp);
            tcg_temp_free_i64(tcg_tmp);
        } else {
            /* Load/store one element per register */
            if (is_load) {
                do_vec_ld(s, rt, index, clean_addr, scale, s->be_data);
            } else {
                do_vec_st(s, rt, index, clean_addr, scale, s->be_data);
            }
        }
        tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
        rt = (rt + 1) % 32;
    }
    tcg_temp_free_i64(tcg_ebytes);

    if (is_postidx) {
        if (rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, rm));
        }
    }
}

/*
 * Load/Store memory tags
 *
 *  31 30 29         24     22  21     12    10      5      0
 * +-----+-------------+-----+---+------+-----+------+------+
 * | 1 1 | 0 1 1 0 0 1 | op1 | 1 | imm9 | op2 |  Rn  |  Rt  |
 * +-----+-------------+-----+---+------+-----+------+------+
 */
static void disas_ldst_tag(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    uint64_t offset = sextract64(insn, 12, 9) << LOG2_TAG_GRANULE;
    int op2 = extract32(insn, 10, 2);
    int op1 = extract32(insn, 22, 2);
    bool is_load = false, is_pair = false, is_zero = false, is_mult = false;
    int index = 0;
    TCGv_i64 addr, clean_addr, tcg_rt;

    /* We checked insn bits [29:24,21] in the caller.  */
    if (extract32(insn, 30, 2) != 3) {
        goto do_unallocated;
    }

    /*
     * @index is a tri-state variable which has 3 states:
     * < 0 : post-index, writeback
     * = 0 : signed offset
     * > 0 : pre-index, writeback
     */
    switch (op1) {
    case 0:
        if (op2 != 0) {
            /* STG */
            index = op2 - 2;
        } else {
            /* STZGM */
            if (s->current_el == 0 || offset != 0) {
                goto do_unallocated;
            }
            is_mult = is_zero = true;
        }
        break;
    case 1:
        if (op2 != 0) {
            /* STZG */
            is_zero = true;
            index = op2 - 2;
        } else {
            /* LDG */
            is_load = true;
        }
        break;
    case 2:
        if (op2 != 0) {
            /* ST2G */
            is_pair = true;
            index = op2 - 2;
        } else {
            /* STGM */
            if (s->current_el == 0 || offset != 0) {
                goto do_unallocated;
            }
            is_mult = true;
        }
        break;
    case 3:
        if (op2 != 0) {
            /* STZ2G */
            is_pair = is_zero = true;
            index = op2 - 2;
        } else {
            /* LDGM */
            if (s->current_el == 0 || offset != 0) {
                goto do_unallocated;
            }
            is_mult = is_load = true;
        }
        break;

    default:
    do_unallocated:
        unallocated_encoding(s);
        return;
    }

    if (is_mult
        ? !dc_isar_feature(aa64_mte, s)
        : !dc_isar_feature(aa64_mte_insn_reg, s)) {
        goto do_unallocated;
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    addr = read_cpu_reg_sp(s, rn, true);
    if (index >= 0) {
        /* pre-index or signed offset */
        tcg_gen_addi_i64(addr, addr, offset);
    }

    if (is_mult) {
        tcg_rt = cpu_reg(s, rt);

        if (is_zero) {
            int size = 4 << s->dcz_blocksize;

            if (s->ata) {
                gen_helper_stzgm_tags(cpu_env, addr, tcg_rt);
            }
            /*
             * The non-tags portion of STZGM is mostly like DC_ZVA,
             * except the alignment happens before the access.
             */
            clean_addr = clean_data_tbi(s, addr);
            tcg_gen_andi_i64(clean_addr, clean_addr, -size);
            gen_helper_dc_zva(cpu_env, clean_addr);
        } else if (s->ata) {
            if (is_load) {
                gen_helper_ldgm(tcg_rt, cpu_env, addr);
            } else {
                gen_helper_stgm(cpu_env, addr, tcg_rt);
            }
        } else {
            MMUAccessType acc = is_load ? MMU_DATA_LOAD : MMU_DATA_STORE;
            int size = 4 << GMID_EL1_BS;

            clean_addr = clean_data_tbi(s, addr);
            tcg_gen_andi_i64(clean_addr, clean_addr, -size);
            gen_probe_access(s, clean_addr, acc, size);

            if (is_load) {
                /* The result tags are zeros.  */
                tcg_gen_movi_i64(tcg_rt, 0);
            }
        }
        return;
    }

    if (is_load) {
        tcg_gen_andi_i64(addr, addr, -TAG_GRANULE);
        tcg_rt = cpu_reg(s, rt);
        if (s->ata) {
            gen_helper_ldg(tcg_rt, cpu_env, addr, tcg_rt);
        } else {
            clean_addr = clean_data_tbi(s, addr);
            gen_probe_access(s, clean_addr, MMU_DATA_LOAD, MO_8);
            gen_address_with_allocation_tag0(tcg_rt, addr);
        }
    } else {
        tcg_rt = cpu_reg_sp(s, rt);
        if (!s->ata) {
            /*
             * For STG and ST2G, we need to check alignment and probe memory.
             * TODO: For STZG and STZ2G, we could rely on the stores below,
             * at least for system mode; user-only won't enforce alignment.
             */
            if (is_pair) {
                gen_helper_st2g_stub(cpu_env, addr);
            } else {
                gen_helper_stg_stub(cpu_env, addr);
            }
        } else if (tb_cflags(s->base.tb) & CF_PARALLEL) {
            if (is_pair) {
                gen_helper_st2g_parallel(cpu_env, addr, tcg_rt);
            } else {
                gen_helper_stg_parallel(cpu_env, addr, tcg_rt);
            }
        } else {
            if (is_pair) {
                gen_helper_st2g(cpu_env, addr, tcg_rt);
            } else {
                gen_helper_stg(cpu_env, addr, tcg_rt);
            }
        }
    }

    if (is_zero) {
        TCGv_i64 clean_addr = clean_data_tbi(s, addr);
        TCGv_i64 tcg_zero = tcg_const_i64(0);
        int mem_index = get_mem_index(s);
        int i, n = (1 + is_pair) << LOG2_TAG_GRANULE;

        tcg_gen_qemu_st_i64(tcg_zero, clean_addr, mem_index,
                            MO_Q | MO_ALIGN_16);
        for (i = 8; i < n; i += 8) {
            tcg_gen_addi_i64(clean_addr, clean_addr, 8);
            tcg_gen_qemu_st_i64(tcg_zero, clean_addr, mem_index, MO_Q);
        }
        tcg_temp_free_i64(tcg_zero);
    }

    if (index != 0) {
        /* pre-index or post-index */
        if (index < 0) {
            /* post-index */
            tcg_gen_addi_i64(addr, addr, offset);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, rn), addr);
    }
}

/* Loads and stores */
static void disas_ldst(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 24, 6)) {
    case 0x08: /* Load/store exclusive */
        disas_ldst_excl(s, insn);
        break;
    case 0x18: case 0x1c: /* Load register (literal) */
        disas_ld_lit(s, insn);
        break;
    case 0x28: case 0x29:
    case 0x2c: case 0x2d: /* Load/store pair (all forms) */
        disas_ldst_pair(s, insn);
        break;
    case 0x38: case 0x39:
    case 0x3c: case 0x3d: /* Load/store register (all forms) */
        disas_ldst_reg(s, insn);
        break;
    case 0x0c: /* AdvSIMD load/store multiple structures */
        disas_ldst_multiple_struct(s, insn);
        break;
    case 0x0d: /* AdvSIMD load/store single structure */
        disas_ldst_single_struct(s, insn);
        break;
    case 0x19:
        if (extract32(insn, 21, 1) != 0) {
            disas_ldst_tag(s, insn);
        } else if (extract32(insn, 10, 2) == 0) {
            disas_ldst_ldapr_stlr(s, insn);
        } else {
            unallocated_encoding(s);
        }
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* PC-rel. addressing
 *   31  30   29 28       24 23                5 4    0
 * +----+-------+-----------+-------------------+------+
 * | op | immlo | 1 0 0 0 0 |       immhi       |  Rd  |
 * +----+-------+-----------+-------------------+------+
 */
static void disas_pc_rel_adr(DisasContext *s, uint32_t insn)
{
    unsigned int page, rd;
    uint64_t base;
    uint64_t offset;

    page = extract32(insn, 31, 1);
    /* SignExtend(immhi:immlo) -> offset */
    offset = sextract64(insn, 5, 19);
    offset = offset << 2 | extract32(insn, 29, 2);
    rd = extract32(insn, 0, 5);
    base = s->pc_curr;

    if (page) {
        /* ADRP (page based) */
        base &= ~0xfff;
        offset <<= 12;
    }

    tcg_gen_movi_i64(cpu_reg(s, rd), base + offset);
}

/*
 * Add/subtract (immediate)
 *
 *  31 30 29 28         23 22 21         10 9   5 4   0
 * +--+--+--+-------------+--+-------------+-----+-----+
 * |sf|op| S| 1 0 0 0 1 0 |sh|    imm12    |  Rn | Rd  |
 * +--+--+--+-------------+--+-------------+-----+-----+
 *
 *    sf: 0 -> 32bit, 1 -> 64bit
 *    op: 0 -> add  , 1 -> sub
 *     S: 1 -> set flags
 *    sh: 1 -> LSL imm by 12
 */
static void disas_add_sub_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    uint64_t imm = extract32(insn, 10, 12);
    bool shift = extract32(insn, 22, 1);
    bool setflags = extract32(insn, 29, 1);
    bool sub_op = extract32(insn, 30, 1);
    bool is_64bit = extract32(insn, 31, 1);

    TCGv_i64 tcg_rn = cpu_reg_sp(s, rn);
    TCGv_i64 tcg_rd = setflags ? cpu_reg(s, rd) : cpu_reg_sp(s, rd);
    TCGv_i64 tcg_result;

    if (shift) {
        imm <<= 12;
    }
    
    if (rd == 31 && sub_op) { // cmp xX, imm
      TCGv_i64 tcg_imm = tcg_const_i64(imm);
      afl_gen_compcov(s->pc_curr, tcg_rn, tcg_imm, is_64bit ? MO_64 : MO_32, 1);
      tcg_temp_free_i64(tcg_imm);
    }

    tcg_result = tcg_temp_new_i64();
    if (!setflags) {
        if (sub_op) {
            tcg_gen_subi_i64(tcg_result, tcg_rn, imm);
        } else {
            tcg_gen_addi_i64(tcg_result, tcg_rn, imm);
        }
    } else {
        TCGv_i64 tcg_imm = tcg_const_i64(imm);
        if (sub_op) {
            gen_sub_CC(is_64bit, tcg_result, tcg_rn, tcg_imm);
        } else {
            gen_add_CC(is_64bit, tcg_result, tcg_rn, tcg_imm);
        }
        tcg_temp_free_i64(tcg_imm);
    }

    if (is_64bit) {
        tcg_gen_mov_i64(tcg_rd, tcg_result);
    } else {
        tcg_gen_ext32u_i64(tcg_rd, tcg_result);
    }

    tcg_temp_free_i64(tcg_result);
}

/*
 * Add/subtract (immediate, with tags)
 *
 *  31 30 29 28         23 22 21     16 14      10 9   5 4   0
 * +--+--+--+-------------+--+---------+--+-------+-----+-----+
 * |sf|op| S| 1 0 0 0 1 1 |o2|  uimm6  |o3| uimm4 |  Rn | Rd  |
 * +--+--+--+-------------+--+---------+--+-------+-----+-----+
 *
 *    op: 0 -> add, 1 -> sub
 */
static void disas_add_sub_imm_with_tags(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int uimm4 = extract32(insn, 10, 4);
    int uimm6 = extract32(insn, 16, 6);
    bool sub_op = extract32(insn, 30, 1);
    TCGv_i64 tcg_rn, tcg_rd;
    int imm;

    /* Test all of sf=1, S=0, o2=0, o3=0.  */
    if ((insn & 0xa040c000u) != 0x80000000u ||
        !dc_isar_feature(aa64_mte_insn_reg, s)) {
        unallocated_encoding(s);
        return;
    }

    imm = uimm6 << LOG2_TAG_GRANULE;
    if (sub_op) {
        imm = -imm;
    }

    tcg_rn = cpu_reg_sp(s, rn);
    tcg_rd = cpu_reg_sp(s, rd);

    if (s->ata) {
        TCGv_i32 offset = tcg_const_i32(imm);
        TCGv_i32 tag_offset = tcg_const_i32(uimm4);

        gen_helper_addsubg(tcg_rd, cpu_env, tcg_rn, offset, tag_offset);
        tcg_temp_free_i32(tag_offset);
        tcg_temp_free_i32(offset);
    } else {
        tcg_gen_addi_i64(tcg_rd, tcg_rn, imm);
        gen_address_with_allocation_tag0(tcg_rd, tcg_rd);
    }
}

/* The input should be a value in the bottom e bits (with higher
 * bits zero); returns that value replicated into every element
 * of size e in a 64 bit integer.
 */
static uint64_t bitfield_replicate(uint64_t mask, unsigned int e)
{
    assert(e != 0);
    while (e < 64) {
        mask |= mask << e;
        e *= 2;
    }
    return mask;
}

/* Return a value with the bottom len bits set (where 0 < len <= 64) */
static inline uint64_t bitmask64(unsigned int length)
{
    assert(length > 0 && length <= 64);
    return ~0ULL >> (64 - length);
}

/* Simplified variant of pseudocode DecodeBitMasks() for the case where we
 * only require the wmask. Returns false if the imms/immr/immn are a reserved
 * value (ie should cause a guest UNDEF exception), and true if they are
 * valid, in which case the decoded bit pattern is written to result.
 */
bool logic_imm_decode_wmask(uint64_t *result, unsigned int immn,
                            unsigned int imms, unsigned int immr)
{
    uint64_t mask;
    unsigned e, levels, s, r;
    int len;

    assert(immn < 2 && imms < 64 && immr < 64);

    /* The bit patterns we create here are 64 bit patterns which
     * are vectors of identical elements of size e = 2, 4, 8, 16, 32 or
     * 64 bits each. Each element contains the same value: a run
     * of between 1 and e-1 non-zero bits, rotated within the
     * element by between 0 and e-1 bits.
     *
     * The element size and run length are encoded into immn (1 bit)
     * and imms (6 bits) as follows:
     * 64 bit elements: immn = 1, imms = <length of run - 1>
     * 32 bit elements: immn = 0, imms = 0 : <length of run - 1>
     * 16 bit elements: immn = 0, imms = 10 : <length of run - 1>
     *  8 bit elements: immn = 0, imms = 110 : <length of run - 1>
     *  4 bit elements: immn = 0, imms = 1110 : <length of run - 1>
     *  2 bit elements: immn = 0, imms = 11110 : <length of run - 1>
     * Notice that immn = 0, imms = 11111x is the only combination
     * not covered by one of the above options; this is reserved.
     * Further, <length of run - 1> all-ones is a reserved pattern.
     *
     * In all cases the rotation is by immr % e (and immr is 6 bits).
     */

    /* First determine the element size */
    len = 31 - clz32((immn << 6) | (~imms & 0x3f));
    if (len < 1) {
        /* This is the immn == 0, imms == 0x11111x case */
        return false;
    }
    e = 1 << len;

    levels = e - 1;
    s = imms & levels;
    r = immr & levels;

    if (s == levels) {
        /* <length of run - 1> mustn't be all-ones. */
        return false;
    }

    /* Create the value of one element: s+1 set bits rotated
     * by r within the element (which is e bits wide)...
     */
    mask = bitmask64(s + 1);
    if (r) {
        mask = (mask >> r) | (mask << (e - r));
        mask &= bitmask64(e);
    }
    /* ...then replicate the element over the whole 64 bit value */
    mask = bitfield_replicate(mask, e);
    *result = mask;
    return true;
}

/* Logical (immediate)
 *   31  30 29 28         23 22  21  16 15  10 9    5 4    0
 * +----+-----+-------------+---+------+------+------+------+
 * | sf | opc | 1 0 0 1 0 0 | N | immr | imms |  Rn  |  Rd  |
 * +----+-----+-------------+---+------+------+------+------+
 */
static void disas_logic_imm(DisasContext *s, uint32_t insn)
{
    unsigned int sf, opc, is_n, immr, imms, rn, rd;
    TCGv_i64 tcg_rd, tcg_rn;
    uint64_t wmask;
    bool is_and = false;

    sf = extract32(insn, 31, 1);
    opc = extract32(insn, 29, 2);
    is_n = extract32(insn, 22, 1);
    immr = extract32(insn, 16, 6);
    imms = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    if (!sf && is_n) {
        unallocated_encoding(s);
        return;
    }

    if (opc == 0x3) { /* ANDS */
        tcg_rd = cpu_reg(s, rd);
    } else {
        tcg_rd = cpu_reg_sp(s, rd);
    }
    tcg_rn = cpu_reg(s, rn);

    if (!logic_imm_decode_wmask(&wmask, is_n, imms, immr)) {
        /* some immediate field values are reserved */
        unallocated_encoding(s);
        return;
    }

    if (!sf) {
        wmask &= 0xffffffff;
    }

    switch (opc) {
    case 0x3: /* ANDS */
    case 0x0: /* AND */
        tcg_gen_andi_i64(tcg_rd, tcg_rn, wmask);
        is_and = true;
        break;
    case 0x1: /* ORR */
        tcg_gen_ori_i64(tcg_rd, tcg_rn, wmask);
        break;
    case 0x2: /* EOR */
        tcg_gen_xori_i64(tcg_rd, tcg_rn, wmask);
        break;
    default:
        assert(FALSE); /* must handle all above */
        break;
    }

    if (!sf && !is_and) {
        /* zero extend final result; we know we can skip this for AND
         * since the immediate had the high 32 bits clear.
         */
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }

    if (opc == 3) { /* ANDS */
        gen_logic_CC(sf, tcg_rd);
    }
}

/*
 * Move wide (immediate)
 *
 *  31 30 29 28         23 22 21 20             5 4    0
 * +--+-----+-------------+-----+----------------+------+
 * |sf| opc | 1 0 0 1 0 1 |  hw |  imm16         |  Rd  |
 * +--+-----+-------------+-----+----------------+------+
 *
 * sf: 0 -> 32 bit, 1 -> 64 bit
 * opc: 00 -> N, 10 -> Z, 11 -> K
 * hw: shift/16 (0,16, and sf only 32, 48)
 */
static void disas_movw_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    uint64_t imm = extract32(insn, 5, 16);
    int sf = extract32(insn, 31, 1);
    int opc = extract32(insn, 29, 2);
    int pos = extract32(insn, 21, 2) << 4;
    TCGv_i64 tcg_rd = cpu_reg(s, rd);
    TCGv_i64 tcg_imm;

    if (!sf && (pos >= 32)) {
        unallocated_encoding(s);
        return;
    }

    switch (opc) {
    case 0: /* MOVN */
    case 2: /* MOVZ */
        imm <<= pos;
        if (opc == 0) {
            imm = ~imm;
        }
        if (!sf) {
            imm &= 0xffffffffu;
        }
        tcg_gen_movi_i64(tcg_rd, imm);
        break;
    case 3: /* MOVK */
        tcg_imm = tcg_const_i64(imm);
        tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_imm, pos, 16);
        tcg_temp_free_i64(tcg_imm);
        if (!sf) {
            tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
        }
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* Bitfield
 *   31  30 29 28         23 22  21  16 15  10 9    5 4    0
 * +----+-----+-------------+---+------+------+------+------+
 * | sf | opc | 1 0 0 1 1 0 | N | immr | imms |  Rn  |  Rd  |
 * +----+-----+-------------+---+------+------+------+------+
 */
static void disas_bitfield(DisasContext *s, uint32_t insn)
{
    unsigned int sf, n, opc, ri, si, rn, rd, bitsize, pos, len;
    TCGv_i64 tcg_rd, tcg_tmp;

    sf = extract32(insn, 31, 1);
    opc = extract32(insn, 29, 2);
    n = extract32(insn, 22, 1);
    ri = extract32(insn, 16, 6);
    si = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);
    bitsize = sf ? 64 : 32;

    if (sf != n || ri >= bitsize || si >= bitsize || opc > 2) {
        unallocated_encoding(s);
        return;
    }

    tcg_rd = cpu_reg(s, rd);

    /* Suppress the zero-extend for !sf.  Since RI and SI are constrained
       to be smaller than bitsize, we'll never reference data outside the
       low 32-bits anyway.  */
    tcg_tmp = read_cpu_reg(s, rn, 1);

    /* Recognize simple(r) extractions.  */
    if (si >= ri) {
        /* Wd<s-r:0> = Wn<s:r> */
        len = (si - ri) + 1;
        if (opc == 0) { /* SBFM: ASR, SBFX, SXTB, SXTH, SXTW */
            tcg_gen_sextract_i64(tcg_rd, tcg_tmp, ri, len);
            goto done;
        } else if (opc == 2) { /* UBFM: UBFX, LSR, UXTB, UXTH */
            tcg_gen_extract_i64(tcg_rd, tcg_tmp, ri, len);
            return;
        }
        /* opc == 1, BFXIL fall through to deposit */
        tcg_gen_shri_i64(tcg_tmp, tcg_tmp, ri);
        pos = 0;
    } else {
        /* Handle the ri > si case with a deposit
         * Wd<32+s-r,32-r> = Wn<s:0>
         */
        len = si + 1;
        pos = (bitsize - ri) & (bitsize - 1);
    }

    if (opc == 0 && len < ri) {
        /* SBFM: sign extend the destination field from len to fill
           the balance of the word.  Let the deposit below insert all
           of those sign bits.  */
        tcg_gen_sextract_i64(tcg_tmp, tcg_tmp, 0, len);
        len = ri;
    }

    if (opc == 1) { /* BFM, BFXIL */
        tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_tmp, pos, len);
    } else {
        /* SBFM or UBFM: We start with zero, and we haven't modified
           any bits outside bitsize, therefore the zero-extension
           below is unneeded.  */
        tcg_gen_deposit_z_i64(tcg_rd, tcg_tmp, pos, len);
        return;
    }

 done:
    if (!sf) { /* zero extend final result */
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
}

/* Extract
 *   31  30  29 28         23 22   21  20  16 15    10 9    5 4    0
 * +----+------+-------------+---+----+------+--------+------+------+
 * | sf | op21 | 1 0 0 1 1 1 | N | o0 |  Rm  |  imms  |  Rn  |  Rd  |
 * +----+------+-------------+---+----+------+--------+------+------+
 */
static void disas_extract(DisasContext *s, uint32_t insn)
{
    unsigned int sf, n, rm, imm, rn, rd, bitsize, op21, op0;

    sf = extract32(insn, 31, 1);
    n = extract32(insn, 22, 1);
    rm = extract32(insn, 16, 5);
    imm = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);
    op21 = extract32(insn, 29, 2);
    op0 = extract32(insn, 21, 1);
    bitsize = sf ? 64 : 32;

    if (sf != n || op21 || op0 || imm >= bitsize) {
        unallocated_encoding(s);
    } else {
        TCGv_i64 tcg_rd, tcg_rm, tcg_rn;

        tcg_rd = cpu_reg(s, rd);

        if (unlikely(imm == 0)) {
            /* tcg shl_i32/shl_i64 is undefined for 32/64 bit shifts,
             * so an extract from bit 0 is a special case.
             */
            if (sf) {
                tcg_gen_mov_i64(tcg_rd, cpu_reg(s, rm));
            } else {
                tcg_gen_ext32u_i64(tcg_rd, cpu_reg(s, rm));
            }
        } else {
            tcg_rm = cpu_reg(s, rm);
            tcg_rn = cpu_reg(s, rn);

            if (sf) {
                /* Specialization to ROR happens in EXTRACT2.  */
                tcg_gen_extract2_i64(tcg_rd, tcg_rm, tcg_rn, imm);
            } else {
                TCGv_i32 t0 = tcg_temp_new_i32();

                tcg_gen_extrl_i64_i32(t0, tcg_rm);
                if (rm == rn) {
                    tcg_gen_rotri_i32(t0, t0, imm);
                } else {
                    TCGv_i32 t1 = tcg_temp_new_i32();
                    tcg_gen_extrl_i64_i32(t1, tcg_rn);
                    tcg_gen_extract2_i32(t0, t0, t1, imm);
                    tcg_temp_free_i32(t1);
                }
                tcg_gen_extu_i32_i64(tcg_rd, t0);
                tcg_temp_free_i32(t0);
            }
        }
    }
}

/* Data processing - immediate */
static void disas_data_proc_imm(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 23, 6)) {
    case 0x20: case 0x21: /* PC-rel. addressing */
        disas_pc_rel_adr(s, insn);
        break;
    case 0x22: /* Add/subtract (immediate) */
        disas_add_sub_imm(s, insn);
        break;
    case 0x23: /* Add/subtract (immediate, with tags) */
        disas_add_sub_imm_with_tags(s, insn);
        break;
    case 0x24: /* Logical (immediate) */
        disas_logic_imm(s, insn);
        break;
    case 0x25: /* Move wide (immediate) */
        disas_movw_imm(s, insn);
        break;
    case 0x26: /* Bitfield */
        disas_bitfield(s, insn);
        break;
    case 0x27: /* Extract */
        disas_extract(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* Shift a TCGv src by TCGv shift_amount, put result in dst.
 * Note that it is the caller's responsibility to ensure that the
 * shift amount is in range (ie 0..31 or 0..63) and provide the ARM
 * mandated semantics for out of range shifts.
 */
static void shift_reg(TCGv_i64 dst, TCGv_i64 src, int sf,
                      enum a64_shift_type shift_type, TCGv_i64 shift_amount)
{
    switch (shift_type) {
    case A64_SHIFT_TYPE_LSL:
        tcg_gen_shl_i64(dst, src, shift_amount);
        break;
    case A64_SHIFT_TYPE_LSR:
        tcg_gen_shr_i64(dst, src, shift_amount);
        break;
    case A64_SHIFT_TYPE_ASR:
        if (!sf) {
            tcg_gen_ext32s_i64(dst, src);
        }
        tcg_gen_sar_i64(dst, sf ? src : dst, shift_amount);
        break;
    case A64_SHIFT_TYPE_ROR:
        if (sf) {
            tcg_gen_rotr_i64(dst, src, shift_amount);
        } else {
            TCGv_i32 t0, t1;
            t0 = tcg_temp_new_i32();
            t1 = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(t0, src);
            tcg_gen_extrl_i64_i32(t1, shift_amount);
            tcg_gen_rotr_i32(t0, t0, t1);
            tcg_gen_extu_i32_i64(dst, t0);
            tcg_temp_free_i32(t0);
            tcg_temp_free_i32(t1);
        }
        break;
    default:
        assert(FALSE); /* all shift types should be handled */
        break;
    }

    if (!sf) { /* zero extend final result */
        tcg_gen_ext32u_i64(dst, dst);
    }
}

/* Shift a TCGv src by immediate, put result in dst.
 * The shift amount must be in range (this should always be true as the
 * relevant instructions will UNDEF on bad shift immediates).
 */
static void shift_reg_imm(TCGv_i64 dst, TCGv_i64 src, int sf,
                          enum a64_shift_type shift_type, unsigned int shift_i)
{
    assert(shift_i < (sf ? 64 : 32));

    if (shift_i == 0) {
        tcg_gen_mov_i64(dst, src);
    } else {
        TCGv_i64 shift_const;

        shift_const = tcg_const_i64(shift_i);
        shift_reg(dst, src, sf, shift_type, shift_const);
        tcg_temp_free_i64(shift_const);
    }
}

/* Logical (shifted register)
 *   31  30 29 28       24 23   22 21  20  16 15    10 9    5 4    0
 * +----+-----+-----------+-------+---+------+--------+------+------+
 * | sf | opc | 0 1 0 1 0 | shift | N |  Rm  |  imm6  |  Rn  |  Rd  |
 * +----+-----+-----------+-------+---+------+--------+------+------+
 */
static void disas_logic_reg(DisasContext *s, uint32_t insn)
{
    TCGv_i64 tcg_rd, tcg_rn, tcg_rm;
    unsigned int sf, opc, shift_type, invert, rm, shift_amount, rn, rd;

    sf = extract32(insn, 31, 1);
    opc = extract32(insn, 29, 2);
    shift_type = extract32(insn, 22, 2);
    invert = extract32(insn, 21, 1);
    rm = extract32(insn, 16, 5);
    shift_amount = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    if (!sf && (shift_amount & (1 << 5))) {
        unallocated_encoding(s);
        return;
    }

    tcg_rd = cpu_reg(s, rd);

    if (opc == 1 && shift_amount == 0 && shift_type == 0 && rn == 31) {
        /* Unshifted ORR and ORN with WZR/XZR is the standard encoding for
         * register-register MOV and MVN, so it is worth special casing.
         */
        tcg_rm = cpu_reg(s, rm);
        if (invert) {
            tcg_gen_not_i64(tcg_rd, tcg_rm);
            if (!sf) {
                tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
            }
        } else {
            if (sf) {
                tcg_gen_mov_i64(tcg_rd, tcg_rm);
            } else {
                tcg_gen_ext32u_i64(tcg_rd, tcg_rm);
            }
        }
        return;
    }

    tcg_rm = read_cpu_reg(s, rm, sf);

    if (shift_amount) {
        shift_reg_imm(tcg_rm, tcg_rm, sf, shift_type, shift_amount);
    }

    tcg_rn = cpu_reg(s, rn);

    switch (opc | (invert << 2)) {
    case 0: /* AND */
    case 3: /* ANDS */
        tcg_gen_and_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    case 1: /* ORR */
        tcg_gen_or_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    case 2: /* EOR */
        tcg_gen_xor_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    case 4: /* BIC */
    case 7: /* BICS */
        tcg_gen_andc_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    case 5: /* ORN */
        tcg_gen_orc_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    case 6: /* EON */
        tcg_gen_eqv_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    default:
        assert(FALSE);
        break;
    }

    if (!sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }

    if (opc == 3) {
        gen_logic_CC(sf, tcg_rd);
    }
}

/*
 * Add/subtract (extended register)
 *
 *  31|30|29|28       24|23 22|21|20   16|15  13|12  10|9  5|4  0|
 * +--+--+--+-----------+-----+--+-------+------+------+----+----+
 * |sf|op| S| 0 1 0 1 1 | opt | 1|  Rm   |option| imm3 | Rn | Rd |
 * +--+--+--+-----------+-----+--+-------+------+------+----+----+
 *
 *  sf: 0 -> 32bit, 1 -> 64bit
 *  op: 0 -> add  , 1 -> sub
 *   S: 1 -> set flags
 * opt: 00
 * option: extension type (see DecodeRegExtend)
 * imm3: optional shift to Rm
 *
 * Rd = Rn + LSL(extend(Rm), amount)
 */
static void disas_add_sub_ext_reg(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int imm3 = extract32(insn, 10, 3);
    int option = extract32(insn, 13, 3);
    int rm = extract32(insn, 16, 5);
    int opt = extract32(insn, 22, 2);
    bool setflags = extract32(insn, 29, 1);
    bool sub_op = extract32(insn, 30, 1);
    bool sf = extract32(insn, 31, 1);

    TCGv_i64 tcg_rm, tcg_rn; /* temps */
    TCGv_i64 tcg_rd;
    TCGv_i64 tcg_result;

    if (imm3 > 4 || opt != 0) {
        unallocated_encoding(s);
        return;
    }

    /* non-flag setting ops may use SP */
    if (!setflags) {
        tcg_rd = cpu_reg_sp(s, rd);
    } else {
        tcg_rd = cpu_reg(s, rd);
    }
    tcg_rn = read_cpu_reg_sp(s, rn, sf);

    tcg_rm = read_cpu_reg(s, rm, sf);
    ext_and_shift_reg(tcg_rm, tcg_rm, option, imm3);
    
    if (rd == 31 && sub_op) // cmp xX, xY
      afl_gen_compcov(s->pc_curr, tcg_rn, tcg_rm, sf ? MO_64 : MO_32, 0);

    tcg_result = tcg_temp_new_i64();

    if (!setflags) {
        if (sub_op) {
            tcg_gen_sub_i64(tcg_result, tcg_rn, tcg_rm);
        } else {
            tcg_gen_add_i64(tcg_result, tcg_rn, tcg_rm);
        }
    } else {
        if (sub_op) {
            gen_sub_CC(sf, tcg_result, tcg_rn, tcg_rm);
        } else {
            gen_add_CC(sf, tcg_result, tcg_rn, tcg_rm);
        }
    }

    if (sf) {
        tcg_gen_mov_i64(tcg_rd, tcg_result);
    } else {
        tcg_gen_ext32u_i64(tcg_rd, tcg_result);
    }

    tcg_temp_free_i64(tcg_result);
}

/*
 * Add/subtract (shifted register)
 *
 *  31 30 29 28       24 23 22 21 20   16 15     10 9    5 4    0
 * +--+--+--+-----------+-----+--+-------+---------+------+------+
 * |sf|op| S| 0 1 0 1 1 |shift| 0|  Rm   |  imm6   |  Rn  |  Rd  |
 * +--+--+--+-----------+-----+--+-------+---------+------+------+
 *
 *    sf: 0 -> 32bit, 1 -> 64bit
 *    op: 0 -> add  , 1 -> sub
 *     S: 1 -> set flags
 * shift: 00 -> LSL, 01 -> LSR, 10 -> ASR, 11 -> RESERVED
 *  imm6: Shift amount to apply to Rm before the add/sub
 */
static void disas_add_sub_reg(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int imm6 = extract32(insn, 10, 6);
    int rm = extract32(insn, 16, 5);
    int shift_type = extract32(insn, 22, 2);
    bool setflags = extract32(insn, 29, 1);
    bool sub_op = extract32(insn, 30, 1);
    bool sf = extract32(insn, 31, 1);

    TCGv_i64 tcg_rd = cpu_reg(s, rd);
    TCGv_i64 tcg_rn, tcg_rm;
    TCGv_i64 tcg_result;

    if ((shift_type == 3) || (!sf && (imm6 > 31))) {
        unallocated_encoding(s);
        return;
    }

    tcg_rn = read_cpu_reg(s, rn, sf);
    tcg_rm = read_cpu_reg(s, rm, sf);

    shift_reg_imm(tcg_rm, tcg_rm, sf, shift_type, imm6);
    
    if (rd == 31 && sub_op) // cmp xX, xY
      afl_gen_compcov(s->pc_curr, tcg_rn, tcg_rm, sf ? MO_64 : MO_32, 0);

    tcg_result = tcg_temp_new_i64();

    if (!setflags) {
        if (sub_op) {
            tcg_gen_sub_i64(tcg_result, tcg_rn, tcg_rm);
        } else {
            tcg_gen_add_i64(tcg_result, tcg_rn, tcg_rm);
        }
    } else {
        if (sub_op) {
            gen_sub_CC(sf, tcg_result, tcg_rn, tcg_rm);
        } else {
            gen_add_CC(sf, tcg_result, tcg_rn, tcg_rm);
        }
    }

    if (sf) {
        tcg_gen_mov_i64(tcg_rd, tcg_result);
    } else {
        tcg_gen_ext32u_i64(tcg_rd, tcg_result);
    }

    tcg_temp_free_i64(tcg_result);
}

/* Data-processing (3 source)
 *
 *    31 30  29 28       24 23 21  20  16  15  14  10 9    5 4    0
 *  +--+------+-----------+------+------+----+------+------+------+
 *  |sf| op54 | 1 1 0 1 1 | op31 |  Rm  | o0 |  Ra  |  Rn  |  Rd  |
 *  +--+------+-----------+------+------+----+------+------+------+
 */
static void disas_data_proc_3src(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int ra = extract32(insn, 10, 5);
    int rm = extract32(insn, 16, 5);
    int op_id = (extract32(insn, 29, 3) << 4) |
        (extract32(insn, 21, 3) << 1) |
        extract32(insn, 15, 1);
    bool sf = extract32(insn, 31, 1);
    bool is_sub = extract32(op_id, 0, 1);
    bool is_high = extract32(op_id, 2, 1);
    bool is_signed = false;
    TCGv_i64 tcg_op1;
    TCGv_i64 tcg_op2;
    TCGv_i64 tcg_tmp;

    /* Note that op_id is sf:op54:op31:o0 so it includes the 32/64 size flag */
    switch (op_id) {
    case 0x42: /* SMADDL */
    case 0x43: /* SMSUBL */
    case 0x44: /* SMULH */
        is_signed = true;
        break;
    case 0x0: /* MADD (32bit) */
    case 0x1: /* MSUB (32bit) */
    case 0x40: /* MADD (64bit) */
    case 0x41: /* MSUB (64bit) */
    case 0x4a: /* UMADDL */
    case 0x4b: /* UMSUBL */
    case 0x4c: /* UMULH */
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (is_high) {
        TCGv_i64 low_bits = tcg_temp_new_i64(); /* low bits discarded */
        TCGv_i64 tcg_rd = cpu_reg(s, rd);
        TCGv_i64 tcg_rn = cpu_reg(s, rn);
        TCGv_i64 tcg_rm = cpu_reg(s, rm);

        if (is_signed) {
            tcg_gen_muls2_i64(low_bits, tcg_rd, tcg_rn, tcg_rm);
        } else {
            tcg_gen_mulu2_i64(low_bits, tcg_rd, tcg_rn, tcg_rm);
        }

        tcg_temp_free_i64(low_bits);
        return;
    }

    tcg_op1 = tcg_temp_new_i64();
    tcg_op2 = tcg_temp_new_i64();
    tcg_tmp = tcg_temp_new_i64();

    if (op_id < 0x42) {
        tcg_gen_mov_i64(tcg_op1, cpu_reg(s, rn));
        tcg_gen_mov_i64(tcg_op2, cpu_reg(s, rm));
    } else {
        if (is_signed) {
            tcg_gen_ext32s_i64(tcg_op1, cpu_reg(s, rn));
            tcg_gen_ext32s_i64(tcg_op2, cpu_reg(s, rm));
        } else {
            tcg_gen_ext32u_i64(tcg_op1, cpu_reg(s, rn));
            tcg_gen_ext32u_i64(tcg_op2, cpu_reg(s, rm));
        }
    }

    if (ra == 31 && !is_sub) {
        /* Special-case MADD with rA == XZR; it is the standard MUL alias */
        tcg_gen_mul_i64(cpu_reg(s, rd), tcg_op1, tcg_op2);
    } else {
        tcg_gen_mul_i64(tcg_tmp, tcg_op1, tcg_op2);
        if (is_sub) {
            tcg_gen_sub_i64(cpu_reg(s, rd), cpu_reg(s, ra), tcg_tmp);
        } else {
            tcg_gen_add_i64(cpu_reg(s, rd), cpu_reg(s, ra), tcg_tmp);
        }
    }

    if (!sf) {
        tcg_gen_ext32u_i64(cpu_reg(s, rd), cpu_reg(s, rd));
    }

    tcg_temp_free_i64(tcg_op1);
    tcg_temp_free_i64(tcg_op2);
    tcg_temp_free_i64(tcg_tmp);
}

/* Add/subtract (with carry)
 *  31 30 29 28 27 26 25 24 23 22 21  20  16  15       10  9    5 4   0
 * +--+--+--+------------------------+------+-------------+------+-----+
 * |sf|op| S| 1  1  0  1  0  0  0  0 |  rm  | 0 0 0 0 0 0 |  Rn  |  Rd |
 * +--+--+--+------------------------+------+-------------+------+-----+
 */

static void disas_adc_sbc(DisasContext *s, uint32_t insn)
{
    unsigned int sf, op, setflags, rm, rn, rd;
    TCGv_i64 tcg_y, tcg_rn, tcg_rd;

    sf = extract32(insn, 31, 1);
    op = extract32(insn, 30, 1);
    setflags = extract32(insn, 29, 1);
    rm = extract32(insn, 16, 5);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    tcg_rd = cpu_reg(s, rd);
    tcg_rn = cpu_reg(s, rn);

    if (op) {
        tcg_y = new_tmp_a64(s);
        tcg_gen_not_i64(tcg_y, cpu_reg(s, rm));
    } else {
        tcg_y = cpu_reg(s, rm);
    }

    if (setflags) {
        gen_adc_CC(sf, tcg_rd, tcg_rn, tcg_y);
    } else {
        gen_adc(sf, tcg_rd, tcg_rn, tcg_y);
    }
}

/*
 * Rotate right into flags
 *  31 30 29                21       15          10      5  4      0
 * +--+--+--+-----------------+--------+-----------+------+--+------+
 * |sf|op| S| 1 1 0 1 0 0 0 0 |  imm6  | 0 0 0 0 1 |  Rn  |o2| mask |
 * +--+--+--+-----------------+--------+-----------+------+--+------+
 */
static void disas_rotate_right_into_flags(DisasContext *s, uint32_t insn)
{
    int mask = extract32(insn, 0, 4);
    int o2 = extract32(insn, 4, 1);
    int rn = extract32(insn, 5, 5);
    int imm6 = extract32(insn, 15, 6);
    int sf_op_s = extract32(insn, 29, 3);
    TCGv_i64 tcg_rn;
    TCGv_i32 nzcv;

    if (sf_op_s != 5 || o2 != 0 || !dc_isar_feature(aa64_condm_4, s)) {
        unallocated_encoding(s);
        return;
    }

    tcg_rn = read_cpu_reg(s, rn, 1);
    tcg_gen_rotri_i64(tcg_rn, tcg_rn, imm6);

    nzcv = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(nzcv, tcg_rn);

    if (mask & 8) { /* N */
        tcg_gen_shli_i32(cpu_NF, nzcv, 31 - 3);
    }
    if (mask & 4) { /* Z */
        tcg_gen_not_i32(cpu_ZF, nzcv);
        tcg_gen_andi_i32(cpu_ZF, cpu_ZF, 4);
    }
    if (mask & 2) { /* C */
        tcg_gen_extract_i32(cpu_CF, nzcv, 1, 1);
    }
    if (mask & 1) { /* V */
        tcg_gen_shli_i32(cpu_VF, nzcv, 31 - 0);
    }

    tcg_temp_free_i32(nzcv);
}

/*
 * Evaluate into flags
 *  31 30 29                21        15   14        10      5  4      0
 * +--+--+--+-----------------+---------+----+---------+------+--+------+
 * |sf|op| S| 1 1 0 1 0 0 0 0 | opcode2 | sz | 0 0 1 0 |  Rn  |o3| mask |
 * +--+--+--+-----------------+---------+----+---------+------+--+------+
 */
static void disas_evaluate_into_flags(DisasContext *s, uint32_t insn)
{
    int o3_mask = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int o2 = extract32(insn, 15, 6);
    int sz = extract32(insn, 14, 1);
    int sf_op_s = extract32(insn, 29, 3);
    TCGv_i32 tmp;
    int shift;

    if (sf_op_s != 1 || o2 != 0 || o3_mask != 0xd ||
        !dc_isar_feature(aa64_condm_4, s)) {
        unallocated_encoding(s);
        return;
    }
    shift = sz ? 16 : 24;  /* SETF16 or SETF8 */

    tmp = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(tmp, cpu_reg(s, rn));
    tcg_gen_shli_i32(cpu_NF, tmp, shift);
    tcg_gen_shli_i32(cpu_VF, tmp, shift - 1);
    tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    tcg_gen_xor_i32(cpu_VF, cpu_VF, cpu_NF);
    tcg_temp_free_i32(tmp);
}

/* Conditional compare (immediate / register)
 *  31 30 29 28 27 26 25 24 23 22 21  20    16 15  12  11  10  9   5  4 3   0
 * +--+--+--+------------------------+--------+------+----+--+------+--+-----+
 * |sf|op| S| 1  1  0  1  0  0  1  0 |imm5/rm | cond |i/r |o2|  Rn  |o3|nzcv |
 * +--+--+--+------------------------+--------+------+----+--+------+--+-----+
 *        [1]                             y                [0]       [0]
 */
static void disas_cc(DisasContext *s, uint32_t insn)
{
    unsigned int sf, op, y, cond, rn, nzcv, is_imm;
    TCGv_i32 tcg_t0, tcg_t1, tcg_t2;
    TCGv_i64 tcg_tmp, tcg_y, tcg_rn;
    DisasCompare c;

    if (!extract32(insn, 29, 1)) {
        unallocated_encoding(s);
        return;
    }
    if (insn & (1 << 10 | 1 << 4)) {
        unallocated_encoding(s);
        return;
    }
    sf = extract32(insn, 31, 1);
    op = extract32(insn, 30, 1);
    is_imm = extract32(insn, 11, 1);
    y = extract32(insn, 16, 5); /* y = rm (reg) or imm5 (imm) */
    cond = extract32(insn, 12, 4);
    rn = extract32(insn, 5, 5);
    nzcv = extract32(insn, 0, 4);

    /* Set T0 = !COND.  */
    tcg_t0 = tcg_temp_new_i32();
    arm_test_cc(&c, cond);
    tcg_gen_setcondi_i32(tcg_invert_cond(c.cond), tcg_t0, c.value, 0);
    arm_free_cc(&c);

    /* Load the arguments for the new comparison.  */
    if (is_imm) {
        tcg_y = new_tmp_a64(s);
        tcg_gen_movi_i64(tcg_y, y);
    } else {
        tcg_y = cpu_reg(s, y);
    }
    tcg_rn = cpu_reg(s, rn);

    afl_gen_compcov(s->pc_curr, tcg_rn, tcg_y, sf ? MO_64 : MO_32, is_imm);

    /* Set the flags for the new comparison.  */
    tcg_tmp = tcg_temp_new_i64();
    if (op) {
        gen_sub_CC(sf, tcg_tmp, tcg_rn, tcg_y);
    } else {
        gen_add_CC(sf, tcg_tmp, tcg_rn, tcg_y);
    }
    tcg_temp_free_i64(tcg_tmp);

    /* If COND was false, force the flags to #nzcv.  Compute two masks
     * to help with this: T1 = (COND ? 0 : -1), T2 = (COND ? -1 : 0).
     * For tcg hosts that support ANDC, we can make do with just T1.
     * In either case, allow the tcg optimizer to delete any unused mask.
     */
    tcg_t1 = tcg_temp_new_i32();
    tcg_t2 = tcg_temp_new_i32();
    tcg_gen_neg_i32(tcg_t1, tcg_t0);
    tcg_gen_subi_i32(tcg_t2, tcg_t0, 1);

    if (nzcv & 8) { /* N */
        tcg_gen_or_i32(cpu_NF, cpu_NF, tcg_t1);
    } else {
        if (TCG_TARGET_HAS_andc_i32) {
            tcg_gen_andc_i32(cpu_NF, cpu_NF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_NF, cpu_NF, tcg_t2);
        }
    }
    if (nzcv & 4) { /* Z */
        if (TCG_TARGET_HAS_andc_i32) {
            tcg_gen_andc_i32(cpu_ZF, cpu_ZF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_ZF, cpu_ZF, tcg_t2);
        }
    } else {
        tcg_gen_or_i32(cpu_ZF, cpu_ZF, tcg_t0);
    }
    if (nzcv & 2) { /* C */
        tcg_gen_or_i32(cpu_CF, cpu_CF, tcg_t0);
    } else {
        if (TCG_TARGET_HAS_andc_i32) {
            tcg_gen_andc_i32(cpu_CF, cpu_CF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_CF, cpu_CF, tcg_t2);
        }
    }
    if (nzcv & 1) { /* V */
        tcg_gen_or_i32(cpu_VF, cpu_VF, tcg_t1);
    } else {
        if (TCG_TARGET_HAS_andc_i32) {
            tcg_gen_andc_i32(cpu_VF, cpu_VF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_VF, cpu_VF, tcg_t2);
        }
    }
    tcg_temp_free_i32(tcg_t0);
    tcg_temp_free_i32(tcg_t1);
    tcg_temp_free_i32(tcg_t2);
}

/* Conditional select
 *   31   30  29  28             21 20  16 15  12 11 10 9    5 4    0
 * +----+----+---+-----------------+------+------+-----+------+------+
 * | sf | op | S | 1 1 0 1 0 1 0 0 |  Rm  | cond | op2 |  Rn  |  Rd  |
 * +----+----+---+-----------------+------+------+-----+------+------+
 */
static void disas_cond_select(DisasContext *s, uint32_t insn)
{
    unsigned int sf, else_inv, rm, cond, else_inc, rn, rd;
    TCGv_i64 tcg_rd, zero;
    DisasCompare64 c;

    if (extract32(insn, 29, 1) || extract32(insn, 11, 1)) {
        /* S == 1 or op2<1> == 1 */
        unallocated_encoding(s);
        return;
    }
    sf = extract32(insn, 31, 1);
    else_inv = extract32(insn, 30, 1);
    rm = extract32(insn, 16, 5);
    cond = extract32(insn, 12, 4);
    else_inc = extract32(insn, 10, 1);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    tcg_rd = cpu_reg(s, rd);

    a64_test_cc(&c, cond);
    zero = tcg_const_i64(0);

    if (rn == 31 && rm == 31 && (else_inc ^ else_inv)) {
        /* CSET & CSETM.  */
        tcg_gen_setcond_i64(tcg_invert_cond(c.cond), tcg_rd, c.value, zero);
        if (else_inv) {
            tcg_gen_neg_i64(tcg_rd, tcg_rd);
        }
    } else {
        TCGv_i64 t_true = cpu_reg(s, rn);
        TCGv_i64 t_false = read_cpu_reg(s, rm, 1);
        if (else_inv && else_inc) {
            tcg_gen_neg_i64(t_false, t_false);
        } else if (else_inv) {
            tcg_gen_not_i64(t_false, t_false);
        } else if (else_inc) {
            tcg_gen_addi_i64(t_false, t_false, 1);
        }
        tcg_gen_movcond_i64(c.cond, tcg_rd, c.value, zero, t_true, t_false);
    }

    tcg_temp_free_i64(zero);
    a64_free_cc(&c);

    if (!sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
}

static void handle_clz(DisasContext *s, unsigned int sf,
                       unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd, tcg_rn;
    tcg_rd = cpu_reg(s, rd);
    tcg_rn = cpu_reg(s, rn);

    if (sf) {
        tcg_gen_clzi_i64(tcg_rd, tcg_rn, 64);
    } else {
        TCGv_i32 tcg_tmp32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(tcg_tmp32, tcg_rn);
        tcg_gen_clzi_i32(tcg_tmp32, tcg_tmp32, 32);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_tmp32);
        tcg_temp_free_i32(tcg_tmp32);
    }
}

static void handle_cls(DisasContext *s, unsigned int sf,
                       unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd, tcg_rn;
    tcg_rd = cpu_reg(s, rd);
    tcg_rn = cpu_reg(s, rn);

    if (sf) {
        tcg_gen_clrsb_i64(tcg_rd, tcg_rn);
    } else {
        TCGv_i32 tcg_tmp32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(tcg_tmp32, tcg_rn);
        tcg_gen_clrsb_i32(tcg_tmp32, tcg_tmp32);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_tmp32);
        tcg_temp_free_i32(tcg_tmp32);
    }
}

static void handle_rbit(DisasContext *s, unsigned int sf,
                        unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd, tcg_rn;
    tcg_rd = cpu_reg(s, rd);
    tcg_rn = cpu_reg(s, rn);

    if (sf) {
        gen_helper_rbit64(tcg_rd, tcg_rn);
    } else {
        TCGv_i32 tcg_tmp32 = tcg_temp_new_i32();
        tcg_gen_extrl_i64_i32(tcg_tmp32, tcg_rn);
        gen_helper_rbit(tcg_tmp32, tcg_tmp32);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_tmp32);
        tcg_temp_free_i32(tcg_tmp32);
    }
}

/* REV with sf==1, opcode==3 ("REV64") */
static void handle_rev64(DisasContext *s, unsigned int sf,
                         unsigned int rn, unsigned int rd)
{
    if (!sf) {
        unallocated_encoding(s);
        return;
    }
    tcg_gen_bswap64_i64(cpu_reg(s, rd), cpu_reg(s, rn));
}

/* REV with sf==0, opcode==2
 * REV32 (sf==1, opcode==2)
 */
static void handle_rev32(DisasContext *s, unsigned int sf,
                         unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd = cpu_reg(s, rd);

    if (sf) {
        TCGv_i64 tcg_tmp = tcg_temp_new_i64();
        TCGv_i64 tcg_rn = read_cpu_reg(s, rn, sf);

        /* bswap32_i64 requires zero high word */
        tcg_gen_ext32u_i64(tcg_tmp, tcg_rn);
        tcg_gen_bswap32_i64(tcg_rd, tcg_tmp);
        tcg_gen_shri_i64(tcg_tmp, tcg_rn, 32);
        tcg_gen_bswap32_i64(tcg_tmp, tcg_tmp);
        tcg_gen_concat32_i64(tcg_rd, tcg_rd, tcg_tmp);

        tcg_temp_free_i64(tcg_tmp);
    } else {
        tcg_gen_ext32u_i64(tcg_rd, cpu_reg(s, rn));
        tcg_gen_bswap32_i64(tcg_rd, tcg_rd);
    }
}

/* REV16 (opcode==1) */
static void handle_rev16(DisasContext *s, unsigned int sf,
                         unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd = cpu_reg(s, rd);
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();
    TCGv_i64 tcg_rn = read_cpu_reg(s, rn, sf);
    TCGv_i64 mask = tcg_const_i64(sf ? 0x00ff00ff00ff00ffull : 0x00ff00ff);

    tcg_gen_shri_i64(tcg_tmp, tcg_rn, 8);
    tcg_gen_and_i64(tcg_rd, tcg_rn, mask);
    tcg_gen_and_i64(tcg_tmp, tcg_tmp, mask);
    tcg_gen_shli_i64(tcg_rd, tcg_rd, 8);
    tcg_gen_or_i64(tcg_rd, tcg_rd, tcg_tmp);

    tcg_temp_free_i64(mask);
    tcg_temp_free_i64(tcg_tmp);
}

/* Data-processing (1 source)
 *   31  30  29  28             21 20     16 15    10 9    5 4    0
 * +----+---+---+-----------------+---------+--------+------+------+
 * | sf | 1 | S | 1 1 0 1 0 1 1 0 | opcode2 | opcode |  Rn  |  Rd  |
 * +----+---+---+-----------------+---------+--------+------+------+
 */
static void disas_data_proc_1src(DisasContext *s, uint32_t insn)
{
    unsigned int sf, opcode, opcode2, rn, rd;
    TCGv_i64 tcg_rd;

    if (extract32(insn, 29, 1)) {
        unallocated_encoding(s);
        return;
    }

    sf = extract32(insn, 31, 1);
    opcode = extract32(insn, 10, 6);
    opcode2 = extract32(insn, 16, 5);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

#define MAP(SF, O2, O1) ((SF) | (O1 << 1) | (O2 << 7))

    switch (MAP(sf, opcode2, opcode)) {
    case MAP(0, 0x00, 0x00): /* RBIT */
    case MAP(1, 0x00, 0x00):
        handle_rbit(s, sf, rn, rd);
        break;
    case MAP(0, 0x00, 0x01): /* REV16 */
    case MAP(1, 0x00, 0x01):
        handle_rev16(s, sf, rn, rd);
        break;
    case MAP(0, 0x00, 0x02): /* REV/REV32 */
    case MAP(1, 0x00, 0x02):
        handle_rev32(s, sf, rn, rd);
        break;
    case MAP(1, 0x00, 0x03): /* REV64 */
        handle_rev64(s, sf, rn, rd);
        break;
    case MAP(0, 0x00, 0x04): /* CLZ */
    case MAP(1, 0x00, 0x04):
        handle_clz(s, sf, rn, rd);
        break;
    case MAP(0, 0x00, 0x05): /* CLS */
    case MAP(1, 0x00, 0x05):
        handle_cls(s, sf, rn, rd);
        break;
    case MAP(1, 0x01, 0x00): /* PACIA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacia(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x01): /* PACIB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacib(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x02): /* PACDA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacda(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x03): /* PACDB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacdb(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x04): /* AUTIA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autia(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x05): /* AUTIB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autib(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x06): /* AUTDA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autda(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x07): /* AUTDB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autdb(tcg_rd, cpu_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x08): /* PACIZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacia(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x09): /* PACIZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacib(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x0a): /* PACDZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacda(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x0b): /* PACDZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacdb(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x0c): /* AUTIZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autia(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x0d): /* AUTIZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autib(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x0e): /* AUTDZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autda(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x0f): /* AUTDZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autdb(tcg_rd, cpu_env, tcg_rd, new_tmp_a64_zero(s));
        }
        break;
    case MAP(1, 0x01, 0x10): /* XPACI */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_xpaci(tcg_rd, cpu_env, tcg_rd);
        }
        break;
    case MAP(1, 0x01, 0x11): /* XPACD */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_xpacd(tcg_rd, cpu_env, tcg_rd);
        }
        break;
    default:
    do_unallocated:
        unallocated_encoding(s);
        break;
    }

#undef MAP
}

static void handle_div(DisasContext *s, bool is_signed, unsigned int sf,
                       unsigned int rm, unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_n, tcg_m, tcg_rd;
    tcg_rd = cpu_reg(s, rd);

    if (!sf && is_signed) {
        tcg_n = new_tmp_a64(s);
        tcg_m = new_tmp_a64(s);
        tcg_gen_ext32s_i64(tcg_n, cpu_reg(s, rn));
        tcg_gen_ext32s_i64(tcg_m, cpu_reg(s, rm));
    } else {
        tcg_n = read_cpu_reg(s, rn, sf);
        tcg_m = read_cpu_reg(s, rm, sf);
    }

    if (is_signed) {
        gen_helper_sdiv64(tcg_rd, tcg_n, tcg_m);
    } else {
        gen_helper_udiv64(tcg_rd, tcg_n, tcg_m);
    }

    if (!sf) { /* zero extend final result */
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
}

/* LSLV, LSRV, ASRV, RORV */
static void handle_shift_reg(DisasContext *s,
                             enum a64_shift_type shift_type, unsigned int sf,
                             unsigned int rm, unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_shift = tcg_temp_new_i64();
    TCGv_i64 tcg_rd = cpu_reg(s, rd);
    TCGv_i64 tcg_rn = read_cpu_reg(s, rn, sf);

    tcg_gen_andi_i64(tcg_shift, cpu_reg(s, rm), sf ? 63 : 31);
    shift_reg(tcg_rd, tcg_rn, sf, shift_type, tcg_shift);
    tcg_temp_free_i64(tcg_shift);
}

/* CRC32[BHWX], CRC32C[BHWX] */
static void handle_crc32(DisasContext *s,
                         unsigned int sf, unsigned int sz, bool crc32c,
                         unsigned int rm, unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_acc, tcg_val;
    TCGv_i32 tcg_bytes;

    if (!dc_isar_feature(aa64_crc32, s)
        || (sf == 1 && sz != 3)
        || (sf == 0 && sz == 3)) {
        unallocated_encoding(s);
        return;
    }

    if (sz == 3) {
        tcg_val = cpu_reg(s, rm);
    } else {
        uint64_t mask;
        switch (sz) {
        case 0:
            mask = 0xFF;
            break;
        case 1:
            mask = 0xFFFF;
            break;
        case 2:
            mask = 0xFFFFFFFF;
            break;
        default:
            g_assert_not_reached();
        }
        tcg_val = new_tmp_a64(s);
        tcg_gen_andi_i64(tcg_val, cpu_reg(s, rm), mask);
    }

    tcg_acc = cpu_reg(s, rn);
    tcg_bytes = tcg_const_i32(1 << sz);

    if (crc32c) {
        gen_helper_crc32c_64(cpu_reg(s, rd), tcg_acc, tcg_val, tcg_bytes);
    } else {
        gen_helper_crc32_64(cpu_reg(s, rd), tcg_acc, tcg_val, tcg_bytes);
    }

    tcg_temp_free_i32(tcg_bytes);
}

/* Data-processing (2 source)
 *   31   30  29 28             21 20  16 15    10 9    5 4    0
 * +----+---+---+-----------------+------+--------+------+------+
 * | sf | 0 | S | 1 1 0 1 0 1 1 0 |  Rm  | opcode |  Rn  |  Rd  |
 * +----+---+---+-----------------+------+--------+------+------+
 */
static void disas_data_proc_2src(DisasContext *s, uint32_t insn)
{
    unsigned int sf, rm, opcode, rn, rd, setflag;
    sf = extract32(insn, 31, 1);
    setflag = extract32(insn, 29, 1);
    rm = extract32(insn, 16, 5);
    opcode = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    if (setflag && opcode != 0) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0: /* SUBP(S) */
        if (sf == 0 || !dc_isar_feature(aa64_mte_insn_reg, s)) {
            goto do_unallocated;
        } else {
            TCGv_i64 tcg_n, tcg_m, tcg_d;

            tcg_n = read_cpu_reg_sp(s, rn, true);
            tcg_m = read_cpu_reg_sp(s, rm, true);
            tcg_gen_sextract_i64(tcg_n, tcg_n, 0, 56);
            tcg_gen_sextract_i64(tcg_m, tcg_m, 0, 56);
            tcg_d = cpu_reg(s, rd);

            if (setflag) {
                gen_sub_CC(true, tcg_d, tcg_n, tcg_m);
            } else {
                tcg_gen_sub_i64(tcg_d, tcg_n, tcg_m);
            }
        }
        break;
    case 2: /* UDIV */
        handle_div(s, false, sf, rm, rn, rd);
        break;
    case 3: /* SDIV */
        handle_div(s, true, sf, rm, rn, rd);
        break;
    case 4: /* IRG */
        if (sf == 0 || !dc_isar_feature(aa64_mte_insn_reg, s)) {
            goto do_unallocated;
        }
        if (s->ata) {
            gen_helper_irg(cpu_reg_sp(s, rd), cpu_env,
                           cpu_reg_sp(s, rn), cpu_reg(s, rm));
        } else {
            gen_address_with_allocation_tag0(cpu_reg_sp(s, rd),
                                             cpu_reg_sp(s, rn));
        }
        break;
    case 5: /* GMI */
        if (sf == 0 || !dc_isar_feature(aa64_mte_insn_reg, s)) {
            goto do_unallocated;
        } else {
            TCGv_i64 t1 = tcg_const_i64(1);
            TCGv_i64 t2 = tcg_temp_new_i64();

            tcg_gen_extract_i64(t2, cpu_reg_sp(s, rn), 56, 4);
            tcg_gen_shl_i64(t1, t1, t2);
            tcg_gen_or_i64(cpu_reg(s, rd), cpu_reg(s, rm), t1);

            tcg_temp_free_i64(t1);
            tcg_temp_free_i64(t2);
        }
        break;
    case 8: /* LSLV */
        handle_shift_reg(s, A64_SHIFT_TYPE_LSL, sf, rm, rn, rd);
        break;
    case 9: /* LSRV */
        handle_shift_reg(s, A64_SHIFT_TYPE_LSR, sf, rm, rn, rd);
        break;
    case 10: /* ASRV */
        handle_shift_reg(s, A64_SHIFT_TYPE_ASR, sf, rm, rn, rd);
        break;
    case 11: /* RORV */
        handle_shift_reg(s, A64_SHIFT_TYPE_ROR, sf, rm, rn, rd);
        break;
    case 12: /* PACGA */
        if (sf == 0 || !dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        gen_helper_pacga(cpu_reg(s, rd), cpu_env,
                         cpu_reg(s, rn), cpu_reg_sp(s, rm));
        break;
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23: /* CRC32 */
    {
        int sz = extract32(opcode, 0, 2);
        bool crc32c = extract32(opcode, 2, 1);
        handle_crc32(s, sf, sz, crc32c, rm, rn, rd);
        break;
    }
    default:
    do_unallocated:
        unallocated_encoding(s);
        break;
    }
}

/*
 * Data processing - register
 *  31  30 29  28      25    21  20  16      10         0
 * +--+---+--+---+-------+-----+-------+-------+---------+
 * |  |op0|  |op1| 1 0 1 | op2 |       |  op3  |         |
 * +--+---+--+---+-------+-----+-------+-------+---------+
 */
static void disas_data_proc_reg(DisasContext *s, uint32_t insn)
{
    int op0 = extract32(insn, 30, 1);
    int op1 = extract32(insn, 28, 1);
    int op2 = extract32(insn, 21, 4);
    int op3 = extract32(insn, 10, 6);

    if (!op1) {
        if (op2 & 8) {
            if (op2 & 1) {
                /* Add/sub (extended register) */
                disas_add_sub_ext_reg(s, insn);
            } else {
                /* Add/sub (shifted register) */
                disas_add_sub_reg(s, insn);
            }
        } else {
            /* Logical (shifted register) */
            disas_logic_reg(s, insn);
        }
        return;
    }

    switch (op2) {
    case 0x0:
        switch (op3) {
        case 0x00: /* Add/subtract (with carry) */
            disas_adc_sbc(s, insn);
            break;

        case 0x01: /* Rotate right into flags */
        case 0x21:
            disas_rotate_right_into_flags(s, insn);
            break;

        case 0x02: /* Evaluate into flags */
        case 0x12:
        case 0x22:
        case 0x32:
            disas_evaluate_into_flags(s, insn);
            break;

        default:
            goto do_unallocated;
        }
        break;

    case 0x2: /* Conditional compare */
        disas_cc(s, insn); /* both imm and reg forms */
        break;

    case 0x4: /* Conditional select */
        disas_cond_select(s, insn);
        break;

    case 0x6: /* Data-processing */
        if (op0) {    /* (1 source) */
            disas_data_proc_1src(s, insn);
        } else {      /* (2 source) */
            disas_data_proc_2src(s, insn);
        }
        break;
    case 0x8 ... 0xf: /* (3 source) */
        disas_data_proc_3src(s, insn);
        break;

    default:
    do_unallocated:
        unallocated_encoding(s);
        break;
    }
}

static void handle_fp_compare(DisasContext *s, int size,
                              unsigned int rn, unsigned int rm,
                              bool cmp_with_zero, bool signal_all_nans)
{
    TCGv_i64 tcg_flags = tcg_temp_new_i64();
    TCGv_ptr fpst = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);

    if (size == MO_64) {
        TCGv_i64 tcg_vn, tcg_vm;

        tcg_vn = read_fp_dreg(s, rn);
        if (cmp_with_zero) {
            tcg_vm = tcg_const_i64(0);
        } else {
            tcg_vm = read_fp_dreg(s, rm);
        }
        if (signal_all_nans) {
            gen_helper_vfp_cmped_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
        } else {
            gen_helper_vfp_cmpd_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
        }
        tcg_temp_free_i64(tcg_vn);
        tcg_temp_free_i64(tcg_vm);
    } else {
        TCGv_i32 tcg_vn = tcg_temp_new_i32();
        TCGv_i32 tcg_vm = tcg_temp_new_i32();

        read_vec_element_i32(s, tcg_vn, rn, 0, size);
        if (cmp_with_zero) {
            tcg_gen_movi_i32(tcg_vm, 0);
        } else {
            read_vec_element_i32(s, tcg_vm, rm, 0, size);
        }

        switch (size) {
        case MO_32:
            if (signal_all_nans) {
                gen_helper_vfp_cmpes_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
            } else {
                gen_helper_vfp_cmps_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
            }
            break;
        case MO_16:
            if (signal_all_nans) {
                gen_helper_vfp_cmpeh_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
            } else {
                gen_helper_vfp_cmph_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
            }
            break;
        default:
            g_assert_not_reached();
        }

        tcg_temp_free_i32(tcg_vn);
        tcg_temp_free_i32(tcg_vm);
    }

    tcg_temp_free_ptr(fpst);

    gen_set_nzcv(tcg_flags);

    tcg_temp_free_i64(tcg_flags);
}

/* Floating point compare
 *   31  30  29 28       24 23  22  21 20  16 15 14 13  10    9    5 4     0
 * +---+---+---+-----------+------+---+------+-----+---------+------+-------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | op  | 1 0 0 0 |  Rn  |  op2  |
 * +---+---+---+-----------+------+---+------+-----+---------+------+-------+
 */
static void disas_fp_compare(DisasContext *s, uint32_t insn)
{
    unsigned int mos, type, rm, op, rn, opc, op2r;
    int size;

    mos = extract32(insn, 29, 3);
    type = extract32(insn, 22, 2);
    rm = extract32(insn, 16, 5);
    op = extract32(insn, 14, 2);
    rn = extract32(insn, 5, 5);
    opc = extract32(insn, 3, 2);
    op2r = extract32(insn, 0, 3);

    if (mos || op || op2r) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0:
        size = MO_32;
        break;
    case 1:
        size = MO_64;
        break;
    case 3:
        size = MO_16;
        if (dc_isar_feature(aa64_fp16, s)) {
            break;
        }
        /* fallthru */
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    handle_fp_compare(s, size, rn, rm, opc & 1, opc & 2);
}

/* Floating point conditional compare
 *   31  30  29 28       24 23  22  21 20  16 15  12 11 10 9    5  4   3    0
 * +---+---+---+-----------+------+---+------+------+-----+------+----+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | cond | 0 1 |  Rn  | op | nzcv |
 * +---+---+---+-----------+------+---+------+------+-----+------+----+------+
 */
static void disas_fp_ccomp(DisasContext *s, uint32_t insn)
{
    unsigned int mos, type, rm, cond, rn, op, nzcv;
    TCGv_i64 tcg_flags;
    TCGLabel *label_continue = NULL;
    int size;

    mos = extract32(insn, 29, 3);
    type = extract32(insn, 22, 2);
    rm = extract32(insn, 16, 5);
    cond = extract32(insn, 12, 4);
    rn = extract32(insn, 5, 5);
    op = extract32(insn, 4, 1);
    nzcv = extract32(insn, 0, 4);

    if (mos) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0:
        size = MO_32;
        break;
    case 1:
        size = MO_64;
        break;
    case 3:
        size = MO_16;
        if (dc_isar_feature(aa64_fp16, s)) {
            break;
        }
        /* fallthru */
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (cond < 0x0e) { /* not always */
        TCGLabel *label_match = gen_new_label();
        label_continue = gen_new_label();
        arm_gen_test_cc(cond, label_match);
        /* nomatch: */
        tcg_flags = tcg_const_i64(nzcv << 28);
        gen_set_nzcv(tcg_flags);
        tcg_temp_free_i64(tcg_flags);
        tcg_gen_br(label_continue);
        gen_set_label(label_match);
    }

    handle_fp_compare(s, size, rn, rm, false, op);

    if (cond < 0x0e) {
        gen_set_label(label_continue);
    }
}

/* Floating point conditional select
 *   31  30  29 28       24 23  22  21 20  16 15  12 11 10 9    5 4    0
 * +---+---+---+-----------+------+---+------+------+-----+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | cond | 1 1 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+------+------+-----+------+------+
 */
static void disas_fp_csel(DisasContext *s, uint32_t insn)
{
    unsigned int mos, type, rm, cond, rn, rd;
    TCGv_i64 t_true, t_false, t_zero;
    DisasCompare64 c;
    MemOp sz;

    mos = extract32(insn, 29, 3);
    type = extract32(insn, 22, 2);
    rm = extract32(insn, 16, 5);
    cond = extract32(insn, 12, 4);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    if (mos) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0:
        sz = MO_32;
        break;
    case 1:
        sz = MO_64;
        break;
    case 3:
        sz = MO_16;
        if (dc_isar_feature(aa64_fp16, s)) {
            break;
        }
        /* fallthru */
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    /* Zero extend sreg & hreg inputs to 64 bits now.  */
    t_true = tcg_temp_new_i64();
    t_false = tcg_temp_new_i64();
    read_vec_element(s, t_true, rn, 0, sz);
    read_vec_element(s, t_false, rm, 0, sz);

    a64_test_cc(&c, cond);
    t_zero = tcg_const_i64(0);
    tcg_gen_movcond_i64(c.cond, t_true, c.value, t_zero, t_true, t_false);
    tcg_temp_free_i64(t_zero);
    tcg_temp_free_i64(t_false);
    a64_free_cc(&c);

    /* Note that sregs & hregs write back zeros to the high bits,
       and we've already done the zero-extension.  */
    write_fp_dreg(s, rd, t_true);
    tcg_temp_free_i64(t_true);
}

/* Floating-point data-processing (1 source) - half precision */
static void handle_fp_1src_half(DisasContext *s, int opcode, int rd, int rn)
{
    TCGv_ptr fpst = NULL;
    TCGv_i32 tcg_op = read_fp_hreg(s, rn);
    TCGv_i32 tcg_res = tcg_temp_new_i32();

    switch (opcode) {
    case 0x0: /* FMOV */
        tcg_gen_mov_i32(tcg_res, tcg_op);
        break;
    case 0x1: /* FABS */
        tcg_gen_andi_i32(tcg_res, tcg_op, 0x7fff);
        break;
    case 0x2: /* FNEG */
        tcg_gen_xori_i32(tcg_res, tcg_op, 0x8000);
        break;
    case 0x3: /* FSQRT */
        fpst = fpstatus_ptr(FPST_FPCR_F16);
        gen_helper_sqrt_f16(tcg_res, tcg_op, fpst);
        break;
    case 0x8: /* FRINTN */
    case 0x9: /* FRINTP */
    case 0xa: /* FRINTM */
    case 0xb: /* FRINTZ */
    case 0xc: /* FRINTA */
    {
        TCGv_i32 tcg_rmode = tcg_const_i32(arm_rmode_to_sf(opcode & 7));
        fpst = fpstatus_ptr(FPST_FPCR_F16);

        gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
        gen_helper_advsimd_rinth(tcg_res, tcg_op, fpst);

        gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
        tcg_temp_free_i32(tcg_rmode);
        break;
    }
    case 0xe: /* FRINTX */
        fpst = fpstatus_ptr(FPST_FPCR_F16);
        gen_helper_advsimd_rinth_exact(tcg_res, tcg_op, fpst);
        break;
    case 0xf: /* FRINTI */
        fpst = fpstatus_ptr(FPST_FPCR_F16);
        gen_helper_advsimd_rinth(tcg_res, tcg_op, fpst);
        break;
    default:
        abort();
    }

    write_fp_sreg(s, rd, tcg_res);

    if (fpst) {
        tcg_temp_free_ptr(fpst);
    }
    tcg_temp_free_i32(tcg_op);
    tcg_temp_free_i32(tcg_res);
}

/* Floating-point data-processing (1 source) - single precision */
static void handle_fp_1src_single(DisasContext *s, int opcode, int rd, int rn)
{
    void (*gen_fpst)(TCGv_i32, TCGv_i32, TCGv_ptr);
    TCGv_i32 tcg_op, tcg_res;
    TCGv_ptr fpst;
    int rmode = -1;

    tcg_op = read_fp_sreg(s, rn);
    tcg_res = tcg_temp_new_i32();

    switch (opcode) {
    case 0x0: /* FMOV */
        tcg_gen_mov_i32(tcg_res, tcg_op);
        goto done;
    case 0x1: /* FABS */
        gen_helper_vfp_abss(tcg_res, tcg_op);
        goto done;
    case 0x2: /* FNEG */
        gen_helper_vfp_negs(tcg_res, tcg_op);
        goto done;
    case 0x3: /* FSQRT */
        gen_helper_vfp_sqrts(tcg_res, tcg_op, cpu_env);
        goto done;
    case 0x8: /* FRINTN */
    case 0x9: /* FRINTP */
    case 0xa: /* FRINTM */
    case 0xb: /* FRINTZ */
    case 0xc: /* FRINTA */
        rmode = arm_rmode_to_sf(opcode & 7);
        gen_fpst = gen_helper_rints;
        break;
    case 0xe: /* FRINTX */
        gen_fpst = gen_helper_rints_exact;
        break;
    case 0xf: /* FRINTI */
        gen_fpst = gen_helper_rints;
        break;
    case 0x10: /* FRINT32Z */
        rmode = float_round_to_zero;
        gen_fpst = gen_helper_frint32_s;
        break;
    case 0x11: /* FRINT32X */
        gen_fpst = gen_helper_frint32_s;
        break;
    case 0x12: /* FRINT64Z */
        rmode = float_round_to_zero;
        gen_fpst = gen_helper_frint64_s;
        break;
    case 0x13: /* FRINT64X */
        gen_fpst = gen_helper_frint64_s;
        break;
    default:
        g_assert_not_reached();
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    if (rmode >= 0) {
        TCGv_i32 tcg_rmode = tcg_const_i32(rmode);
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
        gen_fpst(tcg_res, tcg_op, fpst);
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
        tcg_temp_free_i32(tcg_rmode);
    } else {
        gen_fpst(tcg_res, tcg_op, fpst);
    }
    tcg_temp_free_ptr(fpst);

 done:
    write_fp_sreg(s, rd, tcg_res);
    tcg_temp_free_i32(tcg_op);
    tcg_temp_free_i32(tcg_res);
}

/* Floating-point data-processing (1 source) - double precision */
static void handle_fp_1src_double(DisasContext *s, int opcode, int rd, int rn)
{
    void (*gen_fpst)(TCGv_i64, TCGv_i64, TCGv_ptr);
    TCGv_i64 tcg_op, tcg_res;
    TCGv_ptr fpst;
    int rmode = -1;

    switch (opcode) {
    case 0x0: /* FMOV */
        gen_gvec_fn2(s, false, rd, rn, tcg_gen_gvec_mov, 0);
        return;
    }

    tcg_op = read_fp_dreg(s, rn);
    tcg_res = tcg_temp_new_i64();

    switch (opcode) {
    case 0x1: /* FABS */
        gen_helper_vfp_absd(tcg_res, tcg_op);
        goto done;
    case 0x2: /* FNEG */
        gen_helper_vfp_negd(tcg_res, tcg_op);
        goto done;
    case 0x3: /* FSQRT */
        gen_helper_vfp_sqrtd(tcg_res, tcg_op, cpu_env);
        goto done;
    case 0x8: /* FRINTN */
    case 0x9: /* FRINTP */
    case 0xa: /* FRINTM */
    case 0xb: /* FRINTZ */
    case 0xc: /* FRINTA */
        rmode = arm_rmode_to_sf(opcode & 7);
        gen_fpst = gen_helper_rintd;
        break;
    case 0xe: /* FRINTX */
        gen_fpst = gen_helper_rintd_exact;
        break;
    case 0xf: /* FRINTI */
        gen_fpst = gen_helper_rintd;
        break;
    case 0x10: /* FRINT32Z */
        rmode = float_round_to_zero;
        gen_fpst = gen_helper_frint32_d;
        break;
    case 0x11: /* FRINT32X */
        gen_fpst = gen_helper_frint32_d;
        break;
    case 0x12: /* FRINT64Z */
        rmode = float_round_to_zero;
        gen_fpst = gen_helper_frint64_d;
        break;
    case 0x13: /* FRINT64X */
        gen_fpst = gen_helper_frint64_d;
        break;
    default:
        g_assert_not_reached();
    }

    fpst = fpstatus_ptr(FPST_FPCR);
    if (rmode >= 0) {
        TCGv_i32 tcg_rmode = tcg_const_i32(rmode);
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
        gen_fpst(tcg_res, tcg_op, fpst);
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, fpst);
        tcg_temp_free_i32(tcg_rmode);
    } else {
        gen_fpst(tcg_res, tcg_op, fpst);
    }
    tcg_temp_free_ptr(fpst);

 done:
    write_fp_dreg(s, rd, tcg_res);
    tcg_temp_free_i64(tcg_op);
    tcg_temp_free_i64(tcg_res);
}

static void handle_fp_fcvt(DisasContext *s, int opcode,
                           int rd, int rn, int dtype, int ntype)
{
    switch (ntype) {
    case 0x0:
    {
        TCGv_i32 tcg_rn = read_fp_sreg(s, rn);
        if (dtype == 1) {
            /* Single to double */
            TCGv_i64 tcg_rd = tcg_temp_new_i64();
            gen_helper_vfp_fcvtds(tcg_rd, tcg_rn, cpu_env);
            write_fp_dreg(s, rd, tcg_rd);
            tcg_temp_free_i64(tcg_rd);
        } else {
            /* Single to half */
            TCGv_i32 tcg_rd = tcg_temp_new_i32();
            TCGv_i32 ahp = get_ahp_flag();
            TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);

            gen_helper_vfp_fcvt_f32_to_f16(tcg_rd, tcg_rn, fpst, ahp);
            /* write_fp_sreg is OK here because top half of tcg_rd is zero */
            write_fp_sreg(s, rd, tcg_rd);
            tcg_temp_free_i32(tcg_rd);
            tcg_temp_free_i32(ahp);
            tcg_temp_free_ptr(fpst);
        }
        tcg_temp_free_i32(tcg_rn);
        break;
    }
    case 0x1:
    {
        TCGv_i64 tcg_rn = read_fp_dreg(s, rn);
        TCGv_i32 tcg_rd = tcg_temp_new_i32();
        if (dtype == 0) {
            /* Double to single */
            gen_helper_vfp_fcvtsd(tcg_rd, tcg_rn, cpu_env);
        } else {
            TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);
            TCGv_i32 ahp = get_ahp_flag();
            /* Double to half */
            gen_helper_vfp_fcvt_f64_to_f16(tcg_rd, tcg_rn, fpst, ahp);
            /* write_fp_sreg is OK here because top half of tcg_rd is zero */
            tcg_temp_free_ptr(fpst);
            tcg_temp_free_i32(ahp);
        }
        write_fp_sreg(s, rd, tcg_rd);
        tcg_temp_free_i32(tcg_rd);
        tcg_temp_free_i64(tcg_rn);
        break;
    }
    case 0x3:
    {
        TCGv_i32 tcg_rn = read_fp_sreg(s, rn);
        TCGv_ptr tcg_fpst = fpstatus_ptr(FPST_FPCR);
        TCGv_i32 tcg_ahp = get_ahp_flag();
        tcg_gen_ext16u_i32(tcg_rn, tcg_rn);
        if (dtype == 0) {
            /* Half to single */
            TCGv_i32 tcg_rd = tcg_temp_new_i32();
            gen_helper_vfp_fcvt_f16_to_f32(tcg_rd, tcg_rn, tcg_fpst, tcg_ahp);
            write_fp_sreg(s, rd, tcg_rd);
            tcg_temp_free_i32(tcg_rd);
        } else {
            /* Half to double */
            TCGv_i64 tcg_rd = tcg_temp_new_i64();
            gen_helper_vfp_fcvt_f16_to_f64(tcg_rd, tcg_rn, tcg_fpst, tcg_ahp);
            write_fp_dreg(s, rd, tcg_rd);
            tcg_temp_free_i64(tcg_rd);
        }
        tcg_temp_free_i32(tcg_rn);
        tcg_temp_free_ptr(tcg_fpst);
        tcg_temp_free_i32(tcg_ahp);
        break;
    }
    default:
        abort();
    }
}

/* Floating point data-processing (1 source)
 *   31  30  29 28       24 23  22  21 20    15 14       10 9    5 4    0
 * +---+---+---+-----------+------+---+--------+-----------+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 | opcode | 1 0 0 0 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+--------+-----------+------+------+
 */
static void disas_fp_1src(DisasContext *s, uint32_t insn)
{
    int mos = extract32(insn, 29, 3);
    int type = extract32(insn, 22, 2);
    int opcode = extract32(insn, 15, 6);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);

    if (mos) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0x4: case 0x5: case 0x7:
    {
        /* FCVT between half, single and double precision */
        int dtype = extract32(opcode, 0, 2);
        if (type == 2 || dtype == type) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }

        handle_fp_fcvt(s, opcode, rd, rn, dtype, type);
        break;
    }

    case 0x10 ... 0x13: /* FRINT{32,64}{X,Z} */
        if (type > 1 || !dc_isar_feature(aa64_frint, s)) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x0 ... 0x3:
    case 0x8 ... 0xc:
    case 0xe ... 0xf:
        /* 32-to-32 and 64-to-64 ops */
        switch (type) {
        case 0:
            if (!fp_access_check(s)) {
                return;
            }
            handle_fp_1src_single(s, opcode, rd, rn);
            break;
        case 1:
            if (!fp_access_check(s)) {
                return;
            }
            handle_fp_1src_double(s, opcode, rd, rn);
            break;
        case 3:
            if (!dc_isar_feature(aa64_fp16, s)) {
                unallocated_encoding(s);
                return;
            }

            if (!fp_access_check(s)) {
                return;
            }
            handle_fp_1src_half(s, opcode, rd, rn);
            break;
        default:
            unallocated_encoding(s);
        }
        break;

    default:
        unallocated_encoding(s);
        break;
    }
}

/* Floating-point data-processing (2 source) - single precision */
static void handle_fp_2src_single(DisasContext *s, int opcode,
                                  int rd, int rn, int rm)
{
    TCGv_i32 tcg_op1;
    TCGv_i32 tcg_op2;
    TCGv_i32 tcg_res;
    TCGv_ptr fpst;

    tcg_res = tcg_temp_new_i32();
    fpst = fpstatus_ptr(FPST_FPCR);
    tcg_op1 = read_fp_sreg(s, rn);
    tcg_op2 = read_fp_sreg(s, rm);

    switch (opcode) {
    case 0x0: /* FMUL */
        gen_helper_vfp_muls(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x1: /* FDIV */
        gen_helper_vfp_divs(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x2: /* FADD */
        gen_helper_vfp_adds(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x3: /* FSUB */
        gen_helper_vfp_subs(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x4: /* FMAX */
        gen_helper_vfp_maxs(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x5: /* FMIN */
        gen_helper_vfp_mins(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x6: /* FMAXNM */
        gen_helper_vfp_maxnums(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x7: /* FMINNM */
        gen_helper_vfp_minnums(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x8: /* FNMUL */
        gen_helper_vfp_muls(tcg_res, tcg_op1, tcg_op2, fpst);
        gen_helper_vfp_negs(tcg_res, tcg_res);
        break;
    }

    write_fp_sreg(s, rd, tcg_res);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tcg_op1);
    tcg_temp_free_i32(tcg_op2);
    tcg_temp_free_i32(tcg_res);
}

/* Floating-point data-processing (2 source) - double precision */
static void handle_fp_2src_double(DisasContext *s, int opcode,
                                  int rd, int rn, int rm)
{
    TCGv_i64 tcg_op1;
    TCGv_i64 tcg_op2;
    TCGv_i64 tcg_res;
    TCGv_ptr fpst;

    tcg_res = tcg_temp_new_i64();
    fpst = fpstatus_ptr(FPST_FPCR);
    tcg_op1 = read_fp_dreg(s, rn);
    tcg_op2 = read_fp_dreg(s, rm);

    switch (opcode) {
    case 0x0: /* FMUL */
        gen_helper_vfp_muld(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x1: /* FDIV */
        gen_helper_vfp_divd(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x2: /* FADD */
        gen_helper_vfp_addd(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x3: /* FSUB */
        gen_helper_vfp_subd(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x4: /* FMAX */
        gen_helper_vfp_maxd(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x5: /* FMIN */
        gen_helper_vfp_mind(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x6: /* FMAXNM */
        gen_helper_vfp_maxnumd(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x7: /* FMINNM */
        gen_helper_vfp_minnumd(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x8: /* FNMUL */
        gen_helper_vfp_muld(tcg_res, tcg_op1, tcg_op2, fpst);
        gen_helper_vfp_negd(tcg_res, tcg_res);
        break;
    }

    write_fp_dreg(s, rd, tcg_res);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i64(tcg_op1);
    tcg_temp_free_i64(tcg_op2);
    tcg_temp_free_i64(tcg_res);
}

/* Floating-point data-processing (2 source) - half precision */
static void handle_fp_2src_half(DisasContext *s, int opcode,
                                int rd, int rn, int rm)
{
    TCGv_i32 tcg_op1;
    TCGv_i32 tcg_op2;
    TCGv_i32 tcg_res;
    TCGv_ptr fpst;

    tcg_res = tcg_temp_new_i32();
    fpst = fpstatus_ptr(FPST_FPCR_F16);
    tcg_op1 = read_fp_hreg(s, rn);
    tcg_op2 = read_fp_hreg(s, rm);

    switch (opcode) {
    case 0x0: /* FMUL */
        gen_helper_advsimd_mulh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x1: /* FDIV */
        gen_helper_advsimd_divh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x2: /* FADD */
        gen_helper_advsimd_addh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x3: /* FSUB */
        gen_helper_advsimd_subh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x4: /* FMAX */
        gen_helper_advsimd_maxh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x5: /* FMIN */
        gen_helper_advsimd_minh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x6: /* FMAXNM */
        gen_helper_advsimd_maxnumh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x7: /* FMINNM */
        gen_helper_advsimd_minnumh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x8: /* FNMUL */
        gen_helper_advsimd_mulh(tcg_res, tcg_op1, tcg_op2, fpst);
        tcg_gen_xori_i32(tcg_res, tcg_res, 0x8000);
        break;
    default:
        g_assert_not_reached();
    }

    write_fp_sreg(s, rd, tcg_res);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tcg_op1);
    tcg_temp_free_i32(tcg_op2);
    tcg_temp_free_i32(tcg_res);
}

/* Floating point data-processing (2 source)
 *   31  30  29 28       24 23  22  21 20  16 15    12 11 10 9    5 4    0
 * +---+---+---+-----------+------+---+------+--------+-----+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | opcode | 1 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+------+--------+-----+------+------+
 */
static void disas_fp_2src(DisasContext *s, uint32_t insn)
{
    int mos = extract32(insn, 29, 3);
    int type = extract32(insn, 22, 2);
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rm = extract32(insn, 16, 5);
    int opcode = extract32(insn, 12, 4);

    if (opcode > 8 || mos) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0:
        if (!fp_access_check(s)) {
            return;
        }
        handle_fp_2src_single(s, opcode, rd, rn, rm);
        break;
    case 1:
        if (!fp_access_check(s)) {
            return;
        }
        handle_fp_2src_double(s, opcode, rd, rn, rm);
        break;
    case 3:
        if (!dc_isar_feature(aa64_fp16, s)) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_fp_2src_half(s, opcode, rd, rn, rm);
        break;
    default:
        unallocated_encoding(s);
    }
}

/* Floating-point data-processing (3 source) - single precision */
static void handle_fp_3src_single(DisasContext *s, bool o0, bool o1,
                                  int rd, int rn, int rm, int ra)
{
    TCGv_i32 tcg_op1, tcg_op2, tcg_op3;
    TCGv_i32 tcg_res = tcg_temp_new_i32();
    TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);

    tcg_op1 = read_fp_sreg(s, rn);
    tcg_op2 = read_fp_sreg(s, rm);
    tcg_op3 = read_fp_sreg(s, ra);

    /* These are fused multiply-add, and must be done as one
     * floating point operation with no rounding between the
     * multiplication and addition steps.
     * NB that doing the negations here as separate steps is
     * correct : an input NaN should come out with its sign bit
     * flipped if it is a negated-input.
     */
    if (o1 == true) {
        gen_helper_vfp_negs(tcg_op3, tcg_op3);
    }

    if (o0 != o1) {
        gen_helper_vfp_negs(tcg_op1, tcg_op1);
    }

    gen_helper_vfp_muladds(tcg_res, tcg_op1, tcg_op2, tcg_op3, fpst);

    write_fp_sreg(s, rd, tcg_res);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tcg_op1);
    tcg_temp_free_i32(tcg_op2);
    tcg_temp_free_i32(tcg_op3);
    tcg_temp_free_i32(tcg_res);
}

/* Floating-point data-processing (3 source) - double precision */
static void handle_fp_3src_double(DisasContext *s, bool o0, bool o1,
                                  int rd, int rn, int rm, int ra)
{
    TCGv_i64 tcg_op1, tcg_op2, tcg_op3;
    TCGv_i64 tcg_res = tcg_temp_new_i64();
    TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);

    tcg_op1 = read_fp_dreg(s, rn);
    tcg_op2 = read_fp_dreg(s, rm);
    tcg_op3 = read_fp_dreg(s, ra);

    /* These are fused multiply-add, and must be done as one
     * floating point operation with no rounding between the
     * multiplication and addition steps.
     * NB that doing the negations here as separate steps is
     * correct : an input NaN should come out with its sign bit
     * flipped if it is a negated-input.
     */
    if (o1 == true) {
        gen_helper_vfp_negd(tcg_op3, tcg_op3);
    }

    if (o0 != o1) {
        gen_helper_vfp_negd(tcg_op1, tcg_op1);
    }

    gen_helper_vfp_muladdd(tcg_res, tcg_op1, tcg_op2, tcg_op3, fpst);

    write_fp_dreg(s, rd, tcg_res);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i64(tcg_op1);
    tcg_temp_free_i64(tcg_op2);
    tcg_temp_free_i64(tcg_op3);
    tcg_temp_free_i64(tcg_res);
}

/* Floating-point data-processing (3 source) - half precision */
static void handle_fp_3src_half(DisasContext *s, bool o0, bool o1,
                                int rd, int rn, int rm, int ra)
{
    TCGv_i32 tcg_op1, tcg_op2, tcg_op3;
    TCGv_i32 tcg_res = tcg_temp_new_i32();
    TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR_F16);

    tcg_op1 = read_fp_hreg(s, rn);
    tcg_op2 = read_fp_hreg(s, rm);
    tcg_op3 = read_fp_hreg(s, ra);

    /* These are fused multiply-add, and must be done as one
     * floating point operation with no rounding between the
     * multiplication and addition steps.
     * NB that doing the negations here as separate steps is
     * correct : an input NaN should come out with its sign bit
     * flipped if it is a negated-input.
     */
    if (o1 == true) {
        tcg_gen_xori_i32(tcg_op3, tcg_op3, 0x8000);
    }

    if (o0 != o1) {
        tcg_gen_xori_i32(tcg_op1, tcg_op1, 0x8000);
    }

    gen_helper_advsimd_muladdh(tcg_res, tcg_op1, tcg_op2, tcg_op3, fpst);

    write_fp_sreg(s, rd, tcg_res);

    tcg_temp_free_ptr(fpst);
    tcg_temp_free_i32(tcg_op1);
    tcg_temp_free_i32(tcg_op2);
    tcg_temp_free_i32(tcg_op3);
    tcg_temp_free_i32(tcg_res);
}

/* Floating point data-processing (3 source)
 *   31  30  29 28       24 23  22  21  20  16  15  14  10 9    5 4    0
 * +---+---+---+-----------+------+----+------+----+------+------+------+
 * | M | 0 | S | 1 1 1 1 1 | type | o1 |  Rm  | o0 |  Ra  |  Rn  |  Rd  |
 * +---+---+---+-----------+------+----+------+----+------+------+------+
 */
static void disas_fp_3src(DisasContext *s, uint32_t insn)
{
    int mos = extract32(insn, 29, 3);
    int type = extract32(insn, 22, 2);
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int ra = extract32(insn, 10, 5);
    int rm = extract32(insn, 16, 5);
    bool o0 = extract32(insn, 15, 1);
    bool o1 = extract32(insn, 21, 1);

    if (mos) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0:
        if (!fp_access_check(s)) {
            return;
        }
        handle_fp_3src_single(s, o0, o1, rd, rn, rm, ra);
        break;
    case 1:
        if (!fp_access_check(s)) {
            return;
        }
        handle_fp_3src_double(s, o0, o1, rd, rn, rm, ra);
        break;
    case 3:
        if (!dc_isar_feature(aa64_fp16, s)) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_fp_3src_half(s, o0, o1, rd, rn, rm, ra);
        break;
    default:
        unallocated_encoding(s);
    }
}

/* Floating point immediate
 *   31  30  29 28       24 23  22  21 20        13 12   10 9    5 4    0
 * +---+---+---+-----------+------+---+------------+-------+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |    imm8    | 1 0 0 | imm5 |  Rd  |
 * +---+---+---+-----------+------+---+------------+-------+------+------+
 */
static void disas_fp_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int imm5 = extract32(insn, 5, 5);
    int imm8 = extract32(insn, 13, 8);
    int type = extract32(insn, 22, 2);
    int mos = extract32(insn, 29, 3);
    uint64_t imm;
    TCGv_i64 tcg_res;
    MemOp sz;

    if (mos || imm5) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0:
        sz = MO_32;
        break;
    case 1:
        sz = MO_64;
        break;
    case 3:
        sz = MO_16;
        if (dc_isar_feature(aa64_fp16, s)) {
            break;
        }
        /* fallthru */
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    imm = vfp_expand_imm(sz, imm8);

    tcg_res = tcg_const_i64(imm);
    write_fp_dreg(s, rd, tcg_res);
    tcg_temp_free_i64(tcg_res);
}

/* Handle floating point <=> fixed point conversions. Note that we can
 * also deal with fp <=> integer conversions as a special case (scale == 64)
 * OPTME: consider handling that special case specially or at least skipping
 * the call to scalbn in the helpers for zero shifts.
 */
static void handle_fpfpcvt(DisasContext *s, int rd, int rn, int opcode,
                           bool itof, int rmode, int scale, int sf, int type)
{
    bool is_signed = !(opcode & 1);
    TCGv_ptr tcg_fpstatus;
    TCGv_i32 tcg_shift, tcg_single;
    TCGv_i64 tcg_double;

    tcg_fpstatus = fpstatus_ptr(type == 3 ? FPST_FPCR_F16 : FPST_FPCR);

    tcg_shift = tcg_const_i32(64 - scale);

    if (itof) {
        TCGv_i64 tcg_int = cpu_reg(s, rn);
        if (!sf) {
            TCGv_i64 tcg_extend = new_tmp_a64(s);

            if (is_signed) {
                tcg_gen_ext32s_i64(tcg_extend, tcg_int);
            } else {
                tcg_gen_ext32u_i64(tcg_extend, tcg_int);
            }

            tcg_int = tcg_extend;
        }

        switch (type) {
        case 1: /* float64 */
            tcg_double = tcg_temp_new_i64();
            if (is_signed) {
                gen_helper_vfp_sqtod(tcg_double, tcg_int,
                                     tcg_shift, tcg_fpstatus);
            } else {
                gen_helper_vfp_uqtod(tcg_double, tcg_int,
                                     tcg_shift, tcg_fpstatus);
            }
            write_fp_dreg(s, rd, tcg_double);
            tcg_temp_free_i64(tcg_double);
            break;

        case 0: /* float32 */
            tcg_single = tcg_temp_new_i32();
            if (is_signed) {
                gen_helper_vfp_sqtos(tcg_single, tcg_int,
                                     tcg_shift, tcg_fpstatus);
            } else {
                gen_helper_vfp_uqtos(tcg_single, tcg_int,
                                     tcg_shift, tcg_fpstatus);
            }
            write_fp_sreg(s, rd, tcg_single);
            tcg_temp_free_i32(tcg_single);
            break;

        case 3: /* float16 */
            tcg_single = tcg_temp_new_i32();
            if (is_signed) {
                gen_helper_vfp_sqtoh(tcg_single, tcg_int,
                                     tcg_shift, tcg_fpstatus);
            } else {
                gen_helper_vfp_uqtoh(tcg_single, tcg_int,
                                     tcg_shift, tcg_fpstatus);
            }
            write_fp_sreg(s, rd, tcg_single);
            tcg_temp_free_i32(tcg_single);
            break;

        default:
            g_assert_not_reached();
        }
    } else {
        TCGv_i64 tcg_int = cpu_reg(s, rd);
        TCGv_i32 tcg_rmode;

        if (extract32(opcode, 2, 1)) {
            /* There are too many rounding modes to all fit into rmode,
             * so FCVTA[US] is a special case.
             */
            rmode = FPROUNDING_TIEAWAY;
        }

        tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));

        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);

        switch (type) {
        case 1: /* float64 */
            tcg_double = read_fp_dreg(s, rn);
            if (is_signed) {
                if (!sf) {
                    gen_helper_vfp_tosld(tcg_int, tcg_double,
                                         tcg_shift, tcg_fpstatus);
                } else {
                    gen_helper_vfp_tosqd(tcg_int, tcg_double,
                                         tcg_shift, tcg_fpstatus);
                }
            } else {
                if (!sf) {
                    gen_helper_vfp_tould(tcg_int, tcg_double,
                                         tcg_shift, tcg_fpstatus);
                } else {
                    gen_helper_vfp_touqd(tcg_int, tcg_double,
                                         tcg_shift, tcg_fpstatus);
                }
            }
            if (!sf) {
                tcg_gen_ext32u_i64(tcg_int, tcg_int);
            }
            tcg_temp_free_i64(tcg_double);
            break;

        case 0: /* float32 */
            tcg_single = read_fp_sreg(s, rn);
            if (sf) {
                if (is_signed) {
                    gen_helper_vfp_tosqs(tcg_int, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                } else {
                    gen_helper_vfp_touqs(tcg_int, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                }
            } else {
                TCGv_i32 tcg_dest = tcg_temp_new_i32();
                if (is_signed) {
                    gen_helper_vfp_tosls(tcg_dest, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                } else {
                    gen_helper_vfp_touls(tcg_dest, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                }
                tcg_gen_extu_i32_i64(tcg_int, tcg_dest);
                tcg_temp_free_i32(tcg_dest);
            }
            tcg_temp_free_i32(tcg_single);
            break;

        case 3: /* float16 */
            tcg_single = read_fp_sreg(s, rn);
            if (sf) {
                if (is_signed) {
                    gen_helper_vfp_tosqh(tcg_int, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                } else {
                    gen_helper_vfp_touqh(tcg_int, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                }
            } else {
                TCGv_i32 tcg_dest = tcg_temp_new_i32();
                if (is_signed) {
                    gen_helper_vfp_toslh(tcg_dest, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                } else {
                    gen_helper_vfp_toulh(tcg_dest, tcg_single,
                                         tcg_shift, tcg_fpstatus);
                }
                tcg_gen_extu_i32_i64(tcg_int, tcg_dest);
                tcg_temp_free_i32(tcg_dest);
            }
            tcg_temp_free_i32(tcg_single);
            break;

        default:
            g_assert_not_reached();
        }

        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
        tcg_temp_free_i32(tcg_rmode);
    }

    tcg_temp_free_ptr(tcg_fpstatus);
    tcg_temp_free_i32(tcg_shift);
}

/* Floating point <-> fixed point conversions
 *   31   30  29 28       24 23  22  21 20   19 18    16 15   10 9    5 4    0
 * +----+---+---+-----------+------+---+-------+--------+-------+------+------+
 * | sf | 0 | S | 1 1 1 1 0 | type | 0 | rmode | opcode | scale |  Rn  |  Rd  |
 * +----+---+---+-----------+------+---+-------+--------+-------+------+------+
 */
static void disas_fp_fixed_conv(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int scale = extract32(insn, 10, 6);
    int opcode = extract32(insn, 16, 3);
    int rmode = extract32(insn, 19, 2);
    int type = extract32(insn, 22, 2);
    bool sbit = extract32(insn, 29, 1);
    bool sf = extract32(insn, 31, 1);
    bool itof;

    if (sbit || (!sf && scale < 32)) {
        unallocated_encoding(s);
        return;
    }

    switch (type) {
    case 0: /* float32 */
    case 1: /* float64 */
        break;
    case 3: /* float16 */
        if (dc_isar_feature(aa64_fp16, s)) {
            break;
        }
        /* fallthru */
    default:
        unallocated_encoding(s);
        return;
    }

    switch ((rmode << 3) | opcode) {
    case 0x2: /* SCVTF */
    case 0x3: /* UCVTF */
        itof = true;
        break;
    case 0x18: /* FCVTZS */
    case 0x19: /* FCVTZU */
        itof = false;
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    handle_fpfpcvt(s, rd, rn, opcode, itof, FPROUNDING_ZERO, scale, sf, type);
}

static void handle_fmov(DisasContext *s, int rd, int rn, int type, bool itof)
{
    /* FMOV: gpr to or from float, double, or top half of quad fp reg,
     * without conversion.
     */

    if (itof) {
        TCGv_i64 tcg_rn = cpu_reg(s, rn);
        TCGv_i64 tmp;

        switch (type) {
        case 0:
            /* 32 bit */
            tmp = tcg_temp_new_i64();
            tcg_gen_ext32u_i64(tmp, tcg_rn);
            write_fp_dreg(s, rd, tmp);
            tcg_temp_free_i64(tmp);
            break;
        case 1:
            /* 64 bit */
            write_fp_dreg(s, rd, tcg_rn);
            break;
        case 2:
            /* 64 bit to top half. */
            tcg_gen_st_i64(tcg_rn, cpu_env, fp_reg_hi_offset(s, rd));
            clear_vec_high(s, true, rd);
            break;
        case 3:
            /* 16 bit */
            tmp = tcg_temp_new_i64();
            tcg_gen_ext16u_i64(tmp, tcg_rn);
            write_fp_dreg(s, rd, tmp);
            tcg_temp_free_i64(tmp);
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        TCGv_i64 tcg_rd = cpu_reg(s, rd);

        switch (type) {
        case 0:
            /* 32 bit */
            tcg_gen_ld32u_i64(tcg_rd, cpu_env, fp_reg_offset(s, rn, MO_32));
            break;
        case 1:
            /* 64 bit */
            tcg_gen_ld_i64(tcg_rd, cpu_env, fp_reg_offset(s, rn, MO_64));
            break;
        case 2:
            /* 64 bits from top half */
            tcg_gen_ld_i64(tcg_rd, cpu_env, fp_reg_hi_offset(s, rn));
            break;
        case 3:
            /* 16 bit */
            tcg_gen_ld16u_i64(tcg_rd, cpu_env, fp_reg_offset(s, rn, MO_16));
            break;
        default:
            g_assert_not_reached();
        }
    }
}

static void handle_fjcvtzs(DisasContext *s, int rd, int rn)
{
    TCGv_i64 t = read_fp_dreg(s, rn);
    TCGv_ptr fpstatus = fpstatus_ptr(FPST_FPCR);

    gen_helper_fjcvtzs(t, t, fpstatus);

    tcg_temp_free_ptr(fpstatus);

    tcg_gen_ext32u_i64(cpu_reg(s, rd), t);
    tcg_gen_extrh_i64_i32(cpu_ZF, t);
    tcg_gen_movi_i32(cpu_CF, 0);
    tcg_gen_movi_i32(cpu_NF, 0);
    tcg_gen_movi_i32(cpu_VF, 0);

    tcg_temp_free_i64(t);
}

/* Floating point <-> integer conversions
 *   31   30  29 28       24 23  22  21 20   19 18 16 15         10 9  5 4  0
 * +----+---+---+-----------+------+---+-------+-----+-------------+----+----+
 * | sf | 0 | S | 1 1 1 1 0 | type | 1 | rmode | opc | 0 0 0 0 0 0 | Rn | Rd |
 * +----+---+---+-----------+------+---+-------+-----+-------------+----+----+
 */
static void disas_fp_int_conv(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 16, 3);
    int rmode = extract32(insn, 19, 2);
    int type = extract32(insn, 22, 2);
    bool sbit = extract32(insn, 29, 1);
    bool sf = extract32(insn, 31, 1);
    bool itof = false;

    if (sbit) {
        goto do_unallocated;
    }

    switch (opcode) {
    case 2: /* SCVTF */
    case 3: /* UCVTF */
        itof = true;
        /* fallthru */
    case 4: /* FCVTAS */
    case 5: /* FCVTAU */
        if (rmode != 0) {
            goto do_unallocated;
        }
        /* fallthru */
    case 0: /* FCVT[NPMZ]S */
    case 1: /* FCVT[NPMZ]U */
        switch (type) {
        case 0: /* float32 */
        case 1: /* float64 */
            break;
        case 3: /* float16 */
            if (!dc_isar_feature(aa64_fp16, s)) {
                goto do_unallocated;
            }
            break;
        default:
            goto do_unallocated;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_fpfpcvt(s, rd, rn, opcode, itof, rmode, 64, sf, type);
        break;

    default:
        switch (sf << 7 | type << 5 | rmode << 3 | opcode) {
        case 0b01100110: /* FMOV half <-> 32-bit int */
        case 0b01100111:
        case 0b11100110: /* FMOV half <-> 64-bit int */
        case 0b11100111:
            if (!dc_isar_feature(aa64_fp16, s)) {
                goto do_unallocated;
            }
            /* fallthru */
        case 0b00000110: /* FMOV 32-bit */
        case 0b00000111:
        case 0b10100110: /* FMOV 64-bit */
        case 0b10100111:
        case 0b11001110: /* FMOV top half of 128-bit */
        case 0b11001111:
            if (!fp_access_check(s)) {
                return;
            }
            itof = opcode & 1;
            handle_fmov(s, rd, rn, type, itof);
            break;

        case 0b00111110: /* FJCVTZS */
            if (!dc_isar_feature(aa64_jscvt, s)) {
                goto do_unallocated;
            } else if (fp_access_check(s)) {
                handle_fjcvtzs(s, rd, rn);
            }
            break;

        default:
        do_unallocated:
            unallocated_encoding(s);
            return;
        }
        break;
    }
}

/* FP-specific subcases of table C3-6 (SIMD and FP data processing)
 *   31  30  29 28     25 24                          0
 * +---+---+---+---------+-----------------------------+
 * |   | 0 |   | 1 1 1 1 |                             |
 * +---+---+---+---------+-----------------------------+
 */
static void disas_data_proc_fp(DisasContext *s, uint32_t insn)
{
    if (extract32(insn, 24, 1)) {
        /* Floating point data-processing (3 source) */
        disas_fp_3src(s, insn);
    } else if (extract32(insn, 21, 1) == 0) {
        /* Floating point to fixed point conversions */
        disas_fp_fixed_conv(s, insn);
    } else {
        switch (extract32(insn, 10, 2)) {
        case 1:
            /* Floating point conditional compare */
            disas_fp_ccomp(s, insn);
            break;
        case 2:
            /* Floating point data-processing (2 source) */
            disas_fp_2src(s, insn);
            break;
        case 3:
            /* Floating point conditional select */
            disas_fp_csel(s, insn);
            break;
        case 0:
            switch (ctz32(extract32(insn, 12, 4))) {
            case 0: /* [15:12] == xxx1 */
                /* Floating point immediate */
                disas_fp_imm(s, insn);
                break;
            case 1: /* [15:12] == xx10 */
                /* Floating point compare */
                disas_fp_compare(s, insn);
                break;
            case 2: /* [15:12] == x100 */
                /* Floating point data-processing (1 source) */
                disas_fp_1src(s, insn);
                break;
            case 3: /* [15:12] == 1000 */
                unallocated_encoding(s);
                break;
            default: /* [15:12] == 0000 */
                /* Floating point <-> integer conversions */
                disas_fp_int_conv(s, insn);
                break;
            }
            break;
        }
    }
}

static void do_ext64(DisasContext *s, TCGv_i64 tcg_left, TCGv_i64 tcg_right,
                     int pos)
{
    /* Extract 64 bits from the middle of two concatenated 64 bit
     * vector register slices left:right. The extracted bits start
     * at 'pos' bits into the right (least significant) side.
     * We return the result in tcg_right, and guarantee not to
     * trash tcg_left.
     */
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();
    assert(pos > 0 && pos < 64);

    tcg_gen_shri_i64(tcg_right, tcg_right, pos);
    tcg_gen_shli_i64(tcg_tmp, tcg_left, 64 - pos);
    tcg_gen_or_i64(tcg_right, tcg_right, tcg_tmp);

    tcg_temp_free_i64(tcg_tmp);
}

/* EXT
 *   31  30 29         24 23 22  21 20  16 15  14  11 10  9    5 4    0
 * +---+---+-------------+-----+---+------+---+------+---+------+------+
 * | 0 | Q | 1 0 1 1 1 0 | op2 | 0 |  Rm  | 0 | imm4 | 0 |  Rn  |  Rd  |
 * +---+---+-------------+-----+---+------+---+------+---+------+------+
 */
static void disas_simd_ext(DisasContext *s, uint32_t insn)
{
    int is_q = extract32(insn, 30, 1);
    int op2 = extract32(insn, 22, 2);
    int imm4 = extract32(insn, 11, 4);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    int pos = imm4 << 3;
    TCGv_i64 tcg_resl, tcg_resh;

    if (op2 != 0 || (!is_q && extract32(imm4, 3, 1))) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_resh = tcg_temp_new_i64();
    tcg_resl = tcg_temp_new_i64();

    /* Vd gets bits starting at pos bits into Vm:Vn. This is
     * either extracting 128 bits from a 128:128 concatenation, or
     * extracting 64 bits from a 64:64 concatenation.
     */
    if (!is_q) {
        read_vec_element(s, tcg_resl, rn, 0, MO_64);
        if (pos != 0) {
            read_vec_element(s, tcg_resh, rm, 0, MO_64);
            do_ext64(s, tcg_resh, tcg_resl, pos);
        }
    } else {
        TCGv_i64 tcg_hh;
        typedef struct {
            int reg;
            int elt;
        } EltPosns;
        EltPosns eltposns[] = { {rn, 0}, {rn, 1}, {rm, 0}, {rm, 1} };
        EltPosns *elt = eltposns;

        if (pos >= 64) {
            elt++;
            pos -= 64;
        }

        read_vec_element(s, tcg_resl, elt->reg, elt->elt, MO_64);
        elt++;
        read_vec_element(s, tcg_resh, elt->reg, elt->elt, MO_64);
        elt++;
        if (pos != 0) {
            do_ext64(s, tcg_resh, tcg_resl, pos);
            tcg_hh = tcg_temp_new_i64();
            read_vec_element(s, tcg_hh, elt->reg, elt->elt, MO_64);
            do_ext64(s, tcg_hh, tcg_resh, pos);
            tcg_temp_free_i64(tcg_hh);
        }
    }

    write_vec_element(s, tcg_resl, rd, 0, MO_64);
    tcg_temp_free_i64(tcg_resl);
    if (is_q) {
        write_vec_element(s, tcg_resh, rd, 1, MO_64);
    }
    tcg_temp_free_i64(tcg_resh);
    clear_vec_high(s, is_q, rd);
}

/* TBL/TBX
 *   31  30 29         24 23 22  21 20  16 15  14 13  12  11 10 9    5 4    0
 * +---+---+-------------+-----+---+------+---+-----+----+-----+------+------+
 * | 0 | Q | 0 0 1 1 1 0 | op2 | 0 |  Rm  | 0 | len | op | 0 0 |  Rn  |  Rd  |
 * +---+---+-------------+-----+---+------+---+-----+----+-----+------+------+
 */
static void disas_simd_tb(DisasContext *s, uint32_t insn)
{
    int op2 = extract32(insn, 22, 2);
    int is_q = extract32(insn, 30, 1);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    int is_tblx = extract32(insn, 12, 1);
    int len = extract32(insn, 13, 2);
    TCGv_i64 tcg_resl, tcg_resh, tcg_idx;
    TCGv_i32 tcg_regno, tcg_numregs;

    if (op2 != 0) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    /* This does a table lookup: for every byte element in the input
     * we index into a table formed from up to four vector registers,
     * and then the output is the result of the lookups. Our helper
     * function does the lookup operation for a single 64 bit part of
     * the input.
     */
    tcg_resl = tcg_temp_new_i64();
    tcg_resh = NULL;

    if (is_tblx) {
        read_vec_element(s, tcg_resl, rd, 0, MO_64);
    } else {
        tcg_gen_movi_i64(tcg_resl, 0);
    }

    if (is_q) {
        tcg_resh = tcg_temp_new_i64();
        if (is_tblx) {
            read_vec_element(s, tcg_resh, rd, 1, MO_64);
        } else {
            tcg_gen_movi_i64(tcg_resh, 0);
        }
    }

    tcg_idx = tcg_temp_new_i64();
    tcg_regno = tcg_const_i32(rn);
    tcg_numregs = tcg_const_i32(len + 1);
    read_vec_element(s, tcg_idx, rm, 0, MO_64);
    gen_helper_simd_tbl(tcg_resl, cpu_env, tcg_resl, tcg_idx,
                        tcg_regno, tcg_numregs);
    if (is_q) {
        read_vec_element(s, tcg_idx, rm, 1, MO_64);
        gen_helper_simd_tbl(tcg_resh, cpu_env, tcg_resh, tcg_idx,
                            tcg_regno, tcg_numregs);
    }
    tcg_temp_free_i64(tcg_idx);
    tcg_temp_free_i32(tcg_regno);
    tcg_temp_free_i32(tcg_numregs);

    write_vec_element(s, tcg_resl, rd, 0, MO_64);
    tcg_temp_free_i64(tcg_resl);

    if (is_q) {
        write_vec_element(s, tcg_resh, rd, 1, MO_64);
        tcg_temp_free_i64(tcg_resh);
    }
    clear_vec_high(s, is_q, rd);
}

/* ZIP/UZP/TRN
 *   31  30 29         24 23  22  21 20   16 15 14 12 11 10 9    5 4    0
 * +---+---+-------------+------+---+------+---+------------------+------+
 * | 0 | Q | 0 0 1 1 1 0 | size | 0 |  Rm  | 0 | opc | 1 0 |  Rn  |  Rd  |
 * +---+---+-------------+------+---+------+---+------------------+------+
 */
static void disas_simd_zip_trn(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 22, 2);
    /* opc field bits [1:0] indicate ZIP/UZP/TRN;
     * bit 2 indicates 1 vs 2 variant of the insn.
     */
    int opcode = extract32(insn, 12, 2);
    bool part = extract32(insn, 14, 1);
    bool is_q = extract32(insn, 30, 1);
    int esize = 8 << size;
    int i, ofs;
    int datasize = is_q ? 128 : 64;
    int elements = datasize / esize;
    TCGv_i64 tcg_res, tcg_resl, tcg_resh;

    if (opcode == 0 || (size == 3 && !is_q)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_resl = tcg_const_i64(0);
    tcg_resh = is_q ? tcg_const_i64(0) : NULL;
    tcg_res = tcg_temp_new_i64();

    for (i = 0; i < elements; i++) {
        switch (opcode) {
        case 1: /* UZP1/2 */
        {
            int midpoint = elements / 2;
            if (i < midpoint) {
                read_vec_element(s, tcg_res, rn, 2 * i + part, size);
            } else {
                read_vec_element(s, tcg_res, rm,
                                 2 * (i - midpoint) + part, size);
            }
            break;
        }
        case 2: /* TRN1/2 */
            if (i & 1) {
                read_vec_element(s, tcg_res, rm, (i & ~1) + part, size);
            } else {
                read_vec_element(s, tcg_res, rn, (i & ~1) + part, size);
            }
            break;
        case 3: /* ZIP1/2 */
        {
            int base = part * elements / 2;
            if (i & 1) {
                read_vec_element(s, tcg_res, rm, base + (i >> 1), size);
            } else {
                read_vec_element(s, tcg_res, rn, base + (i >> 1), size);
            }
            break;
        }
        default:
            g_assert_not_reached();
        }

        ofs = i * esize;
        if (ofs < 64) {
            tcg_gen_shli_i64(tcg_res, tcg_res, ofs);
            tcg_gen_or_i64(tcg_resl, tcg_resl, tcg_res);
        } else {
            tcg_gen_shli_i64(tcg_res, tcg_res, ofs - 64);
            tcg_gen_or_i64(tcg_resh, tcg_resh, tcg_res);
        }
    }

    tcg_temp_free_i64(tcg_res);

    write_vec_element(s, tcg_resl, rd, 0, MO_64);
    tcg_temp_free_i64(tcg_resl);

    if (is_q) {
        write_vec_element(s, tcg_resh, rd, 1, MO_64);
        tcg_temp_free_i64(tcg_resh);
    }
    clear_vec_high(s, is_q, rd);
}

/*
 * do_reduction_op helper
 *
 * This mirrors the Reduce() pseudocode in the ARM ARM. It is
 * important for correct NaN propagation that we do these
 * operations in exactly the order specified by the pseudocode.
 *
 * This is a recursive function, TCG temps should be freed by the
 * calling function once it is done with the values.
 */
static TCGv_i32 do_reduction_op(DisasContext *s, int fpopcode, int rn,
                                int esize, int size, int vmap, TCGv_ptr fpst)
{
    if (esize == size) {
        int element;
        MemOp msize = esize == 16 ? MO_16 : MO_32;
        TCGv_i32 tcg_elem;

        /* We should have one register left here */
        assert(ctpop8(vmap) == 1);
        element = ctz32(vmap);
        assert(element < 8);

        tcg_elem = tcg_temp_new_i32();
        read_vec_element_i32(s, tcg_elem, rn, element, msize);
        return tcg_elem;
    } else {
        int bits = size / 2;
        int shift = ctpop8(vmap) / 2;
        int vmap_lo = (vmap >> shift) & vmap;
        int vmap_hi = (vmap & ~vmap_lo);
        TCGv_i32 tcg_hi, tcg_lo, tcg_res;

        tcg_hi = do_reduction_op(s, fpopcode, rn, esize, bits, vmap_hi, fpst);
        tcg_lo = do_reduction_op(s, fpopcode, rn, esize, bits, vmap_lo, fpst);
        tcg_res = tcg_temp_new_i32();

        switch (fpopcode) {
        case 0x0c: /* fmaxnmv half-precision */
            gen_helper_advsimd_maxnumh(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x0f: /* fmaxv half-precision */
            gen_helper_advsimd_maxh(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x1c: /* fminnmv half-precision */
            gen_helper_advsimd_minnumh(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x1f: /* fminv half-precision */
            gen_helper_advsimd_minh(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x2c: /* fmaxnmv */
            gen_helper_vfp_maxnums(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x2f: /* fmaxv */
            gen_helper_vfp_maxs(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x3c: /* fminnmv */
            gen_helper_vfp_minnums(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        case 0x3f: /* fminv */
            gen_helper_vfp_mins(tcg_res, tcg_lo, tcg_hi, fpst);
            break;
        default:
            g_assert_not_reached();
        }

        tcg_temp_free_i32(tcg_hi);
        tcg_temp_free_i32(tcg_lo);
        return tcg_res;
    }
}

/* AdvSIMD across lanes
 *   31  30  29 28       24 23  22 21       17 16    12 11 10 9    5 4    0
 * +---+---+---+-----------+------+-----------+--------+-----+------+------+
 * | 0 | Q | U | 0 1 1 1 0 | size | 1 1 0 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+-----------+--------+-----+------+------+
 */
static void disas_simd_across_lanes(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 5);
    bool is_q = extract32(insn, 30, 1);
    bool is_u = extract32(insn, 29, 1);
    bool is_fp = false;
    bool is_min = false;
    int esize;
    int elements;
    int i;
    TCGv_i64 tcg_res, tcg_elt;

    switch (opcode) {
    case 0x1b: /* ADDV */
        if (is_u) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x3: /* SADDLV, UADDLV */
    case 0xa: /* SMAXV, UMAXV */
    case 0x1a: /* SMINV, UMINV */
        if (size == 3 || (size == 2 && !is_q)) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0xc: /* FMAXNMV, FMINNMV */
    case 0xf: /* FMAXV, FMINV */
        /* Bit 1 of size field encodes min vs max and the actual size
         * depends on the encoding of the U bit. If not set (and FP16
         * enabled) then we do half-precision float instead of single
         * precision.
         */
        is_min = extract32(size, 1, 1);
        is_fp = true;
        if (!is_u && dc_isar_feature(aa64_fp16, s)) {
            size = 1;
        } else if (!is_u || !is_q || extract32(size, 0, 1)) {
            unallocated_encoding(s);
            return;
        } else {
            size = 2;
        }
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    esize = 8 << size;
    elements = (is_q ? 128 : 64) / esize;

    tcg_res = tcg_temp_new_i64();
    tcg_elt = tcg_temp_new_i64();

    /* These instructions operate across all lanes of a vector
     * to produce a single result. We can guarantee that a 64
     * bit intermediate is sufficient:
     *  + for [US]ADDLV the maximum element size is 32 bits, and
     *    the result type is 64 bits
     *  + for FMAX*V, FMIN*V, ADDV the intermediate type is the
     *    same as the element size, which is 32 bits at most
     * For the integer operations we can choose to work at 64
     * or 32 bits and truncate at the end; for simplicity
     * we use 64 bits always. The floating point
     * ops do require 32 bit intermediates, though.
     */
    if (!is_fp) {
        read_vec_element(s, tcg_res, rn, 0, size | (is_u ? 0 : MO_SIGN));

        for (i = 1; i < elements; i++) {
            read_vec_element(s, tcg_elt, rn, i, size | (is_u ? 0 : MO_SIGN));

            switch (opcode) {
            case 0x03: /* SADDLV / UADDLV */
            case 0x1b: /* ADDV */
                tcg_gen_add_i64(tcg_res, tcg_res, tcg_elt);
                break;
            case 0x0a: /* SMAXV / UMAXV */
                if (is_u) {
                    tcg_gen_umax_i64(tcg_res, tcg_res, tcg_elt);
                } else {
                    tcg_gen_smax_i64(tcg_res, tcg_res, tcg_elt);
                }
                break;
            case 0x1a: /* SMINV / UMINV */
                if (is_u) {
                    tcg_gen_umin_i64(tcg_res, tcg_res, tcg_elt);
                } else {
                    tcg_gen_smin_i64(tcg_res, tcg_res, tcg_elt);
                }
                break;
            default:
                g_assert_not_reached();
            }

        }
    } else {
        /* Floating point vector reduction ops which work across 32
         * bit (single) or 16 bit (half-precision) intermediates.
         * Note that correct NaN propagation requires that we do these
         * operations in exactly the order specified by the pseudocode.
         */
        TCGv_ptr fpst = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        int fpopcode = opcode | is_min << 4 | is_u << 5;
        int vmap = (1 << elements) - 1;
        TCGv_i32 tcg_res32 = do_reduction_op(s, fpopcode, rn, esize,
                                             (is_q ? 128 : 64), vmap, fpst);
        tcg_gen_extu_i32_i64(tcg_res, tcg_res32);
        tcg_temp_free_i32(tcg_res32);
        tcg_temp_free_ptr(fpst);
    }

    tcg_temp_free_i64(tcg_elt);

    /* Now truncate the result to the width required for the final output */
    if (opcode == 0x03) {
        /* SADDLV, UADDLV: result is 2*esize */
        size++;
    }

    switch (size) {
    case 0:
        tcg_gen_ext8u_i64(tcg_res, tcg_res);
        break;
    case 1:
        tcg_gen_ext16u_i64(tcg_res, tcg_res);
        break;
    case 2:
        tcg_gen_ext32u_i64(tcg_res, tcg_res);
        break;
    case 3:
        break;
    default:
        g_assert_not_reached();
    }

    write_fp_dreg(s, rd, tcg_res);
    tcg_temp_free_i64(tcg_res);
}

/* DUP (Element, Vector)
 *
 *  31  30   29              21 20    16 15        10  9    5 4    0
 * +---+---+-------------------+--------+-------------+------+------+
 * | 0 | Q | 0 0 1 1 1 0 0 0 0 |  imm5  | 0 0 0 0 0 1 |  Rn  |  Rd  |
 * +---+---+-------------------+--------+-------------+------+------+
 *
 * size: encoded in imm5 (see ARM ARM LowestSetBit())
 */
static void handle_simd_dupe(DisasContext *s, int is_q, int rd, int rn,
                             int imm5)
{
    int size = ctz32(imm5);
    int index;

    if (size > 3 || (size == 3 && !is_q)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    index = imm5 >> (size + 1);
    tcg_gen_gvec_dup_mem(size, vec_full_reg_offset(s, rd),
                         vec_reg_offset(s, rn, index, size),
                         is_q ? 16 : 8, vec_full_reg_size(s));
}

/* DUP (element, scalar)
 *  31                   21 20    16 15        10  9    5 4    0
 * +-----------------------+--------+-------------+------+------+
 * | 0 1 0 1 1 1 1 0 0 0 0 |  imm5  | 0 0 0 0 0 1 |  Rn  |  Rd  |
 * +-----------------------+--------+-------------+------+------+
 */
static void handle_simd_dupes(DisasContext *s, int rd, int rn,
                              int imm5)
{
    int size = ctz32(imm5);
    int index;
    TCGv_i64 tmp;

    if (size > 3) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    index = imm5 >> (size + 1);

    /* This instruction just extracts the specified element and
     * zero-extends it into the bottom of the destination register.
     */
    tmp = tcg_temp_new_i64();
    read_vec_element(s, tmp, rn, index, size);
    write_fp_dreg(s, rd, tmp);
    tcg_temp_free_i64(tmp);
}

/* DUP (General)
 *
 *  31  30   29              21 20    16 15        10  9    5 4    0
 * +---+---+-------------------+--------+-------------+------+------+
 * | 0 | Q | 0 0 1 1 1 0 0 0 0 |  imm5  | 0 0 0 0 1 1 |  Rn  |  Rd  |
 * +---+---+-------------------+--------+-------------+------+------+
 *
 * size: encoded in imm5 (see ARM ARM LowestSetBit())
 */
static void handle_simd_dupg(DisasContext *s, int is_q, int rd, int rn,
                             int imm5)
{
    int size = ctz32(imm5);
    uint32_t dofs, oprsz, maxsz;

    if (size > 3 || ((size == 3) && !is_q)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    dofs = vec_full_reg_offset(s, rd);
    oprsz = is_q ? 16 : 8;
    maxsz = vec_full_reg_size(s);

    tcg_gen_gvec_dup_i64(size, dofs, oprsz, maxsz, cpu_reg(s, rn));
}

/* INS (Element)
 *
 *  31                   21 20    16 15  14    11  10 9    5 4    0
 * +-----------------------+--------+------------+---+------+------+
 * | 0 1 1 0 1 1 1 0 0 0 0 |  imm5  | 0 |  imm4  | 1 |  Rn  |  Rd  |
 * +-----------------------+--------+------------+---+------+------+
 *
 * size: encoded in imm5 (see ARM ARM LowestSetBit())
 * index: encoded in imm5<4:size+1>
 */
static void handle_simd_inse(DisasContext *s, int rd, int rn,
                             int imm4, int imm5)
{
    int size = ctz32(imm5);
    int src_index, dst_index;
    TCGv_i64 tmp;

    if (size > 3) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    dst_index = extract32(imm5, 1+size, 5);
    src_index = extract32(imm4, size, 4);

    tmp = tcg_temp_new_i64();

    read_vec_element(s, tmp, rn, src_index, size);
    write_vec_element(s, tmp, rd, dst_index, size);

    tcg_temp_free_i64(tmp);

    /* INS is considered a 128-bit write for SVE. */
    clear_vec_high(s, true, rd);
}


/* INS (General)
 *
 *  31                   21 20    16 15        10  9    5 4    0
 * +-----------------------+--------+-------------+------+------+
 * | 0 1 0 0 1 1 1 0 0 0 0 |  imm5  | 0 0 0 1 1 1 |  Rn  |  Rd  |
 * +-----------------------+--------+-------------+------+------+
 *
 * size: encoded in imm5 (see ARM ARM LowestSetBit())
 * index: encoded in imm5<4:size+1>
 */
static void handle_simd_insg(DisasContext *s, int rd, int rn, int imm5)
{
    int size = ctz32(imm5);
    int idx;

    if (size > 3) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    idx = extract32(imm5, 1 + size, 4 - size);
    write_vec_element(s, cpu_reg(s, rn), rd, idx, size);

    /* INS is considered a 128-bit write for SVE. */
    clear_vec_high(s, true, rd);
}

/*
 * UMOV (General)
 * SMOV (General)
 *
 *  31  30   29              21 20    16 15    12   10 9    5 4    0
 * +---+---+-------------------+--------+-------------+------+------+
 * | 0 | Q | 0 0 1 1 1 0 0 0 0 |  imm5  | 0 0 1 U 1 1 |  Rn  |  Rd  |
 * +---+---+-------------------+--------+-------------+------+------+
 *
 * U: unsigned when set
 * size: encoded in imm5 (see ARM ARM LowestSetBit())
 */
static void handle_simd_umov_smov(DisasContext *s, int is_q, int is_signed,
                                  int rn, int rd, int imm5)
{
    int size = ctz32(imm5);
    int element;
    TCGv_i64 tcg_rd;

    /* Check for UnallocatedEncodings */
    if (is_signed) {
        if (size > 2 || (size == 2 && !is_q)) {
            unallocated_encoding(s);
            return;
        }
    } else {
        if (size > 3
            || (size < 3 && is_q)
            || (size == 3 && !is_q)) {
            unallocated_encoding(s);
            return;
        }
    }

    if (!fp_access_check(s)) {
        return;
    }

    element = extract32(imm5, 1+size, 4);

    tcg_rd = cpu_reg(s, rd);
    read_vec_element(s, tcg_rd, rn, element, size | (is_signed ? MO_SIGN : 0));
    if (is_signed && !is_q) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
}

/* AdvSIMD copy
 *   31  30  29  28             21 20  16 15  14  11 10  9    5 4    0
 * +---+---+----+-----------------+------+---+------+---+------+------+
 * | 0 | Q | op | 0 1 1 1 0 0 0 0 | imm5 | 0 | imm4 | 1 |  Rn  |  Rd  |
 * +---+---+----+-----------------+------+---+------+---+------+------+
 */
static void disas_simd_copy(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int imm4 = extract32(insn, 11, 4);
    int op = extract32(insn, 29, 1);
    int is_q = extract32(insn, 30, 1);
    int imm5 = extract32(insn, 16, 5);

    if (op) {
        if (is_q) {
            /* INS (element) */
            handle_simd_inse(s, rd, rn, imm4, imm5);
        } else {
            unallocated_encoding(s);
        }
    } else {
        switch (imm4) {
        case 0:
            /* DUP (element - vector) */
            handle_simd_dupe(s, is_q, rd, rn, imm5);
            break;
        case 1:
            /* DUP (general) */
            handle_simd_dupg(s, is_q, rd, rn, imm5);
            break;
        case 3:
            if (is_q) {
                /* INS (general) */
                handle_simd_insg(s, rd, rn, imm5);
            } else {
                unallocated_encoding(s);
            }
            break;
        case 5:
        case 7:
            /* UMOV/SMOV (is_q indicates 32/64; imm4 indicates signedness) */
            handle_simd_umov_smov(s, is_q, (imm4 == 5), rn, rd, imm5);
            break;
        default:
            unallocated_encoding(s);
            break;
        }
    }
}

/* AdvSIMD modified immediate
 *  31  30   29  28                 19 18 16 15   12  11  10  9     5 4    0
 * +---+---+----+---------------------+-----+-------+----+---+-------+------+
 * | 0 | Q | op | 0 1 1 1 1 0 0 0 0 0 | abc | cmode | o2 | 1 | defgh |  Rd  |
 * +---+---+----+---------------------+-----+-------+----+---+-------+------+
 *
 * There are a number of operations that can be carried out here:
 *   MOVI - move (shifted) imm into register
 *   MVNI - move inverted (shifted) imm into register
 *   ORR  - bitwise OR of (shifted) imm with register
 *   BIC  - bitwise clear of (shifted) imm with register
 * With ARMv8.2 we also have:
 *   FMOV half-precision
 */
static void disas_simd_mod_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int cmode = extract32(insn, 12, 4);
    int cmode_3_1 = extract32(cmode, 1, 3);
    int cmode_0 = extract32(cmode, 0, 1);
    int o2 = extract32(insn, 11, 1);
    uint64_t abcdefgh = extract32(insn, 5, 5) | (extract32(insn, 16, 3) << 5);
    bool is_neg = extract32(insn, 29, 1);
    bool is_q = extract32(insn, 30, 1);
    uint64_t imm = 0;

    if (o2 != 0 || ((cmode == 0xf) && is_neg && !is_q)) {
        /* Check for FMOV (vector, immediate) - half-precision */
        if (!(dc_isar_feature(aa64_fp16, s) && o2 && cmode == 0xf)) {
            unallocated_encoding(s);
            return;
        }
    }

    if (!fp_access_check(s)) {
        return;
    }

    /* See AdvSIMDExpandImm() in ARM ARM */
    switch (cmode_3_1) {
    case 0: /* Replicate(Zeros(24):imm8, 2) */
    case 1: /* Replicate(Zeros(16):imm8:Zeros(8), 2) */
    case 2: /* Replicate(Zeros(8):imm8:Zeros(16), 2) */
    case 3: /* Replicate(imm8:Zeros(24), 2) */
    {
        int shift = cmode_3_1 * 8;
        imm = bitfield_replicate(abcdefgh << shift, 32);
        break;
    }
    case 4: /* Replicate(Zeros(8):imm8, 4) */
    case 5: /* Replicate(imm8:Zeros(8), 4) */
    {
        int shift = (cmode_3_1 & 0x1) * 8;
        imm = bitfield_replicate(abcdefgh << shift, 16);
        break;
    }
    case 6:
        if (cmode_0) {
            /* Replicate(Zeros(8):imm8:Ones(16), 2) */
            imm = (abcdefgh << 16) | 0xffff;
        } else {
            /* Replicate(Zeros(16):imm8:Ones(8), 2) */
            imm = (abcdefgh << 8) | 0xff;
        }
        imm = bitfield_replicate(imm, 32);
        break;
    case 7:
        if (!cmode_0 && !is_neg) {
            imm = bitfield_replicate(abcdefgh, 8);
        } else if (!cmode_0 && is_neg) {
            int i;
            imm = 0;
            for (i = 0; i < 8; i++) {
                if ((abcdefgh) & (1 << i)) {
                    imm |= 0xffULL << (i * 8);
                }
            }
        } else if (cmode_0) {
            if (is_neg) {
                imm = (abcdefgh & 0x3f) << 48;
                if (abcdefgh & 0x80) {
                    imm |= 0x8000000000000000ULL;
                }
                if (abcdefgh & 0x40) {
                    imm |= 0x3fc0000000000000ULL;
                } else {
                    imm |= 0x4000000000000000ULL;
                }
            } else {
                if (o2) {
                    /* FMOV (vector, immediate) - half-precision */
                    imm = vfp_expand_imm(MO_16, abcdefgh);
                    /* now duplicate across the lanes */
                    imm = bitfield_replicate(imm, 16);
                } else {
                    imm = (abcdefgh & 0x3f) << 19;
                    if (abcdefgh & 0x80) {
                        imm |= 0x80000000;
                    }
                    if (abcdefgh & 0x40) {
                        imm |= 0x3e000000;
                    } else {
                        imm |= 0x40000000;
                    }
                    imm |= (imm << 32);
                }
            }
        }
        break;
    default:
        fprintf(stderr, "%s: cmode_3_1: %x\n", __func__, cmode_3_1);
        g_assert_not_reached();
    }

    if (cmode_3_1 != 7 && is_neg) {
        imm = ~imm;
    }

    if (!((cmode & 0x9) == 0x1 || (cmode & 0xd) == 0x9)) {
        /* MOVI or MVNI, with MVNI negation handled above.  */
        tcg_gen_gvec_dup_imm(MO_64, vec_full_reg_offset(s, rd), is_q ? 16 : 8,
                             vec_full_reg_size(s), imm);
    } else {
        /* ORR or BIC, with BIC negation to AND handled above.  */
        if (is_neg) {
            gen_gvec_fn2i(s, is_q, rd, rd, imm, tcg_gen_gvec_andi, MO_64);
        } else {
            gen_gvec_fn2i(s, is_q, rd, rd, imm, tcg_gen_gvec_ori, MO_64);
        }
    }
}

/* AdvSIMD scalar copy
 *  31 30  29  28             21 20  16 15  14  11 10  9    5 4    0
 * +-----+----+-----------------+------+---+------+---+------+------+
 * | 0 1 | op | 1 1 1 1 0 0 0 0 | imm5 | 0 | imm4 | 1 |  Rn  |  Rd  |
 * +-----+----+-----------------+------+---+------+---+------+------+
 */
static void disas_simd_scalar_copy(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int imm4 = extract32(insn, 11, 4);
    int imm5 = extract32(insn, 16, 5);
    int op = extract32(insn, 29, 1);

    if (op != 0 || imm4 != 0) {
        unallocated_encoding(s);
        return;
    }

    /* DUP (element, scalar) */
    handle_simd_dupes(s, rd, rn, imm5);
}

/* AdvSIMD scalar pairwise
 *  31 30  29 28       24 23  22 21       17 16    12 11 10 9    5 4    0
 * +-----+---+-----------+------+-----------+--------+-----+------+------+
 * | 0 1 | U | 1 1 1 1 0 | size | 1 1 0 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +-----+---+-----------+------+-----------+--------+-----+------+------+
 */
static void disas_simd_scalar_pairwise(DisasContext *s, uint32_t insn)
{
    int u = extract32(insn, 29, 1);
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    TCGv_ptr fpst;

    /* For some ops (the FP ones), size[1] is part of the encoding.
     * For ADDP strictly it is not but size[1] is always 1 for valid
     * encodings.
     */
    opcode |= (extract32(size, 1, 1) << 5);

    switch (opcode) {
    case 0x3b: /* ADDP */
        if (u || size != 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }

        fpst = NULL;
        break;
    case 0xc: /* FMAXNMP */
    case 0xd: /* FADDP */
    case 0xf: /* FMAXP */
    case 0x2c: /* FMINNMP */
    case 0x2f: /* FMINP */
        /* FP op, size[0] is 32 or 64 bit*/
        if (!u) {
            if (!dc_isar_feature(aa64_fp16, s)) {
                unallocated_encoding(s);
                return;
            } else {
                size = MO_16;
            }
        } else {
            size = extract32(size, 0, 1) ? MO_64 : MO_32;
        }

        if (!fp_access_check(s)) {
            return;
        }

        fpst = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (size == MO_64) {
        TCGv_i64 tcg_op1 = tcg_temp_new_i64();
        TCGv_i64 tcg_op2 = tcg_temp_new_i64();
        TCGv_i64 tcg_res = tcg_temp_new_i64();

        read_vec_element(s, tcg_op1, rn, 0, MO_64);
        read_vec_element(s, tcg_op2, rn, 1, MO_64);

        switch (opcode) {
        case 0x3b: /* ADDP */
            tcg_gen_add_i64(tcg_res, tcg_op1, tcg_op2);
            break;
        case 0xc: /* FMAXNMP */
            gen_helper_vfp_maxnumd(tcg_res, tcg_op1, tcg_op2, fpst);
            break;
        case 0xd: /* FADDP */
            gen_helper_vfp_addd(tcg_res, tcg_op1, tcg_op2, fpst);
            break;
        case 0xf: /* FMAXP */
            gen_helper_vfp_maxd(tcg_res, tcg_op1, tcg_op2, fpst);
            break;
        case 0x2c: /* FMINNMP */
            gen_helper_vfp_minnumd(tcg_res, tcg_op1, tcg_op2, fpst);
            break;
        case 0x2f: /* FMINP */
            gen_helper_vfp_mind(tcg_res, tcg_op1, tcg_op2, fpst);
            break;
        default:
            g_assert_not_reached();
        }

        write_fp_dreg(s, rd, tcg_res);

        tcg_temp_free_i64(tcg_op1);
        tcg_temp_free_i64(tcg_op2);
        tcg_temp_free_i64(tcg_res);
    } else {
        TCGv_i32 tcg_op1 = tcg_temp_new_i32();
        TCGv_i32 tcg_op2 = tcg_temp_new_i32();
        TCGv_i32 tcg_res = tcg_temp_new_i32();

        read_vec_element_i32(s, tcg_op1, rn, 0, size);
        read_vec_element_i32(s, tcg_op2, rn, 1, size);

        if (size == MO_16) {
            switch (opcode) {
            case 0xc: /* FMAXNMP */
                gen_helper_advsimd_maxnumh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0xd: /* FADDP */
                gen_helper_advsimd_addh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0xf: /* FMAXP */
                gen_helper_advsimd_maxh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x2c: /* FMINNMP */
                gen_helper_advsimd_minnumh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x2f: /* FMINP */
                gen_helper_advsimd_minh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            switch (opcode) {
            case 0xc: /* FMAXNMP */
                gen_helper_vfp_maxnums(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0xd: /* FADDP */
                gen_helper_vfp_adds(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0xf: /* FMAXP */
                gen_helper_vfp_maxs(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x2c: /* FMINNMP */
                gen_helper_vfp_minnums(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x2f: /* FMINP */
                gen_helper_vfp_mins(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }
        }

        write_fp_sreg(s, rd, tcg_res);

        tcg_temp_free_i32(tcg_op1);
        tcg_temp_free_i32(tcg_op2);
        tcg_temp_free_i32(tcg_res);
    }

    if (fpst) {
        tcg_temp_free_ptr(fpst);
    }
}

/*
 * Common SSHR[RA]/USHR[RA] - Shift right (optional rounding/accumulate)
 *
 * This code is handles the common shifting code and is used by both
 * the vector and scalar code.
 */
static void handle_shri_with_rndacc(TCGv_i64 tcg_res, TCGv_i64 tcg_src,
                                    TCGv_i64 tcg_rnd, bool accumulate,
                                    bool is_u, int size, int shift)
{
    bool extended_result = false;
    bool round = tcg_rnd != NULL;
    int ext_lshift = 0;
    TCGv_i64 tcg_src_hi;

    if (round && size == 3) {
        extended_result = true;
        ext_lshift = 64 - shift;
        tcg_src_hi = tcg_temp_new_i64();
    } else if (shift == 64) {
        if (!accumulate && is_u) {
            /* result is zero */
            tcg_gen_movi_i64(tcg_res, 0);
            return;
        }
    }

    /* Deal with the rounding step */
    if (round) {
        if (extended_result) {
            TCGv_i64 tcg_zero = tcg_const_i64(0);
            if (!is_u) {
                /* take care of sign extending tcg_res */
                tcg_gen_sari_i64(tcg_src_hi, tcg_src, 63);
                tcg_gen_add2_i64(tcg_src, tcg_src_hi,
                                 tcg_src, tcg_src_hi,
                                 tcg_rnd, tcg_zero);
            } else {
                tcg_gen_add2_i64(tcg_src, tcg_src_hi,
                                 tcg_src, tcg_zero,
                                 tcg_rnd, tcg_zero);
            }
            tcg_temp_free_i64(tcg_zero);
        } else {
            tcg_gen_add_i64(tcg_src, tcg_src, tcg_rnd);
        }
    }

    /* Now do the shift right */
    if (round && extended_result) {
        /* extended case, >64 bit precision required */
        if (ext_lshift == 0) {
            /* special case, only high bits matter */
            tcg_gen_mov_i64(tcg_src, tcg_src_hi);
        } else {
            tcg_gen_shri_i64(tcg_src, tcg_src, shift);
            tcg_gen_shli_i64(tcg_src_hi, tcg_src_hi, ext_lshift);
            tcg_gen_or_i64(tcg_src, tcg_src, tcg_src_hi);
        }
    } else {
        if (is_u) {
            if (shift == 64) {
                /* essentially shifting in 64 zeros */
                tcg_gen_movi_i64(tcg_src, 0);
            } else {
                tcg_gen_shri_i64(tcg_src, tcg_src, shift);
            }
        } else {
            if (shift == 64) {
                /* effectively extending the sign-bit */
                tcg_gen_sari_i64(tcg_src, tcg_src, 63);
            } else {
                tcg_gen_sari_i64(tcg_src, tcg_src, shift);
            }
        }
    }

    if (accumulate) {
        tcg_gen_add_i64(tcg_res, tcg_res, tcg_src);
    } else {
        tcg_gen_mov_i64(tcg_res, tcg_src);
    }

    if (extended_result) {
        tcg_temp_free_i64(tcg_src_hi);
    }
}

/* SSHR[RA]/USHR[RA] - Scalar shift right (optional rounding/accumulate) */
static void handle_scalar_simd_shri(DisasContext *s,
                                    bool is_u, int immh, int immb,
                                    int opcode, int rn, int rd)
{
    const int size = 3;
    int immhb = immh << 3 | immb;
    int shift = 2 * (8 << size) - immhb;
    bool accumulate = false;
    bool round = false;
    bool insert = false;
    TCGv_i64 tcg_rn;
    TCGv_i64 tcg_rd;
    TCGv_i64 tcg_round;

    if (!extract32(immh, 3, 1)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    switch (opcode) {
    case 0x02: /* SSRA / USRA (accumulate) */
        accumulate = true;
        break;
    case 0x04: /* SRSHR / URSHR (rounding) */
        round = true;
        break;
    case 0x06: /* SRSRA / URSRA (accum + rounding) */
        accumulate = round = true;
        break;
    case 0x08: /* SRI */
        insert = true;
        break;
    }

    if (round) {
        uint64_t round_const = 1ULL << (shift - 1);
        tcg_round = tcg_const_i64(round_const);
    } else {
        tcg_round = NULL;
    }

    tcg_rn = read_fp_dreg(s, rn);
    tcg_rd = (accumulate || insert) ? read_fp_dreg(s, rd) : tcg_temp_new_i64();

    if (insert) {
        /* shift count same as element size is valid but does nothing;
         * special case to avoid potential shift by 64.
         */
        int esize = 8 << size;
        if (shift != esize) {
            tcg_gen_shri_i64(tcg_rn, tcg_rn, shift);
            tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_rn, 0, esize - shift);
        }
    } else {
        handle_shri_with_rndacc(tcg_rd, tcg_rn, tcg_round,
                                accumulate, is_u, size, shift);
    }

    write_fp_dreg(s, rd, tcg_rd);

    tcg_temp_free_i64(tcg_rn);
    tcg_temp_free_i64(tcg_rd);
    if (round) {
        tcg_temp_free_i64(tcg_round);
    }
}

/* SHL/SLI - Scalar shift left */
static void handle_scalar_simd_shli(DisasContext *s, bool insert,
                                    int immh, int immb, int opcode,
                                    int rn, int rd)
{
    int size = 32 - clz32(immh) - 1;
    int immhb = immh << 3 | immb;
    int shift = immhb - (8 << size);
    TCGv_i64 tcg_rn;
    TCGv_i64 tcg_rd;

    if (!extract32(immh, 3, 1)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_rn = read_fp_dreg(s, rn);
    tcg_rd = insert ? read_fp_dreg(s, rd) : tcg_temp_new_i64();

    if (insert) {
        tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_rn, shift, 64 - shift);
    } else {
        tcg_gen_shli_i64(tcg_rd, tcg_rn, shift);
    }

    write_fp_dreg(s, rd, tcg_rd);

    tcg_temp_free_i64(tcg_rn);
    tcg_temp_free_i64(tcg_rd);
}

/* SQSHRN/SQSHRUN - Saturating (signed/unsigned) shift right with
 * (signed/unsigned) narrowing */
static void handle_vec_simd_sqshrn(DisasContext *s, bool is_scalar, bool is_q,
                                   bool is_u_shift, bool is_u_narrow,
                                   int immh, int immb, int opcode,
                                   int rn, int rd)
{
    int immhb = immh << 3 | immb;
    int size = 32 - clz32(immh) - 1;
    int esize = 8 << size;
    int shift = (2 * esize) - immhb;
    int elements = is_scalar ? 1 : (64 / esize);
    bool round = extract32(opcode, 0, 1);
    MemOp ldop = (size + 1) | (is_u_shift ? 0 : MO_SIGN);
    TCGv_i64 tcg_rn, tcg_rd, tcg_round;
    TCGv_i32 tcg_rd_narrowed;
    TCGv_i64 tcg_final;

    static NeonGenNarrowEnvFn * const signed_narrow_fns[4][2] = {
        { gen_helper_neon_narrow_sat_s8,
          gen_helper_neon_unarrow_sat8 },
        { gen_helper_neon_narrow_sat_s16,
          gen_helper_neon_unarrow_sat16 },
        { gen_helper_neon_narrow_sat_s32,
          gen_helper_neon_unarrow_sat32 },
        { NULL, NULL },
    };
    static NeonGenNarrowEnvFn * const unsigned_narrow_fns[4] = {
        gen_helper_neon_narrow_sat_u8,
        gen_helper_neon_narrow_sat_u16,
        gen_helper_neon_narrow_sat_u32,
        NULL
    };
    NeonGenNarrowEnvFn *narrowfn;

    int i;

    assert(size < 4);

    if (extract32(immh, 3, 1)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (is_u_shift) {
        narrowfn = unsigned_narrow_fns[size];
    } else {
        narrowfn = signed_narrow_fns[size][is_u_narrow ? 1 : 0];
    }

    tcg_rn = tcg_temp_new_i64();
    tcg_rd = tcg_temp_new_i64();
    tcg_rd_narrowed = tcg_temp_new_i32();
    tcg_final = tcg_const_i64(0);

    if (round) {
        uint64_t round_const = 1ULL << (shift - 1);
        tcg_round = tcg_const_i64(round_const);
    } else {
        tcg_round = NULL;
    }

    for (i = 0; i < elements; i++) {
        read_vec_element(s, tcg_rn, rn, i, ldop);
        handle_shri_with_rndacc(tcg_rd, tcg_rn, tcg_round,
                                false, is_u_shift, size+1, shift);
        narrowfn(tcg_rd_narrowed, cpu_env, tcg_rd);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_rd_narrowed);
        tcg_gen_deposit_i64(tcg_final, tcg_final, tcg_rd, esize * i, esize);
    }

    if (!is_q) {
        write_vec_element(s, tcg_final, rd, 0, MO_64);
    } else {
        write_vec_element(s, tcg_final, rd, 1, MO_64);
    }

    if (round) {
        tcg_temp_free_i64(tcg_round);
    }
    tcg_temp_free_i64(tcg_rn);
    tcg_temp_free_i64(tcg_rd);
    tcg_temp_free_i32(tcg_rd_narrowed);
    tcg_temp_free_i64(tcg_final);

    clear_vec_high(s, is_q, rd);
}

/* SQSHLU, UQSHL, SQSHL: saturating left shifts */
static void handle_simd_qshl(DisasContext *s, bool scalar, bool is_q,
                             bool src_unsigned, bool dst_unsigned,
                             int immh, int immb, int rn, int rd)
{
    int immhb = immh << 3 | immb;
    int size = 32 - clz32(immh) - 1;
    int shift = immhb - (8 << size);
    int pass;

    assert(immh != 0);
    assert(!(scalar && is_q));

    if (!scalar) {
        if (!is_q && extract32(immh, 3, 1)) {
            unallocated_encoding(s);
            return;
        }

        /* Since we use the variable-shift helpers we must
         * replicate the shift count into each element of
         * the tcg_shift value.
         */
        switch (size) {
        case 0:
            shift |= shift << 8;
            /* fall through */
        case 1:
            shift |= shift << 16;
            break;
        case 2:
        case 3:
            break;
        default:
            g_assert_not_reached();
        }
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (size == 3) {
        TCGv_i64 tcg_shift = tcg_const_i64(shift);
        static NeonGenTwo64OpEnvFn * const fns[2][2] = {
            { gen_helper_neon_qshl_s64, gen_helper_neon_qshlu_s64 },
            { NULL, gen_helper_neon_qshl_u64 },
        };
        NeonGenTwo64OpEnvFn *genfn = fns[src_unsigned][dst_unsigned];
        int maxpass = is_q ? 2 : 1;

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i64 tcg_op = tcg_temp_new_i64();

            read_vec_element(s, tcg_op, rn, pass, MO_64);
            genfn(tcg_op, cpu_env, tcg_op, tcg_shift);
            write_vec_element(s, tcg_op, rd, pass, MO_64);

            tcg_temp_free_i64(tcg_op);
        }
        tcg_temp_free_i64(tcg_shift);
        clear_vec_high(s, is_q, rd);
    } else {
        TCGv_i32 tcg_shift = tcg_const_i32(shift);
        static NeonGenTwoOpEnvFn * const fns[2][2][3] = {
            {
                { gen_helper_neon_qshl_s8,
                  gen_helper_neon_qshl_s16,
                  gen_helper_neon_qshl_s32 },
                { gen_helper_neon_qshlu_s8,
                  gen_helper_neon_qshlu_s16,
                  gen_helper_neon_qshlu_s32 }
            }, {
                { NULL, NULL, NULL },
                { gen_helper_neon_qshl_u8,
                  gen_helper_neon_qshl_u16,
                  gen_helper_neon_qshl_u32 }
            }
        };
        NeonGenTwoOpEnvFn *genfn = fns[src_unsigned][dst_unsigned][size];
        MemOp memop = scalar ? size : MO_32;
        int maxpass = scalar ? 1 : is_q ? 4 : 2;

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i32 tcg_op = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op, rn, pass, memop);
            genfn(tcg_op, cpu_env, tcg_op, tcg_shift);
            if (scalar) {
                switch (size) {
                case 0:
                    tcg_gen_ext8u_i32(tcg_op, tcg_op);
                    break;
                case 1:
                    tcg_gen_ext16u_i32(tcg_op, tcg_op);
                    break;
                case 2:
                    break;
                default:
                    g_assert_not_reached();
                }
                write_fp_sreg(s, rd, tcg_op);
            } else {
                write_vec_element_i32(s, tcg_op, rd, pass, MO_32);
            }

            tcg_temp_free_i32(tcg_op);
        }
        tcg_temp_free_i32(tcg_shift);

        if (!scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }
}

/* Common vector code for handling integer to FP conversion */
static void handle_simd_intfp_conv(DisasContext *s, int rd, int rn,
                                   int elements, int is_signed,
                                   int fracbits, int size)
{
    TCGv_ptr tcg_fpst = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
    TCGv_i32 tcg_shift = NULL;

    MemOp mop = size | (is_signed ? MO_SIGN : 0);
    int pass;

    if (fracbits || size == MO_64) {
        tcg_shift = tcg_const_i32(fracbits);
    }

    if (size == MO_64) {
        TCGv_i64 tcg_int64 = tcg_temp_new_i64();
        TCGv_i64 tcg_double = tcg_temp_new_i64();

        for (pass = 0; pass < elements; pass++) {
            read_vec_element(s, tcg_int64, rn, pass, mop);

            if (is_signed) {
                gen_helper_vfp_sqtod(tcg_double, tcg_int64,
                                     tcg_shift, tcg_fpst);
            } else {
                gen_helper_vfp_uqtod(tcg_double, tcg_int64,
                                     tcg_shift, tcg_fpst);
            }
            if (elements == 1) {
                write_fp_dreg(s, rd, tcg_double);
            } else {
                write_vec_element(s, tcg_double, rd, pass, MO_64);
            }
        }

        tcg_temp_free_i64(tcg_int64);
        tcg_temp_free_i64(tcg_double);

    } else {
        TCGv_i32 tcg_int32 = tcg_temp_new_i32();
        TCGv_i32 tcg_float = tcg_temp_new_i32();

        for (pass = 0; pass < elements; pass++) {
            read_vec_element_i32(s, tcg_int32, rn, pass, mop);

            switch (size) {
            case MO_32:
                if (fracbits) {
                    if (is_signed) {
                        gen_helper_vfp_sltos(tcg_float, tcg_int32,
                                             tcg_shift, tcg_fpst);
                    } else {
                        gen_helper_vfp_ultos(tcg_float, tcg_int32,
                                             tcg_shift, tcg_fpst);
                    }
                } else {
                    if (is_signed) {
                        gen_helper_vfp_sitos(tcg_float, tcg_int32, tcg_fpst);
                    } else {
                        gen_helper_vfp_uitos(tcg_float, tcg_int32, tcg_fpst);
                    }
                }
                break;
            case MO_16:
                if (fracbits) {
                    if (is_signed) {
                        gen_helper_vfp_sltoh(tcg_float, tcg_int32,
                                             tcg_shift, tcg_fpst);
                    } else {
                        gen_helper_vfp_ultoh(tcg_float, tcg_int32,
                                             tcg_shift, tcg_fpst);
                    }
                } else {
                    if (is_signed) {
                        gen_helper_vfp_sitoh(tcg_float, tcg_int32, tcg_fpst);
                    } else {
                        gen_helper_vfp_uitoh(tcg_float, tcg_int32, tcg_fpst);
                    }
                }
                break;
            default:
                g_assert_not_reached();
            }

            if (elements == 1) {
                write_fp_sreg(s, rd, tcg_float);
            } else {
                write_vec_element_i32(s, tcg_float, rd, pass, size);
            }
        }

        tcg_temp_free_i32(tcg_int32);
        tcg_temp_free_i32(tcg_float);
    }

    tcg_temp_free_ptr(tcg_fpst);
    if (tcg_shift) {
        tcg_temp_free_i32(tcg_shift);
    }

    clear_vec_high(s, elements << size == 16, rd);
}

/* UCVTF/SCVTF - Integer to FP conversion */
static void handle_simd_shift_intfp_conv(DisasContext *s, bool is_scalar,
                                         bool is_q, bool is_u,
                                         int immh, int immb, int opcode,
                                         int rn, int rd)
{
    int size, elements, fracbits;
    int immhb = immh << 3 | immb;

    if (immh & 8) {
        size = MO_64;
        if (!is_scalar && !is_q) {
            unallocated_encoding(s);
            return;
        }
    } else if (immh & 4) {
        size = MO_32;
    } else if (immh & 2) {
        size = MO_16;
        if (!dc_isar_feature(aa64_fp16, s)) {
            unallocated_encoding(s);
            return;
        }
    } else {
        /* immh == 0 would be a failure of the decode logic */
        g_assert(immh == 1);
        unallocated_encoding(s);
        return;
    }

    if (is_scalar) {
        elements = 1;
    } else {
        elements = (8 << is_q) >> size;
    }
    fracbits = (16 << size) - immhb;

    if (!fp_access_check(s)) {
        return;
    }

    handle_simd_intfp_conv(s, rd, rn, elements, !is_u, fracbits, size);
}

/* FCVTZS, FVCVTZU - FP to fixedpoint conversion */
static void handle_simd_shift_fpint_conv(DisasContext *s, bool is_scalar,
                                         bool is_q, bool is_u,
                                         int immh, int immb, int rn, int rd)
{
    int immhb = immh << 3 | immb;
    int pass, size, fracbits;
    TCGv_ptr tcg_fpstatus;
    TCGv_i32 tcg_rmode, tcg_shift;

    if (immh & 0x8) {
        size = MO_64;
        if (!is_scalar && !is_q) {
            unallocated_encoding(s);
            return;
        }
    } else if (immh & 0x4) {
        size = MO_32;
    } else if (immh & 0x2) {
        size = MO_16;
        if (!dc_isar_feature(aa64_fp16, s)) {
            unallocated_encoding(s);
            return;
        }
    } else {
        /* Should have split out AdvSIMD modified immediate earlier.  */
        assert(immh == 1);
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    assert(!(is_scalar && is_q));

    tcg_rmode = tcg_const_i32(arm_rmode_to_sf(FPROUNDING_ZERO));
    tcg_fpstatus = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
    fracbits = (16 << size) - immhb;
    tcg_shift = tcg_const_i32(fracbits);

    if (size == MO_64) {
        int maxpass = is_scalar ? 1 : 2;

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i64 tcg_op = tcg_temp_new_i64();

            read_vec_element(s, tcg_op, rn, pass, MO_64);
            if (is_u) {
                gen_helper_vfp_touqd(tcg_op, tcg_op, tcg_shift, tcg_fpstatus);
            } else {
                gen_helper_vfp_tosqd(tcg_op, tcg_op, tcg_shift, tcg_fpstatus);
            }
            write_vec_element(s, tcg_op, rd, pass, MO_64);
            tcg_temp_free_i64(tcg_op);
        }
        clear_vec_high(s, is_q, rd);
    } else {
        void (*fn)(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_ptr);
        int maxpass = is_scalar ? 1 : ((8 << is_q) >> size);

        switch (size) {
        case MO_16:
            if (is_u) {
                fn = gen_helper_vfp_touhh;
            } else {
                fn = gen_helper_vfp_toshh;
            }
            break;
        case MO_32:
            if (is_u) {
                fn = gen_helper_vfp_touls;
            } else {
                fn = gen_helper_vfp_tosls;
            }
            break;
        default:
            g_assert_not_reached();
        }

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i32 tcg_op = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op, rn, pass, size);
            fn(tcg_op, tcg_op, tcg_shift, tcg_fpstatus);
            if (is_scalar) {
                write_fp_sreg(s, rd, tcg_op);
            } else {
                write_vec_element_i32(s, tcg_op, rd, pass, size);
            }
            tcg_temp_free_i32(tcg_op);
        }
        if (!is_scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }

    tcg_temp_free_ptr(tcg_fpstatus);
    tcg_temp_free_i32(tcg_shift);
    gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
    tcg_temp_free_i32(tcg_rmode);
}

/* AdvSIMD scalar shift by immediate
 *  31 30  29 28         23 22  19 18  16 15    11  10 9    5 4    0
 * +-----+---+-------------+------+------+--------+---+------+------+
 * | 0 1 | U | 1 1 1 1 1 0 | immh | immb | opcode | 1 |  Rn  |  Rd  |
 * +-----+---+-------------+------+------+--------+---+------+------+
 *
 * This is the scalar version so it works on a fixed sized registers
 */
static void disas_simd_scalar_shift_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 11, 5);
    int immb = extract32(insn, 16, 3);
    int immh = extract32(insn, 19, 4);
    bool is_u = extract32(insn, 29, 1);

    if (immh == 0) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0x08: /* SRI */
        if (!is_u) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x00: /* SSHR / USHR */
    case 0x02: /* SSRA / USRA */
    case 0x04: /* SRSHR / URSHR */
    case 0x06: /* SRSRA / URSRA */
        handle_scalar_simd_shri(s, is_u, immh, immb, opcode, rn, rd);
        break;
    case 0x0a: /* SHL / SLI */
        handle_scalar_simd_shli(s, is_u, immh, immb, opcode, rn, rd);
        break;
    case 0x1c: /* SCVTF, UCVTF */
        handle_simd_shift_intfp_conv(s, true, false, is_u, immh, immb,
                                     opcode, rn, rd);
        break;
    case 0x10: /* SQSHRUN, SQSHRUN2 */
    case 0x11: /* SQRSHRUN, SQRSHRUN2 */
        if (!is_u) {
            unallocated_encoding(s);
            return;
        }
        handle_vec_simd_sqshrn(s, true, false, false, true,
                               immh, immb, opcode, rn, rd);
        break;
    case 0x12: /* SQSHRN, SQSHRN2, UQSHRN */
    case 0x13: /* SQRSHRN, SQRSHRN2, UQRSHRN, UQRSHRN2 */
        handle_vec_simd_sqshrn(s, true, false, is_u, is_u,
                               immh, immb, opcode, rn, rd);
        break;
    case 0xc: /* SQSHLU */
        if (!is_u) {
            unallocated_encoding(s);
            return;
        }
        handle_simd_qshl(s, true, false, false, true, immh, immb, rn, rd);
        break;
    case 0xe: /* SQSHL, UQSHL */
        handle_simd_qshl(s, true, false, is_u, is_u, immh, immb, rn, rd);
        break;
    case 0x1f: /* FCVTZS, FCVTZU */
        handle_simd_shift_fpint_conv(s, true, false, is_u, immh, immb, rn, rd);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* AdvSIMD scalar three different
 *  31 30  29 28       24 23  22  21 20  16 15    12 11 10 9    5 4    0
 * +-----+---+-----------+------+---+------+--------+-----+------+------+
 * | 0 1 | U | 1 1 1 1 0 | size | 1 |  Rm  | opcode | 0 0 |  Rn  |  Rd  |
 * +-----+---+-----------+------+---+------+--------+-----+------+------+
 */
static void disas_simd_scalar_three_reg_diff(DisasContext *s, uint32_t insn)
{
    bool is_u = extract32(insn, 29, 1);
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 4);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);

    if (is_u) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0x9: /* SQDMLAL, SQDMLAL2 */
    case 0xb: /* SQDMLSL, SQDMLSL2 */
    case 0xd: /* SQDMULL, SQDMULL2 */
        if (size == 0 || size == 3) {
            unallocated_encoding(s);
            return;
        }
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (size == 2) {
        TCGv_i64 tcg_op1 = tcg_temp_new_i64();
        TCGv_i64 tcg_op2 = tcg_temp_new_i64();
        TCGv_i64 tcg_res = tcg_temp_new_i64();

        read_vec_element(s, tcg_op1, rn, 0, MO_32 | MO_SIGN);
        read_vec_element(s, tcg_op2, rm, 0, MO_32 | MO_SIGN);

        tcg_gen_mul_i64(tcg_res, tcg_op1, tcg_op2);
        gen_helper_neon_addl_saturate_s64(tcg_res, cpu_env, tcg_res, tcg_res);

        switch (opcode) {
        case 0xd: /* SQDMULL, SQDMULL2 */
            break;
        case 0xb: /* SQDMLSL, SQDMLSL2 */
            tcg_gen_neg_i64(tcg_res, tcg_res);
            /* fall through */
        case 0x9: /* SQDMLAL, SQDMLAL2 */
            read_vec_element(s, tcg_op1, rd, 0, MO_64);
            gen_helper_neon_addl_saturate_s64(tcg_res, cpu_env,
                                              tcg_res, tcg_op1);
            break;
        default:
            g_assert_not_reached();
        }

        write_fp_dreg(s, rd, tcg_res);

        tcg_temp_free_i64(tcg_op1);
        tcg_temp_free_i64(tcg_op2);
        tcg_temp_free_i64(tcg_res);
    } else {
        TCGv_i32 tcg_op1 = read_fp_hreg(s, rn);
        TCGv_i32 tcg_op2 = read_fp_hreg(s, rm);
        TCGv_i64 tcg_res = tcg_temp_new_i64();

        gen_helper_neon_mull_s16(tcg_res, tcg_op1, tcg_op2);
        gen_helper_neon_addl_saturate_s32(tcg_res, cpu_env, tcg_res, tcg_res);

        switch (opcode) {
        case 0xd: /* SQDMULL, SQDMULL2 */
            break;
        case 0xb: /* SQDMLSL, SQDMLSL2 */
            gen_helper_neon_negl_u32(tcg_res, tcg_res);
            /* fall through */
        case 0x9: /* SQDMLAL, SQDMLAL2 */
        {
            TCGv_i64 tcg_op3 = tcg_temp_new_i64();
            read_vec_element(s, tcg_op3, rd, 0, MO_32);
            gen_helper_neon_addl_saturate_s32(tcg_res, cpu_env,
                                              tcg_res, tcg_op3);
            tcg_temp_free_i64(tcg_op3);
            break;
        }
        default:
            g_assert_not_reached();
        }

        tcg_gen_ext32u_i64(tcg_res, tcg_res);
        write_fp_dreg(s, rd, tcg_res);

        tcg_temp_free_i32(tcg_op1);
        tcg_temp_free_i32(tcg_op2);
        tcg_temp_free_i64(tcg_res);
    }
}

static void handle_3same_64(DisasContext *s, int opcode, bool u,
                            TCGv_i64 tcg_rd, TCGv_i64 tcg_rn, TCGv_i64 tcg_rm)
{
    /* Handle 64x64->64 opcodes which are shared between the scalar
     * and vector 3-same groups. We cover every opcode where size == 3
     * is valid in either the three-reg-same (integer, not pairwise)
     * or scalar-three-reg-same groups.
     */
    TCGCond cond;

    switch (opcode) {
    case 0x1: /* SQADD */
        if (u) {
            gen_helper_neon_qadd_u64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        } else {
            gen_helper_neon_qadd_s64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        }
        break;
    case 0x5: /* SQSUB */
        if (u) {
            gen_helper_neon_qsub_u64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        } else {
            gen_helper_neon_qsub_s64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        }
        break;
    case 0x6: /* CMGT, CMHI */
        /* 64 bit integer comparison, result = test ? (2^64 - 1) : 0.
         * We implement this using setcond (test) and then negating.
         */
        cond = u ? TCG_COND_GTU : TCG_COND_GT;
    do_cmop:
        tcg_gen_setcond_i64(cond, tcg_rd, tcg_rn, tcg_rm);
        tcg_gen_neg_i64(tcg_rd, tcg_rd);
        break;
    case 0x7: /* CMGE, CMHS */
        cond = u ? TCG_COND_GEU : TCG_COND_GE;
        goto do_cmop;
    case 0x11: /* CMTST, CMEQ */
        if (u) {
            cond = TCG_COND_EQ;
            goto do_cmop;
        }
        gen_cmtst_i64(tcg_rd, tcg_rn, tcg_rm);
        break;
    case 0x8: /* SSHL, USHL */
        if (u) {
            gen_ushl_i64(tcg_rd, tcg_rn, tcg_rm);
        } else {
            gen_sshl_i64(tcg_rd, tcg_rn, tcg_rm);
        }
        break;
    case 0x9: /* SQSHL, UQSHL */
        if (u) {
            gen_helper_neon_qshl_u64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        } else {
            gen_helper_neon_qshl_s64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        }
        break;
    case 0xa: /* SRSHL, URSHL */
        if (u) {
            gen_helper_neon_rshl_u64(tcg_rd, tcg_rn, tcg_rm);
        } else {
            gen_helper_neon_rshl_s64(tcg_rd, tcg_rn, tcg_rm);
        }
        break;
    case 0xb: /* SQRSHL, UQRSHL */
        if (u) {
            gen_helper_neon_qrshl_u64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        } else {
            gen_helper_neon_qrshl_s64(tcg_rd, cpu_env, tcg_rn, tcg_rm);
        }
        break;
    case 0x10: /* ADD, SUB */
        if (u) {
            tcg_gen_sub_i64(tcg_rd, tcg_rn, tcg_rm);
        } else {
            tcg_gen_add_i64(tcg_rd, tcg_rn, tcg_rm);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

/* Handle the 3-same-operands float operations; shared by the scalar
 * and vector encodings. The caller must filter out any encodings
 * not allocated for the encoding it is dealing with.
 */
static void handle_3same_float(DisasContext *s, int size, int elements,
                               int fpopcode, int rd, int rn, int rm)
{
    int pass;
    TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);

    for (pass = 0; pass < elements; pass++) {
        if (size) {
            /* Double */
            TCGv_i64 tcg_op1 = tcg_temp_new_i64();
            TCGv_i64 tcg_op2 = tcg_temp_new_i64();
            TCGv_i64 tcg_res = tcg_temp_new_i64();

            read_vec_element(s, tcg_op1, rn, pass, MO_64);
            read_vec_element(s, tcg_op2, rm, pass, MO_64);

            switch (fpopcode) {
            case 0x39: /* FMLS */
                /* As usual for ARM, separate negation for fused multiply-add */
                gen_helper_vfp_negd(tcg_op1, tcg_op1);
                /* fall through */
            case 0x19: /* FMLA */
                read_vec_element(s, tcg_res, rd, pass, MO_64);
                gen_helper_vfp_muladdd(tcg_res, tcg_op1, tcg_op2,
                                       tcg_res, fpst);
                break;
            case 0x18: /* FMAXNM */
                gen_helper_vfp_maxnumd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1a: /* FADD */
                gen_helper_vfp_addd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1b: /* FMULX */
                gen_helper_vfp_mulxd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1c: /* FCMEQ */
                gen_helper_neon_ceq_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1e: /* FMAX */
                gen_helper_vfp_maxd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1f: /* FRECPS */
                gen_helper_recpsf_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x38: /* FMINNM */
                gen_helper_vfp_minnumd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3a: /* FSUB */
                gen_helper_vfp_subd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3e: /* FMIN */
                gen_helper_vfp_mind(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3f: /* FRSQRTS */
                gen_helper_rsqrtsf_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5b: /* FMUL */
                gen_helper_vfp_muld(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5c: /* FCMGE */
                gen_helper_neon_cge_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5d: /* FACGE */
                gen_helper_neon_acge_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5f: /* FDIV */
                gen_helper_vfp_divd(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x7a: /* FABD */
                gen_helper_vfp_subd(tcg_res, tcg_op1, tcg_op2, fpst);
                gen_helper_vfp_absd(tcg_res, tcg_res);
                break;
            case 0x7c: /* FCMGT */
                gen_helper_neon_cgt_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x7d: /* FACGT */
                gen_helper_neon_acgt_f64(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }

            write_vec_element(s, tcg_res, rd, pass, MO_64);

            tcg_temp_free_i64(tcg_res);
            tcg_temp_free_i64(tcg_op1);
            tcg_temp_free_i64(tcg_op2);
        } else {
            /* Single */
            TCGv_i32 tcg_op1 = tcg_temp_new_i32();
            TCGv_i32 tcg_op2 = tcg_temp_new_i32();
            TCGv_i32 tcg_res = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op1, rn, pass, MO_32);
            read_vec_element_i32(s, tcg_op2, rm, pass, MO_32);

            switch (fpopcode) {
            case 0x39: /* FMLS */
                /* As usual for ARM, separate negation for fused multiply-add */
                gen_helper_vfp_negs(tcg_op1, tcg_op1);
                /* fall through */
            case 0x19: /* FMLA */
                read_vec_element_i32(s, tcg_res, rd, pass, MO_32);
                gen_helper_vfp_muladds(tcg_res, tcg_op1, tcg_op2,
                                       tcg_res, fpst);
                break;
            case 0x1a: /* FADD */
                gen_helper_vfp_adds(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1b: /* FMULX */
                gen_helper_vfp_mulxs(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1c: /* FCMEQ */
                gen_helper_neon_ceq_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1e: /* FMAX */
                gen_helper_vfp_maxs(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1f: /* FRECPS */
                gen_helper_recpsf_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x18: /* FMAXNM */
                gen_helper_vfp_maxnums(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x38: /* FMINNM */
                gen_helper_vfp_minnums(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3a: /* FSUB */
                gen_helper_vfp_subs(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3e: /* FMIN */
                gen_helper_vfp_mins(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3f: /* FRSQRTS */
                gen_helper_rsqrtsf_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5b: /* FMUL */
                gen_helper_vfp_muls(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5c: /* FCMGE */
                gen_helper_neon_cge_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5d: /* FACGE */
                gen_helper_neon_acge_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x5f: /* FDIV */
                gen_helper_vfp_divs(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x7a: /* FABD */
                gen_helper_vfp_subs(tcg_res, tcg_op1, tcg_op2, fpst);
                gen_helper_vfp_abss(tcg_res, tcg_res);
                break;
            case 0x7c: /* FCMGT */
                gen_helper_neon_cgt_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x7d: /* FACGT */
                gen_helper_neon_acgt_f32(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }

            if (elements == 1) {
                /* scalar single so clear high part */
                TCGv_i64 tcg_tmp = tcg_temp_new_i64();

                tcg_gen_extu_i32_i64(tcg_tmp, tcg_res);
                write_vec_element(s, tcg_tmp, rd, pass, MO_64);
                tcg_temp_free_i64(tcg_tmp);
            } else {
                write_vec_element_i32(s, tcg_res, rd, pass, MO_32);
            }

            tcg_temp_free_i32(tcg_res);
            tcg_temp_free_i32(tcg_op1);
            tcg_temp_free_i32(tcg_op2);
        }
    }

    tcg_temp_free_ptr(fpst);

    clear_vec_high(s, elements * (size ? 8 : 4) > 8, rd);
}

/* AdvSIMD scalar three same
 *  31 30  29 28       24 23  22  21 20  16 15    11  10 9    5 4    0
 * +-----+---+-----------+------+---+------+--------+---+------+------+
 * | 0 1 | U | 1 1 1 1 0 | size | 1 |  Rm  | opcode | 1 |  Rn  |  Rd  |
 * +-----+---+-----------+------+---+------+--------+---+------+------+
 */
static void disas_simd_scalar_three_reg_same(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 11, 5);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 22, 2);
    bool u = extract32(insn, 29, 1);
    TCGv_i64 tcg_rd;

    if (opcode >= 0x18) {
        /* Floating point: U, size[1] and opcode indicate operation */
        int fpopcode = opcode | (extract32(size, 1, 1) << 5) | (u << 6);
        switch (fpopcode) {
        case 0x1b: /* FMULX */
        case 0x1f: /* FRECPS */
        case 0x3f: /* FRSQRTS */
        case 0x5d: /* FACGE */
        case 0x7d: /* FACGT */
        case 0x1c: /* FCMEQ */
        case 0x5c: /* FCMGE */
        case 0x7c: /* FCMGT */
        case 0x7a: /* FABD */
            break;
        default:
            unallocated_encoding(s);
            return;
        }

        if (!fp_access_check(s)) {
            return;
        }

        handle_3same_float(s, extract32(size, 0, 1), 1, fpopcode, rd, rn, rm);
        return;
    }

    switch (opcode) {
    case 0x1: /* SQADD, UQADD */
    case 0x5: /* SQSUB, UQSUB */
    case 0x9: /* SQSHL, UQSHL */
    case 0xb: /* SQRSHL, UQRSHL */
        break;
    case 0x8: /* SSHL, USHL */
    case 0xa: /* SRSHL, URSHL */
    case 0x6: /* CMGT, CMHI */
    case 0x7: /* CMGE, CMHS */
    case 0x11: /* CMTST, CMEQ */
    case 0x10: /* ADD, SUB (vector) */
        if (size != 3) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x16: /* SQDMULH, SQRDMULH (vector) */
        if (size != 1 && size != 2) {
            unallocated_encoding(s);
            return;
        }
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_rd = tcg_temp_new_i64();

    if (size == 3) {
        TCGv_i64 tcg_rn = read_fp_dreg(s, rn);
        TCGv_i64 tcg_rm = read_fp_dreg(s, rm);

        handle_3same_64(s, opcode, u, tcg_rd, tcg_rn, tcg_rm);
        tcg_temp_free_i64(tcg_rn);
        tcg_temp_free_i64(tcg_rm);
    } else {
        /* Do a single operation on the lowest element in the vector.
         * We use the standard Neon helpers and rely on 0 OP 0 == 0 with
         * no side effects for all these operations.
         * OPTME: special-purpose helpers would avoid doing some
         * unnecessary work in the helper for the 8 and 16 bit cases.
         */
        NeonGenTwoOpEnvFn *genenvfn;
        TCGv_i32 tcg_rn = tcg_temp_new_i32();
        TCGv_i32 tcg_rm = tcg_temp_new_i32();
        TCGv_i32 tcg_rd32 = tcg_temp_new_i32();

        read_vec_element_i32(s, tcg_rn, rn, 0, size);
        read_vec_element_i32(s, tcg_rm, rm, 0, size);

        switch (opcode) {
        case 0x1: /* SQADD, UQADD */
        {
            static NeonGenTwoOpEnvFn * const fns[3][2] = {
                { gen_helper_neon_qadd_s8, gen_helper_neon_qadd_u8 },
                { gen_helper_neon_qadd_s16, gen_helper_neon_qadd_u16 },
                { gen_helper_neon_qadd_s32, gen_helper_neon_qadd_u32 },
            };
            genenvfn = fns[size][u];
            break;
        }
        case 0x5: /* SQSUB, UQSUB */
        {
            static NeonGenTwoOpEnvFn * const fns[3][2] = {
                { gen_helper_neon_qsub_s8, gen_helper_neon_qsub_u8 },
                { gen_helper_neon_qsub_s16, gen_helper_neon_qsub_u16 },
                { gen_helper_neon_qsub_s32, gen_helper_neon_qsub_u32 },
            };
            genenvfn = fns[size][u];
            break;
        }
        case 0x9: /* SQSHL, UQSHL */
        {
            static NeonGenTwoOpEnvFn * const fns[3][2] = {
                { gen_helper_neon_qshl_s8, gen_helper_neon_qshl_u8 },
                { gen_helper_neon_qshl_s16, gen_helper_neon_qshl_u16 },
                { gen_helper_neon_qshl_s32, gen_helper_neon_qshl_u32 },
            };
            genenvfn = fns[size][u];
            break;
        }
        case 0xb: /* SQRSHL, UQRSHL */
        {
            static NeonGenTwoOpEnvFn * const fns[3][2] = {
                { gen_helper_neon_qrshl_s8, gen_helper_neon_qrshl_u8 },
                { gen_helper_neon_qrshl_s16, gen_helper_neon_qrshl_u16 },
                { gen_helper_neon_qrshl_s32, gen_helper_neon_qrshl_u32 },
            };
            genenvfn = fns[size][u];
            break;
        }
        case 0x16: /* SQDMULH, SQRDMULH */
        {
            static NeonGenTwoOpEnvFn * const fns[2][2] = {
                { gen_helper_neon_qdmulh_s16, gen_helper_neon_qrdmulh_s16 },
                { gen_helper_neon_qdmulh_s32, gen_helper_neon_qrdmulh_s32 },
            };
            assert(size == 1 || size == 2);
            genenvfn = fns[size - 1][u];
            break;
        }
        default:
            g_assert_not_reached();
        }

        genenvfn(tcg_rd32, cpu_env, tcg_rn, tcg_rm);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_rd32);
        tcg_temp_free_i32(tcg_rd32);
        tcg_temp_free_i32(tcg_rn);
        tcg_temp_free_i32(tcg_rm);
    }

    write_fp_dreg(s, rd, tcg_rd);

    tcg_temp_free_i64(tcg_rd);
}

/* AdvSIMD scalar three same FP16
 *  31 30  29 28       24 23  22 21 20  16 15 14 13    11 10  9  5 4  0
 * +-----+---+-----------+---+-----+------+-----+--------+---+----+----+
 * | 0 1 | U | 1 1 1 1 0 | a | 1 0 |  Rm  | 0 0 | opcode | 1 | Rn | Rd |
 * +-----+---+-----------+---+-----+------+-----+--------+---+----+----+
 * v: 0101 1110 0100 0000 0000 0100 0000 0000 => 5e400400
 * m: 1101 1111 0110 0000 1100 0100 0000 0000 => df60c400
 */
static void disas_simd_scalar_three_reg_same_fp16(DisasContext *s,
                                                  uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 11, 3);
    int rm = extract32(insn, 16, 5);
    bool u = extract32(insn, 29, 1);
    bool a = extract32(insn, 23, 1);
    int fpopcode = opcode | (a << 3) |  (u << 4);
    TCGv_ptr fpst;
    TCGv_i32 tcg_op1;
    TCGv_i32 tcg_op2;
    TCGv_i32 tcg_res;

    switch (fpopcode) {
    case 0x03: /* FMULX */
    case 0x04: /* FCMEQ (reg) */
    case 0x07: /* FRECPS */
    case 0x0f: /* FRSQRTS */
    case 0x14: /* FCMGE (reg) */
    case 0x15: /* FACGE */
    case 0x1a: /* FABD */
    case 0x1c: /* FCMGT (reg) */
    case 0x1d: /* FACGT */
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!dc_isar_feature(aa64_fp16, s)) {
        unallocated_encoding(s);
    }

    if (!fp_access_check(s)) {
        return;
    }

    fpst = fpstatus_ptr(FPST_FPCR_F16);

    tcg_op1 = read_fp_hreg(s, rn);
    tcg_op2 = read_fp_hreg(s, rm);
    tcg_res = tcg_temp_new_i32();

    switch (fpopcode) {
    case 0x03: /* FMULX */
        gen_helper_advsimd_mulxh(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x04: /* FCMEQ (reg) */
        gen_helper_advsimd_ceq_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x07: /* FRECPS */
        gen_helper_recpsf_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x0f: /* FRSQRTS */
        gen_helper_rsqrtsf_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x14: /* FCMGE (reg) */
        gen_helper_advsimd_cge_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x15: /* FACGE */
        gen_helper_advsimd_acge_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x1a: /* FABD */
        gen_helper_advsimd_subh(tcg_res, tcg_op1, tcg_op2, fpst);
        tcg_gen_andi_i32(tcg_res, tcg_res, 0x7fff);
        break;
    case 0x1c: /* FCMGT (reg) */
        gen_helper_advsimd_cgt_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    case 0x1d: /* FACGT */
        gen_helper_advsimd_acgt_f16(tcg_res, tcg_op1, tcg_op2, fpst);
        break;
    default:
        g_assert_not_reached();
    }

    write_fp_sreg(s, rd, tcg_res);


    tcg_temp_free_i32(tcg_res);
    tcg_temp_free_i32(tcg_op1);
    tcg_temp_free_i32(tcg_op2);
    tcg_temp_free_ptr(fpst);
}

/* AdvSIMD scalar three same extra
 *  31 30  29 28       24 23  22  21 20  16  15 14    11  10 9  5 4  0
 * +-----+---+-----------+------+---+------+---+--------+---+----+----+
 * | 0 1 | U | 1 1 1 1 0 | size | 0 |  Rm  | 1 | opcode | 1 | Rn | Rd |
 * +-----+---+-----------+------+---+------+---+--------+---+----+----+
 */
static void disas_simd_scalar_three_reg_same_extra(DisasContext *s,
                                                   uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 11, 4);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 22, 2);
    bool u = extract32(insn, 29, 1);
    TCGv_i32 ele1, ele2, ele3;
    TCGv_i64 res;
    bool feature;

    switch (u * 16 + opcode) {
    case 0x10: /* SQRDMLAH (vector) */
    case 0x11: /* SQRDMLSH (vector) */
        if (size != 1 && size != 2) {
            unallocated_encoding(s);
            return;
        }
        feature = dc_isar_feature(aa64_rdm, s);
        break;
    default:
        unallocated_encoding(s);
        return;
    }
    if (!feature) {
        unallocated_encoding(s);
        return;
    }
    if (!fp_access_check(s)) {
        return;
    }

    /* Do a single operation on the lowest element in the vector.
     * We use the standard Neon helpers and rely on 0 OP 0 == 0
     * with no side effects for all these operations.
     * OPTME: special-purpose helpers would avoid doing some
     * unnecessary work in the helper for the 16 bit cases.
     */
    ele1 = tcg_temp_new_i32();
    ele2 = tcg_temp_new_i32();
    ele3 = tcg_temp_new_i32();

    read_vec_element_i32(s, ele1, rn, 0, size);
    read_vec_element_i32(s, ele2, rm, 0, size);
    read_vec_element_i32(s, ele3, rd, 0, size);

    switch (opcode) {
    case 0x0: /* SQRDMLAH */
        if (size == 1) {
            gen_helper_neon_qrdmlah_s16(ele3, cpu_env, ele1, ele2, ele3);
        } else {
            gen_helper_neon_qrdmlah_s32(ele3, cpu_env, ele1, ele2, ele3);
        }
        break;
    case 0x1: /* SQRDMLSH */
        if (size == 1) {
            gen_helper_neon_qrdmlsh_s16(ele3, cpu_env, ele1, ele2, ele3);
        } else {
            gen_helper_neon_qrdmlsh_s32(ele3, cpu_env, ele1, ele2, ele3);
        }
        break;
    default:
        g_assert_not_reached();
    }
    tcg_temp_free_i32(ele1);
    tcg_temp_free_i32(ele2);

    res = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(res, ele3);
    tcg_temp_free_i32(ele3);

    write_fp_dreg(s, rd, res);
    tcg_temp_free_i64(res);
}

static void handle_2misc_64(DisasContext *s, int opcode, bool u,
                            TCGv_i64 tcg_rd, TCGv_i64 tcg_rn,
                            TCGv_i32 tcg_rmode, TCGv_ptr tcg_fpstatus)
{
    /* Handle 64->64 opcodes which are shared between the scalar and
     * vector 2-reg-misc groups. We cover every integer opcode where size == 3
     * is valid in either group and also the double-precision fp ops.
     * The caller only need provide tcg_rmode and tcg_fpstatus if the op
     * requires them.
     */
    TCGCond cond;

    switch (opcode) {
    case 0x4: /* CLS, CLZ */
        if (u) {
            tcg_gen_clzi_i64(tcg_rd, tcg_rn, 64);
        } else {
            tcg_gen_clrsb_i64(tcg_rd, tcg_rn);
        }
        break;
    case 0x5: /* NOT */
        /* This opcode is shared with CNT and RBIT but we have earlier
         * enforced that size == 3 if and only if this is the NOT insn.
         */
        tcg_gen_not_i64(tcg_rd, tcg_rn);
        break;
    case 0x7: /* SQABS, SQNEG */
        if (u) {
            gen_helper_neon_qneg_s64(tcg_rd, cpu_env, tcg_rn);
        } else {
            gen_helper_neon_qabs_s64(tcg_rd, cpu_env, tcg_rn);
        }
        break;
    case 0xa: /* CMLT */
        /* 64 bit integer comparison against zero, result is
         * test ? (2^64 - 1) : 0. We implement via setcond(!test) and
         * subtracting 1.
         */
        cond = TCG_COND_LT;
    do_cmop:
        tcg_gen_setcondi_i64(cond, tcg_rd, tcg_rn, 0);
        tcg_gen_neg_i64(tcg_rd, tcg_rd);
        break;
    case 0x8: /* CMGT, CMGE */
        cond = u ? TCG_COND_GE : TCG_COND_GT;
        goto do_cmop;
    case 0x9: /* CMEQ, CMLE */
        cond = u ? TCG_COND_LE : TCG_COND_EQ;
        goto do_cmop;
    case 0xb: /* ABS, NEG */
        if (u) {
            tcg_gen_neg_i64(tcg_rd, tcg_rn);
        } else {
            tcg_gen_abs_i64(tcg_rd, tcg_rn);
        }
        break;
    case 0x2f: /* FABS */
        gen_helper_vfp_absd(tcg_rd, tcg_rn);
        break;
    case 0x6f: /* FNEG */
        gen_helper_vfp_negd(tcg_rd, tcg_rn);
        break;
    case 0x7f: /* FSQRT */
        gen_helper_vfp_sqrtd(tcg_rd, tcg_rn, cpu_env);
        break;
    case 0x1a: /* FCVTNS */
    case 0x1b: /* FCVTMS */
    case 0x1c: /* FCVTAS */
    case 0x3a: /* FCVTPS */
    case 0x3b: /* FCVTZS */
    {
        TCGv_i32 tcg_shift = tcg_const_i32(0);
        gen_helper_vfp_tosqd(tcg_rd, tcg_rn, tcg_shift, tcg_fpstatus);
        tcg_temp_free_i32(tcg_shift);
        break;
    }
    case 0x5a: /* FCVTNU */
    case 0x5b: /* FCVTMU */
    case 0x5c: /* FCVTAU */
    case 0x7a: /* FCVTPU */
    case 0x7b: /* FCVTZU */
    {
        TCGv_i32 tcg_shift = tcg_const_i32(0);
        gen_helper_vfp_touqd(tcg_rd, tcg_rn, tcg_shift, tcg_fpstatus);
        tcg_temp_free_i32(tcg_shift);
        break;
    }
    case 0x18: /* FRINTN */
    case 0x19: /* FRINTM */
    case 0x38: /* FRINTP */
    case 0x39: /* FRINTZ */
    case 0x58: /* FRINTA */
    case 0x79: /* FRINTI */
        gen_helper_rintd(tcg_rd, tcg_rn, tcg_fpstatus);
        break;
    case 0x59: /* FRINTX */
        gen_helper_rintd_exact(tcg_rd, tcg_rn, tcg_fpstatus);
        break;
    case 0x1e: /* FRINT32Z */
    case 0x5e: /* FRINT32X */
        gen_helper_frint32_d(tcg_rd, tcg_rn, tcg_fpstatus);
        break;
    case 0x1f: /* FRINT64Z */
    case 0x5f: /* FRINT64X */
        gen_helper_frint64_d(tcg_rd, tcg_rn, tcg_fpstatus);
        break;
    default:
        g_assert_not_reached();
    }
}

static void handle_2misc_fcmp_zero(DisasContext *s, int opcode,
                                   bool is_scalar, bool is_u, bool is_q,
                                   int size, int rn, int rd)
{
    bool is_double = (size == MO_64);
    TCGv_ptr fpst;

    if (!fp_access_check(s)) {
        return;
    }

    fpst = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);

    if (is_double) {
        TCGv_i64 tcg_op = tcg_temp_new_i64();
        TCGv_i64 tcg_zero = tcg_const_i64(0);
        TCGv_i64 tcg_res = tcg_temp_new_i64();
        NeonGenTwoDoubleOpFn *genfn;
        bool swap = false;
        int pass;

        switch (opcode) {
        case 0x2e: /* FCMLT (zero) */
            swap = true;
            /* fallthrough */
        case 0x2c: /* FCMGT (zero) */
            genfn = gen_helper_neon_cgt_f64;
            break;
        case 0x2d: /* FCMEQ (zero) */
            genfn = gen_helper_neon_ceq_f64;
            break;
        case 0x6d: /* FCMLE (zero) */
            swap = true;
            /* fall through */
        case 0x6c: /* FCMGE (zero) */
            genfn = gen_helper_neon_cge_f64;
            break;
        default:
            g_assert_not_reached();
        }

        for (pass = 0; pass < (is_scalar ? 1 : 2); pass++) {
            read_vec_element(s, tcg_op, rn, pass, MO_64);
            if (swap) {
                genfn(tcg_res, tcg_zero, tcg_op, fpst);
            } else {
                genfn(tcg_res, tcg_op, tcg_zero, fpst);
            }
            write_vec_element(s, tcg_res, rd, pass, MO_64);
        }
        tcg_temp_free_i64(tcg_res);
        tcg_temp_free_i64(tcg_zero);
        tcg_temp_free_i64(tcg_op);

        clear_vec_high(s, !is_scalar, rd);
    } else {
        TCGv_i32 tcg_op = tcg_temp_new_i32();
        TCGv_i32 tcg_zero = tcg_const_i32(0);
        TCGv_i32 tcg_res = tcg_temp_new_i32();
        NeonGenTwoSingleOpFn *genfn;
        bool swap = false;
        int pass, maxpasses;

        if (size == MO_16) {
            switch (opcode) {
            case 0x2e: /* FCMLT (zero) */
                swap = true;
                /* fall through */
            case 0x2c: /* FCMGT (zero) */
                genfn = gen_helper_advsimd_cgt_f16;
                break;
            case 0x2d: /* FCMEQ (zero) */
                genfn = gen_helper_advsimd_ceq_f16;
                break;
            case 0x6d: /* FCMLE (zero) */
                swap = true;
                /* fall through */
            case 0x6c: /* FCMGE (zero) */
                genfn = gen_helper_advsimd_cge_f16;
                break;
            default:
                g_assert_not_reached();
            }
        } else {
            switch (opcode) {
            case 0x2e: /* FCMLT (zero) */
                swap = true;
                /* fall through */
            case 0x2c: /* FCMGT (zero) */
                genfn = gen_helper_neon_cgt_f32;
                break;
            case 0x2d: /* FCMEQ (zero) */
                genfn = gen_helper_neon_ceq_f32;
                break;
            case 0x6d: /* FCMLE (zero) */
                swap = true;
                /* fall through */
            case 0x6c: /* FCMGE (zero) */
                genfn = gen_helper_neon_cge_f32;
                break;
            default:
                g_assert_not_reached();
            }
        }

        if (is_scalar) {
            maxpasses = 1;
        } else {
            int vector_size = 8 << is_q;
            maxpasses = vector_size >> size;
        }

        for (pass = 0; pass < maxpasses; pass++) {
            read_vec_element_i32(s, tcg_op, rn, pass, size);
            if (swap) {
                genfn(tcg_res, tcg_zero, tcg_op, fpst);
            } else {
                genfn(tcg_res, tcg_op, tcg_zero, fpst);
            }
            if (is_scalar) {
                write_fp_sreg(s, rd, tcg_res);
            } else {
                write_vec_element_i32(s, tcg_res, rd, pass, size);
            }
        }
        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_zero);
        tcg_temp_free_i32(tcg_op);
        if (!is_scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }

    tcg_temp_free_ptr(fpst);
}

static void handle_2misc_reciprocal(DisasContext *s, int opcode,
                                    bool is_scalar, bool is_u, bool is_q,
                                    int size, int rn, int rd)
{
    bool is_double = (size == 3);
    TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);

    if (is_double) {
        TCGv_i64 tcg_op = tcg_temp_new_i64();
        TCGv_i64 tcg_res = tcg_temp_new_i64();
        int pass;

        for (pass = 0; pass < (is_scalar ? 1 : 2); pass++) {
            read_vec_element(s, tcg_op, rn, pass, MO_64);
            switch (opcode) {
            case 0x3d: /* FRECPE */
                gen_helper_recpe_f64(tcg_res, tcg_op, fpst);
                break;
            case 0x3f: /* FRECPX */
                gen_helper_frecpx_f64(tcg_res, tcg_op, fpst);
                break;
            case 0x7d: /* FRSQRTE */
                gen_helper_rsqrte_f64(tcg_res, tcg_op, fpst);
                break;
            default:
                g_assert_not_reached();
            }
            write_vec_element(s, tcg_res, rd, pass, MO_64);
        }
        tcg_temp_free_i64(tcg_res);
        tcg_temp_free_i64(tcg_op);
        clear_vec_high(s, !is_scalar, rd);
    } else {
        TCGv_i32 tcg_op = tcg_temp_new_i32();
        TCGv_i32 tcg_res = tcg_temp_new_i32();
        int pass, maxpasses;

        if (is_scalar) {
            maxpasses = 1;
        } else {
            maxpasses = is_q ? 4 : 2;
        }

        for (pass = 0; pass < maxpasses; pass++) {
            read_vec_element_i32(s, tcg_op, rn, pass, MO_32);

            switch (opcode) {
            case 0x3c: /* URECPE */
                gen_helper_recpe_u32(tcg_res, tcg_op);
                break;
            case 0x3d: /* FRECPE */
                gen_helper_recpe_f32(tcg_res, tcg_op, fpst);
                break;
            case 0x3f: /* FRECPX */
                gen_helper_frecpx_f32(tcg_res, tcg_op, fpst);
                break;
            case 0x7d: /* FRSQRTE */
                gen_helper_rsqrte_f32(tcg_res, tcg_op, fpst);
                break;
            default:
                g_assert_not_reached();
            }

            if (is_scalar) {
                write_fp_sreg(s, rd, tcg_res);
            } else {
                write_vec_element_i32(s, tcg_res, rd, pass, MO_32);
            }
        }
        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_op);
        if (!is_scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }
    tcg_temp_free_ptr(fpst);
}

static void handle_2misc_narrow(DisasContext *s, bool scalar,
                                int opcode, bool u, bool is_q,
                                int size, int rn, int rd)
{
    /* Handle 2-reg-misc ops which are narrowing (so each 2*size element
     * in the source becomes a size element in the destination).
     */
    int pass;
    TCGv_i32 tcg_res[2];
    int destelt = is_q ? 2 : 0;
    int passes = scalar ? 1 : 2;

    if (scalar) {
        tcg_res[1] = tcg_const_i32(0);
    }

    for (pass = 0; pass < passes; pass++) {
        TCGv_i64 tcg_op = tcg_temp_new_i64();
        NeonGenNarrowFn *genfn = NULL;
        NeonGenNarrowEnvFn *genenvfn = NULL;

        if (scalar) {
            read_vec_element(s, tcg_op, rn, pass, size + 1);
        } else {
            read_vec_element(s, tcg_op, rn, pass, MO_64);
        }
        tcg_res[pass] = tcg_temp_new_i32();

        switch (opcode) {
        case 0x12: /* XTN, SQXTUN */
        {
            static NeonGenNarrowFn * const xtnfns[3] = {
                gen_helper_neon_narrow_u8,
                gen_helper_neon_narrow_u16,
                tcg_gen_extrl_i64_i32,
            };
            static NeonGenNarrowEnvFn * const sqxtunfns[3] = {
                gen_helper_neon_unarrow_sat8,
                gen_helper_neon_unarrow_sat16,
                gen_helper_neon_unarrow_sat32,
            };
            if (u) {
                genenvfn = sqxtunfns[size];
            } else {
                genfn = xtnfns[size];
            }
            break;
        }
        case 0x14: /* SQXTN, UQXTN */
        {
            static NeonGenNarrowEnvFn * const fns[3][2] = {
                { gen_helper_neon_narrow_sat_s8,
                  gen_helper_neon_narrow_sat_u8 },
                { gen_helper_neon_narrow_sat_s16,
                  gen_helper_neon_narrow_sat_u16 },
                { gen_helper_neon_narrow_sat_s32,
                  gen_helper_neon_narrow_sat_u32 },
            };
            genenvfn = fns[size][u];
            break;
        }
        case 0x16: /* FCVTN, FCVTN2 */
            /* 32 bit to 16 bit or 64 bit to 32 bit float conversion */
            if (size == 2) {
                gen_helper_vfp_fcvtsd(tcg_res[pass], tcg_op, cpu_env);
            } else {
                TCGv_i32 tcg_lo = tcg_temp_new_i32();
                TCGv_i32 tcg_hi = tcg_temp_new_i32();
                TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);
                TCGv_i32 ahp = get_ahp_flag();

                tcg_gen_extr_i64_i32(tcg_lo, tcg_hi, tcg_op);
                gen_helper_vfp_fcvt_f32_to_f16(tcg_lo, tcg_lo, fpst, ahp);
                gen_helper_vfp_fcvt_f32_to_f16(tcg_hi, tcg_hi, fpst, ahp);
                tcg_gen_deposit_i32(tcg_res[pass], tcg_lo, tcg_hi, 16, 16);
                tcg_temp_free_i32(tcg_lo);
                tcg_temp_free_i32(tcg_hi);
                tcg_temp_free_ptr(fpst);
                tcg_temp_free_i32(ahp);
            }
            break;
        case 0x56:  /* FCVTXN, FCVTXN2 */
            /* 64 bit to 32 bit float conversion
             * with von Neumann rounding (round to odd)
             */
            assert(size == 2);
            gen_helper_fcvtx_f64_to_f32(tcg_res[pass], tcg_op, cpu_env);
            break;
        default:
            g_assert_not_reached();
        }

        if (genfn) {
            genfn(tcg_res[pass], tcg_op);
        } else if (genenvfn) {
            genenvfn(tcg_res[pass], cpu_env, tcg_op);
        }

        tcg_temp_free_i64(tcg_op);
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element_i32(s, tcg_res[pass], rd, destelt + pass, MO_32);
        tcg_temp_free_i32(tcg_res[pass]);
    }
    clear_vec_high(s, is_q, rd);
}

/* Remaining saturating accumulating ops */
static void handle_2misc_satacc(DisasContext *s, bool is_scalar, bool is_u,
                                bool is_q, int size, int rn, int rd)
{
    bool is_double = (size == 3);

    if (is_double) {
        TCGv_i64 tcg_rn = tcg_temp_new_i64();
        TCGv_i64 tcg_rd = tcg_temp_new_i64();
        int pass;

        for (pass = 0; pass < (is_scalar ? 1 : 2); pass++) {
            read_vec_element(s, tcg_rn, rn, pass, MO_64);
            read_vec_element(s, tcg_rd, rd, pass, MO_64);

            if (is_u) { /* USQADD */
                gen_helper_neon_uqadd_s64(tcg_rd, cpu_env, tcg_rn, tcg_rd);
            } else { /* SUQADD */
                gen_helper_neon_sqadd_u64(tcg_rd, cpu_env, tcg_rn, tcg_rd);
            }
            write_vec_element(s, tcg_rd, rd, pass, MO_64);
        }
        tcg_temp_free_i64(tcg_rd);
        tcg_temp_free_i64(tcg_rn);
        clear_vec_high(s, !is_scalar, rd);
    } else {
        TCGv_i32 tcg_rn = tcg_temp_new_i32();
        TCGv_i32 tcg_rd = tcg_temp_new_i32();
        int pass, maxpasses;

        if (is_scalar) {
            maxpasses = 1;
        } else {
            maxpasses = is_q ? 4 : 2;
        }

        for (pass = 0; pass < maxpasses; pass++) {
            if (is_scalar) {
                read_vec_element_i32(s, tcg_rn, rn, pass, size);
                read_vec_element_i32(s, tcg_rd, rd, pass, size);
            } else {
                read_vec_element_i32(s, tcg_rn, rn, pass, MO_32);
                read_vec_element_i32(s, tcg_rd, rd, pass, MO_32);
            }

            if (is_u) { /* USQADD */
                switch (size) {
                case 0:
                    gen_helper_neon_uqadd_s8(tcg_rd, cpu_env, tcg_rn, tcg_rd);
                    break;
                case 1:
                    gen_helper_neon_uqadd_s16(tcg_rd, cpu_env, tcg_rn, tcg_rd);
                    break;
                case 2:
                    gen_helper_neon_uqadd_s32(tcg_rd, cpu_env, tcg_rn, tcg_rd);
                    break;
                default:
                    g_assert_not_reached();
                }
            } else { /* SUQADD */
                switch (size) {
                case 0:
                    gen_helper_neon_sqadd_u8(tcg_rd, cpu_env, tcg_rn, tcg_rd);
                    break;
                case 1:
                    gen_helper_neon_sqadd_u16(tcg_rd, cpu_env, tcg_rn, tcg_rd);
                    break;
                case 2:
                    gen_helper_neon_sqadd_u32(tcg_rd, cpu_env, tcg_rn, tcg_rd);
                    break;
                default:
                    g_assert_not_reached();
                }
            }

            if (is_scalar) {
                TCGv_i64 tcg_zero = tcg_const_i64(0);
                write_vec_element(s, tcg_zero, rd, 0, MO_64);
                tcg_temp_free_i64(tcg_zero);
            }
            write_vec_element_i32(s, tcg_rd, rd, pass, MO_32);
        }
        tcg_temp_free_i32(tcg_rd);
        tcg_temp_free_i32(tcg_rn);
        clear_vec_high(s, is_q, rd);
    }
}

/* AdvSIMD scalar two reg misc
 *  31 30  29 28       24 23  22 21       17 16    12 11 10 9    5 4    0
 * +-----+---+-----------+------+-----------+--------+-----+------+------+
 * | 0 1 | U | 1 1 1 1 0 | size | 1 0 0 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +-----+---+-----------+------+-----------+--------+-----+------+------+
 */
static void disas_simd_scalar_two_reg_misc(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 12, 5);
    int size = extract32(insn, 22, 2);
    bool u = extract32(insn, 29, 1);
    bool is_fcvt = false;
    int rmode;
    TCGv_i32 tcg_rmode;
    TCGv_ptr tcg_fpstatus;

    switch (opcode) {
    case 0x3: /* USQADD / SUQADD*/
        if (!fp_access_check(s)) {
            return;
        }
        handle_2misc_satacc(s, true, u, false, size, rn, rd);
        return;
    case 0x7: /* SQABS / SQNEG */
        break;
    case 0xa: /* CMLT */
        if (u) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x8: /* CMGT, CMGE */
    case 0x9: /* CMEQ, CMLE */
    case 0xb: /* ABS, NEG */
        if (size != 3) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x12: /* SQXTUN */
        if (!u) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x14: /* SQXTN, UQXTN */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_2misc_narrow(s, true, opcode, u, false, size, rn, rd);
        return;
    case 0xc ... 0xf:
    case 0x16 ... 0x1d:
    case 0x1f:
        /* Floating point: U, size[1] and opcode indicate operation;
         * size[0] indicates single or double precision.
         */
        opcode |= (extract32(size, 1, 1) << 5) | (u << 6);
        size = extract32(size, 0, 1) ? 3 : 2;
        switch (opcode) {
        case 0x2c: /* FCMGT (zero) */
        case 0x2d: /* FCMEQ (zero) */
        case 0x2e: /* FCMLT (zero) */
        case 0x6c: /* FCMGE (zero) */
        case 0x6d: /* FCMLE (zero) */
            handle_2misc_fcmp_zero(s, opcode, true, u, true, size, rn, rd);
            return;
        case 0x1d: /* SCVTF */
        case 0x5d: /* UCVTF */
        {
            bool is_signed = (opcode == 0x1d);
            if (!fp_access_check(s)) {
                return;
            }
            handle_simd_intfp_conv(s, rd, rn, 1, is_signed, 0, size);
            return;
        }
        case 0x3d: /* FRECPE */
        case 0x3f: /* FRECPX */
        case 0x7d: /* FRSQRTE */
            if (!fp_access_check(s)) {
                return;
            }
            handle_2misc_reciprocal(s, opcode, true, u, true, size, rn, rd);
            return;
        case 0x1a: /* FCVTNS */
        case 0x1b: /* FCVTMS */
        case 0x3a: /* FCVTPS */
        case 0x3b: /* FCVTZS */
        case 0x5a: /* FCVTNU */
        case 0x5b: /* FCVTMU */
        case 0x7a: /* FCVTPU */
        case 0x7b: /* FCVTZU */
            is_fcvt = true;
            rmode = extract32(opcode, 5, 1) | (extract32(opcode, 0, 1) << 1);
            break;
        case 0x1c: /* FCVTAS */
        case 0x5c: /* FCVTAU */
            /* TIEAWAY doesn't fit in the usual rounding mode encoding */
            is_fcvt = true;
            rmode = FPROUNDING_TIEAWAY;
            break;
        case 0x56: /* FCVTXN, FCVTXN2 */
            if (size == 2) {
                unallocated_encoding(s);
                return;
            }
            if (!fp_access_check(s)) {
                return;
            }
            handle_2misc_narrow(s, true, opcode, u, false, size - 1, rn, rd);
            return;
        default:
            unallocated_encoding(s);
            return;
        }
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (is_fcvt) {
        tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
        tcg_fpstatus = fpstatus_ptr(FPST_FPCR);
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
    } else {
        tcg_rmode = NULL;
        tcg_fpstatus = NULL;
    }

    if (size == 3) {
        TCGv_i64 tcg_rn = read_fp_dreg(s, rn);
        TCGv_i64 tcg_rd = tcg_temp_new_i64();

        handle_2misc_64(s, opcode, u, tcg_rd, tcg_rn, tcg_rmode, tcg_fpstatus);
        write_fp_dreg(s, rd, tcg_rd);
        tcg_temp_free_i64(tcg_rd);
        tcg_temp_free_i64(tcg_rn);
    } else {
        TCGv_i32 tcg_rn = tcg_temp_new_i32();
        TCGv_i32 tcg_rd = tcg_temp_new_i32();

        read_vec_element_i32(s, tcg_rn, rn, 0, size);

        switch (opcode) {
        case 0x7: /* SQABS, SQNEG */
        {
            NeonGenOneOpEnvFn *genfn;
            static NeonGenOneOpEnvFn * const fns[3][2] = {
                { gen_helper_neon_qabs_s8, gen_helper_neon_qneg_s8 },
                { gen_helper_neon_qabs_s16, gen_helper_neon_qneg_s16 },
                { gen_helper_neon_qabs_s32, gen_helper_neon_qneg_s32 },
            };
            genfn = fns[size][u];
            genfn(tcg_rd, cpu_env, tcg_rn);
            break;
        }
        case 0x1a: /* FCVTNS */
        case 0x1b: /* FCVTMS */
        case 0x1c: /* FCVTAS */
        case 0x3a: /* FCVTPS */
        case 0x3b: /* FCVTZS */
        {
            TCGv_i32 tcg_shift = tcg_const_i32(0);
            gen_helper_vfp_tosls(tcg_rd, tcg_rn, tcg_shift, tcg_fpstatus);
            tcg_temp_free_i32(tcg_shift);
            break;
        }
        case 0x5a: /* FCVTNU */
        case 0x5b: /* FCVTMU */
        case 0x5c: /* FCVTAU */
        case 0x7a: /* FCVTPU */
        case 0x7b: /* FCVTZU */
        {
            TCGv_i32 tcg_shift = tcg_const_i32(0);
            gen_helper_vfp_touls(tcg_rd, tcg_rn, tcg_shift, tcg_fpstatus);
            tcg_temp_free_i32(tcg_shift);
            break;
        }
        default:
            g_assert_not_reached();
        }

        write_fp_sreg(s, rd, tcg_rd);
        tcg_temp_free_i32(tcg_rd);
        tcg_temp_free_i32(tcg_rn);
    }

    if (is_fcvt) {
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
        tcg_temp_free_i32(tcg_rmode);
        tcg_temp_free_ptr(tcg_fpstatus);
    }
}

/* SSHR[RA]/USHR[RA] - Vector shift right (optional rounding/accumulate) */
static void handle_vec_simd_shri(DisasContext *s, bool is_q, bool is_u,
                                 int immh, int immb, int opcode, int rn, int rd)
{
    int size = 32 - clz32(immh) - 1;
    int immhb = immh << 3 | immb;
    int shift = 2 * (8 << size) - immhb;
    GVecGen2iFn *gvec_fn;

    if (extract32(immh, 3, 1) && !is_q) {
        unallocated_encoding(s);
        return;
    }
    tcg_debug_assert(size <= 3);

    if (!fp_access_check(s)) {
        return;
    }

    switch (opcode) {
    case 0x02: /* SSRA / USRA (accumulate) */
        gvec_fn = is_u ? gen_gvec_usra : gen_gvec_ssra;
        break;

    case 0x08: /* SRI */
        gvec_fn = gen_gvec_sri;
        break;

    case 0x00: /* SSHR / USHR */
        if (is_u) {
            if (shift == 8 << size) {
                /* Shift count the same size as element size produces zero.  */
                tcg_gen_gvec_dup_imm(size, vec_full_reg_offset(s, rd),
                                     is_q ? 16 : 8, vec_full_reg_size(s), 0);
                return;
            }
            gvec_fn = tcg_gen_gvec_shri;
        } else {
            /* Shift count the same size as element size produces all sign.  */
            if (shift == 8 << size) {
                shift -= 1;
            }
            gvec_fn = tcg_gen_gvec_sari;
        }
        break;

    case 0x04: /* SRSHR / URSHR (rounding) */
        gvec_fn = is_u ? gen_gvec_urshr : gen_gvec_srshr;
        break;

    case 0x06: /* SRSRA / URSRA (accum + rounding) */
        gvec_fn = is_u ? gen_gvec_ursra : gen_gvec_srsra;
        break;

    default:
        g_assert_not_reached();
    }

    gen_gvec_fn2i(s, is_q, rd, rn, shift, gvec_fn, size);
}

/* SHL/SLI - Vector shift left */
static void handle_vec_simd_shli(DisasContext *s, bool is_q, bool insert,
                                 int immh, int immb, int opcode, int rn, int rd)
{
    int size = 32 - clz32(immh) - 1;
    int immhb = immh << 3 | immb;
    int shift = immhb - (8 << size);

    /* Range of size is limited by decode: immh is a non-zero 4 bit field */
    assert(size >= 0 && size <= 3);

    if (extract32(immh, 3, 1) && !is_q) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (insert) {
        gen_gvec_fn2i(s, is_q, rd, rn, shift, gen_gvec_sli, size);
    } else {
        gen_gvec_fn2i(s, is_q, rd, rn, shift, tcg_gen_gvec_shli, size);
    }
}

/* USHLL/SHLL - Vector shift left with widening */
static void handle_vec_simd_wshli(DisasContext *s, bool is_q, bool is_u,
                                 int immh, int immb, int opcode, int rn, int rd)
{
    int size = 32 - clz32(immh) - 1;
    int immhb = immh << 3 | immb;
    int shift = immhb - (8 << size);
    int dsize = 64;
    int esize = 8 << size;
    int elements = dsize/esize;
    TCGv_i64 tcg_rn = new_tmp_a64(s);
    TCGv_i64 tcg_rd = new_tmp_a64(s);
    int i;

    if (size >= 3) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    /* For the LL variants the store is larger than the load,
     * so if rd == rn we would overwrite parts of our input.
     * So load everything right now and use shifts in the main loop.
     */
    read_vec_element(s, tcg_rn, rn, is_q ? 1 : 0, MO_64);

    for (i = 0; i < elements; i++) {
        tcg_gen_shri_i64(tcg_rd, tcg_rn, i * esize);
        ext_and_shift_reg(tcg_rd, tcg_rd, size | (!is_u << 2), 0);
        tcg_gen_shli_i64(tcg_rd, tcg_rd, shift);
        write_vec_element(s, tcg_rd, rd, i, size + 1);
    }
}

/* SHRN/RSHRN - Shift right with narrowing (and potential rounding) */
static void handle_vec_simd_shrn(DisasContext *s, bool is_q,
                                 int immh, int immb, int opcode, int rn, int rd)
{
    int immhb = immh << 3 | immb;
    int size = 32 - clz32(immh) - 1;
    int dsize = 64;
    int esize = 8 << size;
    int elements = dsize/esize;
    int shift = (2 * esize) - immhb;
    bool round = extract32(opcode, 0, 1);
    TCGv_i64 tcg_rn, tcg_rd, tcg_final;
    TCGv_i64 tcg_round;
    int i;

    if (extract32(immh, 3, 1)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_rn = tcg_temp_new_i64();
    tcg_rd = tcg_temp_new_i64();
    tcg_final = tcg_temp_new_i64();
    read_vec_element(s, tcg_final, rd, is_q ? 1 : 0, MO_64);

    if (round) {
        uint64_t round_const = 1ULL << (shift - 1);
        tcg_round = tcg_const_i64(round_const);
    } else {
        tcg_round = NULL;
    }

    for (i = 0; i < elements; i++) {
        read_vec_element(s, tcg_rn, rn, i, size+1);
        handle_shri_with_rndacc(tcg_rd, tcg_rn, tcg_round,
                                false, true, size+1, shift);

        tcg_gen_deposit_i64(tcg_final, tcg_final, tcg_rd, esize * i, esize);
    }

    if (!is_q) {
        write_vec_element(s, tcg_final, rd, 0, MO_64);
    } else {
        write_vec_element(s, tcg_final, rd, 1, MO_64);
    }
    if (round) {
        tcg_temp_free_i64(tcg_round);
    }
    tcg_temp_free_i64(tcg_rn);
    tcg_temp_free_i64(tcg_rd);
    tcg_temp_free_i64(tcg_final);

    clear_vec_high(s, is_q, rd);
}


/* AdvSIMD shift by immediate
 *  31  30   29 28         23 22  19 18  16 15    11  10 9    5 4    0
 * +---+---+---+-------------+------+------+--------+---+------+------+
 * | 0 | Q | U | 0 1 1 1 1 0 | immh | immb | opcode | 1 |  Rn  |  Rd  |
 * +---+---+---+-------------+------+------+--------+---+------+------+
 */
static void disas_simd_shift_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 11, 5);
    int immb = extract32(insn, 16, 3);
    int immh = extract32(insn, 19, 4);
    bool is_u = extract32(insn, 29, 1);
    bool is_q = extract32(insn, 30, 1);

    /* data_proc_simd[] has sent immh == 0 to disas_simd_mod_imm. */
    assert(immh != 0);

    switch (opcode) {
    case 0x08: /* SRI */
        if (!is_u) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x00: /* SSHR / USHR */
    case 0x02: /* SSRA / USRA (accumulate) */
    case 0x04: /* SRSHR / URSHR (rounding) */
    case 0x06: /* SRSRA / URSRA (accum + rounding) */
        handle_vec_simd_shri(s, is_q, is_u, immh, immb, opcode, rn, rd);
        break;
    case 0x0a: /* SHL / SLI */
        handle_vec_simd_shli(s, is_q, is_u, immh, immb, opcode, rn, rd);
        break;
    case 0x10: /* SHRN */
    case 0x11: /* RSHRN / SQRSHRUN */
        if (is_u) {
            handle_vec_simd_sqshrn(s, false, is_q, false, true, immh, immb,
                                   opcode, rn, rd);
        } else {
            handle_vec_simd_shrn(s, is_q, immh, immb, opcode, rn, rd);
        }
        break;
    case 0x12: /* SQSHRN / UQSHRN */
    case 0x13: /* SQRSHRN / UQRSHRN */
        handle_vec_simd_sqshrn(s, false, is_q, is_u, is_u, immh, immb,
                               opcode, rn, rd);
        break;
    case 0x14: /* SSHLL / USHLL */
        handle_vec_simd_wshli(s, is_q, is_u, immh, immb, opcode, rn, rd);
        break;
    case 0x1c: /* SCVTF / UCVTF */
        handle_simd_shift_intfp_conv(s, false, is_q, is_u, immh, immb,
                                     opcode, rn, rd);
        break;
    case 0xc: /* SQSHLU */
        if (!is_u) {
            unallocated_encoding(s);
            return;
        }
        handle_simd_qshl(s, false, is_q, false, true, immh, immb, rn, rd);
        break;
    case 0xe: /* SQSHL, UQSHL */
        handle_simd_qshl(s, false, is_q, is_u, is_u, immh, immb, rn, rd);
        break;
    case 0x1f: /* FCVTZS/ FCVTZU */
        handle_simd_shift_fpint_conv(s, false, is_q, is_u, immh, immb, rn, rd);
        return;
    default:
        unallocated_encoding(s);
        return;
    }
}

/* Generate code to do a "long" addition or subtraction, ie one done in
 * TCGv_i64 on vector lanes twice the width specified by size.
 */
static void gen_neon_addl(int size, bool is_sub, TCGv_i64 tcg_res,
                          TCGv_i64 tcg_op1, TCGv_i64 tcg_op2)
{
    static NeonGenTwo64OpFn * const fns[3][2] = {
        { gen_helper_neon_addl_u16, gen_helper_neon_subl_u16 },
        { gen_helper_neon_addl_u32, gen_helper_neon_subl_u32 },
        { tcg_gen_add_i64, tcg_gen_sub_i64 },
    };
    NeonGenTwo64OpFn *genfn;
    assert(size < 3);

    genfn = fns[size][is_sub];
    genfn(tcg_res, tcg_op1, tcg_op2);
}

static void handle_3rd_widening(DisasContext *s, int is_q, int is_u, int size,
                                int opcode, int rd, int rn, int rm)
{
    /* 3-reg-different widening insns: 64 x 64 -> 128 */
    TCGv_i64 tcg_res[2];
    int pass, accop;

    tcg_res[0] = tcg_temp_new_i64();
    tcg_res[1] = tcg_temp_new_i64();

    /* Does this op do an adding accumulate, a subtracting accumulate,
     * or no accumulate at all?
     */
    switch (opcode) {
    case 5:
    case 8:
    case 9:
        accop = 1;
        break;
    case 10:
    case 11:
        accop = -1;
        break;
    default:
        accop = 0;
        break;
    }

    if (accop != 0) {
        read_vec_element(s, tcg_res[0], rd, 0, MO_64);
        read_vec_element(s, tcg_res[1], rd, 1, MO_64);
    }

    /* size == 2 means two 32x32->64 operations; this is worth special
     * casing because we can generally handle it inline.
     */
    if (size == 2) {
        for (pass = 0; pass < 2; pass++) {
            TCGv_i64 tcg_op1 = tcg_temp_new_i64();
            TCGv_i64 tcg_op2 = tcg_temp_new_i64();
            TCGv_i64 tcg_passres;
            MemOp memop = MO_32 | (is_u ? 0 : MO_SIGN);

            int elt = pass + is_q * 2;

            read_vec_element(s, tcg_op1, rn, elt, memop);
            read_vec_element(s, tcg_op2, rm, elt, memop);

            if (accop == 0) {
                tcg_passres = tcg_res[pass];
            } else {
                tcg_passres = tcg_temp_new_i64();
            }

            switch (opcode) {
            case 0: /* SADDL, SADDL2, UADDL, UADDL2 */
                tcg_gen_add_i64(tcg_passres, tcg_op1, tcg_op2);
                break;
            case 2: /* SSUBL, SSUBL2, USUBL, USUBL2 */
                tcg_gen_sub_i64(tcg_passres, tcg_op1, tcg_op2);
                break;
            case 5: /* SABAL, SABAL2, UABAL, UABAL2 */
            case 7: /* SABDL, SABDL2, UABDL, UABDL2 */
            {
                TCGv_i64 tcg_tmp1 = tcg_temp_new_i64();
                TCGv_i64 tcg_tmp2 = tcg_temp_new_i64();

                tcg_gen_sub_i64(tcg_tmp1, tcg_op1, tcg_op2);
                tcg_gen_sub_i64(tcg_tmp2, tcg_op2, tcg_op1);
                tcg_gen_movcond_i64(is_u ? TCG_COND_GEU : TCG_COND_GE,
                                    tcg_passres,
                                    tcg_op1, tcg_op2, tcg_tmp1, tcg_tmp2);
                tcg_temp_free_i64(tcg_tmp1);
                tcg_temp_free_i64(tcg_tmp2);
                break;
            }
            case 8: /* SMLAL, SMLAL2, UMLAL, UMLAL2 */
            case 10: /* SMLSL, SMLSL2, UMLSL, UMLSL2 */
            case 12: /* UMULL, UMULL2, SMULL, SMULL2 */
                tcg_gen_mul_i64(tcg_passres, tcg_op1, tcg_op2);
                break;
            case 9: /* SQDMLAL, SQDMLAL2 */
            case 11: /* SQDMLSL, SQDMLSL2 */
            case 13: /* SQDMULL, SQDMULL2 */
                tcg_gen_mul_i64(tcg_passres, tcg_op1, tcg_op2);
                gen_helper_neon_addl_saturate_s64(tcg_passres, cpu_env,
                                                  tcg_passres, tcg_passres);
                break;
            default:
                g_assert_not_reached();
            }

            if (opcode == 9 || opcode == 11) {
                /* saturating accumulate ops */
                if (accop < 0) {
                    tcg_gen_neg_i64(tcg_passres, tcg_passres);
                }
                gen_helper_neon_addl_saturate_s64(tcg_res[pass], cpu_env,
                                                  tcg_res[pass], tcg_passres);
            } else if (accop > 0) {
                tcg_gen_add_i64(tcg_res[pass], tcg_res[pass], tcg_passres);
            } else if (accop < 0) {
                tcg_gen_sub_i64(tcg_res[pass], tcg_res[pass], tcg_passres);
            }

            if (accop != 0) {
                tcg_temp_free_i64(tcg_passres);
            }

            tcg_temp_free_i64(tcg_op1);
            tcg_temp_free_i64(tcg_op2);
        }
    } else {
        /* size 0 or 1, generally helper functions */
        for (pass = 0; pass < 2; pass++) {
            TCGv_i32 tcg_op1 = tcg_temp_new_i32();
            TCGv_i32 tcg_op2 = tcg_temp_new_i32();
            TCGv_i64 tcg_passres;
            int elt = pass + is_q * 2;

            read_vec_element_i32(s, tcg_op1, rn, elt, MO_32);
            read_vec_element_i32(s, tcg_op2, rm, elt, MO_32);

            if (accop == 0) {
                tcg_passres = tcg_res[pass];
            } else {
                tcg_passres = tcg_temp_new_i64();
            }

            switch (opcode) {
            case 0: /* SADDL, SADDL2, UADDL, UADDL2 */
            case 2: /* SSUBL, SSUBL2, USUBL, USUBL2 */
            {
                TCGv_i64 tcg_op2_64 = tcg_temp_new_i64();
                static NeonGenWidenFn * const widenfns[2][2] = {
                    { gen_helper_neon_widen_s8, gen_helper_neon_widen_u8 },
                    { gen_helper_neon_widen_s16, gen_helper_neon_widen_u16 },
                };
                NeonGenWidenFn *widenfn = widenfns[size][is_u];

                widenfn(tcg_op2_64, tcg_op2);
                widenfn(tcg_passres, tcg_op1);
                gen_neon_addl(size, (opcode == 2), tcg_passres,
                              tcg_passres, tcg_op2_64);
                tcg_temp_free_i64(tcg_op2_64);
                break;
            }
            case 5: /* SABAL, SABAL2, UABAL, UABAL2 */
            case 7: /* SABDL, SABDL2, UABDL, UABDL2 */
                if (size == 0) {
                    if (is_u) {
                        gen_helper_neon_abdl_u16(tcg_passres, tcg_op1, tcg_op2);
                    } else {
                        gen_helper_neon_abdl_s16(tcg_passres, tcg_op1, tcg_op2);
                    }
                } else {
                    if (is_u) {
                        gen_helper_neon_abdl_u32(tcg_passres, tcg_op1, tcg_op2);
                    } else {
                        gen_helper_neon_abdl_s32(tcg_passres, tcg_op1, tcg_op2);
                    }
                }
                break;
            case 8: /* SMLAL, SMLAL2, UMLAL, UMLAL2 */
            case 10: /* SMLSL, SMLSL2, UMLSL, UMLSL2 */
            case 12: /* UMULL, UMULL2, SMULL, SMULL2 */
                if (size == 0) {
                    if (is_u) {
                        gen_helper_neon_mull_u8(tcg_passres, tcg_op1, tcg_op2);
                    } else {
                        gen_helper_neon_mull_s8(tcg_passres, tcg_op1, tcg_op2);
                    }
                } else {
                    if (is_u) {
                        gen_helper_neon_mull_u16(tcg_passres, tcg_op1, tcg_op2);
                    } else {
                        gen_helper_neon_mull_s16(tcg_passres, tcg_op1, tcg_op2);
                    }
                }
                break;
            case 9: /* SQDMLAL, SQDMLAL2 */
            case 11: /* SQDMLSL, SQDMLSL2 */
            case 13: /* SQDMULL, SQDMULL2 */
                assert(size == 1);
                gen_helper_neon_mull_s16(tcg_passres, tcg_op1, tcg_op2);
                gen_helper_neon_addl_saturate_s32(tcg_passres, cpu_env,
                                                  tcg_passres, tcg_passres);
                break;
            default:
                g_assert_not_reached();
            }
            tcg_temp_free_i32(tcg_op1);
            tcg_temp_free_i32(tcg_op2);

            if (accop != 0) {
                if (opcode == 9 || opcode == 11) {
                    /* saturating accumulate ops */
                    if (accop < 0) {
                        gen_helper_neon_negl_u32(tcg_passres, tcg_passres);
                    }
                    gen_helper_neon_addl_saturate_s32(tcg_res[pass], cpu_env,
                                                      tcg_res[pass],
                                                      tcg_passres);
                } else {
                    gen_neon_addl(size, (accop < 0), tcg_res[pass],
                                  tcg_res[pass], tcg_passres);
                }
                tcg_temp_free_i64(tcg_passres);
            }
        }
    }

    write_vec_element(s, tcg_res[0], rd, 0, MO_64);
    write_vec_element(s, tcg_res[1], rd, 1, MO_64);
    tcg_temp_free_i64(tcg_res[0]);
    tcg_temp_free_i64(tcg_res[1]);
}

static void handle_3rd_wide(DisasContext *s, int is_q, int is_u, int size,
                            int opcode, int rd, int rn, int rm)
{
    TCGv_i64 tcg_res[2];
    int part = is_q ? 2 : 0;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        TCGv_i64 tcg_op1 = tcg_temp_new_i64();
        TCGv_i32 tcg_op2 = tcg_temp_new_i32();
        TCGv_i64 tcg_op2_wide = tcg_temp_new_i64();
        static NeonGenWidenFn * const widenfns[3][2] = {
            { gen_helper_neon_widen_s8, gen_helper_neon_widen_u8 },
            { gen_helper_neon_widen_s16, gen_helper_neon_widen_u16 },
            { tcg_gen_ext_i32_i64, tcg_gen_extu_i32_i64 },
        };
        NeonGenWidenFn *widenfn = widenfns[size][is_u];

        read_vec_element(s, tcg_op1, rn, pass, MO_64);
        read_vec_element_i32(s, tcg_op2, rm, part + pass, MO_32);
        widenfn(tcg_op2_wide, tcg_op2);
        tcg_temp_free_i32(tcg_op2);
        tcg_res[pass] = tcg_temp_new_i64();
        gen_neon_addl(size, (opcode == 3),
                      tcg_res[pass], tcg_op1, tcg_op2_wide);
        tcg_temp_free_i64(tcg_op1);
        tcg_temp_free_i64(tcg_op2_wide);
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
        tcg_temp_free_i64(tcg_res[pass]);
    }
}

static void do_narrow_round_high_u32(TCGv_i32 res, TCGv_i64 in)
{
    tcg_gen_addi_i64(in, in, 1U << 31);
    tcg_gen_extrh_i64_i32(res, in);
}

static void handle_3rd_narrowing(DisasContext *s, int is_q, int is_u, int size,
                                 int opcode, int rd, int rn, int rm)
{
    TCGv_i32 tcg_res[2];
    int part = is_q ? 2 : 0;
    int pass;

    for (pass = 0; pass < 2; pass++) {
        TCGv_i64 tcg_op1 = tcg_temp_new_i64();
        TCGv_i64 tcg_op2 = tcg_temp_new_i64();
        TCGv_i64 tcg_wideres = tcg_temp_new_i64();
        static NeonGenNarrowFn * const narrowfns[3][2] = {
            { gen_helper_neon_narrow_high_u8,
              gen_helper_neon_narrow_round_high_u8 },
            { gen_helper_neon_narrow_high_u16,
              gen_helper_neon_narrow_round_high_u16 },
            { tcg_gen_extrh_i64_i32, do_narrow_round_high_u32 },
        };
        NeonGenNarrowFn *gennarrow = narrowfns[size][is_u];

        read_vec_element(s, tcg_op1, rn, pass, MO_64);
        read_vec_element(s, tcg_op2, rm, pass, MO_64);

        gen_neon_addl(size, (opcode == 6), tcg_wideres, tcg_op1, tcg_op2);

        tcg_temp_free_i64(tcg_op1);
        tcg_temp_free_i64(tcg_op2);

        tcg_res[pass] = tcg_temp_new_i32();
        gennarrow(tcg_res[pass], tcg_wideres);
        tcg_temp_free_i64(tcg_wideres);
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element_i32(s, tcg_res[pass], rd, pass + part, MO_32);
        tcg_temp_free_i32(tcg_res[pass]);
    }
    clear_vec_high(s, is_q, rd);
}

/* AdvSIMD three different
 *   31  30  29 28       24 23  22  21 20  16 15    12 11 10 9    5 4    0
 * +---+---+---+-----------+------+---+------+--------+-----+------+------+
 * | 0 | Q | U | 0 1 1 1 0 | size | 1 |  Rm  | opcode | 0 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+------+--------+-----+------+------+
 */
static void disas_simd_three_reg_diff(DisasContext *s, uint32_t insn)
{
    /* Instructions in this group fall into three basic classes
     * (in each case with the operation working on each element in
     * the input vectors):
     * (1) widening 64 x 64 -> 128 (with possibly Vd as an extra
     *     128 bit input)
     * (2) wide 64 x 128 -> 128
     * (3) narrowing 128 x 128 -> 64
     * Here we do initial decode, catch unallocated cases and
     * dispatch to separate functions for each class.
     */
    int is_q = extract32(insn, 30, 1);
    int is_u = extract32(insn, 29, 1);
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 4);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);

    switch (opcode) {
    case 1: /* SADDW, SADDW2, UADDW, UADDW2 */
    case 3: /* SSUBW, SSUBW2, USUBW, USUBW2 */
        /* 64 x 128 -> 128 */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_3rd_wide(s, is_q, is_u, size, opcode, rd, rn, rm);
        break;
    case 4: /* ADDHN, ADDHN2, RADDHN, RADDHN2 */
    case 6: /* SUBHN, SUBHN2, RSUBHN, RSUBHN2 */
        /* 128 x 128 -> 64 */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_3rd_narrowing(s, is_q, is_u, size, opcode, rd, rn, rm);
        break;
    case 14: /* PMULL, PMULL2 */
        if (is_u) {
            unallocated_encoding(s);
            return;
        }
        switch (size) {
        case 0: /* PMULL.P8 */
            if (!fp_access_check(s)) {
                return;
            }
            /* The Q field specifies lo/hi half input for this insn.  */
            gen_gvec_op3_ool(s, true, rd, rn, rm, is_q,
                             gen_helper_neon_pmull_h);
            break;

        case 3: /* PMULL.P64 */
            if (!dc_isar_feature(aa64_pmull, s)) {
                unallocated_encoding(s);
                return;
            }
            if (!fp_access_check(s)) {
                return;
            }
            /* The Q field specifies lo/hi half input for this insn.  */
            gen_gvec_op3_ool(s, true, rd, rn, rm, is_q,
                             gen_helper_gvec_pmull_q);
            break;

        default:
            unallocated_encoding(s);
            break;
        }
        return;
    case 9: /* SQDMLAL, SQDMLAL2 */
    case 11: /* SQDMLSL, SQDMLSL2 */
    case 13: /* SQDMULL, SQDMULL2 */
        if (is_u || size == 0) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0: /* SADDL, SADDL2, UADDL, UADDL2 */
    case 2: /* SSUBL, SSUBL2, USUBL, USUBL2 */
    case 5: /* SABAL, SABAL2, UABAL, UABAL2 */
    case 7: /* SABDL, SABDL2, UABDL, UABDL2 */
    case 8: /* SMLAL, SMLAL2, UMLAL, UMLAL2 */
    case 10: /* SMLSL, SMLSL2, UMLSL, UMLSL2 */
    case 12: /* SMULL, SMULL2, UMULL, UMULL2 */
        /* 64 x 64 -> 128 */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }

        handle_3rd_widening(s, is_q, is_u, size, opcode, rd, rn, rm);
        break;
    default:
        /* opcode 15 not allocated */
        unallocated_encoding(s);
        break;
    }
}

/* Logic op (opcode == 3) subgroup of C3.6.16. */
static void disas_simd_3same_logic(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 22, 2);
    bool is_u = extract32(insn, 29, 1);
    bool is_q = extract32(insn, 30, 1);

    if (!fp_access_check(s)) {
        return;
    }

    switch (size + 4 * is_u) {
    case 0: /* AND */
        gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_and, 0);
        return;
    case 1: /* BIC */
        gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_andc, 0);
        return;
    case 2: /* ORR */
        gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_or, 0);
        return;
    case 3: /* ORN */
        gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_orc, 0);
        return;
    case 4: /* EOR */
        gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_xor, 0);
        return;

    case 5: /* BSL bitwise select */
        gen_gvec_fn4(s, is_q, rd, rd, rn, rm, tcg_gen_gvec_bitsel, 0);
        return;
    case 6: /* BIT, bitwise insert if true */
        gen_gvec_fn4(s, is_q, rd, rm, rn, rd, tcg_gen_gvec_bitsel, 0);
        return;
    case 7: /* BIF, bitwise insert if false */
        gen_gvec_fn4(s, is_q, rd, rm, rd, rn, tcg_gen_gvec_bitsel, 0);
        return;

    default:
        g_assert_not_reached();
    }
}

/* Pairwise op subgroup of C3.6.16.
 *
 * This is called directly or via the handle_3same_float for float pairwise
 * operations where the opcode and size are calculated differently.
 */
static void handle_simd_3same_pair(DisasContext *s, int is_q, int u, int opcode,
                                   int size, int rn, int rm, int rd)
{
    TCGv_ptr fpst;
    int pass;

    /* Floating point operations need fpst */
    if (opcode >= 0x58) {
        fpst = fpstatus_ptr(FPST_FPCR);
    } else {
        fpst = NULL;
    }

    if (!fp_access_check(s)) {
        return;
    }

    /* These operations work on the concatenated rm:rn, with each pair of
     * adjacent elements being operated on to produce an element in the result.
     */
    if (size == 3) {
        TCGv_i64 tcg_res[2];

        for (pass = 0; pass < 2; pass++) {
            TCGv_i64 tcg_op1 = tcg_temp_new_i64();
            TCGv_i64 tcg_op2 = tcg_temp_new_i64();
            int passreg = (pass == 0) ? rn : rm;

            read_vec_element(s, tcg_op1, passreg, 0, MO_64);
            read_vec_element(s, tcg_op2, passreg, 1, MO_64);
            tcg_res[pass] = tcg_temp_new_i64();

            switch (opcode) {
            case 0x17: /* ADDP */
                tcg_gen_add_i64(tcg_res[pass], tcg_op1, tcg_op2);
                break;
            case 0x58: /* FMAXNMP */
                gen_helper_vfp_maxnumd(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x5a: /* FADDP */
                gen_helper_vfp_addd(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x5e: /* FMAXP */
                gen_helper_vfp_maxd(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x78: /* FMINNMP */
                gen_helper_vfp_minnumd(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x7e: /* FMINP */
                gen_helper_vfp_mind(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }

            tcg_temp_free_i64(tcg_op1);
            tcg_temp_free_i64(tcg_op2);
        }

        for (pass = 0; pass < 2; pass++) {
            write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
            tcg_temp_free_i64(tcg_res[pass]);
        }
    } else {
        int maxpass = is_q ? 4 : 2;
        TCGv_i32 tcg_res[4];

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i32 tcg_op1 = tcg_temp_new_i32();
            TCGv_i32 tcg_op2 = tcg_temp_new_i32();
            NeonGenTwoOpFn *genfn = NULL;
            int passreg = pass < (maxpass / 2) ? rn : rm;
            int passelt = (is_q && (pass & 1)) ? 2 : 0;

            read_vec_element_i32(s, tcg_op1, passreg, passelt, MO_32);
            read_vec_element_i32(s, tcg_op2, passreg, passelt + 1, MO_32);
            tcg_res[pass] = tcg_temp_new_i32();

            switch (opcode) {
            case 0x17: /* ADDP */
            {
                static NeonGenTwoOpFn * const fns[3] = {
                    gen_helper_neon_padd_u8,
                    gen_helper_neon_padd_u16,
                    tcg_gen_add_i32,
                };
                genfn = fns[size];
                break;
            }
            case 0x14: /* SMAXP, UMAXP */
            {
                static NeonGenTwoOpFn * const fns[3][2] = {
                    { gen_helper_neon_pmax_s8, gen_helper_neon_pmax_u8 },
                    { gen_helper_neon_pmax_s16, gen_helper_neon_pmax_u16 },
                    { tcg_gen_smax_i32, tcg_gen_umax_i32 },
                };
                genfn = fns[size][u];
                break;
            }
            case 0x15: /* SMINP, UMINP */
            {
                static NeonGenTwoOpFn * const fns[3][2] = {
                    { gen_helper_neon_pmin_s8, gen_helper_neon_pmin_u8 },
                    { gen_helper_neon_pmin_s16, gen_helper_neon_pmin_u16 },
                    { tcg_gen_smin_i32, tcg_gen_umin_i32 },
                };
                genfn = fns[size][u];
                break;
            }
            /* The FP operations are all on single floats (32 bit) */
            case 0x58: /* FMAXNMP */
                gen_helper_vfp_maxnums(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x5a: /* FADDP */
                gen_helper_vfp_adds(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x5e: /* FMAXP */
                gen_helper_vfp_maxs(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x78: /* FMINNMP */
                gen_helper_vfp_minnums(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x7e: /* FMINP */
                gen_helper_vfp_mins(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }

            /* FP ops called directly, otherwise call now */
            if (genfn) {
                genfn(tcg_res[pass], tcg_op1, tcg_op2);
            }

            tcg_temp_free_i32(tcg_op1);
            tcg_temp_free_i32(tcg_op2);
        }

        for (pass = 0; pass < maxpass; pass++) {
            write_vec_element_i32(s, tcg_res[pass], rd, pass, MO_32);
            tcg_temp_free_i32(tcg_res[pass]);
        }
        clear_vec_high(s, is_q, rd);
    }

    if (fpst) {
        tcg_temp_free_ptr(fpst);
    }
}

/* Floating point op subgroup of C3.6.16. */
static void disas_simd_3same_float(DisasContext *s, uint32_t insn)
{
    /* For floating point ops, the U, size[1] and opcode bits
     * together indicate the operation. size[0] indicates single
     * or double.
     */
    int fpopcode = extract32(insn, 11, 5)
        | (extract32(insn, 23, 1) << 5)
        | (extract32(insn, 29, 1) << 6);
    int is_q = extract32(insn, 30, 1);
    int size = extract32(insn, 22, 1);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);

    int datasize = is_q ? 128 : 64;
    int esize = 32 << size;
    int elements = datasize / esize;

    if (size == 1 && !is_q) {
        unallocated_encoding(s);
        return;
    }

    switch (fpopcode) {
    case 0x58: /* FMAXNMP */
    case 0x5a: /* FADDP */
    case 0x5e: /* FMAXP */
    case 0x78: /* FMINNMP */
    case 0x7e: /* FMINP */
        if (size && !is_q) {
            unallocated_encoding(s);
            return;
        }
        handle_simd_3same_pair(s, is_q, 0, fpopcode, size ? MO_64 : MO_32,
                               rn, rm, rd);
        return;
    case 0x1b: /* FMULX */
    case 0x1f: /* FRECPS */
    case 0x3f: /* FRSQRTS */
    case 0x5d: /* FACGE */
    case 0x7d: /* FACGT */
    case 0x19: /* FMLA */
    case 0x39: /* FMLS */
    case 0x18: /* FMAXNM */
    case 0x1a: /* FADD */
    case 0x1c: /* FCMEQ */
    case 0x1e: /* FMAX */
    case 0x38: /* FMINNM */
    case 0x3a: /* FSUB */
    case 0x3e: /* FMIN */
    case 0x5b: /* FMUL */
    case 0x5c: /* FCMGE */
    case 0x5f: /* FDIV */
    case 0x7a: /* FABD */
    case 0x7c: /* FCMGT */
        if (!fp_access_check(s)) {
            return;
        }
        handle_3same_float(s, size, elements, fpopcode, rd, rn, rm);
        return;

    case 0x1d: /* FMLAL  */
    case 0x3d: /* FMLSL  */
    case 0x59: /* FMLAL2 */
    case 0x79: /* FMLSL2 */
        if (size & 1 || !dc_isar_feature(aa64_fhm, s)) {
            unallocated_encoding(s);
            return;
        }
        if (fp_access_check(s)) {
            int is_s = extract32(insn, 23, 1);
            int is_2 = extract32(insn, 29, 1);
            int data = (is_2 << 1) | is_s;
            tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                               vec_full_reg_offset(s, rn),
                               vec_full_reg_offset(s, rm), cpu_env,
                               is_q ? 16 : 8, vec_full_reg_size(s),
                               data, gen_helper_gvec_fmlal_a64);
        }
        return;

    default:
        unallocated_encoding(s);
        return;
    }
}

/* Integer op subgroup of C3.6.16. */
static void disas_simd_3same_int(DisasContext *s, uint32_t insn)
{
    int is_q = extract32(insn, 30, 1);
    int u = extract32(insn, 29, 1);
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 11, 5);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    int pass;
    TCGCond cond;

    switch (opcode) {
    case 0x13: /* MUL, PMUL */
        if (u && size != 0) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x0: /* SHADD, UHADD */
    case 0x2: /* SRHADD, URHADD */
    case 0x4: /* SHSUB, UHSUB */
    case 0xc: /* SMAX, UMAX */
    case 0xd: /* SMIN, UMIN */
    case 0xe: /* SABD, UABD */
    case 0xf: /* SABA, UABA */
    case 0x12: /* MLA, MLS */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x16: /* SQDMULH, SQRDMULH */
        if (size == 0 || size == 3) {
            unallocated_encoding(s);
            return;
        }
        break;
    default:
        if (size == 3 && !is_q) {
            unallocated_encoding(s);
            return;
        }
        break;
    }

    if (!fp_access_check(s)) {
        return;
    }

    switch (opcode) {
    case 0x01: /* SQADD, UQADD */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_uqadd_qc, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_sqadd_qc, size);
        }
        return;
    case 0x05: /* SQSUB, UQSUB */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_uqsub_qc, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_sqsub_qc, size);
        }
        return;
    case 0x08: /* SSHL, USHL */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_ushl, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_sshl, size);
        }
        return;
    case 0x0c: /* SMAX, UMAX */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_umax, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_smax, size);
        }
        return;
    case 0x0d: /* SMIN, UMIN */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_umin, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_smin, size);
        }
        return;
    case 0xe: /* SABD, UABD */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_uabd, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_sabd, size);
        }
        return;
    case 0xf: /* SABA, UABA */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_uaba, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_saba, size);
        }
        return;
    case 0x10: /* ADD, SUB */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_sub, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_add, size);
        }
        return;
    case 0x13: /* MUL, PMUL */
        if (!u) { /* MUL */
            gen_gvec_fn3(s, is_q, rd, rn, rm, tcg_gen_gvec_mul, size);
        } else {  /* PMUL */
            gen_gvec_op3_ool(s, is_q, rd, rn, rm, 0, gen_helper_gvec_pmul_b);
        }
        return;
    case 0x12: /* MLA, MLS */
        if (u) {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_mls, size);
        } else {
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_mla, size);
        }
        return;
    case 0x16: /* SQDMULH, SQRDMULH */
        {
            static gen_helper_gvec_3_ptr * const fns[2][2] = {
                { gen_helper_neon_sqdmulh_h, gen_helper_neon_sqrdmulh_h },
                { gen_helper_neon_sqdmulh_s, gen_helper_neon_sqrdmulh_s },
            };
            gen_gvec_op3_qc(s, is_q, rd, rn, rm, fns[size - 1][u]);
        }
        return;
    case 0x11:
        if (!u) { /* CMTST */
            gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_cmtst, size);
            return;
        }
        /* else CMEQ */
        cond = TCG_COND_EQ;
        goto do_gvec_cmp;
    case 0x06: /* CMGT, CMHI */
        cond = u ? TCG_COND_GTU : TCG_COND_GT;
        goto do_gvec_cmp;
    case 0x07: /* CMGE, CMHS */
        cond = u ? TCG_COND_GEU : TCG_COND_GE;
    do_gvec_cmp:
        tcg_gen_gvec_cmp(cond, size, vec_full_reg_offset(s, rd),
                         vec_full_reg_offset(s, rn),
                         vec_full_reg_offset(s, rm),
                         is_q ? 16 : 8, vec_full_reg_size(s));
        return;
    }

    if (size == 3) {
        assert(is_q);
        for (pass = 0; pass < 2; pass++) {
            TCGv_i64 tcg_op1 = tcg_temp_new_i64();
            TCGv_i64 tcg_op2 = tcg_temp_new_i64();
            TCGv_i64 tcg_res = tcg_temp_new_i64();

            read_vec_element(s, tcg_op1, rn, pass, MO_64);
            read_vec_element(s, tcg_op2, rm, pass, MO_64);

            handle_3same_64(s, opcode, u, tcg_res, tcg_op1, tcg_op2);

            write_vec_element(s, tcg_res, rd, pass, MO_64);

            tcg_temp_free_i64(tcg_res);
            tcg_temp_free_i64(tcg_op1);
            tcg_temp_free_i64(tcg_op2);
        }
    } else {
        for (pass = 0; pass < (is_q ? 4 : 2); pass++) {
            TCGv_i32 tcg_op1 = tcg_temp_new_i32();
            TCGv_i32 tcg_op2 = tcg_temp_new_i32();
            TCGv_i32 tcg_res = tcg_temp_new_i32();
            NeonGenTwoOpFn *genfn = NULL;
            NeonGenTwoOpEnvFn *genenvfn = NULL;

            read_vec_element_i32(s, tcg_op1, rn, pass, MO_32);
            read_vec_element_i32(s, tcg_op2, rm, pass, MO_32);

            switch (opcode) {
            case 0x0: /* SHADD, UHADD */
            {
                static NeonGenTwoOpFn * const fns[3][2] = {
                    { gen_helper_neon_hadd_s8, gen_helper_neon_hadd_u8 },
                    { gen_helper_neon_hadd_s16, gen_helper_neon_hadd_u16 },
                    { gen_helper_neon_hadd_s32, gen_helper_neon_hadd_u32 },
                };
                genfn = fns[size][u];
                break;
            }
            case 0x2: /* SRHADD, URHADD */
            {
                static NeonGenTwoOpFn * const fns[3][2] = {
                    { gen_helper_neon_rhadd_s8, gen_helper_neon_rhadd_u8 },
                    { gen_helper_neon_rhadd_s16, gen_helper_neon_rhadd_u16 },
                    { gen_helper_neon_rhadd_s32, gen_helper_neon_rhadd_u32 },
                };
                genfn = fns[size][u];
                break;
            }
            case 0x4: /* SHSUB, UHSUB */
            {
                static NeonGenTwoOpFn * const fns[3][2] = {
                    { gen_helper_neon_hsub_s8, gen_helper_neon_hsub_u8 },
                    { gen_helper_neon_hsub_s16, gen_helper_neon_hsub_u16 },
                    { gen_helper_neon_hsub_s32, gen_helper_neon_hsub_u32 },
                };
                genfn = fns[size][u];
                break;
            }
            case 0x9: /* SQSHL, UQSHL */
            {
                static NeonGenTwoOpEnvFn * const fns[3][2] = {
                    { gen_helper_neon_qshl_s8, gen_helper_neon_qshl_u8 },
                    { gen_helper_neon_qshl_s16, gen_helper_neon_qshl_u16 },
                    { gen_helper_neon_qshl_s32, gen_helper_neon_qshl_u32 },
                };
                genenvfn = fns[size][u];
                break;
            }
            case 0xa: /* SRSHL, URSHL */
            {
                static NeonGenTwoOpFn * const fns[3][2] = {
                    { gen_helper_neon_rshl_s8, gen_helper_neon_rshl_u8 },
                    { gen_helper_neon_rshl_s16, gen_helper_neon_rshl_u16 },
                    { gen_helper_neon_rshl_s32, gen_helper_neon_rshl_u32 },
                };
                genfn = fns[size][u];
                break;
            }
            case 0xb: /* SQRSHL, UQRSHL */
            {
                static NeonGenTwoOpEnvFn * const fns[3][2] = {
                    { gen_helper_neon_qrshl_s8, gen_helper_neon_qrshl_u8 },
                    { gen_helper_neon_qrshl_s16, gen_helper_neon_qrshl_u16 },
                    { gen_helper_neon_qrshl_s32, gen_helper_neon_qrshl_u32 },
                };
                genenvfn = fns[size][u];
                break;
            }
            default:
                g_assert_not_reached();
            }

            if (genenvfn) {
                genenvfn(tcg_res, cpu_env, tcg_op1, tcg_op2);
            } else {
                genfn(tcg_res, tcg_op1, tcg_op2);
            }

            write_vec_element_i32(s, tcg_res, rd, pass, MO_32);

            tcg_temp_free_i32(tcg_res);
            tcg_temp_free_i32(tcg_op1);
            tcg_temp_free_i32(tcg_op2);
        }
    }
    clear_vec_high(s, is_q, rd);
}

/* AdvSIMD three same
 *  31  30  29  28       24 23  22  21 20  16 15    11  10 9    5 4    0
 * +---+---+---+-----------+------+---+------+--------+---+------+------+
 * | 0 | Q | U | 0 1 1 1 0 | size | 1 |  Rm  | opcode | 1 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+------+--------+---+------+------+
 */
static void disas_simd_three_reg_same(DisasContext *s, uint32_t insn)
{
    int opcode = extract32(insn, 11, 5);

    switch (opcode) {
    case 0x3: /* logic ops */
        disas_simd_3same_logic(s, insn);
        break;
    case 0x17: /* ADDP */
    case 0x14: /* SMAXP, UMAXP */
    case 0x15: /* SMINP, UMINP */
    {
        /* Pairwise operations */
        int is_q = extract32(insn, 30, 1);
        int u = extract32(insn, 29, 1);
        int size = extract32(insn, 22, 2);
        int rm = extract32(insn, 16, 5);
        int rn = extract32(insn, 5, 5);
        int rd = extract32(insn, 0, 5);
        if (opcode == 0x17) {
            if (u || (size == 3 && !is_q)) {
                unallocated_encoding(s);
                return;
            }
        } else {
            if (size == 3) {
                unallocated_encoding(s);
                return;
            }
        }
        handle_simd_3same_pair(s, is_q, u, opcode, size, rn, rm, rd);
        break;
    }
    case 0x18 ... 0x31:
        /* floating point ops, sz[1] and U are part of opcode */
        disas_simd_3same_float(s, insn);
        break;
    default:
        disas_simd_3same_int(s, insn);
        break;
    }
}

/*
 * Advanced SIMD three same (ARMv8.2 FP16 variants)
 *
 *  31  30  29  28       24 23  22 21 20  16 15 14 13    11 10  9    5 4    0
 * +---+---+---+-----------+---------+------+-----+--------+---+------+------+
 * | 0 | Q | U | 0 1 1 1 0 | a | 1 0 |  Rm  | 0 0 | opcode | 1 |  Rn  |  Rd  |
 * +---+---+---+-----------+---------+------+-----+--------+---+------+------+
 *
 * This includes FMULX, FCMEQ (register), FRECPS, FRSQRTS, FCMGE
 * (register), FACGE, FABD, FCMGT (register) and FACGT.
 *
 */
static void disas_simd_three_reg_same_fp16(DisasContext *s, uint32_t insn)
{
    int opcode, fpopcode;
    int is_q, u, a, rm, rn, rd;
    int datasize, elements;
    int pass;
    TCGv_ptr fpst;
    bool pairwise = false;

    if (!dc_isar_feature(aa64_fp16, s)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    /* For these floating point ops, the U, a and opcode bits
     * together indicate the operation.
     */
    opcode = extract32(insn, 11, 3);
    u = extract32(insn, 29, 1);
    a = extract32(insn, 23, 1);
    is_q = extract32(insn, 30, 1);
    rm = extract32(insn, 16, 5);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    fpopcode = opcode | (a << 3) |  (u << 4);
    datasize = is_q ? 128 : 64;
    elements = datasize / 16;

    switch (fpopcode) {
    case 0x10: /* FMAXNMP */
    case 0x12: /* FADDP */
    case 0x16: /* FMAXP */
    case 0x18: /* FMINNMP */
    case 0x1e: /* FMINP */
        pairwise = true;
        break;
    }

    fpst = fpstatus_ptr(FPST_FPCR_F16);

    if (pairwise) {
        int maxpass = is_q ? 8 : 4;
        TCGv_i32 tcg_op1 = tcg_temp_new_i32();
        TCGv_i32 tcg_op2 = tcg_temp_new_i32();
        TCGv_i32 tcg_res[8];

        for (pass = 0; pass < maxpass; pass++) {
            int passreg = pass < (maxpass / 2) ? rn : rm;
            int passelt = (pass << 1) & (maxpass - 1);

            read_vec_element_i32(s, tcg_op1, passreg, passelt, MO_16);
            read_vec_element_i32(s, tcg_op2, passreg, passelt + 1, MO_16);
            tcg_res[pass] = tcg_temp_new_i32();

            switch (fpopcode) {
            case 0x10: /* FMAXNMP */
                gen_helper_advsimd_maxnumh(tcg_res[pass], tcg_op1, tcg_op2,
                                           fpst);
                break;
            case 0x12: /* FADDP */
                gen_helper_advsimd_addh(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x16: /* FMAXP */
                gen_helper_advsimd_maxh(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            case 0x18: /* FMINNMP */
                gen_helper_advsimd_minnumh(tcg_res[pass], tcg_op1, tcg_op2,
                                           fpst);
                break;
            case 0x1e: /* FMINP */
                gen_helper_advsimd_minh(tcg_res[pass], tcg_op1, tcg_op2, fpst);
                break;
            default:
                g_assert_not_reached();
            }
        }

        for (pass = 0; pass < maxpass; pass++) {
            write_vec_element_i32(s, tcg_res[pass], rd, pass, MO_16);
            tcg_temp_free_i32(tcg_res[pass]);
        }

        tcg_temp_free_i32(tcg_op1);
        tcg_temp_free_i32(tcg_op2);

    } else {
        for (pass = 0; pass < elements; pass++) {
            TCGv_i32 tcg_op1 = tcg_temp_new_i32();
            TCGv_i32 tcg_op2 = tcg_temp_new_i32();
            TCGv_i32 tcg_res = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op1, rn, pass, MO_16);
            read_vec_element_i32(s, tcg_op2, rm, pass, MO_16);

            switch (fpopcode) {
            case 0x0: /* FMAXNM */
                gen_helper_advsimd_maxnumh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1: /* FMLA */
                read_vec_element_i32(s, tcg_res, rd, pass, MO_16);
                gen_helper_advsimd_muladdh(tcg_res, tcg_op1, tcg_op2, tcg_res,
                                           fpst);
                break;
            case 0x2: /* FADD */
                gen_helper_advsimd_addh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x3: /* FMULX */
                gen_helper_advsimd_mulxh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x4: /* FCMEQ */
                gen_helper_advsimd_ceq_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x6: /* FMAX */
                gen_helper_advsimd_maxh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x7: /* FRECPS */
                gen_helper_recpsf_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x8: /* FMINNM */
                gen_helper_advsimd_minnumh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x9: /* FMLS */
                /* As usual for ARM, separate negation for fused multiply-add */
                tcg_gen_xori_i32(tcg_op1, tcg_op1, 0x8000);
                read_vec_element_i32(s, tcg_res, rd, pass, MO_16);
                gen_helper_advsimd_muladdh(tcg_res, tcg_op1, tcg_op2, tcg_res,
                                           fpst);
                break;
            case 0xa: /* FSUB */
                gen_helper_advsimd_subh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0xe: /* FMIN */
                gen_helper_advsimd_minh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0xf: /* FRSQRTS */
                gen_helper_rsqrtsf_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x13: /* FMUL */
                gen_helper_advsimd_mulh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x14: /* FCMGE */
                gen_helper_advsimd_cge_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x15: /* FACGE */
                gen_helper_advsimd_acge_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x17: /* FDIV */
                gen_helper_advsimd_divh(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1a: /* FABD */
                gen_helper_advsimd_subh(tcg_res, tcg_op1, tcg_op2, fpst);
                tcg_gen_andi_i32(tcg_res, tcg_res, 0x7fff);
                break;
            case 0x1c: /* FCMGT */
                gen_helper_advsimd_cgt_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            case 0x1d: /* FACGT */
                gen_helper_advsimd_acgt_f16(tcg_res, tcg_op1, tcg_op2, fpst);
                break;
            default:
                fprintf(stderr, "%s: insn 0x%04x, fpop 0x%2x @ 0x%" PRIx64 "\n",
                        __func__, insn, fpopcode, s->pc_curr);
                g_assert_not_reached();
            }

            write_vec_element_i32(s, tcg_res, rd, pass, MO_16);
            tcg_temp_free_i32(tcg_res);
            tcg_temp_free_i32(tcg_op1);
            tcg_temp_free_i32(tcg_op2);
        }
    }

    tcg_temp_free_ptr(fpst);

    clear_vec_high(s, is_q, rd);
}

/* AdvSIMD three same extra
 *  31   30  29 28       24 23  22  21 20  16  15 14    11  10 9  5 4  0
 * +---+---+---+-----------+------+---+------+---+--------+---+----+----+
 * | 0 | Q | U | 0 1 1 1 0 | size | 0 |  Rm  | 1 | opcode | 1 | Rn | Rd |
 * +---+---+---+-----------+------+---+------+---+--------+---+----+----+
 */
static void disas_simd_three_reg_same_extra(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int opcode = extract32(insn, 11, 4);
    int rm = extract32(insn, 16, 5);
    int size = extract32(insn, 22, 2);
    bool u = extract32(insn, 29, 1);
    bool is_q = extract32(insn, 30, 1);
    bool feature;
    int rot;

    switch (u * 16 + opcode) {
    case 0x10: /* SQRDMLAH (vector) */
    case 0x11: /* SQRDMLSH (vector) */
        if (size != 1 && size != 2) {
            unallocated_encoding(s);
            return;
        }
        feature = dc_isar_feature(aa64_rdm, s);
        break;
    case 0x02: /* SDOT (vector) */
    case 0x12: /* UDOT (vector) */
        if (size != MO_32) {
            unallocated_encoding(s);
            return;
        }
        feature = dc_isar_feature(aa64_dp, s);
        break;
    case 0x18: /* FCMLA, #0 */
    case 0x19: /* FCMLA, #90 */
    case 0x1a: /* FCMLA, #180 */
    case 0x1b: /* FCMLA, #270 */
    case 0x1c: /* FCADD, #90 */
    case 0x1e: /* FCADD, #270 */
        if (size == 0
            || (size == 1 && !dc_isar_feature(aa64_fp16, s))
            || (size == 3 && !is_q)) {
            unallocated_encoding(s);
            return;
        }
        feature = dc_isar_feature(aa64_fcma, s);
        break;
    default:
        unallocated_encoding(s);
        return;
    }
    if (!feature) {
        unallocated_encoding(s);
        return;
    }
    if (!fp_access_check(s)) {
        return;
    }

    switch (opcode) {
    case 0x0: /* SQRDMLAH (vector) */
        gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_sqrdmlah_qc, size);
        return;

    case 0x1: /* SQRDMLSH (vector) */
        gen_gvec_fn3(s, is_q, rd, rn, rm, gen_gvec_sqrdmlsh_qc, size);
        return;

    case 0x2: /* SDOT / UDOT */
        gen_gvec_op3_ool(s, is_q, rd, rn, rm, 0,
                         u ? gen_helper_gvec_udot_b : gen_helper_gvec_sdot_b);
        return;

    case 0x8: /* FCMLA, #0 */
    case 0x9: /* FCMLA, #90 */
    case 0xa: /* FCMLA, #180 */
    case 0xb: /* FCMLA, #270 */
        rot = extract32(opcode, 0, 2);
        switch (size) {
        case 1:
            gen_gvec_op3_fpst(s, is_q, rd, rn, rm, true, rot,
                              gen_helper_gvec_fcmlah);
            break;
        case 2:
            gen_gvec_op3_fpst(s, is_q, rd, rn, rm, false, rot,
                              gen_helper_gvec_fcmlas);
            break;
        case 3:
            gen_gvec_op3_fpst(s, is_q, rd, rn, rm, false, rot,
                              gen_helper_gvec_fcmlad);
            break;
        default:
            g_assert_not_reached();
        }
        return;

    case 0xc: /* FCADD, #90 */
    case 0xe: /* FCADD, #270 */
        rot = extract32(opcode, 1, 1);
        switch (size) {
        case 1:
            gen_gvec_op3_fpst(s, is_q, rd, rn, rm, size == 1, rot,
                              gen_helper_gvec_fcaddh);
            break;
        case 2:
            gen_gvec_op3_fpst(s, is_q, rd, rn, rm, size == 1, rot,
                              gen_helper_gvec_fcadds);
            break;
        case 3:
            gen_gvec_op3_fpst(s, is_q, rd, rn, rm, size == 1, rot,
                              gen_helper_gvec_fcaddd);
            break;
        default:
            g_assert_not_reached();
        }
        return;

    default:
        g_assert_not_reached();
    }
}

static void handle_2misc_widening(DisasContext *s, int opcode, bool is_q,
                                  int size, int rn, int rd)
{
    /* Handle 2-reg-misc ops which are widening (so each size element
     * in the source becomes a 2*size element in the destination.
     * The only instruction like this is FCVTL.
     */
    int pass;

    if (size == 3) {
        /* 32 -> 64 bit fp conversion */
        TCGv_i64 tcg_res[2];
        int srcelt = is_q ? 2 : 0;

        for (pass = 0; pass < 2; pass++) {
            TCGv_i32 tcg_op = tcg_temp_new_i32();
            tcg_res[pass] = tcg_temp_new_i64();

            read_vec_element_i32(s, tcg_op, rn, srcelt + pass, MO_32);
            gen_helper_vfp_fcvtds(tcg_res[pass], tcg_op, cpu_env);
            tcg_temp_free_i32(tcg_op);
        }
        for (pass = 0; pass < 2; pass++) {
            write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
            tcg_temp_free_i64(tcg_res[pass]);
        }
    } else {
        /* 16 -> 32 bit fp conversion */
        int srcelt = is_q ? 4 : 0;
        TCGv_i32 tcg_res[4];
        TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);
        TCGv_i32 ahp = get_ahp_flag();

        for (pass = 0; pass < 4; pass++) {
            tcg_res[pass] = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_res[pass], rn, srcelt + pass, MO_16);
            gen_helper_vfp_fcvt_f16_to_f32(tcg_res[pass], tcg_res[pass],
                                           fpst, ahp);
        }
        for (pass = 0; pass < 4; pass++) {
            write_vec_element_i32(s, tcg_res[pass], rd, pass, MO_32);
            tcg_temp_free_i32(tcg_res[pass]);
        }

        tcg_temp_free_ptr(fpst);
        tcg_temp_free_i32(ahp);
    }
}

static void handle_rev(DisasContext *s, int opcode, bool u,
                       bool is_q, int size, int rn, int rd)
{
    int op = (opcode << 1) | u;
    int opsz = op + size;
    int grp_size = 3 - opsz;
    int dsize = is_q ? 128 : 64;
    int i;

    if (opsz >= 3) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (size == 0) {
        /* Special case bytes, use bswap op on each group of elements */
        int groups = dsize / (8 << grp_size);

        for (i = 0; i < groups; i++) {
            TCGv_i64 tcg_tmp = tcg_temp_new_i64();

            read_vec_element(s, tcg_tmp, rn, i, grp_size);
            switch (grp_size) {
            case MO_16:
                tcg_gen_bswap16_i64(tcg_tmp, tcg_tmp);
                break;
            case MO_32:
                tcg_gen_bswap32_i64(tcg_tmp, tcg_tmp);
                break;
            case MO_64:
                tcg_gen_bswap64_i64(tcg_tmp, tcg_tmp);
                break;
            default:
                g_assert_not_reached();
            }
            write_vec_element(s, tcg_tmp, rd, i, grp_size);
            tcg_temp_free_i64(tcg_tmp);
        }
        clear_vec_high(s, is_q, rd);
    } else {
        int revmask = (1 << grp_size) - 1;
        int esize = 8 << size;
        int elements = dsize / esize;
        TCGv_i64 tcg_rn = tcg_temp_new_i64();
        TCGv_i64 tcg_rd = tcg_const_i64(0);
        TCGv_i64 tcg_rd_hi = tcg_const_i64(0);

        for (i = 0; i < elements; i++) {
            int e_rev = (i & 0xf) ^ revmask;
            int off = e_rev * esize;
            read_vec_element(s, tcg_rn, rn, i, size);
            if (off >= 64) {
                tcg_gen_deposit_i64(tcg_rd_hi, tcg_rd_hi,
                                    tcg_rn, off - 64, esize);
            } else {
                tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_rn, off, esize);
            }
        }
        write_vec_element(s, tcg_rd, rd, 0, MO_64);
        write_vec_element(s, tcg_rd_hi, rd, 1, MO_64);

        tcg_temp_free_i64(tcg_rd_hi);
        tcg_temp_free_i64(tcg_rd);
        tcg_temp_free_i64(tcg_rn);
    }
}

static void handle_2misc_pairwise(DisasContext *s, int opcode, bool u,
                                  bool is_q, int size, int rn, int rd)
{
    /* Implement the pairwise operations from 2-misc:
     * SADDLP, UADDLP, SADALP, UADALP.
     * These all add pairs of elements in the input to produce a
     * double-width result element in the output (possibly accumulating).
     */
    bool accum = (opcode == 0x6);
    int maxpass = is_q ? 2 : 1;
    int pass;
    TCGv_i64 tcg_res[2];

    if (size == 2) {
        /* 32 + 32 -> 64 op */
        MemOp memop = size + (u ? 0 : MO_SIGN);

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i64 tcg_op1 = tcg_temp_new_i64();
            TCGv_i64 tcg_op2 = tcg_temp_new_i64();

            tcg_res[pass] = tcg_temp_new_i64();

            read_vec_element(s, tcg_op1, rn, pass * 2, memop);
            read_vec_element(s, tcg_op2, rn, pass * 2 + 1, memop);
            tcg_gen_add_i64(tcg_res[pass], tcg_op1, tcg_op2);
            if (accum) {
                read_vec_element(s, tcg_op1, rd, pass, MO_64);
                tcg_gen_add_i64(tcg_res[pass], tcg_res[pass], tcg_op1);
            }

            tcg_temp_free_i64(tcg_op1);
            tcg_temp_free_i64(tcg_op2);
        }
    } else {
        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i64 tcg_op = tcg_temp_new_i64();
            NeonGenOne64OpFn *genfn;
            static NeonGenOne64OpFn * const fns[2][2] = {
                { gen_helper_neon_addlp_s8,  gen_helper_neon_addlp_u8 },
                { gen_helper_neon_addlp_s16,  gen_helper_neon_addlp_u16 },
            };

            genfn = fns[size][u];

            tcg_res[pass] = tcg_temp_new_i64();

            read_vec_element(s, tcg_op, rn, pass, MO_64);
            genfn(tcg_res[pass], tcg_op);

            if (accum) {
                read_vec_element(s, tcg_op, rd, pass, MO_64);
                if (size == 0) {
                    gen_helper_neon_addl_u16(tcg_res[pass],
                                             tcg_res[pass], tcg_op);
                } else {
                    gen_helper_neon_addl_u32(tcg_res[pass],
                                             tcg_res[pass], tcg_op);
                }
            }
            tcg_temp_free_i64(tcg_op);
        }
    }
    if (!is_q) {
        tcg_res[1] = tcg_const_i64(0);
    }
    for (pass = 0; pass < 2; pass++) {
        write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
        tcg_temp_free_i64(tcg_res[pass]);
    }
}

static void handle_shll(DisasContext *s, bool is_q, int size, int rn, int rd)
{
    /* Implement SHLL and SHLL2 */
    int pass;
    int part = is_q ? 2 : 0;
    TCGv_i64 tcg_res[2];

    for (pass = 0; pass < 2; pass++) {
        static NeonGenWidenFn * const widenfns[3] = {
            gen_helper_neon_widen_u8,
            gen_helper_neon_widen_u16,
            tcg_gen_extu_i32_i64,
        };
        NeonGenWidenFn *widenfn = widenfns[size];
        TCGv_i32 tcg_op = tcg_temp_new_i32();

        read_vec_element_i32(s, tcg_op, rn, part + pass, MO_32);
        tcg_res[pass] = tcg_temp_new_i64();
        widenfn(tcg_res[pass], tcg_op);
        tcg_gen_shli_i64(tcg_res[pass], tcg_res[pass], 8 << size);

        tcg_temp_free_i32(tcg_op);
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
        tcg_temp_free_i64(tcg_res[pass]);
    }
}

/* AdvSIMD two reg misc
 *   31  30  29 28       24 23  22 21       17 16    12 11 10 9    5 4    0
 * +---+---+---+-----------+------+-----------+--------+-----+------+------+
 * | 0 | Q | U | 0 1 1 1 0 | size | 1 0 0 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+-----------+--------+-----+------+------+
 */
static void disas_simd_two_reg_misc(DisasContext *s, uint32_t insn)
{
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 5);
    bool u = extract32(insn, 29, 1);
    bool is_q = extract32(insn, 30, 1);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    bool need_fpstatus = false;
    bool need_rmode = false;
    int rmode = -1;
    TCGv_i32 tcg_rmode;
    TCGv_ptr tcg_fpstatus;

    switch (opcode) {
    case 0x0: /* REV64, REV32 */
    case 0x1: /* REV16 */
        handle_rev(s, opcode, u, is_q, size, rn, rd);
        return;
    case 0x5: /* CNT, NOT, RBIT */
        if (u && size == 0) {
            /* NOT */
            break;
        } else if (u && size == 1) {
            /* RBIT */
            break;
        } else if (!u && size == 0) {
            /* CNT */
            break;
        }
        unallocated_encoding(s);
        return;
    case 0x12: /* XTN, XTN2, SQXTUN, SQXTUN2 */
    case 0x14: /* SQXTN, SQXTN2, UQXTN, UQXTN2 */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }

        handle_2misc_narrow(s, false, opcode, u, is_q, size, rn, rd);
        return;
    case 0x4: /* CLS, CLZ */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x2: /* SADDLP, UADDLP */
    case 0x6: /* SADALP, UADALP */
        if (size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_2misc_pairwise(s, opcode, u, is_q, size, rn, rd);
        return;
    case 0x13: /* SHLL, SHLL2 */
        if (u == 0 || size == 3) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_shll(s, is_q, size, rn, rd);
        return;
    case 0xa: /* CMLT */
        if (u == 1) {
            unallocated_encoding(s);
            return;
        }
        /* fall through */
    case 0x8: /* CMGT, CMGE */
    case 0x9: /* CMEQ, CMLE */
    case 0xb: /* ABS, NEG */
        if (size == 3 && !is_q) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x3: /* SUQADD, USQADD */
        if (size == 3 && !is_q) {
            unallocated_encoding(s);
            return;
        }
        if (!fp_access_check(s)) {
            return;
        }
        handle_2misc_satacc(s, false, u, is_q, size, rn, rd);
        return;
    case 0x7: /* SQABS, SQNEG */
        if (size == 3 && !is_q) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0xc ... 0xf:
    case 0x16 ... 0x1f:
    {
        /* Floating point: U, size[1] and opcode indicate operation;
         * size[0] indicates single or double precision.
         */
        int is_double = extract32(size, 0, 1);
        opcode |= (extract32(size, 1, 1) << 5) | (u << 6);
        size = is_double ? 3 : 2;
        switch (opcode) {
        case 0x2f: /* FABS */
        case 0x6f: /* FNEG */
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x1d: /* SCVTF */
        case 0x5d: /* UCVTF */
        {
            bool is_signed = (opcode == 0x1d) ? true : false;
            int elements = is_double ? 2 : is_q ? 4 : 2;
            if (is_double && !is_q) {
                unallocated_encoding(s);
                return;
            }
            if (!fp_access_check(s)) {
                return;
            }
            handle_simd_intfp_conv(s, rd, rn, elements, is_signed, 0, size);
            return;
        }
        case 0x2c: /* FCMGT (zero) */
        case 0x2d: /* FCMEQ (zero) */
        case 0x2e: /* FCMLT (zero) */
        case 0x6c: /* FCMGE (zero) */
        case 0x6d: /* FCMLE (zero) */
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            handle_2misc_fcmp_zero(s, opcode, false, u, is_q, size, rn, rd);
            return;
        case 0x7f: /* FSQRT */
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x1a: /* FCVTNS */
        case 0x1b: /* FCVTMS */
        case 0x3a: /* FCVTPS */
        case 0x3b: /* FCVTZS */
        case 0x5a: /* FCVTNU */
        case 0x5b: /* FCVTMU */
        case 0x7a: /* FCVTPU */
        case 0x7b: /* FCVTZU */
            need_fpstatus = true;
            need_rmode = true;
            rmode = extract32(opcode, 5, 1) | (extract32(opcode, 0, 1) << 1);
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x5c: /* FCVTAU */
        case 0x1c: /* FCVTAS */
            need_fpstatus = true;
            need_rmode = true;
            rmode = FPROUNDING_TIEAWAY;
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x3c: /* URECPE */
            if (size == 3) {
                unallocated_encoding(s);
                return;
            }
            /* fall through */
        case 0x3d: /* FRECPE */
        case 0x7d: /* FRSQRTE */
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            if (!fp_access_check(s)) {
                return;
            }
            handle_2misc_reciprocal(s, opcode, false, u, is_q, size, rn, rd);
            return;
        case 0x56: /* FCVTXN, FCVTXN2 */
            if (size == 2) {
                unallocated_encoding(s);
                return;
            }
            /* fall through */
        case 0x16: /* FCVTN, FCVTN2 */
            /* handle_2misc_narrow does a 2*size -> size operation, but these
             * instructions encode the source size rather than dest size.
             */
            if (!fp_access_check(s)) {
                return;
            }
            handle_2misc_narrow(s, false, opcode, 0, is_q, size - 1, rn, rd);
            return;
        case 0x17: /* FCVTL, FCVTL2 */
            if (!fp_access_check(s)) {
                return;
            }
            handle_2misc_widening(s, opcode, is_q, size, rn, rd);
            return;
        case 0x18: /* FRINTN */
        case 0x19: /* FRINTM */
        case 0x38: /* FRINTP */
        case 0x39: /* FRINTZ */
            need_rmode = true;
            rmode = extract32(opcode, 5, 1) | (extract32(opcode, 0, 1) << 1);
            /* fall through */
        case 0x59: /* FRINTX */
        case 0x79: /* FRINTI */
            need_fpstatus = true;
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x58: /* FRINTA */
            need_rmode = true;
            rmode = FPROUNDING_TIEAWAY;
            need_fpstatus = true;
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x7c: /* URSQRTE */
            if (size == 3) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x1e: /* FRINT32Z */
        case 0x1f: /* FRINT64Z */
            need_rmode = true;
            rmode = FPROUNDING_ZERO;
            /* fall through */
        case 0x5e: /* FRINT32X */
        case 0x5f: /* FRINT64X */
            need_fpstatus = true;
            if ((size == 3 && !is_q) || !dc_isar_feature(aa64_frint, s)) {
                unallocated_encoding(s);
                return;
            }
            break;
        default:
            unallocated_encoding(s);
            return;
        }
        break;
    }
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (need_fpstatus || need_rmode) {
        tcg_fpstatus = fpstatus_ptr(FPST_FPCR);
    } else {
        tcg_fpstatus = NULL;
    }
    if (need_rmode) {
        tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
    } else {
        tcg_rmode = NULL;
    }

    switch (opcode) {
    case 0x5:
        if (u && size == 0) { /* NOT */
            gen_gvec_fn2(s, is_q, rd, rn, tcg_gen_gvec_not, 0);
            return;
        }
        break;
    case 0x8: /* CMGT, CMGE */
        if (u) {
            gen_gvec_fn2(s, is_q, rd, rn, gen_gvec_cge0, size);
        } else {
            gen_gvec_fn2(s, is_q, rd, rn, gen_gvec_cgt0, size);
        }
        return;
    case 0x9: /* CMEQ, CMLE */
        if (u) {
            gen_gvec_fn2(s, is_q, rd, rn, gen_gvec_cle0, size);
        } else {
            gen_gvec_fn2(s, is_q, rd, rn, gen_gvec_ceq0, size);
        }
        return;
    case 0xa: /* CMLT */
        gen_gvec_fn2(s, is_q, rd, rn, gen_gvec_clt0, size);
        return;
    case 0xb:
        if (u) { /* ABS, NEG */
            gen_gvec_fn2(s, is_q, rd, rn, tcg_gen_gvec_neg, size);
        } else {
            gen_gvec_fn2(s, is_q, rd, rn, tcg_gen_gvec_abs, size);
        }
        return;
    }

    if (size == 3) {
        /* All 64-bit element operations can be shared with scalar 2misc */
        int pass;

        /* Coverity claims (size == 3 && !is_q) has been eliminated
         * from all paths leading to here.
         */
        tcg_debug_assert(is_q);
        for (pass = 0; pass < 2; pass++) {
            TCGv_i64 tcg_op = tcg_temp_new_i64();
            TCGv_i64 tcg_res = tcg_temp_new_i64();

            read_vec_element(s, tcg_op, rn, pass, MO_64);

            handle_2misc_64(s, opcode, u, tcg_res, tcg_op,
                            tcg_rmode, tcg_fpstatus);

            write_vec_element(s, tcg_res, rd, pass, MO_64);

            tcg_temp_free_i64(tcg_res);
            tcg_temp_free_i64(tcg_op);
        }
    } else {
        int pass;

        for (pass = 0; pass < (is_q ? 4 : 2); pass++) {
            TCGv_i32 tcg_op = tcg_temp_new_i32();
            TCGv_i32 tcg_res = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op, rn, pass, MO_32);

            if (size == 2) {
                /* Special cases for 32 bit elements */
                switch (opcode) {
                case 0x4: /* CLS */
                    if (u) {
                        tcg_gen_clzi_i32(tcg_res, tcg_op, 32);
                    } else {
                        tcg_gen_clrsb_i32(tcg_res, tcg_op);
                    }
                    break;
                case 0x7: /* SQABS, SQNEG */
                    if (u) {
                        gen_helper_neon_qneg_s32(tcg_res, cpu_env, tcg_op);
                    } else {
                        gen_helper_neon_qabs_s32(tcg_res, cpu_env, tcg_op);
                    }
                    break;
                case 0x2f: /* FABS */
                    gen_helper_vfp_abss(tcg_res, tcg_op);
                    break;
                case 0x6f: /* FNEG */
                    gen_helper_vfp_negs(tcg_res, tcg_op);
                    break;
                case 0x7f: /* FSQRT */
                    gen_helper_vfp_sqrts(tcg_res, tcg_op, cpu_env);
                    break;
                case 0x1a: /* FCVTNS */
                case 0x1b: /* FCVTMS */
                case 0x1c: /* FCVTAS */
                case 0x3a: /* FCVTPS */
                case 0x3b: /* FCVTZS */
                {
                    TCGv_i32 tcg_shift = tcg_const_i32(0);
                    gen_helper_vfp_tosls(tcg_res, tcg_op,
                                         tcg_shift, tcg_fpstatus);
                    tcg_temp_free_i32(tcg_shift);
                    break;
                }
                case 0x5a: /* FCVTNU */
                case 0x5b: /* FCVTMU */
                case 0x5c: /* FCVTAU */
                case 0x7a: /* FCVTPU */
                case 0x7b: /* FCVTZU */
                {
                    TCGv_i32 tcg_shift = tcg_const_i32(0);
                    gen_helper_vfp_touls(tcg_res, tcg_op,
                                         tcg_shift, tcg_fpstatus);
                    tcg_temp_free_i32(tcg_shift);
                    break;
                }
                case 0x18: /* FRINTN */
                case 0x19: /* FRINTM */
                case 0x38: /* FRINTP */
                case 0x39: /* FRINTZ */
                case 0x58: /* FRINTA */
                case 0x79: /* FRINTI */
                    gen_helper_rints(tcg_res, tcg_op, tcg_fpstatus);
                    break;
                case 0x59: /* FRINTX */
                    gen_helper_rints_exact(tcg_res, tcg_op, tcg_fpstatus);
                    break;
                case 0x7c: /* URSQRTE */
                    gen_helper_rsqrte_u32(tcg_res, tcg_op);
                    break;
                case 0x1e: /* FRINT32Z */
                case 0x5e: /* FRINT32X */
                    gen_helper_frint32_s(tcg_res, tcg_op, tcg_fpstatus);
                    break;
                case 0x1f: /* FRINT64Z */
                case 0x5f: /* FRINT64X */
                    gen_helper_frint64_s(tcg_res, tcg_op, tcg_fpstatus);
                    break;
                default:
                    g_assert_not_reached();
                }
            } else {
                /* Use helpers for 8 and 16 bit elements */
                switch (opcode) {
                case 0x5: /* CNT, RBIT */
                    /* For these two insns size is part of the opcode specifier
                     * (handled earlier); they always operate on byte elements.
                     */
                    if (u) {
                        gen_helper_neon_rbit_u8(tcg_res, tcg_op);
                    } else {
                        gen_helper_neon_cnt_u8(tcg_res, tcg_op);
                    }
                    break;
                case 0x7: /* SQABS, SQNEG */
                {
                    NeonGenOneOpEnvFn *genfn;
                    static NeonGenOneOpEnvFn * const fns[2][2] = {
                        { gen_helper_neon_qabs_s8, gen_helper_neon_qneg_s8 },
                        { gen_helper_neon_qabs_s16, gen_helper_neon_qneg_s16 },
                    };
                    genfn = fns[size][u];
                    genfn(tcg_res, cpu_env, tcg_op);
                    break;
                }
                case 0x4: /* CLS, CLZ */
                    if (u) {
                        if (size == 0) {
                            gen_helper_neon_clz_u8(tcg_res, tcg_op);
                        } else {
                            gen_helper_neon_clz_u16(tcg_res, tcg_op);
                        }
                    } else {
                        if (size == 0) {
                            gen_helper_neon_cls_s8(tcg_res, tcg_op);
                        } else {
                            gen_helper_neon_cls_s16(tcg_res, tcg_op);
                        }
                    }
                    break;
                default:
                    g_assert_not_reached();
                }
            }

            write_vec_element_i32(s, tcg_res, rd, pass, MO_32);

            tcg_temp_free_i32(tcg_res);
            tcg_temp_free_i32(tcg_op);
        }
    }
    clear_vec_high(s, is_q, rd);

    if (need_rmode) {
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
        tcg_temp_free_i32(tcg_rmode);
    }
    if (need_fpstatus) {
        tcg_temp_free_ptr(tcg_fpstatus);
    }
}

/* AdvSIMD [scalar] two register miscellaneous (FP16)
 *
 *   31  30  29 28  27     24  23 22 21       17 16    12 11 10 9    5 4    0
 * +---+---+---+---+---------+---+-------------+--------+-----+------+------+
 * | 0 | Q | U | S | 1 1 1 0 | a | 1 1 1 1 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +---+---+---+---+---------+---+-------------+--------+-----+------+------+
 *   mask: 1000 1111 0111 1110 0000 1100 0000 0000 0x8f7e 0c00
 *   val:  0000 1110 0111 1000 0000 1000 0000 0000 0x0e78 0800
 *
 * This actually covers two groups where scalar access is governed by
 * bit 28. A bunch of the instructions (float to integral) only exist
 * in the vector form and are un-allocated for the scalar decode. Also
 * in the scalar decode Q is always 1.
 */
static void disas_simd_two_reg_misc_fp16(DisasContext *s, uint32_t insn)
{
    int fpop, opcode, a, u;
    int rn, rd;
    bool is_q;
    bool is_scalar;
    bool only_in_vector = false;

    int pass;
    TCGv_i32 tcg_rmode = NULL;
    TCGv_ptr tcg_fpstatus = NULL;
    bool need_rmode = false;
    bool need_fpst = true;
    int rmode;

    if (!dc_isar_feature(aa64_fp16, s)) {
        unallocated_encoding(s);
        return;
    }

    rd = extract32(insn, 0, 5);
    rn = extract32(insn, 5, 5);

    a = extract32(insn, 23, 1);
    u = extract32(insn, 29, 1);
    is_scalar = extract32(insn, 28, 1);
    is_q = extract32(insn, 30, 1);

    opcode = extract32(insn, 12, 5);
    fpop = deposit32(opcode, 5, 1, a);
    fpop = deposit32(fpop, 6, 1, u);

    switch (fpop) {
    case 0x1d: /* SCVTF */
    case 0x5d: /* UCVTF */
    {
        int elements;

        if (is_scalar) {
            elements = 1;
        } else {
            elements = (is_q ? 8 : 4);
        }

        if (!fp_access_check(s)) {
            return;
        }
        handle_simd_intfp_conv(s, rd, rn, elements, !u, 0, MO_16);
        return;
    }
    break;
    case 0x2c: /* FCMGT (zero) */
    case 0x2d: /* FCMEQ (zero) */
    case 0x2e: /* FCMLT (zero) */
    case 0x6c: /* FCMGE (zero) */
    case 0x6d: /* FCMLE (zero) */
        handle_2misc_fcmp_zero(s, fpop, is_scalar, 0, is_q, MO_16, rn, rd);
        return;
    case 0x3d: /* FRECPE */
    case 0x3f: /* FRECPX */
        break;
    case 0x18: /* FRINTN */
        need_rmode = true;
        only_in_vector = true;
        rmode = FPROUNDING_TIEEVEN;
        break;
    case 0x19: /* FRINTM */
        need_rmode = true;
        only_in_vector = true;
        rmode = FPROUNDING_NEGINF;
        break;
    case 0x38: /* FRINTP */
        need_rmode = true;
        only_in_vector = true;
        rmode = FPROUNDING_POSINF;
        break;
    case 0x39: /* FRINTZ */
        need_rmode = true;
        only_in_vector = true;
        rmode = FPROUNDING_ZERO;
        break;
    case 0x58: /* FRINTA */
        need_rmode = true;
        only_in_vector = true;
        rmode = FPROUNDING_TIEAWAY;
        break;
    case 0x59: /* FRINTX */
    case 0x79: /* FRINTI */
        only_in_vector = true;
        /* current rounding mode */
        break;
    case 0x1a: /* FCVTNS */
        need_rmode = true;
        rmode = FPROUNDING_TIEEVEN;
        break;
    case 0x1b: /* FCVTMS */
        need_rmode = true;
        rmode = FPROUNDING_NEGINF;
        break;
    case 0x1c: /* FCVTAS */
        need_rmode = true;
        rmode = FPROUNDING_TIEAWAY;
        break;
    case 0x3a: /* FCVTPS */
        need_rmode = true;
        rmode = FPROUNDING_POSINF;
        break;
    case 0x3b: /* FCVTZS */
        need_rmode = true;
        rmode = FPROUNDING_ZERO;
        break;
    case 0x5a: /* FCVTNU */
        need_rmode = true;
        rmode = FPROUNDING_TIEEVEN;
        break;
    case 0x5b: /* FCVTMU */
        need_rmode = true;
        rmode = FPROUNDING_NEGINF;
        break;
    case 0x5c: /* FCVTAU */
        need_rmode = true;
        rmode = FPROUNDING_TIEAWAY;
        break;
    case 0x7a: /* FCVTPU */
        need_rmode = true;
        rmode = FPROUNDING_POSINF;
        break;
    case 0x7b: /* FCVTZU */
        need_rmode = true;
        rmode = FPROUNDING_ZERO;
        break;
    case 0x2f: /* FABS */
    case 0x6f: /* FNEG */
        need_fpst = false;
        break;
    case 0x7d: /* FRSQRTE */
    case 0x7f: /* FSQRT (vector) */
        break;
    default:
        fprintf(stderr, "%s: insn 0x%04x fpop 0x%2x\n", __func__, insn, fpop);
        g_assert_not_reached();
    }


    /* Check additional constraints for the scalar encoding */
    if (is_scalar) {
        if (!is_q) {
            unallocated_encoding(s);
            return;
        }
        /* FRINTxx is only in the vector form */
        if (only_in_vector) {
            unallocated_encoding(s);
            return;
        }
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (need_rmode || need_fpst) {
        tcg_fpstatus = fpstatus_ptr(FPST_FPCR_F16);
    }

    if (need_rmode) {
        tcg_rmode = tcg_const_i32(arm_rmode_to_sf(rmode));
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
    }

    if (is_scalar) {
        TCGv_i32 tcg_op = read_fp_hreg(s, rn);
        TCGv_i32 tcg_res = tcg_temp_new_i32();

        switch (fpop) {
        case 0x1a: /* FCVTNS */
        case 0x1b: /* FCVTMS */
        case 0x1c: /* FCVTAS */
        case 0x3a: /* FCVTPS */
        case 0x3b: /* FCVTZS */
            gen_helper_advsimd_f16tosinth(tcg_res, tcg_op, tcg_fpstatus);
            break;
        case 0x3d: /* FRECPE */
            gen_helper_recpe_f16(tcg_res, tcg_op, tcg_fpstatus);
            break;
        case 0x3f: /* FRECPX */
            gen_helper_frecpx_f16(tcg_res, tcg_op, tcg_fpstatus);
            break;
        case 0x5a: /* FCVTNU */
        case 0x5b: /* FCVTMU */
        case 0x5c: /* FCVTAU */
        case 0x7a: /* FCVTPU */
        case 0x7b: /* FCVTZU */
            gen_helper_advsimd_f16touinth(tcg_res, tcg_op, tcg_fpstatus);
            break;
        case 0x6f: /* FNEG */
            tcg_gen_xori_i32(tcg_res, tcg_op, 0x8000);
            break;
        case 0x7d: /* FRSQRTE */
            gen_helper_rsqrte_f16(tcg_res, tcg_op, tcg_fpstatus);
            break;
        default:
            g_assert_not_reached();
        }

        /* limit any sign extension going on */
        tcg_gen_andi_i32(tcg_res, tcg_res, 0xffff);
        write_fp_sreg(s, rd, tcg_res);

        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_op);
    } else {
        for (pass = 0; pass < (is_q ? 8 : 4); pass++) {
            TCGv_i32 tcg_op = tcg_temp_new_i32();
            TCGv_i32 tcg_res = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op, rn, pass, MO_16);

            switch (fpop) {
            case 0x1a: /* FCVTNS */
            case 0x1b: /* FCVTMS */
            case 0x1c: /* FCVTAS */
            case 0x3a: /* FCVTPS */
            case 0x3b: /* FCVTZS */
                gen_helper_advsimd_f16tosinth(tcg_res, tcg_op, tcg_fpstatus);
                break;
            case 0x3d: /* FRECPE */
                gen_helper_recpe_f16(tcg_res, tcg_op, tcg_fpstatus);
                break;
            case 0x5a: /* FCVTNU */
            case 0x5b: /* FCVTMU */
            case 0x5c: /* FCVTAU */
            case 0x7a: /* FCVTPU */
            case 0x7b: /* FCVTZU */
                gen_helper_advsimd_f16touinth(tcg_res, tcg_op, tcg_fpstatus);
                break;
            case 0x18: /* FRINTN */
            case 0x19: /* FRINTM */
            case 0x38: /* FRINTP */
            case 0x39: /* FRINTZ */
            case 0x58: /* FRINTA */
            case 0x79: /* FRINTI */
                gen_helper_advsimd_rinth(tcg_res, tcg_op, tcg_fpstatus);
                break;
            case 0x59: /* FRINTX */
                gen_helper_advsimd_rinth_exact(tcg_res, tcg_op, tcg_fpstatus);
                break;
            case 0x2f: /* FABS */
                tcg_gen_andi_i32(tcg_res, tcg_op, 0x7fff);
                break;
            case 0x6f: /* FNEG */
                tcg_gen_xori_i32(tcg_res, tcg_op, 0x8000);
                break;
            case 0x7d: /* FRSQRTE */
                gen_helper_rsqrte_f16(tcg_res, tcg_op, tcg_fpstatus);
                break;
            case 0x7f: /* FSQRT */
                gen_helper_sqrt_f16(tcg_res, tcg_op, tcg_fpstatus);
                break;
            default:
                g_assert_not_reached();
            }

            write_vec_element_i32(s, tcg_res, rd, pass, MO_16);

            tcg_temp_free_i32(tcg_res);
            tcg_temp_free_i32(tcg_op);
        }

        clear_vec_high(s, is_q, rd);
    }

    if (tcg_rmode) {
        gen_helper_set_rmode(tcg_rmode, tcg_rmode, tcg_fpstatus);
        tcg_temp_free_i32(tcg_rmode);
    }

    if (tcg_fpstatus) {
        tcg_temp_free_ptr(tcg_fpstatus);
    }
}

/* AdvSIMD scalar x indexed element
 *  31 30  29 28       24 23  22 21  20  19  16 15 12  11  10 9    5 4    0
 * +-----+---+-----------+------+---+---+------+-----+---+---+------+------+
 * | 0 1 | U | 1 1 1 1 1 | size | L | M |  Rm  | opc | H | 0 |  Rn  |  Rd  |
 * +-----+---+-----------+------+---+---+------+-----+---+---+------+------+
 * AdvSIMD vector x indexed element
 *   31  30  29 28       24 23  22 21  20  19  16 15 12  11  10 9    5 4    0
 * +---+---+---+-----------+------+---+---+------+-----+---+---+------+------+
 * | 0 | Q | U | 0 1 1 1 1 | size | L | M |  Rm  | opc | H | 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+---+------+-----+---+---+------+------+
 */
static void disas_simd_indexed(DisasContext *s, uint32_t insn)
{
    /* This encoding has two kinds of instruction:
     *  normal, where we perform elt x idxelt => elt for each
     *     element in the vector
     *  long, where we perform elt x idxelt and generate a result of
     *     double the width of the input element
     * The long ops have a 'part' specifier (ie come in INSN, INSN2 pairs).
     */
    bool is_scalar = extract32(insn, 28, 1);
    bool is_q = extract32(insn, 30, 1);
    bool u = extract32(insn, 29, 1);
    int size = extract32(insn, 22, 2);
    int l = extract32(insn, 21, 1);
    int m = extract32(insn, 20, 1);
    /* Note that the Rm field here is only 4 bits, not 5 as it usually is */
    int rm = extract32(insn, 16, 4);
    int opcode = extract32(insn, 12, 4);
    int h = extract32(insn, 11, 1);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    bool is_long = false;
    int is_fp = 0;
    bool is_fp16 = false;
    int index;
    TCGv_ptr fpst;

    switch (16 * u + opcode) {
    case 0x08: /* MUL */
    case 0x10: /* MLA */
    case 0x14: /* MLS */
        if (is_scalar) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x02: /* SMLAL, SMLAL2 */
    case 0x12: /* UMLAL, UMLAL2 */
    case 0x06: /* SMLSL, SMLSL2 */
    case 0x16: /* UMLSL, UMLSL2 */
    case 0x0a: /* SMULL, SMULL2 */
    case 0x1a: /* UMULL, UMULL2 */
        if (is_scalar) {
            unallocated_encoding(s);
            return;
        }
        is_long = true;
        break;
    case 0x03: /* SQDMLAL, SQDMLAL2 */
    case 0x07: /* SQDMLSL, SQDMLSL2 */
    case 0x0b: /* SQDMULL, SQDMULL2 */
        is_long = true;
        break;
    case 0x0c: /* SQDMULH */
    case 0x0d: /* SQRDMULH */
        break;
    case 0x01: /* FMLA */
    case 0x05: /* FMLS */
    case 0x09: /* FMUL */
    case 0x19: /* FMULX */
        is_fp = 1;
        break;
    case 0x1d: /* SQRDMLAH */
    case 0x1f: /* SQRDMLSH */
        if (!dc_isar_feature(aa64_rdm, s)) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x0e: /* SDOT */
    case 0x1e: /* UDOT */
        if (is_scalar || size != MO_32 || !dc_isar_feature(aa64_dp, s)) {
            unallocated_encoding(s);
            return;
        }
        break;
    case 0x11: /* FCMLA #0 */
    case 0x13: /* FCMLA #90 */
    case 0x15: /* FCMLA #180 */
    case 0x17: /* FCMLA #270 */
        if (is_scalar || !dc_isar_feature(aa64_fcma, s)) {
            unallocated_encoding(s);
            return;
        }
        is_fp = 2;
        break;
    case 0x00: /* FMLAL */
    case 0x04: /* FMLSL */
    case 0x18: /* FMLAL2 */
    case 0x1c: /* FMLSL2 */
        if (is_scalar || size != MO_32 || !dc_isar_feature(aa64_fhm, s)) {
            unallocated_encoding(s);
            return;
        }
        size = MO_16;
        /* is_fp, but we pass cpu_env not fp_status.  */
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    switch (is_fp) {
    case 1: /* normal fp */
        /* convert insn encoded size to MemOp size */
        switch (size) {
        case 0: /* half-precision */
            size = MO_16;
            is_fp16 = true;
            break;
        case MO_32: /* single precision */
        case MO_64: /* double precision */
            break;
        default:
            unallocated_encoding(s);
            return;
        }
        break;

    case 2: /* complex fp */
        /* Each indexable element is a complex pair.  */
        size += 1;
        switch (size) {
        case MO_32:
            if (h && !is_q) {
                unallocated_encoding(s);
                return;
            }
            is_fp16 = true;
            break;
        case MO_64:
            break;
        default:
            unallocated_encoding(s);
            return;
        }
        break;

    default: /* integer */
        switch (size) {
        case MO_8:
        case MO_64:
            unallocated_encoding(s);
            return;
        }
        break;
    }
    if (is_fp16 && !dc_isar_feature(aa64_fp16, s)) {
        unallocated_encoding(s);
        return;
    }

    /* Given MemOp size, adjust register and indexing.  */
    switch (size) {
    case MO_16:
        index = h << 2 | l << 1 | m;
        break;
    case MO_32:
        index = h << 1 | l;
        rm |= m << 4;
        break;
    case MO_64:
        if (l || !is_q) {
            unallocated_encoding(s);
            return;
        }
        index = h;
        rm |= m << 4;
        break;
    default:
        g_assert_not_reached();
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (is_fp) {
        fpst = fpstatus_ptr(is_fp16 ? FPST_FPCR_F16 : FPST_FPCR);
    } else {
        fpst = NULL;
    }

    switch (16 * u + opcode) {
    case 0x0e: /* SDOT */
    case 0x1e: /* UDOT */
        gen_gvec_op3_ool(s, is_q, rd, rn, rm, index,
                         u ? gen_helper_gvec_udot_idx_b
                         : gen_helper_gvec_sdot_idx_b);
        return;
    case 0x11: /* FCMLA #0 */
    case 0x13: /* FCMLA #90 */
    case 0x15: /* FCMLA #180 */
    case 0x17: /* FCMLA #270 */
        {
            int rot = extract32(insn, 13, 2);
            int data = (index << 2) | rot;
            tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                               vec_full_reg_offset(s, rn),
                               vec_full_reg_offset(s, rm), fpst,
                               is_q ? 16 : 8, vec_full_reg_size(s), data,
                               size == MO_64
                               ? gen_helper_gvec_fcmlas_idx
                               : gen_helper_gvec_fcmlah_idx);
            tcg_temp_free_ptr(fpst);
        }
        return;

    case 0x00: /* FMLAL */
    case 0x04: /* FMLSL */
    case 0x18: /* FMLAL2 */
    case 0x1c: /* FMLSL2 */
        {
            int is_s = extract32(opcode, 2, 1);
            int is_2 = u;
            int data = (index << 2) | (is_2 << 1) | is_s;
            tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, rd),
                               vec_full_reg_offset(s, rn),
                               vec_full_reg_offset(s, rm), cpu_env,
                               is_q ? 16 : 8, vec_full_reg_size(s),
                               data, gen_helper_gvec_fmlal_idx_a64);
        }
        return;

    case 0x08: /* MUL */
        if (!is_long && !is_scalar) {
            static gen_helper_gvec_3 * const fns[3] = {
                gen_helper_gvec_mul_idx_h,
                gen_helper_gvec_mul_idx_s,
                gen_helper_gvec_mul_idx_d,
            };
            tcg_gen_gvec_3_ool(vec_full_reg_offset(s, rd),
                               vec_full_reg_offset(s, rn),
                               vec_full_reg_offset(s, rm),
                               is_q ? 16 : 8, vec_full_reg_size(s),
                               index, fns[size - 1]);
            return;
        }
        break;

    case 0x10: /* MLA */
        if (!is_long && !is_scalar) {
            static gen_helper_gvec_4 * const fns[3] = {
                gen_helper_gvec_mla_idx_h,
                gen_helper_gvec_mla_idx_s,
                gen_helper_gvec_mla_idx_d,
            };
            tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                               vec_full_reg_offset(s, rn),
                               vec_full_reg_offset(s, rm),
                               vec_full_reg_offset(s, rd),
                               is_q ? 16 : 8, vec_full_reg_size(s),
                               index, fns[size - 1]);
            return;
        }
        break;

    case 0x14: /* MLS */
        if (!is_long && !is_scalar) {
            static gen_helper_gvec_4 * const fns[3] = {
                gen_helper_gvec_mls_idx_h,
                gen_helper_gvec_mls_idx_s,
                gen_helper_gvec_mls_idx_d,
            };
            tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                               vec_full_reg_offset(s, rn),
                               vec_full_reg_offset(s, rm),
                               vec_full_reg_offset(s, rd),
                               is_q ? 16 : 8, vec_full_reg_size(s),
                               index, fns[size - 1]);
            return;
        }
        break;
    }

    if (size == 3) {
        TCGv_i64 tcg_idx = tcg_temp_new_i64();
        int pass;

        assert(is_fp && is_q && !is_long);

        read_vec_element(s, tcg_idx, rm, index, MO_64);

        for (pass = 0; pass < (is_scalar ? 1 : 2); pass++) {
            TCGv_i64 tcg_op = tcg_temp_new_i64();
            TCGv_i64 tcg_res = tcg_temp_new_i64();

            read_vec_element(s, tcg_op, rn, pass, MO_64);

            switch (16 * u + opcode) {
            case 0x05: /* FMLS */
                /* As usual for ARM, separate negation for fused multiply-add */
                gen_helper_vfp_negd(tcg_op, tcg_op);
                /* fall through */
            case 0x01: /* FMLA */
                read_vec_element(s, tcg_res, rd, pass, MO_64);
                gen_helper_vfp_muladdd(tcg_res, tcg_op, tcg_idx, tcg_res, fpst);
                break;
            case 0x09: /* FMUL */
                gen_helper_vfp_muld(tcg_res, tcg_op, tcg_idx, fpst);
                break;
            case 0x19: /* FMULX */
                gen_helper_vfp_mulxd(tcg_res, tcg_op, tcg_idx, fpst);
                break;
            default:
                g_assert_not_reached();
            }

            write_vec_element(s, tcg_res, rd, pass, MO_64);
            tcg_temp_free_i64(tcg_op);
            tcg_temp_free_i64(tcg_res);
        }

        tcg_temp_free_i64(tcg_idx);
        clear_vec_high(s, !is_scalar, rd);
    } else if (!is_long) {
        /* 32 bit floating point, or 16 or 32 bit integer.
         * For the 16 bit scalar case we use the usual Neon helpers and
         * rely on the fact that 0 op 0 == 0 with no side effects.
         */
        TCGv_i32 tcg_idx = tcg_temp_new_i32();
        int pass, maxpasses;

        if (is_scalar) {
            maxpasses = 1;
        } else {
            maxpasses = is_q ? 4 : 2;
        }

        read_vec_element_i32(s, tcg_idx, rm, index, size);

        if (size == 1 && !is_scalar) {
            /* The simplest way to handle the 16x16 indexed ops is to duplicate
             * the index into both halves of the 32 bit tcg_idx and then use
             * the usual Neon helpers.
             */
            tcg_gen_deposit_i32(tcg_idx, tcg_idx, tcg_idx, 16, 16);
        }

        for (pass = 0; pass < maxpasses; pass++) {
            TCGv_i32 tcg_op = tcg_temp_new_i32();
            TCGv_i32 tcg_res = tcg_temp_new_i32();

            read_vec_element_i32(s, tcg_op, rn, pass, is_scalar ? size : MO_32);

            switch (16 * u + opcode) {
            case 0x08: /* MUL */
            case 0x10: /* MLA */
            case 0x14: /* MLS */
            {
                static NeonGenTwoOpFn * const fns[2][2] = {
                    { gen_helper_neon_add_u16, gen_helper_neon_sub_u16 },
                    { tcg_gen_add_i32, tcg_gen_sub_i32 },
                };
                NeonGenTwoOpFn *genfn;
                bool is_sub = opcode == 0x4;

                if (size == 1) {
                    gen_helper_neon_mul_u16(tcg_res, tcg_op, tcg_idx);
                } else {
                    tcg_gen_mul_i32(tcg_res, tcg_op, tcg_idx);
                }
                if (opcode == 0x8) {
                    break;
                }
                read_vec_element_i32(s, tcg_op, rd, pass, MO_32);
                genfn = fns[size - 1][is_sub];
                genfn(tcg_res, tcg_op, tcg_res);
                break;
            }
            case 0x05: /* FMLS */
            case 0x01: /* FMLA */
                read_vec_element_i32(s, tcg_res, rd, pass,
                                     is_scalar ? size : MO_32);
                switch (size) {
                case 1:
                    if (opcode == 0x5) {
                        /* As usual for ARM, separate negation for fused
                         * multiply-add */
                        tcg_gen_xori_i32(tcg_op, tcg_op, 0x80008000);
                    }
                    if (is_scalar) {
                        gen_helper_advsimd_muladdh(tcg_res, tcg_op, tcg_idx,
                                                   tcg_res, fpst);
                    } else {
                        gen_helper_advsimd_muladd2h(tcg_res, tcg_op, tcg_idx,
                                                    tcg_res, fpst);
                    }
                    break;
                case 2:
                    if (opcode == 0x5) {
                        /* As usual for ARM, separate negation for
                         * fused multiply-add */
                        tcg_gen_xori_i32(tcg_op, tcg_op, 0x80000000);
                    }
                    gen_helper_vfp_muladds(tcg_res, tcg_op, tcg_idx,
                                           tcg_res, fpst);
                    break;
                default:
                    g_assert_not_reached();
                }
                break;
            case 0x09: /* FMUL */
                switch (size) {
                case 1:
                    if (is_scalar) {
                        gen_helper_advsimd_mulh(tcg_res, tcg_op,
                                                tcg_idx, fpst);
                    } else {
                        gen_helper_advsimd_mul2h(tcg_res, tcg_op,
                                                 tcg_idx, fpst);
                    }
                    break;
                case 2:
                    gen_helper_vfp_muls(tcg_res, tcg_op, tcg_idx, fpst);
                    break;
                default:
                    g_assert_not_reached();
                }
                break;
            case 0x19: /* FMULX */
                switch (size) {
                case 1:
                    if (is_scalar) {
                        gen_helper_advsimd_mulxh(tcg_res, tcg_op,
                                                 tcg_idx, fpst);
                    } else {
                        gen_helper_advsimd_mulx2h(tcg_res, tcg_op,
                                                  tcg_idx, fpst);
                    }
                    break;
                case 2:
                    gen_helper_vfp_mulxs(tcg_res, tcg_op, tcg_idx, fpst);
                    break;
                default:
                    g_assert_not_reached();
                }
                break;
            case 0x0c: /* SQDMULH */
                if (size == 1) {
                    gen_helper_neon_qdmulh_s16(tcg_res, cpu_env,
                                               tcg_op, tcg_idx);
                } else {
                    gen_helper_neon_qdmulh_s32(tcg_res, cpu_env,
                                               tcg_op, tcg_idx);
                }
                break;
            case 0x0d: /* SQRDMULH */
                if (size == 1) {
                    gen_helper_neon_qrdmulh_s16(tcg_res, cpu_env,
                                                tcg_op, tcg_idx);
                } else {
                    gen_helper_neon_qrdmulh_s32(tcg_res, cpu_env,
                                                tcg_op, tcg_idx);
                }
                break;
            case 0x1d: /* SQRDMLAH */
                read_vec_element_i32(s, tcg_res, rd, pass,
                                     is_scalar ? size : MO_32);
                if (size == 1) {
                    gen_helper_neon_qrdmlah_s16(tcg_res, cpu_env,
                                                tcg_op, tcg_idx, tcg_res);
                } else {
                    gen_helper_neon_qrdmlah_s32(tcg_res, cpu_env,
                                                tcg_op, tcg_idx, tcg_res);
                }
                break;
            case 0x1f: /* SQRDMLSH */
                read_vec_element_i32(s, tcg_res, rd, pass,
                                     is_scalar ? size : MO_32);
                if (size == 1) {
                    gen_helper_neon_qrdmlsh_s16(tcg_res, cpu_env,
                                                tcg_op, tcg_idx, tcg_res);
                } else {
                    gen_helper_neon_qrdmlsh_s32(tcg_res, cpu_env,
                                                tcg_op, tcg_idx, tcg_res);
                }
                break;
            default:
                g_assert_not_reached();
            }

            if (is_scalar) {
                write_fp_sreg(s, rd, tcg_res);
            } else {
                write_vec_element_i32(s, tcg_res, rd, pass, MO_32);
            }

            tcg_temp_free_i32(tcg_op);
            tcg_temp_free_i32(tcg_res);
        }

        tcg_temp_free_i32(tcg_idx);
        clear_vec_high(s, is_q, rd);
    } else {
        /* long ops: 16x16->32 or 32x32->64 */
        TCGv_i64 tcg_res[2];
        int pass;
        bool satop = extract32(opcode, 0, 1);
        MemOp memop = MO_32;

        if (satop || !u) {
            memop |= MO_SIGN;
        }

        if (size == 2) {
            TCGv_i64 tcg_idx = tcg_temp_new_i64();

            read_vec_element(s, tcg_idx, rm, index, memop);

            for (pass = 0; pass < (is_scalar ? 1 : 2); pass++) {
                TCGv_i64 tcg_op = tcg_temp_new_i64();
                TCGv_i64 tcg_passres;
                int passelt;

                if (is_scalar) {
                    passelt = 0;
                } else {
                    passelt = pass + (is_q * 2);
                }

                read_vec_element(s, tcg_op, rn, passelt, memop);

                tcg_res[pass] = tcg_temp_new_i64();

                if (opcode == 0xa || opcode == 0xb) {
                    /* Non-accumulating ops */
                    tcg_passres = tcg_res[pass];
                } else {
                    tcg_passres = tcg_temp_new_i64();
                }

                tcg_gen_mul_i64(tcg_passres, tcg_op, tcg_idx);
                tcg_temp_free_i64(tcg_op);

                if (satop) {
                    /* saturating, doubling */
                    gen_helper_neon_addl_saturate_s64(tcg_passres, cpu_env,
                                                      tcg_passres, tcg_passres);
                }

                if (opcode == 0xa || opcode == 0xb) {
                    continue;
                }

                /* Accumulating op: handle accumulate step */
                read_vec_element(s, tcg_res[pass], rd, pass, MO_64);

                switch (opcode) {
                case 0x2: /* SMLAL, SMLAL2, UMLAL, UMLAL2 */
                    tcg_gen_add_i64(tcg_res[pass], tcg_res[pass], tcg_passres);
                    break;
                case 0x6: /* SMLSL, SMLSL2, UMLSL, UMLSL2 */
                    tcg_gen_sub_i64(tcg_res[pass], tcg_res[pass], tcg_passres);
                    break;
                case 0x7: /* SQDMLSL, SQDMLSL2 */
                    tcg_gen_neg_i64(tcg_passres, tcg_passres);
                    /* fall through */
                case 0x3: /* SQDMLAL, SQDMLAL2 */
                    gen_helper_neon_addl_saturate_s64(tcg_res[pass], cpu_env,
                                                      tcg_res[pass],
                                                      tcg_passres);
                    break;
                default:
                    g_assert_not_reached();
                }
                tcg_temp_free_i64(tcg_passres);
            }
            tcg_temp_free_i64(tcg_idx);

            clear_vec_high(s, !is_scalar, rd);
        } else {
            TCGv_i32 tcg_idx = tcg_temp_new_i32();

            assert(size == 1);
            read_vec_element_i32(s, tcg_idx, rm, index, size);

            if (!is_scalar) {
                /* The simplest way to handle the 16x16 indexed ops is to
                 * duplicate the index into both halves of the 32 bit tcg_idx
                 * and then use the usual Neon helpers.
                 */
                tcg_gen_deposit_i32(tcg_idx, tcg_idx, tcg_idx, 16, 16);
            }

            for (pass = 0; pass < (is_scalar ? 1 : 2); pass++) {
                TCGv_i32 tcg_op = tcg_temp_new_i32();
                TCGv_i64 tcg_passres;

                if (is_scalar) {
                    read_vec_element_i32(s, tcg_op, rn, pass, size);
                } else {
                    read_vec_element_i32(s, tcg_op, rn,
                                         pass + (is_q * 2), MO_32);
                }

                tcg_res[pass] = tcg_temp_new_i64();

                if (opcode == 0xa || opcode == 0xb) {
                    /* Non-accumulating ops */
                    tcg_passres = tcg_res[pass];
                } else {
                    tcg_passres = tcg_temp_new_i64();
                }

                if (memop & MO_SIGN) {
                    gen_helper_neon_mull_s16(tcg_passres, tcg_op, tcg_idx);
                } else {
                    gen_helper_neon_mull_u16(tcg_passres, tcg_op, tcg_idx);
                }
                if (satop) {
                    gen_helper_neon_addl_saturate_s32(tcg_passres, cpu_env,
                                                      tcg_passres, tcg_passres);
                }
                tcg_temp_free_i32(tcg_op);

                if (opcode == 0xa || opcode == 0xb) {
                    continue;
                }

                /* Accumulating op: handle accumulate step */
                read_vec_element(s, tcg_res[pass], rd, pass, MO_64);

                switch (opcode) {
                case 0x2: /* SMLAL, SMLAL2, UMLAL, UMLAL2 */
                    gen_helper_neon_addl_u32(tcg_res[pass], tcg_res[pass],
                                             tcg_passres);
                    break;
                case 0x6: /* SMLSL, SMLSL2, UMLSL, UMLSL2 */
                    gen_helper_neon_subl_u32(tcg_res[pass], tcg_res[pass],
                                             tcg_passres);
                    break;
                case 0x7: /* SQDMLSL, SQDMLSL2 */
                    gen_helper_neon_negl_u32(tcg_passres, tcg_passres);
                    /* fall through */
                case 0x3: /* SQDMLAL, SQDMLAL2 */
                    gen_helper_neon_addl_saturate_s32(tcg_res[pass], cpu_env,
                                                      tcg_res[pass],
                                                      tcg_passres);
                    break;
                default:
                    g_assert_not_reached();
                }
                tcg_temp_free_i64(tcg_passres);
            }
            tcg_temp_free_i32(tcg_idx);

            if (is_scalar) {
                tcg_gen_ext32u_i64(tcg_res[0], tcg_res[0]);
            }
        }

        if (is_scalar) {
            tcg_res[1] = tcg_const_i64(0);
        }

        for (pass = 0; pass < 2; pass++) {
            write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
            tcg_temp_free_i64(tcg_res[pass]);
        }
    }

    if (fpst) {
        tcg_temp_free_ptr(fpst);
    }
}

/* Crypto AES
 *  31             24 23  22 21       17 16    12 11 10 9    5 4    0
 * +-----------------+------+-----------+--------+-----+------+------+
 * | 0 1 0 0 1 1 1 0 | size | 1 0 1 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +-----------------+------+-----------+--------+-----+------+------+
 */
static void disas_crypto_aes(DisasContext *s, uint32_t insn)
{
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    int decrypt;
    gen_helper_gvec_2 *genfn2 = NULL;
    gen_helper_gvec_3 *genfn3 = NULL;

    if (!dc_isar_feature(aa64_aes, s) || size != 0) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0x4: /* AESE */
        decrypt = 0;
        genfn3 = gen_helper_crypto_aese;
        break;
    case 0x6: /* AESMC */
        decrypt = 0;
        genfn2 = gen_helper_crypto_aesmc;
        break;
    case 0x5: /* AESD */
        decrypt = 1;
        genfn3 = gen_helper_crypto_aese;
        break;
    case 0x7: /* AESIMC */
        decrypt = 1;
        genfn2 = gen_helper_crypto_aesmc;
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }
    if (genfn2) {
        gen_gvec_op2_ool(s, true, rd, rn, decrypt, genfn2);
    } else {
        gen_gvec_op3_ool(s, true, rd, rd, rn, decrypt, genfn3);
    }
}

/* Crypto three-reg SHA
 *  31             24 23  22  21 20  16  15 14    12 11 10 9    5 4    0
 * +-----------------+------+---+------+---+--------+-----+------+------+
 * | 0 1 0 1 1 1 1 0 | size | 0 |  Rm  | 0 | opcode | 0 0 |  Rn  |  Rd  |
 * +-----------------+------+---+------+---+--------+-----+------+------+
 */
static void disas_crypto_three_reg_sha(DisasContext *s, uint32_t insn)
{
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 3);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    gen_helper_gvec_3 *genfn;
    bool feature;

    if (size != 0) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0: /* SHA1C */
        genfn = gen_helper_crypto_sha1c;
        feature = dc_isar_feature(aa64_sha1, s);
        break;
    case 1: /* SHA1P */
        genfn = gen_helper_crypto_sha1p;
        feature = dc_isar_feature(aa64_sha1, s);
        break;
    case 2: /* SHA1M */
        genfn = gen_helper_crypto_sha1m;
        feature = dc_isar_feature(aa64_sha1, s);
        break;
    case 3: /* SHA1SU0 */
        genfn = gen_helper_crypto_sha1su0;
        feature = dc_isar_feature(aa64_sha1, s);
        break;
    case 4: /* SHA256H */
        genfn = gen_helper_crypto_sha256h;
        feature = dc_isar_feature(aa64_sha256, s);
        break;
    case 5: /* SHA256H2 */
        genfn = gen_helper_crypto_sha256h2;
        feature = dc_isar_feature(aa64_sha256, s);
        break;
    case 6: /* SHA256SU1 */
        genfn = gen_helper_crypto_sha256su1;
        feature = dc_isar_feature(aa64_sha256, s);
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!feature) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }
    gen_gvec_op3_ool(s, true, rd, rn, rm, 0, genfn);
}

/* Crypto two-reg SHA
 *  31             24 23  22 21       17 16    12 11 10 9    5 4    0
 * +-----------------+------+-----------+--------+-----+------+------+
 * | 0 1 0 1 1 1 1 0 | size | 1 0 1 0 0 | opcode | 1 0 |  Rn  |  Rd  |
 * +-----------------+------+-----------+--------+-----+------+------+
 */
static void disas_crypto_two_reg_sha(DisasContext *s, uint32_t insn)
{
    int size = extract32(insn, 22, 2);
    int opcode = extract32(insn, 12, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    gen_helper_gvec_2 *genfn;
    bool feature;

    if (size != 0) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 0: /* SHA1H */
        feature = dc_isar_feature(aa64_sha1, s);
        genfn = gen_helper_crypto_sha1h;
        break;
    case 1: /* SHA1SU1 */
        feature = dc_isar_feature(aa64_sha1, s);
        genfn = gen_helper_crypto_sha1su1;
        break;
    case 2: /* SHA256SU0 */
        feature = dc_isar_feature(aa64_sha256, s);
        genfn = gen_helper_crypto_sha256su0;
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!feature) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }
    gen_gvec_op2_ool(s, true, rd, rn, 0, genfn);
}

static void gen_rax1_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    tcg_gen_rotli_i64(d, m, 1);
    tcg_gen_xor_i64(d, d, n);
}

static void gen_rax1_vec(unsigned vece, TCGv_vec d, TCGv_vec n, TCGv_vec m)
{
    tcg_gen_rotli_vec(vece, d, m, 1);
    tcg_gen_xor_vec(vece, d, d, n);
}

void gen_gvec_rax1(unsigned vece, uint32_t rd_ofs, uint32_t rn_ofs,
                   uint32_t rm_ofs, uint32_t opr_sz, uint32_t max_sz)
{
    static const TCGOpcode vecop_list[] = { INDEX_op_rotli_vec, 0 };
    static const GVecGen3 op = {
        .fni8 = gen_rax1_i64,
        .fniv = gen_rax1_vec,
        .opt_opc = vecop_list,
        .fno = gen_helper_crypto_rax1,
        .vece = MO_64,
    };
    tcg_gen_gvec_3(rd_ofs, rn_ofs, rm_ofs, opr_sz, max_sz, &op);
}

/* Crypto three-reg SHA512
 *  31                   21 20  16 15  14  13 12  11  10  9    5 4    0
 * +-----------------------+------+---+---+-----+--------+------+------+
 * | 1 1 0 0 1 1 1 0 0 1 1 |  Rm  | 1 | O | 0 0 | opcode |  Rn  |  Rd  |
 * +-----------------------+------+---+---+-----+--------+------+------+
 */
static void disas_crypto_three_reg_sha512(DisasContext *s, uint32_t insn)
{
    int opcode = extract32(insn, 10, 2);
    int o =  extract32(insn, 14, 1);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    bool feature;
    gen_helper_gvec_3 *oolfn = NULL;
    GVecGen3Fn *gvecfn = NULL;

    if (o == 0) {
        switch (opcode) {
        case 0: /* SHA512H */
            feature = dc_isar_feature(aa64_sha512, s);
            oolfn = gen_helper_crypto_sha512h;
            break;
        case 1: /* SHA512H2 */
            feature = dc_isar_feature(aa64_sha512, s);
            oolfn = gen_helper_crypto_sha512h2;
            break;
        case 2: /* SHA512SU1 */
            feature = dc_isar_feature(aa64_sha512, s);
            oolfn = gen_helper_crypto_sha512su1;
            break;
        case 3: /* RAX1 */
            feature = dc_isar_feature(aa64_sha3, s);
            gvecfn = gen_gvec_rax1;
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        switch (opcode) {
        case 0: /* SM3PARTW1 */
            feature = dc_isar_feature(aa64_sm3, s);
            oolfn = gen_helper_crypto_sm3partw1;
            break;
        case 1: /* SM3PARTW2 */
            feature = dc_isar_feature(aa64_sm3, s);
            oolfn = gen_helper_crypto_sm3partw2;
            break;
        case 2: /* SM4EKEY */
            feature = dc_isar_feature(aa64_sm4, s);
            oolfn = gen_helper_crypto_sm4ekey;
            break;
        default:
            unallocated_encoding(s);
            return;
        }
    }

    if (!feature) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (oolfn) {
        gen_gvec_op3_ool(s, true, rd, rn, rm, 0, oolfn);
    } else {
        gen_gvec_fn3(s, true, rd, rn, rm, gvecfn, MO_64);
    }
}

/* Crypto two-reg SHA512
 *  31                                     12  11  10  9    5 4    0
 * +-----------------------------------------+--------+------+------+
 * | 1 1 0 0 1 1 1 0 1 1 0 0 0 0 0 0 1 0 0 0 | opcode |  Rn  |  Rd  |
 * +-----------------------------------------+--------+------+------+
 */
static void disas_crypto_two_reg_sha512(DisasContext *s, uint32_t insn)
{
    int opcode = extract32(insn, 10, 2);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    bool feature;

    switch (opcode) {
    case 0: /* SHA512SU0 */
        feature = dc_isar_feature(aa64_sha512, s);
        break;
    case 1: /* SM4E */
        feature = dc_isar_feature(aa64_sm4, s);
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!feature) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    switch (opcode) {
    case 0: /* SHA512SU0 */
        gen_gvec_op2_ool(s, true, rd, rn, 0, gen_helper_crypto_sha512su0);
        break;
    case 1: /* SM4E */
        gen_gvec_op3_ool(s, true, rd, rd, rn, 0, gen_helper_crypto_sm4e);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Crypto four-register
 *  31               23 22 21 20  16 15  14  10 9    5 4    0
 * +-------------------+-----+------+---+------+------+------+
 * | 1 1 0 0 1 1 1 0 0 | Op0 |  Rm  | 0 |  Ra  |  Rn  |  Rd  |
 * +-------------------+-----+------+---+------+------+------+
 */
static void disas_crypto_four_reg(DisasContext *s, uint32_t insn)
{
    int op0 = extract32(insn, 21, 2);
    int rm = extract32(insn, 16, 5);
    int ra = extract32(insn, 10, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    bool feature;

    switch (op0) {
    case 0: /* EOR3 */
    case 1: /* BCAX */
        feature = dc_isar_feature(aa64_sha3, s);
        break;
    case 2: /* SM3SS1 */
        feature = dc_isar_feature(aa64_sm3, s);
        break;
    default:
        unallocated_encoding(s);
        return;
    }

    if (!feature) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (op0 < 2) {
        TCGv_i64 tcg_op1, tcg_op2, tcg_op3, tcg_res[2];
        int pass;

        tcg_op1 = tcg_temp_new_i64();
        tcg_op2 = tcg_temp_new_i64();
        tcg_op3 = tcg_temp_new_i64();
        tcg_res[0] = tcg_temp_new_i64();
        tcg_res[1] = tcg_temp_new_i64();

        for (pass = 0; pass < 2; pass++) {
            read_vec_element(s, tcg_op1, rn, pass, MO_64);
            read_vec_element(s, tcg_op2, rm, pass, MO_64);
            read_vec_element(s, tcg_op3, ra, pass, MO_64);

            if (op0 == 0) {
                /* EOR3 */
                tcg_gen_xor_i64(tcg_res[pass], tcg_op2, tcg_op3);
            } else {
                /* BCAX */
                tcg_gen_andc_i64(tcg_res[pass], tcg_op2, tcg_op3);
            }
            tcg_gen_xor_i64(tcg_res[pass], tcg_res[pass], tcg_op1);
        }
        write_vec_element(s, tcg_res[0], rd, 0, MO_64);
        write_vec_element(s, tcg_res[1], rd, 1, MO_64);

        tcg_temp_free_i64(tcg_op1);
        tcg_temp_free_i64(tcg_op2);
        tcg_temp_free_i64(tcg_op3);
        tcg_temp_free_i64(tcg_res[0]);
        tcg_temp_free_i64(tcg_res[1]);
    } else {
        TCGv_i32 tcg_op1, tcg_op2, tcg_op3, tcg_res, tcg_zero;

        tcg_op1 = tcg_temp_new_i32();
        tcg_op2 = tcg_temp_new_i32();
        tcg_op3 = tcg_temp_new_i32();
        tcg_res = tcg_temp_new_i32();
        tcg_zero = tcg_const_i32(0);

        read_vec_element_i32(s, tcg_op1, rn, 3, MO_32);
        read_vec_element_i32(s, tcg_op2, rm, 3, MO_32);
        read_vec_element_i32(s, tcg_op3, ra, 3, MO_32);

        tcg_gen_rotri_i32(tcg_res, tcg_op1, 20);
        tcg_gen_add_i32(tcg_res, tcg_res, tcg_op2);
        tcg_gen_add_i32(tcg_res, tcg_res, tcg_op3);
        tcg_gen_rotri_i32(tcg_res, tcg_res, 25);

        write_vec_element_i32(s, tcg_zero, rd, 0, MO_32);
        write_vec_element_i32(s, tcg_zero, rd, 1, MO_32);
        write_vec_element_i32(s, tcg_zero, rd, 2, MO_32);
        write_vec_element_i32(s, tcg_res, rd, 3, MO_32);

        tcg_temp_free_i32(tcg_op1);
        tcg_temp_free_i32(tcg_op2);
        tcg_temp_free_i32(tcg_op3);
        tcg_temp_free_i32(tcg_res);
        tcg_temp_free_i32(tcg_zero);
    }
}

/* Crypto XAR
 *  31                   21 20  16 15    10 9    5 4    0
 * +-----------------------+------+--------+------+------+
 * | 1 1 0 0 1 1 1 0 1 0 0 |  Rm  |  imm6  |  Rn  |  Rd  |
 * +-----------------------+------+--------+------+------+
 */
static void disas_crypto_xar(DisasContext *s, uint32_t insn)
{
    int rm = extract32(insn, 16, 5);
    int imm6 = extract32(insn, 10, 6);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);
    TCGv_i64 tcg_op1, tcg_op2, tcg_res[2];
    int pass;

    if (!dc_isar_feature(aa64_sha3, s)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_op1 = tcg_temp_new_i64();
    tcg_op2 = tcg_temp_new_i64();
    tcg_res[0] = tcg_temp_new_i64();
    tcg_res[1] = tcg_temp_new_i64();

    for (pass = 0; pass < 2; pass++) {
        read_vec_element(s, tcg_op1, rn, pass, MO_64);
        read_vec_element(s, tcg_op2, rm, pass, MO_64);

        tcg_gen_xor_i64(tcg_res[pass], tcg_op1, tcg_op2);
        tcg_gen_rotri_i64(tcg_res[pass], tcg_res[pass], imm6);
    }
    write_vec_element(s, tcg_res[0], rd, 0, MO_64);
    write_vec_element(s, tcg_res[1], rd, 1, MO_64);

    tcg_temp_free_i64(tcg_op1);
    tcg_temp_free_i64(tcg_op2);
    tcg_temp_free_i64(tcg_res[0]);
    tcg_temp_free_i64(tcg_res[1]);
}

/* Crypto three-reg imm2
 *  31                   21 20  16 15  14 13 12  11  10  9    5 4    0
 * +-----------------------+------+-----+------+--------+------+------+
 * | 1 1 0 0 1 1 1 0 0 1 0 |  Rm  | 1 0 | imm2 | opcode |  Rn  |  Rd  |
 * +-----------------------+------+-----+------+--------+------+------+
 */
static void disas_crypto_three_reg_imm2(DisasContext *s, uint32_t insn)
{
    static gen_helper_gvec_3 * const fns[4] = {
        gen_helper_crypto_sm3tt1a, gen_helper_crypto_sm3tt1b,
        gen_helper_crypto_sm3tt2a, gen_helper_crypto_sm3tt2b,
    };
    int opcode = extract32(insn, 10, 2);
    int imm2 = extract32(insn, 12, 2);
    int rm = extract32(insn, 16, 5);
    int rn = extract32(insn, 5, 5);
    int rd = extract32(insn, 0, 5);

    if (!dc_isar_feature(aa64_sm3, s)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    gen_gvec_op3_ool(s, true, rd, rn, rm, imm2, fns[opcode]);
}

/* C3.6 Data processing - SIMD, inc Crypto
 *
 * As the decode gets a little complex we are using a table based
 * approach for this part of the decode.
 */
static const AArch64DecodeTable data_proc_simd[] = {
    /* pattern  ,  mask     ,  fn                        */
    { 0x0e200400, 0x9f200400, disas_simd_three_reg_same },
    { 0x0e008400, 0x9f208400, disas_simd_three_reg_same_extra },
    { 0x0e200000, 0x9f200c00, disas_simd_three_reg_diff },
    { 0x0e200800, 0x9f3e0c00, disas_simd_two_reg_misc },
    { 0x0e300800, 0x9f3e0c00, disas_simd_across_lanes },
    { 0x0e000400, 0x9fe08400, disas_simd_copy },
    { 0x0f000000, 0x9f000400, disas_simd_indexed }, /* vector indexed */
    /* simd_mod_imm decode is a subset of simd_shift_imm, so must precede it */
    { 0x0f000400, 0x9ff80400, disas_simd_mod_imm },
    { 0x0f000400, 0x9f800400, disas_simd_shift_imm },
    { 0x0e000000, 0xbf208c00, disas_simd_tb },
    { 0x0e000800, 0xbf208c00, disas_simd_zip_trn },
    { 0x2e000000, 0xbf208400, disas_simd_ext },
    { 0x5e200400, 0xdf200400, disas_simd_scalar_three_reg_same },
    { 0x5e008400, 0xdf208400, disas_simd_scalar_three_reg_same_extra },
    { 0x5e200000, 0xdf200c00, disas_simd_scalar_three_reg_diff },
    { 0x5e200800, 0xdf3e0c00, disas_simd_scalar_two_reg_misc },
    { 0x5e300800, 0xdf3e0c00, disas_simd_scalar_pairwise },
    { 0x5e000400, 0xdfe08400, disas_simd_scalar_copy },
    { 0x5f000000, 0xdf000400, disas_simd_indexed }, /* scalar indexed */
    { 0x5f000400, 0xdf800400, disas_simd_scalar_shift_imm },
    { 0x4e280800, 0xff3e0c00, disas_crypto_aes },
    { 0x5e000000, 0xff208c00, disas_crypto_three_reg_sha },
    { 0x5e280800, 0xff3e0c00, disas_crypto_two_reg_sha },
    { 0xce608000, 0xffe0b000, disas_crypto_three_reg_sha512 },
    { 0xcec08000, 0xfffff000, disas_crypto_two_reg_sha512 },
    { 0xce000000, 0xff808000, disas_crypto_four_reg },
    { 0xce800000, 0xffe00000, disas_crypto_xar },
    { 0xce408000, 0xffe0c000, disas_crypto_three_reg_imm2 },
    { 0x0e400400, 0x9f60c400, disas_simd_three_reg_same_fp16 },
    { 0x0e780800, 0x8f7e0c00, disas_simd_two_reg_misc_fp16 },
    { 0x5e400400, 0xdf60c400, disas_simd_scalar_three_reg_same_fp16 },
    { 0x00000000, 0x00000000, NULL }
};

static void disas_data_proc_simd(DisasContext *s, uint32_t insn)
{
    /* Note that this is called with all non-FP cases from
     * table C3-6 so it must UNDEF for entries not specifically
     * allocated to instructions in that table.
     */
    AArch64DecodeFn *fn = lookup_disas_fn(&data_proc_simd[0], insn);
    if (fn) {
        fn(s, insn);
    } else {
        unallocated_encoding(s);
    }
}

/* C3.6 Data processing - SIMD and floating point */
static void disas_data_proc_simd_fp(DisasContext *s, uint32_t insn)
{
    if (extract32(insn, 28, 1) == 1 && extract32(insn, 30, 1) == 0) {
        disas_data_proc_fp(s, insn);
    } else {
        /* SIMD, including crypto */
        disas_data_proc_simd(s, insn);
    }
}

/**
 * is_guarded_page:
 * @env: The cpu environment
 * @s: The DisasContext
 *
 * Return true if the page is guarded.
 */
static bool is_guarded_page(CPUARMState *env, DisasContext *s)
{
    uint64_t addr = s->base.pc_first;
#ifdef CONFIG_USER_ONLY
    return page_get_flags(addr) & PAGE_BTI;
#else
    int mmu_idx = arm_to_core_mmu_idx(s->mmu_idx);
    unsigned int index = tlb_index(env, mmu_idx, addr);
    CPUTLBEntry *entry = tlb_entry(env, mmu_idx, addr);

    /*
     * We test this immediately after reading an insn, which means
     * that any normal page must be in the TLB.  The only exception
     * would be for executing from flash or device memory, which
     * does not retain the TLB entry.
     *
     * FIXME: Assume false for those, for now.  We could use
     * arm_cpu_get_phys_page_attrs_debug to re-read the page
     * table entry even for that case.
     */
    return (tlb_hit(entry->addr_code, addr) &&
            arm_tlb_bti_gp(&env_tlb(env)->d[mmu_idx].iotlb[index].attrs));
#endif
}

/**
 * btype_destination_ok:
 * @insn: The instruction at the branch destination
 * @bt: SCTLR_ELx.BT
 * @btype: PSTATE.BTYPE, and is non-zero
 *
 * On a guarded page, there are a limited number of insns
 * that may be present at the branch target:
 *   - branch target identifiers,
 *   - paciasp, pacibsp,
 *   - BRK insn
 *   - HLT insn
 * Anything else causes a Branch Target Exception.
 *
 * Return true if the branch is compatible, false to raise BTITRAP.
 */
static bool btype_destination_ok(uint32_t insn, bool bt, int btype)
{
    if ((insn & 0xfffff01fu) == 0xd503201fu) {
        /* HINT space */
        switch (extract32(insn, 5, 7)) {
        case 0b011001: /* PACIASP */
        case 0b011011: /* PACIBSP */
            /*
             * If SCTLR_ELx.BT, then PACI*SP are not compatible
             * with btype == 3.  Otherwise all btype are ok.
             */
            return !bt || btype != 3;
        case 0b100000: /* BTI */
            /* Not compatible with any btype.  */
            return false;
        case 0b100010: /* BTI c */
            /* Not compatible with btype == 3 */
            return btype != 3;
        case 0b100100: /* BTI j */
            /* Not compatible with btype == 2 */
            return btype != 2;
        case 0b100110: /* BTI jc */
            /* Compatible with any btype.  */
            return true;
        }
    } else {
        switch (insn & 0xffe0001fu) {
        case 0xd4200000u: /* BRK */
        case 0xd4400000u: /* HLT */
            /* Give priority to the breakpoint exception.  */
            return true;
        }
    }
    return false;
}

/* C3.1 A64 instruction index by encoding */
static void disas_a64_insn(CPUARMState *env, DisasContext *s)
{
    uint32_t insn;
    
    s->pc_curr = s->base.pc_next;
    insn = arm_ldl_code(env, s->base.pc_next, s->sctlr_b);
    s->insn = insn;
    s->base.pc_next += 4;

    s->fp_access_checked = false;
    s->sve_access_checked = false;

    AFL_QEMU_TARGET_ARM64_SNIPPET

    if (dc_isar_feature(aa64_bti, s)) {
        if (s->base.num_insns == 1) {
            /*
             * At the first insn of the TB, compute s->guarded_page.
             * We delayed computing this until successfully reading
             * the first insn of the TB, above.  This (mostly) ensures
             * that the softmmu tlb entry has been populated, and the
             * page table GP bit is available.
             *
             * Note that we need to compute this even if btype == 0,
             * because this value is used for BR instructions later
             * where ENV is not available.
             */
            s->guarded_page = is_guarded_page(env, s);

            /* First insn can have btype set to non-zero.  */
            tcg_debug_assert(s->btype >= 0);

            /*
             * Note that the Branch Target Exception has fairly high
             * priority -- below debugging exceptions but above most
             * everything else.  This allows us to handle this now
             * instead of waiting until the insn is otherwise decoded.
             */
            if (s->btype != 0
                && s->guarded_page
                && !btype_destination_ok(insn, s->bt, s->btype)) {
                gen_exception_insn(s, s->pc_curr, EXCP_UDEF,
                                   syn_btitrap(s->btype),
                                   default_exception_el(s));
                return;
            }
        } else {
            /* Not the first insn: btype must be 0.  */
            tcg_debug_assert(s->btype == 0);
        }
    }

    switch (extract32(insn, 25, 4)) {
    case 0x0: case 0x1: case 0x3: /* UNALLOCATED */
        unallocated_encoding(s);
        break;
    case 0x2:
        if (!dc_isar_feature(aa64_sve, s) || !disas_sve(s, insn)) {
            unallocated_encoding(s);
        }
        break;
    case 0x8: case 0x9: /* Data processing - immediate */
        disas_data_proc_imm(s, insn);
        break;
    case 0xa: case 0xb: /* Branch, exception generation and system insns */
        disas_b_exc_sys(s, insn);
        break;
    case 0x4:
    case 0x6:
    case 0xc:
    case 0xe:      /* Loads and stores */
        disas_ldst(s, insn);
        break;
    case 0x5:
    case 0xd:      /* Data processing - register */
        disas_data_proc_reg(s, insn);
        break;
    case 0x7:
    case 0xf:      /* Data processing - SIMD and floating point */
        disas_data_proc_simd_fp(s, insn);
        break;
    default:
        assert(FALSE); /* all 15 cases should be handled above */
        break;
    }

    /* if we allocated any temporaries, free them here */
    free_tmp_a64(s);

    /*
     * After execution of most insns, btype is reset to 0.
     * Note that we set btype == -1 when the insn sets btype.
     */
    if (s->btype > 0 && s->base.is_jmp != DISAS_NORETURN) {
        reset_btype(s);
    }
}

static void aarch64_tr_init_disas_context(DisasContextBase *dcbase,
                                          CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu->env_ptr;
    ARMCPU *arm_cpu = env_archcpu(env);
    uint32_t tb_flags = dc->base.tb->flags;
    int bound, core_mmu_idx;

    dc->isar = &arm_cpu->isar;
    dc->condjmp = 0;

    dc->aarch64 = 1;
    /* If we are coming from secure EL0 in a system with a 32-bit EL3, then
     * there is no secure EL1, so we route exceptions to EL3.
     */
    dc->secure_routed_to_el3 = arm_feature(env, ARM_FEATURE_EL3) &&
                               !arm_el_is_aa64(env, 3);
    dc->thumb = 0;
    dc->sctlr_b = 0;
    dc->be_data = FIELD_EX32(tb_flags, TBFLAG_ANY, BE_DATA) ? MO_BE : MO_LE;
    dc->condexec_mask = 0;
    dc->condexec_cond = 0;
    core_mmu_idx = FIELD_EX32(tb_flags, TBFLAG_ANY, MMUIDX);
    dc->mmu_idx = core_to_aa64_mmu_idx(core_mmu_idx);
    dc->tbii = FIELD_EX32(tb_flags, TBFLAG_A64, TBII);
    dc->tbid = FIELD_EX32(tb_flags, TBFLAG_A64, TBID);
    dc->tcma = FIELD_EX32(tb_flags, TBFLAG_A64, TCMA);
    dc->current_el = arm_mmu_idx_to_el(dc->mmu_idx);
#if !defined(CONFIG_USER_ONLY)
    dc->user = (dc->current_el == 0);
#endif
    dc->fp_excp_el = FIELD_EX32(tb_flags, TBFLAG_ANY, FPEXC_EL);
    dc->sve_excp_el = FIELD_EX32(tb_flags, TBFLAG_A64, SVEEXC_EL);
    dc->sve_len = (FIELD_EX32(tb_flags, TBFLAG_A64, ZCR_LEN) + 1) * 16;
    dc->pauth_active = FIELD_EX32(tb_flags, TBFLAG_A64, PAUTH_ACTIVE);
    dc->bt = FIELD_EX32(tb_flags, TBFLAG_A64, BT);
    dc->btype = FIELD_EX32(tb_flags, TBFLAG_A64, BTYPE);
    dc->unpriv = FIELD_EX32(tb_flags, TBFLAG_A64, UNPRIV);
    dc->ata = FIELD_EX32(tb_flags, TBFLAG_A64, ATA);
    dc->mte_active[0] = FIELD_EX32(tb_flags, TBFLAG_A64, MTE_ACTIVE);
    dc->mte_active[1] = FIELD_EX32(tb_flags, TBFLAG_A64, MTE0_ACTIVE);
    dc->vec_len = 0;
    dc->vec_stride = 0;
    dc->cp_regs = arm_cpu->cp_regs;
    dc->features = env->features;
    dc->dcz_blocksize = arm_cpu->dcz_blocksize;

#ifdef CONFIG_USER_ONLY
    /* In sve_probe_page, we assume TBI is enabled. */
    tcg_debug_assert(dc->tbid & 1);
#endif

    /* Single step state. The code-generation logic here is:
     *  SS_ACTIVE == 0:
     *   generate code with no special handling for single-stepping (except
     *   that anything that can make us go to SS_ACTIVE == 1 must end the TB;
     *   this happens anyway because those changes are all system register or
     *   PSTATE writes).
     *  SS_ACTIVE == 1, PSTATE.SS == 1: (active-not-pending)
     *   emit code for one insn
     *   emit code to clear PSTATE.SS
     *   emit code to generate software step exception for completed step
     *   end TB (as usual for having generated an exception)
     *  SS_ACTIVE == 1, PSTATE.SS == 0: (active-pending)
     *   emit code to generate a software step exception
     *   end the TB
     */
    dc->ss_active = FIELD_EX32(tb_flags, TBFLAG_ANY, SS_ACTIVE);
    dc->pstate_ss = FIELD_EX32(tb_flags, TBFLAG_ANY, PSTATE_SS);
    dc->is_ldex = false;
    dc->debug_target_el = FIELD_EX32(tb_flags, TBFLAG_ANY, DEBUG_TARGET_EL);

    /* Bound the number of insns to execute to those left on the page.  */
    bound = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;

    /* If architectural single step active, limit to 1.  */
    if (dc->ss_active) {
        bound = 1;
    }
    dc->base.max_insns = MIN(dc->base.max_insns, bound);

    init_tmp_a64_array(dc);
}

static void aarch64_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void aarch64_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_insn_start(dc->base.pc_next, 0, 0);
    dc->insn_start = tcg_last_op();
}

static bool aarch64_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                        const CPUBreakpoint *bp)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (bp->flags & BP_CPU) {
        gen_a64_set_pc_im(dc->base.pc_next);
        gen_helper_check_breakpoints(cpu_env);
        /* End the TB early; it likely won't be executed */
        dc->base.is_jmp = DISAS_TOO_MANY;
    } else {
        gen_exception_internal_insn(dc, dc->base.pc_next, EXCP_DEBUG);
        /* The address covered by the breakpoint must be
           included in [tb->pc, tb->pc + tb->size) in order
           to for it to be properly cleared -- thus we
           increment the PC here so that the logic setting
           tb->size below does the right thing.  */
        dc->base.pc_next += 4;
        dc->base.is_jmp = DISAS_NORETURN;
    }

    return true;
}

static void aarch64_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu->env_ptr;

    if (dc->ss_active && !dc->pstate_ss) {
        /* Singlestep state is Active-pending.
         * If we're in this state at the start of a TB then either
         *  a) we just took an exception to an EL which is being debugged
         *     and this is the first insn in the exception handler
         *  b) debug exceptions were masked and we just unmasked them
         *     without changing EL (eg by clearing PSTATE.D)
         * In either case we're going to take a swstep exception in the
         * "did not step an insn" case, and so the syndrome ISV and EX
         * bits should be zero.
         */
        assert(dc->base.num_insns == 1);
        gen_swstep_exception(dc, 0, 0);
        dc->base.is_jmp = DISAS_NORETURN;
    } else {
        disas_a64_insn(env, dc);
    }

    translator_loop_temp_check(&dc->base);
}

static void aarch64_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (unlikely(dc->base.singlestep_enabled || dc->ss_active)) {
        /* Note that this means single stepping WFI doesn't halt the CPU.
         * For conditional branch insns this is harmless unreachable code as
         * gen_goto_tb() has already handled emitting the debug exception
         * (and thus a tb-jump is not possible when singlestepping).
         */
        switch (dc->base.is_jmp) {
        default:
            gen_a64_set_pc_im(dc->base.pc_next);
            /* fall through */
        case DISAS_EXIT:
        case DISAS_JUMP:
            if (dc->base.singlestep_enabled) {
                gen_exception_internal(EXCP_DEBUG);
            } else {
                gen_step_complete_exception(dc);
            }
            break;
        case DISAS_NORETURN:
            break;
        }
    } else {
        switch (dc->base.is_jmp) {
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
            gen_goto_tb(dc, 1, dc->base.pc_next);
            break;
        default:
        case DISAS_UPDATE_EXIT:
            gen_a64_set_pc_im(dc->base.pc_next);
            /* fall through */
        case DISAS_EXIT:
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_UPDATE_NOCHAIN:
            gen_a64_set_pc_im(dc->base.pc_next);
            /* fall through */
        case DISAS_JUMP:
            tcg_gen_lookup_and_goto_ptr();
            break;
        case DISAS_NORETURN:
        case DISAS_SWI:
            break;
        case DISAS_WFE:
            gen_a64_set_pc_im(dc->base.pc_next);
            gen_helper_wfe(cpu_env);
            break;
        case DISAS_YIELD:
            gen_a64_set_pc_im(dc->base.pc_next);
            gen_helper_yield(cpu_env);
            break;
        case DISAS_WFI:
        {
            /* This is a special case because we don't want to just halt the CPU
             * if trying to debug across a WFI.
             */
            TCGv_i32 tmp = tcg_const_i32(4);

            gen_a64_set_pc_im(dc->base.pc_next);
            gen_helper_wfi(cpu_env, tmp);
            tcg_temp_free_i32(tmp);
            /* The helper doesn't necessarily throw an exception, but we
             * must go back to the main loop to check for interrupts anyway.
             */
            tcg_gen_exit_tb(NULL, 0);
            break;
        }
        }
    }
}

static void aarch64_tr_disas_log(const DisasContextBase *dcbase,
                                      CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    qemu_log("IN: %s\n", lookup_symbol(dc->base.pc_first));
    log_target_disas(cpu, dc->base.pc_first, dc->base.tb->size);
}

const TranslatorOps aarch64_translator_ops = {
    .init_disas_context = aarch64_tr_init_disas_context,
    .tb_start           = aarch64_tr_tb_start,
    .insn_start         = aarch64_tr_insn_start,
    .breakpoint_check   = aarch64_tr_breakpoint_check,
    .translate_insn     = aarch64_tr_translate_insn,
    .tb_stop            = aarch64_tr_tb_stop,
    .disas_log          = aarch64_tr_disas_log,
};
