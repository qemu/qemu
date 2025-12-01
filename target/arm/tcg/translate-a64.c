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
#include "exec/target_page.h"
#include "translate.h"
#include "translate-a64.h"
#include "qemu/log.h"
#include "arm_ldst.h"
#include "semihosting/semihost.h"
#include "cpregs.h"

static TCGv_i64 cpu_X[32];
static TCGv_i64 cpu_gcspr[4];
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

/* initialize TCG globals.  */
void a64_translate_init(void)
{
    static const char gcspr_names[4][12] = {
        "gcspr_el0", "gcspr_el1", "gcspr_el2", "gcspr_el3"
    };

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

    for (i = 0; i < 4; i++) {
        cpu_gcspr[i] =
            tcg_global_mem_new_i64(tcg_env,
                                   offsetof(CPUARMState, cp15.gcspr_el[i]),
                                   gcspr_names[i]);
    }
}

/*
 * Return the full arm mmu_idx to use for A64 load/store insns which
 * have a "unprivileged load/store" variant. Those insns access
 * EL0 if executed from an EL which has control over EL0 (usually
 * EL1) but behave like normal loads and stores if executed from
 * elsewhere (eg EL3).
 *
 * @unpriv : true for the unprivileged encoding; false for the
 *           normal encoding (in which case we will return the same
 *           thing as get_mem_index().
 */
static ARMMMUIdx full_a64_user_mem_index(DisasContext *s, bool unpriv)
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
    return useridx;
}

/* Return the core mmu_idx per above. */
static int core_a64_user_mem_index(DisasContext *s, bool unpriv)
{
    return arm_to_core_mmu_idx(full_a64_user_mem_index(s, unpriv));
}

/* For a given translation regime, return the core mmu_idx for gcs access. */
static int core_gcs_mem_index(ARMMMUIdx armidx)
{
    return arm_to_core_mmu_idx(regime_to_gcs(armidx));
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
        desc = FIELD_DP32(desc, MTEDESC, ALIGN, memop_alignment_bits(memop));
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
        desc = FIELD_DP32(desc, MTEDESC, ALIGN, memop_alignment_bits(single_mop));
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

static void gen_add_gcs_record(DisasContext *s, TCGv_i64 value)
{
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);

    tcg_gen_addi_i64(addr, gcspr, -8);
    tcg_gen_qemu_st_i64(value, clean_data_tbi(s, addr), mmuidx, mop);
    tcg_gen_mov_i64(gcspr, addr);
}

static void gen_load_check_gcs_record(DisasContext *s, TCGv_i64 target,
                                      GCSInstructionType it, int rt)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 rec_va = tcg_temp_new_i64();

    tcg_gen_qemu_ld_i64(rec_va, clean_data_tbi(s, gcspr), mmuidx, mop);

    if (s->gcs_rvcen) {
        TCGLabel *fail_label =
            delay_exception(s, EXCP_UDEF, syn_gcs_data_check(it, rt));

        tcg_gen_brcond_i64(TCG_COND_NE, rec_va, target, fail_label);
    }

    gen_a64_set_pc(s, rec_va);
    tcg_gen_addi_i64(gcspr, gcspr, 8);
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

static void gen_goto_tb(DisasContext *s, unsigned tb_slot_idx, int64_t diff)
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
            tcg_gen_goto_tb(tb_slot_idx);
        } else {
            tcg_gen_goto_tb(tb_slot_idx);
            gen_a64_update_pc(s, diff);
        }
        tcg_gen_exit_tb(s->base.tb, tb_slot_idx);
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

static void clear_vec(DisasContext *s, int rd)
{
    unsigned ofs = fp_reg_offset(s, rd, MO_64);
    unsigned vsz = vec_full_reg_size(s);

    tcg_gen_gvec_dup_imm(MO_64, ofs, vsz, vsz, 0);
}

/*
 * Clear the bits above an N-bit vector, for N = (is_q ? 128 : 64).
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

/*
 * Write a double result to 128 bit vector register reg, honouring FPCR.NEP:
 * - if FPCR.NEP == 0, clear the high elements of reg
 * - if FPCR.NEP == 1, set the high elements of reg from mergereg
 *   (i.e. merge the result with those high elements)
 * In either case, SVE register bits above 128 are zeroed (per R_WKYLB).
 */
static void write_fp_dreg_merging(DisasContext *s, int reg, int mergereg,
                                  TCGv_i64 v)
{
    if (!s->fpcr_nep) {
        write_fp_dreg(s, reg, v);
        return;
    }

    /*
     * Move from mergereg to reg; this sets the high elements and
     * clears the bits above 128 as a side effect.
     */
    tcg_gen_gvec_mov(MO_64, vec_full_reg_offset(s, reg),
                     vec_full_reg_offset(s, mergereg),
                     16, vec_full_reg_size(s));
    tcg_gen_st_i64(v, tcg_env, vec_full_reg_offset(s, reg));
}

/*
 * Write a single-prec result, but only clear the higher elements
 * of the destination register if FPCR.NEP is 0; otherwise preserve them.
 */
static void write_fp_sreg_merging(DisasContext *s, int reg, int mergereg,
                                  TCGv_i32 v)
{
    if (!s->fpcr_nep) {
        write_fp_sreg(s, reg, v);
        return;
    }

    tcg_gen_gvec_mov(MO_64, vec_full_reg_offset(s, reg),
                     vec_full_reg_offset(s, mergereg),
                     16, vec_full_reg_size(s));
    tcg_gen_st_i32(v, tcg_env, fp_reg_offset(s, reg, MO_32));
}

/*
 * Write a half-prec result, but only clear the higher elements
 * of the destination register if FPCR.NEP is 0; otherwise preserve them.
 * The caller must ensure that the top 16 bits of v are zero.
 */
static void write_fp_hreg_merging(DisasContext *s, int reg, int mergereg,
                                  TCGv_i32 v)
{
    if (!s->fpcr_nep) {
        write_fp_sreg(s, reg, v);
        return;
    }

    tcg_gen_gvec_mov(MO_64, vec_full_reg_offset(s, reg),
                     vec_full_reg_offset(s, mergereg),
                     16, vec_full_reg_size(s));
    tcg_gen_st16_i32(v, tcg_env, fp_reg_offset(s, reg, MO_16));
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
                              int rm, ARMFPStatusFlavour fpsttype, int data,
                              gen_helper_gvec_3_ptr *fn)
{
    TCGv_ptr fpst = fpstatus_ptr(fpsttype);
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
 * Expand a 4-operand operation using an out-of-line helper that takes
 * a pointer to the CPU env.
 */
static void gen_gvec_op4_env(DisasContext *s, bool is_q, int rd, int rn,
                             int rm, int ra, int data,
                             gen_helper_gvec_4_ptr *fn)
{
    tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       vec_full_reg_offset(s, ra),
                       tcg_env,
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
}

/*
 * Expand a 4-operand + fpstatus pointer + simd data value operation using
 * an out-of-line helper.
 */
static void gen_gvec_op4_fpst(DisasContext *s, bool is_q, int rd, int rn,
                              int rm, int ra, ARMFPStatusFlavour fpsttype,
                              int data,
                              gen_helper_gvec_4_ptr *fn)
{
    TCGv_ptr fpst = fpstatus_ptr(fpsttype);
    tcg_gen_gvec_4_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn),
                       vec_full_reg_offset(s, rm),
                       vec_full_reg_offset(s, ra), fpst,
                       is_q ? 16 : 8, vec_full_reg_size(s), data, fn);
}

/*
 * When FPCR.AH == 1, NEG and ABS do not flip the sign bit of a NaN.
 * These functions implement
 *   d = floatN_is_any_nan(s) ? s : floatN_chs(s)
 * which for float32 is
 *   d = (s & ~(1 << 31)) > 0x7f800000UL) ? s : (s ^ (1 << 31))
 * and similarly for the other float sizes.
 */
static void gen_vfp_ah_negh(TCGv_i32 d, TCGv_i32 s)
{
    TCGv_i32 abs_s = tcg_temp_new_i32(), chs_s = tcg_temp_new_i32();

    gen_vfp_negh(chs_s, s);
    gen_vfp_absh(abs_s, s);
    tcg_gen_movcond_i32(TCG_COND_GTU, d,
                        abs_s, tcg_constant_i32(0x7c00),
                        s, chs_s);
}

static void gen_vfp_ah_negs(TCGv_i32 d, TCGv_i32 s)
{
    TCGv_i32 abs_s = tcg_temp_new_i32(), chs_s = tcg_temp_new_i32();

    gen_vfp_negs(chs_s, s);
    gen_vfp_abss(abs_s, s);
    tcg_gen_movcond_i32(TCG_COND_GTU, d,
                        abs_s, tcg_constant_i32(0x7f800000UL),
                        s, chs_s);
}

static void gen_vfp_ah_negd(TCGv_i64 d, TCGv_i64 s)
{
    TCGv_i64 abs_s = tcg_temp_new_i64(), chs_s = tcg_temp_new_i64();

    gen_vfp_negd(chs_s, s);
    gen_vfp_absd(abs_s, s);
    tcg_gen_movcond_i64(TCG_COND_GTU, d,
                        abs_s, tcg_constant_i64(0x7ff0000000000000ULL),
                        s, chs_s);
}

/*
 * These functions implement
 *  d = floatN_is_any_nan(s) ? s : floatN_abs(s)
 * which for float32 is
 *  d = (s & ~(1 << 31)) > 0x7f800000UL) ? s : (s & ~(1 << 31))
 * and similarly for the other float sizes.
 */
static void gen_vfp_ah_absh(TCGv_i32 d, TCGv_i32 s)
{
    TCGv_i32 abs_s = tcg_temp_new_i32();

    gen_vfp_absh(abs_s, s);
    tcg_gen_movcond_i32(TCG_COND_GTU, d,
                        abs_s, tcg_constant_i32(0x7c00),
                        s, abs_s);
}

static void gen_vfp_ah_abss(TCGv_i32 d, TCGv_i32 s)
{
    TCGv_i32 abs_s = tcg_temp_new_i32();

    gen_vfp_abss(abs_s, s);
    tcg_gen_movcond_i32(TCG_COND_GTU, d,
                        abs_s, tcg_constant_i32(0x7f800000UL),
                        s, abs_s);
}

static void gen_vfp_ah_absd(TCGv_i64 d, TCGv_i64 s)
{
    TCGv_i64 abs_s = tcg_temp_new_i64();

    gen_vfp_absd(abs_s, s);
    tcg_gen_movcond_i64(TCG_COND_GTU, d,
                        abs_s, tcg_constant_i64(0x7ff0000000000000ULL),
                        s, abs_s);
}

static void gen_vfp_maybe_ah_negh(DisasContext *dc, TCGv_i32 d, TCGv_i32 s)
{
    if (dc->fpcr_ah) {
        gen_vfp_ah_negh(d, s);
    } else {
        gen_vfp_negh(d, s);
    }
}

static void gen_vfp_maybe_ah_negs(DisasContext *dc, TCGv_i32 d, TCGv_i32 s)
{
    if (dc->fpcr_ah) {
        gen_vfp_ah_negs(d, s);
    } else {
        gen_vfp_negs(d, s);
    }
}

static void gen_vfp_maybe_ah_negd(DisasContext *dc, TCGv_i64 d, TCGv_i64 s)
{
    if (dc->fpcr_ah) {
        gen_vfp_ah_negd(d, s);
    } else {
        gen_vfp_negd(d, s);
    }
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

        tcg_gen_extu_i32_i64(cf_64, cpu_CF);
        tcg_gen_addcio_i64(result, cf_64, t0, t1, cf_64);
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

        tcg_gen_extrl_i64_i32(t0_32, t0);
        tcg_gen_extrl_i64_i32(t1_32, t1);
        tcg_gen_addcio_i32(cpu_NF, cpu_CF, t0_32, t1_32, cpu_CF);

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
        s->fp_access_checked = -1;

        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_fp_access_trap(1, 0xe, false, 0),
                              s->fp_excp_el);
        return false;
    }
    s->fp_access_checked = 1;
    return true;
}

static bool nonstreaming_check(DisasContext *s)
{
    if (s->sme_trap_nonstreaming && s->is_nonstreaming) {
        gen_exception_insn(s, 0, EXCP_UDEF,
                           syn_smetrap(SME_ET_Streaming, false));
        return false;
    }
    return true;
}

static bool fp_access_check(DisasContext *s)
{
    return fp_access_check_only(s) && nonstreaming_check(s);
}

/*
 * Return <0 for non-supported element sizes, with MO_16 controlled by
 * FEAT_FP16; return 0 for fp disabled; otherwise return >0 for success.
 */
static int fp_access_check_scalar_hsd(DisasContext *s, MemOp esz)
{
    switch (esz) {
    case MO_64:
    case MO_32:
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return -1;
        }
        break;
    default:
        return -1;
    }
    return fp_access_check(s);
}

/* Likewise, but vector MO_64 must have two elements. */
static int fp_access_check_vector_hsd(DisasContext *s, bool is_q, MemOp esz)
{
    switch (esz) {
    case MO_64:
        if (!is_q) {
            return -1;
        }
        break;
    case MO_32:
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return -1;
        }
        break;
    default:
        return -1;
    }
    return fp_access_check(s);
}

/*
 * Check that SVE access is enabled.  If it is, return true.
 * If not, emit code to generate an appropriate exception and return false.
 * This function corresponds to CheckSVEEnabled().
 */
bool sve_access_check(DisasContext *s)
{
    if (dc_isar_feature(aa64_sme, s)) {
        bool ret;

        if (s->pstate_sm) {
            ret = sme_enabled_check(s);
        } else if (dc_isar_feature(aa64_sve, s)) {
            goto continue_sve;
        } else {
            ret = sme_sm_enabled_check(s);
        }
        if (ret) {
            ret = nonstreaming_check(s);
        }
        s->sve_access_checked = (ret ? 1 : -1);
        return ret;
    }

 continue_sve:
    if (s->sve_excp_el) {
        /* Assert that we only raise one exception per instruction. */
        assert(!s->sve_access_checked);
        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_sve_access_trap(), s->sve_excp_el);
        s->sve_access_checked = -1;
        return false;
    }
    s->sve_access_checked = 1;
    return fp_access_check(s);
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
    if (s->sme_excp_el
        && (!s->fp_excp_el || s->sme_excp_el <= s->fp_excp_el)) {
        bool ret = sme_access_check(s);
        s->fp_access_checked = (ret ? 1 : -1);
        return ret;
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
    TCGv_i64 link = tcg_temp_new_i64();

    gen_pc_plus_diff(s, link, 4);
    if (s->gcs_en) {
        gen_add_gcs_record(s, link);
    }
    tcg_gen_mov_i64(cpu_reg(s, 30), link);

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
    TCGv_i64 link = tcg_temp_new_i64();

    gen_pc_plus_diff(s, link, 4);
    if (s->gcs_en) {
        gen_add_gcs_record(s, link);
    }
    gen_a64_set_pc(s, cpu_reg(s, a->rn));
    tcg_gen_mov_i64(cpu_reg(s, 30), link);

    set_btype_for_blr(s);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_RET(DisasContext *s, arg_r *a)
{
    TCGv_i64 target = cpu_reg(s, a->rn);

    if (s->gcs_en) {
        gen_load_check_gcs_record(s, target, GCS_IT_RET_nPauth, a->rn);
    } else {
        gen_a64_set_pc(s, target);
    }
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
    TCGv_i64 dst, link;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }
    dst = auth_branch_target(s, cpu_reg(s, a->rn), tcg_constant_i64(0), !a->m);

    link = tcg_temp_new_i64();
    gen_pc_plus_diff(s, link, 4);
    if (s->gcs_en) {
        gen_add_gcs_record(s, link);
    }
    gen_a64_set_pc(s, dst);
    tcg_gen_mov_i64(cpu_reg(s, 30), link);

    set_btype_for_blr(s);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_RETA(DisasContext *s, arg_reta *a)
{
    TCGv_i64 dst;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }

    dst = auth_branch_target(s, cpu_reg(s, 30), cpu_X[31], !a->m);
    if (s->gcs_en) {
        GCSInstructionType it = a->m ? GCS_IT_RET_PauthB : GCS_IT_RET_PauthA;
        gen_load_check_gcs_record(s, dst, it, 30);
    } else {
        gen_a64_set_pc(s, dst);
    }
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
    set_btype_for_br(s, a->rn);
    gen_a64_set_pc(s, dst);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_BLRA(DisasContext *s, arg_bra *a)
{
    TCGv_i64 dst, link;

    if (!dc_isar_feature(aa64_pauth, s)) {
        return false;
    }
    dst = auth_branch_target(s, cpu_reg(s, a->rn), cpu_reg_sp(s, a->rm), !a->m);

    link = tcg_temp_new_i64();
    gen_pc_plus_diff(s, link, 4);
    if (s->gcs_en) {
        gen_add_gcs_record(s, link);
    }
    gen_a64_set_pc(s, dst);
    tcg_gen_mov_i64(cpu_reg(s, 30), link);

    set_btype_for_blr(s);
    s->base.is_jmp = DISAS_JUMP;
    return true;
}

static bool trans_ERET(DisasContext *s, arg_ERET *a)
{
#ifdef CONFIG_USER_ONLY
    return false;
#else
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
#endif
}

static bool trans_ERETA(DisasContext *s, arg_reta *a)
{
#ifdef CONFIG_USER_ONLY
    return false;
#else
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
#endif
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

static bool trans_GCSB(DisasContext *s, arg_GCSB *a)
{
    if (dc_isar_feature(aa64_gcs, s)) {
        tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
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

static bool trans_CHKFEAT(DisasContext *s, arg_CHKFEAT *a)
{
    uint64_t feat_en = 0;

    if (s->gcs_en) {
        feat_en |= 1 << 0;
    }
    if (feat_en) {
        TCGv_i64 x16 = cpu_reg(s, 16);
        tcg_gen_andi_i64(x16, x16, ~feat_en);
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

static bool trans_DSB_nXS(DisasContext *s, arg_DSB_nXS *a)
{
    if (!dc_isar_feature(aa64_xs, s)) {
        return false;
    }
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
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

static void gen_gcspopm(DisasContext *s, int rt)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 value = tcg_temp_new_i64();
    TCGLabel *fail_label =
        delay_exception(s, EXCP_UDEF, syn_gcs_data_check(GCS_IT_GCSPOPM, rt));

    /* The value at top-of-stack must have low 2 bits clear. */
    tcg_gen_qemu_ld_i64(value, clean_data_tbi(s, gcspr), mmuidx, mop);
    tcg_gen_brcondi_i64(TCG_COND_TSTNE, value, 3, fail_label);

    /* Complete the pop and return the value. */
    tcg_gen_addi_i64(gcspr, gcspr, 8);
    tcg_gen_mov_i64(cpu_reg(s, rt), value);
}

static void gen_gcspushx(DisasContext *s)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int spsr_idx = aarch64_banked_spsr_index(s->current_el);
    int spsr_off = offsetof(CPUARMState, banked_spsr[spsr_idx]);
    int elr_off = offsetof(CPUARMState, elr_el[s->current_el]);
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();

    tcg_gen_addi_i64(addr, gcspr, -8);
    tcg_gen_qemu_st_i64(cpu_reg(s, 30), addr, mmuidx, mop);

    tcg_gen_ld_i64(tmp, tcg_env, spsr_off);
    tcg_gen_addi_i64(addr, addr, -8);
    tcg_gen_qemu_st_i64(tmp, addr, mmuidx, mop);

    tcg_gen_ld_i64(tmp, tcg_env, elr_off);
    tcg_gen_addi_i64(addr, addr, -8);
    tcg_gen_qemu_st_i64(tmp, addr, mmuidx, mop);

    tcg_gen_addi_i64(addr, addr, -8);
    tcg_gen_qemu_st_i64(tcg_constant_i64(0b1001), addr, mmuidx, mop);

    tcg_gen_mov_i64(gcspr, addr);
    clear_pstate_bits(PSTATE_EXLOCK);
}

static void gen_gcspopcx(DisasContext *s)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int spsr_idx = aarch64_banked_spsr_index(s->current_el);
    int spsr_off = offsetof(CPUARMState, banked_spsr[spsr_idx]);
    int elr_off = offsetof(CPUARMState, elr_el[s->current_el]);
    int gcscr_off = offsetof(CPUARMState, cp15.gcscr_el[s->current_el]);
    int pstate_off = offsetof(CPUARMState, pstate);
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 tmp1 = tcg_temp_new_i64();
    TCGv_i64 tmp2 = tcg_temp_new_i64();
    TCGLabel *fail_label =
        delay_exception(s, EXCP_UDEF, syn_gcs_data_check(GCS_IT_GCSPOPCX, 31));

    /* The value at top-of-stack must be an exception token. */
    tcg_gen_qemu_ld_i64(tmp1, gcspr, mmuidx, mop);
    tcg_gen_brcondi_i64(TCG_COND_NE, tmp1, 0b1001, fail_label);

    /* Validate in turn, ELR ... */
    tcg_gen_addi_i64(addr, gcspr, 8);
    tcg_gen_qemu_ld_i64(tmp1, addr, mmuidx, mop);
    tcg_gen_ld_i64(tmp2, tcg_env, elr_off);
    tcg_gen_brcond_i64(TCG_COND_NE, tmp1, tmp2, fail_label);

    /* ... SPSR ... */
    tcg_gen_addi_i64(addr, addr, 8);
    tcg_gen_qemu_ld_i64(tmp1, addr, mmuidx, mop);
    tcg_gen_ld_i64(tmp2, tcg_env, spsr_off);
    tcg_gen_brcond_i64(TCG_COND_NE, tmp1, tmp2, fail_label);

    /* ... and LR. */
    tcg_gen_addi_i64(addr, addr, 8);
    tcg_gen_qemu_ld_i64(tmp1, addr, mmuidx, mop);
    tcg_gen_brcond_i64(TCG_COND_NE, tmp1, cpu_reg(s, 30), fail_label);

    /* Writeback stack pointer after pop. */
    tcg_gen_addi_i64(gcspr, addr, 8);

    /* PSTATE.EXLOCK = GetCurrentEXLOCKEN(). */
    tcg_gen_ld_i64(tmp1, tcg_env, gcscr_off);
    tcg_gen_ld_i64(tmp2, tcg_env, pstate_off);
    tcg_gen_shri_i64(tmp1, tmp1, ctz64(GCSCR_EXLOCKEN));
    tcg_gen_deposit_i64(tmp2, tmp2, tmp1, ctz64(PSTATE_EXLOCK), 1);
    tcg_gen_st_i64(tmp2, tcg_env, pstate_off);
}

static void gen_gcspopx(DisasContext *s)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 addr = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGLabel *fail_label =
        delay_exception(s, EXCP_UDEF, syn_gcs_data_check(GCS_IT_GCSPOPX, 31));

    /* The value at top-of-stack must be an exception token. */
    tcg_gen_qemu_ld_i64(tmp, gcspr, mmuidx, mop);
    tcg_gen_brcondi_i64(TCG_COND_NE, tmp, 0b1001, fail_label);

    /*
     * The other three values in the exception return record
     * are ignored, but are loaded anyway to raise faults.
     */
    tcg_gen_addi_i64(addr, gcspr, 8);
    tcg_gen_qemu_ld_i64(tmp, addr, mmuidx, mop);
    tcg_gen_addi_i64(addr, addr, 8);
    tcg_gen_qemu_ld_i64(tmp, addr, mmuidx, mop);
    tcg_gen_addi_i64(addr, addr, 8);
    tcg_gen_qemu_ld_i64(tmp, addr, mmuidx, mop);
    tcg_gen_addi_i64(gcspr, addr, 8);
}

static void gen_gcsss1(DisasContext *s, int rt)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 inptr = cpu_reg(s, rt);
    TCGv_i64 cmp = tcg_temp_new_i64();
    TCGv_i64 new = tcg_temp_new_i64();
    TCGv_i64 old = tcg_temp_new_i64();
    TCGLabel *fail_label =
        delay_exception(s, EXCP_UDEF, syn_gcs_data_check(GCS_IT_GCSSS1, rt));

    /* Compute the valid cap entry that the new stack must have. */
    tcg_gen_deposit_i64(cmp, inptr, tcg_constant_i64(1), 0, 12);
    /* Compute the in-progress cap entry for the old stack. */
    tcg_gen_deposit_i64(new, gcspr, tcg_constant_i64(5), 0, 3);

    /* Swap the valid cap the with the in-progress cap. */
    tcg_gen_atomic_cmpxchg_i64(old, inptr, cmp, new, mmuidx, mop);
    tcg_gen_brcond_i64(TCG_COND_NE, old, cmp, fail_label);

    /* The new stack had a valid cap: change gcspr. */
    tcg_gen_andi_i64(gcspr, inptr, ~7);
}

static void gen_gcsss2(DisasContext *s, int rt)
{
    TCGv_i64 gcspr = cpu_gcspr[s->current_el];
    int mmuidx = core_gcs_mem_index(s->mmu_idx);
    MemOp mop = finalize_memop(s, MO_64 | MO_ALIGN);
    TCGv_i64 outptr = tcg_temp_new_i64();
    TCGv_i64 tmp = tcg_temp_new_i64();
    TCGLabel *fail_label =
        delay_exception(s, EXCP_UDEF, syn_gcs_data_check(GCS_IT_GCSSS2, rt));

    /* Validate that the new stack has an in-progress cap. */
    tcg_gen_qemu_ld_i64(outptr, gcspr, mmuidx, mop);
    tcg_gen_andi_i64(tmp, outptr, 7);
    tcg_gen_brcondi_i64(TCG_COND_NE, tmp, 5, fail_label);

    /* Push a valid cap to the old stack. */
    tcg_gen_andi_i64(outptr, outptr, ~7);
    tcg_gen_addi_i64(outptr, outptr, -8);
    tcg_gen_deposit_i64(tmp, outptr, tcg_constant_i64(1), 0, 12);
    tcg_gen_qemu_st_i64(tmp, outptr, mmuidx, mop);
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);

    /* Pop the in-progress cap from the new stack. */
    tcg_gen_addi_i64(gcspr, gcspr, 8);

    /* Return a pointer to the old stack cap. */
    tcg_gen_mov_i64(cpu_reg(s, rt), outptr);
}

/*
 * Look up @key, returning the cpreg, which must exist.
 * Additionally, the new cpreg must also be accessible.
 */
static const ARMCPRegInfo *
redirect_cpreg(DisasContext *s, uint32_t key, bool isread)
{
    const ARMCPRegInfo *ri = get_arm_cp_reginfo(s->cp_regs, key);
    assert(ri);
    assert(cp_access_ok(s->current_el, ri, isread));
    return ri;
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
    uint32_t key = ENCODE_AA64_CP_REG(op0, op1, crn, crm, op2);
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

    if (ri->vhe_redir_to_el2 && s->current_el == 2 && s->e2h) {
        /*
         * This one of the FOO_EL1 registers which redirect to FOO_EL2
         * from EL2 when HCR_EL2.E2H is set.
         */
        key = ri->vhe_redir_to_el2;
        ri = redirect_cpreg(s, key, isread);
    } else if (ri->vhe_redir_to_el01 && s->current_el >= 2) {
        /*
         * This is one of the FOO_EL12 or FOO_EL02 registers.
         * With !E2H, they all UNDEF.
         * With E2H, from EL2 or EL3, they redirect to FOO_EL1/FOO_EL0.
         */
        if (!s->e2h) {
            gen_sysreg_undef(s, isread, op0, op1, op2, crn, crm, rt);
            return;
        }
        key = ri->vhe_redir_to_el01;
        ri = redirect_cpreg(s, key, isread);
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
        key = ENCODE_AA64_CP_REG(op0, 0, crn, crm, op2);
        ri = redirect_cpreg(s, key, isread);
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
    case ARM_CP_GCSPUSHM:
        if (s->gcs_en) {
            gen_add_gcs_record(s, cpu_reg(s, rt));
        }
        return;
    case ARM_CP_GCSPOPM:
        /* Note that X[rt] is unchanged if !GCSEnabled. */
        if (s->gcs_en) {
            gen_gcspopm(s, rt);
        }
        return;
    case ARM_CP_GCSPUSHX:
        /* Choose the CONSTRAINED UNPREDICTABLE for UNDEF. */
        if (rt != 31) {
            unallocated_encoding(s);
        } else if (s->gcs_en) {
            gen_gcspushx(s);
        }
        return;
    case ARM_CP_GCSPOPCX:
        /* Choose the CONSTRAINED UNPREDICTABLE for UNDEF. */
        if (rt != 31) {
            unallocated_encoding(s);
        } else if (s->gcs_en) {
            gen_gcspopcx(s);
        }
        return;
    case ARM_CP_GCSPOPX:
        /* Choose the CONSTRAINED UNPREDICTABLE for UNDEF. */
        if (rt != 31) {
            unallocated_encoding(s);
        } else if (s->gcs_en) {
            gen_gcspopx(s);
        }
        return;
    case ARM_CP_GCSSS1:
        if (s->gcs_en) {
            gen_gcsss1(s, rt);
        }
        return;
    case ARM_CP_GCSSS2:
        if (s->gcs_en) {
            gen_gcsss2(s, rt);
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
    if (!dc_isar_feature(aa64_lse, s)) {
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
    if (!dc_isar_feature(aa64_lse, s)) {
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
    mop |= (a->sz == 2 ? MO_ALIGN_4 : MO_ALIGN_8);
    mop |= (s->align_mem ? 0 : MO_ALIGN_TLB_ONLY);
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
    mop |= (a->sz == 2 ? MO_ALIGN_4 : MO_ALIGN_8);
    mop |= (s->align_mem ? 0 : MO_ALIGN_TLB_ONLY);
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
    memidx = core_a64_user_mem_index(s, a->unpriv);
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
    int memidx = core_a64_user_mem_index(s, a->unpriv);
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
    int memidx = core_a64_user_mem_index(s, a->unpriv);
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

TRANS_FEAT(LDADD, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_add_i64, 0, false)
TRANS_FEAT(LDCLR, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_and_i64, 0, true)
TRANS_FEAT(LDEOR, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_xor_i64, 0, false)
TRANS_FEAT(LDSET, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_or_i64, 0, false)
TRANS_FEAT(LDSMAX, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_smax_i64, MO_SIGN, false)
TRANS_FEAT(LDSMIN, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_smin_i64, MO_SIGN, false)
TRANS_FEAT(LDUMAX, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_umax_i64, 0, false)
TRANS_FEAT(LDUMIN, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_fetch_umin_i64, 0, false)
TRANS_FEAT(SWP, aa64_lse, do_atomic_ld, a, tcg_gen_atomic_xchg_i64, 0, false)

typedef void Atomic128ThreeOpFn(TCGv_i128, TCGv_i64, TCGv_i128, TCGArg, MemOp);

static bool do_atomic128_ld(DisasContext *s, arg_atomic128 *a,
                            Atomic128ThreeOpFn *fn, bool invert)
{
    MemOp mop;
    int rlo, rhi;
    TCGv_i64 clean_addr, tlo, thi;
    TCGv_i128 t16;

    if (a->rt == 31 || a->rt2 == 31 || a->rt == a->rt2) {
        return false;
    }
    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    mop = check_atomic_align(s, a->rn, MO_128);
    clean_addr = gen_mte_check1(s, cpu_reg_sp(s, a->rn), false,
                                a->rn != 31, mop);

    rlo = (s->be_data == MO_LE ? a->rt : a->rt2);
    rhi = (s->be_data == MO_LE ? a->rt2 : a->rt);

    tlo = read_cpu_reg(s, rlo, true);
    thi = read_cpu_reg(s, rhi, true);
    if (invert) {
        tcg_gen_not_i64(tlo, tlo);
        tcg_gen_not_i64(thi, thi);
    }
    /*
     * The tcg atomic primitives are all full barriers.  Therefore we
     * can ignore the Acquire and Release bits of this instruction.
     */
    t16 = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(t16, tlo, thi);

    fn(t16, clean_addr, t16, get_mem_index(s), mop);

    tcg_gen_extr_i128_i64(cpu_reg(s, rlo), cpu_reg(s, rhi), t16);
    return true;
}

TRANS_FEAT(LDCLRP, aa64_lse128, do_atomic128_ld,
           a, tcg_gen_atomic_fetch_and_i128, true)
TRANS_FEAT(LDSETP, aa64_lse128, do_atomic128_ld,
           a, tcg_gen_atomic_fetch_or_i128, false)
TRANS_FEAT(SWPP, aa64_lse128, do_atomic128_ld,
           a, tcg_gen_atomic_xchg_i128, false)

static bool trans_LDAPR(DisasContext *s, arg_LDAPR *a)
{
    bool iss_sf = ldst_iss_sf(a->sz, false, false);
    TCGv_i64 clean_addr;
    MemOp mop;

    if (!dc_isar_feature(aa64_lse, s) ||
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

static bool trans_GCSSTR(DisasContext *s, arg_GCSSTR *a)
{
    ARMMMUIdx armidx;

    if (!dc_isar_feature(aa64_gcs, s)) {
        return false;
    }

    /*
     * The pseudocode for GCSSTTR is
     *
     *   effective_el = AArch64.IsUnprivAccessPriv() ? PSTATE.EL : EL0;
     *   if (effective_el == PSTATE.EL) CheckGCSSTREnabled();
     *
     * We have cached the result of IsUnprivAccessPriv in DisasContext,
     * but since we need the result of full_a64_user_mem_index anyway,
     * use the mmu_idx test as a proxy for the effective_el test.
     */
    armidx = full_a64_user_mem_index(s, a->unpriv);
    if (armidx == s->mmu_idx && s->gcsstr_el != 0) {
        gen_exception_insn_el(s, 0, EXCP_UDEF,
                              syn_gcs_gcsstr(a->rn, a->rt),
                              s->gcsstr_el);
        return true;
    }

    if (a->rn == 31) {
        gen_check_sp_alignment(s);
    }
    tcg_gen_qemu_st_i64(cpu_reg(s, a->rt),
                        clean_data_tbi(s, cpu_reg_sp(s, a->rn)),
                        core_gcs_mem_index(armidx),
                        finalize_memop(s, MO_64 | MO_ALIGN));
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

    memidx = core_a64_user_mem_index(s, a->unpriv);

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

    rmemidx = core_a64_user_mem_index(s, runpriv);
    wmemidx = core_a64_user_mem_index(s, wunpriv);

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
 * Min/Max (immediate)
 */

static void gen_wrap3_i32(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, NeonGenTwoOpFn fn)
{
    TCGv_i32 t1 = tcg_temp_new_i32();
    TCGv_i32 t2 = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t1, n);
    tcg_gen_extrl_i64_i32(t2, m);
    fn(t1, t1, t2);
    tcg_gen_extu_i32_i64(d, t1);
}

static void gen_smax32_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    gen_wrap3_i32(d, n, m, tcg_gen_smax_i32);
}

static void gen_smin32_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    gen_wrap3_i32(d, n, m, tcg_gen_smin_i32);
}

static void gen_umax32_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    gen_wrap3_i32(d, n, m, tcg_gen_umax_i32);
}

static void gen_umin32_i64(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m)
{
    gen_wrap3_i32(d, n, m, tcg_gen_umin_i32);
}

TRANS_FEAT(SMAX_i, aa64_cssc, gen_rri, a, 0, 0,
           a->sf ? tcg_gen_smax_i64 : gen_smax32_i64)
TRANS_FEAT(SMIN_i, aa64_cssc, gen_rri, a, 0, 0,
           a->sf ? tcg_gen_smin_i64 : gen_smin32_i64)
TRANS_FEAT(UMAX_i, aa64_cssc, gen_rri, a, 0, 0,
           a->sf ? tcg_gen_umax_i64 : gen_umax32_i64)
TRANS_FEAT(UMIN_i, aa64_cssc, gen_rri, a, 0, 0,
           a->sf ? tcg_gen_umin_i64 : gen_umin32_i64)

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

static bool trans_TBL_TBX(DisasContext *s, arg_TBL_TBX *a)
{
    if (fp_access_check(s)) {
        int len = (a->len + 1) * 16;

        tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, a->rd),
                           vec_full_reg_offset(s, a->rm), tcg_env,
                           a->q ? 16 : 8, vec_full_reg_size(s),
                           (len << 6) | (a->tbx << 5) | a->rn,
                           gen_helper_simd_tblx);
    }
    return true;
}

typedef int simd_permute_idx_fn(int i, int part, int elements);

static bool do_simd_permute(DisasContext *s, arg_qrrr_e *a,
                            simd_permute_idx_fn *fn, int part)
{
    MemOp esz = a->esz;
    int datasize = a->q ? 16 : 8;
    int elements = datasize >> esz;
    TCGv_i64 tcg_res[2], tcg_ele;

    if (esz == MO_64 && !a->q) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    tcg_res[0] = tcg_temp_new_i64();
    tcg_res[1] = a->q ? tcg_temp_new_i64() : NULL;
    tcg_ele = tcg_temp_new_i64();

    for (int i = 0; i < elements; i++) {
        int o, w, idx;

        idx = fn(i, part, elements);
        read_vec_element(s, tcg_ele, (idx & elements ? a->rm : a->rn),
                         idx & (elements - 1), esz);

        w = (i << (esz + 3)) / 64;
        o = (i << (esz + 3)) % 64;
        if (o == 0) {
            tcg_gen_mov_i64(tcg_res[w], tcg_ele);
        } else {
            tcg_gen_deposit_i64(tcg_res[w], tcg_res[w], tcg_ele, o, 8 << esz);
        }
    }

    for (int i = a->q; i >= 0; --i) {
        write_vec_element(s, tcg_res[i], a->rd, i, MO_64);
    }
    clear_vec_high(s, a->q, a->rd);
    return true;
}

static int permute_load_uzp(int i, int part, int elements)
{
    return 2 * i + part;
}

TRANS(UZP1, do_simd_permute, a, permute_load_uzp, 0)
TRANS(UZP2, do_simd_permute, a, permute_load_uzp, 1)

static int permute_load_trn(int i, int part, int elements)
{
    return (i & 1) * elements + (i & ~1) + part;
}

TRANS(TRN1, do_simd_permute, a, permute_load_trn, 0)
TRANS(TRN2, do_simd_permute, a, permute_load_trn, 1)

static int permute_load_zip(int i, int part, int elements)
{
    return (i & 1) * elements + ((part * elements + i) >> 1);
}

TRANS(ZIP1, do_simd_permute, a, permute_load_zip, 0)
TRANS(ZIP2, do_simd_permute, a, permute_load_zip, 1)

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

        read_vec_element_i32(s, tcg_op1, a->rn, 3, MO_32);
        read_vec_element_i32(s, tcg_op2, a->rm, 3, MO_32);
        read_vec_element_i32(s, tcg_op3, a->ra, 3, MO_32);

        tcg_gen_rotri_i32(tcg_res, tcg_op1, 20);
        tcg_gen_add_i32(tcg_res, tcg_res, tcg_op2);
        tcg_gen_add_i32(tcg_res, tcg_res, tcg_op3);
        tcg_gen_rotri_i32(tcg_res, tcg_res, 25);

        /* Clear the whole register first, then store bits [127:96]. */
        clear_vec(s, a->rd);
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

static bool do_fp3_scalar_with_fpsttype(DisasContext *s, arg_rrr_e *a,
                                        const FPScalar *f, int mergereg,
                                        ARMFPStatusFlavour fpsttype)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t0 = read_fp_dreg(s, a->rn);
            TCGv_i64 t1 = read_fp_dreg(s, a->rm);
            f->gen_d(t0, t0, t1, fpstatus_ptr(fpsttype));
            write_fp_dreg_merging(s, a->rd, mergereg, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rn);
            TCGv_i32 t1 = read_fp_sreg(s, a->rm);
            f->gen_s(t0, t0, t1, fpstatus_ptr(fpsttype));
            write_fp_sreg_merging(s, a->rd, mergereg, t0);
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_hreg(s, a->rn);
            TCGv_i32 t1 = read_fp_hreg(s, a->rm);
            f->gen_h(t0, t0, t1, fpstatus_ptr(fpsttype));
            write_fp_hreg_merging(s, a->rd, mergereg, t0);
        }
        break;
    default:
        return false;
    }
    return true;
}

static bool do_fp3_scalar(DisasContext *s, arg_rrr_e *a, const FPScalar *f,
                          int mergereg)
{
    return do_fp3_scalar_with_fpsttype(s, a, f, mergereg,
                                       a->esz == MO_16 ?
                                       FPST_A64_F16 : FPST_A64);
}

static bool do_fp3_scalar_ah_2fn(DisasContext *s, arg_rrr_e *a,
                                 const FPScalar *fnormal, const FPScalar *fah,
                                 int mergereg)
{
    return do_fp3_scalar_with_fpsttype(s, a, s->fpcr_ah ? fah : fnormal,
                                       mergereg, select_ah_fpst(s, a->esz));
}

/* Some insns need to call different helpers when FPCR.AH == 1 */
static bool do_fp3_scalar_2fn(DisasContext *s, arg_rrr_e *a,
                              const FPScalar *fnormal,
                              const FPScalar *fah,
                              int mergereg)
{
    return do_fp3_scalar(s, a, s->fpcr_ah ? fah : fnormal, mergereg);
}

static const FPScalar f_scalar_fadd = {
    gen_helper_vfp_addh,
    gen_helper_vfp_adds,
    gen_helper_vfp_addd,
};
TRANS(FADD_s, do_fp3_scalar, a, &f_scalar_fadd, a->rn)

static const FPScalar f_scalar_fsub = {
    gen_helper_vfp_subh,
    gen_helper_vfp_subs,
    gen_helper_vfp_subd,
};
TRANS(FSUB_s, do_fp3_scalar, a, &f_scalar_fsub, a->rn)

static const FPScalar f_scalar_fdiv = {
    gen_helper_vfp_divh,
    gen_helper_vfp_divs,
    gen_helper_vfp_divd,
};
TRANS(FDIV_s, do_fp3_scalar, a, &f_scalar_fdiv, a->rn)

static const FPScalar f_scalar_fmul = {
    gen_helper_vfp_mulh,
    gen_helper_vfp_muls,
    gen_helper_vfp_muld,
};
TRANS(FMUL_s, do_fp3_scalar, a, &f_scalar_fmul, a->rn)

static const FPScalar f_scalar_fmax = {
    gen_helper_vfp_maxh,
    gen_helper_vfp_maxs,
    gen_helper_vfp_maxd,
};
static const FPScalar f_scalar_fmax_ah = {
    gen_helper_vfp_ah_maxh,
    gen_helper_vfp_ah_maxs,
    gen_helper_vfp_ah_maxd,
};
TRANS(FMAX_s, do_fp3_scalar_2fn, a, &f_scalar_fmax, &f_scalar_fmax_ah, a->rn)

static const FPScalar f_scalar_fmin = {
    gen_helper_vfp_minh,
    gen_helper_vfp_mins,
    gen_helper_vfp_mind,
};
static const FPScalar f_scalar_fmin_ah = {
    gen_helper_vfp_ah_minh,
    gen_helper_vfp_ah_mins,
    gen_helper_vfp_ah_mind,
};
TRANS(FMIN_s, do_fp3_scalar_2fn, a, &f_scalar_fmin, &f_scalar_fmin_ah, a->rn)

static const FPScalar f_scalar_fmaxnm = {
    gen_helper_vfp_maxnumh,
    gen_helper_vfp_maxnums,
    gen_helper_vfp_maxnumd,
};
TRANS(FMAXNM_s, do_fp3_scalar, a, &f_scalar_fmaxnm, a->rn)

static const FPScalar f_scalar_fminnm = {
    gen_helper_vfp_minnumh,
    gen_helper_vfp_minnums,
    gen_helper_vfp_minnumd,
};
TRANS(FMINNM_s, do_fp3_scalar, a, &f_scalar_fminnm, a->rn)

static const FPScalar f_scalar_fmulx = {
    gen_helper_advsimd_mulxh,
    gen_helper_vfp_mulxs,
    gen_helper_vfp_mulxd,
};
TRANS(FMULX_s, do_fp3_scalar, a, &f_scalar_fmulx, a->rn)

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

static void gen_fnmul_ah_h(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_mulh(d, n, m, s);
    gen_vfp_ah_negh(d, d);
}

static void gen_fnmul_ah_s(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_muls(d, n, m, s);
    gen_vfp_ah_negs(d, d);
}

static void gen_fnmul_ah_d(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_ptr s)
{
    gen_helper_vfp_muld(d, n, m, s);
    gen_vfp_ah_negd(d, d);
}

static const FPScalar f_scalar_fnmul = {
    gen_fnmul_h,
    gen_fnmul_s,
    gen_fnmul_d,
};
static const FPScalar f_scalar_ah_fnmul = {
    gen_fnmul_ah_h,
    gen_fnmul_ah_s,
    gen_fnmul_ah_d,
};
TRANS(FNMUL_s, do_fp3_scalar_2fn, a, &f_scalar_fnmul, &f_scalar_ah_fnmul, a->rn)

static const FPScalar f_scalar_fcmeq = {
    gen_helper_advsimd_ceq_f16,
    gen_helper_neon_ceq_f32,
    gen_helper_neon_ceq_f64,
};
TRANS(FCMEQ_s, do_fp3_scalar, a, &f_scalar_fcmeq, a->rm)

static const FPScalar f_scalar_fcmge = {
    gen_helper_advsimd_cge_f16,
    gen_helper_neon_cge_f32,
    gen_helper_neon_cge_f64,
};
TRANS(FCMGE_s, do_fp3_scalar, a, &f_scalar_fcmge, a->rm)

static const FPScalar f_scalar_fcmgt = {
    gen_helper_advsimd_cgt_f16,
    gen_helper_neon_cgt_f32,
    gen_helper_neon_cgt_f64,
};
TRANS(FCMGT_s, do_fp3_scalar, a, &f_scalar_fcmgt, a->rm)

static const FPScalar f_scalar_facge = {
    gen_helper_advsimd_acge_f16,
    gen_helper_neon_acge_f32,
    gen_helper_neon_acge_f64,
};
TRANS(FACGE_s, do_fp3_scalar, a, &f_scalar_facge, a->rm)

static const FPScalar f_scalar_facgt = {
    gen_helper_advsimd_acgt_f16,
    gen_helper_neon_acgt_f32,
    gen_helper_neon_acgt_f64,
};
TRANS(FACGT_s, do_fp3_scalar, a, &f_scalar_facgt, a->rm)

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

static void gen_fabd_ah_h(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_subh(d, n, m, s);
    gen_vfp_ah_absh(d, d);
}

static void gen_fabd_ah_s(TCGv_i32 d, TCGv_i32 n, TCGv_i32 m, TCGv_ptr s)
{
    gen_helper_vfp_subs(d, n, m, s);
    gen_vfp_ah_abss(d, d);
}

static void gen_fabd_ah_d(TCGv_i64 d, TCGv_i64 n, TCGv_i64 m, TCGv_ptr s)
{
    gen_helper_vfp_subd(d, n, m, s);
    gen_vfp_ah_absd(d, d);
}

static const FPScalar f_scalar_fabd = {
    gen_fabd_h,
    gen_fabd_s,
    gen_fabd_d,
};
static const FPScalar f_scalar_ah_fabd = {
    gen_fabd_ah_h,
    gen_fabd_ah_s,
    gen_fabd_ah_d,
};
TRANS(FABD_s, do_fp3_scalar_2fn, a, &f_scalar_fabd, &f_scalar_ah_fabd, a->rn)

static const FPScalar f_scalar_frecps = {
    gen_helper_recpsf_f16,
    gen_helper_recpsf_f32,
    gen_helper_recpsf_f64,
};
static const FPScalar f_scalar_ah_frecps = {
    gen_helper_recpsf_ah_f16,
    gen_helper_recpsf_ah_f32,
    gen_helper_recpsf_ah_f64,
};
TRANS(FRECPS_s, do_fp3_scalar_ah_2fn, a,
      &f_scalar_frecps, &f_scalar_ah_frecps, a->rn)

static const FPScalar f_scalar_frsqrts = {
    gen_helper_rsqrtsf_f16,
    gen_helper_rsqrtsf_f32,
    gen_helper_rsqrtsf_f64,
};
static const FPScalar f_scalar_ah_frsqrts = {
    gen_helper_rsqrtsf_ah_f16,
    gen_helper_rsqrtsf_ah_f32,
    gen_helper_rsqrtsf_ah_f64,
};
TRANS(FRSQRTS_s, do_fp3_scalar_ah_2fn, a,
      &f_scalar_frsqrts, &f_scalar_ah_frsqrts, a->rn)

static bool do_fcmp0_s(DisasContext *s, arg_rr_e *a,
                       const FPScalar *f, bool swap)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t0 = read_fp_dreg(s, a->rn);
            TCGv_i64 t1 = tcg_constant_i64(0);
            if (swap) {
                f->gen_d(t0, t1, t0, fpstatus_ptr(FPST_A64));
            } else {
                f->gen_d(t0, t0, t1, fpstatus_ptr(FPST_A64));
            }
            write_fp_dreg(s, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rn);
            TCGv_i32 t1 = tcg_constant_i32(0);
            if (swap) {
                f->gen_s(t0, t1, t0, fpstatus_ptr(FPST_A64));
            } else {
                f->gen_s(t0, t0, t1, fpstatus_ptr(FPST_A64));
            }
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_hreg(s, a->rn);
            TCGv_i32 t1 = tcg_constant_i32(0);
            if (swap) {
                f->gen_h(t0, t1, t0, fpstatus_ptr(FPST_A64_F16));
            } else {
                f->gen_h(t0, t0, t1, fpstatus_ptr(FPST_A64_F16));
            }
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    default:
        return false;
    }
    return true;
}

TRANS(FCMEQ0_s, do_fcmp0_s, a, &f_scalar_fcmeq, false)
TRANS(FCMGT0_s, do_fcmp0_s, a, &f_scalar_fcmgt, false)
TRANS(FCMGE0_s, do_fcmp0_s, a, &f_scalar_fcmge, false)
TRANS(FCMLT0_s, do_fcmp0_s, a, &f_scalar_fcmgt, true)
TRANS(FCMLE0_s, do_fcmp0_s, a, &f_scalar_fcmge, true)

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

static bool do_fp3_vector_with_fpsttype(DisasContext *s, arg_qrrr_e *a,
                                        int data,
                                        gen_helper_gvec_3_ptr * const fns[3],
                                        ARMFPStatusFlavour fpsttype)
{
    MemOp esz = a->esz;
    int check = fp_access_check_vector_hsd(s, a->q, esz);

    if (check <= 0) {
        return check == 0;
    }

    gen_gvec_op3_fpst(s, a->q, a->rd, a->rn, a->rm, fpsttype,
                      data, fns[esz - 1]);
    return true;
}

static bool do_fp3_vector(DisasContext *s, arg_qrrr_e *a, int data,
                          gen_helper_gvec_3_ptr * const fns[3])
{
    return do_fp3_vector_with_fpsttype(s, a, data, fns,
                                       a->esz == MO_16 ?
                                       FPST_A64_F16 : FPST_A64);
}

static bool do_fp3_vector_2fn(DisasContext *s, arg_qrrr_e *a, int data,
                              gen_helper_gvec_3_ptr * const fnormal[3],
                              gen_helper_gvec_3_ptr * const fah[3])
{
    return do_fp3_vector(s, a, data, s->fpcr_ah ? fah : fnormal);
}

static bool do_fp3_vector_ah_2fn(DisasContext *s, arg_qrrr_e *a, int data,
                                 gen_helper_gvec_3_ptr * const fnormal[3],
                                 gen_helper_gvec_3_ptr * const fah[3])
{
    return do_fp3_vector_with_fpsttype(s, a, data, s->fpcr_ah ? fah : fnormal,
                                       select_ah_fpst(s, a->esz));
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
static gen_helper_gvec_3_ptr * const f_vector_fmax_ah[3] = {
    gen_helper_gvec_ah_fmax_h,
    gen_helper_gvec_ah_fmax_s,
    gen_helper_gvec_ah_fmax_d,
};
TRANS(FMAX_v, do_fp3_vector_2fn, a, 0, f_vector_fmax, f_vector_fmax_ah)

static gen_helper_gvec_3_ptr * const f_vector_fmin[3] = {
    gen_helper_gvec_fmin_h,
    gen_helper_gvec_fmin_s,
    gen_helper_gvec_fmin_d,
};
static gen_helper_gvec_3_ptr * const f_vector_fmin_ah[3] = {
    gen_helper_gvec_ah_fmin_h,
    gen_helper_gvec_ah_fmin_s,
    gen_helper_gvec_ah_fmin_d,
};
TRANS(FMIN_v, do_fp3_vector_2fn, a, 0, f_vector_fmin, f_vector_fmin_ah)

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
static gen_helper_gvec_3_ptr * const f_vector_fmls_ah[3] = {
    gen_helper_gvec_ah_vfms_h,
    gen_helper_gvec_ah_vfms_s,
    gen_helper_gvec_ah_vfms_d,
};
TRANS(FMLS_v, do_fp3_vector_2fn, a, 0, f_vector_fmls, f_vector_fmls_ah)

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
static gen_helper_gvec_3_ptr * const f_vector_ah_fabd[3] = {
    gen_helper_gvec_ah_fabd_h,
    gen_helper_gvec_ah_fabd_s,
    gen_helper_gvec_ah_fabd_d,
};
TRANS(FABD_v, do_fp3_vector_2fn, a, 0, f_vector_fabd, f_vector_ah_fabd)

static gen_helper_gvec_3_ptr * const f_vector_frecps[3] = {
    gen_helper_gvec_recps_h,
    gen_helper_gvec_recps_s,
    gen_helper_gvec_recps_d,
};
static gen_helper_gvec_3_ptr * const f_vector_ah_frecps[3] = {
    gen_helper_gvec_ah_recps_h,
    gen_helper_gvec_ah_recps_s,
    gen_helper_gvec_ah_recps_d,
};
TRANS(FRECPS_v, do_fp3_vector_ah_2fn, a, 0, f_vector_frecps, f_vector_ah_frecps)

static gen_helper_gvec_3_ptr * const f_vector_frsqrts[3] = {
    gen_helper_gvec_rsqrts_h,
    gen_helper_gvec_rsqrts_s,
    gen_helper_gvec_rsqrts_d,
};
static gen_helper_gvec_3_ptr * const f_vector_ah_frsqrts[3] = {
    gen_helper_gvec_ah_rsqrts_h,
    gen_helper_gvec_ah_rsqrts_s,
    gen_helper_gvec_ah_rsqrts_d,
};
TRANS(FRSQRTS_v, do_fp3_vector_ah_2fn, a, 0, f_vector_frsqrts, f_vector_ah_frsqrts)

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
static gen_helper_gvec_3_ptr * const f_vector_ah_fmaxp[3] = {
    gen_helper_gvec_ah_fmaxp_h,
    gen_helper_gvec_ah_fmaxp_s,
    gen_helper_gvec_ah_fmaxp_d,
};
TRANS(FMAXP_v, do_fp3_vector_2fn, a, 0, f_vector_fmaxp, f_vector_ah_fmaxp)

static gen_helper_gvec_3_ptr * const f_vector_fminp[3] = {
    gen_helper_gvec_fminp_h,
    gen_helper_gvec_fminp_s,
    gen_helper_gvec_fminp_d,
};
static gen_helper_gvec_3_ptr * const f_vector_ah_fminp[3] = {
    gen_helper_gvec_ah_fminp_h,
    gen_helper_gvec_ah_fminp_s,
    gen_helper_gvec_ah_fminp_d,
};
TRANS(FMINP_v, do_fp3_vector_2fn, a, 0, f_vector_fminp, f_vector_ah_fminp)

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

static bool do_dot_vector_env(DisasContext *s, arg_qrrr_e *a,
                              gen_helper_gvec_4_ptr *fn)
{
    if (fp_access_check(s)) {
        gen_gvec_op4_env(s, a->q, a->rd, a->rn, a->rm, a->rd, 0, fn);
    }
    return true;
}

TRANS_FEAT(SDOT_v, aa64_dp, do_dot_vector, a, gen_helper_gvec_sdot_4b)
TRANS_FEAT(UDOT_v, aa64_dp, do_dot_vector, a, gen_helper_gvec_udot_4b)
TRANS_FEAT(USDOT_v, aa64_i8mm, do_dot_vector, a, gen_helper_gvec_usdot_4b)
TRANS_FEAT(BFDOT_v, aa64_bf16, do_dot_vector_env, a, gen_helper_gvec_bfdot)
TRANS_FEAT(BFMMLA, aa64_bf16, do_dot_vector_env, a, gen_helper_gvec_bfmmla)
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
        gen_gvec_op4_fpst(s, true, a->rd, a->rn, a->rm, a->rd,
                          s->fpcr_ah ? FPST_AH : FPST_A64, a->q,
                          gen_helper_gvec_bfmlal);
    }
    return true;
}

static gen_helper_gvec_3_ptr * const f_vector_fcadd[3] = {
    gen_helper_gvec_fcaddh,
    gen_helper_gvec_fcadds,
    gen_helper_gvec_fcaddd,
};
/*
 * Encode FPCR.AH into the data so the helper knows whether the
 * negations it does should avoid flipping the sign bit on a NaN
 */
TRANS_FEAT(FCADD_90, aa64_fcma, do_fp3_vector, a, 0 | (s->fpcr_ah << 1),
           f_vector_fcadd)
TRANS_FEAT(FCADD_270, aa64_fcma, do_fp3_vector, a, 1 | (s->fpcr_ah << 1),
           f_vector_fcadd)

static bool trans_FCMLA_v(DisasContext *s, arg_FCMLA_v *a)
{
    static gen_helper_gvec_4_ptr * const fn[] = {
        [MO_16] = gen_helper_gvec_fcmlah,
        [MO_32] = gen_helper_gvec_fcmlas,
        [MO_64] = gen_helper_gvec_fcmlad,
    };
    int check;

    if (!dc_isar_feature(aa64_fcma, s)) {
        return false;
    }

    check = fp_access_check_vector_hsd(s, a->q, a->esz);
    if (check <= 0) {
        return check == 0;
    }

    gen_gvec_op4_fpst(s, a->q, a->rd, a->rn, a->rm, a->rd,
                      a->esz == MO_16 ? FPST_A64_F16 : FPST_A64,
                      a->rot | (s->fpcr_ah << 2), fn[a->esz]);
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
            f->gen_d(t0, t0, t1, fpstatus_ptr(FPST_A64));
            write_fp_dreg_merging(s, a->rd, a->rn, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rn);
            TCGv_i32 t1 = tcg_temp_new_i32();

            read_vec_element_i32(s, t1, a->rm, a->idx, MO_32);
            f->gen_s(t0, t0, t1, fpstatus_ptr(FPST_A64));
            write_fp_sreg_merging(s, a->rd, a->rn, t0);
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
            f->gen_h(t0, t0, t1, fpstatus_ptr(FPST_A64_F16));
            write_fp_hreg_merging(s, a->rd, a->rn, t0);
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
                gen_vfp_maybe_ah_negd(s, t1, t1);
            }
            gen_helper_vfp_muladdd(t0, t1, t2, t0, fpstatus_ptr(FPST_A64));
            write_fp_dreg_merging(s, a->rd, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = read_fp_sreg(s, a->rd);
            TCGv_i32 t1 = read_fp_sreg(s, a->rn);
            TCGv_i32 t2 = tcg_temp_new_i32();

            read_vec_element_i32(s, t2, a->rm, a->idx, MO_32);
            if (neg) {
                gen_vfp_maybe_ah_negs(s, t1, t1);
            }
            gen_helper_vfp_muladds(t0, t1, t2, t0, fpstatus_ptr(FPST_A64));
            write_fp_sreg_merging(s, a->rd, a->rd, t0);
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
                gen_vfp_maybe_ah_negh(s, t1, t1);
            }
            gen_helper_advsimd_muladdh(t0, t1, t2, t0,
                                       fpstatus_ptr(FPST_A64_F16));
            write_fp_hreg_merging(s, a->rd, a->rd, t0);
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

        if (acc) {
            read_vec_element(s, t0, a->rd, 0, a->esz + 1);
        }
        read_vec_element(s, t1, a->rn, 0, a->esz | MO_SIGN);
        read_vec_element(s, t2, a->rm, a->idx, a->esz | MO_SIGN);
        fn(t0, t1, t2);

        /* Clear the whole register first, then store scalar. */
        clear_vec(s, a->rd);
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
    int check = fp_access_check_vector_hsd(s, a->q, esz);

    if (check <= 0) {
        return check == 0;
    }

    gen_gvec_op3_fpst(s, a->q, a->rd, a->rn, a->rm,
                      esz == MO_16 ? FPST_A64_F16 : FPST_A64,
                      a->idx, fns[esz - 1]);
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
    static gen_helper_gvec_4_ptr * const fns[3][3] = {
        { gen_helper_gvec_fmla_idx_h,
          gen_helper_gvec_fmla_idx_s,
          gen_helper_gvec_fmla_idx_d },
        { gen_helper_gvec_fmls_idx_h,
          gen_helper_gvec_fmls_idx_s,
          gen_helper_gvec_fmls_idx_d },
        { gen_helper_gvec_ah_fmls_idx_h,
          gen_helper_gvec_ah_fmls_idx_s,
          gen_helper_gvec_ah_fmls_idx_d },
    };
    MemOp esz = a->esz;
    int check = fp_access_check_vector_hsd(s, a->q, esz);

    if (check <= 0) {
        return check == 0;
    }

    gen_gvec_op4_fpst(s, a->q, a->rd, a->rn, a->rm, a->rd,
                      esz == MO_16 ? FPST_A64_F16 : FPST_A64,
                      a->idx, fns[neg ? 1 + s->fpcr_ah : 0][esz - 1]);
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

static bool do_dot_vector_idx_env(DisasContext *s, arg_qrrx_e *a,
                                  gen_helper_gvec_4_ptr *fn)
{
    if (fp_access_check(s)) {
        gen_gvec_op4_env(s, a->q, a->rd, a->rn, a->rm, a->rd, a->idx, fn);
    }
    return true;
}

TRANS_FEAT(SDOT_vi, aa64_dp, do_dot_vector_idx, a, gen_helper_gvec_sdot_idx_4b)
TRANS_FEAT(UDOT_vi, aa64_dp, do_dot_vector_idx, a, gen_helper_gvec_udot_idx_4b)
TRANS_FEAT(SUDOT_vi, aa64_i8mm, do_dot_vector_idx, a,
           gen_helper_gvec_sudot_idx_4b)
TRANS_FEAT(USDOT_vi, aa64_i8mm, do_dot_vector_idx, a,
           gen_helper_gvec_usdot_idx_4b)
TRANS_FEAT(BFDOT_vi, aa64_bf16, do_dot_vector_idx_env, a,
           gen_helper_gvec_bfdot_idx)

static bool trans_BFMLAL_vi(DisasContext *s, arg_qrrx_e *a)
{
    if (!dc_isar_feature(aa64_bf16, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        /* Q bit selects BFMLALB vs BFMLALT. */
        gen_gvec_op4_fpst(s, true, a->rd, a->rn, a->rm, a->rd,
                          s->fpcr_ah ? FPST_AH : FPST_A64,
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
                          a->esz == MO_16 ? FPST_A64_F16 : FPST_A64,
                          (s->fpcr_ah << 4) | (a->idx << 2) | a->rot, fn);
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
            f->gen_d(t0, t0, t1, fpstatus_ptr(FPST_A64));
            write_fp_dreg(s, a->rd, t0);
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t0 = tcg_temp_new_i32();
            TCGv_i32 t1 = tcg_temp_new_i32();

            read_vec_element_i32(s, t0, a->rn, 0, MO_32);
            read_vec_element_i32(s, t1, a->rn, 1, MO_32);
            f->gen_s(t0, t0, t1, fpstatus_ptr(FPST_A64));
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
            f->gen_h(t0, t0, t1, fpstatus_ptr(FPST_A64_F16));
            write_fp_sreg(s, a->rd, t0);
        }
        break;
    default:
        g_assert_not_reached();
    }
    return true;
}

static bool do_fp3_scalar_pair_2fn(DisasContext *s, arg_rr_e *a,
                                   const FPScalar *fnormal,
                                   const FPScalar *fah)
{
    return do_fp3_scalar_pair(s, a, s->fpcr_ah ? fah : fnormal);
}

TRANS(FADDP_s, do_fp3_scalar_pair, a, &f_scalar_fadd)
TRANS(FMAXP_s, do_fp3_scalar_pair_2fn, a, &f_scalar_fmax, &f_scalar_fmax_ah)
TRANS(FMINP_s, do_fp3_scalar_pair_2fn, a, &f_scalar_fmin, &f_scalar_fmin_ah)
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
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
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
 * Advanced SIMD Extract
 */

static bool trans_EXT_d(DisasContext *s, arg_EXT_d *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 lo = read_fp_dreg(s, a->rn);
        if (a->imm != 0) {
            TCGv_i64 hi = read_fp_dreg(s, a->rm);
            tcg_gen_extract2_i64(lo, lo, hi, a->imm * 8);
        }
        write_fp_dreg(s, a->rd, lo);
    }
    return true;
}

static bool trans_EXT_q(DisasContext *s, arg_EXT_q *a)
{
    TCGv_i64 lo, hi;
    int pos = (a->imm & 7) * 8;
    int elt = a->imm >> 3;

    if (!fp_access_check(s)) {
        return true;
    }

    lo = tcg_temp_new_i64();
    hi = tcg_temp_new_i64();

    read_vec_element(s, lo, a->rn, elt, MO_64);
    elt++;
    read_vec_element(s, hi, elt & 2 ? a->rm : a->rn, elt & 1, MO_64);
    elt++;

    if (pos != 0) {
        TCGv_i64 hh = tcg_temp_new_i64();
        tcg_gen_extract2_i64(lo, lo, hi, pos);
        read_vec_element(s, hh, a->rm, elt & 1, MO_64);
        tcg_gen_extract2_i64(hi, hi, hh, pos);
    }

    write_vec_element(s, lo, a->rd, 0, MO_64);
    write_vec_element(s, hi, a->rd, 1, MO_64);
    clear_vec_high(s, true, a->rd);
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
                gen_vfp_maybe_ah_negd(s, ta, ta);
            }
            if (neg_n) {
                gen_vfp_maybe_ah_negd(s, tn, tn);
            }
            fpst = fpstatus_ptr(FPST_A64);
            gen_helper_vfp_muladdd(ta, tn, tm, ta, fpst);
            write_fp_dreg_merging(s, a->rd, a->ra, ta);
        }
        break;

    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 tn = read_fp_sreg(s, a->rn);
            TCGv_i32 tm = read_fp_sreg(s, a->rm);
            TCGv_i32 ta = read_fp_sreg(s, a->ra);

            if (neg_a) {
                gen_vfp_maybe_ah_negs(s, ta, ta);
            }
            if (neg_n) {
                gen_vfp_maybe_ah_negs(s, tn, tn);
            }
            fpst = fpstatus_ptr(FPST_A64);
            gen_helper_vfp_muladds(ta, tn, tm, ta, fpst);
            write_fp_sreg_merging(s, a->rd, a->ra, ta);
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
                gen_vfp_maybe_ah_negh(s, ta, ta);
            }
            if (neg_n) {
                gen_vfp_maybe_ah_negh(s, tn, tn);
            }
            fpst = fpstatus_ptr(FPST_A64_F16);
            gen_helper_advsimd_muladdh(ta, tn, tm, ta, fpst);
            write_fp_hreg_merging(s, a->rd, a->ra, ta);
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

/*
 * Advanced SIMD Across Lanes
 */

static bool do_int_reduction(DisasContext *s, arg_qrr_e *a, bool widen,
                             MemOp src_sign, NeonGenTwo64OpFn *fn)
{
    TCGv_i64 tcg_res, tcg_elt;
    MemOp src_mop = a->esz | src_sign;
    int elements = (a->q ? 16 : 8) >> a->esz;

    /* Reject MO_64, and MO_32 without Q: a minimum of 4 elements. */
    if (elements < 4) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    tcg_res = tcg_temp_new_i64();
    tcg_elt = tcg_temp_new_i64();

    read_vec_element(s, tcg_res, a->rn, 0, src_mop);
    for (int i = 1; i < elements; i++) {
        read_vec_element(s, tcg_elt, a->rn, i, src_mop);
        fn(tcg_res, tcg_res, tcg_elt);
    }

    tcg_gen_ext_i64(tcg_res, tcg_res, a->esz + widen);
    write_fp_dreg(s, a->rd, tcg_res);
    return true;
}

TRANS(ADDV, do_int_reduction, a, false, 0, tcg_gen_add_i64)
TRANS(SADDLV, do_int_reduction, a, true, MO_SIGN, tcg_gen_add_i64)
TRANS(UADDLV, do_int_reduction, a, true, 0, tcg_gen_add_i64)
TRANS(SMAXV, do_int_reduction, a, false, MO_SIGN, tcg_gen_smax_i64)
TRANS(UMAXV, do_int_reduction, a, false, 0, tcg_gen_umax_i64)
TRANS(SMINV, do_int_reduction, a, false, MO_SIGN, tcg_gen_smin_i64)
TRANS(UMINV, do_int_reduction, a, false, 0, tcg_gen_umin_i64)

/*
 * do_fp_reduction helper
 *
 * This mirrors the Reduce() pseudocode in the ARM ARM. It is
 * important for correct NaN propagation that we do these
 * operations in exactly the order specified by the pseudocode.
 *
 * This is a recursive function.
 */
static TCGv_i32 do_reduction_op(DisasContext *s, int rn, MemOp esz,
                                int ebase, int ecount, TCGv_ptr fpst,
                                NeonGenTwoSingleOpFn *fn)
{
    if (ecount == 1) {
        TCGv_i32 tcg_elem = tcg_temp_new_i32();
        read_vec_element_i32(s, tcg_elem, rn, ebase, esz);
        return tcg_elem;
    } else {
        int half = ecount >> 1;
        TCGv_i32 tcg_hi, tcg_lo, tcg_res;

        tcg_hi = do_reduction_op(s, rn, esz, ebase + half, half, fpst, fn);
        tcg_lo = do_reduction_op(s, rn, esz, ebase, half, fpst, fn);
        tcg_res = tcg_temp_new_i32();

        fn(tcg_res, tcg_lo, tcg_hi, fpst);
        return tcg_res;
    }
}

static bool do_fp_reduction(DisasContext *s, arg_qrr_e *a,
                            NeonGenTwoSingleOpFn *fnormal,
                            NeonGenTwoSingleOpFn *fah)
{
    if (fp_access_check(s)) {
        MemOp esz = a->esz;
        int elts = (a->q ? 16 : 8) >> esz;
        TCGv_ptr fpst = fpstatus_ptr(esz == MO_16 ? FPST_A64_F16 : FPST_A64);
        TCGv_i32 res = do_reduction_op(s, a->rn, esz, 0, elts, fpst,
                                       s->fpcr_ah ? fah : fnormal);
        write_fp_sreg(s, a->rd, res);
    }
    return true;
}

TRANS_FEAT(FMAXNMV_h, aa64_fp16, do_fp_reduction, a,
           gen_helper_vfp_maxnumh, gen_helper_vfp_maxnumh)
TRANS_FEAT(FMINNMV_h, aa64_fp16, do_fp_reduction, a,
           gen_helper_vfp_minnumh, gen_helper_vfp_minnumh)
TRANS_FEAT(FMAXV_h, aa64_fp16, do_fp_reduction, a,
           gen_helper_vfp_maxh, gen_helper_vfp_ah_maxh)
TRANS_FEAT(FMINV_h, aa64_fp16, do_fp_reduction, a,
           gen_helper_vfp_minh, gen_helper_vfp_ah_minh)

TRANS(FMAXNMV_s, do_fp_reduction, a,
      gen_helper_vfp_maxnums, gen_helper_vfp_maxnums)
TRANS(FMINNMV_s, do_fp_reduction, a,
      gen_helper_vfp_minnums, gen_helper_vfp_minnums)
TRANS(FMAXV_s, do_fp_reduction, a, gen_helper_vfp_maxs, gen_helper_vfp_ah_maxs)
TRANS(FMINV_s, do_fp_reduction, a, gen_helper_vfp_mins, gen_helper_vfp_ah_mins)

/*
 * Floating-point Immediate
 */

static bool trans_FMOVI_s(DisasContext *s, arg_FMOVI_s *a)
{
    int check = fp_access_check_scalar_hsd(s, a->esz);
    uint64_t imm;

    if (check <= 0) {
        return check == 0;
    }

    imm = vfp_expand_imm(a->esz, a->imm);
    write_fp_dreg(s, a->rd, tcg_constant_i64(imm));
    return true;
}

/*
 * Floating point compare, conditional compare
 */

static void handle_fp_compare(DisasContext *s, int size,
                              unsigned int rn, unsigned int rm,
                              bool cmp_with_zero, bool signal_all_nans)
{
    TCGv_i64 tcg_flags = tcg_temp_new_i64();
    TCGv_ptr fpst = fpstatus_ptr(size == MO_16 ? FPST_A64_F16 : FPST_A64);

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

/* FCMP, FCMPE */
static bool trans_FCMP(DisasContext *s, arg_FCMP *a)
{
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    handle_fp_compare(s, a->esz, a->rn, a->rm, a->z, a->e);
    return true;
}

/* FCCMP, FCCMPE */
static bool trans_FCCMP(DisasContext *s, arg_FCCMP *a)
{
    TCGLabel *label_continue = NULL;
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    if (a->cond < 0x0e) { /* not always */
        TCGLabel *label_match = gen_new_label();
        label_continue = gen_new_label();
        arm_gen_test_cc(a->cond, label_match);
        /* nomatch: */
        gen_set_nzcv(tcg_constant_i64(a->nzcv << 28));
        tcg_gen_br(label_continue);
        gen_set_label(label_match);
    }

    handle_fp_compare(s, a->esz, a->rn, a->rm, false, a->e);

    if (label_continue) {
        gen_set_label(label_continue);
    }
    return true;
}

/*
 * Advanced SIMD Modified Immediate
 */

static bool trans_FMOVI_v_h(DisasContext *s, arg_FMOVI_v_h *a)
{
    if (!dc_isar_feature(aa64_fp16, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        tcg_gen_gvec_dup_imm(MO_16, vec_full_reg_offset(s, a->rd),
                             a->q ? 16 : 8, vec_full_reg_size(s),
                             vfp_expand_imm(MO_16, a->abcdefgh));
    }
    return true;
}

static void gen_movi(unsigned vece, uint32_t dofs, uint32_t aofs,
                     int64_t c, uint32_t oprsz, uint32_t maxsz)
{
    tcg_gen_gvec_dup_imm(MO_64, dofs, oprsz, maxsz, c);
}

static bool trans_Vimm(DisasContext *s, arg_Vimm *a)
{
    GVecGen2iFn *fn;

    /* Handle decode of cmode/op here between ORR/BIC/MOVI */
    if ((a->cmode & 1) && a->cmode < 12) {
        /* For op=1, the imm will be inverted, so BIC becomes AND. */
        fn = a->op ? tcg_gen_gvec_andi : tcg_gen_gvec_ori;
    } else {
        /* There is one unallocated cmode/op combination in this space */
        if (a->cmode == 15 && a->op == 1 && a->q == 0) {
            return false;
        }
        fn = gen_movi;
    }

    if (fp_access_check(s)) {
        uint64_t imm = asimd_imm_const(a->abcdefgh, a->cmode, a->op);
        gen_gvec_fn2i(s, a->q, a->rd, a->rd, imm, fn, MO_64);
    }
    return true;
}

/*
 * Advanced SIMD Shift by Immediate
 */

static bool do_vec_shift_imm(DisasContext *s, arg_qrri_e *a, GVecGen2iFn *fn)
{
    if (fp_access_check(s)) {
        gen_gvec_fn2i(s, a->q, a->rd, a->rn, a->imm, fn, a->esz);
    }
    return true;
}

TRANS(SSHR_v, do_vec_shift_imm, a, gen_gvec_sshr)
TRANS(USHR_v, do_vec_shift_imm, a, gen_gvec_ushr)
TRANS(SSRA_v, do_vec_shift_imm, a, gen_gvec_ssra)
TRANS(USRA_v, do_vec_shift_imm, a, gen_gvec_usra)
TRANS(SRSHR_v, do_vec_shift_imm, a, gen_gvec_srshr)
TRANS(URSHR_v, do_vec_shift_imm, a, gen_gvec_urshr)
TRANS(SRSRA_v, do_vec_shift_imm, a, gen_gvec_srsra)
TRANS(URSRA_v, do_vec_shift_imm, a, gen_gvec_ursra)
TRANS(SRI_v, do_vec_shift_imm, a, gen_gvec_sri)
TRANS(SHL_v, do_vec_shift_imm, a, tcg_gen_gvec_shli)
TRANS(SLI_v, do_vec_shift_imm, a, gen_gvec_sli);
TRANS(SQSHL_vi, do_vec_shift_imm, a, gen_neon_sqshli)
TRANS(UQSHL_vi, do_vec_shift_imm, a, gen_neon_uqshli)
TRANS(SQSHLU_vi, do_vec_shift_imm, a, gen_neon_sqshlui)

static bool do_vec_shift_imm_wide(DisasContext *s, arg_qrri_e *a, bool is_u)
{
    TCGv_i64 tcg_rn, tcg_rd;
    int esz = a->esz;
    int esize;

    if (!fp_access_check(s)) {
        return true;
    }

    /*
     * For the LL variants the store is larger than the load,
     * so if rd == rn we would overwrite parts of our input.
     * So load everything right now and use shifts in the main loop.
     */
    tcg_rd = tcg_temp_new_i64();
    tcg_rn = tcg_temp_new_i64();
    read_vec_element(s, tcg_rn, a->rn, a->q, MO_64);

    esize = 8 << esz;
    for (int i = 0, elements = 8 >> esz; i < elements; i++) {
        if (is_u) {
            tcg_gen_extract_i64(tcg_rd, tcg_rn, i * esize, esize);
        } else {
            tcg_gen_sextract_i64(tcg_rd, tcg_rn, i * esize, esize);
        }
        tcg_gen_shli_i64(tcg_rd, tcg_rd, a->imm);
        write_vec_element(s, tcg_rd, a->rd, i, esz + 1);
    }
    clear_vec_high(s, true, a->rd);
    return true;
}

TRANS(SSHLL_v, do_vec_shift_imm_wide, a, false)
TRANS(USHLL_v, do_vec_shift_imm_wide, a, true)

static void gen_sshr_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    assert(shift >= 0 && shift <= 64);
    tcg_gen_sari_i64(dst, src, MIN(shift, 63));
}

static void gen_ushr_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    assert(shift >= 0 && shift <= 64);
    if (shift == 64) {
        tcg_gen_movi_i64(dst, 0);
    } else {
        tcg_gen_shri_i64(dst, src, shift);
    }
}

static void gen_ssra_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    gen_sshr_d(src, src, shift);
    tcg_gen_add_i64(dst, dst, src);
}

static void gen_usra_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    gen_ushr_d(src, src, shift);
    tcg_gen_add_i64(dst, dst, src);
}

static void gen_srshr_bhs(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    assert(shift >= 0 && shift <= 32);
    if (shift) {
        TCGv_i64 rnd = tcg_constant_i64(1ull << (shift - 1));
        tcg_gen_add_i64(dst, src, rnd);
        tcg_gen_sari_i64(dst, dst, shift);
    } else {
        tcg_gen_mov_i64(dst, src);
    }
}

static void gen_urshr_bhs(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    assert(shift >= 0 && shift <= 32);
    if (shift) {
        TCGv_i64 rnd = tcg_constant_i64(1ull << (shift - 1));
        tcg_gen_add_i64(dst, src, rnd);
        tcg_gen_shri_i64(dst, dst, shift);
    } else {
        tcg_gen_mov_i64(dst, src);
    }
}

static void gen_srshr_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    assert(shift >= 0 && shift <= 64);
    if (shift == 0) {
        tcg_gen_mov_i64(dst, src);
    } else if (shift == 64) {
        /* Extension of sign bit (0,-1) plus sign bit (0,1) is zero. */
        tcg_gen_movi_i64(dst, 0);
    } else {
        TCGv_i64 rnd = tcg_temp_new_i64();
        tcg_gen_extract_i64(rnd, src, shift - 1, 1);
        tcg_gen_sari_i64(dst, src, shift);
        tcg_gen_add_i64(dst, dst, rnd);
    }
}

static void gen_urshr_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    assert(shift >= 0 && shift <= 64);
    if (shift == 0) {
        tcg_gen_mov_i64(dst, src);
    } else if (shift == 64) {
        /* Rounding will propagate bit 63 into bit 64. */
        tcg_gen_shri_i64(dst, src, 63);
    } else {
        TCGv_i64 rnd = tcg_temp_new_i64();
        tcg_gen_extract_i64(rnd, src, shift - 1, 1);
        tcg_gen_shri_i64(dst, src, shift);
        tcg_gen_add_i64(dst, dst, rnd);
    }
}

static void gen_srsra_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    gen_srshr_d(src, src, shift);
    tcg_gen_add_i64(dst, dst, src);
}

static void gen_ursra_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    gen_urshr_d(src, src, shift);
    tcg_gen_add_i64(dst, dst, src);
}

static void gen_sri_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    /* If shift is 64, dst is unchanged. */
    if (shift != 64) {
        tcg_gen_shri_i64(src, src, shift);
        tcg_gen_deposit_i64(dst, dst, src, 0, 64 - shift);
    }
}

static void gen_sli_d(TCGv_i64 dst, TCGv_i64 src, int64_t shift)
{
    tcg_gen_deposit_i64(dst, dst, src, shift, 64 - shift);
}

static bool do_vec_shift_imm_narrow(DisasContext *s, arg_qrri_e *a,
                                    WideShiftImmFn * const fns[3], MemOp sign)
{
    TCGv_i64 tcg_rn, tcg_rd;
    int esz = a->esz;
    int esize;
    WideShiftImmFn *fn;

    tcg_debug_assert(esz >= MO_8 && esz <= MO_32);

    if (!fp_access_check(s)) {
        return true;
    }

    tcg_rn = tcg_temp_new_i64();
    tcg_rd = tcg_temp_new_i64();
    tcg_gen_movi_i64(tcg_rd, 0);

    fn = fns[esz];
    esize = 8 << esz;
    for (int i = 0, elements = 8 >> esz; i < elements; i++) {
        read_vec_element(s, tcg_rn, a->rn, i, (esz + 1) | sign);
        fn(tcg_rn, tcg_rn, a->imm);
        tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_rn, esize * i, esize);
    }

    write_vec_element(s, tcg_rd, a->rd, a->q, MO_64);
    clear_vec_high(s, a->q, a->rd);
    return true;
}

static void gen_sqshrn_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    tcg_gen_sari_i64(d, s, i);
    tcg_gen_ext16u_i64(d, d);
    gen_helper_neon_narrow_sat_s8(d, tcg_env, d);
}

static void gen_sqshrn_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    tcg_gen_sari_i64(d, s, i);
    tcg_gen_ext32u_i64(d, d);
    gen_helper_neon_narrow_sat_s16(d, tcg_env, d);
}

static void gen_sqshrn_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_sshr_d(d, s, i);
    gen_helper_neon_narrow_sat_s32(d, tcg_env, d);
}

static void gen_uqshrn_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    tcg_gen_shri_i64(d, s, i);
    gen_helper_neon_narrow_sat_u8(d, tcg_env, d);
}

static void gen_uqshrn_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    tcg_gen_shri_i64(d, s, i);
    gen_helper_neon_narrow_sat_u16(d, tcg_env, d);
}

static void gen_uqshrn_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_ushr_d(d, s, i);
    gen_helper_neon_narrow_sat_u32(d, tcg_env, d);
}

static void gen_sqshrun_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    tcg_gen_sari_i64(d, s, i);
    tcg_gen_ext16u_i64(d, d);
    gen_helper_neon_unarrow_sat8(d, tcg_env, d);
}

static void gen_sqshrun_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    tcg_gen_sari_i64(d, s, i);
    tcg_gen_ext32u_i64(d, d);
    gen_helper_neon_unarrow_sat16(d, tcg_env, d);
}

static void gen_sqshrun_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_sshr_d(d, s, i);
    gen_helper_neon_unarrow_sat32(d, tcg_env, d);
}

static void gen_sqrshrn_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_srshr_bhs(d, s, i);
    tcg_gen_ext16u_i64(d, d);
    gen_helper_neon_narrow_sat_s8(d, tcg_env, d);
}

static void gen_sqrshrn_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_srshr_bhs(d, s, i);
    tcg_gen_ext32u_i64(d, d);
    gen_helper_neon_narrow_sat_s16(d, tcg_env, d);
}

static void gen_sqrshrn_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_srshr_d(d, s, i);
    gen_helper_neon_narrow_sat_s32(d, tcg_env, d);
}

static void gen_uqrshrn_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_urshr_bhs(d, s, i);
    gen_helper_neon_narrow_sat_u8(d, tcg_env, d);
}

static void gen_uqrshrn_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_urshr_bhs(d, s, i);
    gen_helper_neon_narrow_sat_u16(d, tcg_env, d);
}

static void gen_uqrshrn_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_urshr_d(d, s, i);
    gen_helper_neon_narrow_sat_u32(d, tcg_env, d);
}

static void gen_sqrshrun_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_srshr_bhs(d, s, i);
    tcg_gen_ext16u_i64(d, d);
    gen_helper_neon_unarrow_sat8(d, tcg_env, d);
}

static void gen_sqrshrun_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_srshr_bhs(d, s, i);
    tcg_gen_ext32u_i64(d, d);
    gen_helper_neon_unarrow_sat16(d, tcg_env, d);
}

static void gen_sqrshrun_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_srshr_d(d, s, i);
    gen_helper_neon_unarrow_sat32(d, tcg_env, d);
}

static WideShiftImmFn * const shrn_fns[] = {
    tcg_gen_shri_i64,
    tcg_gen_shri_i64,
    gen_ushr_d,
};
TRANS(SHRN_v, do_vec_shift_imm_narrow, a, shrn_fns, 0)

static WideShiftImmFn * const rshrn_fns[] = {
    gen_urshr_bhs,
    gen_urshr_bhs,
    gen_urshr_d,
};
TRANS(RSHRN_v, do_vec_shift_imm_narrow, a, rshrn_fns, 0)

static WideShiftImmFn * const sqshrn_fns[] = {
    gen_sqshrn_b,
    gen_sqshrn_h,
    gen_sqshrn_s,
};
TRANS(SQSHRN_v, do_vec_shift_imm_narrow, a, sqshrn_fns, MO_SIGN)

static WideShiftImmFn * const uqshrn_fns[] = {
    gen_uqshrn_b,
    gen_uqshrn_h,
    gen_uqshrn_s,
};
TRANS(UQSHRN_v, do_vec_shift_imm_narrow, a, uqshrn_fns, 0)

static WideShiftImmFn * const sqshrun_fns[] = {
    gen_sqshrun_b,
    gen_sqshrun_h,
    gen_sqshrun_s,
};
TRANS(SQSHRUN_v, do_vec_shift_imm_narrow, a, sqshrun_fns, MO_SIGN)

static WideShiftImmFn * const sqrshrn_fns[] = {
    gen_sqrshrn_b,
    gen_sqrshrn_h,
    gen_sqrshrn_s,
};
TRANS(SQRSHRN_v, do_vec_shift_imm_narrow, a, sqrshrn_fns, MO_SIGN)

static WideShiftImmFn * const uqrshrn_fns[] = {
    gen_uqrshrn_b,
    gen_uqrshrn_h,
    gen_uqrshrn_s,
};
TRANS(UQRSHRN_v, do_vec_shift_imm_narrow, a, uqrshrn_fns, 0)

static WideShiftImmFn * const sqrshrun_fns[] = {
    gen_sqrshrun_b,
    gen_sqrshrun_h,
    gen_sqrshrun_s,
};
TRANS(SQRSHRUN_v, do_vec_shift_imm_narrow, a, sqrshrun_fns, MO_SIGN)

/*
 * Advanced SIMD Scalar Shift by Immediate
 */

static bool do_scalar_shift_imm(DisasContext *s, arg_rri_e *a,
                                WideShiftImmFn *fn, bool accumulate,
                                MemOp sign)
{
    if (fp_access_check(s)) {
        TCGv_i64 rd = tcg_temp_new_i64();
        TCGv_i64 rn = tcg_temp_new_i64();

        read_vec_element(s, rn, a->rn, 0, a->esz | sign);
        if (accumulate) {
            read_vec_element(s, rd, a->rd, 0, a->esz | sign);
        }
        fn(rd, rn, a->imm);
        write_fp_dreg(s, a->rd, rd);
    }
    return true;
}

TRANS(SSHR_s, do_scalar_shift_imm, a, gen_sshr_d, false, 0)
TRANS(USHR_s, do_scalar_shift_imm, a, gen_ushr_d, false, 0)
TRANS(SSRA_s, do_scalar_shift_imm, a, gen_ssra_d, true, 0)
TRANS(USRA_s, do_scalar_shift_imm, a, gen_usra_d, true, 0)
TRANS(SRSHR_s, do_scalar_shift_imm, a, gen_srshr_d, false, 0)
TRANS(URSHR_s, do_scalar_shift_imm, a, gen_urshr_d, false, 0)
TRANS(SRSRA_s, do_scalar_shift_imm, a, gen_srsra_d, true, 0)
TRANS(URSRA_s, do_scalar_shift_imm, a, gen_ursra_d, true, 0)
TRANS(SRI_s, do_scalar_shift_imm, a, gen_sri_d, true, 0)

TRANS(SHL_s, do_scalar_shift_imm, a, tcg_gen_shli_i64, false, 0)
TRANS(SLI_s, do_scalar_shift_imm, a, gen_sli_d, true, 0)

static void trunc_i64_env_imm(TCGv_i64 d, TCGv_i64 s, int64_t i,
                              NeonGenTwoOpEnvFn *fn)
{
    TCGv_i32 t = tcg_temp_new_i32();
    tcg_gen_extrl_i64_i32(t, s);
    fn(t, tcg_env, t, tcg_constant_i32(i));
    tcg_gen_extu_i32_i64(d, t);
}

static void gen_sqshli_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshl_s8);
}

static void gen_sqshli_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshl_s16);
}

static void gen_sqshli_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshl_s32);
}

static void gen_sqshli_d(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_helper_neon_qshl_s64(d, tcg_env, s, tcg_constant_i64(i));
}

static void gen_uqshli_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshl_u8);
}

static void gen_uqshli_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshl_u16);
}

static void gen_uqshli_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshl_u32);
}

static void gen_uqshli_d(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_helper_neon_qshl_u64(d, tcg_env, s, tcg_constant_i64(i));
}

static void gen_sqshlui_b(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshlu_s8);
}

static void gen_sqshlui_h(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshlu_s16);
}

static void gen_sqshlui_s(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    trunc_i64_env_imm(d, s, i, gen_helper_neon_qshlu_s32);
}

static void gen_sqshlui_d(TCGv_i64 d, TCGv_i64 s, int64_t i)
{
    gen_helper_neon_qshlu_s64(d, tcg_env, s, tcg_constant_i64(i));
}

static WideShiftImmFn * const f_scalar_sqshli[] = {
    gen_sqshli_b, gen_sqshli_h, gen_sqshli_s, gen_sqshli_d
};

static WideShiftImmFn * const f_scalar_uqshli[] = {
    gen_uqshli_b, gen_uqshli_h, gen_uqshli_s, gen_uqshli_d
};

static WideShiftImmFn * const f_scalar_sqshlui[] = {
    gen_sqshlui_b, gen_sqshlui_h, gen_sqshlui_s, gen_sqshlui_d
};

/* Note that the helpers sign-extend their inputs, so don't do it here. */
TRANS(SQSHL_si, do_scalar_shift_imm, a, f_scalar_sqshli[a->esz], false, 0)
TRANS(UQSHL_si, do_scalar_shift_imm, a, f_scalar_uqshli[a->esz], false, 0)
TRANS(SQSHLU_si, do_scalar_shift_imm, a, f_scalar_sqshlui[a->esz], false, 0)

static bool do_scalar_shift_imm_narrow(DisasContext *s, arg_rri_e *a,
                                       WideShiftImmFn * const fns[3],
                                       MemOp sign, bool zext)
{
    MemOp esz = a->esz;

    tcg_debug_assert(esz >= MO_8 && esz <= MO_32);

    if (fp_access_check(s)) {
        TCGv_i64 rd = tcg_temp_new_i64();
        TCGv_i64 rn = tcg_temp_new_i64();

        read_vec_element(s, rn, a->rn, 0, (esz + 1) | sign);
        fns[esz](rd, rn, a->imm);
        if (zext) {
            tcg_gen_ext_i64(rd, rd, esz);
        }
        write_fp_dreg(s, a->rd, rd);
    }
    return true;
}

TRANS(SQSHRN_si, do_scalar_shift_imm_narrow, a, sqshrn_fns, MO_SIGN, true)
TRANS(SQRSHRN_si, do_scalar_shift_imm_narrow, a, sqrshrn_fns, MO_SIGN, true)
TRANS(UQSHRN_si, do_scalar_shift_imm_narrow, a, uqshrn_fns, 0, false)
TRANS(UQRSHRN_si, do_scalar_shift_imm_narrow, a, uqrshrn_fns, 0, false)
TRANS(SQSHRUN_si, do_scalar_shift_imm_narrow, a, sqshrun_fns, MO_SIGN, false)
TRANS(SQRSHRUN_si, do_scalar_shift_imm_narrow, a, sqrshrun_fns, MO_SIGN, false)

static bool do_div(DisasContext *s, arg_rrr_sf *a, bool is_signed)
{
    TCGv_i64 tcg_n, tcg_m, tcg_rd;
    tcg_rd = cpu_reg(s, a->rd);

    if (!a->sf && is_signed) {
        tcg_n = tcg_temp_new_i64();
        tcg_m = tcg_temp_new_i64();
        tcg_gen_ext32s_i64(tcg_n, cpu_reg(s, a->rn));
        tcg_gen_ext32s_i64(tcg_m, cpu_reg(s, a->rm));
    } else {
        tcg_n = read_cpu_reg(s, a->rn, a->sf);
        tcg_m = read_cpu_reg(s, a->rm, a->sf);
    }

    if (is_signed) {
        gen_helper_sdiv64(tcg_rd, tcg_n, tcg_m);
    } else {
        gen_helper_udiv64(tcg_rd, tcg_n, tcg_m);
    }

    if (!a->sf) { /* zero extend final result */
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

TRANS(SDIV, do_div, a, true)
TRANS(UDIV, do_div, a, false)

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

static bool do_shift_reg(DisasContext *s, arg_rrr_sf *a,
                         enum a64_shift_type shift_type)
{
    TCGv_i64 tcg_shift = tcg_temp_new_i64();
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 tcg_rn = read_cpu_reg(s, a->rn, a->sf);

    tcg_gen_andi_i64(tcg_shift, cpu_reg(s, a->rm), a->sf ? 63 : 31);
    shift_reg(tcg_rd, tcg_rn, a->sf, shift_type, tcg_shift);
    return true;
}

TRANS(LSLV, do_shift_reg, a, A64_SHIFT_TYPE_LSL)
TRANS(LSRV, do_shift_reg, a, A64_SHIFT_TYPE_LSR)
TRANS(ASRV, do_shift_reg, a, A64_SHIFT_TYPE_ASR)
TRANS(RORV, do_shift_reg, a, A64_SHIFT_TYPE_ROR)

static bool do_crc32(DisasContext *s, arg_rrr_e *a, bool crc32c)
{
    TCGv_i64 tcg_acc, tcg_val, tcg_rd;
    TCGv_i32 tcg_bytes;

    switch (a->esz) {
    case MO_8:
    case MO_16:
    case MO_32:
        tcg_val = tcg_temp_new_i64();
        tcg_gen_extract_i64(tcg_val, cpu_reg(s, a->rm), 0, 8 << a->esz);
        break;
    case MO_64:
        tcg_val = cpu_reg(s, a->rm);
        break;
    default:
        g_assert_not_reached();
    }
    tcg_acc = cpu_reg(s, a->rn);
    tcg_bytes = tcg_constant_i32(1 << a->esz);
    tcg_rd = cpu_reg(s, a->rd);

    if (crc32c) {
        gen_helper_crc32c_64(tcg_rd, tcg_acc, tcg_val, tcg_bytes);
    } else {
        gen_helper_crc32_64(tcg_rd, tcg_acc, tcg_val, tcg_bytes);
    }
    return true;
}

TRANS_FEAT(CRC32, aa64_crc32, do_crc32, a, false)
TRANS_FEAT(CRC32C, aa64_crc32, do_crc32, a, true)

static bool do_subp(DisasContext *s, arg_rrr *a, bool setflag)
{
    TCGv_i64 tcg_n = read_cpu_reg_sp(s, a->rn, true);
    TCGv_i64 tcg_m = read_cpu_reg_sp(s, a->rm, true);
    TCGv_i64 tcg_d = cpu_reg(s, a->rd);

    tcg_gen_sextract_i64(tcg_n, tcg_n, 0, 56);
    tcg_gen_sextract_i64(tcg_m, tcg_m, 0, 56);

    if (setflag) {
        gen_sub_CC(true, tcg_d, tcg_n, tcg_m);
    } else {
        tcg_gen_sub_i64(tcg_d, tcg_n, tcg_m);
    }
    return true;
}

TRANS_FEAT(SUBP, aa64_mte_insn_reg, do_subp, a, false)
TRANS_FEAT(SUBPS, aa64_mte_insn_reg, do_subp, a, true)

static bool trans_IRG(DisasContext *s, arg_rrr *a)
{
    if (dc_isar_feature(aa64_mte_insn_reg, s)) {
        TCGv_i64 tcg_rd = cpu_reg_sp(s, a->rd);
        TCGv_i64 tcg_rn = cpu_reg_sp(s, a->rn);

        if (s->ata[0]) {
            gen_helper_irg(tcg_rd, tcg_env, tcg_rn, cpu_reg(s, a->rm));
        } else {
            gen_address_with_allocation_tag0(tcg_rd, tcg_rn);
        }
        return true;
    }
    return false;
}

static bool trans_GMI(DisasContext *s, arg_rrr *a)
{
    if (dc_isar_feature(aa64_mte_insn_reg, s)) {
        TCGv_i64 t = tcg_temp_new_i64();

        tcg_gen_extract_i64(t, cpu_reg_sp(s, a->rn), 56, 4);
        tcg_gen_shl_i64(t, tcg_constant_i64(1), t);
        tcg_gen_or_i64(cpu_reg(s, a->rd), cpu_reg(s, a->rm), t);
        return true;
    }
    return false;
}

static bool trans_PACGA(DisasContext *s, arg_rrr *a)
{
    if (dc_isar_feature(aa64_pauth, s)) {
        gen_helper_pacga(cpu_reg(s, a->rd), tcg_env,
                         cpu_reg(s, a->rn), cpu_reg_sp(s, a->rm));
        return true;
    }
    return false;
}

static bool gen_rrr(DisasContext *s, arg_rrr_sf *a, ArithTwoOp fn)
{
    TCGv_i64 tcg_rm = cpu_reg(s, a->rm);
    TCGv_i64 tcg_rn = cpu_reg(s, a->rn);
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);

    fn(tcg_rd, tcg_rn, tcg_rm);
    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

TRANS_FEAT(SMAX, aa64_cssc, gen_rrr, a,
           a->sf ? tcg_gen_smax_i64 : gen_smax32_i64)
TRANS_FEAT(SMIN, aa64_cssc, gen_rrr, a,
           a->sf ? tcg_gen_smin_i64 : gen_smin32_i64)
TRANS_FEAT(UMAX, aa64_cssc, gen_rrr, a,
           a->sf ? tcg_gen_umax_i64 : gen_umax32_i64)
TRANS_FEAT(UMIN, aa64_cssc, gen_rrr, a,
           a->sf ? tcg_gen_umin_i64 : gen_umin32_i64)

typedef void ArithOneOp(TCGv_i64, TCGv_i64);

static bool gen_rr(DisasContext *s, int rd, int rn, ArithOneOp fn)
{
    fn(cpu_reg(s, rd), cpu_reg(s, rn));
    return true;
}

/*
 * Perform 32-bit operation fn on the low half of n;
 * the high half of the output is zeroed.
 */
static void gen_wrap2_i32(TCGv_i64 d, TCGv_i64 n, NeonGenOneOpFn fn)
{
    TCGv_i32 t = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t, n);
    fn(t, t);
    tcg_gen_extu_i32_i64(d, t);
}

static void gen_rbit32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    gen_wrap2_i32(tcg_rd, tcg_rn, gen_helper_rbit);
}

static void gen_rev16_xx(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn, TCGv_i64 mask)
{
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();

    tcg_gen_shri_i64(tcg_tmp, tcg_rn, 8);
    tcg_gen_and_i64(tcg_rd, tcg_rn, mask);
    tcg_gen_and_i64(tcg_tmp, tcg_tmp, mask);
    tcg_gen_shli_i64(tcg_rd, tcg_rd, 8);
    tcg_gen_or_i64(tcg_rd, tcg_rd, tcg_tmp);
}

static void gen_rev16_32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    gen_rev16_xx(tcg_rd, tcg_rn, tcg_constant_i64(0x00ff00ff));
}

static void gen_rev16_64(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    gen_rev16_xx(tcg_rd, tcg_rn, tcg_constant_i64(0x00ff00ff00ff00ffull));
}

static void gen_rev_32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    tcg_gen_bswap32_i64(tcg_rd, tcg_rn, TCG_BSWAP_OZ);
}

static void gen_rev32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    tcg_gen_bswap64_i64(tcg_rd, tcg_rn);
    tcg_gen_rotri_i64(tcg_rd, tcg_rd, 32);
}

TRANS(RBIT, gen_rr, a->rd, a->rn, a->sf ? gen_helper_rbit64 : gen_rbit32)
TRANS(REV16, gen_rr, a->rd, a->rn, a->sf ? gen_rev16_64 : gen_rev16_32)
TRANS(REV32, gen_rr, a->rd, a->rn, a->sf ? gen_rev32 : gen_rev_32)
TRANS(REV64, gen_rr, a->rd, a->rn, tcg_gen_bswap64_i64)

static void gen_clz32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    TCGv_i32 t32 = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t32, tcg_rn);
    tcg_gen_clzi_i32(t32, t32, 32);
    tcg_gen_extu_i32_i64(tcg_rd, t32);
}

static void gen_clz64(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    tcg_gen_clzi_i64(tcg_rd, tcg_rn, 64);
}

static void gen_cls32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    gen_wrap2_i32(tcg_rd, tcg_rn, tcg_gen_clrsb_i32);
}

TRANS(CLZ, gen_rr, a->rd, a->rn, a->sf ? gen_clz64 : gen_clz32)
TRANS(CLS, gen_rr, a->rd, a->rn, a->sf ? tcg_gen_clrsb_i64 : gen_cls32)

static void gen_ctz32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    TCGv_i32 t32 = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(t32, tcg_rn);
    tcg_gen_ctzi_i32(t32, t32, 32);
    tcg_gen_extu_i32_i64(tcg_rd, t32);
}

static void gen_ctz64(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    tcg_gen_ctzi_i64(tcg_rd, tcg_rn, 64);
}

static void gen_cnt32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    gen_wrap2_i32(tcg_rd, tcg_rn, tcg_gen_ctpop_i32);
}

static void gen_abs32(TCGv_i64 tcg_rd, TCGv_i64 tcg_rn)
{
    gen_wrap2_i32(tcg_rd, tcg_rn, tcg_gen_abs_i32);
}

TRANS_FEAT(CTZ, aa64_cssc, gen_rr, a->rd, a->rn,
           a->sf ? gen_ctz64 : gen_ctz32)
TRANS_FEAT(CNT, aa64_cssc, gen_rr, a->rd, a->rn,
           a->sf ? tcg_gen_ctpop_i64 : gen_cnt32)
TRANS_FEAT(ABS, aa64_cssc, gen_rr, a->rd, a->rn,
           a->sf ? tcg_gen_abs_i64 : gen_abs32)

static bool gen_pacaut(DisasContext *s, arg_pacaut *a, NeonGenTwo64OpEnvFn fn)
{
    TCGv_i64 tcg_rd, tcg_rn;

    if (a->z) {
        if (a->rn != 31) {
            return false;
        }
        tcg_rn = tcg_constant_i64(0);
    } else {
        tcg_rn = cpu_reg_sp(s, a->rn);
    }
    if (s->pauth_active) {
        tcg_rd = cpu_reg(s, a->rd);
        fn(tcg_rd, tcg_env, tcg_rd, tcg_rn);
    }
    return true;
}

TRANS_FEAT(PACIA, aa64_pauth, gen_pacaut, a, gen_helper_pacia)
TRANS_FEAT(PACIB, aa64_pauth, gen_pacaut, a, gen_helper_pacib)
TRANS_FEAT(PACDA, aa64_pauth, gen_pacaut, a, gen_helper_pacda)
TRANS_FEAT(PACDB, aa64_pauth, gen_pacaut, a, gen_helper_pacdb)

TRANS_FEAT(AUTIA, aa64_pauth, gen_pacaut, a, gen_helper_autia)
TRANS_FEAT(AUTIB, aa64_pauth, gen_pacaut, a, gen_helper_autib)
TRANS_FEAT(AUTDA, aa64_pauth, gen_pacaut, a, gen_helper_autda)
TRANS_FEAT(AUTDB, aa64_pauth, gen_pacaut, a, gen_helper_autdb)

static bool do_xpac(DisasContext *s, int rd, NeonGenOne64OpEnvFn *fn)
{
    if (s->pauth_active) {
        TCGv_i64 tcg_rd = cpu_reg(s, rd);
        fn(tcg_rd, tcg_env, tcg_rd);
    }
    return true;
}

TRANS_FEAT(XPACI, aa64_pauth, do_xpac, a->rd, gen_helper_xpaci)
TRANS_FEAT(XPACD, aa64_pauth, do_xpac, a->rd, gen_helper_xpacd)

static bool do_logic_reg(DisasContext *s, arg_logic_shift *a,
                         ArithTwoOp *fn, ArithTwoOp *inv_fn, bool setflags)
{
    TCGv_i64 tcg_rd, tcg_rn, tcg_rm;

    if (!a->sf && (a->sa & (1 << 5))) {
        return false;
    }

    tcg_rd = cpu_reg(s, a->rd);
    tcg_rn = cpu_reg(s, a->rn);

    tcg_rm = read_cpu_reg(s, a->rm, a->sf);
    if (a->sa) {
        shift_reg_imm(tcg_rm, tcg_rm, a->sf, a->st, a->sa);
    }

    (a->n ? inv_fn : fn)(tcg_rd, tcg_rn, tcg_rm);
    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    if (setflags) {
        gen_logic_CC(a->sf, tcg_rd);
    }
    return true;
}

static bool trans_ORR_r(DisasContext *s, arg_logic_shift *a)
{
    /*
     * Unshifted ORR and ORN with WZR/XZR is the standard encoding for
     * register-register MOV and MVN, so it is worth special casing.
     */
    if (a->sa == 0 && a->st == 0 && a->rn == 31) {
        TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
        TCGv_i64 tcg_rm = cpu_reg(s, a->rm);

        if (a->n) {
            tcg_gen_not_i64(tcg_rd, tcg_rm);
            if (!a->sf) {
                tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
            }
        } else {
            if (a->sf) {
                tcg_gen_mov_i64(tcg_rd, tcg_rm);
            } else {
                tcg_gen_ext32u_i64(tcg_rd, tcg_rm);
            }
        }
        return true;
    }

    return do_logic_reg(s, a, tcg_gen_or_i64, tcg_gen_orc_i64, false);
}

TRANS(AND_r, do_logic_reg, a, tcg_gen_and_i64, tcg_gen_andc_i64, false)
TRANS(ANDS_r, do_logic_reg, a, tcg_gen_and_i64, tcg_gen_andc_i64, true)
TRANS(EOR_r, do_logic_reg, a, tcg_gen_xor_i64, tcg_gen_eqv_i64, false)

static bool do_addsub_ext(DisasContext *s, arg_addsub_ext *a,
                          bool sub_op, bool setflags)
{
    TCGv_i64 tcg_rm, tcg_rn, tcg_rd, tcg_result;

    if (a->sa > 4) {
        return false;
    }

    /* non-flag setting ops may use SP */
    if (!setflags) {
        tcg_rd = cpu_reg_sp(s, a->rd);
    } else {
        tcg_rd = cpu_reg(s, a->rd);
    }
    tcg_rn = read_cpu_reg_sp(s, a->rn, a->sf);

    tcg_rm = read_cpu_reg(s, a->rm, a->sf);
    ext_and_shift_reg(tcg_rm, tcg_rm, a->st, a->sa);

    tcg_result = tcg_temp_new_i64();
    if (!setflags) {
        if (sub_op) {
            tcg_gen_sub_i64(tcg_result, tcg_rn, tcg_rm);
        } else {
            tcg_gen_add_i64(tcg_result, tcg_rn, tcg_rm);
        }
    } else {
        if (sub_op) {
            gen_sub_CC(a->sf, tcg_result, tcg_rn, tcg_rm);
        } else {
            gen_add_CC(a->sf, tcg_result, tcg_rn, tcg_rm);
        }
    }

    if (a->sf) {
        tcg_gen_mov_i64(tcg_rd, tcg_result);
    } else {
        tcg_gen_ext32u_i64(tcg_rd, tcg_result);
    }
    return true;
}

TRANS(ADD_ext, do_addsub_ext, a, false, false)
TRANS(SUB_ext, do_addsub_ext, a, true, false)
TRANS(ADDS_ext, do_addsub_ext, a, false, true)
TRANS(SUBS_ext, do_addsub_ext, a, true, true)

static bool do_addsub_reg(DisasContext *s, arg_addsub_shift *a,
                          bool sub_op, bool setflags)
{
    TCGv_i64 tcg_rd, tcg_rn, tcg_rm, tcg_result;

    if (a->st == 3 || (!a->sf && (a->sa & 32))) {
        return false;
    }

    tcg_rd = cpu_reg(s, a->rd);
    tcg_rn = read_cpu_reg(s, a->rn, a->sf);
    tcg_rm = read_cpu_reg(s, a->rm, a->sf);

    shift_reg_imm(tcg_rm, tcg_rm, a->sf, a->st, a->sa);

    tcg_result = tcg_temp_new_i64();
    if (!setflags) {
        if (sub_op) {
            tcg_gen_sub_i64(tcg_result, tcg_rn, tcg_rm);
        } else {
            tcg_gen_add_i64(tcg_result, tcg_rn, tcg_rm);
        }
    } else {
        if (sub_op) {
            gen_sub_CC(a->sf, tcg_result, tcg_rn, tcg_rm);
        } else {
            gen_add_CC(a->sf, tcg_result, tcg_rn, tcg_rm);
        }
    }

    if (a->sf) {
        tcg_gen_mov_i64(tcg_rd, tcg_result);
    } else {
        tcg_gen_ext32u_i64(tcg_rd, tcg_result);
    }
    return true;
}

TRANS(ADD_r, do_addsub_reg, a, false, false)
TRANS(SUB_r, do_addsub_reg, a, true, false)
TRANS(ADDS_r, do_addsub_reg, a, false, true)
TRANS(SUBS_r, do_addsub_reg, a, true, true)

static bool do_mulh(DisasContext *s, arg_rrr *a,
                    void (*fn)(TCGv_i64, TCGv_i64, TCGv_i64, TCGv_i64))
{
    TCGv_i64 discard = tcg_temp_new_i64();
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 tcg_rn = cpu_reg(s, a->rn);
    TCGv_i64 tcg_rm = cpu_reg(s, a->rm);

    fn(discard, tcg_rd, tcg_rn, tcg_rm);
    return true;
}

TRANS(SMULH, do_mulh, a, tcg_gen_muls2_i64)
TRANS(UMULH, do_mulh, a, tcg_gen_mulu2_i64)

static bool do_muladd(DisasContext *s, arg_rrrr *a,
                      bool sf, bool is_sub, MemOp mop)
{
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 tcg_op1, tcg_op2;

    if (mop == MO_64) {
        tcg_op1 = cpu_reg(s, a->rn);
        tcg_op2 = cpu_reg(s, a->rm);
    } else {
        tcg_op1 = tcg_temp_new_i64();
        tcg_op2 = tcg_temp_new_i64();
        tcg_gen_ext_i64(tcg_op1, cpu_reg(s, a->rn), mop);
        tcg_gen_ext_i64(tcg_op2, cpu_reg(s, a->rm), mop);
    }

    if (a->ra == 31 && !is_sub) {
        /* Special-case MADD with rA == XZR; it is the standard MUL alias */
        tcg_gen_mul_i64(tcg_rd, tcg_op1, tcg_op2);
    } else {
        TCGv_i64 tcg_tmp = tcg_temp_new_i64();
        TCGv_i64 tcg_ra = cpu_reg(s, a->ra);

        tcg_gen_mul_i64(tcg_tmp, tcg_op1, tcg_op2);
        if (is_sub) {
            tcg_gen_sub_i64(tcg_rd, tcg_ra, tcg_tmp);
        } else {
            tcg_gen_add_i64(tcg_rd, tcg_ra, tcg_tmp);
        }
    }

    if (!sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

TRANS(MADD_w, do_muladd, a, false, false, MO_64)
TRANS(MSUB_w, do_muladd, a, false, true, MO_64)
TRANS(MADD_x, do_muladd, a, true, false, MO_64)
TRANS(MSUB_x, do_muladd, a, true, true, MO_64)

TRANS(SMADDL, do_muladd, a, true, false, MO_SL)
TRANS(SMSUBL, do_muladd, a, true, true, MO_SL)
TRANS(UMADDL, do_muladd, a, true, false, MO_UL)
TRANS(UMSUBL, do_muladd, a, true, true, MO_UL)

static bool do_adc_sbc(DisasContext *s, arg_rrr_sf *a,
                       bool is_sub, bool setflags)
{
    TCGv_i64 tcg_y, tcg_rn, tcg_rd;

    tcg_rd = cpu_reg(s, a->rd);
    tcg_rn = cpu_reg(s, a->rn);

    if (is_sub) {
        tcg_y = tcg_temp_new_i64();
        tcg_gen_not_i64(tcg_y, cpu_reg(s, a->rm));
    } else {
        tcg_y = cpu_reg(s, a->rm);
    }

    if (setflags) {
        gen_adc_CC(a->sf, tcg_rd, tcg_rn, tcg_y);
    } else {
        gen_adc(a->sf, tcg_rd, tcg_rn, tcg_y);
    }
    return true;
}

TRANS(ADC, do_adc_sbc, a, false, false)
TRANS(SBC, do_adc_sbc, a, true, false)
TRANS(ADCS, do_adc_sbc, a, false, true)
TRANS(SBCS, do_adc_sbc, a, true, true)

static bool trans_RMIF(DisasContext *s, arg_RMIF *a)
{
    int mask = a->mask;
    TCGv_i64 tcg_rn;
    TCGv_i32 nzcv;

    if (!dc_isar_feature(aa64_condm_4, s)) {
        return false;
    }

    tcg_rn = read_cpu_reg(s, a->rn, 1);
    tcg_gen_rotri_i64(tcg_rn, tcg_rn, a->imm);

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
    return true;
}

static bool do_setf(DisasContext *s, int rn, int shift)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(tmp, cpu_reg(s, rn));
    tcg_gen_shli_i32(cpu_NF, tmp, shift);
    tcg_gen_shli_i32(cpu_VF, tmp, shift - 1);
    tcg_gen_mov_i32(cpu_ZF, cpu_NF);
    tcg_gen_xor_i32(cpu_VF, cpu_VF, cpu_NF);
    return true;
}

TRANS_FEAT(SETF8, aa64_condm_4, do_setf, a->rn, 24)
TRANS_FEAT(SETF16, aa64_condm_4, do_setf, a->rn, 16)

/* CCMP, CCMN */
static bool trans_CCMP(DisasContext *s, arg_CCMP *a)
{
    TCGv_i32 tcg_t0 = tcg_temp_new_i32();
    TCGv_i32 tcg_t1 = tcg_temp_new_i32();
    TCGv_i32 tcg_t2 = tcg_temp_new_i32();
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();
    TCGv_i64 tcg_rn, tcg_y;
    DisasCompare c;
    unsigned nzcv;
    bool has_andc;

    /* Set T0 = !COND.  */
    arm_test_cc(&c, a->cond);
    tcg_gen_setcondi_i32(tcg_invert_cond(c.cond), tcg_t0, c.value, 0);

    /* Load the arguments for the new comparison.  */
    if (a->imm) {
        tcg_y = tcg_constant_i64(a->y);
    } else {
        tcg_y = cpu_reg(s, a->y);
    }
    tcg_rn = cpu_reg(s, a->rn);

    /* Set the flags for the new comparison.  */
    if (a->op) {
        gen_sub_CC(a->sf, tcg_tmp, tcg_rn, tcg_y);
    } else {
        gen_add_CC(a->sf, tcg_tmp, tcg_rn, tcg_y);
    }

    /*
     * If COND was false, force the flags to #nzcv.  Compute two masks
     * to help with this: T1 = (COND ? 0 : -1), T2 = (COND ? -1 : 0).
     * For tcg hosts that support ANDC, we can make do with just T1.
     * In either case, allow the tcg optimizer to delete any unused mask.
     */
    tcg_gen_neg_i32(tcg_t1, tcg_t0);
    tcg_gen_subi_i32(tcg_t2, tcg_t0, 1);

    nzcv = a->nzcv;
    has_andc = tcg_op_supported(INDEX_op_andc, TCG_TYPE_I32, 0);
    if (nzcv & 8) { /* N */
        tcg_gen_or_i32(cpu_NF, cpu_NF, tcg_t1);
    } else {
        if (has_andc) {
            tcg_gen_andc_i32(cpu_NF, cpu_NF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_NF, cpu_NF, tcg_t2);
        }
    }
    if (nzcv & 4) { /* Z */
        if (has_andc) {
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
        if (has_andc) {
            tcg_gen_andc_i32(cpu_CF, cpu_CF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_CF, cpu_CF, tcg_t2);
        }
    }
    if (nzcv & 1) { /* V */
        tcg_gen_or_i32(cpu_VF, cpu_VF, tcg_t1);
    } else {
        if (has_andc) {
            tcg_gen_andc_i32(cpu_VF, cpu_VF, tcg_t1);
        } else {
            tcg_gen_and_i32(cpu_VF, cpu_VF, tcg_t2);
        }
    }
    return true;
}

static bool trans_CSEL(DisasContext *s, arg_CSEL *a)
{
    TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
    TCGv_i64 zero = tcg_constant_i64(0);
    DisasCompare64 c;

    a64_test_cc(&c, a->cond);

    if (a->rn == 31 && a->rm == 31 && (a->else_inc ^ a->else_inv)) {
        /* CSET & CSETM.  */
        if (a->else_inv) {
            tcg_gen_negsetcond_i64(tcg_invert_cond(c.cond),
                                   tcg_rd, c.value, zero);
        } else {
            tcg_gen_setcond_i64(tcg_invert_cond(c.cond),
                                tcg_rd, c.value, zero);
        }
    } else {
        TCGv_i64 t_true = cpu_reg(s, a->rn);
        TCGv_i64 t_false = read_cpu_reg(s, a->rm, 1);

        if (a->else_inv && a->else_inc) {
            tcg_gen_neg_i64(t_false, t_false);
        } else if (a->else_inv) {
            tcg_gen_not_i64(t_false, t_false);
        } else if (a->else_inc) {
            tcg_gen_addi_i64(t_false, t_false, 1);
        }
        tcg_gen_movcond_i64(c.cond, tcg_rd, c.value, zero, t_true, t_false);
    }

    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
    return true;
}

typedef struct FPScalar1Int {
    void (*gen_h)(TCGv_i32, TCGv_i32);
    void (*gen_s)(TCGv_i32, TCGv_i32);
    void (*gen_d)(TCGv_i64, TCGv_i64);
} FPScalar1Int;

static bool do_fp1_scalar_int(DisasContext *s, arg_rr_e *a,
                              const FPScalar1Int *f,
                              bool merging)
{
    switch (a->esz) {
    case MO_64:
        if (fp_access_check(s)) {
            TCGv_i64 t = read_fp_dreg(s, a->rn);
            f->gen_d(t, t);
            if (merging) {
                write_fp_dreg_merging(s, a->rd, a->rd, t);
            } else {
                write_fp_dreg(s, a->rd, t);
            }
        }
        break;
    case MO_32:
        if (fp_access_check(s)) {
            TCGv_i32 t = read_fp_sreg(s, a->rn);
            f->gen_s(t, t);
            if (merging) {
                write_fp_sreg_merging(s, a->rd, a->rd, t);
            } else {
                write_fp_sreg(s, a->rd, t);
            }
        }
        break;
    case MO_16:
        if (!dc_isar_feature(aa64_fp16, s)) {
            return false;
        }
        if (fp_access_check(s)) {
            TCGv_i32 t = read_fp_hreg(s, a->rn);
            f->gen_h(t, t);
            if (merging) {
                write_fp_hreg_merging(s, a->rd, a->rd, t);
            } else {
                write_fp_sreg(s, a->rd, t);
            }
        }
        break;
    default:
        return false;
    }
    return true;
}

static bool do_fp1_scalar_int_2fn(DisasContext *s, arg_rr_e *a,
                                  const FPScalar1Int *fnormal,
                                  const FPScalar1Int *fah)
{
    return do_fp1_scalar_int(s, a, s->fpcr_ah ? fah : fnormal, true);
}

static const FPScalar1Int f_scalar_fmov = {
    tcg_gen_mov_i32,
    tcg_gen_mov_i32,
    tcg_gen_mov_i64,
};
TRANS(FMOV_s, do_fp1_scalar_int, a, &f_scalar_fmov, false)

static const FPScalar1Int f_scalar_fabs = {
    gen_vfp_absh,
    gen_vfp_abss,
    gen_vfp_absd,
};
static const FPScalar1Int f_scalar_ah_fabs = {
    gen_vfp_ah_absh,
    gen_vfp_ah_abss,
    gen_vfp_ah_absd,
};
TRANS(FABS_s, do_fp1_scalar_int_2fn, a, &f_scalar_fabs, &f_scalar_ah_fabs)

static const FPScalar1Int f_scalar_fneg = {
    gen_vfp_negh,
    gen_vfp_negs,
    gen_vfp_negd,
};
static const FPScalar1Int f_scalar_ah_fneg = {
    gen_vfp_ah_negh,
    gen_vfp_ah_negs,
    gen_vfp_ah_negd,
};
TRANS(FNEG_s, do_fp1_scalar_int_2fn, a, &f_scalar_fneg, &f_scalar_ah_fneg)

typedef struct FPScalar1 {
    void (*gen_h)(TCGv_i32, TCGv_i32, TCGv_ptr);
    void (*gen_s)(TCGv_i32, TCGv_i32, TCGv_ptr);
    void (*gen_d)(TCGv_i64, TCGv_i64, TCGv_ptr);
} FPScalar1;

static bool do_fp1_scalar_with_fpsttype(DisasContext *s, arg_rr_e *a,
                                        const FPScalar1 *f, int rmode,
                                        ARMFPStatusFlavour fpsttype)
{
    TCGv_i32 tcg_rmode = NULL;
    TCGv_ptr fpst;
    TCGv_i64 t64;
    TCGv_i32 t32;
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    fpst = fpstatus_ptr(fpsttype);
    if (rmode >= 0) {
        tcg_rmode = gen_set_rmode(rmode, fpst);
    }

    switch (a->esz) {
    case MO_64:
        t64 = read_fp_dreg(s, a->rn);
        f->gen_d(t64, t64, fpst);
        write_fp_dreg_merging(s, a->rd, a->rd, t64);
        break;
    case MO_32:
        t32 = read_fp_sreg(s, a->rn);
        f->gen_s(t32, t32, fpst);
        write_fp_sreg_merging(s, a->rd, a->rd, t32);
        break;
    case MO_16:
        t32 = read_fp_hreg(s, a->rn);
        f->gen_h(t32, t32, fpst);
        write_fp_hreg_merging(s, a->rd, a->rd, t32);
        break;
    default:
        g_assert_not_reached();
    }

    if (rmode >= 0) {
        gen_restore_rmode(tcg_rmode, fpst);
    }
    return true;
}

static bool do_fp1_scalar(DisasContext *s, arg_rr_e *a,
                          const FPScalar1 *f, int rmode)
{
    return do_fp1_scalar_with_fpsttype(s, a, f, rmode,
                                       a->esz == MO_16 ?
                                       FPST_A64_F16 : FPST_A64);
}

static bool do_fp1_scalar_ah(DisasContext *s, arg_rr_e *a,
                             const FPScalar1 *f, int rmode)
{
    return do_fp1_scalar_with_fpsttype(s, a, f, rmode, select_ah_fpst(s, a->esz));
}

static const FPScalar1 f_scalar_fsqrt = {
    gen_helper_vfp_sqrth,
    gen_helper_vfp_sqrts,
    gen_helper_vfp_sqrtd,
};
TRANS(FSQRT_s, do_fp1_scalar, a, &f_scalar_fsqrt, -1)

static const FPScalar1 f_scalar_frint = {
    gen_helper_advsimd_rinth,
    gen_helper_rints,
    gen_helper_rintd,
};
TRANS(FRINTN_s, do_fp1_scalar, a, &f_scalar_frint, FPROUNDING_TIEEVEN)
TRANS(FRINTP_s, do_fp1_scalar, a, &f_scalar_frint, FPROUNDING_POSINF)
TRANS(FRINTM_s, do_fp1_scalar, a, &f_scalar_frint, FPROUNDING_NEGINF)
TRANS(FRINTZ_s, do_fp1_scalar, a, &f_scalar_frint, FPROUNDING_ZERO)
TRANS(FRINTA_s, do_fp1_scalar, a, &f_scalar_frint, FPROUNDING_TIEAWAY)
TRANS(FRINTI_s, do_fp1_scalar, a, &f_scalar_frint, -1)

static const FPScalar1 f_scalar_frintx = {
    gen_helper_advsimd_rinth_exact,
    gen_helper_rints_exact,
    gen_helper_rintd_exact,
};
TRANS(FRINTX_s, do_fp1_scalar, a, &f_scalar_frintx, -1)

static bool trans_BFCVT_s(DisasContext *s, arg_rr_e *a)
{
    ARMFPStatusFlavour fpsttype = s->fpcr_ah ? FPST_AH : FPST_A64;
    TCGv_i32 t32;
    int check;

    if (!dc_isar_feature(aa64_bf16, s)) {
        return false;
    }

    check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    t32 = read_fp_sreg(s, a->rn);
    gen_helper_bfcvt(t32, t32, fpstatus_ptr(fpsttype));
    write_fp_hreg_merging(s, a->rd, a->rd, t32);
    return true;
}

static const FPScalar1 f_scalar_frint32 = {
    NULL,
    gen_helper_frint32_s,
    gen_helper_frint32_d,
};
TRANS_FEAT(FRINT32Z_s, aa64_frint, do_fp1_scalar, a,
           &f_scalar_frint32, FPROUNDING_ZERO)
TRANS_FEAT(FRINT32X_s, aa64_frint, do_fp1_scalar, a, &f_scalar_frint32, -1)

static const FPScalar1 f_scalar_frint64 = {
    NULL,
    gen_helper_frint64_s,
    gen_helper_frint64_d,
};
TRANS_FEAT(FRINT64Z_s, aa64_frint, do_fp1_scalar, a,
           &f_scalar_frint64, FPROUNDING_ZERO)
TRANS_FEAT(FRINT64X_s, aa64_frint, do_fp1_scalar, a, &f_scalar_frint64, -1)

static const FPScalar1 f_scalar_frecpe = {
    gen_helper_recpe_f16,
    gen_helper_recpe_f32,
    gen_helper_recpe_f64,
};
static const FPScalar1 f_scalar_frecpe_rpres = {
    gen_helper_recpe_f16,
    gen_helper_recpe_rpres_f32,
    gen_helper_recpe_f64,
};
TRANS(FRECPE_s, do_fp1_scalar_ah, a,
      s->fpcr_ah && dc_isar_feature(aa64_rpres, s) ?
      &f_scalar_frecpe_rpres : &f_scalar_frecpe, -1)

static const FPScalar1 f_scalar_frecpx = {
    gen_helper_frecpx_f16,
    gen_helper_frecpx_f32,
    gen_helper_frecpx_f64,
};
TRANS(FRECPX_s, do_fp1_scalar_ah, a, &f_scalar_frecpx, -1)

static const FPScalar1 f_scalar_frsqrte = {
    gen_helper_rsqrte_f16,
    gen_helper_rsqrte_f32,
    gen_helper_rsqrte_f64,
};
static const FPScalar1 f_scalar_frsqrte_rpres = {
    gen_helper_rsqrte_f16,
    gen_helper_rsqrte_rpres_f32,
    gen_helper_rsqrte_f64,
};
TRANS(FRSQRTE_s, do_fp1_scalar_ah, a,
      s->fpcr_ah && dc_isar_feature(aa64_rpres, s) ?
      &f_scalar_frsqrte_rpres : &f_scalar_frsqrte, -1)

static bool trans_FCVT_s_ds(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i32 tcg_rn = read_fp_sreg(s, a->rn);
        TCGv_i64 tcg_rd = tcg_temp_new_i64();
        TCGv_ptr fpst = fpstatus_ptr(FPST_A64);

        gen_helper_vfp_fcvtds(tcg_rd, tcg_rn, fpst);
        write_fp_dreg_merging(s, a->rd, a->rd, tcg_rd);
    }
    return true;
}

static bool trans_FCVT_s_hs(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i32 tmp = read_fp_sreg(s, a->rn);
        TCGv_i32 ahp = get_ahp_flag();
        TCGv_ptr fpst = fpstatus_ptr(FPST_A64);

        gen_helper_vfp_fcvt_f32_to_f16(tmp, tmp, fpst, ahp);
        /* write_fp_hreg_merging is OK here because top half of result is zero */
        write_fp_hreg_merging(s, a->rd, a->rd, tmp);
    }
    return true;
}

static bool trans_FCVT_s_sd(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rn = read_fp_dreg(s, a->rn);
        TCGv_i32 tcg_rd = tcg_temp_new_i32();
        TCGv_ptr fpst = fpstatus_ptr(FPST_A64);

        gen_helper_vfp_fcvtsd(tcg_rd, tcg_rn, fpst);
        write_fp_sreg_merging(s, a->rd, a->rd, tcg_rd);
    }
    return true;
}

static bool trans_FCVT_s_hd(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rn = read_fp_dreg(s, a->rn);
        TCGv_i32 tcg_rd = tcg_temp_new_i32();
        TCGv_i32 ahp = get_ahp_flag();
        TCGv_ptr fpst = fpstatus_ptr(FPST_A64);

        gen_helper_vfp_fcvt_f64_to_f16(tcg_rd, tcg_rn, fpst, ahp);
        /* write_fp_hreg_merging is OK here because top half of tcg_rd is zero */
        write_fp_hreg_merging(s, a->rd, a->rd, tcg_rd);
    }
    return true;
}

static bool trans_FCVT_s_sh(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i32 tcg_rn = read_fp_hreg(s, a->rn);
        TCGv_i32 tcg_rd = tcg_temp_new_i32();
        TCGv_ptr tcg_fpst = fpstatus_ptr(FPST_A64_F16);
        TCGv_i32 tcg_ahp = get_ahp_flag();

        gen_helper_vfp_fcvt_f16_to_f32(tcg_rd, tcg_rn, tcg_fpst, tcg_ahp);
        write_fp_sreg_merging(s, a->rd, a->rd, tcg_rd);
    }
    return true;
}

static bool trans_FCVT_s_dh(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i32 tcg_rn = read_fp_hreg(s, a->rn);
        TCGv_i64 tcg_rd = tcg_temp_new_i64();
        TCGv_ptr tcg_fpst = fpstatus_ptr(FPST_A64_F16);
        TCGv_i32 tcg_ahp = get_ahp_flag();

        gen_helper_vfp_fcvt_f16_to_f64(tcg_rd, tcg_rn, tcg_fpst, tcg_ahp);
        write_fp_dreg_merging(s, a->rd, a->rd, tcg_rd);
    }
    return true;
}

static bool do_cvtf_scalar(DisasContext *s, MemOp esz, int rd, int shift,
                           TCGv_i64 tcg_int, bool is_signed)
{
    TCGv_ptr tcg_fpstatus;
    TCGv_i32 tcg_shift, tcg_single;
    TCGv_i64 tcg_double;

    tcg_fpstatus = fpstatus_ptr(esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    tcg_shift = tcg_constant_i32(shift);

    switch (esz) {
    case MO_64:
        tcg_double = tcg_temp_new_i64();
        if (is_signed) {
            gen_helper_vfp_sqtod(tcg_double, tcg_int, tcg_shift, tcg_fpstatus);
        } else {
            gen_helper_vfp_uqtod(tcg_double, tcg_int, tcg_shift, tcg_fpstatus);
        }
        write_fp_dreg_merging(s, rd, rd, tcg_double);
        break;

    case MO_32:
        tcg_single = tcg_temp_new_i32();
        if (is_signed) {
            gen_helper_vfp_sqtos(tcg_single, tcg_int, tcg_shift, tcg_fpstatus);
        } else {
            gen_helper_vfp_uqtos(tcg_single, tcg_int, tcg_shift, tcg_fpstatus);
        }
        write_fp_sreg_merging(s, rd, rd, tcg_single);
        break;

    case MO_16:
        tcg_single = tcg_temp_new_i32();
        if (is_signed) {
            gen_helper_vfp_sqtoh(tcg_single, tcg_int, tcg_shift, tcg_fpstatus);
        } else {
            gen_helper_vfp_uqtoh(tcg_single, tcg_int, tcg_shift, tcg_fpstatus);
        }
        write_fp_hreg_merging(s, rd, rd, tcg_single);
        break;

    default:
        g_assert_not_reached();
    }
    return true;
}

static bool do_cvtf_g(DisasContext *s, arg_fcvt *a, bool is_signed)
{
    TCGv_i64 tcg_int;
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    if (a->sf) {
        tcg_int = cpu_reg(s, a->rn);
    } else {
        tcg_int = read_cpu_reg(s, a->rn, true);
        if (is_signed) {
            tcg_gen_ext32s_i64(tcg_int, tcg_int);
        } else {
            tcg_gen_ext32u_i64(tcg_int, tcg_int);
        }
    }
    return do_cvtf_scalar(s, a->esz, a->rd, a->shift, tcg_int, is_signed);
}

TRANS(SCVTF_g, do_cvtf_g, a, true)
TRANS(UCVTF_g, do_cvtf_g, a, false)

/*
 * [US]CVTF (vector), scalar version.
 * Which sounds weird, but really just means input from fp register
 * instead of input from general register.  Input and output element
 * size are always equal.
 */
static bool do_cvtf_f(DisasContext *s, arg_fcvt *a, bool is_signed)
{
    TCGv_i64 tcg_int;
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    tcg_int = tcg_temp_new_i64();
    read_vec_element(s, tcg_int, a->rn, 0, a->esz | (is_signed ? MO_SIGN : 0));
    return do_cvtf_scalar(s, a->esz, a->rd, a->shift, tcg_int, is_signed);
}

TRANS(SCVTF_f, do_cvtf_f, a, true)
TRANS(UCVTF_f, do_cvtf_f, a, false)

static void do_fcvt_scalar(DisasContext *s, MemOp out, MemOp esz,
                           TCGv_i64 tcg_out, int shift, int rn,
                           ARMFPRounding rmode)
{
    TCGv_ptr tcg_fpstatus;
    TCGv_i32 tcg_shift, tcg_rmode, tcg_single;

    tcg_fpstatus = fpstatus_ptr(esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    tcg_shift = tcg_constant_i32(shift);
    tcg_rmode = gen_set_rmode(rmode, tcg_fpstatus);

    switch (esz) {
    case MO_64:
        read_vec_element(s, tcg_out, rn, 0, MO_64);
        switch (out) {
        case MO_64 | MO_SIGN:
            gen_helper_vfp_tosqd(tcg_out, tcg_out, tcg_shift, tcg_fpstatus);
            break;
        case MO_64:
            gen_helper_vfp_touqd(tcg_out, tcg_out, tcg_shift, tcg_fpstatus);
            break;
        case MO_32 | MO_SIGN:
            gen_helper_vfp_tosld(tcg_out, tcg_out, tcg_shift, tcg_fpstatus);
            break;
        case MO_32:
            gen_helper_vfp_tould(tcg_out, tcg_out, tcg_shift, tcg_fpstatus);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case MO_32:
        tcg_single = read_fp_sreg(s, rn);
        switch (out) {
        case MO_64 | MO_SIGN:
            gen_helper_vfp_tosqs(tcg_out, tcg_single, tcg_shift, tcg_fpstatus);
            break;
        case MO_64:
            gen_helper_vfp_touqs(tcg_out, tcg_single, tcg_shift, tcg_fpstatus);
            break;
        case MO_32 | MO_SIGN:
            gen_helper_vfp_tosls(tcg_single, tcg_single,
                                 tcg_shift, tcg_fpstatus);
            tcg_gen_extu_i32_i64(tcg_out, tcg_single);
            break;
        case MO_32:
            gen_helper_vfp_touls(tcg_single, tcg_single,
                                 tcg_shift, tcg_fpstatus);
            tcg_gen_extu_i32_i64(tcg_out, tcg_single);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    case MO_16:
        tcg_single = read_fp_hreg(s, rn);
        switch (out) {
        case MO_64 | MO_SIGN:
            gen_helper_vfp_tosqh(tcg_out, tcg_single, tcg_shift, tcg_fpstatus);
            break;
        case MO_64:
            gen_helper_vfp_touqh(tcg_out, tcg_single, tcg_shift, tcg_fpstatus);
            break;
        case MO_32 | MO_SIGN:
            gen_helper_vfp_toslh(tcg_single, tcg_single,
                                 tcg_shift, tcg_fpstatus);
            tcg_gen_extu_i32_i64(tcg_out, tcg_single);
            break;
        case MO_32:
            gen_helper_vfp_toulh(tcg_single, tcg_single,
                                 tcg_shift, tcg_fpstatus);
            tcg_gen_extu_i32_i64(tcg_out, tcg_single);
            break;
        case MO_16 | MO_SIGN:
            gen_helper_vfp_toshh(tcg_single, tcg_single,
                                 tcg_shift, tcg_fpstatus);
            tcg_gen_extu_i32_i64(tcg_out, tcg_single);
            break;
        case MO_16:
            gen_helper_vfp_touhh(tcg_single, tcg_single,
                                 tcg_shift, tcg_fpstatus);
            tcg_gen_extu_i32_i64(tcg_out, tcg_single);
            break;
        default:
            g_assert_not_reached();
        }
        break;

    default:
        g_assert_not_reached();
    }

    gen_restore_rmode(tcg_rmode, tcg_fpstatus);
}

static bool do_fcvt_g(DisasContext *s, arg_fcvt *a,
                      ARMFPRounding rmode, bool is_signed)
{
    TCGv_i64 tcg_int;
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    tcg_int = cpu_reg(s, a->rd);
    do_fcvt_scalar(s, (a->sf ? MO_64 : MO_32) | (is_signed ? MO_SIGN : 0),
                   a->esz, tcg_int, a->shift, a->rn, rmode);

    if (!a->sf) {
        tcg_gen_ext32u_i64(tcg_int, tcg_int);
    }
    return true;
}

TRANS(FCVTNS_g, do_fcvt_g, a, FPROUNDING_TIEEVEN, true)
TRANS(FCVTNU_g, do_fcvt_g, a, FPROUNDING_TIEEVEN, false)
TRANS(FCVTPS_g, do_fcvt_g, a, FPROUNDING_POSINF, true)
TRANS(FCVTPU_g, do_fcvt_g, a, FPROUNDING_POSINF, false)
TRANS(FCVTMS_g, do_fcvt_g, a, FPROUNDING_NEGINF, true)
TRANS(FCVTMU_g, do_fcvt_g, a, FPROUNDING_NEGINF, false)
TRANS(FCVTZS_g, do_fcvt_g, a, FPROUNDING_ZERO, true)
TRANS(FCVTZU_g, do_fcvt_g, a, FPROUNDING_ZERO, false)
TRANS(FCVTAS_g, do_fcvt_g, a, FPROUNDING_TIEAWAY, true)
TRANS(FCVTAU_g, do_fcvt_g, a, FPROUNDING_TIEAWAY, false)

/*
 * FCVT* (vector), scalar version.
 * Which sounds weird, but really just means output to fp register
 * instead of output to general register.  Input and output element
 * size are always equal.
 */
static bool do_fcvt_f(DisasContext *s, arg_fcvt *a,
                      ARMFPRounding rmode, bool is_signed)
{
    TCGv_i64 tcg_int;
    int check = fp_access_check_scalar_hsd(s, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    tcg_int = tcg_temp_new_i64();
    do_fcvt_scalar(s, a->esz | (is_signed ? MO_SIGN : 0),
                   a->esz, tcg_int, a->shift, a->rn, rmode);

    if (!s->fpcr_nep) {
        clear_vec(s, a->rd);
    }
    write_vec_element(s, tcg_int, a->rd, 0, a->esz);
    return true;
}

TRANS(FCVTNS_f, do_fcvt_f, a, FPROUNDING_TIEEVEN, true)
TRANS(FCVTNU_f, do_fcvt_f, a, FPROUNDING_TIEEVEN, false)
TRANS(FCVTPS_f, do_fcvt_f, a, FPROUNDING_POSINF, true)
TRANS(FCVTPU_f, do_fcvt_f, a, FPROUNDING_POSINF, false)
TRANS(FCVTMS_f, do_fcvt_f, a, FPROUNDING_NEGINF, true)
TRANS(FCVTMU_f, do_fcvt_f, a, FPROUNDING_NEGINF, false)
TRANS(FCVTZS_f, do_fcvt_f, a, FPROUNDING_ZERO, true)
TRANS(FCVTZU_f, do_fcvt_f, a, FPROUNDING_ZERO, false)
TRANS(FCVTAS_f, do_fcvt_f, a, FPROUNDING_TIEAWAY, true)
TRANS(FCVTAU_f, do_fcvt_f, a, FPROUNDING_TIEAWAY, false)

static bool trans_FJCVTZS(DisasContext *s, arg_FJCVTZS *a)
{
    if (!dc_isar_feature(aa64_jscvt, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i64 t = read_fp_dreg(s, a->rn);
        TCGv_ptr fpstatus = fpstatus_ptr(FPST_A64);

        gen_helper_fjcvtzs(t, t, fpstatus);

        tcg_gen_ext32u_i64(cpu_reg(s, a->rd), t);
        tcg_gen_extrh_i64_i32(cpu_ZF, t);
        tcg_gen_movi_i32(cpu_CF, 0);
        tcg_gen_movi_i32(cpu_NF, 0);
        tcg_gen_movi_i32(cpu_VF, 0);
    }
    return true;
}

static bool trans_FMOV_hx(DisasContext *s, arg_rr *a)
{
    if (!dc_isar_feature(aa64_fp16, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rn = cpu_reg(s, a->rn);
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_ext16u_i64(tmp, tcg_rn);
        write_fp_dreg(s, a->rd, tmp);
    }
    return true;
}

static bool trans_FMOV_sw(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rn = cpu_reg(s, a->rn);
        TCGv_i64 tmp = tcg_temp_new_i64();
        tcg_gen_ext32u_i64(tmp, tcg_rn);
        write_fp_dreg(s, a->rd, tmp);
    }
    return true;
}

static bool trans_FMOV_dx(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rn = cpu_reg(s, a->rn);
        write_fp_dreg(s, a->rd, tcg_rn);
    }
    return true;
}

static bool trans_FMOV_ux(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rn = cpu_reg(s, a->rn);
        tcg_gen_st_i64(tcg_rn, tcg_env, fp_reg_hi_offset(s, a->rd));
        clear_vec_high(s, true, a->rd);
    }
    return true;
}

static bool trans_FMOV_xh(DisasContext *s, arg_rr *a)
{
    if (!dc_isar_feature(aa64_fp16, s)) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
        tcg_gen_ld16u_i64(tcg_rd, tcg_env, fp_reg_offset(s, a->rn, MO_16));
    }
    return true;
}

static bool trans_FMOV_ws(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
        tcg_gen_ld32u_i64(tcg_rd, tcg_env, fp_reg_offset(s, a->rn, MO_32));
    }
    return true;
}

static bool trans_FMOV_xd(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
        tcg_gen_ld_i64(tcg_rd, tcg_env, fp_reg_offset(s, a->rn, MO_64));
    }
    return true;
}

static bool trans_FMOV_xu(DisasContext *s, arg_rr *a)
{
    if (fp_access_check(s)) {
        TCGv_i64 tcg_rd = cpu_reg(s, a->rd);
        tcg_gen_ld_i64(tcg_rd, tcg_env, fp_reg_hi_offset(s, a->rn));
    }
    return true;
}

typedef struct ENVScalar1 {
    NeonGenOneOpEnvFn *gen_bhs[3];
    NeonGenOne64OpEnvFn *gen_d;
} ENVScalar1;

static bool do_env_scalar1(DisasContext *s, arg_rr_e *a, const ENVScalar1 *f)
{
    if (!fp_access_check(s)) {
        return true;
    }
    if (a->esz == MO_64) {
        TCGv_i64 t = read_fp_dreg(s, a->rn);
        f->gen_d(t, tcg_env, t);
        write_fp_dreg(s, a->rd, t);
    } else {
        TCGv_i32 t = tcg_temp_new_i32();

        read_vec_element_i32(s, t, a->rn, 0, a->esz);
        f->gen_bhs[a->esz](t, tcg_env, t);
        write_fp_sreg(s, a->rd, t);
    }
    return true;
}

static bool do_env_vector1(DisasContext *s, arg_qrr_e *a, const ENVScalar1 *f)
{
    if (a->esz == MO_64 && !a->q) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }
    if (a->esz == MO_64) {
        TCGv_i64 t = tcg_temp_new_i64();

        for (int i = 0; i < 2; ++i) {
            read_vec_element(s, t, a->rn, i, MO_64);
            f->gen_d(t, tcg_env, t);
            write_vec_element(s, t, a->rd, i, MO_64);
        }
    } else {
        TCGv_i32 t = tcg_temp_new_i32();
        int n = (a->q ? 16 : 8) >> a->esz;

        for (int i = 0; i < n; ++i) {
            read_vec_element_i32(s, t, a->rn, i, a->esz);
            f->gen_bhs[a->esz](t, tcg_env, t);
            write_vec_element_i32(s, t, a->rd, i, a->esz);
        }
    }
    clear_vec_high(s, a->q, a->rd);
    return true;
}

static const ENVScalar1 f_scalar_sqabs = {
    { gen_helper_neon_qabs_s8,
      gen_helper_neon_qabs_s16,
      gen_helper_neon_qabs_s32 },
    gen_helper_neon_qabs_s64,
};
TRANS(SQABS_s, do_env_scalar1, a, &f_scalar_sqabs)
TRANS(SQABS_v, do_env_vector1, a, &f_scalar_sqabs)

static const ENVScalar1 f_scalar_sqneg = {
    { gen_helper_neon_qneg_s8,
      gen_helper_neon_qneg_s16,
      gen_helper_neon_qneg_s32 },
    gen_helper_neon_qneg_s64,
};
TRANS(SQNEG_s, do_env_scalar1, a, &f_scalar_sqneg)
TRANS(SQNEG_v, do_env_vector1, a, &f_scalar_sqneg)

static bool do_scalar1_d(DisasContext *s, arg_rr *a, ArithOneOp *f)
{
    if (fp_access_check(s)) {
        TCGv_i64 t = read_fp_dreg(s, a->rn);
        f(t, t);
        write_fp_dreg(s, a->rd, t);
    }
    return true;
}

TRANS(ABS_s, do_scalar1_d, a, tcg_gen_abs_i64)
TRANS(NEG_s, do_scalar1_d, a, tcg_gen_neg_i64)

static bool do_cmop0_d(DisasContext *s, arg_rr *a, TCGCond cond)
{
    if (fp_access_check(s)) {
        TCGv_i64 t = read_fp_dreg(s, a->rn);
        tcg_gen_negsetcond_i64(cond, t, t, tcg_constant_i64(0));
        write_fp_dreg(s, a->rd, t);
    }
    return true;
}

TRANS(CMGT0_s, do_cmop0_d, a, TCG_COND_GT)
TRANS(CMGE0_s, do_cmop0_d, a, TCG_COND_GE)
TRANS(CMLE0_s, do_cmop0_d, a, TCG_COND_LE)
TRANS(CMLT0_s, do_cmop0_d, a, TCG_COND_LT)
TRANS(CMEQ0_s, do_cmop0_d, a, TCG_COND_EQ)

static bool do_2misc_narrow_scalar(DisasContext *s, arg_rr_e *a,
                                   ArithOneOp * const fn[3])
{
    if (a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i64 t = tcg_temp_new_i64();

        read_vec_element(s, t, a->rn, 0, a->esz + 1);
        fn[a->esz](t, t);
        clear_vec(s, a->rd);
        write_vec_element(s, t, a->rd, 0, a->esz);
    }
    return true;
}

#define WRAP_ENV(NAME) \
    static void gen_##NAME(TCGv_i64 d, TCGv_i64 n) \
    { gen_helper_##NAME(d, tcg_env, n); }

WRAP_ENV(neon_unarrow_sat8)
WRAP_ENV(neon_unarrow_sat16)
WRAP_ENV(neon_unarrow_sat32)

static ArithOneOp * const f_scalar_sqxtun[] = {
    gen_neon_unarrow_sat8,
    gen_neon_unarrow_sat16,
    gen_neon_unarrow_sat32,
};
TRANS(SQXTUN_s, do_2misc_narrow_scalar, a, f_scalar_sqxtun)

WRAP_ENV(neon_narrow_sat_s8)
WRAP_ENV(neon_narrow_sat_s16)
WRAP_ENV(neon_narrow_sat_s32)

static ArithOneOp * const f_scalar_sqxtn[] = {
    gen_neon_narrow_sat_s8,
    gen_neon_narrow_sat_s16,
    gen_neon_narrow_sat_s32,
};
TRANS(SQXTN_s, do_2misc_narrow_scalar, a, f_scalar_sqxtn)

WRAP_ENV(neon_narrow_sat_u8)
WRAP_ENV(neon_narrow_sat_u16)
WRAP_ENV(neon_narrow_sat_u32)

static ArithOneOp * const f_scalar_uqxtn[] = {
    gen_neon_narrow_sat_u8,
    gen_neon_narrow_sat_u16,
    gen_neon_narrow_sat_u32,
};
TRANS(UQXTN_s, do_2misc_narrow_scalar, a, f_scalar_uqxtn)

static bool trans_FCVTXN_s(DisasContext *s, arg_rr_e *a)
{
    if (fp_access_check(s)) {
        /*
         * 64 bit to 32 bit float conversion
         * with von Neumann rounding (round to odd)
         */
        TCGv_i64 src = read_fp_dreg(s, a->rn);
        TCGv_i32 dst = tcg_temp_new_i32();
        gen_helper_fcvtx_f64_to_f32(dst, src, fpstatus_ptr(FPST_A64));
        write_fp_sreg_merging(s, a->rd, a->rd, dst);
    }
    return true;
}

#undef WRAP_ENV

static bool do_gvec_fn2(DisasContext *s, arg_qrr_e *a, GVecGen2Fn *fn)
{
    if (!a->q && a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_fn2(s, a->q, a->rd, a->rn, fn, a->esz);
    }
    return true;
}

TRANS(ABS_v, do_gvec_fn2, a, tcg_gen_gvec_abs)
TRANS(NEG_v, do_gvec_fn2, a, tcg_gen_gvec_neg)
TRANS(NOT_v, do_gvec_fn2, a, tcg_gen_gvec_not)
TRANS(CNT_v, do_gvec_fn2, a, gen_gvec_cnt)
TRANS(RBIT_v, do_gvec_fn2, a, gen_gvec_rbit)
TRANS(CMGT0_v, do_gvec_fn2, a, gen_gvec_cgt0)
TRANS(CMGE0_v, do_gvec_fn2, a, gen_gvec_cge0)
TRANS(CMLT0_v, do_gvec_fn2, a, gen_gvec_clt0)
TRANS(CMLE0_v, do_gvec_fn2, a, gen_gvec_cle0)
TRANS(CMEQ0_v, do_gvec_fn2, a, gen_gvec_ceq0)
TRANS(REV16_v, do_gvec_fn2, a, gen_gvec_rev16)
TRANS(REV32_v, do_gvec_fn2, a, gen_gvec_rev32)
TRANS(URECPE_v, do_gvec_fn2, a, gen_gvec_urecpe)
TRANS(URSQRTE_v, do_gvec_fn2, a, gen_gvec_ursqrte)

static bool do_gvec_fn2_bhs(DisasContext *s, arg_qrr_e *a, GVecGen2Fn *fn)
{
    if (a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        gen_gvec_fn2(s, a->q, a->rd, a->rn, fn, a->esz);
    }
    return true;
}

TRANS(CLS_v, do_gvec_fn2_bhs, a, gen_gvec_cls)
TRANS(CLZ_v, do_gvec_fn2_bhs, a, gen_gvec_clz)
TRANS(REV64_v, do_gvec_fn2_bhs, a, gen_gvec_rev64)
TRANS(SADDLP_v, do_gvec_fn2_bhs, a, gen_gvec_saddlp)
TRANS(UADDLP_v, do_gvec_fn2_bhs, a, gen_gvec_uaddlp)
TRANS(SADALP_v, do_gvec_fn2_bhs, a, gen_gvec_sadalp)
TRANS(UADALP_v, do_gvec_fn2_bhs, a, gen_gvec_uadalp)

static bool do_2misc_narrow_vector(DisasContext *s, arg_qrr_e *a,
                                   ArithOneOp * const fn[3])
{
    if (a->esz == MO_64) {
        return false;
    }
    if (fp_access_check(s)) {
        TCGv_i64 t0 = tcg_temp_new_i64();
        TCGv_i64 t1 = tcg_temp_new_i64();

        read_vec_element(s, t0, a->rn, 0, MO_64);
        read_vec_element(s, t1, a->rn, 1, MO_64);
        fn[a->esz](t0, t0);
        fn[a->esz](t1, t1);
        write_vec_element(s, t0, a->rd, a->q ? 2 : 0, MO_32);
        write_vec_element(s, t1, a->rd, a->q ? 3 : 1, MO_32);
        clear_vec_high(s, a->q, a->rd);
    }
    return true;
}

static ArithOneOp * const f_scalar_xtn[] = {
    gen_helper_neon_narrow_u8,
    gen_helper_neon_narrow_u16,
    tcg_gen_ext32u_i64,
};
TRANS(XTN, do_2misc_narrow_vector, a, f_scalar_xtn)
TRANS(SQXTUN_v, do_2misc_narrow_vector, a, f_scalar_sqxtun)
TRANS(SQXTN_v, do_2misc_narrow_vector, a, f_scalar_sqxtn)
TRANS(UQXTN_v, do_2misc_narrow_vector, a, f_scalar_uqxtn)

static void gen_fcvtn_hs(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i32 tcg_lo = tcg_temp_new_i32();
    TCGv_i32 tcg_hi = tcg_temp_new_i32();
    TCGv_ptr fpst = fpstatus_ptr(FPST_A64);
    TCGv_i32 ahp = get_ahp_flag();

    tcg_gen_extr_i64_i32(tcg_lo, tcg_hi, n);
    gen_helper_vfp_fcvt_f32_to_f16(tcg_lo, tcg_lo, fpst, ahp);
    gen_helper_vfp_fcvt_f32_to_f16(tcg_hi, tcg_hi, fpst, ahp);
    tcg_gen_deposit_i32(tcg_lo, tcg_lo, tcg_hi, 16, 16);
    tcg_gen_extu_i32_i64(d, tcg_lo);
}

static void gen_fcvtn_sd(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_ptr fpst = fpstatus_ptr(FPST_A64);

    gen_helper_vfp_fcvtsd(tmp, n, fpst);
    tcg_gen_extu_i32_i64(d, tmp);
}

static void gen_fcvtxn_sd(TCGv_i64 d, TCGv_i64 n)
{
    /*
     * 64 bit to 32 bit float conversion
     * with von Neumann rounding (round to odd)
     */
    TCGv_i32 tmp = tcg_temp_new_i32();
    gen_helper_fcvtx_f64_to_f32(tmp, n, fpstatus_ptr(FPST_A64));
    tcg_gen_extu_i32_i64(d, tmp);
}

static ArithOneOp * const f_vector_fcvtn[] = {
    NULL,
    gen_fcvtn_hs,
    gen_fcvtn_sd,
};
static ArithOneOp * const f_scalar_fcvtxn[] = {
    NULL,
    NULL,
    gen_fcvtxn_sd,
};
TRANS(FCVTN_v, do_2misc_narrow_vector, a, f_vector_fcvtn)
TRANS(FCVTXN_v, do_2misc_narrow_vector, a, f_scalar_fcvtxn)

static void gen_bfcvtn_hs(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_ptr fpst = fpstatus_ptr(FPST_A64);
    TCGv_i32 tmp = tcg_temp_new_i32();
    gen_helper_bfcvt_pair(tmp, n, fpst);
    tcg_gen_extu_i32_i64(d, tmp);
}

static void gen_bfcvtn_ah_hs(TCGv_i64 d, TCGv_i64 n)
{
    TCGv_ptr fpst = fpstatus_ptr(FPST_AH);
    TCGv_i32 tmp = tcg_temp_new_i32();
    gen_helper_bfcvt_pair(tmp, n, fpst);
    tcg_gen_extu_i32_i64(d, tmp);
}

static ArithOneOp * const f_vector_bfcvtn[2][3] = {
    {
        NULL,
        gen_bfcvtn_hs,
        NULL,
    }, {
        NULL,
        gen_bfcvtn_ah_hs,
        NULL,
    }
};
TRANS_FEAT(BFCVTN_v, aa64_bf16, do_2misc_narrow_vector, a,
           f_vector_bfcvtn[s->fpcr_ah])

static bool trans_SHLL_v(DisasContext *s, arg_qrr_e *a)
{
    static NeonGenWidenFn * const widenfns[3] = {
        gen_helper_neon_widen_u8,
        gen_helper_neon_widen_u16,
        tcg_gen_extu_i32_i64,
    };
    NeonGenWidenFn *widenfn;
    TCGv_i64 tcg_res[2];
    TCGv_i32 tcg_op;
    int part, pass;

    if (a->esz == MO_64) {
        return false;
    }
    if (!fp_access_check(s)) {
        return true;
    }

    tcg_op = tcg_temp_new_i32();
    widenfn = widenfns[a->esz];
    part = a->q ? 2 : 0;

    for (pass = 0; pass < 2; pass++) {
        read_vec_element_i32(s, tcg_op, a->rn, part + pass, MO_32);
        tcg_res[pass] = tcg_temp_new_i64();
        widenfn(tcg_res[pass], tcg_op);
        tcg_gen_shli_i64(tcg_res[pass], tcg_res[pass], 8 << a->esz);
    }

    for (pass = 0; pass < 2; pass++) {
        write_vec_element(s, tcg_res[pass], a->rd, pass, MO_64);
    }
    return true;
}

static bool do_fabs_fneg_v(DisasContext *s, arg_qrr_e *a, GVecGen2Fn *fn)
{
    int check = fp_access_check_vector_hsd(s, a->q, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    gen_gvec_fn2(s, a->q, a->rd, a->rn, fn, a->esz);
    return true;
}

TRANS(FABS_v, do_fabs_fneg_v, a, gen_gvec_fabs)
TRANS(FNEG_v, do_fabs_fneg_v, a, gen_gvec_fneg)

static bool do_fp1_vector(DisasContext *s, arg_qrr_e *a,
                          const FPScalar1 *f, int rmode)
{
    TCGv_i32 tcg_rmode = NULL;
    TCGv_ptr fpst;
    int check = fp_access_check_vector_hsd(s, a->q, a->esz);

    if (check <= 0) {
        return check == 0;
    }

    fpst = fpstatus_ptr(a->esz == MO_16 ? FPST_A64_F16 : FPST_A64);
    if (rmode >= 0) {
        tcg_rmode = gen_set_rmode(rmode, fpst);
    }

    if (a->esz == MO_64) {
        TCGv_i64 t64 = tcg_temp_new_i64();

        for (int pass = 0; pass < 2; ++pass) {
            read_vec_element(s, t64, a->rn, pass, MO_64);
            f->gen_d(t64, t64, fpst);
            write_vec_element(s, t64, a->rd, pass, MO_64);
        }
    } else {
        TCGv_i32 t32 = tcg_temp_new_i32();
        void (*gen)(TCGv_i32, TCGv_i32, TCGv_ptr)
            = (a->esz == MO_16 ? f->gen_h : f->gen_s);

        for (int pass = 0, n = (a->q ? 16 : 8) >> a->esz; pass < n; ++pass) {
            read_vec_element_i32(s, t32, a->rn, pass, a->esz);
            gen(t32, t32, fpst);
            write_vec_element_i32(s, t32, a->rd, pass, a->esz);
        }
    }
    clear_vec_high(s, a->q, a->rd);

    if (rmode >= 0) {
        gen_restore_rmode(tcg_rmode, fpst);
    }
    return true;
}

TRANS(FSQRT_v, do_fp1_vector, a, &f_scalar_fsqrt, -1)

TRANS(FRINTN_v, do_fp1_vector, a, &f_scalar_frint, FPROUNDING_TIEEVEN)
TRANS(FRINTP_v, do_fp1_vector, a, &f_scalar_frint, FPROUNDING_POSINF)
TRANS(FRINTM_v, do_fp1_vector, a, &f_scalar_frint, FPROUNDING_NEGINF)
TRANS(FRINTZ_v, do_fp1_vector, a, &f_scalar_frint, FPROUNDING_ZERO)
TRANS(FRINTA_v, do_fp1_vector, a, &f_scalar_frint, FPROUNDING_TIEAWAY)
TRANS(FRINTI_v, do_fp1_vector, a, &f_scalar_frint, -1)
TRANS(FRINTX_v, do_fp1_vector, a, &f_scalar_frintx, -1)

TRANS_FEAT(FRINT32Z_v, aa64_frint, do_fp1_vector, a,
           &f_scalar_frint32, FPROUNDING_ZERO)
TRANS_FEAT(FRINT32X_v, aa64_frint, do_fp1_vector, a, &f_scalar_frint32, -1)
TRANS_FEAT(FRINT64Z_v, aa64_frint, do_fp1_vector, a,
           &f_scalar_frint64, FPROUNDING_ZERO)
TRANS_FEAT(FRINT64X_v, aa64_frint, do_fp1_vector, a, &f_scalar_frint64, -1)

static bool do_gvec_op2_fpst_with_fpsttype(DisasContext *s, MemOp esz,
                                           bool is_q, int rd, int rn, int data,
                                           gen_helper_gvec_2_ptr * const fns[3],
                                           ARMFPStatusFlavour fpsttype)
{
    int check = fp_access_check_vector_hsd(s, is_q, esz);
    TCGv_ptr fpst;

    if (check <= 0) {
        return check == 0;
    }

    fpst = fpstatus_ptr(fpsttype);
    tcg_gen_gvec_2_ptr(vec_full_reg_offset(s, rd),
                       vec_full_reg_offset(s, rn), fpst,
                       is_q ? 16 : 8, vec_full_reg_size(s),
                       data, fns[esz - 1]);
    return true;
}

static bool do_gvec_op2_fpst(DisasContext *s, MemOp esz, bool is_q,
                             int rd, int rn, int data,
                             gen_helper_gvec_2_ptr * const fns[3])
{
    return do_gvec_op2_fpst_with_fpsttype(s, esz, is_q, rd, rn, data, fns,
                                          esz == MO_16 ? FPST_A64_F16 :
                                          FPST_A64);
}

static bool do_gvec_op2_ah_fpst(DisasContext *s, MemOp esz, bool is_q,
                                int rd, int rn, int data,
                                gen_helper_gvec_2_ptr * const fns[3])
{
    return do_gvec_op2_fpst_with_fpsttype(s, esz, is_q, rd, rn, data,
                                          fns, select_ah_fpst(s, esz));
}

static gen_helper_gvec_2_ptr * const f_scvtf_v[] = {
    gen_helper_gvec_vcvt_sh,
    gen_helper_gvec_vcvt_sf,
    gen_helper_gvec_vcvt_sd,
};
TRANS(SCVTF_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, 0, f_scvtf_v)
TRANS(SCVTF_vf, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, a->shift, f_scvtf_v)

static gen_helper_gvec_2_ptr * const f_ucvtf_v[] = {
    gen_helper_gvec_vcvt_uh,
    gen_helper_gvec_vcvt_uf,
    gen_helper_gvec_vcvt_ud,
};
TRANS(UCVTF_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, 0, f_ucvtf_v)
TRANS(UCVTF_vf, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, a->shift, f_ucvtf_v)

static gen_helper_gvec_2_ptr * const f_fcvtzs_vf[] = {
    gen_helper_gvec_vcvt_rz_hs,
    gen_helper_gvec_vcvt_rz_fs,
    gen_helper_gvec_vcvt_rz_ds,
};
TRANS(FCVTZS_vf, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, a->shift, f_fcvtzs_vf)

static gen_helper_gvec_2_ptr * const f_fcvtzu_vf[] = {
    gen_helper_gvec_vcvt_rz_hu,
    gen_helper_gvec_vcvt_rz_fu,
    gen_helper_gvec_vcvt_rz_du,
};
TRANS(FCVTZU_vf, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, a->shift, f_fcvtzu_vf)

static gen_helper_gvec_2_ptr * const f_fcvt_s_vi[] = {
    gen_helper_gvec_vcvt_rm_sh,
    gen_helper_gvec_vcvt_rm_ss,
    gen_helper_gvec_vcvt_rm_sd,
};

static gen_helper_gvec_2_ptr * const f_fcvt_u_vi[] = {
    gen_helper_gvec_vcvt_rm_uh,
    gen_helper_gvec_vcvt_rm_us,
    gen_helper_gvec_vcvt_rm_ud,
};

TRANS(FCVTNS_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_nearest_even, f_fcvt_s_vi)
TRANS(FCVTNU_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_nearest_even, f_fcvt_u_vi)
TRANS(FCVTPS_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_up, f_fcvt_s_vi)
TRANS(FCVTPU_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_up, f_fcvt_u_vi)
TRANS(FCVTMS_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_down, f_fcvt_s_vi)
TRANS(FCVTMU_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_down, f_fcvt_u_vi)
TRANS(FCVTZS_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_to_zero, f_fcvt_s_vi)
TRANS(FCVTZU_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_to_zero, f_fcvt_u_vi)
TRANS(FCVTAS_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_ties_away, f_fcvt_s_vi)
TRANS(FCVTAU_vi, do_gvec_op2_fpst,
      a->esz, a->q, a->rd, a->rn, float_round_ties_away, f_fcvt_u_vi)

static gen_helper_gvec_2_ptr * const f_fceq0[] = {
    gen_helper_gvec_fceq0_h,
    gen_helper_gvec_fceq0_s,
    gen_helper_gvec_fceq0_d,
};
TRANS(FCMEQ0_v, do_gvec_op2_fpst, a->esz, a->q, a->rd, a->rn, 0, f_fceq0)

static gen_helper_gvec_2_ptr * const f_fcgt0[] = {
    gen_helper_gvec_fcgt0_h,
    gen_helper_gvec_fcgt0_s,
    gen_helper_gvec_fcgt0_d,
};
TRANS(FCMGT0_v, do_gvec_op2_fpst, a->esz, a->q, a->rd, a->rn, 0, f_fcgt0)

static gen_helper_gvec_2_ptr * const f_fcge0[] = {
    gen_helper_gvec_fcge0_h,
    gen_helper_gvec_fcge0_s,
    gen_helper_gvec_fcge0_d,
};
TRANS(FCMGE0_v, do_gvec_op2_fpst, a->esz, a->q, a->rd, a->rn, 0, f_fcge0)

static gen_helper_gvec_2_ptr * const f_fclt0[] = {
    gen_helper_gvec_fclt0_h,
    gen_helper_gvec_fclt0_s,
    gen_helper_gvec_fclt0_d,
};
TRANS(FCMLT0_v, do_gvec_op2_fpst, a->esz, a->q, a->rd, a->rn, 0, f_fclt0)

static gen_helper_gvec_2_ptr * const f_fcle0[] = {
    gen_helper_gvec_fcle0_h,
    gen_helper_gvec_fcle0_s,
    gen_helper_gvec_fcle0_d,
};
TRANS(FCMLE0_v, do_gvec_op2_fpst, a->esz, a->q, a->rd, a->rn, 0, f_fcle0)

static gen_helper_gvec_2_ptr * const f_frecpe[] = {
    gen_helper_gvec_frecpe_h,
    gen_helper_gvec_frecpe_s,
    gen_helper_gvec_frecpe_d,
};
static gen_helper_gvec_2_ptr * const f_frecpe_rpres[] = {
    gen_helper_gvec_frecpe_h,
    gen_helper_gvec_frecpe_rpres_s,
    gen_helper_gvec_frecpe_d,
};
TRANS(FRECPE_v, do_gvec_op2_ah_fpst, a->esz, a->q, a->rd, a->rn, 0,
      s->fpcr_ah && dc_isar_feature(aa64_rpres, s) ? f_frecpe_rpres : f_frecpe)

static gen_helper_gvec_2_ptr * const f_frsqrte[] = {
    gen_helper_gvec_frsqrte_h,
    gen_helper_gvec_frsqrte_s,
    gen_helper_gvec_frsqrte_d,
};
static gen_helper_gvec_2_ptr * const f_frsqrte_rpres[] = {
    gen_helper_gvec_frsqrte_h,
    gen_helper_gvec_frsqrte_rpres_s,
    gen_helper_gvec_frsqrte_d,
};
TRANS(FRSQRTE_v, do_gvec_op2_ah_fpst, a->esz, a->q, a->rd, a->rn, 0,
      s->fpcr_ah && dc_isar_feature(aa64_rpres, s) ? f_frsqrte_rpres : f_frsqrte)

static bool trans_FCVTL_v(DisasContext *s, arg_qrr_e *a)
{
    /* Handle 2-reg-misc ops which are widening (so each size element
     * in the source becomes a 2*size element in the destination.
     * The only instruction like this is FCVTL.
     */
    int pass;
    TCGv_ptr fpst;

    if (!fp_access_check(s)) {
        return true;
    }

    if (a->esz == MO_64) {
        /* 32 -> 64 bit fp conversion */
        TCGv_i64 tcg_res[2];
        TCGv_i32 tcg_op = tcg_temp_new_i32();
        int srcelt = a->q ? 2 : 0;

        fpst = fpstatus_ptr(FPST_A64);

        for (pass = 0; pass < 2; pass++) {
            tcg_res[pass] = tcg_temp_new_i64();
            read_vec_element_i32(s, tcg_op, a->rn, srcelt + pass, MO_32);
            gen_helper_vfp_fcvtds(tcg_res[pass], tcg_op, fpst);
        }
        for (pass = 0; pass < 2; pass++) {
            write_vec_element(s, tcg_res[pass], a->rd, pass, MO_64);
        }
    } else {
        /* 16 -> 32 bit fp conversion */
        int srcelt = a->q ? 4 : 0;
        TCGv_i32 tcg_res[4];
        TCGv_i32 ahp = get_ahp_flag();

        fpst = fpstatus_ptr(FPST_A64_F16);

        for (pass = 0; pass < 4; pass++) {
            tcg_res[pass] = tcg_temp_new_i32();
            read_vec_element_i32(s, tcg_res[pass], a->rn, srcelt + pass, MO_16);
            gen_helper_vfp_fcvt_f16_to_f32(tcg_res[pass], tcg_res[pass],
                                           fpst, ahp);
        }
        for (pass = 0; pass < 4; pass++) {
            write_vec_element_i32(s, tcg_res[pass], a->rd, pass, MO_32);
        }
    }
    clear_vec_high(s, true, a->rd);
    return true;
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
    dc->current_el = arm_mmu_idx_to_el(dc->mmu_idx);
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
    dc->zt0_excp_el = EX_TBFLAG_A64(tb_flags, ZT0EXC_EL);
    dc->vl = (EX_TBFLAG_A64(tb_flags, VL) + 1) * 16;
    dc->svl = (EX_TBFLAG_A64(tb_flags, SVL) + 1) * 16;
    dc->max_svl = arm_cpu->sme_max_vq * 16;
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
    dc->e2h = EX_TBFLAG_A64(tb_flags, E2H);
    dc->nv = EX_TBFLAG_A64(tb_flags, NV);
    dc->nv1 = EX_TBFLAG_A64(tb_flags, NV1);
    dc->nv2 = EX_TBFLAG_A64(tb_flags, NV2);
    dc->nv2_mem_e20 = dc->nv2 && dc->e2h;
    dc->nv2_mem_be = EX_TBFLAG_A64(tb_flags, NV2_MEM_BE);
    dc->fpcr_ah = EX_TBFLAG_A64(tb_flags, AH);
    dc->fpcr_nep = EX_TBFLAG_A64(tb_flags, NEP);
    dc->gcs_en = EX_TBFLAG_A64(tb_flags, GCS_EN);
    dc->gcs_rvcen = EX_TBFLAG_A64(tb_flags, GCS_RVCEN);
    dc->gcsstr_el = EX_TBFLAG_A64(tb_flags, GCSSTR_EL);
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
        gen_helper_exception_pc_alignment(tcg_env, tcg_constant_vaddr(pc));
        s->base.is_jmp = DISAS_NORETURN;
        s->base.pc_next = QEMU_ALIGN_UP(pc, 4);
        return;
    }

    s->pc_curr = pc;
    insn = arm_ldl_code(env, &s->base, pc, s->sctlr_b);
    s->insn = insn;
    s->base.pc_next = pc + 4;

    s->fp_access_checked = 0;
    s->sve_access_checked = 0;

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
        unallocated_encoding(s);
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

    emit_delayed_exceptions(dc);
}

const TranslatorOps aarch64_translator_ops = {
    .init_disas_context = aarch64_tr_init_disas_context,
    .tb_start           = aarch64_tr_tb_start,
    .insn_start         = aarch64_tr_insn_start,
    .translate_insn     = aarch64_tr_translate_insn,
    .tb_stop            = aarch64_tr_tb_stop,
};
