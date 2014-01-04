/*
 *  AArch64 translation
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "translate.h"
#include "qemu/host-utils.h"

#include "exec/gen-icount.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"

static TCGv_i64 cpu_X[32];
static TCGv_i64 cpu_pc;
static TCGv_i32 cpu_NF, cpu_ZF, cpu_CF, cpu_VF;

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

/* initialize TCG globals.  */
void a64_translate_init(void)
{
    int i;

    cpu_pc = tcg_global_mem_new_i64(TCG_AREG0,
                                    offsetof(CPUARMState, pc),
                                    "pc");
    for (i = 0; i < 32; i++) {
        cpu_X[i] = tcg_global_mem_new_i64(TCG_AREG0,
                                          offsetof(CPUARMState, xregs[i]),
                                          regnames[i]);
    }

    cpu_NF = tcg_global_mem_new_i32(TCG_AREG0, offsetof(CPUARMState, NF), "NF");
    cpu_ZF = tcg_global_mem_new_i32(TCG_AREG0, offsetof(CPUARMState, ZF), "ZF");
    cpu_CF = tcg_global_mem_new_i32(TCG_AREG0, offsetof(CPUARMState, CF), "CF");
    cpu_VF = tcg_global_mem_new_i32(TCG_AREG0, offsetof(CPUARMState, VF), "VF");
}

void aarch64_cpu_dump_state(CPUState *cs, FILE *f,
                            fprintf_function cpu_fprintf, int flags)
{
    ARMCPU *cpu = ARM_CPU(cs);
    CPUARMState *env = &cpu->env;
    uint32_t psr = pstate_read(env);
    int i;

    cpu_fprintf(f, "PC=%016"PRIx64"  SP=%016"PRIx64"\n",
            env->pc, env->xregs[31]);
    for (i = 0; i < 31; i++) {
        cpu_fprintf(f, "X%02d=%016"PRIx64, i, env->xregs[i]);
        if ((i % 4) == 3) {
            cpu_fprintf(f, "\n");
        } else {
            cpu_fprintf(f, " ");
        }
    }
    cpu_fprintf(f, "PSTATE=%08x (flags %c%c%c%c)\n",
                psr,
                psr & PSTATE_N ? 'N' : '-',
                psr & PSTATE_Z ? 'Z' : '-',
                psr & PSTATE_C ? 'C' : '-',
                psr & PSTATE_V ? 'V' : '-');
    cpu_fprintf(f, "\n");
}

static int get_mem_index(DisasContext *s)
{
#ifdef CONFIG_USER_ONLY
    return 1;
#else
    return s->user;
#endif
}

void gen_a64_set_pc_im(uint64_t val)
{
    tcg_gen_movi_i64(cpu_pc, val);
}

static void gen_exception(int excp)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_movi_i32(tmp, excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free_i32(tmp);
}

static void gen_exception_insn(DisasContext *s, int offset, int excp)
{
    gen_a64_set_pc_im(s->pc - offset);
    gen_exception(excp);
    s->is_jmp = DISAS_EXC;
}

static inline bool use_goto_tb(DisasContext *s, int n, uint64_t dest)
{
    /* No direct tb linking with singlestep or deterministic io */
    if (s->singlestep_enabled || (s->tb->cflags & CF_LAST_IO)) {
        return false;
    }

    /* Only link tbs from inside the same guest page */
    if ((s->tb->pc & TARGET_PAGE_MASK) != (dest & TARGET_PAGE_MASK)) {
        return false;
    }

    return true;
}

static inline void gen_goto_tb(DisasContext *s, int n, uint64_t dest)
{
    TranslationBlock *tb;

    tb = s->tb;
    if (use_goto_tb(s, n, dest)) {
        tcg_gen_goto_tb(n);
        gen_a64_set_pc_im(dest);
        tcg_gen_exit_tb((tcg_target_long)tb + n);
        s->is_jmp = DISAS_TB_JUMP;
    } else {
        gen_a64_set_pc_im(dest);
        if (s->singlestep_enabled) {
            gen_exception(EXCP_DEBUG);
        }
        tcg_gen_exit_tb(0);
        s->is_jmp = DISAS_JUMP;
    }
}

static void unallocated_encoding(DisasContext *s)
{
    gen_exception_insn(s, 4, EXCP_UDEF);
}

#define unsupported_encoding(s, insn)                                    \
    do {                                                                 \
        qemu_log_mask(LOG_UNIMP,                                         \
                      "%s:%d: unsupported instruction encoding 0x%08x "  \
                      "at pc=%016" PRIx64 "\n",                          \
                      __FILE__, __LINE__, insn, s->pc - 4);              \
        unallocated_encoding(s);                                         \
    } while (0);

static void init_tmp_a64_array(DisasContext *s)
{
#ifdef CONFIG_DEBUG_TCG
    int i;
    for (i = 0; i < ARRAY_SIZE(s->tmp_a64); i++) {
        TCGV_UNUSED_I64(s->tmp_a64[i]);
    }
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

static TCGv_i64 new_tmp_a64(DisasContext *s)
{
    assert(s->tmp_a64_count < TMP_A64_MAX);
    return s->tmp_a64[s->tmp_a64_count++] = tcg_temp_new_i64();
}

static TCGv_i64 new_tmp_a64_zero(DisasContext *s)
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
static TCGv_i64 cpu_reg(DisasContext *s, int reg)
{
    if (reg == 31) {
        return new_tmp_a64_zero(s);
    } else {
        return cpu_X[reg];
    }
}

/* register access for when 31 == SP */
static TCGv_i64 cpu_reg_sp(DisasContext *s, int reg)
{
    return cpu_X[reg];
}

/* read a cpu register in 32bit/64bit mode. Returns a TCGv_i64
 * representing the register contents. This TCGv is an auto-freed
 * temporary so it need not be explicitly freed, and may be modified.
 */
static TCGv_i64 read_cpu_reg(DisasContext *s, int reg, int sf)
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

static TCGv_i64 read_cpu_reg_sp(DisasContext *s, int reg, int sf)
{
    TCGv_i64 v = new_tmp_a64(s);
    if (sf) {
        tcg_gen_mov_i64(v, cpu_X[reg]);
    } else {
        tcg_gen_ext32u_i64(v, cpu_X[reg]);
    }
    return v;
}

/* Set ZF and NF based on a 64 bit result. This is alas fiddlier
 * than the 32 bit equivalent.
 */
static inline void gen_set_NZ64(TCGv_i64 result)
{
    TCGv_i64 flag = tcg_temp_new_i64();

    tcg_gen_setcondi_i64(TCG_COND_NE, flag, result, 0);
    tcg_gen_trunc_i64_i32(cpu_ZF, flag);
    tcg_gen_shri_i64(flag, result, 32);
    tcg_gen_trunc_i64_i32(cpu_NF, flag);
    tcg_temp_free_i64(flag);
}

/* Set NZCV as for a logical operation: NZ as per result, CV cleared. */
static inline void gen_logic_CC(int sf, TCGv_i64 result)
{
    if (sf) {
        gen_set_NZ64(result);
    } else {
        tcg_gen_trunc_i64_i32(cpu_ZF, result);
        tcg_gen_trunc_i64_i32(cpu_NF, result);
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

        tcg_gen_trunc_i64_i32(cpu_CF, flag);

        gen_set_NZ64(result);

        tcg_gen_xor_i64(flag, result, t0);
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_andc_i64(flag, flag, tmp);
        tcg_temp_free_i64(tmp);
        tcg_gen_shri_i64(flag, flag, 32);
        tcg_gen_trunc_i64_i32(cpu_VF, flag);

        tcg_gen_mov_i64(dest, result);
        tcg_temp_free_i64(result);
        tcg_temp_free_i64(flag);
    } else {
        /* 32 bit arithmetic */
        TCGv_i32 t0_32 = tcg_temp_new_i32();
        TCGv_i32 t1_32 = tcg_temp_new_i32();
        TCGv_i32 tmp = tcg_temp_new_i32();

        tcg_gen_movi_i32(tmp, 0);
        tcg_gen_trunc_i64_i32(t0_32, t0);
        tcg_gen_trunc_i64_i32(t1_32, t1);
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
        tcg_gen_trunc_i64_i32(cpu_CF, flag);

        tcg_gen_xor_i64(flag, result, t0);
        tmp = tcg_temp_new_i64();
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_and_i64(flag, flag, tmp);
        tcg_temp_free_i64(tmp);
        tcg_gen_shri_i64(flag, flag, 32);
        tcg_gen_trunc_i64_i32(cpu_VF, flag);
        tcg_gen_mov_i64(dest, result);
        tcg_temp_free_i64(flag);
        tcg_temp_free_i64(result);
    } else {
        /* 32 bit arithmetic */
        TCGv_i32 t0_32 = tcg_temp_new_i32();
        TCGv_i32 t1_32 = tcg_temp_new_i32();
        TCGv_i32 tmp;

        tcg_gen_trunc_i64_i32(t0_32, t0);
        tcg_gen_trunc_i64_i32(t1_32, t1);
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
        tcg_gen_trunc_i64_i32(cpu_CF, cf_64);
        gen_set_NZ64(result);

        tcg_gen_xor_i64(vf_64, result, t0);
        tcg_gen_xor_i64(tmp, t0, t1);
        tcg_gen_andc_i64(vf_64, vf_64, tmp);
        tcg_gen_shri_i64(vf_64, vf_64, 32);
        tcg_gen_trunc_i64_i32(cpu_VF, vf_64);

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

        tcg_gen_trunc_i64_i32(t0_32, t0);
        tcg_gen_trunc_i64_i32(t1_32, t1);
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
 * Store from GPR register to memory
 */
static void do_gpr_st(DisasContext *s, TCGv_i64 source,
                      TCGv_i64 tcg_addr, int size)
{
    g_assert(size <= 3);
    tcg_gen_qemu_st_i64(source, tcg_addr, get_mem_index(s), MO_TE + size);
}

/*
 * Load from memory to GPR register
 */
static void do_gpr_ld(DisasContext *s, TCGv_i64 dest, TCGv_i64 tcg_addr,
                      int size, bool is_signed, bool extend)
{
    TCGMemOp memop = MO_TE + size;

    g_assert(size <= 3);

    if (is_signed) {
        memop += MO_SIGN;
    }

    tcg_gen_qemu_ld_i64(dest, tcg_addr, get_mem_index(s), memop);

    if (extend && is_signed) {
        g_assert(size < 3);
        tcg_gen_ext32u_i64(dest, dest);
    }
}

/*
 * Store from FP register to memory
 */
static void do_fp_st(DisasContext *s, int srcidx, TCGv_i64 tcg_addr, int size)
{
    /* This writes the bottom N bits of a 128 bit wide vector to memory */
    int freg_offs = offsetof(CPUARMState, vfp.regs[srcidx * 2]);
    TCGv_i64 tmp = tcg_temp_new_i64();

    if (size < 4) {
        switch (size) {
        case 0:
            tcg_gen_ld8u_i64(tmp, cpu_env, freg_offs);
            break;
        case 1:
            tcg_gen_ld16u_i64(tmp, cpu_env, freg_offs);
            break;
        case 2:
            tcg_gen_ld32u_i64(tmp, cpu_env, freg_offs);
            break;
        case 3:
            tcg_gen_ld_i64(tmp, cpu_env, freg_offs);
            break;
        }
        tcg_gen_qemu_st_i64(tmp, tcg_addr, get_mem_index(s), MO_TE + size);
    } else {
        TCGv_i64 tcg_hiaddr = tcg_temp_new_i64();
        tcg_gen_ld_i64(tmp, cpu_env, freg_offs);
        tcg_gen_qemu_st_i64(tmp, tcg_addr, get_mem_index(s), MO_TEQ);
        tcg_gen_qemu_st64(tmp, tcg_addr, get_mem_index(s));
        tcg_gen_ld_i64(tmp, cpu_env, freg_offs + sizeof(float64));
        tcg_gen_addi_i64(tcg_hiaddr, tcg_addr, 8);
        tcg_gen_qemu_st_i64(tmp, tcg_hiaddr, get_mem_index(s), MO_TEQ);
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
    int freg_offs = offsetof(CPUARMState, vfp.regs[destidx * 2]);
    TCGv_i64 tmplo = tcg_temp_new_i64();
    TCGv_i64 tmphi;

    if (size < 4) {
        TCGMemOp memop = MO_TE + size;
        tmphi = tcg_const_i64(0);
        tcg_gen_qemu_ld_i64(tmplo, tcg_addr, get_mem_index(s), memop);
    } else {
        TCGv_i64 tcg_hiaddr;
        tmphi = tcg_temp_new_i64();
        tcg_hiaddr = tcg_temp_new_i64();

        tcg_gen_qemu_ld_i64(tmplo, tcg_addr, get_mem_index(s), MO_TEQ);
        tcg_gen_addi_i64(tcg_hiaddr, tcg_addr, 8);
        tcg_gen_qemu_ld_i64(tmphi, tcg_hiaddr, get_mem_index(s), MO_TEQ);
        tcg_temp_free_i64(tcg_hiaddr);
    }

    tcg_gen_st_i64(tmplo, cpu_env, freg_offs);
    tcg_gen_st_i64(tmphi, cpu_env, freg_offs + sizeof(float64));

    tcg_temp_free_i64(tmplo);
    tcg_temp_free_i64(tmphi);
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
 * the instruction disassembly implemented here matches
 * the instruction encoding classifications in chapter 3 (C3)
 * of the ARM Architecture Reference Manual (DDI0487A_a)
 */

/* C3.2.7 Unconditional branch (immediate)
 *   31  30       26 25                                  0
 * +----+-----------+-------------------------------------+
 * | op | 0 0 1 0 1 |                 imm26               |
 * +----+-----------+-------------------------------------+
 */
static void disas_uncond_b_imm(DisasContext *s, uint32_t insn)
{
    uint64_t addr = s->pc + sextract32(insn, 0, 26) * 4 - 4;

    if (insn & (1 << 31)) {
        /* C5.6.26 BL Branch with link */
        tcg_gen_movi_i64(cpu_reg(s, 30), s->pc);
    }

    /* C5.6.20 B Branch / C5.6.26 BL Branch with link */
    gen_goto_tb(s, 0, addr);
}

/* C3.2.1 Compare & branch (immediate)
 *   31  30         25  24  23                  5 4      0
 * +----+-------------+----+---------------------+--------+
 * | sf | 0 1 1 0 1 0 | op |         imm19       |   Rt   |
 * +----+-------------+----+---------------------+--------+
 */
static void disas_comp_b_imm(DisasContext *s, uint32_t insn)
{
    unsigned int sf, op, rt;
    uint64_t addr;
    int label_match;
    TCGv_i64 tcg_cmp;

    sf = extract32(insn, 31, 1);
    op = extract32(insn, 24, 1); /* 0: CBZ; 1: CBNZ */
    rt = extract32(insn, 0, 5);
    addr = s->pc + sextract32(insn, 5, 19) * 4 - 4;

    tcg_cmp = read_cpu_reg(s, rt, sf);
    label_match = gen_new_label();

    tcg_gen_brcondi_i64(op ? TCG_COND_NE : TCG_COND_EQ,
                        tcg_cmp, 0, label_match);

    gen_goto_tb(s, 0, s->pc);
    gen_set_label(label_match);
    gen_goto_tb(s, 1, addr);
}

/* C3.2.5 Test & branch (immediate)
 *   31  30         25  24  23   19 18          5 4    0
 * +----+-------------+----+-------+-------------+------+
 * | b5 | 0 1 1 0 1 1 | op |  b40  |    imm14    |  Rt  |
 * +----+-------------+----+-------+-------------+------+
 */
static void disas_test_b_imm(DisasContext *s, uint32_t insn)
{
    unsigned int bit_pos, op, rt;
    uint64_t addr;
    int label_match;
    TCGv_i64 tcg_cmp;

    bit_pos = (extract32(insn, 31, 1) << 5) | extract32(insn, 19, 5);
    op = extract32(insn, 24, 1); /* 0: TBZ; 1: TBNZ */
    addr = s->pc + sextract32(insn, 5, 14) * 4 - 4;
    rt = extract32(insn, 0, 5);

    tcg_cmp = tcg_temp_new_i64();
    tcg_gen_andi_i64(tcg_cmp, cpu_reg(s, rt), (1ULL << bit_pos));
    label_match = gen_new_label();
    tcg_gen_brcondi_i64(op ? TCG_COND_NE : TCG_COND_EQ,
                        tcg_cmp, 0, label_match);
    tcg_temp_free_i64(tcg_cmp);
    gen_goto_tb(s, 0, s->pc);
    gen_set_label(label_match);
    gen_goto_tb(s, 1, addr);
}

/* C3.2.2 / C5.6.19 Conditional branch (immediate)
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
    addr = s->pc + sextract32(insn, 5, 19) * 4 - 4;
    cond = extract32(insn, 0, 4);

    if (cond < 0x0e) {
        /* genuinely conditional branches */
        int label_match = gen_new_label();
        arm_gen_test_cc(cond, label_match);
        gen_goto_tb(s, 0, s->pc);
        gen_set_label(label_match);
        gen_goto_tb(s, 1, addr);
    } else {
        /* 0xe and 0xf are both "always" conditions */
        gen_goto_tb(s, 0, addr);
    }
}

/* C5.6.68 HINT */
static void handle_hint(DisasContext *s, uint32_t insn,
                        unsigned int op1, unsigned int op2, unsigned int crm)
{
    unsigned int selector = crm << 3 | op2;

    if (op1 != 3) {
        unallocated_encoding(s);
        return;
    }

    switch (selector) {
    case 0: /* NOP */
        return;
    case 1: /* YIELD */
    case 2: /* WFE */
    case 3: /* WFI */
    case 4: /* SEV */
    case 5: /* SEVL */
        /* we treat all as NOP at least for now */
        return;
    default:
        /* default specified as NOP equivalent */
        return;
    }
}

/* CLREX, DSB, DMB, ISB */
static void handle_sync(DisasContext *s, uint32_t insn,
                        unsigned int op1, unsigned int op2, unsigned int crm)
{
    if (op1 != 3) {
        unallocated_encoding(s);
        return;
    }

    switch (op2) {
    case 2: /* CLREX */
        unsupported_encoding(s, insn);
        return;
    case 4: /* DSB */
    case 5: /* DMB */
    case 6: /* ISB */
        /* We don't emulate caches so barriers are no-ops */
        return;
    default:
        unallocated_encoding(s);
        return;
    }
}

/* C5.6.130 MSR (immediate) - move immediate to processor state field */
static void handle_msr_i(DisasContext *s, uint32_t insn,
                         unsigned int op1, unsigned int op2, unsigned int crm)
{
    unsupported_encoding(s, insn);
}

static void gen_get_nzcv(TCGv_i64 tcg_rt)
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 nzcv = tcg_temp_new_i32();

    /* build bit 31, N */
    tcg_gen_andi_i32(nzcv, cpu_NF, (1 << 31));
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
    tcg_gen_trunc_i64_i32(nzcv, tcg_rt);

    /* bit 31, N */
    tcg_gen_andi_i32(cpu_NF, nzcv, (1 << 31));
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

/* C5.6.129 MRS - move from system register
 * C5.6.131 MSR (register) - move to system register
 * C5.6.204 SYS
 * C5.6.205 SYSL
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
        /* Unknown register */
        unallocated_encoding(s);
        return;
    }

    /* Check access permissions */
    if (!cp_access_ok(s->current_pl, ri, isread)) {
        unallocated_encoding(s);
        return;
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
    default:
        break;
    }

    if (use_icount && (ri->type & ARM_CP_IO)) {
        gen_io_start();
    }

    tcg_rt = cpu_reg(s, rt);

    if (isread) {
        if (ri->type & ARM_CP_CONST) {
            tcg_gen_movi_i64(tcg_rt, ri->resetvalue);
        } else if (ri->readfn) {
            TCGv_ptr tmpptr;
            gen_a64_set_pc_im(s->pc - 4);
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
            gen_a64_set_pc_im(s->pc - 4);
            tmpptr = tcg_const_ptr(ri);
            gen_helper_set_cp_reg64(cpu_env, tmpptr, tcg_rt);
            tcg_temp_free_ptr(tmpptr);
        } else {
            tcg_gen_st_i64(tcg_rt, cpu_env, ri->fieldoffset);
        }
    }

    if (use_icount && (ri->type & ARM_CP_IO)) {
        /* I/O operations must end the TB here (whether read or write) */
        gen_io_end();
        s->is_jmp = DISAS_UPDATE;
    } else if (!isread && !(ri->type & ARM_CP_SUPPRESS_TB_END)) {
        /* We default to ending the TB on a coprocessor register write,
         * but allow this to be suppressed by the register definition
         * (usually only necessary to work around guest bugs).
         */
        s->is_jmp = DISAS_UPDATE;
    }
}

/* C3.2.4 System
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
        case 2: /* C5.6.68 HINT */
            handle_hint(s, insn, op1, op2, crm);
            break;
        case 3: /* CLREX, DSB, DMB, ISB */
            handle_sync(s, insn, op1, op2, crm);
            break;
        case 4: /* C5.6.130 MSR (immediate) */
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

/* C3.2.3 Exception generation
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

    switch (opc) {
    case 0:
        /* SVC, HVC, SMC; since we don't support the Virtualization
         * or TrustZone extensions these all UNDEF except SVC.
         */
        if (op2_ll != 1) {
            unallocated_encoding(s);
            break;
        }
        gen_exception_insn(s, 0, EXCP_SWI);
        break;
    case 1:
        if (op2_ll != 0) {
            unallocated_encoding(s);
            break;
        }
        /* BRK */
        gen_exception_insn(s, 0, EXCP_BKPT);
        break;
    case 2:
        if (op2_ll != 0) {
            unallocated_encoding(s);
            break;
        }
        /* HLT */
        unsupported_encoding(s, insn);
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

/* C3.2.7 Unconditional branch (register)
 *  31           25 24   21 20   16 15   10 9    5 4     0
 * +---------------+-------+-------+-------+------+-------+
 * | 1 1 0 1 0 1 1 |  opc  |  op2  |  op3  |  Rn  |  op4  |
 * +---------------+-------+-------+-------+------+-------+
 */
static void disas_uncond_b_reg(DisasContext *s, uint32_t insn)
{
    unsigned int opc, op2, op3, rn, op4;

    opc = extract32(insn, 21, 4);
    op2 = extract32(insn, 16, 5);
    op3 = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    op4 = extract32(insn, 0, 5);

    if (op4 != 0x0 || op3 != 0x0 || op2 != 0x1f) {
        unallocated_encoding(s);
        return;
    }

    switch (opc) {
    case 0: /* BR */
    case 2: /* RET */
        break;
    case 1: /* BLR */
        tcg_gen_movi_i64(cpu_reg(s, 30), s->pc);
        break;
    case 4: /* ERET */
    case 5: /* DRPS */
        if (rn != 0x1f) {
            unallocated_encoding(s);
        } else {
            unsupported_encoding(s, insn);
        }
        return;
    default:
        unallocated_encoding(s);
        return;
    }

    tcg_gen_mov_i64(cpu_pc, cpu_reg(s, rn));
    s->is_jmp = DISAS_JUMP;
}

/* C3.2 Branches, exception generating and system instructions */
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
            disas_system(s, insn);
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

/* Load/store exclusive */
static void disas_ldst_excl(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* Load register (literal) */
static void disas_ld_lit(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/*
 * C5.6.80 LDNP (Load Pair - non-temporal hint)
 * C5.6.81 LDP (Load Pair - non vector)
 * C5.6.82 LDPSW (Load Pair Signed Word - non vector)
 * C5.6.176 STNP (Store Pair - non-temporal hint)
 * C5.6.177 STP (Store Pair - non vector)
 * C6.3.165 LDNP (Load Pair of SIMD&FP - non-temporal hint)
 * C6.3.165 LDP (Load Pair of SIMD&FP)
 * C6.3.284 STNP (Store Pair of SIMD&FP - non-temporal hint)
 * C6.3.284 STP (Store Pair of SIMD&FP)
 *
 *  31 30 29   27  26  25 24   23  22 21   15 14   10 9    5 4    0
 * +-----+-------+---+---+-------+---+-----------------------------+
 * | opc | 1 0 1 | V | 0 | index | L |  imm7 |  Rt2  |  Rn  | Rt   |
 * +-----+-------+---+---+-------+---+-------+-------+------+------+
 *
 * opc: LDP/STP/LDNP/STNP        00 -> 32 bit, 10 -> 64 bit
 *      LDPSW                    01
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
    int64_t offset = sextract32(insn, 15, 7);
    int index = extract32(insn, 23, 2);
    bool is_vector = extract32(insn, 26, 1);
    bool is_load = extract32(insn, 22, 1);
    int opc = extract32(insn, 30, 2);

    bool is_signed = false;
    bool postindex = false;
    bool wback = false;

    TCGv_i64 tcg_addr; /* calculated address */
    int size;

    if (opc == 3) {
        unallocated_encoding(s);
        return;
    }

    if (is_vector) {
        size = 2 + opc;
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

    offset <<= size;

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }

    tcg_addr = read_cpu_reg_sp(s, rn, 1);

    if (!postindex) {
        tcg_gen_addi_i64(tcg_addr, tcg_addr, offset);
    }

    if (is_vector) {
        if (is_load) {
            do_fp_ld(s, rt, tcg_addr, size);
        } else {
            do_fp_st(s, rt, tcg_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        if (is_load) {
            do_gpr_ld(s, tcg_rt, tcg_addr, size, is_signed, false);
        } else {
            do_gpr_st(s, tcg_rt, tcg_addr, size);
        }
    }
    tcg_gen_addi_i64(tcg_addr, tcg_addr, 1 << size);
    if (is_vector) {
        if (is_load) {
            do_fp_ld(s, rt2, tcg_addr, size);
        } else {
            do_fp_st(s, rt2, tcg_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt2 = cpu_reg(s, rt2);
        if (is_load) {
            do_gpr_ld(s, tcg_rt2, tcg_addr, size, is_signed, false);
        } else {
            do_gpr_st(s, tcg_rt2, tcg_addr, size);
        }
    }

    if (wback) {
        if (postindex) {
            tcg_gen_addi_i64(tcg_addr, tcg_addr, offset - (1 << size));
        } else {
            tcg_gen_subi_i64(tcg_addr, tcg_addr, 1 << size);
        }
        tcg_gen_mov_i64(cpu_reg_sp(s, rn), tcg_addr);
    }
}

/*
 * C3.3.8 Load/store (immediate post-indexed)
 * C3.3.9 Load/store (immediate pre-indexed)
 * C3.3.12 Load/store (unscaled immediate)
 *
 * 31 30 29   27  26 25 24 23 22 21  20    12 11 10 9    5 4    0
 * +----+-------+---+-----+-----+---+--------+-----+------+------+
 * |size| 1 1 1 | V | 0 0 | opc | 0 |  imm9  | idx |  Rn  |  Rt  |
 * +----+-------+---+-----+-----+---+--------+-----+------+------+
 *
 * idx = 01 -> post-indexed, 11 pre-indexed, 00 unscaled imm. (no writeback)
 * V = 0 -> non-vector
 * size: 00 -> 8 bit, 01 -> 16 bit, 10 -> 32 bit, 11 -> 64bit
 * opc: 00 -> store, 01 -> loadu, 10 -> loads 64, 11 -> loads 32
 */
static void disas_ldst_reg_imm9(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int imm9 = sextract32(insn, 12, 9);
    int opc = extract32(insn, 22, 2);
    int size = extract32(insn, 30, 2);
    int idx = extract32(insn, 10, 2);
    bool is_signed = false;
    bool is_store = false;
    bool is_extended = false;
    bool is_vector = extract32(insn, 26, 1);
    bool post_index;
    bool writeback;

    TCGv_i64 tcg_addr;

    if (is_vector) {
        size |= (opc & 2) << 1;
        if (size > 4) {
            unallocated_encoding(s);
            return;
        }
        is_store = ((opc & 1) == 0);
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
        is_signed = opc & (1<<1);
        is_extended = (size < 3) && (opc & 1);
    }

    switch (idx) {
    case 0:
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
    case 2:
        g_assert(false);
        break;
    }

    if (rn == 31) {
        gen_check_sp_alignment(s);
    }
    tcg_addr = read_cpu_reg_sp(s, rn, 1);

    if (!post_index) {
        tcg_gen_addi_i64(tcg_addr, tcg_addr, imm9);
    }

    if (is_vector) {
        if (is_store) {
            do_fp_st(s, rt, tcg_addr, size);
        } else {
            do_fp_ld(s, rt, tcg_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        if (is_store) {
            do_gpr_st(s, tcg_rt, tcg_addr, size);
        } else {
            do_gpr_ld(s, tcg_rt, tcg_addr, size, is_signed, is_extended);
        }
    }

    if (writeback) {
        TCGv_i64 tcg_rn = cpu_reg_sp(s, rn);
        if (post_index) {
            tcg_gen_addi_i64(tcg_addr, tcg_addr, imm9);
        }
        tcg_gen_mov_i64(tcg_rn, tcg_addr);
    }
}

/*
 * C3.3.10 Load/store (register offset)
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
static void disas_ldst_reg_roffset(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    int shift = extract32(insn, 12, 1);
    int rm = extract32(insn, 16, 5);
    int opc = extract32(insn, 22, 2);
    int opt = extract32(insn, 13, 3);
    int size = extract32(insn, 30, 2);
    bool is_signed = false;
    bool is_store = false;
    bool is_extended = false;
    bool is_vector = extract32(insn, 26, 1);

    TCGv_i64 tcg_rm;
    TCGv_i64 tcg_addr;

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
    tcg_addr = read_cpu_reg_sp(s, rn, 1);

    tcg_rm = read_cpu_reg(s, rm, 1);
    ext_and_shift_reg(tcg_rm, tcg_rm, opt, shift ? size : 0);

    tcg_gen_add_i64(tcg_addr, tcg_addr, tcg_rm);

    if (is_vector) {
        if (is_store) {
            do_fp_st(s, rt, tcg_addr, size);
        } else {
            do_fp_ld(s, rt, tcg_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        if (is_store) {
            do_gpr_st(s, tcg_rt, tcg_addr, size);
        } else {
            do_gpr_ld(s, tcg_rt, tcg_addr, size, is_signed, is_extended);
        }
    }
}

/*
 * C3.3.13 Load/store (unsigned immediate)
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
static void disas_ldst_reg_unsigned_imm(DisasContext *s, uint32_t insn)
{
    int rt = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    unsigned int imm12 = extract32(insn, 10, 12);
    bool is_vector = extract32(insn, 26, 1);
    int size = extract32(insn, 30, 2);
    int opc = extract32(insn, 22, 2);
    unsigned int offset;

    TCGv_i64 tcg_addr;

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
    tcg_addr = read_cpu_reg_sp(s, rn, 1);
    offset = imm12 << size;
    tcg_gen_addi_i64(tcg_addr, tcg_addr, offset);

    if (is_vector) {
        if (is_store) {
            do_fp_st(s, rt, tcg_addr, size);
        } else {
            do_fp_ld(s, rt, tcg_addr, size);
        }
    } else {
        TCGv_i64 tcg_rt = cpu_reg(s, rt);
        if (is_store) {
            do_gpr_st(s, tcg_rt, tcg_addr, size);
        } else {
            do_gpr_ld(s, tcg_rt, tcg_addr, size, is_signed, is_extended);
        }
    }
}

/* Load/store register (immediate forms) */
static void disas_ldst_reg_imm(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 10, 2)) {
    case 0: case 1: case 3:
        /* Load/store register (unscaled immediate) */
        /* Load/store immediate pre/post-indexed */
        disas_ldst_reg_imm9(s, insn);
        break;
    case 2:
        /* Load/store register unprivileged */
        unsupported_encoding(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* Load/store register (all forms) */
static void disas_ldst_reg(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 24, 2)) {
    case 0:
        if (extract32(insn, 21, 1) == 1 && extract32(insn, 10, 2) == 2) {
            disas_ldst_reg_roffset(s, insn);
        } else {
            disas_ldst_reg_imm(s, insn);
        }
        break;
    case 1:
        disas_ldst_reg_unsigned_imm(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* AdvSIMD load/store multiple structures */
static void disas_ldst_multiple_struct(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* AdvSIMD load/store single structure */
static void disas_ldst_single_struct(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.3 Loads and stores */
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
    default:
        unallocated_encoding(s);
        break;
    }
}

/* C3.4.6 PC-rel. addressing
 *   31  30   29 28       24 23                5 4    0
 * +----+-------+-----------+-------------------+------+
 * | op | immlo | 1 0 0 0 0 |       immhi       |  Rd  |
 * +----+-------+-----------+-------------------+------+
 */
static void disas_pc_rel_adr(DisasContext *s, uint32_t insn)
{
    unsigned int page, rd;
    uint64_t base;
    int64_t offset;

    page = extract32(insn, 31, 1);
    /* SignExtend(immhi:immlo) -> offset */
    offset = ((int64_t)sextract32(insn, 5, 19) << 2) | extract32(insn, 29, 2);
    rd = extract32(insn, 0, 5);
    base = s->pc - 4;

    if (page) {
        /* ADRP (page based) */
        base &= ~0xfff;
        offset <<= 12;
    }

    tcg_gen_movi_i64(cpu_reg(s, rd), base + offset);
}

/*
 * C3.4.1 Add/subtract (immediate)
 *
 *  31 30 29 28       24 23 22 21         10 9   5 4   0
 * +--+--+--+-----------+-----+-------------+-----+-----+
 * |sf|op| S| 1 0 0 0 1 |shift|    imm12    |  Rn | Rd  |
 * +--+--+--+-----------+-----+-------------+-----+-----+
 *
 *    sf: 0 -> 32bit, 1 -> 64bit
 *    op: 0 -> add  , 1 -> sub
 *     S: 1 -> set flags
 * shift: 00 -> LSL imm by 0, 01 -> LSL imm by 12
 */
static void disas_add_sub_imm(DisasContext *s, uint32_t insn)
{
    int rd = extract32(insn, 0, 5);
    int rn = extract32(insn, 5, 5);
    uint64_t imm = extract32(insn, 10, 12);
    int shift = extract32(insn, 22, 2);
    bool setflags = extract32(insn, 29, 1);
    bool sub_op = extract32(insn, 30, 1);
    bool is_64bit = extract32(insn, 31, 1);

    TCGv_i64 tcg_rn = cpu_reg_sp(s, rn);
    TCGv_i64 tcg_rd = setflags ? cpu_reg(s, rd) : cpu_reg_sp(s, rd);
    TCGv_i64 tcg_result;

    switch (shift) {
    case 0x0:
        break;
    case 0x1:
        imm <<= 12;
        break;
    default:
        unallocated_encoding(s);
        return;
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
static bool logic_imm_decode_wmask(uint64_t *result, unsigned int immn,
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
    mask = (mask >> r) | (mask << (e - r));
    /* ...then replicate the element over the whole 64 bit value */
    mask = bitfield_replicate(mask, e);
    *result = mask;
    return true;
}

/* C3.4.4 Logical (immediate)
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
 * C3.4.5 Move wide (immediate)
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

/* C3.4.2 Bitfield
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
    tcg_tmp = read_cpu_reg(s, rn, sf);

    /* OPTME: probably worth recognizing common cases of ext{8,16,32}{u,s} */

    if (opc != 1) { /* SBFM or UBFM */
        tcg_gen_movi_i64(tcg_rd, 0);
    }

    /* do the bit move operation */
    if (si >= ri) {
        /* Wd<s-r:0> = Wn<s:r> */
        tcg_gen_shri_i64(tcg_tmp, tcg_tmp, ri);
        pos = 0;
        len = (si - ri) + 1;
    } else {
        /* Wd<32+s-r,32-r> = Wn<s:0> */
        pos = bitsize - ri;
        len = si + 1;
    }

    tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_tmp, pos, len);

    if (opc == 0) { /* SBFM - sign extend the destination field */
        tcg_gen_shli_i64(tcg_rd, tcg_rd, 64 - (pos + len));
        tcg_gen_sari_i64(tcg_rd, tcg_rd, 64 - (pos + len));
    }

    if (!sf) { /* zero extend final result */
        tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
    }
}

/* C3.4.3 Extract
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

        if (imm) {
            /* OPTME: we can special case rm==rn as a rotate */
            tcg_rm = read_cpu_reg(s, rm, sf);
            tcg_rn = read_cpu_reg(s, rn, sf);
            tcg_gen_shri_i64(tcg_rm, tcg_rm, imm);
            tcg_gen_shli_i64(tcg_rn, tcg_rn, bitsize - imm);
            tcg_gen_or_i64(tcg_rd, tcg_rm, tcg_rn);
            if (!sf) {
                tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
            }
        } else {
            /* tcg shl_i32/shl_i64 is undefined for 32/64 bit shifts,
             * so an extract from bit 0 is a special case.
             */
            if (sf) {
                tcg_gen_mov_i64(tcg_rd, cpu_reg(s, rm));
            } else {
                tcg_gen_ext32u_i64(tcg_rd, cpu_reg(s, rm));
            }
        }

    }
}

/* C3.4 Data processing - immediate */
static void disas_data_proc_imm(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 23, 6)) {
    case 0x20: case 0x21: /* PC-rel. addressing */
        disas_pc_rel_adr(s, insn);
        break;
    case 0x22: case 0x23: /* Add/subtract (immediate) */
        disas_add_sub_imm(s, insn);
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
            tcg_gen_trunc_i64_i32(t0, src);
            tcg_gen_trunc_i64_i32(t1, shift_amount);
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

/* C3.5.10 Logical (shifted register)
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
 * C3.5.1 Add/subtract (extended register)
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
    bool setflags = extract32(insn, 29, 1);
    bool sub_op = extract32(insn, 30, 1);
    bool sf = extract32(insn, 31, 1);

    TCGv_i64 tcg_rm, tcg_rn; /* temps */
    TCGv_i64 tcg_rd;
    TCGv_i64 tcg_result;

    if (imm3 > 4) {
        unallocated_encoding(s);
        return;
    }

    /* non-flag setting ops may use SP */
    if (!setflags) {
        tcg_rn = read_cpu_reg_sp(s, rn, sf);
        tcg_rd = cpu_reg_sp(s, rd);
    } else {
        tcg_rn = read_cpu_reg(s, rn, sf);
        tcg_rd = cpu_reg(s, rd);
    }

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

    tcg_temp_free_i64(tcg_result);
}

/*
 * C3.5.2 Add/subtract (shifted register)
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

    tcg_temp_free_i64(tcg_result);
}

/* C3.5.9 Data-processing (3 source)

   31 30  29 28       24 23 21  20  16  15  14  10 9    5 4    0
  +--+------+-----------+------+------+----+------+------+------+
  |sf| op54 | 1 1 0 1 1 | op31 |  Rm  | o0 |  Ra  |  Rn  |  Rd  |
  +--+------+-----------+------+------+----+------+------+------+

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

/* C3.5.3 - Add/subtract (with carry)
 *  31 30 29 28 27 26 25 24 23 22 21  20  16  15   10  9    5 4   0
 * +--+--+--+------------------------+------+---------+------+-----+
 * |sf|op| S| 1  1  0  1  0  0  0  0 |  rm  | opcode2 |  Rn  |  Rd |
 * +--+--+--+------------------------+------+---------+------+-----+
 *                                            [000000]
 */

static void disas_adc_sbc(DisasContext *s, uint32_t insn)
{
    unsigned int sf, op, setflags, rm, rn, rd;
    TCGv_i64 tcg_y, tcg_rn, tcg_rd;

    if (extract32(insn, 10, 6) != 0) {
        unallocated_encoding(s);
        return;
    }

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

/* Conditional compare (immediate) */
static void disas_cc_imm(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* Conditional compare (register) */
static void disas_cc_reg(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.5.6 Conditional select
 *   31   30  29  28             21 20  16 15  12 11 10 9    5 4    0
 * +----+----+---+-----------------+------+------+-----+------+------+
 * | sf | op | S | 1 1 0 1 0 1 0 0 |  Rm  | cond | op2 |  Rn  |  Rd  |
 * +----+----+---+-----------------+------+------+-----+------+------+
 */
static void disas_cond_select(DisasContext *s, uint32_t insn)
{
    unsigned int sf, else_inv, rm, cond, else_inc, rn, rd;
    TCGv_i64 tcg_rd, tcg_src;

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

    if (rd == 31) {
        /* silly no-op write; until we use movcond we must special-case
         * this to avoid a dead temporary across basic blocks.
         */
        return;
    }

    tcg_rd = cpu_reg(s, rd);

    if (cond >= 0x0e) { /* condition "always" */
        tcg_src = read_cpu_reg(s, rn, sf);
        tcg_gen_mov_i64(tcg_rd, tcg_src);
    } else {
        /* OPTME: we could use movcond here, at the cost of duplicating
         * a lot of the arm_gen_test_cc() logic.
         */
        int label_match = gen_new_label();
        int label_continue = gen_new_label();

        arm_gen_test_cc(cond, label_match);
        /* nomatch: */
        tcg_src = cpu_reg(s, rm);

        if (else_inv && else_inc) {
            tcg_gen_neg_i64(tcg_rd, tcg_src);
        } else if (else_inv) {
            tcg_gen_not_i64(tcg_rd, tcg_src);
        } else if (else_inc) {
            tcg_gen_addi_i64(tcg_rd, tcg_src, 1);
        } else {
            tcg_gen_mov_i64(tcg_rd, tcg_src);
        }
        if (!sf) {
            tcg_gen_ext32u_i64(tcg_rd, tcg_rd);
        }
        tcg_gen_br(label_continue);
        /* match: */
        gen_set_label(label_match);
        tcg_src = read_cpu_reg(s, rn, sf);
        tcg_gen_mov_i64(tcg_rd, tcg_src);
        /* continue: */
        gen_set_label(label_continue);
    }
}

static void handle_clz(DisasContext *s, unsigned int sf,
                       unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd, tcg_rn;
    tcg_rd = cpu_reg(s, rd);
    tcg_rn = cpu_reg(s, rn);

    if (sf) {
        gen_helper_clz64(tcg_rd, tcg_rn);
    } else {
        TCGv_i32 tcg_tmp32 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tcg_tmp32, tcg_rn);
        gen_helper_clz(tcg_tmp32, tcg_tmp32);
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
        gen_helper_cls64(tcg_rd, tcg_rn);
    } else {
        TCGv_i32 tcg_tmp32 = tcg_temp_new_i32();
        tcg_gen_trunc_i64_i32(tcg_tmp32, tcg_rn);
        gen_helper_cls32(tcg_tmp32, tcg_tmp32);
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
        tcg_gen_trunc_i64_i32(tcg_tmp32, tcg_rn);
        gen_helper_rbit(tcg_tmp32, tcg_tmp32);
        tcg_gen_extu_i32_i64(tcg_rd, tcg_tmp32);
        tcg_temp_free_i32(tcg_tmp32);
    }
}

/* C5.6.149 REV with sf==1, opcode==3 ("REV64") */
static void handle_rev64(DisasContext *s, unsigned int sf,
                         unsigned int rn, unsigned int rd)
{
    if (!sf) {
        unallocated_encoding(s);
        return;
    }
    tcg_gen_bswap64_i64(cpu_reg(s, rd), cpu_reg(s, rn));
}

/* C5.6.149 REV with sf==0, opcode==2
 * C5.6.151 REV32 (sf==1, opcode==2)
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

/* C5.6.150 REV16 (opcode==1) */
static void handle_rev16(DisasContext *s, unsigned int sf,
                         unsigned int rn, unsigned int rd)
{
    TCGv_i64 tcg_rd = cpu_reg(s, rd);
    TCGv_i64 tcg_tmp = tcg_temp_new_i64();
    TCGv_i64 tcg_rn = read_cpu_reg(s, rn, sf);

    tcg_gen_andi_i64(tcg_tmp, tcg_rn, 0xffff);
    tcg_gen_bswap16_i64(tcg_rd, tcg_tmp);

    tcg_gen_shri_i64(tcg_tmp, tcg_rn, 16);
    tcg_gen_andi_i64(tcg_tmp, tcg_tmp, 0xffff);
    tcg_gen_bswap16_i64(tcg_tmp, tcg_tmp);
    tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_tmp, 16, 16);

    if (sf) {
        tcg_gen_shri_i64(tcg_tmp, tcg_rn, 32);
        tcg_gen_andi_i64(tcg_tmp, tcg_tmp, 0xffff);
        tcg_gen_bswap16_i64(tcg_tmp, tcg_tmp);
        tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_tmp, 32, 16);

        tcg_gen_shri_i64(tcg_tmp, tcg_rn, 48);
        tcg_gen_bswap16_i64(tcg_tmp, tcg_tmp);
        tcg_gen_deposit_i64(tcg_rd, tcg_rd, tcg_tmp, 48, 16);
    }

    tcg_temp_free_i64(tcg_tmp);
}

/* C3.5.7 Data-processing (1 source)
 *   31  30  29  28             21 20     16 15    10 9    5 4    0
 * +----+---+---+-----------------+---------+--------+------+------+
 * | sf | 1 | S | 1 1 0 1 0 1 1 0 | opcode2 | opcode |  Rn  |  Rd  |
 * +----+---+---+-----------------+---------+--------+------+------+
 */
static void disas_data_proc_1src(DisasContext *s, uint32_t insn)
{
    unsigned int sf, opcode, rn, rd;

    if (extract32(insn, 29, 1) || extract32(insn, 16, 5)) {
        unallocated_encoding(s);
        return;
    }

    sf = extract32(insn, 31, 1);
    opcode = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    switch (opcode) {
    case 0: /* RBIT */
        handle_rbit(s, sf, rn, rd);
        break;
    case 1: /* REV16 */
        handle_rev16(s, sf, rn, rd);
        break;
    case 2: /* REV32 */
        handle_rev32(s, sf, rn, rd);
        break;
    case 3: /* REV64 */
        handle_rev64(s, sf, rn, rd);
        break;
    case 4: /* CLZ */
        handle_clz(s, sf, rn, rd);
        break;
    case 5: /* CLS */
        handle_cls(s, sf, rn, rd);
        break;
    }
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

/* C5.6.115 LSLV, C5.6.118 LSRV, C5.6.17 ASRV, C5.6.154 RORV */
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

/* C3.5.8 Data-processing (2 source)
 *   31   30  29 28             21 20  16 15    10 9    5 4    0
 * +----+---+---+-----------------+------+--------+------+------+
 * | sf | 0 | S | 1 1 0 1 0 1 1 0 |  Rm  | opcode |  Rn  |  Rd  |
 * +----+---+---+-----------------+------+--------+------+------+
 */
static void disas_data_proc_2src(DisasContext *s, uint32_t insn)
{
    unsigned int sf, rm, opcode, rn, rd;
    sf = extract32(insn, 31, 1);
    rm = extract32(insn, 16, 5);
    opcode = extract32(insn, 10, 6);
    rn = extract32(insn, 5, 5);
    rd = extract32(insn, 0, 5);

    if (extract32(insn, 29, 1)) {
        unallocated_encoding(s);
        return;
    }

    switch (opcode) {
    case 2: /* UDIV */
        handle_div(s, false, sf, rm, rn, rd);
        break;
    case 3: /* SDIV */
        handle_div(s, true, sf, rm, rn, rd);
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
    case 16:
    case 17:
    case 18:
    case 19:
    case 20:
    case 21:
    case 22:
    case 23: /* CRC32 */
        unsupported_encoding(s, insn);
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* C3.5 Data processing - register */
static void disas_data_proc_reg(DisasContext *s, uint32_t insn)
{
    switch (extract32(insn, 24, 5)) {
    case 0x0a: /* Logical (shifted register) */
        disas_logic_reg(s, insn);
        break;
    case 0x0b: /* Add/subtract */
        if (insn & (1 << 21)) { /* (extended register) */
            disas_add_sub_ext_reg(s, insn);
        } else {
            disas_add_sub_reg(s, insn);
        }
        break;
    case 0x1b: /* Data-processing (3 source) */
        disas_data_proc_3src(s, insn);
        break;
    case 0x1a:
        switch (extract32(insn, 21, 3)) {
        case 0x0: /* Add/subtract (with carry) */
            disas_adc_sbc(s, insn);
            break;
        case 0x2: /* Conditional compare */
            if (insn & (1 << 11)) { /* (immediate) */
                disas_cc_imm(s, insn);
            } else {            /* (register) */
                disas_cc_reg(s, insn);
            }
            break;
        case 0x4: /* Conditional select */
            disas_cond_select(s, insn);
            break;
        case 0x6: /* Data-processing */
            if (insn & (1 << 30)) { /* (1 source) */
                disas_data_proc_1src(s, insn);
            } else {            /* (2 source) */
                disas_data_proc_2src(s, insn);
            }
            break;
        default:
            unallocated_encoding(s);
            break;
        }
        break;
    default:
        unallocated_encoding(s);
        break;
    }
}

/* C3.6.22 Floating point compare
 *   31  30  29 28       24 23  22  21 20  16 15 14 13  10    9    5 4     0
 * +---+---+---+-----------+------+---+------+-----+---------+------+-------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | op  | 1 0 0 0 |  Rn  |  op2  |
 * +---+---+---+-----------+------+---+------+-----+---------+------+-------+
 */
static void disas_fp_compare(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.23 Floating point conditional compare
 *   31  30  29 28       24 23  22  21 20  16 15  12 11 10 9    5  4   3    0
 * +---+---+---+-----------+------+---+------+------+-----+------+----+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | cond | 0 1 |  Rn  | op | nzcv |
 * +---+---+---+-----------+------+---+------+------+-----+------+----+------+
 */
static void disas_fp_ccomp(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.24 Floating point conditional select
 *   31  30  29 28       24 23  22  21 20  16 15  12 11 10 9    5 4    0
 * +---+---+---+-----------+------+---+------+------+-----+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | cond | 1 1 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+------+------+-----+------+------+
 */
static void disas_fp_csel(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.25 Floating point data-processing (1 source)
 *   31  30  29 28       24 23  22  21 20    15 14       10 9    5 4    0
 * +---+---+---+-----------+------+---+--------+-----------+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 | opcode | 1 0 0 0 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+--------+-----------+------+------+
 */
static void disas_fp_1src(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.26 Floating point data-processing (2 source)
 *   31  30  29 28       24 23  22  21 20  16 15    12 11 10 9    5 4    0
 * +---+---+---+-----------+------+---+------+--------+-----+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |  Rm  | opcode | 1 0 |  Rn  |  Rd  |
 * +---+---+---+-----------+------+---+------+--------+-----+------+------+
 */
static void disas_fp_2src(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.27 Floating point data-processing (3 source)
 *   31  30  29 28       24 23  22  21  20  16  15  14  10 9    5 4    0
 * +---+---+---+-----------+------+----+------+----+------+------+------+
 * | M | 0 | S | 1 1 1 1 1 | type | o1 |  Rm  | o0 |  Ra  |  Rn  |  Rd  |
 * +---+---+---+-----------+------+----+------+----+------+------+------+
 */
static void disas_fp_3src(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.28 Floating point immediate
 *   31  30  29 28       24 23  22  21 20        13 12   10 9    5 4    0
 * +---+---+---+-----------+------+---+------------+-------+------+------+
 * | M | 0 | S | 1 1 1 1 0 | type | 1 |    imm8    | 1 0 0 | imm5 |  Rd  |
 * +---+---+---+-----------+------+---+------------+-------+------+------+
 */
static void disas_fp_imm(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

/* C3.6.29 Floating point <-> fixed point conversions
 *   31   30  29 28       24 23  22  21 20   19 18    16 15   10 9    5 4    0
 * +----+---+---+-----------+------+---+-------+--------+-------+------+------+
 * | sf | 0 | S | 1 1 1 1 0 | type | 0 | rmode | opcode | scale |  Rn  |  Rd  |
 * +----+---+---+-----------+------+---+-------+--------+-------+------+------+
 */
static void disas_fp_fixed_conv(DisasContext *s, uint32_t insn)
{
    unsupported_encoding(s, insn);
}

static void handle_fmov(DisasContext *s, int rd, int rn, int type, bool itof)
{
    /* FMOV: gpr to or from float, double, or top half of quad fp reg,
     * without conversion.
     */

    if (itof) {
        int freg_offs = offsetof(CPUARMState, vfp.regs[rd * 2]);
        TCGv_i64 tcg_rn = cpu_reg(s, rn);

        switch (type) {
        case 0:
        {
            /* 32 bit */
            TCGv_i64 tmp = tcg_temp_new_i64();
            tcg_gen_ext32u_i64(tmp, tcg_rn);
            tcg_gen_st_i64(tmp, cpu_env, freg_offs);
            tcg_gen_movi_i64(tmp, 0);
            tcg_gen_st_i64(tmp, cpu_env, freg_offs + sizeof(float64));
            tcg_temp_free_i64(tmp);
            break;
        }
        case 1:
        {
            /* 64 bit */
            TCGv_i64 tmp = tcg_const_i64(0);
            tcg_gen_st_i64(tcg_rn, cpu_env, freg_offs);
            tcg_gen_st_i64(tmp, cpu_env, freg_offs + sizeof(float64));
            tcg_temp_free_i64(tmp);
            break;
        }
        case 2:
            /* 64 bit to top half. */
            tcg_gen_st_i64(tcg_rn, cpu_env, freg_offs + sizeof(float64));
            break;
        }
    } else {
        int freg_offs = offsetof(CPUARMState, vfp.regs[rn * 2]);
        TCGv_i64 tcg_rd = cpu_reg(s, rd);

        switch (type) {
        case 0:
            /* 32 bit */
            tcg_gen_ld32u_i64(tcg_rd, cpu_env, freg_offs);
            break;
        case 2:
            /* 64 bits from top half */
            freg_offs += sizeof(float64);
            /* fall through */
        case 1:
            /* 64 bit */
            tcg_gen_ld_i64(tcg_rd, cpu_env, freg_offs);
            break;
        }
    }
}

/* C3.6.30 Floating point <-> integer conversions
 *   31   30  29 28       24 23  22  21 20   19 18 16 15         10 9  5 4  0
 * +----+---+---+-----------+------+---+-------+-----+-------------+----+----+
 * | sf | 0 | S | 1 1 1 1 0 | type | 0 | rmode | opc | 0 0 0 0 0 0 | Rn | Rd |
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

    if (!sbit && (rmode < 2) && (opcode > 5)) {
        /* FMOV */
        bool itof = opcode & 1;

        switch (sf << 3 | type << 1 | rmode) {
        case 0x0: /* 32 bit */
        case 0xa: /* 64 bit */
        case 0xd: /* 64 bit to top half of quad */
            break;
        default:
            /* all other sf/type/rmode combinations are invalid */
            unallocated_encoding(s);
            break;
        }

        handle_fmov(s, rd, rn, type, itof);
    } else {
        /* actual FP conversions */
        unsupported_encoding(s, insn);
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

static void disas_data_proc_simd(DisasContext *s, uint32_t insn)
{
    /* Note that this is called with all non-FP cases from
     * table C3-6 so it must UNDEF for entries not specifically
     * allocated to instructions in that table.
     */
    unsupported_encoding(s, insn);
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

/* C3.1 A64 instruction index by encoding */
static void disas_a64_insn(CPUARMState *env, DisasContext *s)
{
    uint32_t insn;

    insn = arm_ldl_code(env, s->pc, s->bswap_code);
    s->insn = insn;
    s->pc += 4;

    switch (extract32(insn, 25, 4)) {
    case 0x0: case 0x1: case 0x2: case 0x3: /* UNALLOCATED */
        unallocated_encoding(s);
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
}

void gen_intermediate_code_internal_a64(ARMCPU *cpu,
                                        TranslationBlock *tb,
                                        bool search_pc)
{
    CPUState *cs = CPU(cpu);
    CPUARMState *env = &cpu->env;
    DisasContext dc1, *dc = &dc1;
    CPUBreakpoint *bp;
    uint16_t *gen_opc_end;
    int j, lj;
    target_ulong pc_start;
    target_ulong next_page_start;
    int num_insns;
    int max_insns;

    pc_start = tb->pc;

    dc->tb = tb;

    gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;

    dc->is_jmp = DISAS_NEXT;
    dc->pc = pc_start;
    dc->singlestep_enabled = cs->singlestep_enabled;
    dc->condjmp = 0;

    dc->aarch64 = 1;
    dc->thumb = 0;
    dc->bswap_code = 0;
    dc->condexec_mask = 0;
    dc->condexec_cond = 0;
#if !defined(CONFIG_USER_ONLY)
    dc->user = 0;
#endif
    dc->vfp_enabled = 0;
    dc->vec_len = 0;
    dc->vec_stride = 0;
    dc->cp_regs = cpu->cp_regs;
    dc->current_pl = arm_current_pl(env);

    init_tmp_a64_array(dc);

    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    lj = -1;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    gen_tb_start();

    tcg_clear_temp_count();

    do {
        if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
            QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
                if (bp->pc == dc->pc) {
                    gen_exception_insn(dc, 0, EXCP_DEBUG);
                    /* Advance PC so that clearing the breakpoint will
                       invalidate this TB.  */
                    dc->pc += 2;
                    goto done_generating;
                }
            }
        }

        if (search_pc) {
            j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    tcg_ctx.gen_opc_instr_start[lj++] = 0;
                }
            }
            tcg_ctx.gen_opc_pc[lj] = dc->pc;
            tcg_ctx.gen_opc_instr_start[lj] = 1;
            tcg_ctx.gen_opc_icount[lj] = num_insns;
        }

        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT))) {
            tcg_gen_debug_insn_start(dc->pc);
        }

        disas_a64_insn(env, dc);

        if (tcg_check_temp_count()) {
            fprintf(stderr, "TCG temporary leak before "TARGET_FMT_lx"\n",
                    dc->pc);
        }

        /* Translation stops when a conditional branch is encountered.
         * Otherwise the subsequent code could get translated several times.
         * Also stop translation when a page boundary is reached.  This
         * ensures prefetch aborts occur at the right place.
         */
        num_insns++;
    } while (!dc->is_jmp && tcg_ctx.gen_opc_ptr < gen_opc_end &&
             !cs->singlestep_enabled &&
             !singlestep &&
             dc->pc < next_page_start &&
             num_insns < max_insns);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (unlikely(cs->singlestep_enabled) && dc->is_jmp != DISAS_EXC) {
        /* Note that this means single stepping WFI doesn't halt the CPU.
         * For conditional branch insns this is harmless unreachable code as
         * gen_goto_tb() has already handled emitting the debug exception
         * (and thus a tb-jump is not possible when singlestepping).
         */
        assert(dc->is_jmp != DISAS_TB_JUMP);
        if (dc->is_jmp != DISAS_JUMP) {
            gen_a64_set_pc_im(dc->pc);
        }
        gen_exception(EXCP_DEBUG);
    } else {
        switch (dc->is_jmp) {
        case DISAS_NEXT:
            gen_goto_tb(dc, 1, dc->pc);
            break;
        default:
        case DISAS_UPDATE:
            gen_a64_set_pc_im(dc->pc);
            /* fall through */
        case DISAS_JUMP:
            /* indicate that the hash table must be used to find the next TB */
            tcg_gen_exit_tb(0);
            break;
        case DISAS_TB_JUMP:
        case DISAS_EXC:
        case DISAS_SWI:
            break;
        case DISAS_WFI:
            /* This is a special case because we don't want to just halt the CPU
             * if trying to debug across a WFI.
             */
            gen_helper_wfi(cpu_env);
            break;
        }
    }

done_generating:
    gen_tb_end(tb, num_insns);
    *tcg_ctx.gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(env, pc_start, dc->pc - pc_start,
                         dc->thumb | (dc->bswap_code << 1));
        qemu_log("\n");
    }
#endif
    if (search_pc) {
        j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
        lj++;
        while (lj <= j) {
            tcg_ctx.gen_opc_instr_start[lj++] = 0;
        }
    } else {
        tb->size = dc->pc - pc_start;
        tb->icount = num_insns;
    }
}
