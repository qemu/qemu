/*
 * Xtensa ISA:
 * http://www.tensilica.com/products/literature-docs/documentation/xtensa-isa-databook.htm
 *
 * Copyright (c) 2011, Max Filippov, Open Source and Linux Lab.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the Open Source and Linux Lab nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-log.h"
#include "sysemu.h"

#include "helpers.h"
#define GEN_HELPER 1
#include "helpers.h"

typedef struct DisasContext {
    const XtensaConfig *config;
    TranslationBlock *tb;
    uint32_t pc;
    uint32_t next_pc;
    int cring;
    int ring;
    uint32_t lbeg;
    uint32_t lend;
    TCGv_i32 litbase;
    int is_jmp;
    int singlestep_enabled;

    bool sar_5bit;
    bool sar_m32_5bit;
    bool sar_m32_allocated;
    TCGv_i32 sar_m32;

    uint32_t ccount_delta;
    unsigned used_window;
} DisasContext;

static TCGv_ptr cpu_env;
static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_R[16];
static TCGv_i32 cpu_SR[256];
static TCGv_i32 cpu_UR[256];

#include "gen-icount.h"

static const char * const sregnames[256] = {
    [LBEG] = "LBEG",
    [LEND] = "LEND",
    [LCOUNT] = "LCOUNT",
    [SAR] = "SAR",
    [LITBASE] = "LITBASE",
    [SCOMPARE1] = "SCOMPARE1",
    [WINDOW_BASE] = "WINDOW_BASE",
    [WINDOW_START] = "WINDOW_START",
    [EPC1] = "EPC1",
    [EPC1 + 1] = "EPC2",
    [EPC1 + 2] = "EPC3",
    [EPC1 + 3] = "EPC4",
    [EPC1 + 4] = "EPC5",
    [EPC1 + 5] = "EPC6",
    [EPC1 + 6] = "EPC7",
    [DEPC] = "DEPC",
    [EPS2] = "EPS2",
    [EPS2 + 1] = "EPS3",
    [EPS2 + 2] = "EPS4",
    [EPS2 + 3] = "EPS5",
    [EPS2 + 4] = "EPS6",
    [EPS2 + 5] = "EPS7",
    [EXCSAVE1] = "EXCSAVE1",
    [EXCSAVE1 + 1] = "EXCSAVE2",
    [EXCSAVE1 + 2] = "EXCSAVE3",
    [EXCSAVE1 + 3] = "EXCSAVE4",
    [EXCSAVE1 + 4] = "EXCSAVE5",
    [EXCSAVE1 + 5] = "EXCSAVE6",
    [EXCSAVE1 + 6] = "EXCSAVE7",
    [CPENABLE] = "CPENABLE",
    [INTSET] = "INTSET",
    [INTCLEAR] = "INTCLEAR",
    [INTENABLE] = "INTENABLE",
    [PS] = "PS",
    [VECBASE] = "VECBASE",
    [EXCCAUSE] = "EXCCAUSE",
    [CCOUNT] = "CCOUNT",
    [PRID] = "PRID",
    [EXCVADDR] = "EXCVADDR",
    [CCOMPARE] = "CCOMPARE0",
    [CCOMPARE + 1] = "CCOMPARE1",
    [CCOMPARE + 2] = "CCOMPARE2",
};

static const char * const uregnames[256] = {
    [THREADPTR] = "THREADPTR",
    [FCR] = "FCR",
    [FSR] = "FSR",
};

void xtensa_translate_init(void)
{
    static const char * const regnames[] = {
        "ar0", "ar1", "ar2", "ar3",
        "ar4", "ar5", "ar6", "ar7",
        "ar8", "ar9", "ar10", "ar11",
        "ar12", "ar13", "ar14", "ar15",
    };
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_pc = tcg_global_mem_new_i32(TCG_AREG0,
            offsetof(CPUState, pc), "pc");

    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(TCG_AREG0,
                offsetof(CPUState, regs[i]),
                regnames[i]);
    }

    for (i = 0; i < 256; ++i) {
        if (sregnames[i]) {
            cpu_SR[i] = tcg_global_mem_new_i32(TCG_AREG0,
                    offsetof(CPUState, sregs[i]),
                    sregnames[i]);
        }
    }

    for (i = 0; i < 256; ++i) {
        if (uregnames[i]) {
            cpu_UR[i] = tcg_global_mem_new_i32(TCG_AREG0,
                    offsetof(CPUState, uregs[i]),
                    uregnames[i]);
        }
    }
#define GEN_HELPER 2
#include "helpers.h"
}

static inline bool option_enabled(DisasContext *dc, int opt)
{
    return xtensa_option_enabled(dc->config, opt);
}

static void init_litbase(DisasContext *dc)
{
    if (dc->tb->flags & XTENSA_TBFLAG_LITBASE) {
        dc->litbase = tcg_temp_local_new_i32();
        tcg_gen_andi_i32(dc->litbase, cpu_SR[LITBASE], 0xfffff000);
    }
}

static void reset_litbase(DisasContext *dc)
{
    if (dc->tb->flags & XTENSA_TBFLAG_LITBASE) {
        tcg_temp_free(dc->litbase);
    }
}

static void init_sar_tracker(DisasContext *dc)
{
    dc->sar_5bit = false;
    dc->sar_m32_5bit = false;
    dc->sar_m32_allocated = false;
}

static void reset_sar_tracker(DisasContext *dc)
{
    if (dc->sar_m32_allocated) {
        tcg_temp_free(dc->sar_m32);
    }
}

static void gen_right_shift_sar(DisasContext *dc, TCGv_i32 sa)
{
    tcg_gen_andi_i32(cpu_SR[SAR], sa, 0x1f);
    if (dc->sar_m32_5bit) {
        tcg_gen_discard_i32(dc->sar_m32);
    }
    dc->sar_5bit = true;
    dc->sar_m32_5bit = false;
}

static void gen_left_shift_sar(DisasContext *dc, TCGv_i32 sa)
{
    TCGv_i32 tmp = tcg_const_i32(32);
    if (!dc->sar_m32_allocated) {
        dc->sar_m32 = tcg_temp_local_new_i32();
        dc->sar_m32_allocated = true;
    }
    tcg_gen_andi_i32(dc->sar_m32, sa, 0x1f);
    tcg_gen_sub_i32(cpu_SR[SAR], tmp, dc->sar_m32);
    dc->sar_5bit = false;
    dc->sar_m32_5bit = true;
    tcg_temp_free(tmp);
}

static void gen_advance_ccount(DisasContext *dc)
{
    if (dc->ccount_delta > 0) {
        TCGv_i32 tmp = tcg_const_i32(dc->ccount_delta);
        dc->ccount_delta = 0;
        gen_helper_advance_ccount(tmp);
        tcg_temp_free(tmp);
    }
}

static void reset_used_window(DisasContext *dc)
{
    dc->used_window = 0;
}

static void gen_exception(DisasContext *dc, int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_advance_ccount(dc);
    gen_helper_exception(tmp);
    tcg_temp_free(tmp);
}

static void gen_exception_cause(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_advance_ccount(dc);
    gen_helper_exception_cause(tpc, tcause);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
}

static void gen_exception_cause_vaddr(DisasContext *dc, uint32_t cause,
        TCGv_i32 vaddr)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_advance_ccount(dc);
    gen_helper_exception_cause_vaddr(tpc, tcause, vaddr);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
}

static void gen_check_privilege(DisasContext *dc)
{
    if (dc->cring) {
        gen_exception_cause(dc, PRIVILEGED_CAUSE);
    }
}

static void gen_jump_slot(DisasContext *dc, TCGv dest, int slot)
{
    tcg_gen_mov_i32(cpu_pc, dest);
    if (dc->singlestep_enabled) {
        gen_exception(dc, EXCP_DEBUG);
    } else {
        gen_advance_ccount(dc);
        if (slot >= 0) {
            tcg_gen_goto_tb(slot);
            tcg_gen_exit_tb((tcg_target_long)dc->tb + slot);
        } else {
            tcg_gen_exit_tb(0);
        }
    }
    dc->is_jmp = DISAS_UPDATE;
}

static void gen_jump(DisasContext *dc, TCGv dest)
{
    gen_jump_slot(dc, dest, -1);
}

static void gen_jumpi(DisasContext *dc, uint32_t dest, int slot)
{
    TCGv_i32 tmp = tcg_const_i32(dest);
    if (((dc->pc ^ dest) & TARGET_PAGE_MASK) != 0) {
        slot = -1;
    }
    gen_jump_slot(dc, tmp, slot);
    tcg_temp_free(tmp);
}

static void gen_callw_slot(DisasContext *dc, int callinc, TCGv_i32 dest,
        int slot)
{
    TCGv_i32 tcallinc = tcg_const_i32(callinc);

    tcg_gen_deposit_i32(cpu_SR[PS], cpu_SR[PS],
            tcallinc, PS_CALLINC_SHIFT, PS_CALLINC_LEN);
    tcg_temp_free(tcallinc);
    tcg_gen_movi_i32(cpu_R[callinc << 2],
            (callinc << 30) | (dc->next_pc & 0x3fffffff));
    gen_jump_slot(dc, dest, slot);
}

static void gen_callw(DisasContext *dc, int callinc, TCGv_i32 dest)
{
    gen_callw_slot(dc, callinc, dest, -1);
}

static void gen_callwi(DisasContext *dc, int callinc, uint32_t dest, int slot)
{
    TCGv_i32 tmp = tcg_const_i32(dest);
    if (((dc->pc ^ dest) & TARGET_PAGE_MASK) != 0) {
        slot = -1;
    }
    gen_callw_slot(dc, callinc, tmp, slot);
    tcg_temp_free(tmp);
}

static bool gen_check_loop_end(DisasContext *dc, int slot)
{
    if (option_enabled(dc, XTENSA_OPTION_LOOP) &&
            !(dc->tb->flags & XTENSA_TBFLAG_EXCM) &&
            dc->next_pc == dc->lend) {
        int label = gen_new_label();

        tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_SR[LCOUNT], 0, label);
        tcg_gen_subi_i32(cpu_SR[LCOUNT], cpu_SR[LCOUNT], 1);
        gen_jumpi(dc, dc->lbeg, slot);
        gen_set_label(label);
        gen_jumpi(dc, dc->next_pc, -1);
        return true;
    }
    return false;
}

static void gen_jumpi_check_loop_end(DisasContext *dc, int slot)
{
    if (!gen_check_loop_end(dc, slot)) {
        gen_jumpi(dc, dc->next_pc, slot);
    }
}

static void gen_brcond(DisasContext *dc, TCGCond cond,
        TCGv_i32 t0, TCGv_i32 t1, uint32_t offset)
{
    int label = gen_new_label();

    tcg_gen_brcond_i32(cond, t0, t1, label);
    gen_jumpi_check_loop_end(dc, 0);
    gen_set_label(label);
    gen_jumpi(dc, dc->pc + offset, 1);
}

static void gen_brcondi(DisasContext *dc, TCGCond cond,
        TCGv_i32 t0, uint32_t t1, uint32_t offset)
{
    TCGv_i32 tmp = tcg_const_i32(t1);
    gen_brcond(dc, cond, t0, tmp, offset);
    tcg_temp_free(tmp);
}

static void gen_rsr_ccount(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    gen_advance_ccount(dc);
    tcg_gen_mov_i32(d, cpu_SR[sr]);
}

static void gen_rsr(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    static void (* const rsr_handler[256])(DisasContext *dc,
            TCGv_i32 d, uint32_t sr) = {
        [CCOUNT] = gen_rsr_ccount,
    };

    if (sregnames[sr]) {
        if (rsr_handler[sr]) {
            rsr_handler[sr](dc, d, sr);
        } else {
            tcg_gen_mov_i32(d, cpu_SR[sr]);
        }
    } else {
        qemu_log("RSR %d not implemented, ", sr);
    }
}

static void gen_wsr_lbeg(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    gen_helper_wsr_lbeg(s);
}

static void gen_wsr_lend(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    gen_helper_wsr_lend(s);
}

static void gen_wsr_sar(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    tcg_gen_andi_i32(cpu_SR[sr], s, 0x3f);
    if (dc->sar_m32_5bit) {
        tcg_gen_discard_i32(dc->sar_m32);
    }
    dc->sar_5bit = false;
    dc->sar_m32_5bit = false;
}

static void gen_wsr_litbase(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    tcg_gen_andi_i32(cpu_SR[sr], s, 0xfffff001);
    /* This can change tb->flags, so exit tb */
    gen_jumpi_check_loop_end(dc, -1);
}

static void gen_wsr_windowbase(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_windowbase(v);
    reset_used_window(dc);
}

static void gen_wsr_windowstart(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_mov_i32(cpu_SR[sr], v);
    reset_used_window(dc);
}

static void gen_wsr_intset(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v,
            dc->config->inttype_mask[INTTYPE_SOFTWARE]);
    gen_helper_check_interrupts(cpu_env);
    gen_jumpi_check_loop_end(dc, 0);
}

static void gen_wsr_intclear(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, v,
            dc->config->inttype_mask[INTTYPE_EDGE] |
            dc->config->inttype_mask[INTTYPE_NMI] |
            dc->config->inttype_mask[INTTYPE_SOFTWARE]);
    tcg_gen_andc_i32(cpu_SR[INTSET], cpu_SR[INTSET], tmp);
    tcg_temp_free(tmp);
    gen_helper_check_interrupts(cpu_env);
}

static void gen_wsr_intenable(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_mov_i32(cpu_SR[sr], v);
    gen_helper_check_interrupts(cpu_env);
    gen_jumpi_check_loop_end(dc, 0);
}

static void gen_wsr_ps(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    uint32_t mask = PS_WOE | PS_CALLINC | PS_OWB |
        PS_UM | PS_EXCM | PS_INTLEVEL;

    if (option_enabled(dc, XTENSA_OPTION_MMU)) {
        mask |= PS_RING;
    }
    tcg_gen_andi_i32(cpu_SR[sr], v, mask);
    reset_used_window(dc);
    gen_helper_check_interrupts(cpu_env);
    /* This can change mmu index and tb->flags, so exit tb */
    gen_jumpi_check_loop_end(dc, -1);
}

static void gen_wsr_prid(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
}

static void gen_wsr_ccompare(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    uint32_t id = sr - CCOMPARE;
    if (id < dc->config->nccompare) {
        uint32_t int_bit = 1 << dc->config->timerint[id];
        gen_advance_ccount(dc);
        tcg_gen_mov_i32(cpu_SR[sr], v);
        tcg_gen_andi_i32(cpu_SR[INTSET], cpu_SR[INTSET], ~int_bit);
        gen_helper_check_interrupts(cpu_env);
    }
}

static void gen_wsr(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    static void (* const wsr_handler[256])(DisasContext *dc,
            uint32_t sr, TCGv_i32 v) = {
        [LBEG] = gen_wsr_lbeg,
        [LEND] = gen_wsr_lend,
        [SAR] = gen_wsr_sar,
        [LITBASE] = gen_wsr_litbase,
        [WINDOW_BASE] = gen_wsr_windowbase,
        [WINDOW_START] = gen_wsr_windowstart,
        [INTSET] = gen_wsr_intset,
        [INTCLEAR] = gen_wsr_intclear,
        [INTENABLE] = gen_wsr_intenable,
        [PS] = gen_wsr_ps,
        [PRID] = gen_wsr_prid,
        [CCOMPARE] = gen_wsr_ccompare,
        [CCOMPARE + 1] = gen_wsr_ccompare,
        [CCOMPARE + 2] = gen_wsr_ccompare,
    };

    if (sregnames[sr]) {
        if (wsr_handler[sr]) {
            wsr_handler[sr](dc, sr, s);
        } else {
            tcg_gen_mov_i32(cpu_SR[sr], s);
        }
    } else {
        qemu_log("WSR %d not implemented, ", sr);
    }
}

static void gen_load_store_alignment(DisasContext *dc, int shift,
        TCGv_i32 addr, bool no_hw_alignment)
{
    if (!option_enabled(dc, XTENSA_OPTION_UNALIGNED_EXCEPTION)) {
        tcg_gen_andi_i32(addr, addr, ~0 << shift);
    } else if (option_enabled(dc, XTENSA_OPTION_HW_ALIGNMENT) &&
            no_hw_alignment) {
        int label = gen_new_label();
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_andi_i32(tmp, addr, ~(~0 << shift));
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        gen_exception_cause_vaddr(dc, LOAD_STORE_ALIGNMENT_CAUSE, addr);
        gen_set_label(label);
        tcg_temp_free(tmp);
    }
}

static void gen_waiti(DisasContext *dc, uint32_t imm4)
{
    TCGv_i32 pc = tcg_const_i32(dc->next_pc);
    TCGv_i32 intlevel = tcg_const_i32(imm4);
    gen_advance_ccount(dc);
    gen_helper_waiti(pc, intlevel);
    tcg_temp_free(pc);
    tcg_temp_free(intlevel);
}

static void gen_window_check1(DisasContext *dc, unsigned r1)
{
    if (dc->tb->flags & XTENSA_TBFLAG_EXCM) {
        return;
    }
    if (option_enabled(dc, XTENSA_OPTION_WINDOWED_REGISTER) &&
            r1 / 4 > dc->used_window) {
        TCGv_i32 pc = tcg_const_i32(dc->pc);
        TCGv_i32 w = tcg_const_i32(r1 / 4);

        dc->used_window = r1 / 4;
        gen_advance_ccount(dc);
        gen_helper_window_check(pc, w);

        tcg_temp_free(w);
        tcg_temp_free(pc);
    }
}

static void gen_window_check2(DisasContext *dc, unsigned r1, unsigned r2)
{
    gen_window_check1(dc, r1 > r2 ? r1 : r2);
}

static void gen_window_check3(DisasContext *dc, unsigned r1, unsigned r2,
        unsigned r3)
{
    gen_window_check2(dc, r1, r2 > r3 ? r2 : r3);
}

static void disas_xtensa_insn(DisasContext *dc)
{
#define HAS_OPTION(opt) do { \
        if (!option_enabled(dc, opt)) { \
            qemu_log("Option %d is not enabled %s:%d\n", \
                    (opt), __FILE__, __LINE__); \
            goto invalid_opcode; \
        } \
    } while (0)

#define TBD() qemu_log("TBD(pc = %08x): %s:%d\n", dc->pc, __FILE__, __LINE__)
#define RESERVED() do { \
        qemu_log("RESERVED(pc = %08x, %02x%02x%02x): %s:%d\n", \
                dc->pc, b0, b1, b2, __FILE__, __LINE__); \
        goto invalid_opcode; \
    } while (0)


#ifdef TARGET_WORDS_BIGENDIAN
#define OP0 (((b0) & 0xf0) >> 4)
#define OP1 (((b2) & 0xf0) >> 4)
#define OP2 ((b2) & 0xf)
#define RRR_R ((b1) & 0xf)
#define RRR_S (((b1) & 0xf0) >> 4)
#define RRR_T ((b0) & 0xf)
#else
#define OP0 (((b0) & 0xf))
#define OP1 (((b2) & 0xf))
#define OP2 (((b2) & 0xf0) >> 4)
#define RRR_R (((b1) & 0xf0) >> 4)
#define RRR_S (((b1) & 0xf))
#define RRR_T (((b0) & 0xf0) >> 4)
#endif

#define RRRN_R RRR_R
#define RRRN_S RRR_S
#define RRRN_T RRR_T

#define RRI8_R RRR_R
#define RRI8_S RRR_S
#define RRI8_T RRR_T
#define RRI8_IMM8 (b2)
#define RRI8_IMM8_SE ((((b2) & 0x80) ? 0xffffff00 : 0) | RRI8_IMM8)

#ifdef TARGET_WORDS_BIGENDIAN
#define RI16_IMM16 (((b1) << 8) | (b2))
#else
#define RI16_IMM16 (((b2) << 8) | (b1))
#endif

#ifdef TARGET_WORDS_BIGENDIAN
#define CALL_N (((b0) & 0xc) >> 2)
#define CALL_OFFSET ((((b0) & 0x3) << 16) | ((b1) << 8) | (b2))
#else
#define CALL_N (((b0) & 0x30) >> 4)
#define CALL_OFFSET ((((b0) & 0xc0) >> 6) | ((b1) << 2) | ((b2) << 10))
#endif
#define CALL_OFFSET_SE \
    (((CALL_OFFSET & 0x20000) ? 0xfffc0000 : 0) | CALL_OFFSET)

#define CALLX_N CALL_N
#ifdef TARGET_WORDS_BIGENDIAN
#define CALLX_M ((b0) & 0x3)
#else
#define CALLX_M (((b0) & 0xc0) >> 6)
#endif
#define CALLX_S RRR_S

#define BRI12_M CALLX_M
#define BRI12_S RRR_S
#ifdef TARGET_WORDS_BIGENDIAN
#define BRI12_IMM12 ((((b1) & 0xf) << 8) | (b2))
#else
#define BRI12_IMM12 ((((b1) & 0xf0) >> 4) | ((b2) << 4))
#endif
#define BRI12_IMM12_SE (((BRI12_IMM12 & 0x800) ? 0xfffff000 : 0) | BRI12_IMM12)

#define BRI8_M BRI12_M
#define BRI8_R RRI8_R
#define BRI8_S RRI8_S
#define BRI8_IMM8 RRI8_IMM8
#define BRI8_IMM8_SE RRI8_IMM8_SE

#define RSR_SR (b1)

    uint8_t b0 = ldub_code(dc->pc);
    uint8_t b1 = ldub_code(dc->pc + 1);
    uint8_t b2 = ldub_code(dc->pc + 2);

    static const uint32_t B4CONST[] = {
        0xffffffff, 1, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256
    };

    static const uint32_t B4CONSTU[] = {
        32768, 65536, 2, 3, 4, 5, 6, 7, 8, 10, 12, 16, 32, 64, 128, 256
    };

    if (OP0 >= 8) {
        dc->next_pc = dc->pc + 2;
        HAS_OPTION(XTENSA_OPTION_CODE_DENSITY);
    } else {
        dc->next_pc = dc->pc + 3;
    }

    switch (OP0) {
    case 0: /*QRST*/
        switch (OP1) {
        case 0: /*RST0*/
            switch (OP2) {
            case 0: /*ST0*/
                if ((RRR_R & 0xc) == 0x8) {
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                }

                switch (RRR_R) {
                case 0: /*SNM0*/
                    switch (CALLX_M) {
                    case 0: /*ILL*/
                        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
                        break;

                    case 1: /*reserved*/
                        RESERVED();
                        break;

                    case 2: /*JR*/
                        switch (CALLX_N) {
                        case 0: /*RET*/
                        case 2: /*JX*/
                            gen_window_check1(dc, CALLX_S);
                            gen_jump(dc, cpu_R[CALLX_S]);
                            break;

                        case 1: /*RETWw*/
                            HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                            {
                                TCGv_i32 tmp = tcg_const_i32(dc->pc);
                                gen_advance_ccount(dc);
                                gen_helper_retw(tmp, tmp);
                                gen_jump(dc, tmp);
                                tcg_temp_free(tmp);
                            }
                            break;

                        case 3: /*reserved*/
                            RESERVED();
                            break;
                        }
                        break;

                    case 3: /*CALLX*/
                        gen_window_check2(dc, CALLX_S, CALLX_N << 2);
                        switch (CALLX_N) {
                        case 0: /*CALLX0*/
                            {
                                TCGv_i32 tmp = tcg_temp_new_i32();
                                tcg_gen_mov_i32(tmp, cpu_R[CALLX_S]);
                                tcg_gen_movi_i32(cpu_R[0], dc->next_pc);
                                gen_jump(dc, tmp);
                                tcg_temp_free(tmp);
                            }
                            break;

                        case 1: /*CALLX4w*/
                        case 2: /*CALLX8w*/
                        case 3: /*CALLX12w*/
                            HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                            {
                                TCGv_i32 tmp = tcg_temp_new_i32();

                                tcg_gen_mov_i32(tmp, cpu_R[CALLX_S]);
                                gen_callw(dc, CALLX_N, tmp);
                                tcg_temp_free(tmp);
                            }
                            break;
                        }
                        break;
                    }
                    break;

                case 1: /*MOVSPw*/
                    HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                    gen_window_check2(dc, RRR_T, RRR_S);
                    {
                        TCGv_i32 pc = tcg_const_i32(dc->pc);
                        gen_advance_ccount(dc);
                        gen_helper_movsp(pc);
                        tcg_gen_mov_i32(cpu_R[RRR_T], cpu_R[RRR_S]);
                        tcg_temp_free(pc);
                    }
                    break;

                case 2: /*SYNC*/
                    switch (RRR_T) {
                    case 0: /*ISYNC*/
                        break;

                    case 1: /*RSYNC*/
                        break;

                    case 2: /*ESYNC*/
                        break;

                    case 3: /*DSYNC*/
                        break;

                    case 8: /*EXCW*/
                        HAS_OPTION(XTENSA_OPTION_EXCEPTION);
                        break;

                    case 12: /*MEMW*/
                        break;

                    case 13: /*EXTW*/
                        break;

                    case 15: /*NOP*/
                        break;

                    default: /*reserved*/
                        RESERVED();
                        break;
                    }
                    break;

                case 3: /*RFEIx*/
                    switch (RRR_T) {
                    case 0: /*RFETx*/
                        HAS_OPTION(XTENSA_OPTION_EXCEPTION);
                        switch (RRR_S) {
                        case 0: /*RFEx*/
                            gen_check_privilege(dc);
                            tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_EXCM);
                            gen_helper_check_interrupts(cpu_env);
                            gen_jump(dc, cpu_SR[EPC1]);
                            break;

                        case 1: /*RFUEx*/
                            RESERVED();
                            break;

                        case 2: /*RFDEx*/
                            gen_check_privilege(dc);
                            gen_jump(dc, cpu_SR[
                                    dc->config->ndepc ? DEPC : EPC1]);
                            break;

                        case 4: /*RFWOw*/
                        case 5: /*RFWUw*/
                            HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                            gen_check_privilege(dc);
                            {
                                TCGv_i32 tmp = tcg_const_i32(1);

                                tcg_gen_andi_i32(
                                        cpu_SR[PS], cpu_SR[PS], ~PS_EXCM);
                                tcg_gen_shl_i32(tmp, tmp, cpu_SR[WINDOW_BASE]);

                                if (RRR_S == 4) {
                                    tcg_gen_andc_i32(cpu_SR[WINDOW_START],
                                            cpu_SR[WINDOW_START], tmp);
                                } else {
                                    tcg_gen_or_i32(cpu_SR[WINDOW_START],
                                            cpu_SR[WINDOW_START], tmp);
                                }

                                gen_helper_restore_owb();
                                gen_helper_check_interrupts(cpu_env);
                                gen_jump(dc, cpu_SR[EPC1]);

                                tcg_temp_free(tmp);
                            }
                            break;

                        default: /*reserved*/
                            RESERVED();
                            break;
                        }
                        break;

                    case 1: /*RFIx*/
                        HAS_OPTION(XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT);
                        if (RRR_S >= 2 && RRR_S <= dc->config->nlevel) {
                            gen_check_privilege(dc);
                            tcg_gen_mov_i32(cpu_SR[PS],
                                    cpu_SR[EPS2 + RRR_S - 2]);
                            gen_helper_check_interrupts(cpu_env);
                            gen_jump(dc, cpu_SR[EPC1 + RRR_S - 1]);
                        } else {
                            qemu_log("RFI %d is illegal\n", RRR_S);
                            gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
                        }
                        break;

                    case 2: /*RFME*/
                        TBD();
                        break;

                    default: /*reserved*/
                        RESERVED();
                        break;

                    }
                    break;

                case 4: /*BREAKx*/
                    HAS_OPTION(XTENSA_OPTION_EXCEPTION);
                    TBD();
                    break;

                case 5: /*SYSCALLx*/
                    HAS_OPTION(XTENSA_OPTION_EXCEPTION);
                    switch (RRR_S) {
                    case 0: /*SYSCALLx*/
                        gen_exception_cause(dc, SYSCALL_CAUSE);
                        break;

                    case 1: /*SIMCALL*/
                        if (semihosting_enabled) {
                            gen_check_privilege(dc);
                            gen_helper_simcall(cpu_env);
                        } else {
                            qemu_log("SIMCALL but semihosting is disabled\n");
                            gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
                        }
                        break;

                    default:
                        RESERVED();
                        break;
                    }
                    break;

                case 6: /*RSILx*/
                    HAS_OPTION(XTENSA_OPTION_INTERRUPT);
                    gen_check_privilege(dc);
                    gen_window_check1(dc, RRR_T);
                    tcg_gen_mov_i32(cpu_R[RRR_T], cpu_SR[PS]);
                    tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_INTLEVEL);
                    tcg_gen_ori_i32(cpu_SR[PS], cpu_SR[PS], RRR_S);
                    gen_helper_check_interrupts(cpu_env);
                    gen_jumpi_check_loop_end(dc, 0);
                    break;

                case 7: /*WAITIx*/
                    HAS_OPTION(XTENSA_OPTION_INTERRUPT);
                    gen_check_privilege(dc);
                    gen_waiti(dc, RRR_S);
                    break;

                case 8: /*ANY4p*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    TBD();
                    break;

                case 9: /*ALL4p*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    TBD();
                    break;

                case 10: /*ANY8p*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    TBD();
                    break;

                case 11: /*ALL8p*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    TBD();
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;

                }
                break;

            case 1: /*AND*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                tcg_gen_and_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 2: /*OR*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                tcg_gen_or_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 3: /*XOR*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                tcg_gen_xor_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 4: /*ST1*/
                switch (RRR_R) {
                case 0: /*SSR*/
                    gen_window_check1(dc, RRR_S);
                    gen_right_shift_sar(dc, cpu_R[RRR_S]);
                    break;

                case 1: /*SSL*/
                    gen_window_check1(dc, RRR_S);
                    gen_left_shift_sar(dc, cpu_R[RRR_S]);
                    break;

                case 2: /*SSA8L*/
                    gen_window_check1(dc, RRR_S);
                    {
                        TCGv_i32 tmp = tcg_temp_new_i32();
                        tcg_gen_shli_i32(tmp, cpu_R[RRR_S], 3);
                        gen_right_shift_sar(dc, tmp);
                        tcg_temp_free(tmp);
                    }
                    break;

                case 3: /*SSA8B*/
                    gen_window_check1(dc, RRR_S);
                    {
                        TCGv_i32 tmp = tcg_temp_new_i32();
                        tcg_gen_shli_i32(tmp, cpu_R[RRR_S], 3);
                        gen_left_shift_sar(dc, tmp);
                        tcg_temp_free(tmp);
                    }
                    break;

                case 4: /*SSAI*/
                    {
                        TCGv_i32 tmp = tcg_const_i32(
                                RRR_S | ((RRR_T & 1) << 4));
                        gen_right_shift_sar(dc, tmp);
                        tcg_temp_free(tmp);
                    }
                    break;

                case 6: /*RER*/
                    TBD();
                    break;

                case 7: /*WER*/
                    TBD();
                    break;

                case 8: /*ROTWw*/
                    HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                    gen_check_privilege(dc);
                    {
                        TCGv_i32 tmp = tcg_const_i32(
                                RRR_T | ((RRR_T & 8) ? 0xfffffff0 : 0));
                        gen_helper_rotw(tmp);
                        tcg_temp_free(tmp);
                        reset_used_window(dc);
                    }
                    break;

                case 14: /*NSAu*/
                    HAS_OPTION(XTENSA_OPTION_MISC_OP);
                    gen_window_check2(dc, RRR_S, RRR_T);
                    gen_helper_nsa(cpu_R[RRR_T], cpu_R[RRR_S]);
                    break;

                case 15: /*NSAUu*/
                    HAS_OPTION(XTENSA_OPTION_MISC_OP);
                    gen_window_check2(dc, RRR_S, RRR_T);
                    gen_helper_nsau(cpu_R[RRR_T], cpu_R[RRR_S]);
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;
                }
                break;

            case 5: /*TLB*/
                TBD();
                break;

            case 6: /*RT0*/
                gen_window_check2(dc, RRR_R, RRR_T);
                switch (RRR_S) {
                case 0: /*NEG*/
                    tcg_gen_neg_i32(cpu_R[RRR_R], cpu_R[RRR_T]);
                    break;

                case 1: /*ABS*/
                    {
                        int label = gen_new_label();
                        tcg_gen_mov_i32(cpu_R[RRR_R], cpu_R[RRR_T]);
                        tcg_gen_brcondi_i32(
                                TCG_COND_GE, cpu_R[RRR_R], 0, label);
                        tcg_gen_neg_i32(cpu_R[RRR_R], cpu_R[RRR_T]);
                        gen_set_label(label);
                    }
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;
                }
                break;

            case 7: /*reserved*/
                RESERVED();
                break;

            case 8: /*ADD*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                tcg_gen_add_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 9: /*ADD**/
            case 10:
            case 11:
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_shli_i32(tmp, cpu_R[RRR_S], OP2 - 8);
                    tcg_gen_add_i32(cpu_R[RRR_R], tmp, cpu_R[RRR_T]);
                    tcg_temp_free(tmp);
                }
                break;

            case 12: /*SUB*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                tcg_gen_sub_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 13: /*SUB**/
            case 14:
            case 15:
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_shli_i32(tmp, cpu_R[RRR_S], OP2 - 12);
                    tcg_gen_sub_i32(cpu_R[RRR_R], tmp, cpu_R[RRR_T]);
                    tcg_temp_free(tmp);
                }
                break;
            }
            break;

        case 1: /*RST1*/
            switch (OP2) {
            case 0: /*SLLI*/
            case 1:
                gen_window_check2(dc, RRR_R, RRR_S);
                tcg_gen_shli_i32(cpu_R[RRR_R], cpu_R[RRR_S],
                        32 - (RRR_T | ((OP2 & 1) << 4)));
                break;

            case 2: /*SRAI*/
            case 3:
                gen_window_check2(dc, RRR_R, RRR_T);
                tcg_gen_sari_i32(cpu_R[RRR_R], cpu_R[RRR_T],
                        RRR_S | ((OP2 & 1) << 4));
                break;

            case 4: /*SRLI*/
                gen_window_check2(dc, RRR_R, RRR_T);
                tcg_gen_shri_i32(cpu_R[RRR_R], cpu_R[RRR_T], RRR_S);
                break;

            case 6: /*XSR*/
                {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    if (RSR_SR >= 64) {
                        gen_check_privilege(dc);
                    }
                    gen_window_check1(dc, RRR_T);
                    tcg_gen_mov_i32(tmp, cpu_R[RRR_T]);
                    gen_rsr(dc, cpu_R[RRR_T], RSR_SR);
                    gen_wsr(dc, RSR_SR, tmp);
                    tcg_temp_free(tmp);
                    if (!sregnames[RSR_SR]) {
                        TBD();
                    }
                }
                break;

                /*
                 * Note: 64 bit ops are used here solely because SAR values
                 * have range 0..63
                 */
#define gen_shift_reg(cmd, reg) do { \
                    TCGv_i64 tmp = tcg_temp_new_i64(); \
                    tcg_gen_extu_i32_i64(tmp, reg); \
                    tcg_gen_##cmd##_i64(v, v, tmp); \
                    tcg_gen_trunc_i64_i32(cpu_R[RRR_R], v); \
                    tcg_temp_free_i64(v); \
                    tcg_temp_free_i64(tmp); \
                } while (0)

#define gen_shift(cmd) gen_shift_reg(cmd, cpu_SR[SAR])

            case 8: /*SRC*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    TCGv_i64 v = tcg_temp_new_i64();
                    tcg_gen_concat_i32_i64(v, cpu_R[RRR_T], cpu_R[RRR_S]);
                    gen_shift(shr);
                }
                break;

            case 9: /*SRL*/
                gen_window_check2(dc, RRR_R, RRR_T);
                if (dc->sar_5bit) {
                    tcg_gen_shr_i32(cpu_R[RRR_R], cpu_R[RRR_T], cpu_SR[SAR]);
                } else {
                    TCGv_i64 v = tcg_temp_new_i64();
                    tcg_gen_extu_i32_i64(v, cpu_R[RRR_T]);
                    gen_shift(shr);
                }
                break;

            case 10: /*SLL*/
                gen_window_check2(dc, RRR_R, RRR_S);
                if (dc->sar_m32_5bit) {
                    tcg_gen_shl_i32(cpu_R[RRR_R], cpu_R[RRR_S], dc->sar_m32);
                } else {
                    TCGv_i64 v = tcg_temp_new_i64();
                    TCGv_i32 s = tcg_const_i32(32);
                    tcg_gen_sub_i32(s, s, cpu_SR[SAR]);
                    tcg_gen_andi_i32(s, s, 0x3f);
                    tcg_gen_extu_i32_i64(v, cpu_R[RRR_S]);
                    gen_shift_reg(shl, s);
                    tcg_temp_free(s);
                }
                break;

            case 11: /*SRA*/
                gen_window_check2(dc, RRR_R, RRR_T);
                if (dc->sar_5bit) {
                    tcg_gen_sar_i32(cpu_R[RRR_R], cpu_R[RRR_T], cpu_SR[SAR]);
                } else {
                    TCGv_i64 v = tcg_temp_new_i64();
                    tcg_gen_ext_i32_i64(v, cpu_R[RRR_T]);
                    gen_shift(sar);
                }
                break;
#undef gen_shift
#undef gen_shift_reg

            case 12: /*MUL16U*/
                HAS_OPTION(XTENSA_OPTION_16_BIT_IMUL);
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    TCGv_i32 v1 = tcg_temp_new_i32();
                    TCGv_i32 v2 = tcg_temp_new_i32();
                    tcg_gen_ext16u_i32(v1, cpu_R[RRR_S]);
                    tcg_gen_ext16u_i32(v2, cpu_R[RRR_T]);
                    tcg_gen_mul_i32(cpu_R[RRR_R], v1, v2);
                    tcg_temp_free(v2);
                    tcg_temp_free(v1);
                }
                break;

            case 13: /*MUL16S*/
                HAS_OPTION(XTENSA_OPTION_16_BIT_IMUL);
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    TCGv_i32 v1 = tcg_temp_new_i32();
                    TCGv_i32 v2 = tcg_temp_new_i32();
                    tcg_gen_ext16s_i32(v1, cpu_R[RRR_S]);
                    tcg_gen_ext16s_i32(v2, cpu_R[RRR_T]);
                    tcg_gen_mul_i32(cpu_R[RRR_R], v1, v2);
                    tcg_temp_free(v2);
                    tcg_temp_free(v1);
                }
                break;

            default: /*reserved*/
                RESERVED();
                break;
            }
            break;

        case 2: /*RST2*/
            gen_window_check3(dc, RRR_R, RRR_S, RRR_T);

            if (OP2 >= 12) {
                HAS_OPTION(XTENSA_OPTION_32_BIT_IDIV);
                int label = gen_new_label();
                tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[RRR_T], 0, label);
                gen_exception_cause(dc, INTEGER_DIVIDE_BY_ZERO_CAUSE);
                gen_set_label(label);
            }

            switch (OP2) {
            case 8: /*MULLi*/
                HAS_OPTION(XTENSA_OPTION_32_BIT_IMUL);
                tcg_gen_mul_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 10: /*MULUHi*/
            case 11: /*MULSHi*/
                HAS_OPTION(XTENSA_OPTION_32_BIT_IMUL);
                {
                    TCGv_i64 r = tcg_temp_new_i64();
                    TCGv_i64 s = tcg_temp_new_i64();
                    TCGv_i64 t = tcg_temp_new_i64();

                    if (OP2 == 10) {
                        tcg_gen_extu_i32_i64(s, cpu_R[RRR_S]);
                        tcg_gen_extu_i32_i64(t, cpu_R[RRR_T]);
                    } else {
                        tcg_gen_ext_i32_i64(s, cpu_R[RRR_S]);
                        tcg_gen_ext_i32_i64(t, cpu_R[RRR_T]);
                    }
                    tcg_gen_mul_i64(r, s, t);
                    tcg_gen_shri_i64(r, r, 32);
                    tcg_gen_trunc_i64_i32(cpu_R[RRR_R], r);

                    tcg_temp_free_i64(r);
                    tcg_temp_free_i64(s);
                    tcg_temp_free_i64(t);
                }
                break;

            case 12: /*QUOUi*/
                tcg_gen_divu_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 13: /*QUOSi*/
            case 15: /*REMSi*/
                {
                    int label1 = gen_new_label();
                    int label2 = gen_new_label();

                    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[RRR_S], 0x80000000,
                            label1);
                    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[RRR_T], 0xffffffff,
                            label1);
                    tcg_gen_movi_i32(cpu_R[RRR_R],
                            OP2 == 13 ? 0x80000000 : 0);
                    tcg_gen_br(label2);
                    gen_set_label(label1);
                    if (OP2 == 13) {
                        tcg_gen_div_i32(cpu_R[RRR_R],
                                cpu_R[RRR_S], cpu_R[RRR_T]);
                    } else {
                        tcg_gen_rem_i32(cpu_R[RRR_R],
                                cpu_R[RRR_S], cpu_R[RRR_T]);
                    }
                    gen_set_label(label2);
                }
                break;

            case 14: /*REMUi*/
                tcg_gen_remu_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            default: /*reserved*/
                RESERVED();
                break;
            }
            break;

        case 3: /*RST3*/
            switch (OP2) {
            case 0: /*RSR*/
                if (RSR_SR >= 64) {
                    gen_check_privilege(dc);
                }
                gen_window_check1(dc, RRR_T);
                gen_rsr(dc, cpu_R[RRR_T], RSR_SR);
                if (!sregnames[RSR_SR]) {
                    TBD();
                }
                break;

            case 1: /*WSR*/
                if (RSR_SR >= 64) {
                    gen_check_privilege(dc);
                }
                gen_window_check1(dc, RRR_T);
                gen_wsr(dc, RSR_SR, cpu_R[RRR_T]);
                if (!sregnames[RSR_SR]) {
                    TBD();
                }
                break;

            case 2: /*SEXTu*/
                HAS_OPTION(XTENSA_OPTION_MISC_OP);
                gen_window_check2(dc, RRR_R, RRR_S);
                {
                    int shift = 24 - RRR_T;

                    if (shift == 24) {
                        tcg_gen_ext8s_i32(cpu_R[RRR_R], cpu_R[RRR_S]);
                    } else if (shift == 16) {
                        tcg_gen_ext16s_i32(cpu_R[RRR_R], cpu_R[RRR_S]);
                    } else {
                        TCGv_i32 tmp = tcg_temp_new_i32();
                        tcg_gen_shli_i32(tmp, cpu_R[RRR_S], shift);
                        tcg_gen_sari_i32(cpu_R[RRR_R], tmp, shift);
                        tcg_temp_free(tmp);
                    }
                }
                break;

            case 3: /*CLAMPSu*/
                HAS_OPTION(XTENSA_OPTION_MISC_OP);
                gen_window_check2(dc, RRR_R, RRR_S);
                {
                    TCGv_i32 tmp1 = tcg_temp_new_i32();
                    TCGv_i32 tmp2 = tcg_temp_new_i32();
                    int label = gen_new_label();

                    tcg_gen_sari_i32(tmp1, cpu_R[RRR_S], 24 - RRR_T);
                    tcg_gen_xor_i32(tmp2, tmp1, cpu_R[RRR_S]);
                    tcg_gen_andi_i32(tmp2, tmp2, 0xffffffff << (RRR_T + 7));
                    tcg_gen_mov_i32(cpu_R[RRR_R], cpu_R[RRR_S]);
                    tcg_gen_brcondi_i32(TCG_COND_EQ, tmp2, 0, label);

                    tcg_gen_sari_i32(tmp1, cpu_R[RRR_S], 31);
                    tcg_gen_xori_i32(cpu_R[RRR_R], tmp1,
                            0xffffffff >> (25 - RRR_T));

                    gen_set_label(label);

                    tcg_temp_free(tmp1);
                    tcg_temp_free(tmp2);
                }
                break;

            case 4: /*MINu*/
            case 5: /*MAXu*/
            case 6: /*MINUu*/
            case 7: /*MAXUu*/
                HAS_OPTION(XTENSA_OPTION_MISC_OP);
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    static const TCGCond cond[] = {
                        TCG_COND_LE,
                        TCG_COND_GE,
                        TCG_COND_LEU,
                        TCG_COND_GEU
                    };
                    int label = gen_new_label();

                    if (RRR_R != RRR_T) {
                        tcg_gen_mov_i32(cpu_R[RRR_R], cpu_R[RRR_S]);
                        tcg_gen_brcond_i32(cond[OP2 - 4],
                                cpu_R[RRR_S], cpu_R[RRR_T], label);
                        tcg_gen_mov_i32(cpu_R[RRR_R], cpu_R[RRR_T]);
                    } else {
                        tcg_gen_brcond_i32(cond[OP2 - 4],
                                cpu_R[RRR_T], cpu_R[RRR_S], label);
                        tcg_gen_mov_i32(cpu_R[RRR_R], cpu_R[RRR_S]);
                    }
                    gen_set_label(label);
                }
                break;

            case 8: /*MOVEQZ*/
            case 9: /*MOVNEZ*/
            case 10: /*MOVLTZ*/
            case 11: /*MOVGEZ*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    static const TCGCond cond[] = {
                        TCG_COND_NE,
                        TCG_COND_EQ,
                        TCG_COND_GE,
                        TCG_COND_LT
                    };
                    int label = gen_new_label();
                    tcg_gen_brcondi_i32(cond[OP2 - 8], cpu_R[RRR_T], 0, label);
                    tcg_gen_mov_i32(cpu_R[RRR_R], cpu_R[RRR_S]);
                    gen_set_label(label);
                }
                break;

            case 12: /*MOVFp*/
                HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                TBD();
                break;

            case 13: /*MOVTp*/
                HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                TBD();
                break;

            case 14: /*RUR*/
                gen_window_check1(dc, RRR_R);
                {
                    int st = (RRR_S << 4) + RRR_T;
                    if (uregnames[st]) {
                        tcg_gen_mov_i32(cpu_R[RRR_R], cpu_UR[st]);
                    } else {
                        qemu_log("RUR %d not implemented, ", st);
                        TBD();
                    }
                }
                break;

            case 15: /*WUR*/
                gen_window_check1(dc, RRR_T);
                {
                    if (uregnames[RSR_SR]) {
                        tcg_gen_mov_i32(cpu_UR[RSR_SR], cpu_R[RRR_T]);
                    } else {
                        qemu_log("WUR %d not implemented, ", RSR_SR);
                        TBD();
                    }
                }
                break;

            }
            break;

        case 4: /*EXTUI*/
        case 5:
            gen_window_check2(dc, RRR_R, RRR_T);
            {
                int shiftimm = RRR_S | (OP1 << 4);
                int maskimm = (1 << (OP2 + 1)) - 1;

                TCGv_i32 tmp = tcg_temp_new_i32();
                tcg_gen_shri_i32(tmp, cpu_R[RRR_T], shiftimm);
                tcg_gen_andi_i32(cpu_R[RRR_R], tmp, maskimm);
                tcg_temp_free(tmp);
            }
            break;

        case 6: /*CUST0*/
            RESERVED();
            break;

        case 7: /*CUST1*/
            RESERVED();
            break;

        case 8: /*LSCXp*/
            HAS_OPTION(XTENSA_OPTION_COPROCESSOR);
            TBD();
            break;

        case 9: /*LSC4*/
            gen_window_check2(dc, RRR_S, RRR_T);
            switch (OP2) {
            case 0: /*L32E*/
                HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                gen_check_privilege(dc);
                {
                    TCGv_i32 addr = tcg_temp_new_i32();
                    tcg_gen_addi_i32(addr, cpu_R[RRR_S],
                            (0xffffffc0 | (RRR_R << 2)));
                    tcg_gen_qemu_ld32u(cpu_R[RRR_T], addr, dc->ring);
                    tcg_temp_free(addr);
                }
                break;

            case 4: /*S32E*/
                HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                gen_check_privilege(dc);
                {
                    TCGv_i32 addr = tcg_temp_new_i32();
                    tcg_gen_addi_i32(addr, cpu_R[RRR_S],
                            (0xffffffc0 | (RRR_R << 2)));
                    tcg_gen_qemu_st32(cpu_R[RRR_T], addr, dc->ring);
                    tcg_temp_free(addr);
                }
                break;

            default:
                RESERVED();
                break;
            }
            break;

        case 10: /*FP0*/
            HAS_OPTION(XTENSA_OPTION_FP_COPROCESSOR);
            TBD();
            break;

        case 11: /*FP1*/
            HAS_OPTION(XTENSA_OPTION_FP_COPROCESSOR);
            TBD();
            break;

        default: /*reserved*/
            RESERVED();
            break;
        }
        break;

    case 1: /*L32R*/
        gen_window_check1(dc, RRR_T);
        {
            TCGv_i32 tmp = tcg_const_i32(
                    ((dc->tb->flags & XTENSA_TBFLAG_LITBASE) ?
                     0 : ((dc->pc + 3) & ~3)) +
                    (0xfffc0000 | (RI16_IMM16 << 2)));

            if (dc->tb->flags & XTENSA_TBFLAG_LITBASE) {
                tcg_gen_add_i32(tmp, tmp, dc->litbase);
            }
            tcg_gen_qemu_ld32u(cpu_R[RRR_T], tmp, dc->cring);
            tcg_temp_free(tmp);
        }
        break;

    case 2: /*LSAI*/
#define gen_load_store(type, shift) do { \
            TCGv_i32 addr = tcg_temp_new_i32(); \
            gen_window_check2(dc, RRI8_S, RRI8_T); \
            tcg_gen_addi_i32(addr, cpu_R[RRI8_S], RRI8_IMM8 << shift); \
            if (shift) { \
                gen_load_store_alignment(dc, shift, addr, false); \
            } \
            tcg_gen_qemu_##type(cpu_R[RRI8_T], addr, dc->cring); \
            tcg_temp_free(addr); \
        } while (0)

        switch (RRI8_R) {
        case 0: /*L8UI*/
            gen_load_store(ld8u, 0);
            break;

        case 1: /*L16UI*/
            gen_load_store(ld16u, 1);
            break;

        case 2: /*L32I*/
            gen_load_store(ld32u, 2);
            break;

        case 4: /*S8I*/
            gen_load_store(st8, 0);
            break;

        case 5: /*S16I*/
            gen_load_store(st16, 1);
            break;

        case 6: /*S32I*/
            gen_load_store(st32, 2);
            break;

        case 7: /*CACHEc*/
            if (RRI8_T < 8) {
                HAS_OPTION(XTENSA_OPTION_DCACHE);
            }

            switch (RRI8_T) {
            case 0: /*DPFRc*/
                break;

            case 1: /*DPFWc*/
                break;

            case 2: /*DPFROc*/
                break;

            case 3: /*DPFWOc*/
                break;

            case 4: /*DHWBc*/
                break;

            case 5: /*DHWBIc*/
                break;

            case 6: /*DHIc*/
                break;

            case 7: /*DIIc*/
                break;

            case 8: /*DCEc*/
                switch (OP1) {
                case 0: /*DPFLl*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE_INDEX_LOCK);
                    break;

                case 2: /*DHUl*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE_INDEX_LOCK);
                    break;

                case 3: /*DIUl*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE_INDEX_LOCK);
                    break;

                case 4: /*DIWBc*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE);
                    break;

                case 5: /*DIWBIc*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE);
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;

                }
                break;

            case 12: /*IPFc*/
                HAS_OPTION(XTENSA_OPTION_ICACHE);
                break;

            case 13: /*ICEc*/
                switch (OP1) {
                case 0: /*IPFLl*/
                    HAS_OPTION(XTENSA_OPTION_ICACHE_INDEX_LOCK);
                    break;

                case 2: /*IHUl*/
                    HAS_OPTION(XTENSA_OPTION_ICACHE_INDEX_LOCK);
                    break;

                case 3: /*IIUl*/
                    HAS_OPTION(XTENSA_OPTION_ICACHE_INDEX_LOCK);
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;
                }
                break;

            case 14: /*IHIc*/
                HAS_OPTION(XTENSA_OPTION_ICACHE);
                break;

            case 15: /*IIIc*/
                HAS_OPTION(XTENSA_OPTION_ICACHE);
                break;

            default: /*reserved*/
                RESERVED();
                break;
            }
            break;

        case 9: /*L16SI*/
            gen_load_store(ld16s, 1);
            break;
#undef gen_load_store

        case 10: /*MOVI*/
            gen_window_check1(dc, RRI8_T);
            tcg_gen_movi_i32(cpu_R[RRI8_T],
                    RRI8_IMM8 | (RRI8_S << 8) |
                    ((RRI8_S & 0x8) ? 0xfffff000 : 0));
            break;

#define gen_load_store_no_hw_align(type) do { \
            TCGv_i32 addr = tcg_temp_local_new_i32(); \
            gen_window_check2(dc, RRI8_S, RRI8_T); \
            tcg_gen_addi_i32(addr, cpu_R[RRI8_S], RRI8_IMM8 << 2); \
            gen_load_store_alignment(dc, 2, addr, true); \
            tcg_gen_qemu_##type(cpu_R[RRI8_T], addr, dc->cring); \
            tcg_temp_free(addr); \
        } while (0)

        case 11: /*L32AIy*/
            HAS_OPTION(XTENSA_OPTION_MP_SYNCHRO);
            gen_load_store_no_hw_align(ld32u); /*TODO acquire?*/
            break;

        case 12: /*ADDI*/
            gen_window_check2(dc, RRI8_S, RRI8_T);
            tcg_gen_addi_i32(cpu_R[RRI8_T], cpu_R[RRI8_S], RRI8_IMM8_SE);
            break;

        case 13: /*ADDMI*/
            gen_window_check2(dc, RRI8_S, RRI8_T);
            tcg_gen_addi_i32(cpu_R[RRI8_T], cpu_R[RRI8_S], RRI8_IMM8_SE << 8);
            break;

        case 14: /*S32C1Iy*/
            HAS_OPTION(XTENSA_OPTION_MP_SYNCHRO);
            gen_window_check2(dc, RRI8_S, RRI8_T);
            {
                int label = gen_new_label();
                TCGv_i32 tmp = tcg_temp_local_new_i32();
                TCGv_i32 addr = tcg_temp_local_new_i32();

                tcg_gen_mov_i32(tmp, cpu_R[RRI8_T]);
                tcg_gen_addi_i32(addr, cpu_R[RRI8_S], RRI8_IMM8 << 2);
                gen_load_store_alignment(dc, 2, addr, true);
                tcg_gen_qemu_ld32u(cpu_R[RRI8_T], addr, dc->cring);
                tcg_gen_brcond_i32(TCG_COND_NE, cpu_R[RRI8_T],
                        cpu_SR[SCOMPARE1], label);

                tcg_gen_qemu_st32(tmp, addr, dc->cring);

                gen_set_label(label);
                tcg_temp_free(addr);
                tcg_temp_free(tmp);
            }
            break;

        case 15: /*S32RIy*/
            HAS_OPTION(XTENSA_OPTION_MP_SYNCHRO);
            gen_load_store_no_hw_align(st32); /*TODO release?*/
            break;
#undef gen_load_store_no_hw_align

        default: /*reserved*/
            RESERVED();
            break;
        }
        break;

    case 3: /*LSCIp*/
        HAS_OPTION(XTENSA_OPTION_COPROCESSOR);
        TBD();
        break;

    case 4: /*MAC16d*/
        HAS_OPTION(XTENSA_OPTION_MAC16);
        TBD();
        break;

    case 5: /*CALLN*/
        switch (CALL_N) {
        case 0: /*CALL0*/
            tcg_gen_movi_i32(cpu_R[0], dc->next_pc);
            gen_jumpi(dc, (dc->pc & ~3) + (CALL_OFFSET_SE << 2) + 4, 0);
            break;

        case 1: /*CALL4w*/
        case 2: /*CALL8w*/
        case 3: /*CALL12w*/
            HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
            gen_window_check1(dc, CALL_N << 2);
            gen_callwi(dc, CALL_N,
                    (dc->pc & ~3) + (CALL_OFFSET_SE << 2) + 4, 0);
            break;
        }
        break;

    case 6: /*SI*/
        switch (CALL_N) {
        case 0: /*J*/
            gen_jumpi(dc, dc->pc + 4 + CALL_OFFSET_SE, 0);
            break;

        case 1: /*BZ*/
            gen_window_check1(dc, BRI12_S);
            {
                static const TCGCond cond[] = {
                    TCG_COND_EQ, /*BEQZ*/
                    TCG_COND_NE, /*BNEZ*/
                    TCG_COND_LT, /*BLTZ*/
                    TCG_COND_GE, /*BGEZ*/
                };

                gen_brcondi(dc, cond[BRI12_M & 3], cpu_R[BRI12_S], 0,
                        4 + BRI12_IMM12_SE);
            }
            break;

        case 2: /*BI0*/
            gen_window_check1(dc, BRI8_S);
            {
                static const TCGCond cond[] = {
                    TCG_COND_EQ, /*BEQI*/
                    TCG_COND_NE, /*BNEI*/
                    TCG_COND_LT, /*BLTI*/
                    TCG_COND_GE, /*BGEI*/
                };

                gen_brcondi(dc, cond[BRI8_M & 3],
                        cpu_R[BRI8_S], B4CONST[BRI8_R], 4 + BRI8_IMM8_SE);
            }
            break;

        case 3: /*BI1*/
            switch (BRI8_M) {
            case 0: /*ENTRYw*/
                HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                {
                    TCGv_i32 pc = tcg_const_i32(dc->pc);
                    TCGv_i32 s = tcg_const_i32(BRI12_S);
                    TCGv_i32 imm = tcg_const_i32(BRI12_IMM12);
                    gen_advance_ccount(dc);
                    gen_helper_entry(pc, s, imm);
                    tcg_temp_free(imm);
                    tcg_temp_free(s);
                    tcg_temp_free(pc);
                    reset_used_window(dc);
                }
                break;

            case 1: /*B1*/
                switch (BRI8_R) {
                case 0: /*BFp*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    TBD();
                    break;

                case 1: /*BTp*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    TBD();
                    break;

                case 8: /*LOOP*/
                case 9: /*LOOPNEZ*/
                case 10: /*LOOPGTZ*/
                    HAS_OPTION(XTENSA_OPTION_LOOP);
                    gen_window_check1(dc, RRI8_S);
                    {
                        uint32_t lend = dc->pc + RRI8_IMM8 + 4;
                        TCGv_i32 tmp = tcg_const_i32(lend);

                        tcg_gen_subi_i32(cpu_SR[LCOUNT], cpu_R[RRI8_S], 1);
                        tcg_gen_movi_i32(cpu_SR[LBEG], dc->next_pc);
                        gen_wsr_lend(dc, LEND, tmp);
                        tcg_temp_free(tmp);

                        if (BRI8_R > 8) {
                            int label = gen_new_label();
                            tcg_gen_brcondi_i32(
                                    BRI8_R == 9 ? TCG_COND_NE : TCG_COND_GT,
                                    cpu_R[RRI8_S], 0, label);
                            gen_jumpi(dc, lend, 1);
                            gen_set_label(label);
                        }

                        gen_jumpi(dc, dc->next_pc, 0);
                    }
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;

                }
                break;

            case 2: /*BLTUI*/
            case 3: /*BGEUI*/
                gen_window_check1(dc, BRI8_S);
                gen_brcondi(dc, BRI8_M == 2 ? TCG_COND_LTU : TCG_COND_GEU,
                        cpu_R[BRI8_S], B4CONSTU[BRI8_R], 4 + BRI8_IMM8_SE);
                break;
            }
            break;

        }
        break;

    case 7: /*B*/
        {
            TCGCond eq_ne = (RRI8_R & 8) ? TCG_COND_NE : TCG_COND_EQ;

            switch (RRI8_R & 7) {
            case 0: /*BNONE*/ /*BANY*/
                gen_window_check2(dc, RRI8_S, RRI8_T);
                {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_and_i32(tmp, cpu_R[RRI8_S], cpu_R[RRI8_T]);
                    gen_brcondi(dc, eq_ne, tmp, 0, 4 + RRI8_IMM8_SE);
                    tcg_temp_free(tmp);
                }
                break;

            case 1: /*BEQ*/ /*BNE*/
            case 2: /*BLT*/ /*BGE*/
            case 3: /*BLTU*/ /*BGEU*/
                gen_window_check2(dc, RRI8_S, RRI8_T);
                {
                    static const TCGCond cond[] = {
                        [1] = TCG_COND_EQ,
                        [2] = TCG_COND_LT,
                        [3] = TCG_COND_LTU,
                        [9] = TCG_COND_NE,
                        [10] = TCG_COND_GE,
                        [11] = TCG_COND_GEU,
                    };
                    gen_brcond(dc, cond[RRI8_R], cpu_R[RRI8_S], cpu_R[RRI8_T],
                            4 + RRI8_IMM8_SE);
                }
                break;

            case 4: /*BALL*/ /*BNALL*/
                gen_window_check2(dc, RRI8_S, RRI8_T);
                {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_and_i32(tmp, cpu_R[RRI8_S], cpu_R[RRI8_T]);
                    gen_brcond(dc, eq_ne, tmp, cpu_R[RRI8_T],
                            4 + RRI8_IMM8_SE);
                    tcg_temp_free(tmp);
                }
                break;

            case 5: /*BBC*/ /*BBS*/
                gen_window_check2(dc, RRI8_S, RRI8_T);
                {
                    TCGv_i32 bit = tcg_const_i32(1);
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_andi_i32(tmp, cpu_R[RRI8_T], 0x1f);
                    tcg_gen_shl_i32(bit, bit, tmp);
                    tcg_gen_and_i32(tmp, cpu_R[RRI8_S], bit);
                    gen_brcondi(dc, eq_ne, tmp, 0, 4 + RRI8_IMM8_SE);
                    tcg_temp_free(tmp);
                    tcg_temp_free(bit);
                }
                break;

            case 6: /*BBCI*/ /*BBSI*/
            case 7:
                gen_window_check1(dc, RRI8_S);
                {
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_andi_i32(tmp, cpu_R[RRI8_S],
                            1 << (((RRI8_R & 1) << 4) | RRI8_T));
                    gen_brcondi(dc, eq_ne, tmp, 0, 4 + RRI8_IMM8_SE);
                    tcg_temp_free(tmp);
                }
                break;

            }
        }
        break;

#define gen_narrow_load_store(type) do { \
            TCGv_i32 addr = tcg_temp_new_i32(); \
            gen_window_check2(dc, RRRN_S, RRRN_T); \
            tcg_gen_addi_i32(addr, cpu_R[RRRN_S], RRRN_R << 2); \
            gen_load_store_alignment(dc, 2, addr, false); \
            tcg_gen_qemu_##type(cpu_R[RRRN_T], addr, dc->cring); \
            tcg_temp_free(addr); \
        } while (0)

    case 8: /*L32I.Nn*/
        gen_narrow_load_store(ld32u);
        break;

    case 9: /*S32I.Nn*/
        gen_narrow_load_store(st32);
        break;
#undef gen_narrow_load_store

    case 10: /*ADD.Nn*/
        gen_window_check3(dc, RRRN_R, RRRN_S, RRRN_T);
        tcg_gen_add_i32(cpu_R[RRRN_R], cpu_R[RRRN_S], cpu_R[RRRN_T]);
        break;

    case 11: /*ADDI.Nn*/
        gen_window_check2(dc, RRRN_R, RRRN_S);
        tcg_gen_addi_i32(cpu_R[RRRN_R], cpu_R[RRRN_S], RRRN_T ? RRRN_T : -1);
        break;

    case 12: /*ST2n*/
        gen_window_check1(dc, RRRN_S);
        if (RRRN_T < 8) { /*MOVI.Nn*/
            tcg_gen_movi_i32(cpu_R[RRRN_S],
                    RRRN_R | (RRRN_T << 4) |
                    ((RRRN_T & 6) == 6 ? 0xffffff80 : 0));
        } else { /*BEQZ.Nn*/ /*BNEZ.Nn*/
            TCGCond eq_ne = (RRRN_T & 4) ? TCG_COND_NE : TCG_COND_EQ;

            gen_brcondi(dc, eq_ne, cpu_R[RRRN_S], 0,
                    4 + (RRRN_R | ((RRRN_T & 3) << 4)));
        }
        break;

    case 13: /*ST3n*/
        switch (RRRN_R) {
        case 0: /*MOV.Nn*/
            gen_window_check2(dc, RRRN_S, RRRN_T);
            tcg_gen_mov_i32(cpu_R[RRRN_T], cpu_R[RRRN_S]);
            break;

        case 15: /*S3*/
            switch (RRRN_T) {
            case 0: /*RET.Nn*/
                gen_jump(dc, cpu_R[0]);
                break;

            case 1: /*RETW.Nn*/
                HAS_OPTION(XTENSA_OPTION_WINDOWED_REGISTER);
                {
                    TCGv_i32 tmp = tcg_const_i32(dc->pc);
                    gen_advance_ccount(dc);
                    gen_helper_retw(tmp, tmp);
                    gen_jump(dc, tmp);
                    tcg_temp_free(tmp);
                }
                break;

            case 2: /*BREAK.Nn*/
                TBD();
                break;

            case 3: /*NOP.Nn*/
                break;

            case 6: /*ILL.Nn*/
                gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
                break;

            default: /*reserved*/
                RESERVED();
                break;
            }
            break;

        default: /*reserved*/
            RESERVED();
            break;
        }
        break;

    default: /*reserved*/
        RESERVED();
        break;
    }

    gen_check_loop_end(dc, 0);
    dc->pc = dc->next_pc;

    return;

invalid_opcode:
    qemu_log("INVALID(pc = %08x)\n", dc->pc);
    dc->pc = dc->next_pc;
#undef HAS_OPTION
}

static void check_breakpoint(CPUState *env, DisasContext *dc)
{
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&env->breakpoints))) {
        QTAILQ_FOREACH(bp, &env->breakpoints, entry) {
            if (bp->pc == dc->pc) {
                tcg_gen_movi_i32(cpu_pc, dc->pc);
                gen_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_UPDATE;
             }
        }
    }
}

static void gen_intermediate_code_internal(
        CPUState *env, TranslationBlock *tb, int search_pc)
{
    DisasContext dc;
    int insn_count = 0;
    int j, lj = -1;
    uint16_t *gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    int max_insns = tb->cflags & CF_COUNT_MASK;
    uint32_t pc_start = tb->pc;
    uint32_t next_page_start =
        (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    dc.config = env->config;
    dc.singlestep_enabled = env->singlestep_enabled;
    dc.tb = tb;
    dc.pc = pc_start;
    dc.ring = tb->flags & XTENSA_TBFLAG_RING_MASK;
    dc.cring = (tb->flags & XTENSA_TBFLAG_EXCM) ? 0 : dc.ring;
    dc.lbeg = env->sregs[LBEG];
    dc.lend = env->sregs[LEND];
    dc.is_jmp = DISAS_NEXT;
    dc.ccount_delta = 0;

    init_litbase(&dc);
    init_sar_tracker(&dc);
    reset_used_window(&dc);

    gen_icount_start();

    if (env->singlestep_enabled && env->exception_taken) {
        env->exception_taken = 0;
        tcg_gen_movi_i32(cpu_pc, dc.pc);
        gen_exception(&dc, EXCP_DEBUG);
    }

    do {
        check_breakpoint(env, &dc);

        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    gen_opc_instr_start[lj++] = 0;
                }
            }
            gen_opc_pc[lj] = dc.pc;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = insn_count;
        }

        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP))) {
            tcg_gen_debug_insn_start(dc.pc);
        }

        ++dc.ccount_delta;

        if (insn_count + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        disas_xtensa_insn(&dc);
        ++insn_count;
        if (env->singlestep_enabled) {
            tcg_gen_movi_i32(cpu_pc, dc.pc);
            gen_exception(&dc, EXCP_DEBUG);
            break;
        }
    } while (dc.is_jmp == DISAS_NEXT &&
            insn_count < max_insns &&
            dc.pc < next_page_start &&
            gen_opc_ptr < gen_opc_end);

    reset_litbase(&dc);
    reset_sar_tracker(&dc);

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (dc.is_jmp == DISAS_NEXT) {
        gen_jumpi(&dc, dc.pc, 0);
    }
    gen_icount_end(tb, insn_count);
    *gen_opc_ptr = INDEX_op_end;

    if (!search_pc) {
        tb->size = dc.pc - pc_start;
        tb->icount = insn_count;
    }
}

void gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

void cpu_dump_state(CPUState *env, FILE *f, fprintf_function cpu_fprintf,
        int flags)
{
    int i, j;

    cpu_fprintf(f, "PC=%08x\n\n", env->pc);

    for (i = j = 0; i < 256; ++i) {
        if (sregnames[i]) {
            cpu_fprintf(f, "%s=%08x%c", sregnames[i], env->sregs[i],
                    (j++ % 4) == 3 ? '\n' : ' ');
        }
    }

    cpu_fprintf(f, (j % 4) == 0 ? "\n" : "\n\n");

    for (i = j = 0; i < 256; ++i) {
        if (uregnames[i]) {
            cpu_fprintf(f, "%s=%08x%c", uregnames[i], env->uregs[i],
                    (j++ % 4) == 3 ? '\n' : ' ');
        }
    }

    cpu_fprintf(f, (j % 4) == 0 ? "\n" : "\n\n");

    for (i = 0; i < 16; ++i) {
        cpu_fprintf(f, "A%02d=%08x%c", i, env->regs[i],
                (i % 4) == 3 ? '\n' : ' ');
    }

    cpu_fprintf(f, "\n");

    for (i = 0; i < env->config->nareg; ++i) {
        cpu_fprintf(f, "AR%02d=%08x%c", i, env->phys_regs[i],
                (i % 4) == 3 ? '\n' : ' ');
    }
}

void restore_state_to_opc(CPUState *env, TranslationBlock *tb, int pc_pos)
{
    env->pc = gen_opc_pc[pc_pos];
}
