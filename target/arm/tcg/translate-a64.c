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

#include "exec/exec-all.h"
#include "translate.h"
#include "translate-a64.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "semihosting/semihost.h"
#include "cpregs.h"

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

/*
 * Helpers for extracting complex instruction fields
 */

/*
 * For load/store with an unsigned 12 bit immediate scaled by the element
 * size. The input has the immediate field in bits [14:3] and the element
 * size in [2:0].
 */
static int uimm_scaled(DisasContext *s, int x)
{
    unsigned imm = x >> 3;
    unsigned scale = extract32(x, 0, 3);
    return imm << scale;
}

/* For load/store memory tags: scale offset by LOG2_TAG_GRANULE */
static int scale_by_log2_tag_granule(DisasContext *s, int x)
{
    return x << LOG2_TAG_GRANULE;
}

/*
 * Include the generated decoders.
 */

#include "decode-sme-fa64.c.inc"
#include "decode-a64.c.inc"

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

    cpu_pc = tcg_global_mem_new_i64(tcg_env,
                                    offsetof(CPUARMState, pc),
                                    "pc");
    for (i = 0; i < 32; i++) {
        cpu_X[i] = tcg_global_mem_new_i64(tcg_env,
                                          offsetof(CPUARMState, xregs[i]),
                                          regnames[i]);
    }

    cpu_exclusive_high = tcg_global_mem_new_i64(tcg_env,
        offsetof(CPUARMState, exclusive_high), "exclusive_high");
}

/*
 * Return the core mmu_idx to use for A64 load/store insns which
 * have a "unprivileged load/store" variant. Those insns access
 * EL0 if executed from an EL which has control over EL0 (usually
 * EL1) but behave like normal loads and stores if executed from
 * elsewhere (eg EL3).
 *
 * @unpriv : true for the unprivileged encoding; false for the
 *           normal encoding (in which case we will return the same
 *           thing as get_mem_index().
 */
static int get_a64_user_mem_index(DisasContext *s, bool unpriv)
{
    /*
     * If AccType_UNPRIV is not used, the insn uses AccType_NORMAL,
     * which is the usual mmu_idx for this cpu state.
     */
    ARMMMUIdx useridx = s->mmu_idx;

    if (unpriv && s->unpriv) {
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
        default:
            g_assert_not_reached();
        }
    }
    return arm_to_core_mmu_idx(useridx);
}

static void set_btype_raw(int val)
{
    tcg_gen_st_i32(tcg_constant_i32(val), tcg_env,
                   offsetof(CPUARMState, btype));
}

static void set_btype(DisasContext *s, int val)
{
    /* BTYPE is a 2-bit field, and 0 should be done with reset_btype.  */
    tcg_debug_assert(val >= 1 && val <= 3);
    set_btype_raw(val);
    s->btype = -1;
}

static void reset_btype(DisasContext *s)
{
    if (s->btype != 0) {
        set_btype_raw(0);
        s->btype = 0;
    }
}

static void gen_pc_plus_diff(DisasContext *s, TCGv_i64 dest, target_long diff)
{
    assert(s->pc_save != -1);
    if (tb_cflags(s->base.tb) & CF_PCREL) {
        tcg_gen_addi_i64(dest, cpu_pc, (s->pc_curr - s->pc_save) + diff);
    } else {
        tcg_gen_movi_i64(dest, s->pc_curr + diff);
    }
}

void gen_a64_update_pc(DisasContext *s, target_long diff)
{
    gen_pc_plus_diff(s, cpu_pc, diff);
    s->pc_save = s->pc_curr + diff;
}

/*
 * Handle Top Byte Ignore (TBI) bits.
 *
 * If address tagging is enabled via the TCR TBI bits:
 *  + for EL2 and EL3 there is only one TBI bit, and if it is set
 *    then the address is zero-extended, clearing bits [63:56]
 *  + for EL0 and EL1, TBI0 controls addresses with bit 55 == 0
 *    and TBI1 controls addresses with bit 55 == 1.
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
    s->pc_save = -1;
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
    TCGv_i64 clean = tcg_temp_new_i64();
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
    gen_helper_probe_access(tcg_env, ptr,
                            tcg_constant_i32(acc),
                            tcg_constant_i32(get_mem_index(s)),
                            tcg_constant_i32(1 << log2_size));
}

/*
 * For MTE, check a single logical or atomic access.  This probes a single
 * address, the exact one specified.  The size and alignment of the access
 * is not relevant to MTE, per se, but watchpoints do require the size,
 * and we want to recognize those before making any other changes to state.
 */
static TCGv_i64 gen_mte_check1_mmuidx(DisasContext *s, TCGv_i64 addr,
                                      bool is_write, bool tag_checked,
                                      MemOp memop, bool is_unpriv,
                                      int core_idx)
{
    if (tag_checked && s->mte_active[is_unpriv]) {
        TCGv_i64 ret;
        int desc = 0;

        desc = FIELD_DP32(desc, MTEDESC, MIDX, core_idx);
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, ALIGN, get_alignment_bits(memop));
        desc = FIELD_DP32(desc, MTEDESC, SIZEM1, memop_size(memop) - 1);

        ret = tcg_temp_new_i64();
        gen_helper_mte_check(ret, tcg_env, tcg_constant_i32(desc), addr);

        return ret;
    }
    return clean_data_tbi(s, addr);
}

TCGv_i64 gen_mte_check1(DisasContext *s, TCGv_i64 addr, bool is_write,
                        bool tag_checked, MemOp memop)
{
    return gen_mte_check1_mmuidx(s, addr, is_write, tag_checked, memop,
                                 false, get_mem_index(s));
}

/*
 * For MTE, check multiple logical sequential accesses.
 */
TCGv_i64 gen_mte_checkN(DisasContext *s, TCGv_i64 addr, bool is_write,
                        bool tag_checked, int total_size, MemOp single_mop)
{
    if (tag_checked && s->mte_active[0]) {
        TCGv_i64 ret;
        int desc = 0;

        desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, is_write);
        desc = FIELD_DP32(desc, MTEDESC, ALIGN, get_alignment_bits(single_mop));
        desc = FIELD_DP32(desc, MTEDESC, SIZEM1, total_size - 1);

        ret = tcg_temp_new_i64();
        gen_helper_mte_check(ret, tcg_env, tcg_constant_i32(desc), addr);

        return ret;
    }
    return clean_data_tbi(s, addr);
}

/*
 * Generate the special alignment check that applies to AccType_ATOMIC
 * and AccType_ORDERED insns under FEAT_LSE2: the access need not be
 * naturally aligned, but it must not cross a 16-byte boundary.
 * See AArch64.CheckAlignment().
 */
static void check_lse2_align(DisasContext *s, int rn, int imm,
                             bool is_write, MemOp mop)
{
    TCGv_i32 tmp;
    TCGv_i64 addr;
    TCGLabel *over_label;
    MMUAccessType type;
    int mmu_idx;

    tmp = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(tmp, cpu_reg_sp(s, rn));
    tcg_gen_addi_i32(tmp, tmp, imm & 15);
    tcg_gen_andi_i32(tmp, tmp, 15);
    tcg_gen_addi_i32(tmp, tmp, memop_size(mop));

    over_label = gen_new_label();
    tcg_gen_brcondi_i32(TCG_COND_LEU, tmp, 16, over_label);

    addr = tcg_temp_new_i64();
    tcg_gen_addi_i64(addr, cpu_reg_sp(s, rn), imm);

    type = is_write ? MMU_DATA_STORE : MMU_DATA_LOAD,
    mmu_idx = get_mem_index(s);
    gen_helper_unaligned_access(tcg_env, addr, tcg_constant_i32(type),
                                tcg_constant_i32(mmu_idx));

    gen_set_label(over_label);

}

/* Handle the alignment check for AccType_ATOMIC instructions. */
static MemOp check_atomic_align(DisasContext *s, int rn, MemOp mop)
{
    MemOp size = mop & MO_SIZE;

    if (size == MO_8) {
        return mop;
    }

    /*
     * If size == MO_128, this is a LDXP, and the operation is single-copy
     * atomic for each doubleword, not the entire quadword; it still must
     * be quadword aligned.
     */
    if (size == MO_128) {
        return finalize_memop_atom(s, MO_128 | MO_ALIGN,
                                   MO_ATOM_IFALIGN_PAIR);
    }
    if (dc_isar_feature(aa64_lse2, s)) {
        check_lse2_align(s, rn, 0, true, mop);
    } else {
        mop |= MO_ALIGN;
    }
    return finalize_memop(s, mop);
}

/* Handle the alignment check for AccType_ORDERED instructions. */
static MemOp check_ordered_align(DisasContext *s, int rn, int imm,
                                 bool is_write, MemOp mop)
{
    MemOp size = mop & MO_SIZE;

    if (size == MO_8) {
        return mop;
    }
    if (size == MO_128) {
        return finalize_memop_atom(s, MO_128 | MO_ALIGN,
                                   MO_ATOM_IFALIGN_PAIR);
    }
    if (!dc_isar_feature(aa64_lse2, s)) {
        mop |= MO_ALIGN;
    } else if (!s->naa) {
        check_lse2_align(s, rn, imm, is_write, mop);
    }
    return finalize_memop(s, mop);
}

typedef struct DisasCompare64 {
    TCGCond cond;
    TCGv_i64 value;
} DisasCompare64;

static void a64_test_cc(DisasCompare64 *c64, int cc)
{
    DisasCompare c32;

    arm_test_cc(&c32, cc);

    /*
     * Sign-extend the 32-bit value so that the GE/LT comparisons work
     * properly.  The NE/EQ comparisons are also fine with this choice.
      */
    c64->cond = c32.cond;
    c64->value = tcg_temp_new_i64();
    tcg_gen_ext_i32_i64(c64->value, c32.value);
}

static void gen_rebuild_hflags(DisasContext *s)
{
    gen_helper_rebuild_hflags_a64(tcg_env, tcg_constant_i32(s->current_el));
}

static void gen_exception_internal(int excp)
{
    assert(excp_is_internal(excp));
    gen_helper_exception_internal(tcg_env, tcg_constant_i32(excp));
}

static void gen_exception_internal_insn(DisasContext *s, int excp)
{
    gen_a64_update_pc(s, 0);
    gen_exception_internal(excp);
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_exception_bkpt_insn(DisasContext *s, uint32_t syndrome)
{
    gen_a64_update_pc(s, 0);
    gen_helper_exception_bkpt_insn(tcg_env, tcg_constant_i32(syndrome));
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

static inline bool use_goto_tb(DisasContext *s, uint64_t dest)
{
    if (s->ss_active) {
        return false;
    }
    return translator_use_goto_tb(&s->base, dest);
}

static void gen_goto_tb(DisasContext *s, int n, int64_t diff)
{
    if (use_goto_tb(s, s->pc_curr + diff)) {
        /*
         * For pcrel, the pc must always be up-to-date on entry to
         * the linked TB, so that it can use simple additions for all
         * further adjustments.  For !pcrel, the linked TB is compiled
         * to know its full virtual address, so we can delay the
         * update to pc to the unlinked path.  A long chain of links
         * can thus avoid many updates to the PC.
         */
        if (tb_cflags(s->base.tb) & CF_PCREL) {
            gen_a64_update_pc(s, diff);
            tcg_gen_goto_tb(n);
        } else {
            tcg_gen_goto_tb(n);
            gen_a64_update_pc(s, diff);
        }
        tcg_gen_exit_tb(s->base.tb, n);
        s->base.is_jmp = DISAS_NORETURN;
    } else {
        gen_a64_update_pc(s, diff);
        if (s->ss_active) {
            gen_step_complete_exception(s);
        } else {
            tcg_gen_lookup_and_goto_ptr();
            s->base.is_jmp = DISAS_NORETURN;
        }
    }
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
        TCGv_i64 t = tcg_temp_new_i64();
        tcg_gen_movi_i64(t, 0);
        return t;
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
    TCGv_i64 v = tcg_temp_new_i64();
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
    TCGv_i64 v = tcg_temp_new_i64();
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

    tcg_gen_ld_i64(v, tcg_env, fp_reg_offset(s, reg, MO_64));
    return v;
}

static TCGv_i32 read_fp_sreg(DisasContext *s, int reg)
{
    TCGv_i32 v = tcg_temp_new_i32();

    tcg_gen_ld_i32(v, tcg_env, fp_reg_offset(s, reg, MO_32));
    return v;
}

static TCGv_i32 read_fp_hreg(DisasContext *s, int reg)
{
    TCGv_i32 v = tcg_temp_new_i32();

    tcg_gen_ld16u_i32(v, tcg_env, fp_reg_offset(s, reg, MO_16));
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

    tcg_gen_st_i64(v, tcg_env, ofs);
    clear_vec_high(s, false, reg);
}

static void write_fp_sreg(DisasContext *s, int reg, TCGv_i32 v)
{
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_extu_i32_i64(tmp, v);
    write_fp_dreg(s, reg, tmp);
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
}

/* Expand a 4-operand operation using an out-of-line helper.  */
static void gen_gvec_op4_ool(DisasContext *s, bool is_q, int rd, int rn,
                             int rm, int ra, int data, gen_helper_gvec_4 *fn)
{
    tcg_gen_gvec_4_ool(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       vec_full_reg_offset(s, ra),
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
}

/*
 * Expand a 4-operand + fpstatus pointer + simd data value operation using
 * an out-of-line helper.
 */
static void gen_gvec_op4_fpst(DisasContext *s, bool is_q, int rd, int rn,
                              int rm, int ra, bool is_fp16, int data,
                              gen_helper_gvec_4_ptr *fn)
{
    TCGv_ptr fpst = fpstatus_ptr(is_fp16 ? FPST_FPCR_F16 : FPST_FPCR);
    tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       vec_full_reg_offset(s, ra), fpst,
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
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
static void gen_add64_CC(TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
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
    tcg_gen_extrh_i64_i32(cpu_VF, flag);

    tcg_gen_mov_i64(dest, result);
}

static void gen_add32_CC(TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
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
}

static void gen_add_CC(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    if (sf) {
        gen_add64_CC(dest, t0, t1);
    } else {
        gen_add32_CC(dest, t0, t1);
    }
}

/* dest = T0 - T1; compute C, N, V and Z flags */
static void gen_sub64_CC(TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
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
    tcg_gen_extrh_i64_i32(cpu_VF, flag);
    tcg_gen_mov_i64(dest, result);
}

static void gen_sub32_CC(TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
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
    tcg_gen_and_i32(cpu_VF, cpu_VF, tmp);
    tcg_gen_extu_i32_i64(dest, cpu_NF);
}

static void gen_sub_CC(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    if (sf) {
        gen_sub64_CC(dest, t0, t1);
    } else {
        gen_sub32_CC(dest, t0, t1);
    }
}

/* dest = T0 + T1 + CF; do not compute flags. */
static void gen_adc(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    TCGv_i64 flag = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(flag, cpu_CF);
    tcg_gen_add_i64(dest, t0, t1);
    tcg_gen_add_i64(dest, dest, flag);

    if (!sf) {
        tcg_gen_ext32u_i64(dest, dest);
    }
}

/* dest = T0 + T1 + CF; compute C, N, V and Z flags. */
static void gen_adc_CC(int sf, TCGv_i64 dest, TCGv_i64 t0, TCGv_i64 t1)
{
    if (sf) {
        TCGv_i64 result = tcg_temp_new_i64();
        TCGv_i64 cf_64 = tcg_temp_new_i64();
        TCGv_i64 vf_64 = tcg_temp_new_i64();
        TCGv_i64 tmp = tcg_temp_new_i64();
        TCGv_i64 zero = tcg_constant_i64(0);

        tcg_gen_extu_i32_i64(cf_64, cpu_CF);
        tcg_gen_add2_i64(result, cf_64, t0, zero, cf_64, zero);
        tcg_gen_add2_i64(result, cf_64, result, cf_64, t1, zero);
        tcg_gen_extrl_i64_i32(cpu_CF, cf_64);
        gen_set_NZ64(result);

        tcg_gen_xor_i64(vf_64, result, t0);
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_andc_i64(vf_64, vf_64, tmp);
        tcg_gen_extrh_i64_i32(cpu_VF, vf_64);

        tcg_gen_mov_i64(dest, result);
    } else {
        TCGv_i32 t0_32 = tcg_temp_new_i32();
        TCGv_i32 t1_32 = tcg_temp_new_i32();
        TCGv_i32 tmp = tcg_temp_new_i32();
        TCGv_i32 zero = tcg_constant_i32(0);

        tcg_gen_extrl_i64_i32(t0_32, t0);
        tcg_gen_extrl_i64_i32(t1_32, t1);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, t0_32, zero, cpu_CF, zero);
        tcg_gen_add2_i32(cpu_NF, cpu_CF, cpu_NF, cpu_CF, t1_32, zero);

        tcg_gen_mov_i32(cpu_ZF, cpu_NF);
        tcg_gen_xor_i32(cpu_VF, cpu_NF, t0_32);
        tcg_gen_xor_i32(tmp, t0_32, t1_32);
        tcg_gen_andc_i32(cpu_VF, cpu_VF, tmp);
        tcg_gen_extu_i32_i64(dest, cpu_NF);
    }
}

/*
 * Load/Store generators
 */

/*
 * Store from GPR register to memory.
 */
static void do_gpr_st_memidx(DisasContext *s, TCGv_i64 source,
                             TCGv_i64 tcg_addr, MemOp memop, int memidx,
                             bool iss_valid,
                             unsigned int iss_srt,
                             bool iss_sf, bool iss_ar)
{
    tcg_gen_qemu_st_i64(source, tcg_addr, memidx, memop);

    if (iss_valid) {
        uint32_t syn;

        syn = syn_data_abort_with_iss(0,
                                      (memop & MO_SIZE),
                                      false,
                                      iss_srt,
                                      iss_sf,
                                      iss_ar,
                                      0, 0, 0, 0, 0, false);
        disas_set_insn_syndrome(s, syn);
    }
}

static void do_gpr_st(DisasContext *s, TCGv_i64 source,
                      TCGv_i64 tcg_addr, MemOp memop,
                      bool iss_valid,
                      unsigned int iss_srt,
                      bool iss_sf, bool iss_ar)
{
    do_gpr_st_memidx(s, source, tcg_addr, memop, get_mem_index(s),
                     iss_valid, iss_srt, iss_sf, iss_ar);
}

/*
 * Load from memory to GPR register
 */
static void do_gpr_ld_memidx(DisasContext *s, TCGv_i64 dest, TCGv_i64 tcg_addr,
                             MemOp memop, bool extend, int memidx,
                             bool iss_valid, unsigned int iss_srt,
                             bool iss_sf, bool iss_ar)
{
    tcg_gen_qemu_ld_i64(dest, tcg_addr, memidx, memop);

    if (extend && (memop & MO_SIGN)) {
        g_assert((memop & MO_SIZE) <= MO_32);
        tcg_gen_ext32u_i64(dest, dest);
    }

    if (iss_valid) {
        uint32_t syn;

        syn = syn_data_abort_with_iss(0,
                                      (memop & MO_SIZE),
                                      (memop & MO_SIGN) != 0,
                                      iss_srt,
                                      iss_sf,
                                      iss_ar,
                                      0, 0, 0, 0, 0, false);
        disas_set_insn_syndrome(s, syn);
    }
}

static void do_gpr_ld(DisasContext *s, TCGv_i64 dest, TCGv_i64 tcg_addr,
                      MemOp memop, bool extend,
                      bool iss_valid, unsigned int iss_srt,
                      bool iss_sf, bool iss_ar)
{
    do_gpr_ld_memidx(s, dest, tcg_addr, memop, extend, get_mem_index(s),
                     iss_valid, iss_srt, iss_sf, iss_ar);
}

/*
 * Store from FP register to memory
 */
static void do_fp_st(DisasContext *s, int srcidx, TCGv_i64 tcg_addr, MemOp mop)
{
    /* This writes the bottom N bits of a 128 bit wide vector to memory */
    TCGv_i64 tmplo = tcg_temp_new_i64();

    tcg_gen_ld_i64(tmplo, tcg_env, fp_reg_offset(s, srcidx, MO_64));

    if ((mop & MO_SIZE) < MO_128) {
        tcg_gen_qemu_st_i64(tmplo, tcg_addr, get_mem_index(s), mop);
    } else {
        TCGv_i64 tmphi = tcg_temp_new_i64();
        TCGv_i128 t16 = tcg_temp_new_i128();

        tcg_gen_ld_i64(tmphi, tcg_env, fp_reg_hi_offset(s, srcidx));
        tcg_gen_concat_i64_i128(t16, tmplo, tmphi);

        tcg_gen_qemu_st_i128(t16, tcg_addr, get_mem_index(s), mop);
    }
}

/*
 * Load from memory to FP register
 */
static void do_fp_ld(DisasContext *s, int destidx, TCGv_i64 tcg_addr, MemOp mop)
{
    /* This always zero-extends and writes to a full 128 bit wide vector */
    TCGv_i64 tmplo = tcg_temp_new_i64();
    TCGv_i64 tmphi = NULL;

    if ((mop & MO_SIZE) < MO_128) {
        tcg_gen_qemu_ld_i64(tmplo, tcg_addr, get_mem_index(s), mop);
    } else {
        TCGv_i128 t16 = tcg_temp_new_i128();

        tcg_gen_qemu_ld_i128(t16, tcg_addr, get_mem_index(s), mop);

        tmphi = tcg_temp_new_i64();
        tcg_gen_extr_i128_i64(tmplo, tmphi, t16);
    }

    tcg_gen_st_i64(tmplo, tcg_env, fp_reg_offset(s, destidx, MO_64));

    if (tmphi) {
        tcg_gen_st_i64(tmphi, tcg_env, fp_reg_hi_offset(s, destidx));
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
    switch ((unsigned)memop) {
    case MO_8:
        tcg_gen_ld8u_i64(tcg_dest, tcg_env, vect_off);
        break;
    case MO_16:
        tcg_gen_ld16u_i64(tcg_dest, tcg_env, vect_off);
        break;
    case MO_32:
        tcg_gen_ld32u_i64(tcg_dest, tcg_env, vect_off);
        break;
    case MO_8|MO_SIGN:
        tcg_gen_ld8s_i64(tcg_dest, tcg_env, vect_off);
        break;
    case MO_16|MO_SIGN:
        tcg_gen_ld16s_i64(tcg_dest, tcg_env, vect_off);
        break;
    case MO_32|MO_SIGN:
        tcg_gen_ld32s_i64(tcg_dest, tcg_env, vect_off);
        break;
    case MO_64:
    case MO_64|MO_SIGN:
        tcg_gen_ld_i64(tcg_dest, tcg_env, vect_off);
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
        tcg_gen_ld8u_i32(tcg_dest, tcg_env, vect_off);
        break;
    case MO_16:
        tcg_gen_ld16u_i32(tcg_dest, tcg_env, vect_off);
        break;
    case MO_8|MO_SIGN:
        tcg_gen_ld8s_i32(tcg_dest, tcg_env, vect_off);
        break;
    case MO_16|MO_SIGN:
        tcg_gen_ld16s_i32(tcg_dest, tcg_env, vect_off);
        break;
    case MO_32:
    case MO_32|MO_SIGN:
        tcg_gen_ld_i32(tcg_dest, tcg_env, vect_off);
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
        tcg_gen_st8_i64(tcg_src, tcg_env, vect_off);
        break;
    case MO_16:
        tcg_gen_st16_i64(tcg_src, tcg_env, vect_off);
        break;
    case MO_32:
        tcg_gen_st32_i64(tcg_src, tcg_env, vect_off);
        break;
    case MO_64:
        tcg_gen_st_i64(tcg_src, tcg_env, vect_off);
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
        tcg_gen_st8_i32(tcg_src, tcg_env, vect_off);
        break;
    case MO_16:
        tcg_gen_st16_i32(tcg_src, tcg_env, vect_off);
        break;
    case MO_32:
        tcg_gen_st_i32(tcg_src, tcg_env, vect_off);
        break;
    default:
        g_assert_not_reached();
    }
}

/* Store from vector register to memory */
static void do_vec_st(DisasContext *s, int srcidx, int element,
                      TCGv_i64 tcg_addr, MemOp mop)
{
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();

    read_vec_element(s, tcg_tmp, srcidx, element, mop & MO_SIZE);
    tcg_gen_qemu_st_i64(tcg_tmp, tcg_addr, get_mem_index(s), mop);
}

/* Load from memory to vector register */
static void do_vec_ld(DisasContext *s, int destidx, int element,
                      TCGv_i64 tcg_addr, MemOp mop)
{
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i64(tcg_tmp, tcg_addr, get_mem_index(s), mop);
    write_vec_element(s, tcg_tmp, destidx, element, mop & MO_SIZE);
}

/* Check that FP/Neon access is enabled. If it is, return
 * true. If not, emit code to generate an appropriate exception,
 * and return false; the caller should not emit any code for
 * the instruction. Note that this check must happen after all
 * unallocated-encoding checks (otherwise the syndrome information
 * for the resulting exception will be incorrect).
 */
static bool fp_access_check_only(DisasContext *s)
{
    if (s->fp_excp_el) {
        assert(!s->fp_access_checked);
        s->fp_access_checked = true;

        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_fp_access_trap(1, 0xe, false, 0),
                              s->fp_excp_el);
        return false;
    }
    s->fp_access_checked = true;
    return true;
}

static bool fp_access_check(DisasContext *s)
{
    if (!fp_access_check_only(s)) {
        return false;
    }
    if (s->sme_trap_nonstreaming && s->is_nonstreaming) {
        gen_exception_insn(s, 0, EXCP_UDEF,
                           syn_smetrap(SME_ET_Streaming, false));
        return false;
    }
    return true;
}

/*
 * Check that SVE access is enabled.  If it is, return true.
 * If not, emit code to generate an appropriate exception and return false.
 * This function corresponds to CheckSVEEnabled().
 */
bool sve_access_check(DisasContext *s)
{
    if (s->pstate_sm || !dc_isar_feature(aa64_sve, s)) {
        assert(dc_isar_feature(aa64_sme, s));
        if (!sme_sm_enabled_check(s)) {
            goto fail_exit;
        }
    } else if (s->sve_excp_el) {
        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_sve_access_trap(), s->sve_excp_el);
        goto fail_exit;
    }
    s->sve_access_checked = true;
    return fp_access_check(s);

 fail_exit:
    /* Assert that we only raise one exception per instruction. */
    assert(!s->sve_access_checked);
    s->sve_access_checked = true;
    return false;
}

/*
 * Check that SME access is enabled, raise an exception if not.
 * Note that this function corresponds to CheckSMEAccess and is
 * only used directly for cpregs.
 */
static bool sme_access_check(DisasContext *s)
{
    if (s->sme_excp_el) {
        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_smetrap(SME_ET_AccessTrap, false),
                              s->sme_excp_el);
        return false;
    }
    return true;
}

/* This function corresponds to CheckSMEEnabled. */
bool sme_enabled_check(DisasContext *s)
{
    /*
     * Note that unlike sve_excp_el, we have not constrained sme_excp_el
     * to be zero when fp_excp_el has priority.  This is because we need
     * sme_excp_el by itself for cpregs access checks.
     */
    if (!s->fp_excp_el || s->sme_excp_el < s->fp_excp_el) {
        s->fp_access_checked = true;
        return sme_access_check(s);
    }
    return fp_access_check_only(s);
}

/* Common subroutine for CheckSMEAnd*Enabled. */
bool sme_enabled_check_with_svcr(DisasContext *s, unsigned req)
{
    if (!sme_enabled_check(s)) {
        return false;
    }
    if (FIELD_EX64(req, SVCR, SM) && !s->pstate_sm) {
        gen_exception_insn(s, 0, EXCP_UDEF,
                           syn_smetrap(SME_ET_NotStreaming, false));
        return false;
    }
    if (FIELD_EX64(req, SVCR, ZA) && !s->pstate_za) {
        gen_exception_insn(s, 0, EXCP_UDEF,
                           syn_smetrap(SME_ET_InactiveZA, false));
        return false;
    }
    return true;
}

/*
 * Expanders for AdvSIMD translation functions.
 */

static bool do_gvec_op2_ool(DisasContext *s, arg_qrr_e *a, int data,
                            gen_helper_gvec_2 *fn)
{
    if (!a->q && a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_op2_ool(s, a->q, a->rd, a->rn, data, fn);
    }
    return true;
}

static bool do_gvec_op3_ool(DisasContext *s, arg_qrrr_e *a, int data,
                            gen_helper_gvec_3 *fn)
{
    if (!a->q && a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_op3_ool(s, a->q, a->rd, a->rn, a->rm, data, fn);
    }
    return true;
}

static bool do_gvec_fn3(DisasContext *s, arg_qrrr_e *a, GVecGen3Fn *fn)
{
    if (!a->q && a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_fn3(s, a->q, a->rd, a->rn, a->rm, fn, a->esz);
    }
    return true;
}

static bool do_gvec_fn3_no64(DisasContext *s, arg_qrrr_e *a, GVecGen3Fn *fn)
{
    if (a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_fn3(s, a->q, a->rd, a->rn, a->rm, fn, a->esz);
    }
    return true;
}

static bool do_gvec_fn3_no8_no64(DisasContext *s, arg_qrrr_e *a, GVecGen3Fn *fn)
{
    if (a->esz == MO_8) {
        return false;
    }
    return do_gvec_fn3_no64(s, a, fn);
}

static bool do_gvec_fn4(DisasContext *s, arg_qrrrr_e *a, GVecGen4Fn *fn)
{
    if (!a->q && a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_fn4(s, a->q, a->rd, a->rn, a->rm, a->ra, fn, a->esz);
    }
    return true;
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

    tcg_gen_ext_i64(tcg_out, tcg_in, extsize | (is_signed ? MO_SIGN : 0));
    tcg_gen_shli_i64(tcg_out, tcg_out, shift);
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

static bool trans_B(DisasContext *s, arg_i *a)
{
    reset_btype(s);
    gen_goto_tb(s, 0, a->imm);
    return true;
}

static bool trans_BL(DisasContext *s, arg_i *a)
{
    gen_pc_plus_diff(s, cpu_reg(s, 30), curr_insn_len(s));
    reset_btype(s);
    gen_goto_tb(s, 0, a->imm);
    return true;
}


static bool trans_CBZ(DisasContext *s, arg_cbz *a)
{
    DisasLabel match;
    TCGv_i64 tcg_cmp;

    tcg_cmp = read_cpu_reg(s, a->rt, a->sf);
    reset_btype(s);

    match = gen_disas_label(s);
    tcg_gen_brcondi_i64(a->nz ? TCG_COND_NE : TCG_COND_EQ,
                        tcg_cmp, 0, match.label);
    gen_goto_tb(s, 0, 4);
    set_disas_label(s, match);
    gen_goto_tb(s, 1, a->imm);
    return true;
}

static bool trans_TBZ(DisasContext *s, arg_tbz *a)
{
    DisasLabel match;
    TCGv_i64 tcg_cmp;

    tcg_cmp = tcg_temp_new_i64();
    tcg_gen_andi_i64(tcg_cmp, cpu_reg(s, a->rt), 1ULL << a->bitpos);

    reset_btype(s);

    match = gen_disas_label(s);
    tcg_gen_brcondi_i64(a->nz ? TCG_COND_NE : TCG_COND_EQ,
                        tcg_cmp, 0, match.label);
    gen_goto_tb(s, 0, 4);
    set_disas_label(s, match);
    gen_goto_tb(s, 1, a->imm);
    return true;
}

static bool trans_B_cond(DisasContext *s, arg_B_cond *a)
{
    /* BC.cond is only present with FEAT_HBC */
    if (a->c && !dc_isar_feature(aa64_hbc, s)) {
        return false;
    }
    reset_btype(s);
    if (a->cond < 0x0e) {
        /* genuinely conditional branches */
        DisasLabel match = gen_disas_label(s);
        arm_gen_test_cc(a->cond, match.label);
        gen_goto_tb(s, 0, 4);
        set_disas_label(s, match);
        gen_goto_tb(s, 1, a->imm);
    } else {
        /* 0xe and 0xf are both "always" conditions */
        gen_goto_tb(s, 0, a->imm);
    }
    return true;
}

static void set_btype_for_br(DisasContext *s, int rn)
{
    if (dc_isar_feature(aa64_bti, s)) {
        /* BR to {x16,x17} or !guard -> 1, else 3.  */
        if (rn == 16 || rn == 17) {
            set_btype(s, 1);
        } else {
            TCGv_i64 pc = tcg_temp_new_i64();
            gen_pc_plus_diff(s, pc, 0);
            gen_helper_guarded_page_br(tcg_env, pc);
            s->btype = -1;
        }
    }
}

static void set_btype_for_blr(DisasContext *s)
{
    if (dc_isar_feature(aa64_bti, s)) {
        /* BLR sets BTYPE to 2, regardless of source guarded page.  */
        set_btype(s, 2);
    }
}

static bool trans_BR(DisasContext *s, arg_r *a)
{
    set_btype_for_br(s, a->rn);
    gen_a64_set_pc(s, cpu_reg(s, a->rn));
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_BLR(DisasContext *s, arg_r *a)
{
    TCGv_i64 dst = cpu_reg(s, a->rn);
    TCGv_i64 lr = cpu_reg(s, 30);
    if (dst == lr) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_mov_i64(tmp, dst);
        dst = tmp;
    }
    gen_pc_plus_diff(s, lr, curr_insn_len(s));
    gen_a64_set_pc(s, dst);
    set_btype_for_blr(s);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_RET(DisasContext *s, arg_r *a)
{
    gen_a64_set_pc(s, cpu_reg(s, a->rn));
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static TCGv_i64 auth_branch_target(DisasContext *s, TCGv_i64 dst,
                                   TCGv_i64 modifier, bool use_key_a)
{
    TCGv_i64 truedst;
    /*
     * Return the branch target for a BRAA/RETA/etc, which is either
     * just the destination dst, or that value with the pauth check
     * done and the code removed from the high bits.
     */
    if (!s->pauth_active) {
        return dst;
    }

    truedst = tcg_temp_new_i64();
    if (use_key_a) {
        gen_helper_autia_combined(truedst, tcg_env, dst, modifier);
    } else {
        gen_helper_autib_combined(truedst, tcg_env, dst, modifier);
    }
    return truedst;
}

static bool trans_BRAZ(DisasContext *s, arg_braz *a)
{
    TCGv_i64 dst;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }

    dst = auth_branch_target(s, cpu_reg(s, a->rn), tcg_constant_i64(0), !a->m);
    set_btype_for_br(s, a->rn);
    gen_a64_set_pc(s, dst);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_BLRAZ(DisasContext *s, arg_braz *a)
{
    TCGv_i64 dst, lr;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }

    dst = auth_branch_target(s, cpu_reg(s, a->rn), tcg_constant_i64(0), !a->m);
    lr = cpu_reg(s, 30);
    if (dst == lr) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_mov_i64(tmp, dst);
        dst = tmp;
    }
    gen_pc_plus_diff(s, lr, curr_insn_len(s));
    gen_a64_set_pc(s, dst);
    set_btype_for_blr(s);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_RETA(DisasContext *s, arg_reta *a)
{
    TCGv_i64 dst;

    dst = auth_branch_target(s, cpu_reg(s, 30), cpu_X[31], !a->m);
    gen_a64_set_pc(s, dst);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_BRA(DisasContext *s, arg_bra *a)
{
    TCGv_i64 dst;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }
    dst = auth_branch_target(s, cpu_reg(s,a->rn), cpu_reg_sp(s, a->rm), !a->m);
    gen_a64_set_pc(s, dst);
    set_btype_for_br(s, a->rn);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_BLRA(DisasContext *s, arg_bra *a)
{
    TCGv_i64 dst, lr;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }
    dst = auth_branch_target(s, cpu_reg(s, a->rn), cpu_reg_sp(s, a->rm), !a->m);
    lr = cpu_reg(s, 30);
    if (dst == lr) {
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_mov_i64(tmp, dst);
        dst = tmp;
    }
    gen_pc_plus_diff(s, lr, curr_insn_len(s));
    gen_a64_set_pc(s, dst);
    set_btype_for_blr(s);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_ERET(DisasContext *s, arg_ERET *a)
{
    TCGv_i64 dst;

    if (s->current_el == 0) {
        return false;
    }
    if (s->trap_eret) {
        gen_exception_insn_el(s, 0, EXCP_UDEF, syn_erettrap(0), 2);
        return true;
    }
    dst = tcg_temp_new_i64();
    tcg_gen_ld_i64(dst, tcg_env,
                   offsetof(CPUARMState, elr_el[s->current_el]));

    translator_io_start(&s->base);

    gen_helper_exception_return(tcg_env, dst);
    /* Must exit loop to check un-masked IRQs */
    s->base.is_jmp = DISAS_EXIT;
    return true;
}

static bool trans_ERETA(DisasContext *s, arg_reta *a)
{
    TCGv_i64 dst;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }
    if (s->current_el == 0) {
        return false;
    }
    /* The FGT trap takes precedence over an auth trap. */
    if (s->trap_eret) {
        gen_exception_insn_el(s, 0, EXCP_UDEF, syn_erettrap(a->m ? 3 : 2), 2);
        return true;
    }
    dst = tcg_temp_new_i64();
    tcg_gen_ld_i64(dst, tcg_env,
                   offsetof(CPUARMState, elr_el[s->current_el]));

    dst = auth_branch_target(s, dst, cpu_X[31], !a->m);

    translator_io_start(&s->base);

    gen_helper_exception_return(tcg_env, dst);
    /* Must exit loop to check un-masked IRQs */
    s->base.is_jmp = DISAS_EXIT;
    return true;
}

static bool trans_NOP(DisasContext *s, arg_NOP *a)
{
    return true;
}

static bool trans_YIELD(DisasContext *s, arg_YIELD *a)
{
    /*
     * When running in MTTCG we don't generate jumps to the yield and
     * WFE helpers as it won't affect the scheduling of other vCPUs.
     * If we wanted to more completely model WFE/SEV so we don't busy
     * spin unnecessarily we would need to do something more involved.
     */
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        s->base.is_jmp = DISAS_YIELD;
    }
    return true;
}

static bool trans_WFI(DisasContext *s, arg_WFI *a)
{
    s->base.is_jmp = DISAS_WFI;
    return true;
}

static bool trans_WFE(DisasContext *s, arg_WFI *a)
{
    /*
     * When running in MTTCG we don't generate jumps to the yield and
     * WFE helpers as it won't affect the scheduling of other vCPUs.
     * If we wanted to more completely model WFE/SEV so we don't busy
     * spin unnecessarily we would need to do something more involved.
     */
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        s->base.is_jmp = DISAS_WFE;
    }
    return true;
}

static bool trans_WFIT(DisasContext *s, arg_WFIT *a)
{
    if (!dc_isar_feature(aa64_wfxt, s)) {
        return false;
    }

    /*
     * Because we need to pass the register value to the helper,
     * it's easier to emit the code now, unlike trans_WFI which
     * defers it to aarch64_tr_tb_stop(). That means we need to
     * check ss_active so that single-stepping a WFIT doesn't halt.
     */
    if (s->ss_active) {
        /* Act like a NOP under architectural singlestep */
        return true;
    }

    gen_a64_update_pc(s, 4);
    gen_helper_wfit(tcg_env, cpu_reg(s, a->rd));
    /* Go back to the main loop to check for interrupts */
    s->base.is_jmp = DISAS_EXIT;
    return true;
}

static bool trans_WFET(DisasContext *s, arg_WFET *a)
{
    if (!dc_isar_feature(aa64_wfxt, s)) {
        return false;
    }

    /*
     * We rely here on our WFE implementation being a NOP, so we
     * don't need to do anything different to handle the WFET timeout
     * from what trans_WFE does.
     */
    if (!(tb_cflags(s->base.tb) & CF_PARALLEL)) {
        s->base.is_jmp = DISAS_WFE;
    }
    return true;
}

static bool trans_XPACLRI(DisasContext *s, arg_XPACLRI *a)
{
    if (s->pauth_active) {
        gen_helper_xpaci(cpu_X[30], tcg_env, cpu_X[30]);
    }
    return true;
}

static bool trans_PACIA1716(DisasContext *s, arg_PACIA1716 *a)
{
    if (s->pauth_active) {
        gen_helper_pacia(cpu_X[17], tcg_env, cpu_X[17], cpu_X[16]);
    }
    return true;
}

static bool trans_PACIB1716(DisasContext *s, arg_PACIB1716 *a)
{
    if (s->pauth_active) {
        gen_helper_pacib(cpu_X[17], tcg_env, cpu_X[17], cpu_X[16]);
    }
    return true;
}

static bool trans_AUTIA1716(DisasContext *s, arg_AUTIA1716 *a)
{
    if (s->pauth_active) {
        gen_helper_autia(cpu_X[17], tcg_env, cpu_X[17], cpu_X[16]);
    }
    return true;
}

static bool trans_AUTIB1716(DisasContext *s, arg_AUTIB1716 *a)
{
    if (s->pauth_active) {
        gen_helper_autib(cpu_X[17], tcg_env, cpu_X[17], cpu_X[16]);
    }
    return true;
}

static bool trans_ESB(DisasContext *s, arg_ESB *a)
{
    /* Without RAS, we must implement this as NOP. */
    if (dc_isar_feature(aa64_ras, s)) {
        /*
         * QEMU does not have a source of physical SErrors,
         * so we are only concerned with virtual SErrors.
         * The pseudocode in the ARM for this case is
         *   if PSTATE.EL IN {EL0, EL1} && EL2Enabled() then
         *      AArch64.vESBOperation();
         * Most of the condition can be evaluated at translation time.
         * Test for EL2 present, and defer test for SEL2 to runtime.
         */
        if (s->current_el <= 1 && arm_dc_feature(s, ARM_FEATURE_EL2)) {
            gen_helper_vesb(tcg_env);
        }
    }
    return true;
}

static bool trans_PACIAZ(DisasContext *s, arg_PACIAZ *a)
{
    if (s->pauth_active) {
        gen_helper_pacia(cpu_X[30], tcg_env, cpu_X[30], tcg_constant_i64(0));
    }
    return true;
}

static bool trans_PACIASP(DisasContext *s, arg_PACIASP *a)
{
    if (s->pauth_active) {
        gen_helper_pacia(cpu_X[30], tcg_env, cpu_X[30], cpu_X[31]);
    }
    return true;
}

static bool trans_PACIBZ(DisasContext *s, arg_PACIBZ *a)
{
    if (s->pauth_active) {
        gen_helper_pacib(cpu_X[30], tcg_env, cpu_X[30], tcg_constant_i64(0));
    }
    return true;
}

static bool trans_PACIBSP(DisasContext *s, arg_PACIBSP *a)
{
    if (s->pauth_active) {
        gen_helper_pacib(cpu_X[30], tcg_env, cpu_X[30], cpu_X[31]);
    }
    return true;
}

static bool trans_AUTIAZ(DisasContext *s, arg_AUTIAZ *a)
{
    if (s->pauth_active) {
        gen_helper_autia(cpu_X[30], tcg_env, cpu_X[30], tcg_constant_i64(0));
    }
    return true;
}

static bool trans_AUTIASP(DisasContext *s, arg_AUTIASP *a)
{
    if (s->pauth_active) {
        gen_helper_autia(cpu_X[30], tcg_env, cpu_X[30], cpu_X[31]);
    }
    return true;
}

static bool trans_AUTIBZ(DisasContext *s, arg_AUTIBZ *a)
{
    if (s->pauth_active) {
        gen_helper_autib(cpu_X[30], tcg_env, cpu_X[30], tcg_constant_i64(0));
    }
    return true;
}

static bool trans_AUTIBSP(DisasContext *s, arg_AUTIBSP *a)
{
    if (s->pauth_active) {
        gen_helper_autib(cpu_X[30], tcg_env, cpu_X[30], cpu_X[31]);
    }
    return true;
}

static bool trans_CLREX(DisasContext *s, arg_CLREX *a)
{
    tcg_gen_movi_i64(cpu_exclusive_addr, -1);
    return true;
}

static bool trans_DSB_DMB(DisasContext *s, arg_DSB_DMB *a)
{
    /* We handle DSB and DMB the same way */
    TCGBar bar;

    switch (a->types) {
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
    return true;
}

static bool trans_ISB(DisasContext *s, arg_ISB *a)
{
    /*
     * We need to break the TB after this insn to execute
     * self-modifying code correctly and also to take
     * any pending interrupts immediately.
     */
    reset_btype(s);
    gen_goto_tb(s, 0, 4);
    return true;
}

static bool trans_SB(DisasContext *s, arg_SB *a)
{
    if (!dc_isar_feature(aa64_sb, s)) {
        return false;
    }
    /*
     * TODO: There is no speculation barrier opcode for TCG;
     * MB and end the TB instead.
     */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    gen_goto_tb(s, 0, 4);
    return true;
}

static bool trans_CFINV(DisasContext *s, arg_CFINV *a)
{
    if (!dc_isar_feature(aa64_condm_4, s)) {
        return false;
    }
    tcg_gen_xori_i32(cpu_CF, cpu_CF, 1);
    return true;
}

static bool trans_XAFLAG(DisasContext *s, arg_XAFLAG *a)
{
    TCGv_i32 z;

    if (!dc_isar_feature(aa64_condm_5, s)) {
        return false;
    }

    z = tcg_temp_new_i32();

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

    return true;
}

static bool trans_AXFLAG(DisasContext *s, arg_AXFLAG *a)
{
    if (!dc_isar_feature(aa64_condm_5, s)) {
        return false;
    }

    tcg_gen_sari_i32(cpu_VF, cpu_VF, 31);         /* V ? -1 : 0 */
    tcg_gen_andc_i32(cpu_CF, cpu_CF, cpu_VF);     /* C & !V */

    /* !(Z | V) -> !(!ZF | V) -> ZF & !V -> ZF & ~VF */
    tcg_gen_andc_i32(cpu_ZF, cpu_ZF, cpu_VF);

    tcg_gen_movi_i32(cpu_NF, 0);
    tcg_gen_movi_i32(cpu_VF, 0);

    return true;
}

static bool trans_MSR_i_UAO(DisasContext *s, arg_i *a)
{
    if (!dc_isar_feature(aa64_uao, s) || s->current_el == 0) {
        return false;
    }
    if (a->imm & 1) {
        set_pstate_bits(PSTATE_UAO);
    } else {
        clear_pstate_bits(PSTATE_UAO);
    }
    gen_rebuild_hflags(s);
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_MSR_i_PAN(DisasContext *s, arg_i *a)
{
    if (!dc_isar_feature(aa64_pan, s) || s->current_el == 0) {
        return false;
    }
    if (a->imm & 1) {
        set_pstate_bits(PSTATE_PAN);
    } else {
        clear_pstate_bits(PSTATE_PAN);
    }
    gen_rebuild_hflags(s);
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_MSR_i_SPSEL(DisasContext *s, arg_i *a)
{
    if (s->current_el == 0) {
        return false;
    }
    gen_helper_msr_i_spsel(tcg_env, tcg_constant_i32(a->imm & PSTATE_SP));
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_MSR_i_SBSS(DisasContext *s, arg_i *a)
{
    if (!dc_isar_feature(aa64_ssbs, s)) {
        return false;
    }
    if (a->imm & 1) {
        set_pstate_bits(PSTATE_SSBS);
    } else {
        clear_pstate_bits(PSTATE_SSBS);
    }
    /* Don't need to rebuild hflags since SSBS is a nop */
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_MSR_i_DIT(DisasContext *s, arg_i *a)
{
    if (!dc_isar_feature(aa64_dit, s)) {
        return false;
    }
    if (a->imm & 1) {
        set_pstate_bits(PSTATE_DIT);
    } else {
        clear_pstate_bits(PSTATE_DIT);
    }
    /* There's no need to rebuild hflags because DIT is a nop */
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_MSR_i_TCO(DisasContext *s, arg_i *a)
{
    if (dc_isar_feature(aa64_mte, s)) {
        /* Full MTE is enabled -- set the TCO bit as directed. */
        if (a->imm & 1) {
            set_pstate_bits(PSTATE_TCO);
        } else {
            clear_pstate_bits(PSTATE_TCO);
        }
        gen_rebuild_hflags(s);
        /* Many factors, including TCO, go into MTE_ACTIVE. */
        s->base.is_jmp = DISAS_UPDATE_NOCHAIN;
        return true;
    } else if (dc_isar_feature(aa64_mte_insn_reg, s)) {
        /* Only "instructions accessible at EL0" -- PSTATE.TCO is WI.  */
        return true;
    } else {
        /* Insn not present */
        return false;
    }
}

static bool trans_MSR_i_DAIFSET(DisasContext *s, arg_i *a)
{
    gen_helper_msr_i_daifset(tcg_env, tcg_constant_i32(a->imm));
    s->base.is_jmp = DISAS_TOO_MANY;
    return true;
}

static bool trans_MSR_i_DAIFCLEAR(DisasContext *s, arg_i *a)
{
    gen_helper_msr_i_daifclear(tcg_env, tcg_constant_i32(a->imm));
    /* Exit the cpu loop to re-evaluate pending IRQs. */
    s->base.is_jmp = DISAS_UPDATE_EXIT;
    return true;
}

static bool trans_MSR_i_ALLINT(DisasContext *s, arg_i *a)
{
    if (!dc_isar_feature(aa64_nmi, s) || s->current_el == 0) {
        return false;
    }

    if (a->imm == 0) {
        clear_pstate_bits(PSTATE_ALLINT);
    } else if (s->current_el > 1) {
        set_pstate_bits(PSTATE_ALLINT);
    } else {
        gen_helper_msr_set_allint_el1(tcg_env);
    }

    /* Exit the cpu loop to re-evaluate pending IRQs. */
    s->base.is_jmp = DISAS_UPDATE_EXIT;
    return true;
}

static bool trans_MSR_i_SVCR(DisasContext *s, arg_MSR_i_SVCR *a)
{
    if (!dc_isar_feature(aa64_sme, s) || a->mask == 0) {
        return false;
    }
    if (sme_access_check(s)) {
        int old = s->pstate_sm | (s->pstate_za << 1);
        int new = a->imm * 3;

        if ((old ^ new) & a->mask) {
            /* At least one bit changes. */
            gen_helper_set_svcr(tcg_env, tcg_constant_i32(new),
                                tcg_constant_i32(a->mask));
            s->base.is_jmp = DISAS_TOO_MANY;
        }
    }
    return true;
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
}

static void gen_sysreg_undef(DisasContext *s, bool isread,
                             uint8_t op0, uint8_t op1, uint8_t op2,
                             uint8_t crn, uint8_t crm, uint8_t rt)
{
    /*
     * Generate code to emit an UNDEF with correct syndrome
     * information for a failed system register access.
     * This is EC_UNCATEGORIZED (ie a standard UNDEF) in most cases,
     * but if FEAT_IDST is implemented then read accesses to registers
     * in the feature ID space are reported with the EC_SYSTEMREGISTERTRAP
     * syndrome.
     */
    uint32_t syndrome;

    if (isread && dc_isar_feature(aa64_ids, s) &&
        arm_cpreg_encoding_in_idspace(op0, op1, op2, crn, crm)) {
        syndrome = syn_aa64_sysregtrap(op0, op1, op2, crn, crm, rt, isread);
    } else {
        syndrome = syn_uncategorized();
    }
    gen_exception_insn(s, 0, EXCP_UDEF, syndrome);
}

/* MRS - move from system register
 * MSR (register) - move to system register
 * SYS
 * SYSL
 * These are all essentially the same insn in 'read' and 'write'
 * versions, with varying op0 fields.
 */
static void handle_sys(DisasContext *s, bool isread,
                       unsigned int op0, unsigned int op1, unsigned int op2,
                       unsigned int crn, unsigned int crm, unsigned int rt)
{
    uint32_t key = ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP,
                                      crn, crm, op0, op1, op2);
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(s->cp_regs, key);
    bool need_exit_tb = false;
    bool nv_trap_to_el2 = false;
    bool nv_redirect_reg = false;
    bool skip_fp_access_checks = false;
    bool nv2_mem_redirect = false;
    TCGv_ptr tcg_ri = NULL;
    TCGv_i64 tcg_rt;
    uint32_t syndrome = syn_aa64_sysregtrap(op0, op1, op2, crn, crm, rt, isread);

    if (crn == 11 || crn == 15) {
        /*
         * Check for TIDCP trap, which must take precedence over
         * the UNDEF for "no such register" etc.
         */
        switch (s->current_el) {
        case 0:
            if (dc_isar_feature(aa64_tidcp1, s)) {
                gen_helper_tidcp_el0(tcg_env, tcg_constant_i32(syndrome));
            }
            break;
        case 1:
            gen_helper_tidcp_el1(tcg_env, tcg_constant_i32(syndrome));
            break;
        }
    }

    if (!ri) {
        /* Unknown register; this might be a guest error or a QEMU
         * unimplemented feature.
         */
        qemu_log_mask(LOG_UNIMP, "%s access to unsupported AArch64 "
                      "system register op0:%d op1:%d crn:%d crm:%d op2:%d\n",
                      isread ? "read" : "write", op0, op1, crn, crm, op2);
        gen_sysreg_undef(s, isread, op0, op1, op2, crn, crm, rt);
        return;
    }

    if (s->nv2 && ri->nv2_redirect_offset) {
        /*
         * Some registers always redirect to memory; some only do so if
         * HCR_EL2.NV1 is 0, and some only if NV1 is 1 (these come in
         * pairs which share an offset; see the table in R_CSRPQ).
         */
        if (ri->nv2_redirect_offset & NV2_REDIR_NV1) {
            nv2_mem_redirect = s->nv1;
        } else if (ri->nv2_redirect_offset & NV2_REDIR_NO_NV1) {
            nv2_mem_redirect = !s->nv1;
        } else {
            nv2_mem_redirect = true;
        }
    }

    /* Check access permissions */
    if (!cp_access_ok(s->current_el, ri, isread)) {
        /*
         * FEAT_NV/NV2 handling does not do the usual FP access checks
         * for registers only accessible at EL2 (though it *does* do them
         * for registers accessible at EL1).
         */
        skip_fp_access_checks = true;
        if (s->nv2 && (ri->type & ARM_CP_NV2_REDIRECT)) {
            /*
             * This is one of the few EL2 registers which should redirect
             * to the equivalent EL1 register. We do that after running
             * the EL2 register's accessfn.
             */
            nv_redirect_reg = true;
            assert(!nv2_mem_redirect);
        } else if (nv2_mem_redirect) {
            /*
             * NV2 redirect-to-memory takes precedence over trap to EL2 or
             * UNDEF to EL1.
             */
        } else if (s->nv && arm_cpreg_traps_in_nv(ri)) {
            /*
             * This register / instruction exists and is an EL2 register, so
             * we must trap to EL2 if accessed in nested virtualization EL1
             * instead of UNDEFing. We'll do that after the usual access checks.
             * (This makes a difference only for a couple of registers like
             * VSTTBR_EL2 where the "UNDEF if NonSecure" should take priority
             * over the trap-to-EL2. Most trapped-by-FEAT_NV registers have
             * an accessfn which does nothing when called from EL1, because
             * the trap-to-EL3 controls which would apply to that register
             * at EL2 don't take priority over the FEAT_NV trap-to-EL2.)
             */
            nv_trap_to_el2 = true;
        } else {
            gen_sysreg_undef(s, isread, op0, op1, op2, crn, crm, rt);
            return;
        }
    }

    if (ri->accessfn || (ri->fgt && s->fgt_active)) {
        /* Emit code to perform further access permissions checks at
         * runtime; this may result in an exception.
         */
        gen_a64_update_pc(s, 0);
        tcg_ri = tcg_temp_new_ptr();
        gen_helper_access_check_cp_reg(tcg_ri, tcg_env,
                                       tcg_constant_i32(key),
                                       tcg_constant_i32(syndrome),
                                       tcg_constant_i32(isread));
    } else if (ri->type & ARM_CP_RAISES_EXC) {
        /*
         * The readfn or writefn might raise an exception;
         * synchronize the CPU state in case it does.
         */
        gen_a64_update_pc(s, 0);
    }

    if (!skip_fp_access_checks) {
        if ((ri->type & ARM_CP_FPU) && !fp_access_check_only(s)) {
            return;
        } else if ((ri->type & ARM_CP_SVE) && !sve_access_check(s)) {
            return;
        } else if ((ri->type & ARM_CP_SME) && !sme_access_check(s)) {
            return;
        }
    }

    if (nv_trap_to_el2) {
        gen_exception_insn_el(s, 0, EXCP_UDEF, syndrome, 2);
        return;
    }

    if (nv_redirect_reg) {
        /*
         * FEAT_NV2 redirection of an EL2 register to an EL1 register.
         * Conveniently in all cases the encoding of the EL1 register is
         * identical to the EL2 register except that opc1 is 0.
         * Get the reginfo for the EL1 register to use for the actual access.
         * We don't use the EL1 register's access function, and
         * fine-grained-traps on EL1 also do not apply here.
         */
        key = ENCODE_AA64_CP_REG(CP_REG_ARM64_SYSREG_CP,
                                 crn, crm, op0, 0, op2);
        ri = get_arm_cp_reginfo(s->cp_regs, key);
        assert(ri);
        assert(cp_access_ok(s->current_el, ri, isread));
        /*
         * We might not have done an update_pc earlier, so check we don't
         * need it. We could support this in future if necessary.
         */
        assert(!(ri->type & ARM_CP_RAISES_EXC));
    }

    if (nv2_mem_redirect) {
        /*
         * This system register is being redirected into an EL2 memory access.
         * This means it is not an IO operation, doesn't change hflags,
         * and need not end the TB, because it has no side effects.
         *
         * The access is 64-bit single copy atomic, guaranteed aligned because
         * of the definition of VCNR_EL2. Its endianness depends on
         * SCTLR_EL2.EE, not on the data endianness of EL1.
         * It is done under either the EL2 translation regime or the EL2&0
         * translation regime, depending on HCR_EL2.E2H. It behaves as if
         * PSTATE.PAN is 0.
         */
        TCGv_i64 ptr = tcg_temp_new_i64();
        MemOp mop = MO_64 | MO_ALIGN | MO_ATOM_IFALIGN;
        ARMMMUIdx armmemidx = s->nv2_mem_e20 ? ARMMMUIdx_E20_2 : ARMMMUIdx_E2;
        int memidx = arm_to_core_mmu_idx(armmemidx);
        uint32_t syn;

        mop |= (s->nv2_mem_be ? MO_BE : MO_LE);

        tcg_gen_ld_i64(ptr, tcg_env, offsetof(CPUARMState, cp15.vncr_el2));
        tcg_gen_addi_i64(ptr, ptr,
                         (ri->nv2_redirect_offset & ~NV2_REDIR_FLAG_MASK));
        tcg_rt = cpu_reg(s, rt);

        syn = syn_data_abort_vncr(0, !isread, 0);
        disas_set_insn_syndrome(s, syn);
        if (isread) {
            tcg_gen_qemu_ld_i64(tcg_rt, ptr, memidx, mop);
        } else {
            tcg_gen_qemu_st_i64(tcg_rt, ptr, memidx, mop);
        }
        return;
    }

    /* Handle special cases first */
    switch (ri->type & ARM_CP_SPECIAL_MASK) {
    case 0:
        break;
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
    {
        /*
         * Reads as current EL value from pstate, which is
         * guaranteed to be constant by the tb flags.
         * For nested virt we should report EL2.
         */
        int el = s->nv ? 2 : s->current_el;
        tcg_rt = cpu_reg(s, rt);
        tcg_gen_movi_i64(tcg_rt, el << 2);
        return;
    }
    case ARM_CP_DC_ZVA:
        /* Writes clear the aligned block of memory which rt points into. */
        if (s->mte_active[0]) {
            int desc = 0;

            desc = FIELD_DP32(desc, MTEDESC, MIDX, get_mem_index(s));
            desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
            desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);

            tcg_rt = tcg_temp_new_i64();
            gen_helper_mte_check_zva(tcg_rt, tcg_env,
                                     tcg_constant_i32(desc), cpu_reg(s, rt));
        } else {
            tcg_rt = clean_data_tbi(s, cpu_reg(s, rt));
        }
        gen_helper_dc_zva(tcg_env, tcg_rt);
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

            if (s->ata[0]) {
                /* Extract the tag from the register to match STZGM.  */
                tag = tcg_temp_new_i64();
                tcg_gen_shri_i64(tag, tcg_rt, 56);
                gen_helper_stzgm_tags(tcg_env, clean_addr, tag);
            }
        }
        return;
    case ARM_CP_DC_GZVA:
        {
            TCGv_i64 clean_addr, tag;

            /* For DC_GZVA, we can rely on DC_ZVA for the proper fault. */
            tcg_rt = cpu_reg(s, rt);
            clean_addr = clean_data_tbi(s, tcg_rt);
            gen_helper_dc_zva(tcg_env, clean_addr);

            if (s->ata[0]) {
                /* Extract the tag from the register to match STZGM.  */
                tag = tcg_temp_new_i64();
                tcg_gen_shri_i64(tag, tcg_rt, 56);
                gen_helper_stzgm_tags(tcg_env, clean_addr, tag);
            }
        }
        return;
    default:
        g_assert_not_reached();
    }

    if (ri->type & ARM_CP_IO) {
        /* I/O operations must end the TB here (whether read or write) */
        need_exit_tb = translator_io_start(&s->base);
    }

    tcg_rt = cpu_reg(s, rt);

    if (isread) {
        if (ri->type & ARM_CP_CONST) {
            tcg_gen_movi_i64(tcg_rt, ri->resetvalue);
        } else if (ri->readfn) {
            if (!tcg_ri) {
                tcg_ri = gen_lookup_cp_reg(key);
            }
            gen_helper_get_cp_reg64(tcg_rt, tcg_env, tcg_ri);
        } else {
            tcg_gen_ld_i64(tcg_rt, tcg_env, ri->fieldoffset);
        }
    } else {
        if (ri->type & ARM_CP_CONST) {
            /* If not forbidden by access permissions, treat as WI */
            return;
        } else if (ri->writefn) {
            if (!tcg_ri) {
                tcg_ri = gen_lookup_cp_reg(key);
            }
            gen_helper_set_cp_reg64(tcg_env, tcg_ri, tcg_rt);
        } else {
            tcg_gen_st_i64(tcg_rt, tcg_env, ri->fieldoffset);
        }
    }

    if (!isread && !(ri->type & ARM_CP_SUPPRESS_TB_END)) {
        /*
         * A write to any coprocessor register that ends a TB
         * must rebuild the hflags for the next TB.
         */
        gen_rebuild_hflags(s);
        /*
         * We default to ending the TB on a coprocessor register write,
         * but allow this to be suppressed by the register definition
         * (usually only necessary to work around guest bugs).
         */
        need_exit_tb = true;
    }
    if (need_exit_tb) {
        s->base.is_jmp = DISAS_UPDATE_EXIT;
    }
}

static bool trans_SYS(DisasContext *s, arg_SYS *a)
{
    handle_sys(s, a->l, a->op0, a->op1, a->op2, a->crn, a->crm, a->rt);
    return true;
}

static bool trans_SVC(DisasContext *s, arg_i *a)
{
    /*
     * For SVC, HVC and SMC we advance the single-step state
     * machine before taking the exception. This is architecturally
     * mandated, to ensure that single-stepping a system call
     * instruction works properly.
     */
    uint32_t syndrome = syn_aa64_svc(a->imm);
    if (s->fgt_svc) {
        gen_exception_insn_el(s, 0, EXCP_UDEF, syndrome, 2);
        return true;
    }
    gen_ss_advance(s);
    gen_exception_insn(s, 4, EXCP_SWI, syndrome);
    return true;
}

static bool trans_HVC(DisasContext *s, arg_i *a)
{
    int target_el = s->current_el == 3 ? 3 : 2;

    if (s->current_el == 0) {
        unallocated_encoding(s);
        return true;
    }
    /*
     * The pre HVC helper handles cases when HVC gets trapped
     * as an undefined insn by runtime configuration.
     */
    gen_a64_update_pc(s, 0);
    gen_helper_pre_hvc(tcg_env);
    /* Architecture requires ss advance before we do the actual work */
    gen_ss_advance(s);
    gen_exception_insn_el(s, 4, EXCP_HVC, syn_aa64_hvc(a->imm), target_el);
    return true;
}

static bool trans_SMC(DisasContext *s, arg_i *a)
{
    if (s->current_el == 0) {
        unallocated_encoding(s);
        return true;
    }
    gen_a64_update_pc(s, 0);
    gen_helper_pre_smc(tcg_env, tcg_constant_i32(syn_aa64_smc(a->imm)));
    /* Architecture requires ss advance before we do the actual work */
    gen_ss_advance(s);
    gen_exception_insn_el(s, 4, EXCP_SMC, syn_aa64_smc(a->imm), 3);
    return true;
}

static bool trans_BRK(DisasContext *s, arg_i *a)
{
    gen_exception_bkpt_insn(s, syn_aa64_bkpt(a->imm));
    return true;
}

static bool trans_HLT(DisasContext *s, arg_i *a)
{
    /*
     * HLT. This has two purposes.
     * Architecturally, it is an external halting debug instruction.
     * Since QEMU doesn't implement external debug, we treat this as
     * it is required for halting debug disabled: it will UNDEF.
     * Secondly, "HLT 0xf000" is the A64 semihosting syscall instruction.
     */
    if (semihosting_enabled(s->current_el == 0) && a->imm == 0xf000) {
        gen_exception_internal_insn(s, EXCP_SEMIHOST);
    } else {
        unallocated_encoding(s);
    }
    return true;
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
static void gen_load_exclusive(DisasContext *s, int rt, int rt2, int rn,
                               int size, bool is_pair)
{
    int idx = get_mem_index(s);
    TCGv_i64 dirty_addr, clean_addr;
    MemOp memop = check_atomic_align(s, rn, size + is_pair);

    s->is_ldex = true;
    dirty_addr = cpu_reg_sp(s, rn);
    clean_addr = gen_mte_check1(s, dirty_addr, false, rn != 31, memop);

    g_assert(size <= 3);
    if (is_pair) {
        g_assert(size >= 2);
        if (size == 2) {
            tcg_gen_qemu_ld_i64(cpu_exclusive_val, clean_addr, idx, memop);
            if (s->be_data == MO_LE) {
                tcg_gen_extract_i64(cpu_reg(s, rt), cpu_exclusive_val, 0, 32);
                tcg_gen_extract_i64(cpu_reg(s, rt2), cpu_exclusive_val, 32, 32);
            } else {
                tcg_gen_extract_i64(cpu_reg(s, rt), cpu_exclusive_val, 32, 32);
                tcg_gen_extract_i64(cpu_reg(s, rt2), cpu_exclusive_val, 0, 32);
            }
        } else {
            TCGv_i128 t16 = tcg_temp_new_i128();

            tcg_gen_qemu_ld_i128(t16, clean_addr, idx, memop);

            if (s->be_data == MO_LE) {
                tcg_gen_extr_i128_i64(cpu_exclusive_val,
                                      cpu_exclusive_high, t16);
            } else {
                tcg_gen_extr_i128_i64(cpu_exclusive_high,
                                      cpu_exclusive_val, t16);
            }
            tcg_gen_mov_i64(cpu_reg(s, rt), cpu_exclusive_val);
            tcg_gen_mov_i64(cpu_reg(s, rt2), cpu_exclusive_high);
        }
    } else {
        tcg_gen_qemu_ld_i64(cpu_exclusive_val, clean_addr, idx, memop);
        tcg_gen_mov_i64(cpu_reg(s, rt), cpu_exclusive_val);
    }
    tcg_gen_mov_i64(cpu_exclusive_addr, clean_addr);
}

static void gen_store_exclusive(DisasContext *s, int rd, int rt, int rt2,
                                int rn, int size, int is_pair)
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
    TCGv_i64 tmp, clean_addr;
    MemOp memop;

    /*
     * FIXME: We are out of spec here.  We have recorded only the address
     * from load_exclusive, not the entire range, and we assume that the
     * size of the access on both sides match.  The architecture allows the
     * store to be smaller than the load, so long as the stored bytes are
     * within the range recorded by the load.
     */

    /* See AArch64.ExclusiveMonitorsPass() and AArch64.IsExclusiveVA(). */
    clean_addr = clean_data_tbi(s, cpu_reg_sp(s, rn));
    tcg_gen_brcond_i64(TCG_COND_NE, clean_addr, cpu_exclusive_addr, fail_label);

    /*
     * The write, and any associated faults, only happen if the virtual
     * and physical addresses pass the exclusive monitor check.  These
     * faults are exceedingly unlikely, because normally the guest uses
     * the exact same address register for the load_exclusive, and we
     * would have recognized these faults there.
     *
     * It is possible to trigger an alignment fault pre-LSE2, e.g. with an
     * unaligned 4-byte write within the range of an aligned 8-byte load.
     * With LSE2, the store would need to cross a 16-byte boundary when the
     * load did not, which would mean the store is outside the range
     * recorded for the monitor, which would have failed a corrected monitor
     * check above.  For now, we assume no size change and retain the
     * MO_ALIGN to let tcg know what we checked in the load_exclusive.
     *
     * It is possible to trigger an MTE fault, by performing the load with
     * a virtual address with a valid tag and performing the store with the
     * same virtual address and a different invalid tag.
     */
    memop = size + is_pair;
    if (memop == MO_128 || !dc_isar_feature(aa64_lse2, s)) {
        memop |= MO_ALIGN;
    }
    memop = finalize_memop(s, memop);
    gen_mte_check1(s, cpu_reg_sp(s, rn), true, rn != 31, memop);

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
                                       get_mem_index(s), memop);
            tcg_gen_setcond_i64(TCG_COND_NE, tmp, tmp, cpu_exclusive_val);
        } else {
            TCGv_i128 t16 = tcg_temp_new_i128();
            TCGv_i128 c16 = tcg_temp_new_i128();
            TCGv_i64 a, b;

            if (s->be_data == MO_LE) {
                tcg_gen_concat_i64_i128(t16, cpu_reg(s, rt), cpu_reg(s, rt2));
                tcg_gen_concat_i64_i128(c16, cpu_exclusive_val,
                                        cpu_exclusive_high);
            } else {
                tcg_gen_concat_i64_i128(t16, cpu_reg(s, rt2), cpu_reg(s, rt));
                tcg_gen_concat_i64_i128(c16, cpu_exclusive_high,
                                        cpu_exclusive_val);
            }

            tcg_gen_atomic_cmpxchg_i128(t16, cpu_exclusive_addr, c16, t16,
                                        get_mem_index(s), memop);

            a = tcg_temp_new_i64();
            b = tcg_temp_new_i64();
            if (s->be_data == MO_LE) {
                tcg_gen_extr_i128_i64(a, b, t16);
            } else {
                tcg_gen_extr_i128_i64(b, a, t16);
            }

            tcg_gen_xor_i64(a, a, cpu_exclusive_val);
            tcg_gen_xor_i64(b, b, cpu_exclusive_high);
            tcg_gen_or_i64(tmp, a, b);

            tcg_gen_setcondi_i64(TCG_COND_NE, tmp, tmp, 0);
        }
    } else {
        tcg_gen_atomic_cmpxchg_i64(tmp, cpu_exclusive_addr, cpu_exclusive_val,
                                   cpu_reg(s, rt), get_mem_index(s), memop);
        tcg_gen_setcond_i64(TCG_COND_NE, tmp, tmp, cpu_exclusive_val);
    }
    tcg_gen_mov_i64(cpu_reg(s, rd), tmp);
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
    MemOp memop;

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    memop = check_atomic_align(s, rn, size);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn), true, rn != 31, memop);
    tcg_gen_atomic_cmpxchg_i64(tcg_rs, clean_addr, tcg_rs, tcg_rt,
                               memidx, memop);
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
    MemOp memop;

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    /* This is a single atomic access, despite the "pair". */
    memop = check_atomic_align(s, rn, size + 1);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, rn), true, rn != 31, memop);

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

        tcg_gen_atomic_cmpxchg_i64(cmp, clean_addr, cmp, val, memidx, memop);

        if (s->be_data == MO_LE) {
            tcg_gen_extr32_i64(s1, s2, cmp);
        } else {
            tcg_gen_extr32_i64(s2, s1, cmp);
        }
    } else {
        TCGv_i128 cmp = tcg_temp_new_i128();
        TCGv_i128 val = tcg_temp_new_i128();

        if (s->be_data == MO_LE) {
            tcg_gen_concat_i64_i128(val, t1, t2);
            tcg_gen_concat_i64_i128(cmp, s1, s2);
        } else {
            tcg_gen_concat_i64_i128(val, t2, t1);
            tcg_gen_concat_i64_i128(cmp, s2, s1);
        }

        tcg_gen_atomic_cmpxchg_i128(cmp, clean_addr, cmp, val, memidx, memop);

        if (s->be_data == MO_LE) {
            tcg_gen_extr_i128_i64(s1, s2, cmp);
        } else {
            tcg_gen_extr_i128_i64(s2, s1, cmp);
        }
    }
}

/*
 * Compute the ISS.SF bit for syndrome information if an exception
 * is taken on a load or store. This indicates whether the instruction
 * is accessing a 32-bit or 64-bit register. This logic is derived
 * from the ARMv8 specs for LDR (Shared decode for all encodings).
 */
static bool ldst_iss_sf(int size, bool sign, bool ext)
{

    if (sign) {
        /*
         * Signed loads are 64 bit results if we are not going to
         * do a zero-extend from 32 to 64 after the load.
         * (For a store, sign and ext are always false.)
         */
        return !ext;
    } else {
        /* Unsigned loads/stores work at the specified size */
        return size == MO_64;
    }
}

static bool trans_STXR(DisasContext *s, arg_stxr *a)
{
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    if (a->lasr) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }
    gen_store_exclusive(s, a->rs, a->rt, a->rt2, a->rn, a->sz, false);
    return true;
}

static bool trans_LDXR(DisasContext *s, arg_stxr *a)
{
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    gen_load_exclusive(s, a->rt, a->rt2, a->rn, a->sz, false);
    if (a->lasr) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }
    return true;
}

static bool trans_STLR(DisasContext *s, arg_stlr *a)
{
    TCGv_i64 clean_addr;
    MemOp memop;
    bool iss_sf = ldst_iss_sf(a->sz, false, false);

    /*
     * StoreLORelease is the same as Store-Release for QEMU, but
     * needs the feature-test.
     */
    if (!a->lasr && !dc_isar_feature(aa64_lor, s)) {
        return false;
    }
    /* Generate ISS for non-exclusive accesses including LASR.  */
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    memop = check_ordered_align(s, a->rn, 0, true, a->sz);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, a->rn),
                                true, a->rn != 31, memop);
    do_gpr_st(s, cpu_reg(s, a->rt), clean_addr, memop, true, a->rt,
              iss_sf, a->lasr);
    return true;
}

static bool trans_LDAR(DisasContext *s, arg_stlr *a)
{
    TCGv_i64 clean_addr;
    MemOp memop;
    bool iss_sf = ldst_iss_sf(a->sz, false, false);

    /* LoadLOAcquire is the same as Load-Acquire for QEMU.  */
    if (!a->lasr && !dc_isar_feature(aa64_lor, s)) {
        return false;
    }
    /* Generate ISS for non-exclusive accesses including LASR.  */
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    memop = check_ordered_align(s, a->rn, 0, false, a->sz);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, a->rn),
                                false, a->rn != 31, memop);
    do_gpr_ld(s, cpu_reg(s, a->rt), clean_addr, memop, false, true,
              a->rt, iss_sf, a->lasr);
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    return true;
}

static bool trans_STXP(DisasContext *s, arg_stxr *a)
{
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    if (a->lasr) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    }
    gen_store_exclusive(s, a->rs, a->rt, a->rt2, a->rn, a->sz, true);
    return true;
}

static bool trans_LDXP(DisasContext *s, arg_stxr *a)
{
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    gen_load_exclusive(s, a->rt, a->rt2, a->rn, a->sz, true);
    if (a->lasr) {
        tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    }
    return true;
}

static bool trans_CASP(DisasContext *s, arg_CASP *a)
{
    if (!dc_isar_feature(aa64_atomics, s)) {
        return false;
    }
    if (((a->rt | a->rs) & 1) != 0) {
        return false;
    }

    gen_compare_and_swap_pair(s, a->rs, a->rt, a->rn, a->sz);
    return true;
}

static bool trans_CAS(DisasContext *s, arg_CAS *a)
{
    if (!dc_isar_feature(aa64_atomics, s)) {
        return false;
    }
    gen_compare_and_swap(s, a->rs, a->rt, a->rn, a->sz);
    return true;
}

static bool trans_LD_lit(DisasContext *s, arg_ldlit *a)
{
    bool iss_sf = ldst_iss_sf(a->sz, a->sign, false);
    TCGv_i64 tcg_rt = cpu_reg(s, a->rt);
    TCGv_i64 clean_addr = tcg_temp_new_i64();
    MemOp memop = finalize_memop(s, a->sz + a->sign * MO_SIGN);

    gen_pc_plus_diff(s, clean_addr, a->imm);
    do_gpr_ld(s, tcg_rt, clean_addr, memop,
              false, true, a->rt, iss_sf, false);
    return true;
}

static bool trans_LD_lit_v(DisasContext *s, arg_ldlit *a)
{
    /* Load register (literal), vector version */
    TCGv_i64 clean_addr;
    MemOp memop;

    if (!fp_access_check(s)) {
        return true;
    }
    memop = finalize_memop_asimd(s, a->sz);
    clean_addr = tcg_temp_new_i64();
    gen_pc_plus_diff(s, clean_addr, a->imm);
    do_fp_ld(s, a->rt, clean_addr, memop);
    return true;
}

static void op_addr_ldstpair_pre(DisasContext *s, arg_ldstpair *a,
                                 TCGv_i64 *clean_addr, TCGv_i64 *dirty_addr,
                                 uint64_t offset, bool is_store, MemOp mop)
{
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    *dirty_addr = read_cpu_reg_sp(s, a->rn, 1);
    if (!a->p) {
        tcg_gen_addi_i64(*dirty_addr, *dirty_addr, offset);
    }

    *clean_addr = gen_mte_checkN(s, *dirty_addr, is_store,
                                 (a->w || a->rn != 31), 2 << a->sz, mop);
}

static void op_addr_ldstpair_post(DisasContext *s, arg_ldstpair *a,
                                  TCGv_i64 dirty_addr, uint64_t offset)
{
    if (a->w) {
        if (a->p) {
            tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, a->rn), dirty_addr);
    }
}

static bool trans_STP(DisasContext *s, arg_ldstpair *a)
{
    uint64_t offset = a->imm << a->sz;
    TCGv_i64 clean_addr, dirty_addr, tcg_rt, tcg_rt2;
    MemOp mop = finalize_memop(s, a->sz);

    op_addr_ldstpair_pre(s, a, &clean_addr, &dirty_addr, offset, true, mop);
    tcg_rt = cpu_reg(s, a->rt);
    tcg_rt2 = cpu_reg(s, a->rt2);
    /*
     * We built mop above for the single logical access -- rebuild it
     * now for the paired operation.
     *
     * With LSE2, non-sign-extending pairs are treated atomically if
     * aligned, and if unaligned one of the pair will be completely
     * within a 16-byte block and that element will be atomic.
     * Otherwise each element is separately atomic.
     * In all cases, issue one operation with the correct atomicity.
     */
    mop = a->sz + 1;
    if (s->align_mem) {
        mop |= (a->sz == 2 ? MO_ALIGN_4 : MO_ALIGN_8);
    }
    mop = finalize_memop_pair(s, mop);
    if (a->sz == 2) {
        TCGv_i64 tmp = tcg_temp_new_i64();

        if (s->be_data == MO_LE) {
            tcg_gen_concat32_i64(tmp, tcg_rt, tcg_rt2);
        } else {
            tcg_gen_concat32_i64(tmp, tcg_rt2, tcg_rt);
        }
        tcg_gen_qemu_st_i64(tmp, clean_addr, get_mem_index(s), mop);
    } else {
        TCGv_i128 tmp = tcg_temp_new_i128();

        if (s->be_data == MO_LE) {
            tcg_gen_concat_i64_i128(tmp, tcg_rt, tcg_rt2);
        } else {
            tcg_gen_concat_i64_i128(tmp, tcg_rt2, tcg_rt);
        }
        tcg_gen_qemu_st_i128(tmp, clean_addr, get_mem_index(s), mop);
    }
    op_addr_ldstpair_post(s, a, dirty_addr, offset);
    return true;
}

static bool trans_LDP(DisasContext *s, arg_ldstpair *a)
{
    uint64_t offset = a->imm << a->sz;
    TCGv_i64 clean_addr, dirty_addr, tcg_rt, tcg_rt2;
    MemOp mop = finalize_memop(s, a->sz);

    op_addr_ldstpair_pre(s, a, &clean_addr, &dirty_addr, offset, false, mop);
    tcg_rt = cpu_reg(s, a->rt);
    tcg_rt2 = cpu_reg(s, a->rt2);

    /*
     * We built mop above for the single logical access -- rebuild it
     * now for the paired operation.
     *
     * With LSE2, non-sign-extending pairs are treated atomically if
     * aligned, and if unaligned one of the pair will be completely
     * within a 16-byte block and that element will be atomic.
     * Otherwise each element is separately atomic.
     * In all cases, issue one operation with the correct atomicity.
     *
     * This treats sign-extending loads like zero-extending loads,
     * since that reuses the most code below.
     */
    mop = a->sz + 1;
    if (s->align_mem) {
        mop |= (a->sz == 2 ? MO_ALIGN_4 : MO_ALIGN_8);
    }
    mop = finalize_memop_pair(s, mop);
    if (a->sz == 2) {
        int o2 = s->be_data == MO_LE ? 32 : 0;
        int o1 = o2 ^ 32;

        tcg_gen_qemu_ld_i64(tcg_rt, clean_addr, get_mem_index(s), mop);
        if (a->sign) {
            tcg_gen_sextract_i64(tcg_rt2, tcg_rt, o2, 32);
            tcg_gen_sextract_i64(tcg_rt, tcg_rt, o1, 32);
        } else {
            tcg_gen_extract_i64(tcg_rt2, tcg_rt, o2, 32);
            tcg_gen_extract_i64(tcg_rt, tcg_rt, o1, 32);
        }
    } else {
        TCGv_i128 tmp = tcg_temp_new_i128();

        tcg_gen_qemu_ld_i128(tmp, clean_addr, get_mem_index(s), mop);
        if (s->be_data == MO_LE) {
            tcg_gen_extr_i128_i64(tcg_rt, tcg_rt2, tmp);
        } else {
            tcg_gen_extr_i128_i64(tcg_rt2, tcg_rt, tmp);
        }
    }
    op_addr_ldstpair_post(s, a, dirty_addr, offset);
    return true;
}

static bool trans_STP_v(DisasContext *s, arg_ldstpair *a)
{
    uint64_t offset = a->imm << a->sz;
    TCGv_i64 clean_addr, dirty_addr;
    MemOp mop;

    if (!fp_access_check(s)) {
        return true;
    }

    /* LSE2 does not merge FP pairs; leave these as separate operations. */
    mop = finalize_memop_asimd(s, a->sz);
    op_addr_ldstpair_pre(s, a, &clean_addr, &dirty_addr, offset, true, mop);
    do_fp_st(s, a->rt, clean_addr, mop);
    tcg_gen_addi_i64(clean_addr, clean_addr, 1 << a->sz);
    do_fp_st(s, a->rt2, clean_addr, mop);
    op_addr_ldstpair_post(s, a, dirty_addr, offset);
    return true;
}

static bool trans_LDP_v(DisasContext *s, arg_ldstpair *a)
{
    uint64_t offset = a->imm << a->sz;
    TCGv_i64 clean_addr, dirty_addr;
    MemOp mop;

    if (!fp_access_check(s)) {
        return true;
    }

    /* LSE2 does not merge FP pairs; leave these as separate operations. */
    mop = finalize_memop_asimd(s, a->sz);
    op_addr_ldstpair_pre(s, a, &clean_addr, &dirty_addr, offset, false, mop);
    do_fp_ld(s, a->rt, clean_addr, mop);
    tcg_gen_addi_i64(clean_addr, clean_addr, 1 << a->sz);
    do_fp_ld(s, a->rt2, clean_addr, mop);
    op_addr_ldstpair_post(s, a, dirty_addr, offset);
    return true;
}

static bool trans_STGP(DisasContext *s, arg_ldstpair *a)
{
    TCGv_i64 clean_addr, dirty_addr, tcg_rt, tcg_rt2;
    uint64_t offset = a->imm << LOG2_TAG_GRANULE;
    MemOp mop;
    TCGv_i128 tmp;

    /* STGP only comes in one size. */
    tcg_debug_assert(a->sz == MO_64);

    if (!dc_isar_feature(aa64_mte_insn_reg, s)) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    dirty_addr = read_cpu_reg_sp(s, a->rn, 1);
    if (!a->p) {
        tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
    }

    clean_addr = clean_data_tbi(s, dirty_addr);
    tcg_rt = cpu_reg(s, a->rt);
    tcg_rt2 = cpu_reg(s, a->rt2);

    /*
     * STGP is defined as two 8-byte memory operations, aligned to TAG_GRANULE,
     * and one tag operation.  We implement it as one single aligned 16-byte
     * memory operation for convenience.  Note that the alignment ensures
     * MO_ATOM_IFALIGN_PAIR produces 8-byte atomicity for the memory store.
     */
    mop = finalize_memop_atom(s, MO_128 | MO_ALIGN, MO_ATOM_IFALIGN_PAIR);

    tmp = tcg_temp_new_i128();
    if (s->be_data == MO_LE) {
        tcg_gen_concat_i64_i128(tmp, tcg_rt, tcg_rt2);
    } else {
        tcg_gen_concat_i64_i128(tmp, tcg_rt2, tcg_rt);
    }
    tcg_gen_qemu_st_i128(tmp, clean_addr, get_mem_index(s), mop);

    /* Perform the tag store, if tag access enabled. */
    if (s->ata[0]) {
        if (tb_cflags(s->base.tb) & CF_PARALLEL) {
            gen_helper_stg_parallel(tcg_env, dirty_addr, dirty_addr);
        } else {
            gen_helper_stg(tcg_env, dirty_addr, dirty_addr);
        }
    }

    op_addr_ldstpair_post(s, a, dirty_addr, offset);
    return true;
}

static void op_addr_ldst_imm_pre(DisasContext *s, arg_ldst_imm *a,
                                 TCGv_i64 *clean_addr, TCGv_i64 *dirty_addr,
                                 uint64_t offset, bool is_store, MemOp mop)
{
    int memidx;

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    *dirty_addr = read_cpu_reg_sp(s, a->rn, 1);
    if (!a->p) {
        tcg_gen_addi_i64(*dirty_addr, *dirty_addr, offset);
    }
    memidx = get_a64_user_mem_index(s, a->unpriv);
    *clean_addr = gen_mte_check1_mmuidx(s, *dirty_addr, is_store,
                                        a->w || a->rn != 31,
                                        mop, a->unpriv, memidx);
}

static void op_addr_ldst_imm_post(DisasContext *s, arg_ldst_imm *a,
                                  TCGv_i64 dirty_addr, uint64_t offset)
{
    if (a->w) {
        if (a->p) {
            tcg_gen_addi_i64(dirty_addr, dirty_addr, offset);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, a->rn), dirty_addr);
    }
}

static bool trans_STR_i(DisasContext *s, arg_ldst_imm *a)
{
    bool iss_sf, iss_valid = !a->w;
    TCGv_i64 clean_addr, dirty_addr, tcg_rt;
    int memidx = get_a64_user_mem_index(s, a->unpriv);
    MemOp mop = finalize_memop(s, a->sz + a->sign * MO_SIGN);

    op_addr_ldst_imm_pre(s, a, &clean_addr, &dirty_addr, a->imm, true, mop);

    tcg_rt = cpu_reg(s, a->rt);
    iss_sf = ldst_iss_sf(a->sz, a->sign, a->ext);

    do_gpr_st_memidx(s, tcg_rt, clean_addr, mop, memidx,
                     iss_valid, a->rt, iss_sf, false);
    op_addr_ldst_imm_post(s, a, dirty_addr, a->imm);
    return true;
}

static bool trans_LDR_i(DisasContext *s, arg_ldst_imm *a)
{
    bool iss_sf, iss_valid = !a->w;
    TCGv_i64 clean_addr, dirty_addr, tcg_rt;
    int memidx = get_a64_user_mem_index(s, a->unpriv);
    MemOp mop = finalize_memop(s, a->sz + a->sign * MO_SIGN);

    op_addr_ldst_imm_pre(s, a, &clean_addr, &dirty_addr, a->imm, false, mop);

    tcg_rt = cpu_reg(s, a->rt);
    iss_sf = ldst_iss_sf(a->sz, a->sign, a->ext);

    do_gpr_ld_memidx(s, tcg_rt, clean_addr, mop,
                     a->ext, memidx, iss_valid, a->rt, iss_sf, false);
    op_addr_ldst_imm_post(s, a, dirty_addr, a->imm);
    return true;
}

static bool trans_STR_v_i(DisasContext *s, arg_ldst_imm *a)
{
    TCGv_i64 clean_addr, dirty_addr;
    MemOp mop;

    if (!fp_access_check(s)) {
        return true;
    }
    mop = finalize_memop_asimd(s, a->sz);
    op_addr_ldst_imm_pre(s, a, &clean_addr, &dirty_addr, a->imm, true, mop);
    do_fp_st(s, a->rt, clean_addr, mop);
    op_addr_ldst_imm_post(s, a, dirty_addr, a->imm);
    return true;
}

static bool trans_LDR_v_i(DisasContext *s, arg_ldst_imm *a)
{
    TCGv_i64 clean_addr, dirty_addr;
    MemOp mop;

    if (!fp_access_check(s)) {
        return true;
    }
    mop = finalize_memop_asimd(s, a->sz);
    op_addr_ldst_imm_pre(s, a, &clean_addr, &dirty_addr, a->imm, false, mop);
    do_fp_ld(s, a->rt, clean_addr, mop);
    op_addr_ldst_imm_post(s, a, dirty_addr, a->imm);
    return true;
}

static void op_addr_ldst_pre(DisasContext *s, arg_ldst *a,
                             TCGv_i64 *clean_addr, TCGv_i64 *dirty_addr,
                             bool is_store, MemOp memop)
{
    TCGv_i64 tcg_rm;

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    *dirty_addr = read_cpu_reg_sp(s, a->rn, 1);

    tcg_rm = read_cpu_reg(s, a->rm, 1);
    ext_and_shift_reg(tcg_rm, tcg_rm, a->opt, a->s ? a->sz : 0);

    tcg_gen_add_i64(*dirty_addr, *dirty_addr, tcg_rm);
    *clean_addr = gen_mte_check1(s, *dirty_addr, is_store, true, memop);
}

static bool trans_LDR(DisasContext *s, arg_ldst *a)
{
    TCGv_i64 clean_addr, dirty_addr, tcg_rt;
    bool iss_sf = ldst_iss_sf(a->sz, a->sign, a->ext);
    MemOp memop;

    if (extract32(a->opt, 1, 1) == 0) {
        return false;
    }

    memop = finalize_memop(s, a->sz + a->sign * MO_SIGN);
    op_addr_ldst_pre(s, a, &clean_addr, &dirty_addr, false, memop);
    tcg_rt = cpu_reg(s, a->rt);
    do_gpr_ld(s, tcg_rt, clean_addr, memop,
              a->ext, true, a->rt, iss_sf, false);
    return true;
}

static bool trans_STR(DisasContext *s, arg_ldst *a)
{
    TCGv_i64 clean_addr, dirty_addr, tcg_rt;
    bool iss_sf = ldst_iss_sf(a->sz, a->sign, a->ext);
    MemOp memop;

    if (extract32(a->opt, 1, 1) == 0) {
        return false;
    }

    memop = finalize_memop(s, a->sz);
    op_addr_ldst_pre(s, a, &clean_addr, &dirty_addr, true, memop);
    tcg_rt = cpu_reg(s, a->rt);
    do_gpr_st(s, tcg_rt, clean_addr, memop, true, a->rt, iss_sf, false);
    return true;
}

static bool trans_LDR_v(DisasContext *s, arg_ldst *a)
{
    TCGv_i64 clean_addr, dirty_addr;
    MemOp memop;

    if (extract32(a->opt, 1, 1) == 0) {
        return false;
    }

    if (!fp_access_check(s)) {
        return true;
    }

    memop = finalize_memop_asimd(s, a->sz);
    op_addr_ldst_pre(s, a, &clean_addr, &dirty_addr, false, memop);
    do_fp_ld(s, a->rt, clean_addr, memop);
    return true;
}

static bool trans_STR_v(DisasContext *s, arg_ldst *a)
{
    TCGv_i64 clean_addr, dirty_addr;
    MemOp memop;

    if (extract32(a->opt, 1, 1) == 0) {
        return false;
    }

    if (!fp_access_check(s)) {
        return true;
    }

    memop = finalize_memop_asimd(s, a->sz);
    op_addr_ldst_pre(s, a, &clean_addr, &dirty_addr, true, memop);
    do_fp_st(s, a->rt, clean_addr, memop);
    return true;
}


static bool do_atomic_ld(DisasContext *s, arg_atomic *a, AtomicThreeOpFn *fn,
                         int sign, bool invert)
{
    MemOp mop = a->sz | sign;
    TCGv_i64 clean_addr, tcg_rs, tcg_rt;

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    mop = check_atomic_align(s, a->rn, mop);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, a->rn), false,
                                a->rn != 31, mop);
    tcg_rs = read_cpu_reg(s, a->rs, true);
    tcg_rt = cpu_reg(s, a->rt);
    if (invert) {
        tcg_gen_not_i64(tcg_rs, tcg_rs);
    }
    /*
     * The tcg atomic primitives are all full barriers.  Therefore we
     * can ignore the Acquire and Release bits of this instruction.
     */
    fn(tcg_rt, clean_addr, tcg_rs, get_mem_index(s), mop);

    if (mop & MO_SIGN) {
        switch (a->sz) {
        case MO_8:
            tcg_gen_ext8u_i64(tcg_rt, tcg_rt);
            break;
        case MO_16:
            tcg_gen_ext16u_i64(tcg_rt, tcg_rt);
            break;
        case MO_32:
            tcg_gen_ext32u_i64(tcg_rt, tcg_rt);
            break;
        case MO_64:
            break;
        default:
            g_assert_not_reached();
        }
    }
    return true;
}

TRANS_FEAT(LDADD, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_add_i64, 0, false)
TRANS_FEAT(LDCLR, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_and_i64, 0, true)
TRANS_FEAT(LDEOR, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_xor_i64, 0, false)
TRANS_FEAT(LDSET, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_or_i64, 0, false)
TRANS_FEAT(LDSMAX, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_smax_i64, MO_SIGN, false)
TRANS_FEAT(LDSMIN, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_smin_i64, MO_SIGN, false)
TRANS_FEAT(LDUMAX, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_umax_i64, 0, false)
TRANS_FEAT(LDUMIN, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_fetch_umin_i64, 0, false)
TRANS_FEAT(SWP, aa64_atomics, do_atomic_ld, a, tcg_gen_atomic_xchg_i64, 0, false)

static bool trans_LDAPR(DisasContext *s, arg_LDAPR *a)
{
    bool iss_sf = ldst_iss_sf(a->sz, false, false);
    TCGv_i64 clean_addr;
    MemOp mop;

    if (!dc_isar_feature(aa64_atomics, s) ||
        !dc_isar_feature(aa64_rcpc_8_3, s)) {
        return false;
    }
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    mop = check_ordered_align(s, a->rn, 0, false, a->sz);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, a->rn), false,
                                a->rn != 31, mop);
    /*
     * LDAPR* are a special case because they are a simple load, not a
     * fetch-and-do-something op.
     * The architectural consistency requirements here are weaker than
     * full load-acquire (we only need "load-acquire processor consistent"),
     * but we choose to implement them as full LDAQ.
     */
    do_gpr_ld(s, cpu_reg(s, a->rt), clean_addr, mop, false,
              true, a->rt, iss_sf, true);
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    return true;
}

static bool trans_LDRA(DisasContext *s, arg_LDRA *a)
{
    TCGv_i64 clean_addr, dirty_addr, tcg_rt;
    MemOp memop;

    /* Load with pointer authentication */
    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    dirty_addr = read_cpu_reg_sp(s, a->rn, 1);

    if (s->pauth_active) {
        if (!a->m) {
            gen_helper_autda_combined(dirty_addr, tcg_env, dirty_addr,
                                      tcg_constant_i64(0));
        } else {
            gen_helper_autdb_combined(dirty_addr, tcg_env, dirty_addr,
                                      tcg_constant_i64(0));
        }
    }

    tcg_gen_addi_i64(dirty_addr, dirty_addr, a->imm);

    memop = finalize_memop(s, MO_64);

    /* Note that "clean" and "dirty" here refer to TBI not PAC.  */
    clean_addr = gen_mte_check1(s, dirty_addr, false,
                                a->w || a->rn != 31, memop);

    tcg_rt = cpu_reg(s, a->rt);
    do_gpr_ld(s, tcg_rt, clean_addr, memop,
              /* extend */ false, /* iss_valid */ !a->w,
              /* iss_srt */ a->rt, /* iss_sf */ true, /* iss_ar */ false);

    if (a->w) {
        tcg_gen_mov_i64(cpu_reg_sp(s, a->rn), dirty_addr);
    }
    return true;
}

static bool trans_LDAPR_i(DisasContext *s, arg_ldapr_stlr_i *a)
{
    TCGv_i64 clean_addr, dirty_addr;
    MemOp mop = a->sz | (a->sign ? MO_SIGN : 0);
    bool iss_sf = ldst_iss_sf(a->sz, a->sign, a->ext);

    if (!dc_isar_feature(aa64_rcpc_8_4, s)) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    mop = check_ordered_align(s, a->rn, a->imm, false, mop);
    dirty_addr = read_cpu_reg_sp(s, a->rn, 1);
    tcg_gen_addi_i64(dirty_addr, dirty_addr, a->imm);
    clean_addr = clean_data_tbi(s, dirty_addr);

    /*
     * Load-AcquirePC semantics; we implement as the slightly more
     * restrictive Load-Acquire.
     */
    do_gpr_ld(s, cpu_reg(s, a->rt), clean_addr, mop, a->ext, true,
              a->rt, iss_sf, true);
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_LDAQ);
    return true;
}

static bool trans_STLR_i(DisasContext *s, arg_ldapr_stlr_i *a)
{
    TCGv_i64 clean_addr, dirty_addr;
    MemOp mop = a->sz;
    bool iss_sf = ldst_iss_sf(a->sz, a->sign, a->ext);

    if (!dc_isar_feature(aa64_rcpc_8_4, s)) {
        return false;
    }

    /* TODO: ARMv8.4-LSE SCTLR.nAA */

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    mop = check_ordered_align(s, a->rn, a->imm, true, mop);
    dirty_addr = read_cpu_reg_sp(s, a->rn, 1);
    tcg_gen_addi_i64(dirty_addr, dirty_addr, a->imm);
    clean_addr = clean_data_tbi(s, dirty_addr);

    /* Store-Release semantics */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_STRL);
    do_gpr_st(s, cpu_reg(s, a->rt), clean_addr, mop, true, a->rt, iss_sf, true);
    return true;
}

static bool trans_LD_mult(DisasContext *s, arg_ldst_mult *a)
{
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;
    MemOp endian, align, mop;

    int total;    /* total bytes */
    int elements; /* elements per vector */
    int r;
    int size = a->sz;

    if (!a->p && a->rm != 0) {
        /* For non-postindexed accesses the Rm field must be 0 */
        return false;
    }
    if (size == 3 && !a->q && a->selem != 1) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    /* For our purposes, bytes are always little-endian.  */
    endian = s->be_data;
    if (size == 0) {
        endian = MO_LE;
    }

    total = a->rpt * a->selem * (a->q ? 16 : 8);
    tcg_rn = cpu_reg_sp(s, a->rn);

    /*
     * Issue the MTE check vs the logical repeat count, before we
     * promote consecutive little-endian elements below.
     */
    clean_addr = gen_mte_checkN(s, tcg_rn, false, a->p || a->rn != 31, total,
                                finalize_memop_asimd(s, size));

    /*
     * Consecutive little-endian elements from a single register
     * can be promoted to a larger little-endian operation.
     */
    align = MO_ALIGN;
    if (a->selem == 1 && endian == MO_LE) {
        align = pow2_align(size);
        size = 3;
    }
    if (!s->align_mem) {
        align = 0;
    }
    mop = endian | size | align;

    elements = (a->q ? 16 : 8) >> size;
    tcg_ebytes = tcg_constant_i64(1 << size);
    for (r = 0; r < a->rpt; r++) {
        int e;
        for (e = 0; e < elements; e++) {
            int xs;
            for (xs = 0; xs < a->selem; xs++) {
                int tt = (a->rt + r + xs) % 32;
                do_vec_ld(s, tt, e, clean_addr, mop);
                tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
            }
        }
    }

    /*
     * For non-quad operations, setting a slice of the low 64 bits of
     * the register clears the high 64 bits (in the ARM ARM pseudocode
     * this is implicit in the fact that 'rval' is a 64 bit wide
     * variable).  For quad operations, we might still need to zero
     * the high bits of SVE.
     */
    for (r = 0; r < a->rpt * a->selem; r++) {
        int tt = (a->rt + r) % 32;
        clear_vec_high(s, a->q, tt);
    }

    if (a->p) {
        if (a->rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, a->rm));
        }
    }
    return true;
}

static bool trans_ST_mult(DisasContext *s, arg_ldst_mult *a)
{
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;
    MemOp endian, align, mop;

    int total;    /* total bytes */
    int elements; /* elements per vector */
    int r;
    int size = a->sz;

    if (!a->p && a->rm != 0) {
        /* For non-postindexed accesses the Rm field must be 0 */
        return false;
    }
    if (size == 3 && !a->q && a->selem != 1) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    /* For our purposes, bytes are always little-endian.  */
    endian = s->be_data;
    if (size == 0) {
        endian = MO_LE;
    }

    total = a->rpt * a->selem * (a->q ? 16 : 8);
    tcg_rn = cpu_reg_sp(s, a->rn);

    /*
     * Issue the MTE check vs the logical repeat count, before we
     * promote consecutive little-endian elements below.
     */
    clean_addr = gen_mte_checkN(s, tcg_rn, true, a->p || a->rn != 31, total,
                                finalize_memop_asimd(s, size));

    /*
     * Consecutive little-endian elements from a single register
     * can be promoted to a larger little-endian operation.
     */
    align = MO_ALIGN;
    if (a->selem == 1 && endian == MO_LE) {
        align = pow2_align(size);
        size = 3;
    }
    if (!s->align_mem) {
        align = 0;
    }
    mop = endian | size | align;

    elements = (a->q ? 16 : 8) >> size;
    tcg_ebytes = tcg_constant_i64(1 << size);
    for (r = 0; r < a->rpt; r++) {
        int e;
        for (e = 0; e < elements; e++) {
            int xs;
            for (xs = 0; xs < a->selem; xs++) {
                int tt = (a->rt + r + xs) % 32;
                do_vec_st(s, tt, e, clean_addr, mop);
                tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
            }
        }
    }

    if (a->p) {
        if (a->rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, a->rm));
        }
    }
    return true;
}

static bool trans_ST_single(DisasContext *s, arg_ldst_single *a)
{
    int xs, total, rt;
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;
    MemOp mop;

    if (!a->p && a->rm != 0) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    total = a->selem << a->scale;
    tcg_rn = cpu_reg_sp(s, a->rn);

    mop = finalize_memop_asimd(s, a->scale);
    clean_addr = gen_mte_checkN(s, tcg_rn, true, a->p || a->rn != 31,
                                total, mop);

    tcg_ebytes = tcg_constant_i64(1 << a->scale);
    for (xs = 0, rt = a->rt; xs < a->selem; xs++, rt = (rt + 1) % 32) {
        do_vec_st(s, rt, a->index, clean_addr, mop);
        tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
    }

    if (a->p) {
        if (a->rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, a->rm));
        }
    }
    return true;
}

static bool trans_LD_single(DisasContext *s, arg_ldst_single *a)
{
    int xs, total, rt;
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;
    MemOp mop;

    if (!a->p && a->rm != 0) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    total = a->selem << a->scale;
    tcg_rn = cpu_reg_sp(s, a->rn);

    mop = finalize_memop_asimd(s, a->scale);
    clean_addr = gen_mte_checkN(s, tcg_rn, false, a->p || a->rn != 31,
                                total, mop);

    tcg_ebytes = tcg_constant_i64(1 << a->scale);
    for (xs = 0, rt = a->rt; xs < a->selem; xs++, rt = (rt + 1) % 32) {
        do_vec_ld(s, rt, a->index, clean_addr, mop);
        tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
    }

    if (a->p) {
        if (a->rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, a->rm));
        }
    }
    return true;
}

static bool trans_LD_single_repl(DisasContext *s, arg_LD_single_repl *a)
{
    int xs, total, rt;
    TCGv_i64 clean_addr, tcg_rn, tcg_ebytes;
    MemOp mop;

    if (!a->p && a->rm != 0) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    total = a->selem << a->scale;
    tcg_rn = cpu_reg_sp(s, a->rn);

    mop = finalize_memop_asimd(s, a->scale);
    clean_addr = gen_mte_checkN(s, tcg_rn, false, a->p || a->rn != 31,
                                total, mop);

    tcg_ebytes = tcg_constant_i64(1 << a->scale);
    for (xs = 0, rt = a->rt; xs < a->selem; xs++, rt = (rt + 1) % 32) {
        /* Load and replicate to all elements */
        TCGv_i64 tcg_tmp = tcg_temp_new_i64();

        tcg_gen_qemu_ld_i64(tcg_tmp, clean_addr, get_mem_index(s), mop);
        tcg_gen_gvec_dup_i64(a->scale, vec_full_reg_offset(s, rt),
                             (a->q + 1) * 8, vec_full_reg_size(s), tcg_tmp);
        tcg_gen_add_i64(clean_addr, clean_addr, tcg_ebytes);
    }

    if (a->p) {
        if (a->rm == 31) {
            tcg_gen_addi_i64(tcg_rn, tcg_rn, total);
        } else {
            tcg_gen_add_i64(tcg_rn, tcg_rn, cpu_reg(s, a->rm));
        }
    }
    return true;
}

static bool trans_STZGM(DisasContext *s, arg_ldst_tag *a)
{
    TCGv_i64 addr, clean_addr, tcg_rt;
    int size = 4 << s->dcz_blocksize;

    if (!dc_isar_feature(aa64_mte, s)) {
        return false;
    }
    if (s->current_el == 0) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    addr = read_cpu_reg_sp(s, a->rn, true);
    tcg_gen_addi_i64(addr, addr, a->imm);
    tcg_rt = cpu_reg(s, a->rt);

    if (s->ata[0]) {
        gen_helper_stzgm_tags(tcg_env, addr, tcg_rt);
    }
    /*
     * The non-tags portion of STZGM is mostly like DC_ZVA,
     * except the alignment happens before the access.
     */
    clean_addr = clean_data_tbi(s, addr);
    tcg_gen_andi_i64(clean_addr, clean_addr, -size);
    gen_helper_dc_zva(tcg_env, clean_addr);
    return true;
}

static bool trans_STGM(DisasContext *s, arg_ldst_tag *a)
{
    TCGv_i64 addr, clean_addr, tcg_rt;

    if (!dc_isar_feature(aa64_mte, s)) {
        return false;
    }
    if (s->current_el == 0) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    addr = read_cpu_reg_sp(s, a->rn, true);
    tcg_gen_addi_i64(addr, addr, a->imm);
    tcg_rt = cpu_reg(s, a->rt);

    if (s->ata[0]) {
        gen_helper_stgm(tcg_env, addr, tcg_rt);
    } else {
        MMUAccessType acc = MMU_DATA_STORE;
        int size = 4 << s->gm_blocksize;

        clean_addr = clean_data_tbi(s, addr);
        tcg_gen_andi_i64(clean_addr, clean_addr, -size);
        gen_probe_access(s, clean_addr, acc, size);
    }
    return true;
}

static bool trans_LDGM(DisasContext *s, arg_ldst_tag *a)
{
    TCGv_i64 addr, clean_addr, tcg_rt;

    if (!dc_isar_feature(aa64_mte, s)) {
        return false;
    }
    if (s->current_el == 0) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    addr = read_cpu_reg_sp(s, a->rn, true);
    tcg_gen_addi_i64(addr, addr, a->imm);
    tcg_rt = cpu_reg(s, a->rt);

    if (s->ata[0]) {
        gen_helper_ldgm(tcg_rt, tcg_env, addr);
    } else {
        MMUAccessType acc = MMU_DATA_LOAD;
        int size = 4 << s->gm_blocksize;

        clean_addr = clean_data_tbi(s, addr);
        tcg_gen_andi_i64(clean_addr, clean_addr, -size);
        gen_probe_access(s, clean_addr, acc, size);
        /* The result tags are zeros.  */
        tcg_gen_movi_i64(tcg_rt, 0);
    }
    return true;
}

static bool trans_LDG(DisasContext *s, arg_ldst_tag *a)
{
    TCGv_i64 addr, clean_addr, tcg_rt;

    if (!dc_isar_feature(aa64_mte_insn_reg, s)) {
        return false;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    addr = read_cpu_reg_sp(s, a->rn, true);
    if (!a->p) {
        /* pre-index or signed offset */
        tcg_gen_addi_i64(addr, addr, a->imm);
    }

    tcg_gen_andi_i64(addr, addr, -TAG_GRANULE);
    tcg_rt = cpu_reg(s, a->rt);
    if (s->ata[0]) {
        gen_helper_ldg(tcg_rt, tcg_env, addr, tcg_rt);
    } else {
        /*
         * Tag access disabled: we must check for aborts on the load
         * load from [rn+offset], and then insert a 0 tag into rt.
         */
        clean_addr = clean_data_tbi(s, addr);
        gen_probe_access(s, clean_addr, MMU_DATA_LOAD, MO_8);
        gen_address_with_allocation_tag0(tcg_rt, tcg_rt);
    }

    if (a->w) {
        /* pre-index or post-index */
        if (a->p) {
            /* post-index */
            tcg_gen_addi_i64(addr, addr, a->imm);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, a->rn), addr);
    }
    return true;
}

static bool do_STG(DisasContext *s, arg_ldst_tag *a, bool is_zero, bool is_pair)
{
    TCGv_i64 addr, tcg_rt;

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }

    addr = read_cpu_reg_sp(s, a->rn, true);
    if (!a->p) {
        /* pre-index or signed offset */
        tcg_gen_addi_i64(addr, addr, a->imm);
    }
    tcg_rt = cpu_reg_sp(s, a->rt);
    if (!s->ata[0]) {
        /*
         * For STG and ST2G, we need to check alignment and probe memory.
         * TODO: For STZG and STZ2G, we could rely on the stores below,
         * at least for system mode; user-only won't enforce alignment.
         */
        if (is_pair) {
            gen_helper_st2g_stub(tcg_env, addr);
        } else {
            gen_helper_stg_stub(tcg_env, addr);
        }
    } else if (tb_cflags(s->base.tb) & CF_PARALLEL) {
        if (is_pair) {
            gen_helper_st2g_parallel(tcg_env, addr, tcg_rt);
        } else {
            gen_helper_stg_parallel(tcg_env, addr, tcg_rt);
        }
    } else {
        if (is_pair) {
            gen_helper_st2g(tcg_env, addr, tcg_rt);
        } else {
            gen_helper_stg(tcg_env, addr, tcg_rt);
        }
    }

    if (is_zero) {
        TCGv_i64 clean_addr = clean_data_tbi(s, addr);
        TCGv_i64 zero64 = tcg_constant_i64(0);
        TCGv_i128 zero128 = tcg_temp_new_i128();
        int mem_index = get_mem_index(s);
        MemOp mop = finalize_memop(s, MO_128 | MO_ALIGN);

        tcg_gen_concat_i64_i128(zero128, zero64, zero64);

        /* This is 1 or 2 atomic 16-byte operations. */
        tcg_gen_qemu_st_i128(zero128, clean_addr, mem_index, mop);
        if (is_pair) {
            tcg_gen_addi_i64(clean_addr, clean_addr, 16);
            tcg_gen_qemu_st_i128(zero128, clean_addr, mem_index, mop);
        }
    }

    if (a->w) {
        /* pre-index or post-index */
        if (a->p) {
            /* post-index */
            tcg_gen_addi_i64(addr, addr, a->imm);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, a->rn), addr);
    }
    return true;
}

TRANS_FEAT(STG, aa64_mte_insn_reg, do_STG, a, false, false)
TRANS_FEAT(STZG, aa64_mte_insn_reg, do_STG, a, true, false)
TRANS_FEAT(ST2G, aa64_mte_insn_reg, do_STG, a, false, true)
TRANS_FEAT(STZ2G, aa64_mte_insn_reg, do_STG, a, true, true)

typedef void SetFn(TCGv_env, TCGv_i32, TCGv_i32);

static bool do_SET(DisasContext *s, arg_set *a, bool is_epilogue,
                   bool is_setg, SetFn fn)
{
    int memidx;
    uint32_t syndrome, desc = 0;

    if (is_setg && !dc_isar_feature(aa64_mte, s)) {
        return false;
    }

    /*
     * UNPREDICTABLE cases: we choose to UNDEF, which allows
     * us to pull this check before the CheckMOPSEnabled() test
     * (which we do in the helper function)
     */
    if (a->rs == a->rn || a->rs == a->rd || a->rn == a->rd ||
        a->rd == 31 || a->rn == 31) {
        return false;
    }

    memidx = get_a64_user_mem_index(s, a->unpriv);

    /*
     * We pass option_a == true, matching our implementation;
     * we pass wrong_option == false: helper function may set that bit.
     */
    syndrome = syn_mop(true, is_setg, (a->nontemp << 1) | a->unpriv,
                       is_epilogue, false, true, a->rd, a->rs, a->rn);

    if (is_setg ? s->ata[a->unpriv] : s->mte_active[a->unpriv]) {
        /* We may need to do MTE tag checking, so assemble the descriptor */
        desc = FIELD_DP32(desc, MTEDESC, TBI, s->tbid);
        desc = FIELD_DP32(desc, MTEDESC, TCMA, s->tcma);
        desc = FIELD_DP32(desc, MTEDESC, WRITE, true);
        /* SIZEM1 and ALIGN we leave 0 (byte write) */
    }
    /* The helper function always needs the memidx even with MTE disabled */
    desc = FIELD_DP32(desc, MTEDESC, MIDX, memidx);

    /*
     * The helper needs the register numbers, but since they're in
     * the syndrome anyway, we let it extract them from there rather
     * than passing in an extra three integer arguments.
     */
    fn(tcg_env, tcg_constant_i32(syndrome), tcg_constant_i32(desc));
    return true;
}

TRANS_FEAT(SETP, aa64_mops, do_SET, a, false, false, gen_helper_setp)
TRANS_FEAT(SETM, aa64_mops, do_SET, a, false, false, gen_helper_setm)
TRANS_FEAT(SETE, aa64_mops, do_SET, a, true, false, gen_helper_sete)
TRANS_FEAT(SETGP, aa64_mops, do_SET, a, false, true, gen_helper_setgp)
TRANS_FEAT(SETGM, aa64_mops, do_SET, a, false, true, gen_helper_setgm)
TRANS_FEAT(SETGE, aa64_mops, do_SET, a, true, true, gen_helper_setge)

typedef void CpyFn(TCGv_env, TCGv_i32, TCGv_i32, TCGv_i32);

static bool do_CPY(DisasContext *s, arg_cpy *a, bool is_epilogue, CpyFn fn)
{
    int rmemidx, wmemidx;
    uint32_t syndrome, rdesc = 0, wdesc = 0;
    bool wunpriv = extract32(a->options, 0, 1);
    bool runpriv = extract32(a->options, 1, 1);

    /*
     * UNPREDICTABLE cases: we choose to UNDEF, which allows
     * us to pull this check before the CheckMOPSEnabled() test
     * (which we do in the helper function)
     */
    if (a->rs == a->rn || a->rs == a->rd || a->rn == a->rd ||
        a->rd == 31 || a->rs == 31 || a->rn == 31) {
        return false;
    }

    rmemidx = get_a64_user_mem_index(s, runpriv);
    wmemidx = get_a64_user_mem_index(s, wunpriv);

    /*
     * We pass option_a == true, matching our implementation;
     * we pass wrong_option == false: helper function may set that bit.
     */
    syndrome = syn_mop(false, false, a->options, is_epilogue,
                       false, true, a->rd, a->rs, a->rn);

    /* If we need to do MTE tag checking, assemble the descriptors */
    if (s->mte_active[runpriv]) {
        rdesc = FIELD_DP32(rdesc, MTEDESC, TBI, s->tbid);
        rdesc = FIELD_DP32(rdesc, MTEDESC, TCMA, s->tcma);
    }
    if (s->mte_active[wunpriv]) {
        wdesc = FIELD_DP32(wdesc, MTEDESC, TBI, s->tbid);
        wdesc = FIELD_DP32(wdesc, MTEDESC, TCMA, s->tcma);
        wdesc = FIELD_DP32(wdesc, MTEDESC, WRITE, true);
    }
    /* The helper function needs these parts of the descriptor regardless */
    rdesc = FIELD_DP32(rdesc, MTEDESC, MIDX, rmemidx);
    wdesc = FIELD_DP32(wdesc, MTEDESC, MIDX, wmemidx);

    /*
     * The helper needs the register numbers, but since they're in
     * the syndrome anyway, we let it extract them from there rather
     * than passing in an extra three integer arguments.
     */
    fn(tcg_env, tcg_constant_i32(syndrome), tcg_constant_i32(wdesc),
       tcg_constant_i32(rdesc));
    return true;
}

TRANS_FEAT(CPYP, aa64_mops, do_CPY, a, false, gen_helper_cpyp)
TRANS_FEAT(CPYM, aa64_mops, do_CPY, a, false, gen_helper_cpym)
TRANS_FEAT(CPYE, aa64_mops, do_CPY, a, true, gen_helper_cpye)
TRANS_FEAT(CPYFP, aa64_mops, do_CPY, a, false, gen_helper_cpyfp)
TRANS_FEAT(CPYFM, aa64_mops, do_CPY, a, false, gen_helper_cpyfm)
TRANS_FEAT(CPYFE, aa64_mops, do_CPY, a, true, gen_helper_cpyfe)

typedef void ArithTwoOp(TCGv_i64, TCGv_i64, TCGv_i64);

static bool gen_rri(DisasContext *s, arg_rri_sf *a,
                    bool rd_sp, bool rn_sp, ArithTwoOp *fn)
{
    TCGv_i64 tcg_rn = rn_sp ? cpu_reg_sp(s, a->rn) : cpu_reg(s, a->rn);
    TCGv_i64 tcg_rd = rd_sp ? cpu_reg_sp(s, a->rd) : cpu_reg(s, a->rd);
    TCGv_i64 tcg_imm = tcg_constant_i64(a->imm);

    fn(tcg_rd, tcg_rn, tcg_imm);
    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

/*
 * PC-rel. addressing
 */

static bool trans_ADR(DisasContext *s, arg_ri *a)
{
    gen_pc_plus_diff(s, cpu_reg(s, a->rd), a->imm);
    return true;
}

static bool trans_ADRP(DisasContext *s, arg_ri *a)
{
    int64_t offset = (int64_t)a->imm << 12;

    /* The page offset is ok for CF_PCREL. */
    offset -= s->pc_curr & 0xfff;
    gen_pc_plus_diff(s, cpu_reg(s, a->rd), offset);
    return true;
}

/*
 * Add/subtract (immediate)
 */
TRANS(ADD_i, gen_rri, a, 1, 1, tcg_gen_add_i64)
TRANS(SUB_i, gen_rri, a, 1, 1, tcg_gen_sub_i64)
TRANS(ADDS_i, gen_rri, a, 0, 1, a->sf ? gen_add64_CC : gen_add32_CC)
TRANS(SUBS_i, gen_rri, a, 0, 1, a->sf ? gen_sub64_CC : gen_sub32_CC)

/*
 * Add/subtract (immediate, with tags)
 */

static bool gen_add_sub_imm_with_tags(DisasContext *s, arg_rri_tag *a,
                                      bool sub_op)
{
    TCGv_i64 tcg_rn, tcg_rd;
    int imm;

    imm = a->uimm6 << LOG2_TAG_GRANULE;
    if (sub_op) {
        imm = -imm;
    }

    tcg_rn = cpu_reg_sp(s, a->rn);
    tcg_rd = cpu_reg_sp(s, a->rd);

    if (s->ata[0]) {
        gen_helper_addsubg(tcg_rd, tcg_env, tcg_rn,
                           tcg_constant_i32(imm),
                           tcg_constant_i32(a->uimm4));
    } else {
        tcg_gen_addi_i64(tcg_rd, tcg_rn, imm);
        gen_address_with_allocation_tag0(tcg_rd, tcg_rd);
    }
    return true;
}

TRANS_FEAT(ADDG_i, aa64_mte_insn_reg, gen_add_sub_imm_with_tags, a, false)
TRANS_FEAT(SUBG_i, aa64_mte_insn_reg, gen_add_sub_imm_with_tags, a, true)

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

/*
 * Logical (immediate)
 */

/*
 * Simplified variant of pseudocode DecodeBitMasks() for the case where we
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
    mask = MAKE_64BIT_MASK(0, s + 1);
    if (r) {
        mask = (mask >> r) | (mask << (e - r));
        mask &= MAKE_64BIT_MASK(0, e);
    }
    /* ...then replicate the element over the whole 64 bit value */
    mask = bitfield_replicate(mask, e);
    *result = mask;
    return true;
}

static bool gen_rri_log(DisasContext *s, arg_rri_log *a, bool set_cc,
                        void (*fn)(TCGv_i64, TCGv_i64, int64_t))
{
    TCGv_i64 tcg_rd, tcg_rn;
    uint64_t imm;

    /* Some immediate field values are reserved. */
    if (!logic_imm_decode_wmask(&imm, extract32(a->dbm, 12, 1),
                                extract32(a->dbm, 0, 6),
                                extract32(a->dbm, 6, 6))) {
        return false;
    }
    if (!a->sf) {
        imm &= 0xffffffffull;
    }

    tcg_rd = set_cc ? cpu_reg(s, a->rd) : cpu_reg_sp(s, a->rd);
    tcg_rn = cpu_reg(s, a->rn);

    fn(tcg_rd, tcg_rn, imm);
    if (set_cc) {
        gen_logic_CC(a->sf, tcg_rd);
    }
    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

TRANS(AND_i, gen_rri_log, a, false, tcg_gen_andi_i64)
TRANS(ORR_i, gen_rri_log, a, false, tcg_gen_ori_i64)
TRANS(EOR_i, gen_rri_log, a, false, tcg_gen_xori_i64)
TRANS(ANDS_i, gen_rri_log, a, true, tcg_gen_andi_i64)

/*
 * Move wide (immediate)
 */

static bool trans_MOVZ(DisasContext *s, arg_movw *a)
{
    int pos = a->hw << 4;
    tcg_gen_movi_i64(cpu_reg(s, a->rd), (uint64_t)a->imm << pos);
    return true;
}

static bool trans_MOVN(DisasContext *s, arg_movw *a)
{
    int pos = a->hw << 4;
    uint64_t imm = a->imm;

    imm = ~(imm << pos);
    if (!a->sf) {
        imm = (uint32_t)imm;
    }
    tcg_gen_movi_i64(cpu_reg(s, a->rd), imm);
    return true;
}

static bool trans_MOVK(DisasContext *s, arg_movw *a)
{
    int pos = a->hw << 4;
    TCGv_i64 tcg_rd, tcg_im;

    tcg_rd = cpu_reg(s, a->rd);
    tcg_im = tcg_constant_i64(a->imm);
    tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_im, pos, 16);
    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

/*
 * Bitfield
 */

static bool trans_SBFM(DisasContext *s, arg_SBFM *a)
{
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 tcg_tmp = read_cpu_reg(s, a->rn, 1);
    unsigned int bitsize = a->sf ? 64 : 32;
    unsigned int ri = a->immr;
    unsigned int si = a->imms;
    unsigned int pos, len;

    if (si >= ri) {
        /* Wd<s-r:0> = Wn<s:r> */
        len = (si - ri) + 1;
        tcg_gen_sextract_i64(tcg_rd, tcg_tmp, ri, len);
        if (!a->sf) {
            tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
        }
    } else {
        /* Wd<32+s-r,32-r> = Wn<s:0> */
        len = si + 1;
        pos = (bitsize - ri) & (bitsize - 1);

        if (len < ri) {
            /*
             * Sign extend the destination field from len to fill the
             * balance of the word.  Let the deposit below insert all
             * of those sign bits.
             */
            tcg_gen_sextract_i64(tcg_tmp, tcg_tmp, 0, len);
            len = ri;
        }

        /*
         * We start with zero, and we haven't modified any bits outside
         * bitsize, therefore no final zero-extension is unneeded for !sf.
         */
        tcg_gen_deposit_z_i64(tcg_rd, tcg_tmp, pos, len);
    }
    return true;
}

static bool trans_UBFM(DisasContext *s, arg_UBFM *a)
{
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 tcg_tmp = read_cpu_reg(s, a->rn, 1);
    unsigned int bitsize = a->sf ? 64 : 32;
    unsigned int ri = a->immr;
    unsigned int si = a->imms;
    unsigned int pos, len;

    tcg_rd = cpu_reg(s, a->rd);
    tcg_tmp = read_cpu_reg(s, a->rn, 1);

    if (si >= ri) {
        /* Wd<s-r:0> = Wn<s:r> */
        len = (si - ri) + 1;
        tcg_gen_extract_i64(tcg_rd, tcg_tmp, ri, len);
    } else {
        /* Wd<32+s-r,32-r> = Wn<s:0> */
        len = si + 1;
        pos = (bitsize - ri) & (bitsize - 1);
        tcg_gen_deposit_z_i64(tcg_rd, tcg_tmp, pos, len);
    }
    return true;
}

static bool trans_BFM(DisasContext *s, arg_BFM *a)
{
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 tcg_tmp = read_cpu_reg(s, a->rn, 1);
    unsigned int bitsize = a->sf ? 64 : 32;
    unsigned int ri = a->immr;
    unsigned int si = a->imms;
    unsigned int pos, len;

    tcg_rd = cpu_reg(s, a->rd);
    tcg_tmp = read_cpu_reg(s, a->rn, 1);

    if (si >= ri) {
        /* Wd<s-r:0> = Wn<s:r> */
        tcg_gen_shri_i64(tcg_tmp, tcg_tmp, ri);
        len = (si - ri) + 1;
        pos = 0;
    } else {
        /* Wd<32+s-r,32-r> = Wn<s:0> */
        len = si + 1;
        pos = (bitsize - ri) & (bitsize - 1);
    }

    tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_tmp, pos, len);
    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

static bool trans_EXTR(DisasContext *s, arg_extract *a)
{
    TCGv_i64 tcg_rd, tcg_rm, tcg_rn;

    tcg_rd = cpu_reg(s, a->rd);

    if (unlikely(a->imm == 0)) {
        /*
         * tcg shl_i32/shl_i64 is undefined for 32/64 bit shifts,
         * so an extract from bit 0 is a special case.
         */
        if (a->sf) {
            tcg_gen_mov_i64(tcg_rd, cpu_reg(s, a->rm));
        } else {
            tcg_gen_ext32u_i64(tcg_rd, cpu_reg(s, a->rm));
        }
    } else {
        tcg_rm = cpu_reg(s, a->rm);
        tcg_rn = cpu_reg(s, a->rn);

        if (a->sf) {
            /* Specialization to ROR happens in EXTRACT2.  */
            tcg_gen_extract2_i64(tcg_rd, tcg_rm, tcg_rn, a->imm);
        } else {
            TCGv_i32 t0 = tcg_temp_new_i32();

            tcg_gen_extrl_i64_i32(t0, tcg_rm);
            if (a->rm == a->rn) {
                tcg_gen_rotri_i32(t0, t0, a->imm);
            } else {
                TCGv_i32 t1 = tcg_temp_new_i32();
                tcg_gen_extrl_i64_i32(t1, tcg_rn);
                tcg_gen_extract2_i32(t0, t0, t1, a->imm);
            }
            tcg_gen_extu_i32_i64(tcg_rd, t0);
        }
    }
    return true;
}

/*
 * Cryptographic AES, SHA, SHA512
 */

TRANS_FEAT(AESE, aa64_aes, do_gvec_op3_ool, a, 0, gen_helper_crypto_aese)
TRANS_FEAT(AESD, aa64_aes, do_gvec_op3_ool, a, 0, gen_helper_crypto_aesd)
TRANS_FEAT(AESMC, aa64_aes, do_gvec_op2_ool, a, 0, gen_helper_crypto_aesmc)
TRANS_FEAT(AESIMC, aa64_aes, do_gvec_op2_ool, a, 0, gen_helper_crypto_aesimc)

TRANS_FEAT(SHA1C, aa64_sha1, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha1c)
TRANS_FEAT(SHA1P, aa64_sha1, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha1p)
TRANS_FEAT(SHA1M, aa64_sha1, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha1m)
TRANS_FEAT(SHA1SU0, aa64_sha1, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha1su0)

TRANS_FEAT(SHA256H, aa64_sha256, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha256h)
TRANS_FEAT(SHA256H2, aa64_sha256, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha256h2)
TRANS_FEAT(SHA256SU1, aa64_sha256, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha256su1)

TRANS_FEAT(SHA1H, aa64_sha1, do_gvec_op2_ool, a, 0, gen_helper_crypto_sha1h)
TRANS_FEAT(SHA1SU1, aa64_sha1, do_gvec_op2_ool, a, 0, gen_helper_crypto_sha1su1)
TRANS_FEAT(SHA256SU0, aa64_sha256, do_gvec_op2_ool, a, 0, gen_helper_crypto_sha256su0)

TRANS_FEAT(SHA512H, aa64_sha512, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha512h)
TRANS_FEAT(SHA512H2, aa64_sha512, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha512h2)
TRANS_FEAT(SHA512SU1, aa64_sha512, do_gvec_op3_ool, a, 0, gen_helper_crypto_sha512su1)
TRANS_FEAT(RAX1, aa64_sha3, do_gvec_fn3, a, gen_gvec_rax1)
TRANS_FEAT(SM3PARTW1, aa64_sm3, do_gvec_op3_ool, a, 0, gen_helper_crypto_sm3partw1)
TRANS_FEAT(SM3PARTW2, aa64_sm3, do_gvec_op3_ool, a, 0, gen_helper_crypto_sm3partw2)
TRANS_FEAT(SM4EKEY, aa64_sm4, do_gvec_op3_ool, a, 0, gen_helper_crypto_sm4ekey)

TRANS_FEAT(SHA512SU0, aa64_sha512, do_gvec_op2_ool, a, 0, gen_helper_crypto_sha512su0)
TRANS_FEAT(SM4E, aa64_sm4, do_gvec_op3_ool, a, 0, gen_helper_crypto_sm4e)

TRANS_FEAT(EOR3, aa64_sha3, do_gvec_fn4, a, gen_gvec_eor3)
TRANS_FEAT(BCAX, aa64_sha3, do_gvec_fn4, a, gen_gvec_bcax)

static bool trans_SM3SS1(DisasContext *s, arg_SM3SS1 *a)
{
    if (!dc_isar_feature(aa64_sm3, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i32 tcg_op1 = tcg_temp_new_i32();
        TCGv_i32 tcg_op2 = tcg_temp_new_i32();
        TCGv_i32 tcg_op3 = tcg_temp_new_i32();
        TCGv_i32 tcg_res = tcg_temp_new_i32();
        unsigned vsz, dofs;

        read_vec_element_i32(s, tcg_op1, a->rn, 3, MO_32);
        read_vec_element_i32(s, tcg_op2, a->rm, 3, MO_32);
        read_vec_element_i32(s, tcg_op3, a->ra, 3, MO_32);

        tcg_gen_rotri_i32(tcg_res, tcg_op1, 20);
        tcg_gen_add_i32(tcg_res, tcg_res, tcg_op2);
        tcg_gen_add_i32(tcg_res, tcg_res, tcg_op3);
        tcg_gen_rotri_i32(tcg_res, tcg_res, 25);

        /* Clear the whole register first, then store bits [127:96]. */
        vsz = vec_full_reg_size(s);
        dofs = vec_full_reg_offset(s, a->rd);
        tcg_gen_gvec_dup_imm(MO_64, dofs, vsz, vsz, 0);
        write_vec_element_i32(s, tcg_res, a->rd, 3, MO_32);
    }
    return true;
}

static bool do_crypto3i(DisasContext *s, arg_crypto3i *a, gen_helper_gvec_3 *fn)
{
    if (fp_access_check(s)) {
        gen_gvec_op3_ool(s, true, a->rd, a->rn, a->rm, a->imm, fn);
    }
    return true;
}
TRANS_FEAT(SM3TT1A, aa64_sm3, do_crypto3i, a, gen_helper_crypto_sm3tt1a)
TRANS_FEAT(SM3TT1B, aa64_sm3, do_crypto3i, a, gen_helper_crypto_sm3tt1b)
TRANS_FEAT(SM3TT2A, aa64_sm3, do_crypto3i, a, gen_helper_crypto_sm3tt2a)
TRANS_FEAT(SM3TT2B, aa64_sm3, do_crypto3i, a, gen_helper_crypto_sm3tt2b)

static bool trans_XAR(DisasContext *s, arg_XAR *a)
{
    if (!dc_isar_feature(aa64_sha3, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_xar(MO_64, vec_full_reg_offset(s, a->rd),
                     vec_full_reg_offset(s, a->rn),
                     vec_full_reg_offset(s, a->rm), a->imm, 16,
                     vec_full_reg_size(s));
    }
    return true;
}

/*
 * Advanced SIMD copy
 */

static bool decode_esz_idx(int imm, MemOp *pesz, unsigned *pidx)
{
    unsigned esz = ctz32(imm);
    if (esz <= MO_64) {
        *pesz = esz;
        *pidx = imm >> (esz + 1);
        return true;
    }
    return false;
}

static bool trans_DUP_element_s(DisasContext *s, arg_DUP_element_s *a)
{
    MemOp esz;
    unsigned idx;

    if (!decode_esz_idx(a->imm, &esz, &idx)) {
        return false;
    }
    if (fp_access_check(s)) {
        /*
         * This instruction just extracts the specified element and
         * zero-extends it into the bottom of the destination register.
         */
        TCGv_i64 tmp = tcg_temp_new_i64();
        read_vec_element(s, tmp, a->rn, idx, esz);
        write_fp_dreg(s, a->rd, tmp);
    }
    return true;
}

static bool trans_DUP_element_v(DisasContext *s, arg_DUP_element_v *a)
{
    MemOp esz;
    unsigned idx;

    if (!decode_esz_idx(a->imm, &esz, &idx)) {
        return false;
    }
    if (esz == MO_64 && !a->q) {
        return false;
    }
    if (fp_access_check(s)) {
        tcg_gen_gvec_dup_mem(esz, vec_full_reg_offset(s, a->rd),
                             vec_reg_offset(s, a->rn, idx, esz),
                             a->q ? 16 : 8, vec_full_reg_size(s));
    }
    return true;
}

static bool trans_DUP_general(DisasContext *s, arg_DUP_general *a)
{
    MemOp esz;
    unsigned idx;

    if (!decode_esz_idx(a->imm, &esz, &idx)) {
        return false;
    }
    if (esz == MO_64 && !a->q) {
        return false;
    }
    if (fp_access_check(s)) {
        tcg_gen_gvec_dup_i64(esz, vec_full_reg_offset(s, a->rd),
                             a->q ? 16 : 8, vec_full_reg_size(s),
                             cpu_reg(s, a->rn));
    }
    return true;
}

static bool do_smov_umov(DisasContext *s, arg_SMOV *a, MemOp is_signed)
{
    MemOp esz;
    unsigned idx;

    if (!decode_esz_idx(a->imm, &esz, &idx)) {
        return false;
    }
    if (is_signed) {
        if (esz == MO_64 || (esz == MO_32 && !a->q)) {
            return false;
        }
    } else {
        if (esz == MO_64 ? !a->q : a->q) {
            return false;
        }
    }
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
        read_vec_element(s, tcg_rd, a->rn, idx, esz | is_signed);
        if (is_signed && !a->q) {
            tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
        }
    }
    return true;
}

TRANS(SMOV, do_smov_umov, a, MO_SIGN)
TRANS(UMOV, do_smov_umov, a, 0)

static bool trans_INS_general(DisasContext *s, arg_INS_general *a)
{
    MemOp esz;
    unsigned idx;

    if (!decode_esz_idx(a->imm, &esz, &idx)) {
        return false;
    }
    if (fp_access_check(s)) {
        write_vec_element(s, cpu_reg(s, a->rn), a->rd, idx, esz);
        clear_vec_high(s, true, a->rd);
    }
    return true;
}

static bool trans_INS_element(DisasContext *s, arg_INS_element *a)
{
    MemOp esz;
    unsigned didx, sidx;

    if (!decode_esz_idx(a->di, &esz, &didx)) {
        return false;
    }
    sidx = a->si >> esz;
    if (fp_access_check(s)) {
        TCGv_i64 tmp = tcg_temp_new_i64();

        read_vec_element(s, tmp, a->rn, sidx, esz);
        write_vec_element(s, tmp, a->rd, didx, esz);

        /* INS is considered a 128-bit write for SVE. */
        clear_vec_high(s, true, a->rd);
    }
    return true;
}

/*
 * Advanced SIMD three same
 */

typedef struct FPScalar {
    void (*gen_h)(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_ptr);
    void (*gen_s)(TCGv_i32, TCGv_i32, TCGv_i32, TCGv_ptr);
    void (*gen_d)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_ptr);
} FPScalar;

static bool do_fp3_scalar(DisasContext *s, arg_rrr_e *a, const FPScalar *f)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t0 = read_fp_dreg(s, a->rn);
            TCGv_i64 t1 = read_fp_dreg(s, a->rm);
            f->gen_d(t0, t0, t1, fpstatus_ptr(FPST_FPCR));
            write_fp_dreg(s, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rn);
            TCGv_i32 t1 = read_fp_sreg(s, a->rm);
            f->gen_s(t0, t0, t1, fpstatus_ptr(FPST_FPCR));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_hreg(s, a->rn);
            TCGv_i32 t1 = read_fp_hreg(s, a->rm);
            f->gen_h(t0, t0, t1, fpstatus_ptr(FPST_FPCR_F16));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    default:
        return false;
    }
    return true;
}

static const FPScalar f_scalar_fadd = {
    gen_helper_vfp_addh,
    gen_helper_vfp_adds,
    gen_helper_vfp_addd,
};
TRANS(FADD_s, do_fp3_scalar, a, &f_scalar_fadd)

static const FPScalar f_scalar_fsub = {
    gen_helper_vfp_subh,
    gen_helper_vfp_subs,
    gen_helper_vfp_subd,
};
TRANS(FSUB_s, do_fp3_scalar, a, &f_scalar_fsub)

static const FPScalar f_scalar_fdiv = {
    gen_helper_vfp_divh,
    gen_helper_vfp_divs,
    gen_helper_vfp_divd,
};
TRANS(FDIV_s, do_fp3_scalar, a, &f_scalar_fdiv)

static const FPScalar f_scalar_fmul = {
    gen_helper_vfp_mulh,
    gen_helper_vfp_muls,
    gen_helper_vfp_muld,
};
TRANS(FMUL_s, do_fp3_scalar, a, &f_scalar_fmul)

static const FPScalar f_scalar_fmax = {
    gen_helper_advsimd_maxh,
    gen_helper_vfp_maxs,
    gen_helper_vfp_maxd,
};
TRANS(FMAX_s, do_fp3_scalar, a, &f_scalar_fmax)

static const FPScalar f_scalar_fmin = {
    gen_helper_advsimd_minh,
    gen_helper_vfp_mins,
    gen_helper_vfp_mind,
};
TRANS(FMIN_s, do_fp3_scalar, a, &f_scalar_fmin)

static const FPScalar f_scalar_fmaxnm = {
    gen_helper_advsimd_maxnumh,
    gen_helper_vfp_maxnums,
    gen_helper_vfp_maxnumd,
};
TRANS(FMAXNM_s, do_fp3_scalar, a, &f_scalar_fmaxnm)

static const FPScalar f_scalar_fminnm = {
    gen_helper_advsimd_minnumh,
    gen_helper_vfp_minnums,
    gen_helper_vfp_minnumd,
};
TRANS(FMINNM_s, do_fp3_scalar, a, &f_scalar_fminnm)

static const FPScalar f_scalar_fmulx = {
    gen_helper_advsimd_mulxh,
    gen_helper_vfp_mulxs,
    gen_helper_vfp_mulxd,
};
TRANS(FMULX_s, do_fp3_scalar, a, &f_scalar_fmulx)

static void gen_fnmul_h(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_mulh(d, n, m, s);
    gen_vfp_negh(d, d);
}

static void gen_fnmul_s(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_muls(d, n, m, s);
    gen_vfp_negs(d, d);
}

static void gen_fnmul_d(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_ptr s)
{
    gen_helper_vfp_muld(d, n, m, s);
    gen_vfp_negd(d, d);
}

static const FPScalar f_scalar_fnmul = {
    gen_fnmul_h,
    gen_fnmul_s,
    gen_fnmul_d,
};
TRANS(FNMUL_s, do_fp3_scalar, a, &f_scalar_fnmul)

static const FPScalar f_scalar_fcmeq = {
    gen_helper_advsimd_ceq_f16,
    gen_helper_neon_ceq_f32,
    gen_helper_neon_ceq_f64,
};
TRANS(FCMEQ_s, do_fp3_scalar, a, &f_scalar_fcmeq)

static const FPScalar f_scalar_fcmge = {
    gen_helper_advsimd_cge_f16,
    gen_helper_neon_cge_f32,
    gen_helper_neon_cge_f64,
};
TRANS(FCMGE_s, do_fp3_scalar, a, &f_scalar_fcmge)

static const FPScalar f_scalar_fcmgt = {
    gen_helper_advsimd_cgt_f16,
    gen_helper_neon_cgt_f32,
    gen_helper_neon_cgt_f64,
};
TRANS(FCMGT_s, do_fp3_scalar, a, &f_scalar_fcmgt)

static const FPScalar f_scalar_facge = {
    gen_helper_advsimd_acge_f16,
    gen_helper_neon_acge_f32,
    gen_helper_neon_acge_f64,
};
TRANS(FACGE_s, do_fp3_scalar, a, &f_scalar_facge)

static const FPScalar f_scalar_facgt = {
    gen_helper_advsimd_acgt_f16,
    gen_helper_neon_acgt_f32,
    gen_helper_neon_acgt_f64,
};
TRANS(FACGT_s, do_fp3_scalar, a, &f_scalar_facgt)

static void gen_fabd_h(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_subh(d, n, m, s);
    gen_vfp_absh(d, d);
}

static void gen_fabd_s(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_subs(d, n, m, s);
    gen_vfp_abss(d, d);
}

static void gen_fabd_d(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_ptr s)
{
    gen_helper_vfp_subd(d, n, m, s);
    gen_vfp_absd(d, d);
}

static const FPScalar f_scalar_fabd = {
    gen_fabd_h,
    gen_fabd_s,
    gen_fabd_d,
};
TRANS(FABD_s, do_fp3_scalar, a, &f_scalar_fabd)

static const FPScalar f_scalar_frecps = {
    gen_helper_recpsf_f16,
    gen_helper_recpsf_f32,
    gen_helper_recpsf_f64,
};
TRANS(FRECPS_s, do_fp3_scalar, a, &f_scalar_frecps)

static const FPScalar f_scalar_frsqrts = {
    gen_helper_rsqrtsf_f16,
    gen_helper_rsqrtsf_f32,
    gen_helper_rsqrtsf_f64,
};
TRANS(FRSQRTS_s, do_fp3_scalar, a, &f_scalar_frsqrts)

static bool do_satacc_s(DisasContext *s, arg_rrr_e *a,
                MemOp sgn_n, MemOp sgn_m,
                void (*gen_bhs)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64, MemOp),
                void (*gen_d)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 t0, t1, t2, qc;
    MemOp esz = a->esz;

    if (!fp_access_check(s)) {
        return true;
    }

    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    t2 = tcg_temp_new_i64();
    qc = tcg_temp_new_i64();
    read_vec_element(s, t1, a->rn, 0, esz | sgn_n);
    read_vec_element(s, t2, a->rm, 0, esz | sgn_m);
    tcg_gen_ld_i64(qc, tcg_env, offsetof(CPUARMState, vfp.qc));

    if (esz == MO_64) {
        gen_d(t0, qc, t1, t2);
    } else {
        gen_bhs(t0, qc, t1, t2, esz);
        tcg_gen_ext_i64(t0, t0, esz);
    }

    write_fp_dreg(s, a->rd, t0);
    tcg_gen_st_i64(qc, tcg_env, offsetof(CPUARMState, vfp.qc));
    return true;
}

TRANS(SQADD_s, do_satacc_s, a, MO_SIGN, MO_SIGN, gen_sqadd_bhs, gen_sqadd_d)
TRANS(SQSUB_s, do_satacc_s, a, MO_SIGN, MO_SIGN, gen_sqsub_bhs, gen_sqsub_d)
TRANS(UQADD_s, do_satacc_s, a, 0, 0, gen_uqadd_bhs, gen_uqadd_d)
TRANS(UQSUB_s, do_satacc_s, a, 0, 0, gen_uqsub_bhs, gen_uqsub_d)
TRANS(SUQADD_s, do_satacc_s, a, MO_SIGN, 0, gen_suqadd_bhs, gen_suqadd_d)
TRANS(USQADD_s, do_satacc_s, a, 0, MO_SIGN, gen_usqadd_bhs, gen_usqadd_d)

static bool do_int3_scalar_d(DisasContext *s, arg_rrr_e *a,
                             void (*fn)(TCGv_i64, TCGv_i64, TCGv_i64))
{
    if (fp_access_check(s)) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();

        read_vec_element(s, t0, a->rn, 0, MO_64);
        read_vec_element(s, t1, a->rm, 0, MO_64);
        fn(t0, t0, t1);
        write_fp_dreg(s, a->rd, t0);
    }
    return true;
}

TRANS(SSHL_s, do_int3_scalar_d, a, gen_sshl_i64)
TRANS(USHL_s, do_int3_scalar_d, a, gen_ushl_i64)
TRANS(SRSHL_s, do_int3_scalar_d, a, gen_helper_neon_rshl_s64)
TRANS(URSHL_s, do_int3_scalar_d, a, gen_helper_neon_rshl_u64)
TRANS(ADD_s, do_int3_scalar_d, a, tcg_gen_add_i64)
TRANS(SUB_s, do_int3_scalar_d, a, tcg_gen_sub_i64)

typedef struct ENVScalar2 {
    NeonGenTwoOpEnvFn *gen_bhs[3];
    NeonGenTwo64OpEnvFn *gen_d;
} ENVScalar2;

static bool do_env_scalar2(DisasContext *s, arg_rrr_e *a, const ENVScalar2 *f)
{
    if (!fp_access_check(s)) {
        return true;
    }
    if (a->esz == MO_64) {
        TCGv_i64 t0 = read_fp_dreg(s, a->rn);
        TCGv_i64 t1 = read_fp_dreg(s, a->rm);
        f->gen_d(t0, tcg_env, t0, t1);
        write_fp_dreg(s, a->rd, t0);
    } else {
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();

        read_vec_element_i32(s, t0, a->rn, 0, a->esz);
        read_vec_element_i32(s, t1, a->rm, 0, a->esz);
        f->gen_bhs[a->esz](t0, tcg_env, t0, t1);
        write_fp_sreg(s, a->rd, t0);
    }
    return true;
}

static const ENVScalar2 f_scalar_sqshl = {
    { gen_helper_neon_qshl_s8,
      gen_helper_neon_qshl_s16,
      gen_helper_neon_qshl_s32 },
    gen_helper_neon_qshl_s64,
};
TRANS(SQSHL_s, do_env_scalar2, a, &f_scalar_sqshl)

static const ENVScalar2 f_scalar_uqshl = {
    { gen_helper_neon_qshl_u8,
      gen_helper_neon_qshl_u16,
      gen_helper_neon_qshl_u32 },
    gen_helper_neon_qshl_u64,
};
TRANS(UQSHL_s, do_env_scalar2, a, &f_scalar_uqshl)

static const ENVScalar2 f_scalar_sqrshl = {
    { gen_helper_neon_qrshl_s8,
      gen_helper_neon_qrshl_s16,
      gen_helper_neon_qrshl_s32 },
    gen_helper_neon_qrshl_s64,
};
TRANS(SQRSHL_s, do_env_scalar2, a, &f_scalar_sqrshl)

static const ENVScalar2 f_scalar_uqrshl = {
    { gen_helper_neon_qrshl_u8,
      gen_helper_neon_qrshl_u16,
      gen_helper_neon_qrshl_u32 },
    gen_helper_neon_qrshl_u64,
};
TRANS(UQRSHL_s, do_env_scalar2, a, &f_scalar_uqrshl)

static bool do_env_scalar2_hs(DisasContext *s, arg_rrr_e *a,
                              const ENVScalar2 *f)
{
    if (a->esz == MO_16 || a->esz == MO_32) {
        return do_env_scalar2(s, a, f);
    }
    return false;
}

static const ENVScalar2 f_scalar_sqdmulh = {
    { NULL, gen_helper_neon_qdmulh_s16, gen_helper_neon_qdmulh_s32 }
};
TRANS(SQDMULH_s, do_env_scalar2_hs, a, &f_scalar_sqdmulh)

static const ENVScalar2 f_scalar_sqrdmulh = {
    { NULL, gen_helper_neon_qrdmulh_s16, gen_helper_neon_qrdmulh_s32 }
};
TRANS(SQRDMULH_s, do_env_scalar2_hs, a, &f_scalar_sqrdmulh)

typedef struct ENVScalar3 {
    NeonGenThreeOpEnvFn *gen_hs[2];
} ENVScalar3;

static bool do_env_scalar3_hs(DisasContext *s, arg_rrr_e *a,
                              const ENVScalar3 *f)
{
    TCGv_i32 t0, t1, t2;

    if (a->esz != MO_16 && a->esz != MO_32) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    t0 = tcg_temp_new_i32();
    t1 = tcg_temp_new_i32();
    t2 = tcg_temp_new_i32();
    read_vec_element_i32(s, t0, a->rn, 0, a->esz);
    read_vec_element_i32(s, t1, a->rm, 0, a->esz);
    read_vec_element_i32(s, t2, a->rd, 0, a->esz);
    f->gen_hs[a->esz - 1](t0, tcg_env, t0, t1, t2);
    write_fp_sreg(s, a->rd, t0);
    return true;
}

static const ENVScalar3 f_scalar_sqrdmlah = {
    { gen_helper_neon_qrdmlah_s16, gen_helper_neon_qrdmlah_s32 }
};
TRANS_FEAT(SQRDMLAH_s, aa64_rdm, do_env_scalar3_hs, a, &f_scalar_sqrdmlah)

static const ENVScalar3 f_scalar_sqrdmlsh = {
    { gen_helper_neon_qrdmlsh_s16, gen_helper_neon_qrdmlsh_s32 }
};
TRANS_FEAT(SQRDMLSH_s, aa64_rdm, do_env_scalar3_hs, a, &f_scalar_sqrdmlsh)

static bool do_cmop_d(DisasContext *s, arg_rrr_e *a, TCGCond cond)
{
    if (fp_access_check(s)) {
        TCGv_i64 t0 = read_fp_dreg(s, a->rn);
        TCGv_i64 t1 = read_fp_dreg(s, a->rm);
        tcg_gen_negsetcond_i64(cond, t0, t0, t1);
        write_fp_dreg(s, a->rd, t0);
    }
    return true;
}

TRANS(CMGT_s, do_cmop_d, a, TCG_COND_GT)
TRANS(CMHI_s, do_cmop_d, a, TCG_COND_GTU)
TRANS(CMGE_s, do_cmop_d, a, TCG_COND_GE)
TRANS(CMHS_s, do_cmop_d, a, TCG_COND_GEU)
TRANS(CMEQ_s, do_cmop_d, a, TCG_COND_EQ)
TRANS(CMTST_s, do_cmop_d, a, TCG_COND_TSTNE)

static bool do_fp3_vector(DisasContext *s, arg_qrrr_e *a, int data,
                          gen_helper_gvec_3_ptr * const fns[3])
{
    MemOp esz = a->esz;

    switch (esz) {
    case MO_64:
        if (!a->q) {
            return false;
        }
        break;
    case MO_32:
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        break;
    default:
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_op3_fpst(s, a->q, a->rd, a->rn, a->rm,
                          esz == MO_16, data, fns[esz - 1]);
    }
    return true;
}

static gen_helper_gvec_3_ptr * const f_vector_fadd[3] = {
    gen_helper_gvec_fadd_h,
    gen_helper_gvec_fadd_s,
    gen_helper_gvec_fadd_d,
};
TRANS(FADD_v, do_fp3_vector, a, 0, f_vector_fadd)

static gen_helper_gvec_3_ptr * const f_vector_fsub[3] = {
    gen_helper_gvec_fsub_h,
    gen_helper_gvec_fsub_s,
    gen_helper_gvec_fsub_d,
};
TRANS(FSUB_v, do_fp3_vector, a, 0, f_vector_fsub)

static gen_helper_gvec_3_ptr * const f_vector_fdiv[3] = {
    gen_helper_gvec_fdiv_h,
    gen_helper_gvec_fdiv_s,
    gen_helper_gvec_fdiv_d,
};
TRANS(FDIV_v, do_fp3_vector, a, 0, f_vector_fdiv)

static gen_helper_gvec_3_ptr * const f_vector_fmul[3] = {
    gen_helper_gvec_fmul_h,
    gen_helper_gvec_fmul_s,
    gen_helper_gvec_fmul_d,
};
TRANS(FMUL_v, do_fp3_vector, a, 0, f_vector_fmul)

static gen_helper_gvec_3_ptr * const f_vector_fmax[3] = {
    gen_helper_gvec_fmax_h,
    gen_helper_gvec_fmax_s,
    gen_helper_gvec_fmax_d,
};
TRANS(FMAX_v, do_fp3_vector, a, 0, f_vector_fmax)

static gen_helper_gvec_3_ptr * const f_vector_fmin[3] = {
    gen_helper_gvec_fmin_h,
    gen_helper_gvec_fmin_s,
    gen_helper_gvec_fmin_d,
};
TRANS(FMIN_v, do_fp3_vector, a, 0, f_vector_fmin)

static gen_helper_gvec_3_ptr * const f_vector_fmaxnm[3] = {
    gen_helper_gvec_fmaxnum_h,
    gen_helper_gvec_fmaxnum_s,
    gen_helper_gvec_fmaxnum_d,
};
TRANS(FMAXNM_v, do_fp3_vector, a, 0, f_vector_fmaxnm)

static gen_helper_gvec_3_ptr * const f_vector_fminnm[3] = {
    gen_helper_gvec_fminnum_h,
    gen_helper_gvec_fminnum_s,
    gen_helper_gvec_fminnum_d,
};
TRANS(FMINNM_v, do_fp3_vector, a, 0, f_vector_fminnm)

static gen_helper_gvec_3_ptr * const f_vector_fmulx[3] = {
    gen_helper_gvec_fmulx_h,
    gen_helper_gvec_fmulx_s,
    gen_helper_gvec_fmulx_d,
};
TRANS(FMULX_v, do_fp3_vector, a, 0, f_vector_fmulx)

static gen_helper_gvec_3_ptr * const f_vector_fmla[3] = {
    gen_helper_gvec_vfma_h,
    gen_helper_gvec_vfma_s,
    gen_helper_gvec_vfma_d,
};
TRANS(FMLA_v, do_fp3_vector, a, 0, f_vector_fmla)

static gen_helper_gvec_3_ptr * const f_vector_fmls[3] = {
    gen_helper_gvec_vfms_h,
    gen_helper_gvec_vfms_s,
    gen_helper_gvec_vfms_d,
};
TRANS(FMLS_v, do_fp3_vector, a, 0, f_vector_fmls)

static gen_helper_gvec_3_ptr * const f_vector_fcmeq[3] = {
    gen_helper_gvec_fceq_h,
    gen_helper_gvec_fceq_s,
    gen_helper_gvec_fceq_d,
};
TRANS(FCMEQ_v, do_fp3_vector, a, 0, f_vector_fcmeq)

static gen_helper_gvec_3_ptr * const f_vector_fcmge[3] = {
    gen_helper_gvec_fcge_h,
    gen_helper_gvec_fcge_s,
    gen_helper_gvec_fcge_d,
};
TRANS(FCMGE_v, do_fp3_vector, a, 0, f_vector_fcmge)

static gen_helper_gvec_3_ptr * const f_vector_fcmgt[3] = {
    gen_helper_gvec_fcgt_h,
    gen_helper_gvec_fcgt_s,
    gen_helper_gvec_fcgt_d,
};
TRANS(FCMGT_v, do_fp3_vector, a, 0, f_vector_fcmgt)

static gen_helper_gvec_3_ptr * const f_vector_facge[3] = {
    gen_helper_gvec_facge_h,
    gen_helper_gvec_facge_s,
    gen_helper_gvec_facge_d,
};
TRANS(FACGE_v, do_fp3_vector, a, 0, f_vector_facge)

static gen_helper_gvec_3_ptr * const f_vector_facgt[3] = {
    gen_helper_gvec_facgt_h,
    gen_helper_gvec_facgt_s,
    gen_helper_gvec_facgt_d,
};
TRANS(FACGT_v, do_fp3_vector, a, 0, f_vector_facgt)

static gen_helper_gvec_3_ptr * const f_vector_fabd[3] = {
    gen_helper_gvec_fabd_h,
    gen_helper_gvec_fabd_s,
    gen_helper_gvec_fabd_d,
};
TRANS(FABD_v, do_fp3_vector, a, 0, f_vector_fabd)

static gen_helper_gvec_3_ptr * const f_vector_frecps[3] = {
    gen_helper_gvec_recps_h,
    gen_helper_gvec_recps_s,
    gen_helper_gvec_recps_d,
};
TRANS(FRECPS_v, do_fp3_vector, a, 0, f_vector_frecps)

static gen_helper_gvec_3_ptr * const f_vector_frsqrts[3] = {
    gen_helper_gvec_rsqrts_h,
    gen_helper_gvec_rsqrts_s,
    gen_helper_gvec_rsqrts_d,
};
TRANS(FRSQRTS_v, do_fp3_vector, a, 0, f_vector_frsqrts)

static gen_helper_gvec_3_ptr * const f_vector_faddp[3] = {
    gen_helper_gvec_faddp_h,
    gen_helper_gvec_faddp_s,
    gen_helper_gvec_faddp_d,
};
TRANS(FADDP_v, do_fp3_vector, a, 0, f_vector_faddp)

static gen_helper_gvec_3_ptr * const f_vector_fmaxp[3] = {
    gen_helper_gvec_fmaxp_h,
    gen_helper_gvec_fmaxp_s,
    gen_helper_gvec_fmaxp_d,
};
TRANS(FMAXP_v, do_fp3_vector, a, 0, f_vector_fmaxp)

static gen_helper_gvec_3_ptr * const f_vector_fminp[3] = {
    gen_helper_gvec_fminp_h,
    gen_helper_gvec_fminp_s,
    gen_helper_gvec_fminp_d,
};
TRANS(FMINP_v, do_fp3_vector, a, 0, f_vector_fminp)

static gen_helper_gvec_3_ptr * const f_vector_fmaxnmp[3] = {
    gen_helper_gvec_fmaxnump_h,
    gen_helper_gvec_fmaxnump_s,
    gen_helper_gvec_fmaxnump_d,
};
TRANS(FMAXNMP_v, do_fp3_vector, a, 0, f_vector_fmaxnmp)

static gen_helper_gvec_3_ptr * const f_vector_fminnmp[3] = {
    gen_helper_gvec_fminnump_h,
    gen_helper_gvec_fminnump_s,
    gen_helper_gvec_fminnump_d,
};
TRANS(FMINNMP_v, do_fp3_vector, a, 0, f_vector_fminnmp)

static bool do_fmlal(DisasContext *s, arg_qrrr_e *a, bool is_s, bool is_2)
{
    if (fp_access_check(s)) {
        int data = (is_2 << 1) | is_s;
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm), tcg_env,
                           a->q ? 16 : 8, vec_full_reg_size(s),
                           data, gen_helper_gvec_fmlal_a64);
    }
    return true;
}

TRANS_FEAT(FMLAL_v, aa64_fhm, do_fmlal, a, false, false)
TRANS_FEAT(FMLSL_v, aa64_fhm, do_fmlal, a, true, false)
TRANS_FEAT(FMLAL2_v, aa64_fhm, do_fmlal, a, false, true)
TRANS_FEAT(FMLSL2_v, aa64_fhm, do_fmlal, a, true, true)

TRANS(ADDP_v, do_gvec_fn3, a, gen_gvec_addp)
TRANS(SMAXP_v, do_gvec_fn3_no64, a, gen_gvec_smaxp)
TRANS(SMINP_v, do_gvec_fn3_no64, a, gen_gvec_sminp)
TRANS(UMAXP_v, do_gvec_fn3_no64, a, gen_gvec_umaxp)
TRANS(UMINP_v, do_gvec_fn3_no64, a, gen_gvec_uminp)

TRANS(AND_v, do_gvec_fn3, a, tcg_gen_gvec_and)
TRANS(BIC_v, do_gvec_fn3, a, tcg_gen_gvec_andc)
TRANS(ORR_v, do_gvec_fn3, a, tcg_gen_gvec_or)
TRANS(ORN_v, do_gvec_fn3, a, tcg_gen_gvec_orc)
TRANS(EOR_v, do_gvec_fn3, a, tcg_gen_gvec_xor)

static bool do_bitsel(DisasContext *s, bool is_q, int d, int a, int b, int c)
{
    if (fp_access_check(s)) {
        gen_gvec_fn4(s, is_q, d, a, b, c, tcg_gen_gvec_bitsel, 0);
    }
    return true;
}

TRANS(BSL_v, do_bitsel, a->q, a->rd, a->rd, a->rn, a->rm)
TRANS(BIT_v, do_bitsel, a->q, a->rd, a->rm, a->rn, a->rd)
TRANS(BIF_v, do_bitsel, a->q, a->rd, a->rm, a->rd, a->rn)

TRANS(SQADD_v, do_gvec_fn3, a, gen_gvec_sqadd_qc)
TRANS(UQADD_v, do_gvec_fn3, a, gen_gvec_uqadd_qc)
TRANS(SQSUB_v, do_gvec_fn3, a, gen_gvec_sqsub_qc)
TRANS(UQSUB_v, do_gvec_fn3, a, gen_gvec_uqsub_qc)
TRANS(SUQADD_v, do_gvec_fn3, a, gen_gvec_suqadd_qc)
TRANS(USQADD_v, do_gvec_fn3, a, gen_gvec_usqadd_qc)

TRANS(SSHL_v, do_gvec_fn3, a, gen_gvec_sshl)
TRANS(USHL_v, do_gvec_fn3, a, gen_gvec_ushl)
TRANS(SRSHL_v, do_gvec_fn3, a, gen_gvec_srshl)
TRANS(URSHL_v, do_gvec_fn3, a, gen_gvec_urshl)
TRANS(SQSHL_v, do_gvec_fn3, a, gen_neon_sqshl)
TRANS(UQSHL_v, do_gvec_fn3, a, gen_neon_uqshl)
TRANS(SQRSHL_v, do_gvec_fn3, a, gen_neon_sqrshl)
TRANS(UQRSHL_v, do_gvec_fn3, a, gen_neon_uqrshl)

TRANS(ADD_v, do_gvec_fn3, a, tcg_gen_gvec_add)
TRANS(SUB_v, do_gvec_fn3, a, tcg_gen_gvec_sub)
TRANS(SHADD_v, do_gvec_fn3_no64, a, gen_gvec_shadd)
TRANS(UHADD_v, do_gvec_fn3_no64, a, gen_gvec_uhadd)
TRANS(SHSUB_v, do_gvec_fn3_no64, a, gen_gvec_shsub)
TRANS(UHSUB_v, do_gvec_fn3_no64, a, gen_gvec_uhsub)
TRANS(SRHADD_v, do_gvec_fn3_no64, a, gen_gvec_srhadd)
TRANS(URHADD_v, do_gvec_fn3_no64, a, gen_gvec_urhadd)
TRANS(SMAX_v, do_gvec_fn3_no64, a, tcg_gen_gvec_smax)
TRANS(UMAX_v, do_gvec_fn3_no64, a, tcg_gen_gvec_umax)
TRANS(SMIN_v, do_gvec_fn3_no64, a, tcg_gen_gvec_smin)
TRANS(UMIN_v, do_gvec_fn3_no64, a, tcg_gen_gvec_umin)
TRANS(SABA_v, do_gvec_fn3_no64, a, gen_gvec_saba)
TRANS(UABA_v, do_gvec_fn3_no64, a, gen_gvec_uaba)
TRANS(SABD_v, do_gvec_fn3_no64, a, gen_gvec_sabd)
TRANS(UABD_v, do_gvec_fn3_no64, a, gen_gvec_uabd)
TRANS(MUL_v, do_gvec_fn3_no64, a, tcg_gen_gvec_mul)
TRANS(PMUL_v, do_gvec_op3_ool, a, 0, gen_helper_gvec_pmul_b)
TRANS(MLA_v, do_gvec_fn3_no64, a, gen_gvec_mla)
TRANS(MLS_v, do_gvec_fn3_no64, a, gen_gvec_mls)

static bool do_cmop_v(DisasContext *s, arg_qrrr_e *a, TCGCond cond)
{
    if (a->esz == MO_64 && !a->q) {
        return false;
    }
    if (fp_access_check(s)) {
        tcg_gen_gvec_cmp(cond, a->esz,
                         vec_full_reg_offset(s, a->rd),
                         vec_full_reg_offset(s, a->rn),
                         vec_full_reg_offset(s, a->rm),
                         a->q ? 16 : 8, vec_full_reg_size(s));
    }
    return true;
}

TRANS(CMGT_v, do_cmop_v, a, TCG_COND_GT)
TRANS(CMHI_v, do_cmop_v, a, TCG_COND_GTU)
TRANS(CMGE_v, do_cmop_v, a, TCG_COND_GE)
TRANS(CMHS_v, do_cmop_v, a, TCG_COND_GEU)
TRANS(CMEQ_v, do_cmop_v, a, TCG_COND_EQ)
TRANS(CMTST_v, do_gvec_fn3, a, gen_gvec_cmtst)

TRANS(SQDMULH_v, do_gvec_fn3_no8_no64, a, gen_gvec_sqdmulh_qc)
TRANS(SQRDMULH_v, do_gvec_fn3_no8_no64, a, gen_gvec_sqrdmulh_qc)
TRANS_FEAT(SQRDMLAH_v, aa64_rdm, do_gvec_fn3_no8_no64, a, gen_gvec_sqrdmlah_qc)
TRANS_FEAT(SQRDMLSH_v, aa64_rdm, do_gvec_fn3_no8_no64, a, gen_gvec_sqrdmlsh_qc)

static bool do_dot_vector(DisasContext *s, arg_qrrr_e *a,
                          gen_helper_gvec_4 *fn)
{
    if (fp_access_check(s)) {
        gen_gvec_op4_ool(s, a->q, a->rd, a->rn, a->rm, a->rd, 0, fn);
    }
    return true;
}

TRANS_FEAT(SDOT_v, aa64_dp, do_dot_vector, a, gen_helper_gvec_sdot_b)
TRANS_FEAT(UDOT_v, aa64_dp, do_dot_vector, a, gen_helper_gvec_udot_b)
TRANS_FEAT(USDOT_v, aa64_i8mm, do_dot_vector, a, gen_helper_gvec_usdot_b)
TRANS_FEAT(BFDOT_v, aa64_bf16, do_dot_vector, a, gen_helper_gvec_bfdot)
TRANS_FEAT(BFMMLA, aa64_bf16, do_dot_vector, a, gen_helper_gvec_bfmmla)
TRANS_FEAT(SMMLA, aa64_i8mm, do_dot_vector, a, gen_helper_gvec_smmla_b)
TRANS_FEAT(UMMLA, aa64_i8mm, do_dot_vector, a, gen_helper_gvec_ummla_b)
TRANS_FEAT(USMMLA, aa64_i8mm, do_dot_vector, a, gen_helper_gvec_usmmla_b)

static bool trans_BFMLAL_v(DisasContext *s, arg_qrrr_e *a)
{
    if (!dc_isar_feature(aa64_bf16, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        /* Q bit selects BFMLALB vs BFMLALT. */
        gen_gvec_op4_fpst(s, true, a->rd, a->rn, a->rm, a->rd, false, a->q,
                          gen_helper_gvec_bfmlal);
    }
    return true;
}

static gen_helper_gvec_3_ptr * const f_vector_fcadd[3] = {
    gen_helper_gvec_fcaddh,
    gen_helper_gvec_fcadds,
    gen_helper_gvec_fcaddd,
};
TRANS_FEAT(FCADD_90, aa64_fcma, do_fp3_vector, a, 0, f_vector_fcadd)
TRANS_FEAT(FCADD_270, aa64_fcma, do_fp3_vector, a, 1, f_vector_fcadd)

static bool trans_FCMLA_v(DisasContext *s, arg_FCMLA_v *a)
{
    gen_helper_gvec_4_ptr *fn;

    if (!dc_isar_feature(aa64_fcma, s)) {
        return false;
    }
    switch (a->esz) {
    case MO_64:
        if (!a->q) {
            return false;
        }
        fn = gen_helper_gvec_fcmlad;
        break;
    case MO_32:
        fn = gen_helper_gvec_fcmlas;
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        fn = gen_helper_gvec_fcmlah;
        break;
    default:
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_op4_fpst(s, a->q, a->rd, a->rn, a->rm, a->rd,
                          a->esz == MO_16, a->rot, fn);
    }
    return true;
}

/*
 * Widening vector x vector/indexed.
 *
 * These read from the top or bottom half of a 128-bit vector.
 * After widening, optionally accumulate with a 128-bit vector.
 * Implement these inline, as the number of elements are limited
 * and the related SVE and SME operations on larger vectors use
 * even/odd elements instead of top/bottom half.
 *
 * If idx >= 0, operand 2 is indexed, otherwise vector.
 * If acc, operand 0 is loaded with rd.
 */

/* For low half, iterating up. */
static bool do_3op_widening(DisasContext *s, MemOp memop, int top,
                            int rd, int rn, int rm, int idx,
                            NeonGenTwo64OpFn *fn, bool acc)
{
    TCGv_i64 tcg_op0 = tcg_temp_new_i64();
    TCGv_i64 tcg_op1 = tcg_temp_new_i64();
    TCGv_i64 tcg_op2 = tcg_temp_new_i64();
    MemOp esz = memop & MO_SIZE;
    int half = 8 >> esz;
    int top_swap, top_half;

    /* There are no 64x64->128 bit operations. */
    if (esz >= MO_64) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    if (idx >= 0) {
        read_vec_element(s, tcg_op2, rm, idx, memop);
    }

    /*
     * For top half inputs, iterate forward; backward for bottom half.
     * This means the store to the destination will not occur until
     * overlapping input inputs are consumed.
     * Use top_swap to conditionally invert the forward iteration index.
     */
    top_swap = top ? 0 : half - 1;
    top_half = top ? half : 0;

    for (int elt_fwd = 0; elt_fwd < half; ++elt_fwd) {
        int elt = elt_fwd ^ top_swap;

        read_vec_element(s, tcg_op1, rn, elt + top_half, memop);
        if (idx < 0) {
            read_vec_element(s, tcg_op2, rm, elt + top_half, memop);
        }
        if (acc) {
            read_vec_element(s, tcg_op0, rd, elt, memop + 1);
        }
        fn(tcg_op0, tcg_op1, tcg_op2);
        write_vec_element(s, tcg_op0, rd, elt, esz + 1);
    }
    clear_vec_high(s, 1, rd);
    return true;
}

static void gen_muladd_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();
    tcg_gen_mul_i64(t, n, m);
    tcg_gen_add_i64(d, d, t);
}

static void gen_mulsub_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();
    tcg_gen_mul_i64(t, n, m);
    tcg_gen_sub_i64(d, d, t);
}

TRANS(SMULL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      tcg_gen_mul_i64, false)
TRANS(UMULL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      tcg_gen_mul_i64, false)
TRANS(SMLAL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      gen_muladd_i64, true)
TRANS(UMLAL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      gen_muladd_i64, true)
TRANS(SMLSL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      gen_mulsub_i64, true)
TRANS(UMLSL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      gen_mulsub_i64, true)

TRANS(SMULL_vi, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, a->idx,
      tcg_gen_mul_i64, false)
TRANS(UMULL_vi, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, a->idx,
      tcg_gen_mul_i64, false)
TRANS(SMLAL_vi, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, a->idx,
      gen_muladd_i64, true)
TRANS(UMLAL_vi, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, a->idx,
      gen_muladd_i64, true)
TRANS(SMLSL_vi, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, a->idx,
      gen_mulsub_i64, true)
TRANS(UMLSL_vi, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, a->idx,
      gen_mulsub_i64, true)

static void gen_sabd_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_sub_i64(t1, n, m);
    tcg_gen_sub_i64(t2, m, n);
    tcg_gen_movcond_i64(TCG_COND_GE, d, n, m, t1, t2);
}

static void gen_uabd_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t1 = tcg_temp_new_i64();
    TCGv_i64 t2 = tcg_temp_new_i64();

    tcg_gen_sub_i64(t1, n, m);
    tcg_gen_sub_i64(t2, m, n);
    tcg_gen_movcond_i64(TCG_COND_GEU, d, n, m, t1, t2);
}

static void gen_saba_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();
    gen_sabd_i64(t, n, m);
    tcg_gen_add_i64(d, d, t);
}

static void gen_uaba_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();
    gen_uabd_i64(t, n, m);
    tcg_gen_add_i64(d, d, t);
}

TRANS(SADDL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      tcg_gen_add_i64, false)
TRANS(UADDL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      tcg_gen_add_i64, false)
TRANS(SSUBL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      tcg_gen_sub_i64, false)
TRANS(USUBL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      tcg_gen_sub_i64, false)
TRANS(SABDL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      gen_sabd_i64, false)
TRANS(UABDL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      gen_uabd_i64, false)
TRANS(SABAL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      gen_saba_i64, true)
TRANS(UABAL_v, do_3op_widening,
      a->esz, a->q, a->rd, a->rn, a->rm, -1,
      gen_uaba_i64, true)

static void gen_sqdmull_h(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    tcg_gen_mul_i64(d, n, m);
    gen_helper_neon_addl_saturate_s32(d, tcg_env, d, d);
}

static void gen_sqdmull_s(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    tcg_gen_mul_i64(d, n, m);
    gen_helper_neon_addl_saturate_s64(d, tcg_env, d, d);
}

static void gen_sqdmlal_h(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_mul_i64(t, n, m);
    gen_helper_neon_addl_saturate_s32(t, tcg_env, t, t);
    gen_helper_neon_addl_saturate_s32(d, tcg_env, d, t);
}

static void gen_sqdmlal_s(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_mul_i64(t, n, m);
    gen_helper_neon_addl_saturate_s64(t, tcg_env, t, t);
    gen_helper_neon_addl_saturate_s64(d, tcg_env, d, t);
}

static void gen_sqdmlsl_h(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_mul_i64(t, n, m);
    gen_helper_neon_addl_saturate_s32(t, tcg_env, t, t);
    tcg_gen_neg_i64(t, t);
    gen_helper_neon_addl_saturate_s32(d, tcg_env, d, t);
}

static void gen_sqdmlsl_s(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    TCGv_i64 t = tcg_temp_new_i64();

    tcg_gen_mul_i64(t, n, m);
    gen_helper_neon_addl_saturate_s64(t, tcg_env, t, t);
    tcg_gen_neg_i64(t, t);
    gen_helper_neon_addl_saturate_s64(d, tcg_env, d, t);
}

TRANS(SQDMULL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      a->esz == MO_16 ? gen_sqdmull_h : gen_sqdmull_s, false)
TRANS(SQDMLAL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      a->esz == MO_16 ? gen_sqdmlal_h : gen_sqdmlal_s, true)
TRANS(SQDMLSL_v, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, -1,
      a->esz == MO_16 ? gen_sqdmlsl_h : gen_sqdmlsl_s, true)

TRANS(SQDMULL_vi, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, a->idx,
      a->esz == MO_16 ? gen_sqdmull_h : gen_sqdmull_s, false)
TRANS(SQDMLAL_vi, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, a->idx,
      a->esz == MO_16 ? gen_sqdmlal_h : gen_sqdmlal_s, true)
TRANS(SQDMLSL_vi, do_3op_widening,
      a->esz | MO_SIGN, a->q, a->rd, a->rn, a->rm, a->idx,
      a->esz == MO_16 ? gen_sqdmlsl_h : gen_sqdmlsl_s, true)

static bool do_addsub_wide(DisasContext *s, arg_qrrr_e *a,
                           MemOp sign, bool sub)
{
    TCGv_i64 tcg_op0, tcg_op1;
    MemOp esz = a->esz;
    int half = 8 >> esz;
    bool top = a->q;
    int top_swap = top ? 0 : half - 1;
    int top_half = top ? half : 0;

    /* There are no 64x64->128 bit operations. */
    if (esz >= MO_64) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }
    tcg_op0 = tcg_temp_new_i64();
    tcg_op1 = tcg_temp_new_i64();

    for (int elt_fwd = 0; elt_fwd < half; ++elt_fwd) {
        int elt = elt_fwd ^ top_swap;

        read_vec_element(s, tcg_op1, a->rm, elt + top_half, esz | sign);
        read_vec_element(s, tcg_op0, a->rn, elt, esz + 1);
        if (sub) {
            tcg_gen_sub_i64(tcg_op0, tcg_op0, tcg_op1);
        } else {
            tcg_gen_add_i64(tcg_op0, tcg_op0, tcg_op1);
        }
        write_vec_element(s, tcg_op0, a->rd, elt, esz + 1);
    }
    clear_vec_high(s, 1, a->rd);
    return true;
}

TRANS(SADDW, do_addsub_wide, a, MO_SIGN, false)
TRANS(UADDW, do_addsub_wide, a, 0, false)
TRANS(SSUBW, do_addsub_wide, a, MO_SIGN, true)
TRANS(USUBW, do_addsub_wide, a, 0, true)

static bool do_addsub_highnarrow(DisasContext *s, arg_qrrr_e *a,
                                 bool sub, bool round)
{
    TCGv_i64 tcg_op0, tcg_op1;
    MemOp esz = a->esz;
    int half = 8 >> esz;
    bool top = a->q;
    int ebits = 8 << esz;
    uint64_t rbit = 1ull << (ebits - 1);
    int top_swap, top_half;

    /* There are no 128x128->64 bit operations. */
    if (esz >= MO_64) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }
    tcg_op0 = tcg_temp_new_i64();
    tcg_op1 = tcg_temp_new_i64();

    /*
     * For top half inputs, iterate backward; forward for bottom half.
     * This means the store to the destination will not occur until
     * overlapping input inputs are consumed.
     */
    top_swap = top ? half - 1 : 0;
    top_half = top ? half : 0;

    for (int elt_fwd = 0; elt_fwd < half; ++elt_fwd) {
        int elt = elt_fwd ^ top_swap;

        read_vec_element(s, tcg_op1, a->rm, elt, esz + 1);
        read_vec_element(s, tcg_op0, a->rn, elt, esz + 1);
        if (sub) {
            tcg_gen_sub_i64(tcg_op0, tcg_op0, tcg_op1);
        } else {
            tcg_gen_add_i64(tcg_op0, tcg_op0, tcg_op1);
        }
        if (round) {
            tcg_gen_addi_i64(tcg_op0, tcg_op0, rbit);
        }
        tcg_gen_shri_i64(tcg_op0, tcg_op0, ebits);
        write_vec_element(s, tcg_op0, a->rd, elt + top_half, esz);
    }
    clear_vec_high(s, top, a->rd);
    return true;
}

TRANS(ADDHN, do_addsub_highnarrow, a, false, false)
TRANS(SUBHN, do_addsub_highnarrow, a, true, false)
TRANS(RADDHN, do_addsub_highnarrow, a, false, true)
TRANS(RSUBHN, do_addsub_highnarrow, a, true, true)

static bool do_pmull(DisasContext *s, arg_qrrr_e *a, gen_helper_gvec_3 *fn)
{
    if (fp_access_check(s)) {
        /* The Q field specifies lo/hi half input for these insns.  */
        gen_gvec_op3_ool(s, true, a->rd, a->rn, a->rm, a->q, fn);
    }
    return true;
}

TRANS(PMULL_p8, do_pmull, a, gen_helper_neon_pmull_h)
TRANS_FEAT(PMULL_p64, aa64_pmull, do_pmull, a, gen_helper_gvec_pmull_q)

/*
 * Advanced SIMD scalar/vector x indexed element
 */

static bool do_fp3_scalar_idx(DisasContext *s, arg_rrx_e *a, const FPScalar *f)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t0 = read_fp_dreg(s, a->rn);
            TCGv_i64 t1 = tcg_temp_new_i64();

            read_vec_element(s, t1, a->rm, a->idx, MO_64);
            f->gen_d(t0, t0, t1, fpstatus_ptr(FPST_FPCR));
            write_fp_dreg(s, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rn);
            TCGv_i32 t1 = tcg_temp_new_i32();

            read_vec_element_i32(s, t1, a->rm, a->idx, MO_32);
            f->gen_s(t0, t0, t1, fpstatus_ptr(FPST_FPCR));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_hreg(s, a->rn);
            TCGv_i32 t1 = tcg_temp_new_i32();

            read_vec_element_i32(s, t1, a->rm, a->idx, MO_16);
            f->gen_h(t0, t0, t1, fpstatus_ptr(FPST_FPCR_F16));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

TRANS(FMUL_si, do_fp3_scalar_idx, a, &f_scalar_fmul)
TRANS(FMULX_si, do_fp3_scalar_idx, a, &f_scalar_fmulx)

static bool do_fmla_scalar_idx(DisasContext *s, arg_rrx_e *a, bool neg)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t0 = read_fp_dreg(s, a->rd);
            TCGv_i64 t1 = read_fp_dreg(s, a->rn);
            TCGv_i64 t2 = tcg_temp_new_i64();

            read_vec_element(s, t2, a->rm, a->idx, MO_64);
            if (neg) {
                gen_vfp_negd(t1, t1);
            }
            gen_helper_vfp_muladdd(t0, t1, t2, t0, fpstatus_ptr(FPST_FPCR));
            write_fp_dreg(s, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rd);
            TCGv_i32 t1 = read_fp_sreg(s, a->rn);
            TCGv_i32 t2 = tcg_temp_new_i32();

            read_vec_element_i32(s, t2, a->rm, a->idx, MO_32);
            if (neg) {
                gen_vfp_negs(t1, t1);
            }
            gen_helper_vfp_muladds(t0, t1, t2, t0, fpstatus_ptr(FPST_FPCR));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_hreg(s, a->rd);
            TCGv_i32 t1 = read_fp_hreg(s, a->rn);
            TCGv_i32 t2 = tcg_temp_new_i32();

            read_vec_element_i32(s, t2, a->rm, a->idx, MO_16);
            if (neg) {
                gen_vfp_negh(t1, t1);
            }
            gen_helper_advsimd_muladdh(t0, t1, t2, t0,
                                       fpstatus_ptr(FPST_FPCR_F16));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

TRANS(FMLA_si, do_fmla_scalar_idx, a, false)
TRANS(FMLS_si, do_fmla_scalar_idx, a, true)

static bool do_env_scalar2_idx_hs(DisasContext *s, arg_rrx_e *a,
                                  const ENVScalar2 *f)
{
    if (a->esz < MO_16 || a->esz > MO_32) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();

        read_vec_element_i32(s, t0, a->rn, 0, a->esz);
        read_vec_element_i32(s, t1, a->rm, a->idx, a->esz);
        f->gen_bhs[a->esz](t0, tcg_env, t0, t1);
        write_fp_sreg(s, a->rd, t0);
    }
    return true;
}

TRANS(SQDMULH_si, do_env_scalar2_idx_hs, a, &f_scalar_sqdmulh)
TRANS(SQRDMULH_si, do_env_scalar2_idx_hs, a, &f_scalar_sqrdmulh)

static bool do_env_scalar3_idx_hs(DisasContext *s, arg_rrx_e *a,
                                  const ENVScalar3 *f)
{
    if (a->esz < MO_16 || a->esz > MO_32) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i32 t0 = tcg_temp_new_i32();
        TCGv_i32 t1 = tcg_temp_new_i32();
        TCGv_i32 t2 = tcg_temp_new_i32();

        read_vec_element_i32(s, t0, a->rn, 0, a->esz);
        read_vec_element_i32(s, t1, a->rm, a->idx, a->esz);
        read_vec_element_i32(s, t2, a->rd, 0, a->esz);
        f->gen_hs[a->esz - 1](t0, tcg_env, t0, t1, t2);
        write_fp_sreg(s, a->rd, t0);
    }
    return true;
}

TRANS_FEAT(SQRDMLAH_si, aa64_rdm, do_env_scalar3_idx_hs, a, &f_scalar_sqrdmlah)
TRANS_FEAT(SQRDMLSH_si, aa64_rdm, do_env_scalar3_idx_hs, a, &f_scalar_sqrdmlsh)

static bool do_scalar_muladd_widening_idx(DisasContext *s, arg_rrx_e *a,
                                          NeonGenTwo64OpFn *fn, bool acc)
{
    if (fp_access_check(s)) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();
        TCGv_i64 t2 = tcg_temp_new_i64();
        unsigned vsz, dofs;

        if (acc) {
            read_vec_element(s, t0, a->rd, 0, a->esz + 1);
        }
        read_vec_element(s, t1, a->rn, 0, a->esz | MO_SIGN);
        read_vec_element(s, t2, a->rm, a->idx, a->esz | MO_SIGN);
        fn(t0, t1, t2);

        /* Clear the whole register first, then store scalar. */
        vsz = vec_full_reg_size(s);
        dofs = vec_full_reg_offset(s, a->rd);
        tcg_gen_gvec_dup_imm(MO_64, dofs, vsz, vsz, 0);
        write_vec_element(s, t0, a->rd, 0, a->esz + 1);
    }
    return true;
}

TRANS(SQDMULL_si, do_scalar_muladd_widening_idx, a,
      a->esz == MO_16 ? gen_sqdmull_h : gen_sqdmull_s, false)
TRANS(SQDMLAL_si, do_scalar_muladd_widening_idx, a,
      a->esz == MO_16 ? gen_sqdmlal_h : gen_sqdmlal_s, true)
TRANS(SQDMLSL_si, do_scalar_muladd_widening_idx, a,
      a->esz == MO_16 ? gen_sqdmlsl_h : gen_sqdmlsl_s, true)

static bool do_fp3_vector_idx(DisasContext *s, arg_qrrx_e *a,
                              gen_helper_gvec_3_ptr * const fns[3])
{
    MemOp esz = a->esz;

    switch (esz) {
    case MO_64:
        if (!a->q) {
            return false;
        }
        break;
    case MO_32:
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        break;
    default:
        g_assert_not_reached();
    }
    if (fp_access_check(s)) {
        gen_gvec_op3_fpst(s, a->q, a->rd, a->rn, a->rm,
                          esz == MO_16, a->idx, fns[esz - 1]);
    }
    return true;
}

static gen_helper_gvec_3_ptr * const f_vector_idx_fmul[3] = {
    gen_helper_gvec_fmul_idx_h,
    gen_helper_gvec_fmul_idx_s,
    gen_helper_gvec_fmul_idx_d,
};
TRANS(FMUL_vi, do_fp3_vector_idx, a, f_vector_idx_fmul)

static gen_helper_gvec_3_ptr * const f_vector_idx_fmulx[3] = {
    gen_helper_gvec_fmulx_idx_h,
    gen_helper_gvec_fmulx_idx_s,
    gen_helper_gvec_fmulx_idx_d,
};
TRANS(FMULX_vi, do_fp3_vector_idx, a, f_vector_idx_fmulx)

static bool do_fmla_vector_idx(DisasContext *s, arg_qrrx_e *a, bool neg)
{
    static gen_helper_gvec_4_ptr * const fns[3] = {
        gen_helper_gvec_fmla_idx_h,
        gen_helper_gvec_fmla_idx_s,
        gen_helper_gvec_fmla_idx_d,
    };
    MemOp esz = a->esz;

    switch (esz) {
    case MO_64:
        if (!a->q) {
            return false;
        }
        break;
    case MO_32:
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        break;
    default:
        g_assert_not_reached();
    }
    if (fp_access_check(s)) {
        gen_gvec_op4_fpst(s, a->q, a->rd, a->rn, a->rm, a->rd,
                          esz == MO_16, (a->idx << 1) | neg,
                          fns[esz - 1]);
    }
    return true;
}

TRANS(FMLA_vi, do_fmla_vector_idx, a, false)
TRANS(FMLS_vi, do_fmla_vector_idx, a, true)

static bool do_fmlal_idx(DisasContext *s, arg_qrrx_e *a, bool is_s, bool is_2)
{
    if (fp_access_check(s)) {
        int data = (a->idx << 2) | (is_2 << 1) | is_s;
        tcg_gen_gvec_3_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm), tcg_env,
                           a->q ? 16 : 8, vec_full_reg_size(s),
                           data, gen_helper_gvec_fmlal_idx_a64);
    }
    return true;
}

TRANS_FEAT(FMLAL_vi, aa64_fhm, do_fmlal_idx, a, false, false)
TRANS_FEAT(FMLSL_vi, aa64_fhm, do_fmlal_idx, a, true, false)
TRANS_FEAT(FMLAL2_vi, aa64_fhm, do_fmlal_idx, a, false, true)
TRANS_FEAT(FMLSL2_vi, aa64_fhm, do_fmlal_idx, a, true, true)

static bool do_int3_vector_idx(DisasContext *s, arg_qrrx_e *a,
                               gen_helper_gvec_3 * const fns[2])
{
    assert(a->esz == MO_16 || a->esz == MO_32);
    if (fp_access_check(s)) {
        gen_gvec_op3_ool(s, a->q, a->rd, a->rn, a->rm, a->idx, fns[a->esz - 1]);
    }
    return true;
}

static gen_helper_gvec_3 * const f_vector_idx_mul[2] = {
    gen_helper_gvec_mul_idx_h,
    gen_helper_gvec_mul_idx_s,
};
TRANS(MUL_vi, do_int3_vector_idx, a, f_vector_idx_mul)

static bool do_mla_vector_idx(DisasContext *s, arg_qrrx_e *a, bool sub)
{
    static gen_helper_gvec_4 * const fns[2][2] = {
        { gen_helper_gvec_mla_idx_h, gen_helper_gvec_mls_idx_h },
        { gen_helper_gvec_mla_idx_s, gen_helper_gvec_mls_idx_s },
    };

    assert(a->esz == MO_16 || a->esz == MO_32);
    if (fp_access_check(s)) {
        gen_gvec_op4_ool(s, a->q, a->rd, a->rn, a->rm, a->rd,
                         a->idx, fns[a->esz - 1][sub]);
    }
    return true;
}

TRANS(MLA_vi, do_mla_vector_idx, a, false)
TRANS(MLS_vi, do_mla_vector_idx, a, true)

static bool do_int3_qc_vector_idx(DisasContext *s, arg_qrrx_e *a,
                                  gen_helper_gvec_4 * const fns[2])
{
    assert(a->esz == MO_16 || a->esz == MO_32);
    if (fp_access_check(s)) {
        tcg_gen_gvec_4_ool(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rn),
                           vec_full_reg_offset(s, a->rm),
                           offsetof(CPUARMState, vfp.qc),
                           a->q ? 16 : 8, vec_full_reg_size(s),
                           a->idx, fns[a->esz - 1]);
    }
    return true;
}

static gen_helper_gvec_4 * const f_vector_idx_sqdmulh[2] = {
    gen_helper_neon_sqdmulh_idx_h,
    gen_helper_neon_sqdmulh_idx_s,
};
TRANS(SQDMULH_vi, do_int3_qc_vector_idx, a, f_vector_idx_sqdmulh)

static gen_helper_gvec_4 * const f_vector_idx_sqrdmulh[2] = {
    gen_helper_neon_sqrdmulh_idx_h,
    gen_helper_neon_sqrdmulh_idx_s,
};
TRANS(SQRDMULH_vi, do_int3_qc_vector_idx, a, f_vector_idx_sqrdmulh)

static gen_helper_gvec_4 * const f_vector_idx_sqrdmlah[2] = {
    gen_helper_neon_sqrdmlah_idx_h,
    gen_helper_neon_sqrdmlah_idx_s,
};
TRANS_FEAT(SQRDMLAH_vi, aa64_rdm, do_int3_qc_vector_idx, a,
           f_vector_idx_sqrdmlah)

static gen_helper_gvec_4 * const f_vector_idx_sqrdmlsh[2] = {
    gen_helper_neon_sqrdmlsh_idx_h,
    gen_helper_neon_sqrdmlsh_idx_s,
};
TRANS_FEAT(SQRDMLSH_vi, aa64_rdm, do_int3_qc_vector_idx, a,
           f_vector_idx_sqrdmlsh)

static bool do_dot_vector_idx(DisasContext *s, arg_qrrx_e *a,
                              gen_helper_gvec_4 *fn)
{
    if (fp_access_check(s)) {
        gen_gvec_op4_ool(s, a->q, a->rd, a->rn, a->rm, a->rd, a->idx, fn);
    }
    return true;
}

TRANS_FEAT(SDOT_vi, aa64_dp, do_dot_vector_idx, a, gen_helper_gvec_sdot_idx_b)
TRANS_FEAT(UDOT_vi, aa64_dp, do_dot_vector_idx, a, gen_helper_gvec_udot_idx_b)
TRANS_FEAT(SUDOT_vi, aa64_i8mm, do_dot_vector_idx, a,
           gen_helper_gvec_sudot_idx_b)
TRANS_FEAT(USDOT_vi, aa64_i8mm, do_dot_vector_idx, a,
           gen_helper_gvec_usdot_idx_b)
TRANS_FEAT(BFDOT_vi, aa64_bf16, do_dot_vector_idx, a,
           gen_helper_gvec_bfdot_idx)

static bool trans_BFMLAL_vi(DisasContext *s, arg_qrrx_e *a)
{
    if (!dc_isar_feature(aa64_bf16, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        /* Q bit selects BFMLALB vs BFMLALT. */
        gen_gvec_op4_fpst(s, true, a->rd, a->rn, a->rm, a->rd, 0,
                          (a->idx << 1) | a->q,
                          gen_helper_gvec_bfmlal_idx);
    }
    return true;
}

static bool trans_FCMLA_vi(DisasContext *s, arg_FCMLA_vi *a)
{
    gen_helper_gvec_4_ptr *fn;

    if (!dc_isar_feature(aa64_fcma, s)) {
        return false;
    }
    switch (a->esz) {
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        fn = gen_helper_gvec_fcmlah_idx;
        break;
    case MO_32:
        fn = gen_helper_gvec_fcmlas_idx;
        break;
    default:
        g_assert_not_reached();
    }
    if (fp_access_check(s)) {
        gen_gvec_op4_fpst(s, a->q, a->rd, a->rn, a->rm, a->rd,
                          a->esz == MO_16, (a->idx << 2) | a->rot, fn);
    }
    return true;
}

/*
 * Advanced SIMD scalar pairwise
 */

static bool do_fp3_scalar_pair(DisasContext *s, arg_rr_e *a, const FPScalar *f)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t0 = tcg_temp_new_i64();
            TCGv_i64 t1 = tcg_temp_new_i64();

            read_vec_element(s, t0, a->rn, 0, MO_64);
            read_vec_element(s, t1, a->rn, 1, MO_64);
            f->gen_d(t0, t0, t1, fpstatus_ptr(FPST_FPCR));
            write_fp_dreg(s, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();

            read_vec_element_i32(s, t0, a->rn, 0, MO_32);
            read_vec_element_i32(s, t1, a->rn, 1, MO_32);
            f->gen_s(t0, t0, t1, fpstatus_ptr(FPST_FPCR));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();

            read_vec_element_i32(s, t0, a->rn, 0, MO_16);
            read_vec_element_i32(s, t1, a->rn, 1, MO_16);
            f->gen_h(t0, t0, t1, fpstatus_ptr(FPST_FPCR_F16));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

TRANS(FADDP_s, do_fp3_scalar_pair, a, &f_scalar_fadd)
TRANS(FMAXP_s, do_fp3_scalar_pair, a, &f_scalar_fmax)
TRANS(FMINP_s, do_fp3_scalar_pair, a, &f_scalar_fmin)
TRANS(FMAXNMP_s, do_fp3_scalar_pair, a, &f_scalar_fmaxnm)
TRANS(FMINNMP_s, do_fp3_scalar_pair, a, &f_scalar_fminnm)

static bool trans_ADDP_s(DisasContext *s, arg_rr_e *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();

        read_vec_element(s, t0, a->rn, 0, MO_64);
        read_vec_element(s, t1, a->rn, 1, MO_64);
        tcg_gen_add_i64(t0, t0, t1);
        write_fp_dreg(s, a->rd, t0);
    }
    return true;
}

/*
 * Floating-point conditional select
 */

static bool trans_FCSEL(DisasContext *s, arg_FCSEL *a)
{
    TCGv_i64 t_true, t_false;
    DisasCompare64 c;

    switch (a->esz) {
    case MO_32:
    case MO_64:
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        break;
    default:
        return false;
    }

    if (!fp_access_check(s)) {
        return true;
    }

    /* Zero extend sreg & hreg inputs to 64 bits now.  */
    t_true = tcg_temp_new_i64();
    t_false = tcg_temp_new_i64();
    read_vec_element(s, t_true, a->rn, 0, a->esz);
    read_vec_element(s, t_false, a->rm, 0, a->esz);

    a64_test_cc(&c, a->cond);
    tcg_gen_movcond_i64(c.cond, t_true, c.value, tcg_constant_i64(0),
                        t_true, t_false);

    /*
     * Note that sregs & hregs write back zeros to the high bits,
     * and we've already done the zero-extension.
     */
    write_fp_dreg(s, a->rd, t_true);
    return true;
}

/*
 * Floating-point data-processing (3 source)
 */

static bool do_fmadd(DisasContext *s, arg_rrrr_e *a, bool neg_a, bool neg_n)
{
    TCGv_ptr fpst;

    /*
     * These are fused multiply-add.  Note that doing the negations here
     * as separate steps is correct: an input NaN should come out with
     * its sign bit flipped if it is a negated-input.
     */
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 tn = read_fp_dreg(s, a->rn);
            TCGv_i64 tm = read_fp_dreg(s, a->rm);
            TCGv_i64 ta = read_fp_dreg(s, a->ra);

            if (neg_a) {
                gen_vfp_negd(ta, ta);
            }
            if (neg_n) {
                gen_vfp_negd(tn, tn);
            }
            fpst = fpstatus_ptr(FPST_FPCR);
            gen_helper_vfp_muladdd(ta, tn, tm, ta, fpst);
            write_fp_dreg(s, a->rd, ta);
        }
        break;

    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 tn = read_fp_sreg(s, a->rn);
            TCGv_i32 tm = read_fp_sreg(s, a->rm);
            TCGv_i32 ta = read_fp_sreg(s, a->ra);

            if (neg_a) {
                gen_vfp_negs(ta, ta);
            }
            if (neg_n) {
                gen_vfp_negs(tn, tn);
            }
            fpst = fpstatus_ptr(FPST_FPCR);
            gen_helper_vfp_muladds(ta, tn, tm, ta, fpst);
            write_fp_sreg(s, a->rd, ta);
        }
        break;

    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 tn = read_fp_hreg(s, a->rn);
            TCGv_i32 tm = read_fp_hreg(s, a->rm);
            TCGv_i32 ta = read_fp_hreg(s, a->ra);

            if (neg_a) {
                gen_vfp_negh(ta, ta);
            }
            if (neg_n) {
                gen_vfp_negh(tn, tn);
            }
            fpst = fpstatus_ptr(FPST_FPCR_F16);
            gen_helper_advsimd_muladdh(ta, tn, tm, ta, fpst);
            write_fp_sreg(s, a->rd, ta);
        }
        break;

    default:
        return false;
    }
    return true;
}

TRANS(FMADD, do_fmadd, a, false, false)
TRANS(FNMADD, do_fmadd, a, true, true)
TRANS(FMSUB, do_fmadd, a, false, true)
TRANS(FNMSUB, do_fmadd, a, true, false)

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
        shift_reg(dst, src, sf, shift_type, tcg_constant_i64(shift_i));
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
        tcg_y = tcg_temp_new_i64();
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

    /* Load the arguments for the new comparison.  */
    if (is_imm) {
        tcg_y = tcg_temp_new_i64();
        tcg_gen_movi_i64(tcg_y, y);
    } else {
        tcg_y = cpu_reg(s, y);
    }
    tcg_rn = cpu_reg(s, rn);

    /* Set the flags for the new comparison.  */
    tcg_tmp = tcg_temp_new_i64();
    if (op) {
        gen_sub_CC(sf, tcg_tmp, tcg_rn, tcg_y);
    } else {
        gen_add_CC(sf, tcg_tmp, tcg_rn, tcg_y);
    }

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
    zero = tcg_constant_i64(0);

    if (rn == 31 && rm == 31 && (else_inc ^ else_inv)) {
        /* CSET & CSETM.  */
        if (else_inv) {
            tcg_gen_negsetcond_i64(tcg_invert_cond(c.cond),
                                   tcg_rd, c.value, zero);
        } else {
            tcg_gen_setcond_i64(tcg_invert_cond(c.cond),
                                tcg_rd, c.value, zero);
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
    TCGv_i64 tcg_rn = cpu_reg(s, rn);

    if (sf) {
        tcg_gen_bswap64_i64(tcg_rd, tcg_rn);
        tcg_gen_rotri_i64(tcg_rd, tcg_rd, 32);
    } else {
        tcg_gen_bswap32_i64(tcg_rd, tcg_rn, TCG_BSWAP_OZ);
    }
}

/* REV16 (opcode==1) */
static void handle_rev16(DisasContext *s, unsigned int sf,
                         unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd = cpu_reg(s, rd);
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();
    TCGv_i64 tcg_rn = read_cpu_reg(s, rn, sf);
    TCGv_i64 mask = tcg_constant_i64(sf ? 0x00ff00ff00ff00ffull : 0x00ff00ff);

    tcg_gen_shri_i64(tcg_tmp, tcg_rn, 8);
    tcg_gen_and_i64(tcg_rd, tcg_rn, mask);
    tcg_gen_and_i64(tcg_tmp, tcg_tmp, mask);
    tcg_gen_shli_i64(tcg_rd, tcg_rd, 8);
    tcg_gen_or_i64(tcg_rd, tcg_rd, tcg_tmp);
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
            gen_helper_pacia(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x01): /* PACIB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacib(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x02): /* PACDA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacda(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x03): /* PACDB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacdb(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x04): /* AUTIA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autia(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x05): /* AUTIB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autib(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x06): /* AUTDA */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autda(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x07): /* AUTDB */
        if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autdb(tcg_rd, tcg_env, tcg_rd, cpu_reg_sp(s, rn));
        } else if (!dc_isar_feature(aa64_pauth, s)) {
            goto do_unallocated;
        }
        break;
    case MAP(1, 0x01, 0x08): /* PACIZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacia(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x09): /* PACIZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacib(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x0a): /* PACDZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacda(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x0b): /* PACDZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_pacdb(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x0c): /* AUTIZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autia(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x0d): /* AUTIZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autib(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x0e): /* AUTDZA */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autda(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x0f): /* AUTDZB */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_autdb(tcg_rd, tcg_env, tcg_rd, tcg_constant_i64(0));
        }
        break;
    case MAP(1, 0x01, 0x10): /* XPACI */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_xpaci(tcg_rd, tcg_env, tcg_rd);
        }
        break;
    case MAP(1, 0x01, 0x11): /* XPACD */
        if (!dc_isar_feature(aa64_pauth, s) || rn != 31) {
            goto do_unallocated;
        } else if (s->pauth_active) {
            tcg_rd = cpu_reg(s, rd);
            gen_helper_xpacd(tcg_rd, tcg_env, tcg_rd);
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
        tcg_n = tcg_temp_new_i64();
        tcg_m = tcg_temp_new_i64();
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
        tcg_val = tcg_temp_new_i64();
        tcg_gen_andi_i64(tcg_val, cpu_reg(s, rm), mask);
    }

    tcg_acc = cpu_reg(s, rn);
    tcg_bytes = tcg_constant_i32(1 << sz);

    if (crc32c) {
        gen_helper_crc32c_64(cpu_reg(s, rd), tcg_acc, tcg_val, tcg_bytes);
    } else {
        gen_helper_crc32_64(cpu_reg(s, rd), tcg_acc, tcg_val, tcg_bytes);
    }
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
        if (s->ata[0]) {
            gen_helper_irg(cpu_reg_sp(s, rd), tcg_env,
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
            TCGv_i64 t = tcg_temp_new_i64();

            tcg_gen_extract_i64(t, cpu_reg_sp(s, rn), 56, 4);
            tcg_gen_shl_i64(t, tcg_constant_i64(1), t);
            tcg_gen_or_i64(cpu_reg(s, rd), cpu_reg(s, rm), t);
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
        gen_helper_pacga(cpu_reg(s, rd), tcg_env,
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
            tcg_vm = tcg_constant_i64(0);
        } else {
            tcg_vm = read_fp_dreg(s, rm);
        }
        if (signal_all_nans) {
            gen_helper_vfp_cmped_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
        } else {
            gen_helper_vfp_cmpd_a64(tcg_flags, tcg_vn, tcg_vm, fpst);
        }
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
    }

    gen_set_nzcv(tcg_flags);
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
        gen_set_nzcv(tcg_constant_i64(nzcv << 28));
        tcg_gen_br(label_continue);
        gen_set_label(label_match);
    }

    handle_fp_compare(s, size, rn, rm, false, op);

    if (cond < 0x0e) {
        gen_set_label(label_continue);
    }
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
        gen_vfp_absh(tcg_res, tcg_op);
        break;
    case 0x2: /* FNEG */
        gen_vfp_negh(tcg_res, tcg_op);
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
        TCGv_i32 tcg_rmode;

        fpst = fpstatus_ptr(FPST_FPCR_F16);
        tcg_rmode = gen_set_rmode(opcode & 7, fpst);
        gen_helper_advsimd_rinth(tcg_res, tcg_op, fpst);
        gen_restore_rmode(tcg_rmode, fpst);
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
        g_assert_not_reached();
    }

    write_fp_sreg(s, rd, tcg_res);
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
        gen_vfp_abss(tcg_res, tcg_op);
        goto done;
    case 0x2: /* FNEG */
        gen_vfp_negs(tcg_res, tcg_op);
        goto done;
    case 0x3: /* FSQRT */
        gen_helper_vfp_sqrts(tcg_res, tcg_op, tcg_env);
        goto done;
    case 0x6: /* BFCVT */
        gen_fpst = gen_helper_bfcvt;
        break;
    case 0x8: /* FRINTN */
    case 0x9: /* FRINTP */
    case 0xa: /* FRINTM */
    case 0xb: /* FRINTZ */
    case 0xc: /* FRINTA */
        rmode = opcode & 7;
        gen_fpst = gen_helper_rints;
        break;
    case 0xe: /* FRINTX */
        gen_fpst = gen_helper_rints_exact;
        break;
    case 0xf: /* FRINTI */
        gen_fpst = gen_helper_rints;
        break;
    case 0x10: /* FRINT32Z */
        rmode = FPROUNDING_ZERO;
        gen_fpst = gen_helper_frint32_s;
        break;
    case 0x11: /* FRINT32X */
        gen_fpst = gen_helper_frint32_s;
        break;
    case 0x12: /* FRINT64Z */
        rmode = FPROUNDING_ZERO;
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
        TCGv_i32 tcg_rmode = gen_set_rmode(rmode, fpst);
        gen_fpst(tcg_res, tcg_op, fpst);
        gen_restore_rmode(tcg_rmode, fpst);
    } else {
        gen_fpst(tcg_res, tcg_op, fpst);
    }

 done:
    write_fp_sreg(s, rd, tcg_res);
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
        gen_vfp_absd(tcg_res, tcg_op);
        goto done;
    case 0x2: /* FNEG */
        gen_vfp_negd(tcg_res, tcg_op);
        goto done;
    case 0x3: /* FSQRT */
        gen_helper_vfp_sqrtd(tcg_res, tcg_op, tcg_env);
        goto done;
    case 0x8: /* FRINTN */
    case 0x9: /* FRINTP */
    case 0xa: /* FRINTM */
    case 0xb: /* FRINTZ */
    case 0xc: /* FRINTA */
        rmode = opcode & 7;
        gen_fpst = gen_helper_rintd;
        break;
    case 0xe: /* FRINTX */
        gen_fpst = gen_helper_rintd_exact;
        break;
    case 0xf: /* FRINTI */
        gen_fpst = gen_helper_rintd;
        break;
    case 0x10: /* FRINT32Z */
        rmode = FPROUNDING_ZERO;
        gen_fpst = gen_helper_frint32_d;
        break;
    case 0x11: /* FRINT32X */
        gen_fpst = gen_helper_frint32_d;
        break;
    case 0x12: /* FRINT64Z */
        rmode = FPROUNDING_ZERO;
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
        TCGv_i32 tcg_rmode = gen_set_rmode(rmode, fpst);
        gen_fpst(tcg_res, tcg_op, fpst);
        gen_restore_rmode(tcg_rmode, fpst);
    } else {
        gen_fpst(tcg_res, tcg_op, fpst);
    }

 done:
    write_fp_dreg(s, rd, tcg_res);
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
            gen_helper_vfp_fcvtds(tcg_rd, tcg_rn, tcg_env);
            write_fp_dreg(s, rd, tcg_rd);
        } else {
            /* Single to half */
            TCGv_i32 tcg_rd = tcg_temp_new_i32();
            TCGv_i32 ahp = get_ahp_flag();
            TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);

            gen_helper_vfp_fcvt_f32_to_f16(tcg_rd, tcg_rn, fpst, ahp);
            /* write_fp_sreg is OK here because top half of tcg_rd is zero */
            write_fp_sreg(s, rd, tcg_rd);
        }
        break;
    }
    case 0x1:
    {
        TCGv_i64 tcg_rn = read_fp_dreg(s, rn);
        TCGv_i32 tcg_rd = tcg_temp_new_i32();
        if (dtype == 0) {
            /* Double to single */
            gen_helper_vfp_fcvtsd(tcg_rd, tcg_rn, tcg_env);
        } else {
            TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);
            TCGv_i32 ahp = get_ahp_flag();
            /* Double to half */
            gen_helper_vfp_fcvt_f64_to_f16(tcg_rd, tcg_rn, fpst, ahp);
            /* write_fp_sreg is OK here because top half of tcg_rd is zero */
        }
        write_fp_sreg(s, rd, tcg_rd);
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
        } else {
            /* Half to double */
            TCGv_i64 tcg_rd = tcg_temp_new_i64();
            gen_helper_vfp_fcvt_f16_to_f64(tcg_rd, tcg_rn, tcg_fpst, tcg_ahp);
            write_fp_dreg(s, rd, tcg_rd);
        }
        break;
    }
    default:
        g_assert_not_reached();
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
        goto do_unallocated;
    }

    switch (opcode) {
    case 0x4: case 0x5: case 0x7:
    {
        /* FCVT between half, single and double precision */
        int dtype = extract32(opcode, 0, 2);
        if (type == 2 || dtype == type) {
            goto do_unallocated;
        }
        if (!fp_access_check(s)) {
            return;
        }

        handle_fp_fcvt(s, opcode, rd, rn, dtype, type);
        break;
    }

    case 0x10 ... 0x13: /* FRINT{32,64}{X,Z} */
        if (type > 1 || !dc_isar_feature(aa64_frint, s)) {
            goto do_unallocated;
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
                goto do_unallocated;
            }

            if (!fp_access_check(s)) {
                return;
            }
            handle_fp_1src_half(s, opcode, rd, rn);
            break;
        default:
            goto do_unallocated;
        }
        break;

    case 0x6:
        switch (type) {
        case 1: /* BFCVT */
            if (!dc_isar_feature(aa64_bf16, s)) {
                goto do_unallocated;
            }
            if (!fp_access_check(s)) {
                return;
            }
            handle_fp_1src_single(s, opcode, rd, rn);
            break;
        default:
            goto do_unallocated;
        }
        break;

    default:
    do_unallocated:
        unallocated_encoding(s);
        break;
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
    write_fp_dreg(s, rd, tcg_constant_i64(imm));
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

    tcg_shift = tcg_constant_i32(64 - scale);

    if (itof) {
        TCGv_i64 tcg_int = cpu_reg(s, rn);
        if (!sf) {
            TCGv_i64 tcg_extend = tcg_temp_new_i64();

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

        tcg_rmode = gen_set_rmode(rmode, tcg_fpstatus);

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
            }
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
            }
            break;

        default:
            g_assert_not_reached();
        }

        gen_restore_rmode(tcg_rmode, tcg_fpstatus);
    }
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
            break;
        case 1:
            /* 64 bit */
            write_fp_dreg(s, rd, tcg_rn);
            break;
        case 2:
            /* 64 bit to top half. */
            tcg_gen_st_i64(tcg_rn, tcg_env, fp_reg_hi_offset(s, rd));
            clear_vec_high(s, true, rd);
            break;
        case 3:
            /* 16 bit */
            tmp = tcg_temp_new_i64();
            tcg_gen_ext16u_i64(tmp, tcg_rn);
            write_fp_dreg(s, rd, tmp);
            break;
        default:
            g_assert_not_reached();
        }
    } else {
        TCGv_i64 tcg_rd = cpu_reg(s, rd);

        switch (type) {
        case 0:
            /* 32 bit */
            tcg_gen_ld32u_i64(tcg_rd, tcg_env, fp_reg_offset(s, rn, MO_32));
            break;
        case 1:
            /* 64 bit */
            tcg_gen_ld_i64(tcg_rd, tcg_env, fp_reg_offset(s, rn, MO_64));
            break;
        case 2:
            /* 64 bits from top half */
            tcg_gen_ld_i64(tcg_rd, tcg_env, fp_reg_hi_offset(s, rn));
            break;
        case 3:
            /* 16 bit */
            tcg_gen_ld16u_i64(tcg_rd, tcg_env, fp_reg_offset(s, rn, MO_16));
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

    tcg_gen_ext32u_i64(cpu_reg(s, rd), t);
    tcg_gen_extrh_i64_i32(cpu_ZF, t);
    tcg_gen_movi_i32(cpu_CF, 0);
    tcg_gen_movi_i32(cpu_NF, 0);
    tcg_gen_movi_i32(cpu_VF, 0);
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
        unallocated_encoding(s); /* in decodetree */
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
            unallocated_encoding(s); /* in decodetree */
            break;
        case 3:
            /* Floating point conditional select */
            unallocated_encoding(s); /* in decodetree */
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
        }
    }

    write_vec_element(s, tcg_resl, rd, 0, MO_64);
    if (is_q) {
        write_vec_element(s, tcg_resh, rd, 1, MO_64);
    }
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
    int is_tbx = extract32(insn, 12, 1);
    int len = (extract32(insn, 13, 2) + 1) * 16;

    if (op2 != 0) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rm), tcg_env,
                       is_q ? 16 : 8, vec_full_reg_size(s),
                       (len << 6) | (is_tbx << 5) | rn,
                       gen_helper_simd_tblx);
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
    int i;
    int datasize = is_q ? 128 : 64;
    int elements = datasize / esize;
    TCGv_i64 tcg_res[2], tcg_ele;

    if (opcode == 0 || (size == 3 && !is_q)) {
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    tcg_res[0] = tcg_temp_new_i64();
    tcg_res[1] = is_q ? tcg_temp_new_i64() : NULL;
    tcg_ele = tcg_temp_new_i64();

    for (i = 0; i < elements; i++) {
        int o, w;

        switch (opcode) {
        case 1: /* UZP1/2 */
        {
            int midpoint = elements / 2;
            if (i < midpoint) {
                read_vec_element(s, tcg_ele, rn, 2 * i + part, size);
            } else {
                read_vec_element(s, tcg_ele, rm,
                                 2 * (i - midpoint) + part, size);
            }
            break;
        }
        case 2: /* TRN1/2 */
            if (i & 1) {
                read_vec_element(s, tcg_ele, rm, (i & ~1) + part, size);
            } else {
                read_vec_element(s, tcg_ele, rn, (i & ~1) + part, size);
            }
            break;
        case 3: /* ZIP1/2 */
        {
            int base = part * elements / 2;
            if (i & 1) {
                read_vec_element(s, tcg_ele, rm, base + (i >> 1), size);
            } else {
                read_vec_element(s, tcg_ele, rn, base + (i >> 1), size);
            }
            break;
        }
        default:
            g_assert_not_reached();
        }

        w = (i * esize) / 64;
        o = (i * esize) % 64;
        if (o == 0) {
            tcg_gen_mov_i64(tcg_res[w], tcg_ele);
        } else {
            tcg_gen_shli_i64(tcg_ele, tcg_ele, o);
            tcg_gen_or_i64(tcg_res[w], tcg_res[w], tcg_ele);
        }
    }

    for (i = 0; i <= is_q; ++i) {
        write_vec_element(s, tcg_res[i], rd, i, MO_64);
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
    }

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
    int o2 = extract32(insn, 11, 1);
    uint64_t abcdefgh = extract32(insn, 5, 5) | (extract32(insn, 16, 3) << 5);
    bool is_neg = extract32(insn, 29, 1);
    bool is_q = extract32(insn, 30, 1);
    uint64_t imm = 0;

    if (o2) {
        if (cmode != 0xf || is_neg) {
            unallocated_encoding(s);
            return;
        }
        /* FMOV (vector, immediate) - half-precision */
        if (!dc_isar_feature(aa64_fp16, s)) {
            unallocated_encoding(s);
            return;
        }
        imm = vfp_expand_imm(MO_16, abcdefgh);
        /* now duplicate across the lanes */
        imm = dup_const(MO_16, imm);
    } else {
        if (cmode == 0xf && is_neg && !is_q) {
            unallocated_encoding(s);
            return;
        }
        imm = asimd_imm_const(abcdefgh, cmode, is_neg);
    }

    if (!fp_access_check(s)) {
        return;
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
            TCGv_i64 tcg_zero = tcg_constant_i64(0);
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
        tcg_round = tcg_constant_i64(1ULL << (shift - 1));
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
    tcg_final = tcg_temp_new_i64();

    if (round) {
        tcg_round = tcg_constant_i64(1ULL << (shift - 1));
    } else {
        tcg_round = NULL;
    }

    for (i = 0; i < elements; i++) {
        read_vec_element(s, tcg_rn, rn, i, ldop);
        handle_shri_with_rndacc(tcg_rd, tcg_rn, tcg_round,
                                false, is_u_shift, size+1, shift);
        narrowfn(tcg_rd_narrowed, tcg_env, tcg_rd);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_rd_narrowed);
        if (i == 0) {
            tcg_gen_extract_i64(tcg_final, tcg_rd, 0, esize);
        } else {
            tcg_gen_deposit_i64(tcg_final, tcg_final, tcg_rd, esize * i, esize);
        }
    }

    if (!is_q) {
        write_vec_element(s, tcg_final, rd, 0, MO_64);
    } else {
        write_vec_element(s, tcg_final, rd, 1, MO_64);
    }
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
        TCGv_i64 tcg_shift = tcg_constant_i64(shift);
        static NeonGenTwo64OpEnvFn * const fns[2][2] = {
            { gen_helper_neon_qshl_s64, gen_helper_neon_qshlu_s64 },
            { NULL, gen_helper_neon_qshl_u64 },
        };
        NeonGenTwo64OpEnvFn *genfn = fns[src_unsigned][dst_unsigned];
        int maxpass = is_q ? 2 : 1;

        for (pass = 0; pass < maxpass; pass++) {
            TCGv_i64 tcg_op = tcg_temp_new_i64();

            read_vec_element(s, tcg_op, rn, pass, MO_64);
            genfn(tcg_op, tcg_env, tcg_op, tcg_shift);
            write_vec_element(s, tcg_op, rd, pass, MO_64);
        }
        clear_vec_high(s, is_q, rd);
    } else {
        TCGv_i32 tcg_shift = tcg_constant_i32(shift);
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
            genfn(tcg_op, tcg_env, tcg_op, tcg_shift);
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
        }

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
        tcg_shift = tcg_constant_i32(fracbits);
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

    tcg_fpstatus = fpstatus_ptr(size == MO_16 ? FPST_FPCR_F16 : FPST_FPCR);
    tcg_rmode = gen_set_rmode(FPROUNDING_ZERO, tcg_fpstatus);
    fracbits = (16 << size) - immhb;
    tcg_shift = tcg_constant_i32(fracbits);

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
                if (size == MO_16 && !is_u) {
                    tcg_gen_ext16u_i32(tcg_op, tcg_op);
                }
                write_fp_sreg(s, rd, tcg_op);
            } else {
                write_vec_element_i32(s, tcg_op, rd, pass, size);
            }
        }
        if (!is_scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }

    gen_restore_rmode(tcg_rmode, tcg_fpstatus);
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
            gen_helper_neon_qneg_s64(tcg_rd, tcg_env, tcg_rn);
        } else {
            gen_helper_neon_qabs_s64(tcg_rd, tcg_env, tcg_rn);
        }
        break;
    case 0xa: /* CMLT */
        cond = TCG_COND_LT;
    do_cmop:
        /* 64 bit integer comparison against zero, result is test ? -1 : 0. */
        tcg_gen_negsetcond_i64(cond, tcg_rd, tcg_rn, tcg_constant_i64(0));
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
        gen_vfp_absd(tcg_rd, tcg_rn);
        break;
    case 0x6f: /* FNEG */
        gen_vfp_negd(tcg_rd, tcg_rn);
        break;
    case 0x7f: /* FSQRT */
        gen_helper_vfp_sqrtd(tcg_rd, tcg_rn, tcg_env);
        break;
    case 0x1a: /* FCVTNS */
    case 0x1b: /* FCVTMS */
    case 0x1c: /* FCVTAS */
    case 0x3a: /* FCVTPS */
    case 0x3b: /* FCVTZS */
        gen_helper_vfp_tosqd(tcg_rd, tcg_rn, tcg_constant_i32(0), tcg_fpstatus);
        break;
    case 0x5a: /* FCVTNU */
    case 0x5b: /* FCVTMU */
    case 0x5c: /* FCVTAU */
    case 0x7a: /* FCVTPU */
    case 0x7b: /* FCVTZU */
        gen_helper_vfp_touqd(tcg_rd, tcg_rn, tcg_constant_i32(0), tcg_fpstatus);
        break;
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
        TCGv_i64 tcg_zero = tcg_constant_i64(0);
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

        clear_vec_high(s, !is_scalar, rd);
    } else {
        TCGv_i32 tcg_op = tcg_temp_new_i32();
        TCGv_i32 tcg_zero = tcg_constant_i32(0);
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

        if (!is_scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }
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
        if (!is_scalar) {
            clear_vec_high(s, is_q, rd);
        }
    }
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
        tcg_res[1] = tcg_constant_i32(0);
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
                gen_helper_vfp_fcvtsd(tcg_res[pass], tcg_op, tcg_env);
            } else {
                TCGv_i32 tcg_lo = tcg_temp_new_i32();
                TCGv_i32 tcg_hi = tcg_temp_new_i32();
                TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);
                TCGv_i32 ahp = get_ahp_flag();

                tcg_gen_extr_i64_i32(tcg_lo, tcg_hi, tcg_op);
                gen_helper_vfp_fcvt_f32_to_f16(tcg_lo, tcg_lo, fpst, ahp);
                gen_helper_vfp_fcvt_f32_to_f16(tcg_hi, tcg_hi, fpst, ahp);
                tcg_gen_deposit_i32(tcg_res[pass], tcg_lo, tcg_hi, 16, 16);
            }
            break;
        case 0x36: /* BFCVTN, BFCVTN2 */
            {
                TCGv_ptr fpst = fpstatus_ptr(FPST_FPCR);
                gen_helper_bfcvt_pair(tcg_res[pass], tcg_op, fpst);
            }
            break;
        case 0x56:  /* FCVTXN, FCVTXN2 */
            /* 64 bit to 32 bit float conversion
             * with von Neumann rounding (round to odd)
             */
            assert(size == 2);
            gen_helper_fcvtx_f64_to_f32(tcg_res[pass], tcg_op, tcg_env);
            break;
        default:
            g_assert_not_reached();
        }

        if (genfn) {
            genfn(tcg_res[pass], tcg_op);
        } else if (genenvfn) {
            genenvfn(tcg_res[pass], tcg_env, tcg_op);
        }
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element_i32(s, tcg_res[pass], rd, destelt + pass, MO_32);
    }
    clear_vec_high(s, is_q, rd);
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
    case 0x3: /* USQADD / SUQADD */
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (is_fcvt) {
        tcg_fpstatus = fpstatus_ptr(FPST_FPCR);
        tcg_rmode = gen_set_rmode(rmode, tcg_fpstatus);
    } else {
        tcg_fpstatus = NULL;
        tcg_rmode = NULL;
    }

    if (size == 3) {
        TCGv_i64 tcg_rn = read_fp_dreg(s, rn);
        TCGv_i64 tcg_rd = tcg_temp_new_i64();

        handle_2misc_64(s, opcode, u, tcg_rd, tcg_rn, tcg_rmode, tcg_fpstatus);
        write_fp_dreg(s, rd, tcg_rd);
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
            genfn(tcg_rd, tcg_env, tcg_rn);
            break;
        }
        case 0x1a: /* FCVTNS */
        case 0x1b: /* FCVTMS */
        case 0x1c: /* FCVTAS */
        case 0x3a: /* FCVTPS */
        case 0x3b: /* FCVTZS */
            gen_helper_vfp_tosls(tcg_rd, tcg_rn, tcg_constant_i32(0),
                                 tcg_fpstatus);
            break;
        case 0x5a: /* FCVTNU */
        case 0x5b: /* FCVTMU */
        case 0x5c: /* FCVTAU */
        case 0x7a: /* FCVTPU */
        case 0x7b: /* FCVTZU */
            gen_helper_vfp_touls(tcg_rd, tcg_rn, tcg_constant_i32(0),
                                 tcg_fpstatus);
            break;
        default:
            g_assert_not_reached();
        }

        write_fp_sreg(s, rd, tcg_rd);
    }

    if (is_fcvt) {
        gen_restore_rmode(tcg_rmode, tcg_fpstatus);
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
    TCGv_i64 tcg_rn = tcg_temp_new_i64();
    TCGv_i64 tcg_rd = tcg_temp_new_i64();
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
    clear_vec_high(s, true, rd);
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
        tcg_round = tcg_constant_i64(1ULL << (shift - 1));
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
            gen_helper_vfp_fcvtds(tcg_res[pass], tcg_op, tcg_env);
        }
        for (pass = 0; pass < 2; pass++) {
            write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
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
        }
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
                tcg_gen_bswap16_i64(tcg_tmp, tcg_tmp, TCG_BSWAP_IZ);
                break;
            case MO_32:
                tcg_gen_bswap32_i64(tcg_tmp, tcg_tmp, TCG_BSWAP_IZ);
                break;
            case MO_64:
                tcg_gen_bswap64_i64(tcg_tmp, tcg_tmp);
                break;
            default:
                g_assert_not_reached();
            }
            write_vec_element(s, tcg_tmp, rd, i, grp_size);
        }
        clear_vec_high(s, is_q, rd);
    } else {
        int revmask = (1 << grp_size) - 1;
        int esize = 8 << size;
        int elements = dsize / esize;
        TCGv_i64 tcg_rn = tcg_temp_new_i64();
        TCGv_i64 tcg_rd[2];

        for (i = 0; i < 2; i++) {
            tcg_rd[i] = tcg_temp_new_i64();
            tcg_gen_movi_i64(tcg_rd[i], 0);
        }

        for (i = 0; i < elements; i++) {
            int e_rev = (i & 0xf) ^ revmask;
            int w = (e_rev * esize) / 64;
            int o = (e_rev * esize) % 64;

            read_vec_element(s, tcg_rn, rn, i, size);
            tcg_gen_deposit_i64(tcg_rd[w], tcg_rd[w], tcg_rn, o, esize);
        }

        for (i = 0; i < 2; i++) {
            write_vec_element(s, tcg_rd[i], rd, i, MO_64);
        }
        clear_vec_high(s, true, rd);
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
        }
    }
    if (!is_q) {
        tcg_res[1] = tcg_constant_i64(0);
    }
    for (pass = 0; pass < 2; pass++) {
        write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
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
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element(s, tcg_res[pass], rd, pass, MO_64);
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
            rmode = extract32(opcode, 5, 1) | (extract32(opcode, 0, 1) << 1);
            if (size == 3 && !is_q) {
                unallocated_encoding(s);
                return;
            }
            break;
        case 0x5c: /* FCVTAU */
        case 0x1c: /* FCVTAS */
            need_fpstatus = true;
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
        case 0x36: /* BFCVTN, BFCVTN2 */
            if (!dc_isar_feature(aa64_bf16, s) || size != 2) {
                unallocated_encoding(s);
                return;
            }
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
    case 0x3: /* SUQADD, USQADD */
        unallocated_encoding(s);
        return;
    }

    if (!fp_access_check(s)) {
        return;
    }

    if (need_fpstatus || rmode >= 0) {
        tcg_fpstatus = fpstatus_ptr(FPST_FPCR);
    } else {
        tcg_fpstatus = NULL;
    }
    if (rmode >= 0) {
        tcg_rmode = gen_set_rmode(rmode, tcg_fpstatus);
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
                        gen_helper_neon_qneg_s32(tcg_res, tcg_env, tcg_op);
                    } else {
                        gen_helper_neon_qabs_s32(tcg_res, tcg_env, tcg_op);
                    }
                    break;
                case 0x2f: /* FABS */
                    gen_vfp_abss(tcg_res, tcg_op);
                    break;
                case 0x6f: /* FNEG */
                    gen_vfp_negs(tcg_res, tcg_op);
                    break;
                case 0x7f: /* FSQRT */
                    gen_helper_vfp_sqrts(tcg_res, tcg_op, tcg_env);
                    break;
                case 0x1a: /* FCVTNS */
                case 0x1b: /* FCVTMS */
                case 0x1c: /* FCVTAS */
                case 0x3a: /* FCVTPS */
                case 0x3b: /* FCVTZS */
                    gen_helper_vfp_tosls(tcg_res, tcg_op,
                                         tcg_constant_i32(0), tcg_fpstatus);
                    break;
                case 0x5a: /* FCVTNU */
                case 0x5b: /* FCVTMU */
                case 0x5c: /* FCVTAU */
                case 0x7a: /* FCVTPU */
                case 0x7b: /* FCVTZU */
                    gen_helper_vfp_touls(tcg_res, tcg_op,
                                         tcg_constant_i32(0), tcg_fpstatus);
                    break;
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
                    genfn(tcg_res, tcg_env, tcg_op);
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
        }
    }
    clear_vec_high(s, is_q, rd);

    if (tcg_rmode) {
        gen_restore_rmode(tcg_rmode, tcg_fpstatus);
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
    bool need_fpst = true;
    int rmode = -1;

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
        only_in_vector = true;
        rmode = FPROUNDING_TIEEVEN;
        break;
    case 0x19: /* FRINTM */
        only_in_vector = true;
        rmode = FPROUNDING_NEGINF;
        break;
    case 0x38: /* FRINTP */
        only_in_vector = true;
        rmode = FPROUNDING_POSINF;
        break;
    case 0x39: /* FRINTZ */
        only_in_vector = true;
        rmode = FPROUNDING_ZERO;
        break;
    case 0x58: /* FRINTA */
        only_in_vector = true;
        rmode = FPROUNDING_TIEAWAY;
        break;
    case 0x59: /* FRINTX */
    case 0x79: /* FRINTI */
        only_in_vector = true;
        /* current rounding mode */
        break;
    case 0x1a: /* FCVTNS */
        rmode = FPROUNDING_TIEEVEN;
        break;
    case 0x1b: /* FCVTMS */
        rmode = FPROUNDING_NEGINF;
        break;
    case 0x1c: /* FCVTAS */
        rmode = FPROUNDING_TIEAWAY;
        break;
    case 0x3a: /* FCVTPS */
        rmode = FPROUNDING_POSINF;
        break;
    case 0x3b: /* FCVTZS */
        rmode = FPROUNDING_ZERO;
        break;
    case 0x5a: /* FCVTNU */
        rmode = FPROUNDING_TIEEVEN;
        break;
    case 0x5b: /* FCVTMU */
        rmode = FPROUNDING_NEGINF;
        break;
    case 0x5c: /* FCVTAU */
        rmode = FPROUNDING_TIEAWAY;
        break;
    case 0x7a: /* FCVTPU */
        rmode = FPROUNDING_POSINF;
        break;
    case 0x7b: /* FCVTZU */
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
        unallocated_encoding(s);
        return;
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

    if (rmode >= 0 || need_fpst) {
        tcg_fpstatus = fpstatus_ptr(FPST_FPCR_F16);
    }

    if (rmode >= 0) {
        tcg_rmode = gen_set_rmode(rmode, tcg_fpstatus);
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
        }

        clear_vec_high(s, is_q, rd);
    }

    if (tcg_rmode) {
        gen_restore_rmode(tcg_rmode, tcg_fpstatus);
    }
}

/* C3.6 Data processing - SIMD, inc Crypto
 *
 * As the decode gets a little complex we are using a table based
 * approach for this part of the decode.
 */
static const AArch64DecodeTable data_proc_simd[] = {
    /* pattern  ,  mask     ,  fn                        */
    { 0x0e200800, 0x9f3e0c00, disas_simd_two_reg_misc },
    { 0x0e300800, 0x9f3e0c00, disas_simd_across_lanes },
    /* simd_mod_imm decode is a subset of simd_shift_imm, so must precede it */
    { 0x0f000400, 0x9ff80400, disas_simd_mod_imm },
    { 0x0f000400, 0x9f800400, disas_simd_shift_imm },
    { 0x0e000000, 0xbf208c00, disas_simd_tb },
    { 0x0e000800, 0xbf208c00, disas_simd_zip_trn },
    { 0x2e000000, 0xbf208400, disas_simd_ext },
    { 0x5e200800, 0xdf3e0c00, disas_simd_scalar_two_reg_misc },
    { 0x5f000400, 0xdf800400, disas_simd_scalar_shift_imm },
    { 0x0e780800, 0x8f7e0c00, disas_simd_two_reg_misc_fp16 },
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

static bool trans_OK(DisasContext *s, arg_OK *a)
{
    return true;
}

static bool trans_FAIL(DisasContext *s, arg_OK *a)
{
    s->is_nonstreaming = true;
    return true;
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
static void disas_a64_legacy(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 25, 4)) {
    case 0x5:
    case 0xd:      /* Data processing - register */
        disas_data_proc_reg(s, insn);
        break;
    case 0x7:
    case 0xf:      /* Data processing - SIMD and floating point */
        disas_data_proc_simd_fp(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

static void aarch64_tr_init_disas_context(DisasContextBase *dcbase,
                                          CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu_env(cpu);
    ARMCPU *arm_cpu = env_archcpu(env);
    CPUARMTBFlags tb_flags = arm_tbflags_from_tb(dc->base.tb);
    int bound, core_mmu_idx;

    dc->isar = &arm_cpu->isar;
    dc->condjmp = 0;
    dc->pc_save = dc->base.pc_first;
    dc->aarch64 = true;
    dc->thumb = false;
    dc->sctlr_b = 0;
    dc->be_data = EX_TBFLAG_ANY(tb_flags, BE_DATA) ? MO_BE : MO_LE;
    dc->condexec_mask = 0;
    dc->condexec_cond = 0;
    core_mmu_idx = EX_TBFLAG_ANY(tb_flags, MMUIDX);
    dc->mmu_idx = core_to_aa64_mmu_idx(core_mmu_idx);
    dc->tbii = EX_TBFLAG_A64(tb_flags, TBII);
    dc->tbid = EX_TBFLAG_A64(tb_flags, TBID);
    dc->tcma = EX_TBFLAG_A64(tb_flags, TCMA);
    dc->current_el = arm_mmu_idx_to_el(dc->mmu_idx, false);
#if !defined(CONFIG_USER_ONLY)
    dc->user = (dc->current_el == 0);
#endif
    dc->fp_excp_el = EX_TBFLAG_ANY(tb_flags, FPEXC_EL);
    dc->align_mem = EX_TBFLAG_ANY(tb_flags, ALIGN_MEM);
    dc->pstate_il = EX_TBFLAG_ANY(tb_flags, PSTATE__IL);
    dc->fgt_active = EX_TBFLAG_ANY(tb_flags, FGT_ACTIVE);
    dc->fgt_svc = EX_TBFLAG_ANY(tb_flags, FGT_SVC);
    dc->trap_eret = EX_TBFLAG_A64(tb_flags, TRAP_ERET);
    dc->sve_excp_el = EX_TBFLAG_A64(tb_flags, SVEEXC_EL);
    dc->sme_excp_el = EX_TBFLAG_A64(tb_flags, SMEEXC_EL);
    dc->vl = (EX_TBFLAG_A64(tb_flags, VL) + 1) * 16;
    dc->svl = (EX_TBFLAG_A64(tb_flags, SVL) + 1) * 16;
    dc->pauth_active = EX_TBFLAG_A64(tb_flags, PAUTH_ACTIVE);
    dc->bt = EX_TBFLAG_A64(tb_flags, BT);
    dc->btype = EX_TBFLAG_A64(tb_flags, BTYPE);
    dc->unpriv = EX_TBFLAG_A64(tb_flags, UNPRIV);
    dc->ata[0] = EX_TBFLAG_A64(tb_flags, ATA);
    dc->ata[1] = EX_TBFLAG_A64(tb_flags, ATA0);
    dc->mte_active[0] = EX_TBFLAG_A64(tb_flags, MTE_ACTIVE);
    dc->mte_active[1] = EX_TBFLAG_A64(tb_flags, MTE0_ACTIVE);
    dc->pstate_sm = EX_TBFLAG_A64(tb_flags, PSTATE_SM);
    dc->pstate_za = EX_TBFLAG_A64(tb_flags, PSTATE_ZA);
    dc->sme_trap_nonstreaming = EX_TBFLAG_A64(tb_flags, SME_TRAP_NONSTREAMING);
    dc->naa = EX_TBFLAG_A64(tb_flags, NAA);
    dc->nv = EX_TBFLAG_A64(tb_flags, NV);
    dc->nv1 = EX_TBFLAG_A64(tb_flags, NV1);
    dc->nv2 = EX_TBFLAG_A64(tb_flags, NV2);
    dc->nv2_mem_e20 = EX_TBFLAG_A64(tb_flags, NV2_MEM_E20);
    dc->nv2_mem_be = EX_TBFLAG_A64(tb_flags, NV2_MEM_BE);
    dc->vec_len = 0;
    dc->vec_stride = 0;
    dc->cp_regs = arm_cpu->cp_regs;
    dc->features = env->features;
    dc->dcz_blocksize = arm_cpu->dcz_blocksize;
    dc->gm_blocksize = arm_cpu->gm_blocksize;

#ifdef CONFIG_USER_ONLY
    /* In sve_probe_page, we assume TBI is enabled. */
    tcg_debug_assert(dc->tbid & 1);
#endif

    dc->lse2 = dc_isar_feature(aa64_lse2, dc);

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
    dc->ss_active = EX_TBFLAG_ANY(tb_flags, SS_ACTIVE);
    dc->pstate_ss = EX_TBFLAG_ANY(tb_flags, PSTATE__SS);
    dc->is_ldex = false;

    /* Bound the number of insns to execute to those left on the page.  */
    bound = -(dc->base.pc_first | TARGET_PAGE_MASK) / 4;

    /* If architectural single step active, limit to 1.  */
    if (dc->ss_active) {
        bound = 1;
    }
    dc->base.max_insns = MIN(dc->base.max_insns, bound);
}

static void aarch64_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void aarch64_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    target_ulong pc_arg = dc->base.pc_next;

    if (tb_cflags(dcbase->tb) & CF_PCREL) {
        pc_arg &= ~TARGET_PAGE_MASK;
    }
    tcg_gen_insn_start(pc_arg, 0, 0);
    dc->insn_start_updated = false;
}

static void aarch64_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *s = container_of(dcbase, DisasContext, base);
    CPUARMState *env = cpu_env(cpu);
    uint64_t pc = s->base.pc_next;
    uint32_t insn;

    /* Singlestep exceptions have the highest priority. */
    if (s->ss_active && !s->pstate_ss) {
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
        assert(s->base.num_insns == 1);
        gen_swstep_exception(s, 0, 0);
        s->base.is_jmp = DISAS_NORETURN;
        s->base.pc_next = pc + 4;
        return;
    }

    if (pc & 3) {
        /*
         * PC alignment fault.  This has priority over the instruction abort
         * that we would receive from a translation fault via arm_ldl_code.
         * This should only be possible after an indirect branch, at the
         * start of the TB.
         */
        assert(s->base.num_insns == 1);
        gen_helper_exception_pc_alignment(tcg_env, tcg_constant_tl(pc));
        s->base.is_jmp = DISAS_NORETURN;
        s->base.pc_next = QEMU_ALIGN_UP(pc, 4);
        return;
    }

    s->pc_curr = pc;
    insn = arm_ldl_code(env, &s->base, pc, s->sctlr_b);
    s->insn = insn;
    s->base.pc_next = pc + 4;

    s->fp_access_checked = false;
    s->sve_access_checked = false;

    if (s->pstate_il) {
        /*
         * Illegal execution state. This has priority over BTI
         * exceptions, but comes after instruction abort exceptions.
         */
        gen_exception_insn(s, 0, EXCP_UDEF, syn_illegalstate());
        return;
    }

    if (dc_isar_feature(aa64_bti, s)) {
        if (s->base.num_insns == 1) {
            /* First insn can have btype set to non-zero.  */
            tcg_debug_assert(s->btype >= 0);

            /*
             * Note that the Branch Target Exception has fairly high
             * priority -- below debugging exceptions but above most
             * everything else.  This allows us to handle this now
             * instead of waiting until the insn is otherwise decoded.
             *
             * We can check all but the guarded page check here;
             * defer the latter to a helper.
             */
            if (s->btype != 0
                && !btype_destination_ok(insn, s->bt, s->btype)) {
                gen_helper_guarded_page_check(tcg_env);
            }
        } else {
            /* Not the first insn: btype must be 0.  */
            tcg_debug_assert(s->btype == 0);
        }
    }

    s->is_nonstreaming = false;
    if (s->sme_trap_nonstreaming) {
        disas_sme_fa64(s, insn);
    }

    if (!disas_a64(s, insn) &&
        !disas_sme(s, insn) &&
        !disas_sve(s, insn)) {
        disas_a64_legacy(s, insn);
    }

    /*
     * After execution of most insns, btype is reset to 0.
     * Note that we set btype == -1 when the insn sets btype.
     */
    if (s->btype > 0 && s->base.is_jmp != DISAS_NORETURN) {
        reset_btype(s);
    }
}

static void aarch64_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (unlikely(dc->ss_active)) {
        /* Note that this means single stepping WFI doesn't halt the CPU.
         * For conditional branch insns this is harmless unreachable code as
         * gen_goto_tb() has already handled emitting the debug exception
         * (and thus a tb-jump is not possible when singlestepping).
         */
        switch (dc->base.is_jmp) {
        default:
            gen_a64_update_pc(dc, 4);
            /* fall through */
        case DISAS_EXIT:
        case DISAS_JUMP:
            gen_step_complete_exception(dc);
            break;
        case DISAS_NORETURN:
            break;
        }
    } else {
        switch (dc->base.is_jmp) {
        case DISAS_NEXT:
        case DISAS_TOO_MANY:
            gen_goto_tb(dc, 1, 4);
            break;
        default:
        case DISAS_UPDATE_EXIT:
            gen_a64_update_pc(dc, 4);
            /* fall through */
        case DISAS_EXIT:
            tcg_gen_exit_tb(NULL, 0);
            break;
        case DISAS_UPDATE_NOCHAIN:
            gen_a64_update_pc(dc, 4);
            /* fall through */
        case DISAS_JUMP:
            tcg_gen_lookup_and_goto_ptr();
            break;
        case DISAS_NORETURN:
        case DISAS_SWI:
            break;
        case DISAS_WFE:
            gen_a64_update_pc(dc, 4);
            gen_helper_wfe(tcg_env);
            break;
        case DISAS_YIELD:
            gen_a64_update_pc(dc, 4);
            gen_helper_yield(tcg_env);
            break;
        case DISAS_WFI:
            /*
             * This is a special case because we don't want to just halt
             * the CPU if trying to debug across a WFI.
             */
            gen_a64_update_pc(dc, 4);
            gen_helper_wfi(tcg_env, tcg_constant_i32(4));
            /*
             * The helper doesn't necessarily throw an exception, but we
             * must go back to the main loop to check for interrupts anyway.
             */
            tcg_gen_exit_tb(NULL, 0);
            break;
        }
    }
}

const TranslatorOps aarch64_translator_ops = {
    .init_disas_context = aarch64_tr_init_disas_context,
    .tb_start           = aarch64_tr_tb_start,
    .insn_start         = aarch64_tr_insn_start,
    .translate_insn     = aarch64_tr_translate_insn,
    .tb_stop            = aarch64_tr_tb_stop,
};
