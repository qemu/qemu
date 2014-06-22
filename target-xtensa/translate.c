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
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

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

    bool debug;
    bool icount;
    TCGv_i32 next_icount;

    unsigned cpenable;
} DisasContext;

static TCGv_ptr cpu_env;
static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_R[16];
static TCGv_i32 cpu_FR[16];
static TCGv_i32 cpu_SR[256];
static TCGv_i32 cpu_UR[256];

#include "exec/gen-icount.h"

typedef struct XtensaReg {
    const char *name;
    uint64_t opt_bits;
    enum {
        SR_R = 1,
        SR_W = 2,
        SR_X = 4,
        SR_RW = 3,
        SR_RWX = 7,
    } access;
} XtensaReg;

#define XTENSA_REG_ACCESS(regname, opt, acc) { \
        .name = (regname), \
        .opt_bits = XTENSA_OPTION_BIT(opt), \
        .access = (acc), \
    }

#define XTENSA_REG(regname, opt) XTENSA_REG_ACCESS(regname, opt, SR_RWX)

#define XTENSA_REG_BITS_ACCESS(regname, opt, acc) { \
        .name = (regname), \
        .opt_bits = (opt), \
        .access = (acc), \
    }

#define XTENSA_REG_BITS(regname, opt) \
    XTENSA_REG_BITS_ACCESS(regname, opt, SR_RWX)

static const XtensaReg sregnames[256] = {
    [LBEG] = XTENSA_REG("LBEG", XTENSA_OPTION_LOOP),
    [LEND] = XTENSA_REG("LEND", XTENSA_OPTION_LOOP),
    [LCOUNT] = XTENSA_REG("LCOUNT", XTENSA_OPTION_LOOP),
    [SAR] = XTENSA_REG_BITS("SAR", XTENSA_OPTION_ALL),
    [BR] = XTENSA_REG("BR", XTENSA_OPTION_BOOLEAN),
    [LITBASE] = XTENSA_REG("LITBASE", XTENSA_OPTION_EXTENDED_L32R),
    [SCOMPARE1] = XTENSA_REG("SCOMPARE1", XTENSA_OPTION_CONDITIONAL_STORE),
    [ACCLO] = XTENSA_REG("ACCLO", XTENSA_OPTION_MAC16),
    [ACCHI] = XTENSA_REG("ACCHI", XTENSA_OPTION_MAC16),
    [MR] = XTENSA_REG("MR0", XTENSA_OPTION_MAC16),
    [MR + 1] = XTENSA_REG("MR1", XTENSA_OPTION_MAC16),
    [MR + 2] = XTENSA_REG("MR2", XTENSA_OPTION_MAC16),
    [MR + 3] = XTENSA_REG("MR3", XTENSA_OPTION_MAC16),
    [WINDOW_BASE] = XTENSA_REG("WINDOW_BASE", XTENSA_OPTION_WINDOWED_REGISTER),
    [WINDOW_START] = XTENSA_REG("WINDOW_START",
            XTENSA_OPTION_WINDOWED_REGISTER),
    [PTEVADDR] = XTENSA_REG("PTEVADDR", XTENSA_OPTION_MMU),
    [RASID] = XTENSA_REG("RASID", XTENSA_OPTION_MMU),
    [ITLBCFG] = XTENSA_REG("ITLBCFG", XTENSA_OPTION_MMU),
    [DTLBCFG] = XTENSA_REG("DTLBCFG", XTENSA_OPTION_MMU),
    [IBREAKENABLE] = XTENSA_REG("IBREAKENABLE", XTENSA_OPTION_DEBUG),
    [CACHEATTR] = XTENSA_REG("CACHEATTR", XTENSA_OPTION_CACHEATTR),
    [ATOMCTL] = XTENSA_REG("ATOMCTL", XTENSA_OPTION_ATOMCTL),
    [IBREAKA] = XTENSA_REG("IBREAKA0", XTENSA_OPTION_DEBUG),
    [IBREAKA + 1] = XTENSA_REG("IBREAKA1", XTENSA_OPTION_DEBUG),
    [DBREAKA] = XTENSA_REG("DBREAKA0", XTENSA_OPTION_DEBUG),
    [DBREAKA + 1] = XTENSA_REG("DBREAKA1", XTENSA_OPTION_DEBUG),
    [DBREAKC] = XTENSA_REG("DBREAKC0", XTENSA_OPTION_DEBUG),
    [DBREAKC + 1] = XTENSA_REG("DBREAKC1", XTENSA_OPTION_DEBUG),
    [CONFIGID0] = XTENSA_REG_BITS_ACCESS("CONFIGID0", XTENSA_OPTION_ALL, SR_R),
    [EPC1] = XTENSA_REG("EPC1", XTENSA_OPTION_EXCEPTION),
    [EPC1 + 1] = XTENSA_REG("EPC2", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPC1 + 2] = XTENSA_REG("EPC3", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPC1 + 3] = XTENSA_REG("EPC4", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPC1 + 4] = XTENSA_REG("EPC5", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPC1 + 5] = XTENSA_REG("EPC6", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPC1 + 6] = XTENSA_REG("EPC7", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [DEPC] = XTENSA_REG("DEPC", XTENSA_OPTION_EXCEPTION),
    [EPS2] = XTENSA_REG("EPS2", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPS2 + 1] = XTENSA_REG("EPS3", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPS2 + 2] = XTENSA_REG("EPS4", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPS2 + 3] = XTENSA_REG("EPS5", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPS2 + 4] = XTENSA_REG("EPS6", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EPS2 + 5] = XTENSA_REG("EPS7", XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [CONFIGID1] = XTENSA_REG_BITS_ACCESS("CONFIGID1", XTENSA_OPTION_ALL, SR_R),
    [EXCSAVE1] = XTENSA_REG("EXCSAVE1", XTENSA_OPTION_EXCEPTION),
    [EXCSAVE1 + 1] = XTENSA_REG("EXCSAVE2",
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EXCSAVE1 + 2] = XTENSA_REG("EXCSAVE3",
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EXCSAVE1 + 3] = XTENSA_REG("EXCSAVE4",
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EXCSAVE1 + 4] = XTENSA_REG("EXCSAVE5",
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EXCSAVE1 + 5] = XTENSA_REG("EXCSAVE6",
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [EXCSAVE1 + 6] = XTENSA_REG("EXCSAVE7",
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT),
    [CPENABLE] = XTENSA_REG("CPENABLE", XTENSA_OPTION_COPROCESSOR),
    [INTSET] = XTENSA_REG_ACCESS("INTSET", XTENSA_OPTION_INTERRUPT, SR_RW),
    [INTCLEAR] = XTENSA_REG_ACCESS("INTCLEAR", XTENSA_OPTION_INTERRUPT, SR_W),
    [INTENABLE] = XTENSA_REG("INTENABLE", XTENSA_OPTION_INTERRUPT),
    [PS] = XTENSA_REG_BITS("PS", XTENSA_OPTION_ALL),
    [VECBASE] = XTENSA_REG("VECBASE", XTENSA_OPTION_RELOCATABLE_VECTOR),
    [EXCCAUSE] = XTENSA_REG("EXCCAUSE", XTENSA_OPTION_EXCEPTION),
    [DEBUGCAUSE] = XTENSA_REG_ACCESS("DEBUGCAUSE", XTENSA_OPTION_DEBUG, SR_R),
    [CCOUNT] = XTENSA_REG("CCOUNT", XTENSA_OPTION_TIMER_INTERRUPT),
    [PRID] = XTENSA_REG_ACCESS("PRID", XTENSA_OPTION_PROCESSOR_ID, SR_R),
    [ICOUNT] = XTENSA_REG("ICOUNT", XTENSA_OPTION_DEBUG),
    [ICOUNTLEVEL] = XTENSA_REG("ICOUNTLEVEL", XTENSA_OPTION_DEBUG),
    [EXCVADDR] = XTENSA_REG("EXCVADDR", XTENSA_OPTION_EXCEPTION),
    [CCOMPARE] = XTENSA_REG("CCOMPARE0", XTENSA_OPTION_TIMER_INTERRUPT),
    [CCOMPARE + 1] = XTENSA_REG("CCOMPARE1",
            XTENSA_OPTION_TIMER_INTERRUPT),
    [CCOMPARE + 2] = XTENSA_REG("CCOMPARE2",
            XTENSA_OPTION_TIMER_INTERRUPT),
    [MISC] = XTENSA_REG("MISC0", XTENSA_OPTION_MISC_SR),
    [MISC + 1] = XTENSA_REG("MISC1", XTENSA_OPTION_MISC_SR),
    [MISC + 2] = XTENSA_REG("MISC2", XTENSA_OPTION_MISC_SR),
    [MISC + 3] = XTENSA_REG("MISC3", XTENSA_OPTION_MISC_SR),
};

static const XtensaReg uregnames[256] = {
    [THREADPTR] = XTENSA_REG("THREADPTR", XTENSA_OPTION_THREAD_POINTER),
    [FCR] = XTENSA_REG("FCR", XTENSA_OPTION_FP_COPROCESSOR),
    [FSR] = XTENSA_REG("FSR", XTENSA_OPTION_FP_COPROCESSOR),
};

void xtensa_translate_init(void)
{
    static const char * const regnames[] = {
        "ar0", "ar1", "ar2", "ar3",
        "ar4", "ar5", "ar6", "ar7",
        "ar8", "ar9", "ar10", "ar11",
        "ar12", "ar13", "ar14", "ar15",
    };
    static const char * const fregnames[] = {
        "f0", "f1", "f2", "f3",
        "f4", "f5", "f6", "f7",
        "f8", "f9", "f10", "f11",
        "f12", "f13", "f14", "f15",
    };
    int i;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    cpu_pc = tcg_global_mem_new_i32(TCG_AREG0,
            offsetof(CPUXtensaState, pc), "pc");

    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(TCG_AREG0,
                offsetof(CPUXtensaState, regs[i]),
                regnames[i]);
    }

    for (i = 0; i < 16; i++) {
        cpu_FR[i] = tcg_global_mem_new_i32(TCG_AREG0,
                offsetof(CPUXtensaState, fregs[i]),
                fregnames[i]);
    }

    for (i = 0; i < 256; ++i) {
        if (sregnames[i].name) {
            cpu_SR[i] = tcg_global_mem_new_i32(TCG_AREG0,
                    offsetof(CPUXtensaState, sregs[i]),
                    sregnames[i].name);
        }
    }

    for (i = 0; i < 256; ++i) {
        if (uregnames[i].name) {
            cpu_UR[i] = tcg_global_mem_new_i32(TCG_AREG0,
                    offsetof(CPUXtensaState, uregs[i]),
                    uregnames[i].name);
        }
    }
}

static inline bool option_bits_enabled(DisasContext *dc, uint64_t opt)
{
    return xtensa_option_bits_enabled(dc->config, opt);
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

static void gen_advance_ccount_cond(DisasContext *dc)
{
    if (dc->ccount_delta > 0) {
        TCGv_i32 tmp = tcg_const_i32(dc->ccount_delta);
        gen_helper_advance_ccount(cpu_env, tmp);
        tcg_temp_free(tmp);
    }
}

static void gen_advance_ccount(DisasContext *dc)
{
    gen_advance_ccount_cond(dc);
    dc->ccount_delta = 0;
}

static void reset_used_window(DisasContext *dc)
{
    dc->used_window = 0;
}

static void gen_exception(DisasContext *dc, int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_advance_ccount(dc);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free(tmp);
}

static void gen_exception_cause(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_advance_ccount(dc);
    gen_helper_exception_cause(cpu_env, tpc, tcause);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
    if (cause == ILLEGAL_INSTRUCTION_CAUSE ||
            cause == SYSCALL_CAUSE) {
        dc->is_jmp = DISAS_UPDATE;
    }
}

static void gen_exception_cause_vaddr(DisasContext *dc, uint32_t cause,
        TCGv_i32 vaddr)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_advance_ccount(dc);
    gen_helper_exception_cause_vaddr(cpu_env, tpc, tcause, vaddr);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
}

static void gen_debug_exception(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_advance_ccount(dc);
    gen_helper_debug_exception(cpu_env, tpc, tcause);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
    if (cause & (DEBUGCAUSE_IB | DEBUGCAUSE_BI | DEBUGCAUSE_BN)) {
        dc->is_jmp = DISAS_UPDATE;
    }
}

static void gen_check_privilege(DisasContext *dc)
{
    if (dc->cring) {
        gen_exception_cause(dc, PRIVILEGED_CAUSE);
        dc->is_jmp = DISAS_UPDATE;
    }
}

static void gen_check_cpenable(DisasContext *dc, unsigned cp)
{
    if (option_enabled(dc, XTENSA_OPTION_COPROCESSOR) &&
            !(dc->cpenable & (1 << cp))) {
        gen_exception_cause(dc, COPROCESSOR0_DISABLED + cp);
        dc->is_jmp = DISAS_UPDATE;
    }
}

static void gen_jump_slot(DisasContext *dc, TCGv dest, int slot)
{
    tcg_gen_mov_i32(cpu_pc, dest);
    gen_advance_ccount(dc);
    if (dc->icount) {
        tcg_gen_mov_i32(cpu_SR[ICOUNT], dc->next_icount);
    }
    if (dc->singlestep_enabled) {
        gen_exception(dc, EXCP_DEBUG);
    } else {
        if (slot >= 0) {
            tcg_gen_goto_tb(slot);
            tcg_gen_exit_tb((uintptr_t)dc->tb + slot);
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
    if (((dc->tb->pc ^ dest) & TARGET_PAGE_MASK) != 0) {
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
    if (((dc->tb->pc ^ dest) & TARGET_PAGE_MASK) != 0) {
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

        gen_advance_ccount(dc);
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

    gen_advance_ccount(dc);
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

static bool gen_check_sr(DisasContext *dc, uint32_t sr, unsigned access)
{
    if (!xtensa_option_bits_enabled(dc->config, sregnames[sr].opt_bits)) {
        if (sregnames[sr].name) {
            qemu_log("SR %s is not configured\n", sregnames[sr].name);
        } else {
            qemu_log("SR %d is not implemented\n", sr);
        }
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
        return false;
    } else if (!(sregnames[sr].access & access)) {
        static const char * const access_text[] = {
            [SR_R] = "rsr",
            [SR_W] = "wsr",
            [SR_X] = "xsr",
        };
        assert(access < ARRAY_SIZE(access_text) && access_text[access]);
        qemu_log("SR %s is not available for %s\n", sregnames[sr].name,
                access_text[access]);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
        return false;
    }
    return true;
}

static void gen_rsr_ccount(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    gen_advance_ccount(dc);
    tcg_gen_mov_i32(d, cpu_SR[sr]);
}

static void gen_rsr_ptevaddr(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    tcg_gen_shri_i32(d, cpu_SR[EXCVADDR], 10);
    tcg_gen_or_i32(d, d, cpu_SR[sr]);
    tcg_gen_andi_i32(d, d, 0xfffffffc);
}

static void gen_rsr(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    static void (* const rsr_handler[256])(DisasContext *dc,
            TCGv_i32 d, uint32_t sr) = {
        [CCOUNT] = gen_rsr_ccount,
        [PTEVADDR] = gen_rsr_ptevaddr,
    };

    if (rsr_handler[sr]) {
        rsr_handler[sr](dc, d, sr);
    } else {
        tcg_gen_mov_i32(d, cpu_SR[sr]);
    }
}

static void gen_wsr_lbeg(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    gen_helper_wsr_lbeg(cpu_env, s);
    gen_jumpi_check_loop_end(dc, 0);
}

static void gen_wsr_lend(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    gen_helper_wsr_lend(cpu_env, s);
    gen_jumpi_check_loop_end(dc, 0);
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

static void gen_wsr_br(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    tcg_gen_andi_i32(cpu_SR[sr], s, 0xffff);
}

static void gen_wsr_litbase(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    tcg_gen_andi_i32(cpu_SR[sr], s, 0xfffff001);
    /* This can change tb->flags, so exit tb */
    gen_jumpi_check_loop_end(dc, -1);
}

static void gen_wsr_acchi(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    tcg_gen_ext8s_i32(cpu_SR[sr], s);
}

static void gen_wsr_windowbase(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_windowbase(cpu_env, v);
    reset_used_window(dc);
}

static void gen_wsr_windowstart(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, (1 << dc->config->nareg / 4) - 1);
    reset_used_window(dc);
}

static void gen_wsr_ptevaddr(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0xffc00000);
}

static void gen_wsr_rasid(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_rasid(cpu_env, v);
    /* This can change tb->flags, so exit tb */
    gen_jumpi_check_loop_end(dc, -1);
}

static void gen_wsr_tlbcfg(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0x01130000);
}

static void gen_wsr_ibreakenable(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_ibreakenable(cpu_env, v);
    gen_jumpi_check_loop_end(dc, 0);
}

static void gen_wsr_atomctl(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0x3f);
}

static void gen_wsr_ibreaka(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    unsigned id = sr - IBREAKA;

    if (id < dc->config->nibreak) {
        TCGv_i32 tmp = tcg_const_i32(id);
        gen_helper_wsr_ibreaka(cpu_env, tmp, v);
        tcg_temp_free(tmp);
        gen_jumpi_check_loop_end(dc, 0);
    }
}

static void gen_wsr_dbreaka(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    unsigned id = sr - DBREAKA;

    if (id < dc->config->ndbreak) {
        TCGv_i32 tmp = tcg_const_i32(id);
        gen_helper_wsr_dbreaka(cpu_env, tmp, v);
        tcg_temp_free(tmp);
    }
}

static void gen_wsr_dbreakc(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    unsigned id = sr - DBREAKC;

    if (id < dc->config->ndbreak) {
        TCGv_i32 tmp = tcg_const_i32(id);
        gen_helper_wsr_dbreakc(cpu_env, tmp, v);
        tcg_temp_free(tmp);
    }
}

static void gen_wsr_cpenable(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0xff);
    /* This can change tb->flags, so exit tb */
    gen_jumpi_check_loop_end(dc, -1);
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

static void gen_wsr_icount(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    if (dc->icount) {
        tcg_gen_mov_i32(dc->next_icount, v);
    } else {
        tcg_gen_mov_i32(cpu_SR[sr], v);
    }
}

static void gen_wsr_icountlevel(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0xf);
    /* This can change tb->flags, so exit tb */
    gen_jumpi_check_loop_end(dc, -1);
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
        [BR] = gen_wsr_br,
        [LITBASE] = gen_wsr_litbase,
        [ACCHI] = gen_wsr_acchi,
        [WINDOW_BASE] = gen_wsr_windowbase,
        [WINDOW_START] = gen_wsr_windowstart,
        [PTEVADDR] = gen_wsr_ptevaddr,
        [RASID] = gen_wsr_rasid,
        [ITLBCFG] = gen_wsr_tlbcfg,
        [DTLBCFG] = gen_wsr_tlbcfg,
        [IBREAKENABLE] = gen_wsr_ibreakenable,
        [ATOMCTL] = gen_wsr_atomctl,
        [IBREAKA] = gen_wsr_ibreaka,
        [IBREAKA + 1] = gen_wsr_ibreaka,
        [DBREAKA] = gen_wsr_dbreaka,
        [DBREAKA + 1] = gen_wsr_dbreaka,
        [DBREAKC] = gen_wsr_dbreakc,
        [DBREAKC + 1] = gen_wsr_dbreakc,
        [CPENABLE] = gen_wsr_cpenable,
        [INTSET] = gen_wsr_intset,
        [INTCLEAR] = gen_wsr_intclear,
        [INTENABLE] = gen_wsr_intenable,
        [PS] = gen_wsr_ps,
        [ICOUNT] = gen_wsr_icount,
        [ICOUNTLEVEL] = gen_wsr_icountlevel,
        [CCOMPARE] = gen_wsr_ccompare,
        [CCOMPARE + 1] = gen_wsr_ccompare,
        [CCOMPARE + 2] = gen_wsr_ccompare,
    };

    if (wsr_handler[sr]) {
        wsr_handler[sr](dc, sr, s);
    } else {
        tcg_gen_mov_i32(cpu_SR[sr], s);
    }
}

static void gen_wur(uint32_t ur, TCGv_i32 s)
{
    switch (ur) {
    case FCR:
        gen_helper_wur_fcr(cpu_env, s);
        break;

    case FSR:
        tcg_gen_andi_i32(cpu_UR[ur], s, 0xffffff80);
        break;

    default:
        tcg_gen_mov_i32(cpu_UR[ur], s);
        break;
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
    gen_helper_waiti(cpu_env, pc, intlevel);
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
        int label = gen_new_label();
        TCGv_i32 ws = tcg_temp_new_i32();

        dc->used_window = r1 / 4;
        tcg_gen_deposit_i32(ws, cpu_SR[WINDOW_START], cpu_SR[WINDOW_START],
                dc->config->nareg / 4, dc->config->nareg / 4);
        tcg_gen_shr_i32(ws, ws, cpu_SR[WINDOW_BASE]);
        tcg_gen_andi_i32(ws, ws, (2 << (r1 / 4)) - 2);
        tcg_gen_brcondi_i32(TCG_COND_EQ, ws, 0, label);
        {
            TCGv_i32 pc = tcg_const_i32(dc->pc);
            TCGv_i32 w = tcg_const_i32(r1 / 4);

            gen_advance_ccount_cond(dc);
            gen_helper_window_check(cpu_env, pc, w);

            tcg_temp_free(w);
            tcg_temp_free(pc);
        }
        gen_set_label(label);
        tcg_temp_free(ws);
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

static TCGv_i32 gen_mac16_m(TCGv_i32 v, bool hi, bool is_unsigned)
{
    TCGv_i32 m = tcg_temp_new_i32();

    if (hi) {
        (is_unsigned ? tcg_gen_shri_i32 : tcg_gen_sari_i32)(m, v, 16);
    } else {
        (is_unsigned ? tcg_gen_ext16u_i32 : tcg_gen_ext16s_i32)(m, v);
    }
    return m;
}

static void disas_xtensa_insn(CPUXtensaState *env, DisasContext *dc)
{
#define HAS_OPTION_BITS(opt) do { \
        if (!option_bits_enabled(dc, opt)) { \
            qemu_log("Option is not enabled %s:%d\n", \
                    __FILE__, __LINE__); \
            goto invalid_opcode; \
        } \
    } while (0)

#define HAS_OPTION(opt) HAS_OPTION_BITS(XTENSA_OPTION_BIT(opt))

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
#define RRR_X ((RRR_R & 0x4) >> 2)
#define RRR_Y ((RRR_T & 0x4) >> 2)
#define RRR_W (RRR_R & 0x3)

#define RRRN_R RRR_R
#define RRRN_S RRR_S
#define RRRN_T RRR_T

#define RRI4_R RRR_R
#define RRI4_S RRR_S
#define RRI4_T RRR_T
#ifdef TARGET_WORDS_BIGENDIAN
#define RRI4_IMM4 ((b2) & 0xf)
#else
#define RRI4_IMM4 (((b2) & 0xf0) >> 4)
#endif

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

    uint8_t b0 = cpu_ldub_code(env, dc->pc);
    uint8_t b1 = cpu_ldub_code(env, dc->pc + 1);
    uint8_t b2 = 0;

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
        b2 = cpu_ldub_code(env, dc->pc + 2);
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
                                gen_helper_retw(tmp, cpu_env, tmp);
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
                        gen_helper_movsp(cpu_env, pc);
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

                                gen_helper_restore_owb(cpu_env);
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
                    HAS_OPTION(XTENSA_OPTION_DEBUG);
                    if (dc->debug) {
                        gen_debug_exception(dc, DEBUGCAUSE_BI);
                    }
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
                case 9: /*ALL4p*/
                case 10: /*ANY8p*/
                case 11: /*ALL8p*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    {
                        const unsigned shift = (RRR_R & 2) ? 8 : 4;
                        TCGv_i32 mask = tcg_const_i32(
                                ((1 << shift) - 1) << RRR_S);
                        TCGv_i32 tmp = tcg_temp_new_i32();

                        tcg_gen_and_i32(tmp, cpu_SR[BR], mask);
                        if (RRR_R & 1) { /*ALL*/
                            tcg_gen_addi_i32(tmp, tmp, 1 << RRR_S);
                        } else { /*ANY*/
                            tcg_gen_add_i32(tmp, tmp, mask);
                        }
                        tcg_gen_shri_i32(tmp, tmp, RRR_S + shift);
                        tcg_gen_deposit_i32(cpu_SR[BR], cpu_SR[BR],
                                tmp, RRR_T, 1);
                        tcg_temp_free(mask);
                        tcg_temp_free(tmp);
                    }
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
                        gen_helper_rotw(cpu_env, tmp);
                        tcg_temp_free(tmp);
                        reset_used_window(dc);
                    }
                    break;

                case 14: /*NSAu*/
                    HAS_OPTION(XTENSA_OPTION_MISC_OP_NSA);
                    gen_window_check2(dc, RRR_S, RRR_T);
                    gen_helper_nsa(cpu_R[RRR_T], cpu_R[RRR_S]);
                    break;

                case 15: /*NSAUu*/
                    HAS_OPTION(XTENSA_OPTION_MISC_OP_NSA);
                    gen_window_check2(dc, RRR_S, RRR_T);
                    gen_helper_nsau(cpu_R[RRR_T], cpu_R[RRR_S]);
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;
                }
                break;

            case 5: /*TLB*/
                HAS_OPTION_BITS(
                        XTENSA_OPTION_BIT(XTENSA_OPTION_MMU) |
                        XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_PROTECTION) |
                        XTENSA_OPTION_BIT(XTENSA_OPTION_REGION_TRANSLATION));
                gen_check_privilege(dc);
                gen_window_check2(dc, RRR_S, RRR_T);
                {
                    TCGv_i32 dtlb = tcg_const_i32((RRR_R & 8) != 0);

                    switch (RRR_R & 7) {
                    case 3: /*RITLB0*/ /*RDTLB0*/
                        gen_helper_rtlb0(cpu_R[RRR_T],
                                cpu_env, cpu_R[RRR_S], dtlb);
                        break;

                    case 4: /*IITLB*/ /*IDTLB*/
                        gen_helper_itlb(cpu_env, cpu_R[RRR_S], dtlb);
                        /* This could change memory mapping, so exit tb */
                        gen_jumpi_check_loop_end(dc, -1);
                        break;

                    case 5: /*PITLB*/ /*PDTLB*/
                        tcg_gen_movi_i32(cpu_pc, dc->pc);
                        gen_helper_ptlb(cpu_R[RRR_T],
                                cpu_env, cpu_R[RRR_S], dtlb);
                        break;

                    case 6: /*WITLB*/ /*WDTLB*/
                        gen_helper_wtlb(
                                cpu_env, cpu_R[RRR_T], cpu_R[RRR_S], dtlb);
                        /* This could change memory mapping, so exit tb */
                        gen_jumpi_check_loop_end(dc, -1);
                        break;

                    case 7: /*RITLB1*/ /*RDTLB1*/
                        gen_helper_rtlb1(cpu_R[RRR_T],
                                cpu_env, cpu_R[RRR_S], dtlb);
                        break;

                    default:
                        tcg_temp_free(dtlb);
                        RESERVED();
                        break;
                    }
                    tcg_temp_free(dtlb);
                }
                break;

            case 6: /*RT0*/
                gen_window_check2(dc, RRR_R, RRR_T);
                switch (RRR_S) {
                case 0: /*NEG*/
                    tcg_gen_neg_i32(cpu_R[RRR_R], cpu_R[RRR_T]);
                    break;

                case 1: /*ABS*/
                    {
                        TCGv_i32 zero = tcg_const_i32(0);
                        TCGv_i32 neg = tcg_temp_new_i32();

                        tcg_gen_neg_i32(neg, cpu_R[RRR_T]);
                        tcg_gen_movcond_i32(TCG_COND_GE, cpu_R[RRR_R],
                                cpu_R[RRR_T], zero, cpu_R[RRR_T], neg);
                        tcg_temp_free(neg);
                        tcg_temp_free(zero);
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
                if (gen_check_sr(dc, RSR_SR, SR_X)) {
                    TCGv_i32 tmp = tcg_temp_new_i32();

                    if (RSR_SR >= 64) {
                        gen_check_privilege(dc);
                    }
                    gen_window_check1(dc, RRR_T);
                    tcg_gen_mov_i32(tmp, cpu_R[RRR_T]);
                    gen_rsr(dc, cpu_R[RRR_T], RSR_SR);
                    gen_wsr(dc, RSR_SR, tmp);
                    tcg_temp_free(tmp);
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
            if (OP2 >= 8) {
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
            }

            if (OP2 >= 12) {
                HAS_OPTION(XTENSA_OPTION_32_BIT_IDIV);
                int label = gen_new_label();
                tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[RRR_T], 0, label);
                gen_exception_cause(dc, INTEGER_DIVIDE_BY_ZERO_CAUSE);
                gen_set_label(label);
            }

            switch (OP2) {
#define BOOLEAN_LOGIC(fn, r, s, t) \
                do { \
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN); \
                    TCGv_i32 tmp1 = tcg_temp_new_i32(); \
                    TCGv_i32 tmp2 = tcg_temp_new_i32(); \
                    \
                    tcg_gen_shri_i32(tmp1, cpu_SR[BR], s); \
                    tcg_gen_shri_i32(tmp2, cpu_SR[BR], t); \
                    tcg_gen_##fn##_i32(tmp1, tmp1, tmp2); \
                    tcg_gen_deposit_i32(cpu_SR[BR], cpu_SR[BR], tmp1, r, 1); \
                    tcg_temp_free(tmp1); \
                    tcg_temp_free(tmp2); \
                } while (0)

            case 0: /*ANDBp*/
                BOOLEAN_LOGIC(and, RRR_R, RRR_S, RRR_T);
                break;

            case 1: /*ANDBCp*/
                BOOLEAN_LOGIC(andc, RRR_R, RRR_S, RRR_T);
                break;

            case 2: /*ORBp*/
                BOOLEAN_LOGIC(or, RRR_R, RRR_S, RRR_T);
                break;

            case 3: /*ORBCp*/
                BOOLEAN_LOGIC(orc, RRR_R, RRR_S, RRR_T);
                break;

            case 4: /*XORBp*/
                BOOLEAN_LOGIC(xor, RRR_R, RRR_S, RRR_T);
                break;

#undef BOOLEAN_LOGIC

            case 8: /*MULLi*/
                HAS_OPTION(XTENSA_OPTION_32_BIT_IMUL);
                tcg_gen_mul_i32(cpu_R[RRR_R], cpu_R[RRR_S], cpu_R[RRR_T]);
                break;

            case 10: /*MULUHi*/
            case 11: /*MULSHi*/
                HAS_OPTION(XTENSA_OPTION_32_BIT_IMUL_HIGH);
                {
                    TCGv lo = tcg_temp_new();

                    if (OP2 == 10) {
                        tcg_gen_mulu2_i32(lo, cpu_R[RRR_R],
                                          cpu_R[RRR_S], cpu_R[RRR_T]);
                    } else {
                        tcg_gen_muls2_i32(lo, cpu_R[RRR_R],
                                          cpu_R[RRR_S], cpu_R[RRR_T]);
                    }
                    tcg_temp_free(lo);
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
                if (gen_check_sr(dc, RSR_SR, SR_R)) {
                    if (RSR_SR >= 64) {
                        gen_check_privilege(dc);
                    }
                    gen_window_check1(dc, RRR_T);
                    gen_rsr(dc, cpu_R[RRR_T], RSR_SR);
                }
                break;

            case 1: /*WSR*/
                if (gen_check_sr(dc, RSR_SR, SR_W)) {
                    if (RSR_SR >= 64) {
                        gen_check_privilege(dc);
                    }
                    gen_window_check1(dc, RRR_T);
                    gen_wsr(dc, RSR_SR, cpu_R[RRR_T]);
                }
                break;

            case 2: /*SEXTu*/
                HAS_OPTION(XTENSA_OPTION_MISC_OP_SEXT);
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
                HAS_OPTION(XTENSA_OPTION_MISC_OP_CLAMPS);
                gen_window_check2(dc, RRR_R, RRR_S);
                {
                    TCGv_i32 tmp1 = tcg_temp_new_i32();
                    TCGv_i32 tmp2 = tcg_temp_new_i32();
                    TCGv_i32 zero = tcg_const_i32(0);

                    tcg_gen_sari_i32(tmp1, cpu_R[RRR_S], 24 - RRR_T);
                    tcg_gen_xor_i32(tmp2, tmp1, cpu_R[RRR_S]);
                    tcg_gen_andi_i32(tmp2, tmp2, 0xffffffff << (RRR_T + 7));

                    tcg_gen_sari_i32(tmp1, cpu_R[RRR_S], 31);
                    tcg_gen_xori_i32(tmp1, tmp1, 0xffffffff >> (25 - RRR_T));

                    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_R[RRR_R], tmp2, zero,
                            cpu_R[RRR_S], tmp1);
                    tcg_temp_free(tmp1);
                    tcg_temp_free(tmp2);
                    tcg_temp_free(zero);
                }
                break;

            case 4: /*MINu*/
            case 5: /*MAXu*/
            case 6: /*MINUu*/
            case 7: /*MAXUu*/
                HAS_OPTION(XTENSA_OPTION_MISC_OP_MINMAX);
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    static const TCGCond cond[] = {
                        TCG_COND_LE,
                        TCG_COND_GE,
                        TCG_COND_LEU,
                        TCG_COND_GEU
                    };
                    tcg_gen_movcond_i32(cond[OP2 - 4], cpu_R[RRR_R],
                            cpu_R[RRR_S], cpu_R[RRR_T],
                            cpu_R[RRR_S], cpu_R[RRR_T]);
                }
                break;

            case 8: /*MOVEQZ*/
            case 9: /*MOVNEZ*/
            case 10: /*MOVLTZ*/
            case 11: /*MOVGEZ*/
                gen_window_check3(dc, RRR_R, RRR_S, RRR_T);
                {
                    static const TCGCond cond[] = {
                        TCG_COND_EQ,
                        TCG_COND_NE,
                        TCG_COND_LT,
                        TCG_COND_GE,
                    };
                    TCGv_i32 zero = tcg_const_i32(0);

                    tcg_gen_movcond_i32(cond[OP2 - 8], cpu_R[RRR_R],
                            cpu_R[RRR_T], zero, cpu_R[RRR_S], cpu_R[RRR_R]);
                    tcg_temp_free(zero);
                }
                break;

            case 12: /*MOVFp*/
            case 13: /*MOVTp*/
                HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                gen_window_check2(dc, RRR_R, RRR_S);
                {
                    TCGv_i32 zero = tcg_const_i32(0);
                    TCGv_i32 tmp = tcg_temp_new_i32();

                    tcg_gen_andi_i32(tmp, cpu_SR[BR], 1 << RRR_T);
                    tcg_gen_movcond_i32(OP2 & 1 ? TCG_COND_NE : TCG_COND_EQ,
                            cpu_R[RRR_R], tmp, zero,
                            cpu_R[RRR_S], cpu_R[RRR_R]);

                    tcg_temp_free(tmp);
                    tcg_temp_free(zero);
                }
                break;

            case 14: /*RUR*/
                gen_window_check1(dc, RRR_R);
                {
                    int st = (RRR_S << 4) + RRR_T;
                    if (uregnames[st].name) {
                        tcg_gen_mov_i32(cpu_R[RRR_R], cpu_UR[st]);
                    } else {
                        qemu_log("RUR %d not implemented, ", st);
                        TBD();
                    }
                }
                break;

            case 15: /*WUR*/
                gen_window_check1(dc, RRR_T);
                if (uregnames[RSR_SR].name) {
                    gen_wur(RSR_SR, cpu_R[RRR_T]);
                } else {
                    qemu_log("WUR %d not implemented, ", RSR_SR);
                    TBD();
                }
                break;

            }
            break;

        case 4: /*EXTUI*/
        case 5:
            gen_window_check2(dc, RRR_R, RRR_T);
            {
                int shiftimm = RRR_S | ((OP1 & 1) << 4);
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
            switch (OP2) {
            case 0: /*LSXf*/
            case 1: /*LSXUf*/
            case 4: /*SSXf*/
            case 5: /*SSXUf*/
                HAS_OPTION(XTENSA_OPTION_FP_COPROCESSOR);
                gen_window_check2(dc, RRR_S, RRR_T);
                gen_check_cpenable(dc, 0);
                {
                    TCGv_i32 addr = tcg_temp_new_i32();
                    tcg_gen_add_i32(addr, cpu_R[RRR_S], cpu_R[RRR_T]);
                    gen_load_store_alignment(dc, 2, addr, false);
                    if (OP2 & 0x4) {
                        tcg_gen_qemu_st32(cpu_FR[RRR_R], addr, dc->cring);
                    } else {
                        tcg_gen_qemu_ld32u(cpu_FR[RRR_R], addr, dc->cring);
                    }
                    if (OP2 & 0x1) {
                        tcg_gen_mov_i32(cpu_R[RRR_S], addr);
                    }
                    tcg_temp_free(addr);
                }
                break;

            default: /*reserved*/
                RESERVED();
                break;
            }
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
            switch (OP2) {
            case 0: /*ADD.Sf*/
                gen_check_cpenable(dc, 0);
                gen_helper_add_s(cpu_FR[RRR_R], cpu_env,
                        cpu_FR[RRR_S], cpu_FR[RRR_T]);
                break;

            case 1: /*SUB.Sf*/
                gen_check_cpenable(dc, 0);
                gen_helper_sub_s(cpu_FR[RRR_R], cpu_env,
                        cpu_FR[RRR_S], cpu_FR[RRR_T]);
                break;

            case 2: /*MUL.Sf*/
                gen_check_cpenable(dc, 0);
                gen_helper_mul_s(cpu_FR[RRR_R], cpu_env,
                        cpu_FR[RRR_S], cpu_FR[RRR_T]);
                break;

            case 4: /*MADD.Sf*/
                gen_check_cpenable(dc, 0);
                gen_helper_madd_s(cpu_FR[RRR_R], cpu_env,
                        cpu_FR[RRR_R], cpu_FR[RRR_S], cpu_FR[RRR_T]);
                break;

            case 5: /*MSUB.Sf*/
                gen_check_cpenable(dc, 0);
                gen_helper_msub_s(cpu_FR[RRR_R], cpu_env,
                        cpu_FR[RRR_R], cpu_FR[RRR_S], cpu_FR[RRR_T]);
                break;

            case 8: /*ROUND.Sf*/
            case 9: /*TRUNC.Sf*/
            case 10: /*FLOOR.Sf*/
            case 11: /*CEIL.Sf*/
            case 14: /*UTRUNC.Sf*/
                gen_window_check1(dc, RRR_R);
                gen_check_cpenable(dc, 0);
                {
                    static const unsigned rounding_mode_const[] = {
                        float_round_nearest_even,
                        float_round_to_zero,
                        float_round_down,
                        float_round_up,
                        [6] = float_round_to_zero,
                    };
                    TCGv_i32 rounding_mode = tcg_const_i32(
                            rounding_mode_const[OP2 & 7]);
                    TCGv_i32 scale = tcg_const_i32(RRR_T);

                    if (OP2 == 14) {
                        gen_helper_ftoui(cpu_R[RRR_R], cpu_FR[RRR_S],
                                rounding_mode, scale);
                    } else {
                        gen_helper_ftoi(cpu_R[RRR_R], cpu_FR[RRR_S],
                                rounding_mode, scale);
                    }

                    tcg_temp_free(rounding_mode);
                    tcg_temp_free(scale);
                }
                break;

            case 12: /*FLOAT.Sf*/
            case 13: /*UFLOAT.Sf*/
                gen_window_check1(dc, RRR_S);
                gen_check_cpenable(dc, 0);
                {
                    TCGv_i32 scale = tcg_const_i32(-RRR_T);

                    if (OP2 == 13) {
                        gen_helper_uitof(cpu_FR[RRR_R], cpu_env,
                                cpu_R[RRR_S], scale);
                    } else {
                        gen_helper_itof(cpu_FR[RRR_R], cpu_env,
                                cpu_R[RRR_S], scale);
                    }
                    tcg_temp_free(scale);
                }
                break;

            case 15: /*FP1OP*/
                switch (RRR_T) {
                case 0: /*MOV.Sf*/
                    gen_check_cpenable(dc, 0);
                    tcg_gen_mov_i32(cpu_FR[RRR_R], cpu_FR[RRR_S]);
                    break;

                case 1: /*ABS.Sf*/
                    gen_check_cpenable(dc, 0);
                    gen_helper_abs_s(cpu_FR[RRR_R], cpu_FR[RRR_S]);
                    break;

                case 4: /*RFRf*/
                    gen_window_check1(dc, RRR_R);
                    gen_check_cpenable(dc, 0);
                    tcg_gen_mov_i32(cpu_R[RRR_R], cpu_FR[RRR_S]);
                    break;

                case 5: /*WFRf*/
                    gen_window_check1(dc, RRR_S);
                    gen_check_cpenable(dc, 0);
                    tcg_gen_mov_i32(cpu_FR[RRR_R], cpu_R[RRR_S]);
                    break;

                case 6: /*NEG.Sf*/
                    gen_check_cpenable(dc, 0);
                    gen_helper_neg_s(cpu_FR[RRR_R], cpu_FR[RRR_S]);
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

        case 11: /*FP1*/
            HAS_OPTION(XTENSA_OPTION_FP_COPROCESSOR);

#define gen_compare(rel, br, a, b) \
    do { \
        TCGv_i32 bit = tcg_const_i32(1 << br); \
        \
        gen_check_cpenable(dc, 0); \
        gen_helper_##rel(cpu_env, bit, cpu_FR[a], cpu_FR[b]); \
        tcg_temp_free(bit); \
    } while (0)

            switch (OP2) {
            case 1: /*UN.Sf*/
                gen_compare(un_s, RRR_R, RRR_S, RRR_T);
                break;

            case 2: /*OEQ.Sf*/
                gen_compare(oeq_s, RRR_R, RRR_S, RRR_T);
                break;

            case 3: /*UEQ.Sf*/
                gen_compare(ueq_s, RRR_R, RRR_S, RRR_T);
                break;

            case 4: /*OLT.Sf*/
                gen_compare(olt_s, RRR_R, RRR_S, RRR_T);
                break;

            case 5: /*ULT.Sf*/
                gen_compare(ult_s, RRR_R, RRR_S, RRR_T);
                break;

            case 6: /*OLE.Sf*/
                gen_compare(ole_s, RRR_R, RRR_S, RRR_T);
                break;

            case 7: /*ULE.Sf*/
                gen_compare(ule_s, RRR_R, RRR_S, RRR_T);
                break;

#undef gen_compare

            case 8: /*MOVEQZ.Sf*/
            case 9: /*MOVNEZ.Sf*/
            case 10: /*MOVLTZ.Sf*/
            case 11: /*MOVGEZ.Sf*/
                gen_window_check1(dc, RRR_T);
                gen_check_cpenable(dc, 0);
                {
                    static const TCGCond cond[] = {
                        TCG_COND_EQ,
                        TCG_COND_NE,
                        TCG_COND_LT,
                        TCG_COND_GE,
                    };
                    TCGv_i32 zero = tcg_const_i32(0);

                    tcg_gen_movcond_i32(cond[OP2 - 8], cpu_FR[RRR_R],
                            cpu_R[RRR_T], zero, cpu_FR[RRR_S], cpu_FR[RRR_R]);
                    tcg_temp_free(zero);
                }
                break;

            case 12: /*MOVF.Sf*/
            case 13: /*MOVT.Sf*/
                HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                gen_check_cpenable(dc, 0);
                {
                    TCGv_i32 zero = tcg_const_i32(0);
                    TCGv_i32 tmp = tcg_temp_new_i32();

                    tcg_gen_andi_i32(tmp, cpu_SR[BR], 1 << RRR_T);
                    tcg_gen_movcond_i32(OP2 & 1 ? TCG_COND_NE : TCG_COND_EQ,
                            cpu_FR[RRR_R], tmp, zero,
                            cpu_FR[RRR_S], cpu_FR[RRR_R]);

                    tcg_temp_free(tmp);
                    tcg_temp_free(zero);
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

#define gen_dcache_hit_test(w, shift) do { \
            TCGv_i32 addr = tcg_temp_new_i32(); \
            TCGv_i32 res = tcg_temp_new_i32(); \
            gen_window_check1(dc, RRI##w##_S); \
            tcg_gen_addi_i32(addr, cpu_R[RRI##w##_S], \
                             RRI##w##_IMM##w << shift); \
            tcg_gen_qemu_ld8u(res, addr, dc->cring); \
            tcg_temp_free(addr); \
            tcg_temp_free(res); \
        } while (0)

#define gen_dcache_hit_test4() gen_dcache_hit_test(4, 4)
#define gen_dcache_hit_test8() gen_dcache_hit_test(8, 2)

        case 7: /*CACHEc*/
            if (RRI8_T < 8) {
                HAS_OPTION(XTENSA_OPTION_DCACHE);
            }

            switch (RRI8_T) {
            case 0: /*DPFRc*/
                gen_window_check1(dc, RRI8_S);
                break;

            case 1: /*DPFWc*/
                gen_window_check1(dc, RRI8_S);
                break;

            case 2: /*DPFROc*/
                gen_window_check1(dc, RRI8_S);
                break;

            case 3: /*DPFWOc*/
                gen_window_check1(dc, RRI8_S);
                break;

            case 4: /*DHWBc*/
                gen_dcache_hit_test8();
                break;

            case 5: /*DHWBIc*/
                gen_dcache_hit_test8();
                break;

            case 6: /*DHIc*/
                gen_check_privilege(dc);
                gen_dcache_hit_test8();
                break;

            case 7: /*DIIc*/
                gen_check_privilege(dc);
                gen_window_check1(dc, RRI8_S);
                break;

            case 8: /*DCEc*/
                switch (OP1) {
                case 0: /*DPFLl*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE_INDEX_LOCK);
                    gen_check_privilege(dc);
                    gen_dcache_hit_test4();
                    break;

                case 2: /*DHUl*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE_INDEX_LOCK);
                    gen_check_privilege(dc);
                    gen_dcache_hit_test4();
                    break;

                case 3: /*DIUl*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE_INDEX_LOCK);
                    gen_check_privilege(dc);
                    gen_window_check1(dc, RRI4_S);
                    break;

                case 4: /*DIWBc*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE);
                    gen_check_privilege(dc);
                    gen_window_check1(dc, RRI4_S);
                    break;

                case 5: /*DIWBIc*/
                    HAS_OPTION(XTENSA_OPTION_DCACHE);
                    gen_check_privilege(dc);
                    gen_window_check1(dc, RRI4_S);
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;

                }
                break;

#undef gen_dcache_hit_test
#undef gen_dcache_hit_test4
#undef gen_dcache_hit_test8

#define gen_icache_hit_test(w, shift) do { \
            TCGv_i32 addr = tcg_temp_new_i32(); \
            gen_window_check1(dc, RRI##w##_S); \
            tcg_gen_movi_i32(cpu_pc, dc->pc); \
            tcg_gen_addi_i32(addr, cpu_R[RRI##w##_S], \
                             RRI##w##_IMM##w << shift); \
            gen_helper_itlb_hit_test(cpu_env, addr); \
            tcg_temp_free(addr); \
        } while (0)

#define gen_icache_hit_test4() gen_icache_hit_test(4, 4)
#define gen_icache_hit_test8() gen_icache_hit_test(8, 2)

            case 12: /*IPFc*/
                HAS_OPTION(XTENSA_OPTION_ICACHE);
                gen_window_check1(dc, RRI8_S);
                break;

            case 13: /*ICEc*/
                switch (OP1) {
                case 0: /*IPFLl*/
                    HAS_OPTION(XTENSA_OPTION_ICACHE_INDEX_LOCK);
                    gen_check_privilege(dc);
                    gen_icache_hit_test4();
                    break;

                case 2: /*IHUl*/
                    HAS_OPTION(XTENSA_OPTION_ICACHE_INDEX_LOCK);
                    gen_check_privilege(dc);
                    gen_icache_hit_test4();
                    break;

                case 3: /*IIUl*/
                    HAS_OPTION(XTENSA_OPTION_ICACHE_INDEX_LOCK);
                    gen_check_privilege(dc);
                    gen_window_check1(dc, RRI4_S);
                    break;

                default: /*reserved*/
                    RESERVED();
                    break;
                }
                break;

            case 14: /*IHIc*/
                HAS_OPTION(XTENSA_OPTION_ICACHE);
                gen_icache_hit_test8();
                break;

            case 15: /*IIIc*/
                HAS_OPTION(XTENSA_OPTION_ICACHE);
                gen_check_privilege(dc);
                gen_window_check1(dc, RRI8_S);
                break;

            default: /*reserved*/
                RESERVED();
                break;
            }
            break;

#undef gen_icache_hit_test
#undef gen_icache_hit_test4
#undef gen_icache_hit_test8

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
            HAS_OPTION(XTENSA_OPTION_CONDITIONAL_STORE);
            gen_window_check2(dc, RRI8_S, RRI8_T);
            {
                int label = gen_new_label();
                TCGv_i32 tmp = tcg_temp_local_new_i32();
                TCGv_i32 addr = tcg_temp_local_new_i32();
                TCGv_i32 tpc;

                tcg_gen_mov_i32(tmp, cpu_R[RRI8_T]);
                tcg_gen_addi_i32(addr, cpu_R[RRI8_S], RRI8_IMM8 << 2);
                gen_load_store_alignment(dc, 2, addr, true);

                gen_advance_ccount(dc);
                tpc = tcg_const_i32(dc->pc);
                gen_helper_check_atomctl(cpu_env, tpc, addr);
                tcg_gen_qemu_ld32u(cpu_R[RRI8_T], addr, dc->cring);
                tcg_gen_brcond_i32(TCG_COND_NE, cpu_R[RRI8_T],
                        cpu_SR[SCOMPARE1], label);

                tcg_gen_qemu_st32(tmp, addr, dc->cring);

                gen_set_label(label);
                tcg_temp_free(tpc);
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
        switch (RRI8_R) {
        case 0: /*LSIf*/
        case 4: /*SSIf*/
        case 8: /*LSIUf*/
        case 12: /*SSIUf*/
            HAS_OPTION(XTENSA_OPTION_FP_COPROCESSOR);
            gen_window_check1(dc, RRI8_S);
            gen_check_cpenable(dc, 0);
            {
                TCGv_i32 addr = tcg_temp_new_i32();
                tcg_gen_addi_i32(addr, cpu_R[RRI8_S], RRI8_IMM8 << 2);
                gen_load_store_alignment(dc, 2, addr, false);
                if (RRI8_R & 0x4) {
                    tcg_gen_qemu_st32(cpu_FR[RRI8_T], addr, dc->cring);
                } else {
                    tcg_gen_qemu_ld32u(cpu_FR[RRI8_T], addr, dc->cring);
                }
                if (RRI8_R & 0x8) {
                    tcg_gen_mov_i32(cpu_R[RRI8_S], addr);
                }
                tcg_temp_free(addr);
            }
            break;

        default: /*reserved*/
            RESERVED();
            break;
        }
        break;

    case 4: /*MAC16d*/
        HAS_OPTION(XTENSA_OPTION_MAC16);
        {
            enum {
                MAC16_UMUL = 0x0,
                MAC16_MUL  = 0x4,
                MAC16_MULA = 0x8,
                MAC16_MULS = 0xc,
                MAC16_NONE = 0xf,
            } op = OP1 & 0xc;
            bool is_m1_sr = (OP2 & 0x3) == 2;
            bool is_m2_sr = (OP2 & 0xc) == 0;
            uint32_t ld_offset = 0;

            if (OP2 > 9) {
                RESERVED();
            }

            switch (OP2 & 2) {
            case 0: /*MACI?/MACC?*/
                is_m1_sr = true;
                ld_offset = (OP2 & 1) ? -4 : 4;

                if (OP2 >= 8) { /*MACI/MACC*/
                    if (OP1 == 0) { /*LDINC/LDDEC*/
                        op = MAC16_NONE;
                    } else {
                        RESERVED();
                    }
                } else if (op != MAC16_MULA) { /*MULA.*.*.LDINC/LDDEC*/
                    RESERVED();
                }
                break;

            case 2: /*MACD?/MACA?*/
                if (op == MAC16_UMUL && OP2 != 7) { /*UMUL only in MACAA*/
                    RESERVED();
                }
                break;
            }

            if (op != MAC16_NONE) {
                if (!is_m1_sr) {
                    gen_window_check1(dc, RRR_S);
                }
                if (!is_m2_sr) {
                    gen_window_check1(dc, RRR_T);
                }
            }

            {
                TCGv_i32 vaddr = tcg_temp_new_i32();
                TCGv_i32 mem32 = tcg_temp_new_i32();

                if (ld_offset) {
                    gen_window_check1(dc, RRR_S);
                    tcg_gen_addi_i32(vaddr, cpu_R[RRR_S], ld_offset);
                    gen_load_store_alignment(dc, 2, vaddr, false);
                    tcg_gen_qemu_ld32u(mem32, vaddr, dc->cring);
                }
                if (op != MAC16_NONE) {
                    TCGv_i32 m1 = gen_mac16_m(
                            is_m1_sr ? cpu_SR[MR + RRR_X] : cpu_R[RRR_S],
                            OP1 & 1, op == MAC16_UMUL);
                    TCGv_i32 m2 = gen_mac16_m(
                            is_m2_sr ? cpu_SR[MR + 2 + RRR_Y] : cpu_R[RRR_T],
                            OP1 & 2, op == MAC16_UMUL);

                    if (op == MAC16_MUL || op == MAC16_UMUL) {
                        tcg_gen_mul_i32(cpu_SR[ACCLO], m1, m2);
                        if (op == MAC16_UMUL) {
                            tcg_gen_movi_i32(cpu_SR[ACCHI], 0);
                        } else {
                            tcg_gen_sari_i32(cpu_SR[ACCHI], cpu_SR[ACCLO], 31);
                        }
                    } else {
                        TCGv_i32 lo = tcg_temp_new_i32();
                        TCGv_i32 hi = tcg_temp_new_i32();

                        tcg_gen_mul_i32(lo, m1, m2);
                        tcg_gen_sari_i32(hi, lo, 31);
                        if (op == MAC16_MULA) {
                            tcg_gen_add2_i32(cpu_SR[ACCLO], cpu_SR[ACCHI],
                                             cpu_SR[ACCLO], cpu_SR[ACCHI],
                                             lo, hi);
                        } else {
                            tcg_gen_sub2_i32(cpu_SR[ACCLO], cpu_SR[ACCHI],
                                             cpu_SR[ACCLO], cpu_SR[ACCHI],
                                             lo, hi);
                        }
                        tcg_gen_ext8s_i32(cpu_SR[ACCHI], cpu_SR[ACCHI]);

                        tcg_temp_free_i32(lo);
                        tcg_temp_free_i32(hi);
                    }
                    tcg_temp_free(m1);
                    tcg_temp_free(m2);
                }
                if (ld_offset) {
                    tcg_gen_mov_i32(cpu_R[RRR_S], vaddr);
                    tcg_gen_mov_i32(cpu_SR[MR + RRR_W], mem32);
                }
                tcg_temp_free(vaddr);
                tcg_temp_free(mem32);
            }
        }
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
                    gen_helper_entry(cpu_env, pc, s, imm);
                    tcg_temp_free(imm);
                    tcg_temp_free(s);
                    tcg_temp_free(pc);
                    reset_used_window(dc);
                }
                break;

            case 1: /*B1*/
                switch (BRI8_R) {
                case 0: /*BFp*/
                case 1: /*BTp*/
                    HAS_OPTION(XTENSA_OPTION_BOOLEAN);
                    {
                        TCGv_i32 tmp = tcg_temp_new_i32();
                        tcg_gen_andi_i32(tmp, cpu_SR[BR], 1 << RRI8_S);
                        gen_brcondi(dc,
                                BRI8_R == 1 ? TCG_COND_NE : TCG_COND_EQ,
                                tmp, 0, 4 + RRI8_IMM8_SE);
                        tcg_temp_free(tmp);
                    }
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
                        gen_helper_wsr_lend(cpu_env, tmp);
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
#ifdef TARGET_WORDS_BIGENDIAN
                    TCGv_i32 bit = tcg_const_i32(0x80000000);
#else
                    TCGv_i32 bit = tcg_const_i32(0x00000001);
#endif
                    TCGv_i32 tmp = tcg_temp_new_i32();
                    tcg_gen_andi_i32(tmp, cpu_R[RRI8_T], 0x1f);
#ifdef TARGET_WORDS_BIGENDIAN
                    tcg_gen_shr_i32(bit, bit, tmp);
#else
                    tcg_gen_shl_i32(bit, bit, tmp);
#endif
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
#ifdef TARGET_WORDS_BIGENDIAN
                            0x80000000 >> (((RRI8_R & 1) << 4) | RRI8_T));
#else
                            0x00000001 << (((RRI8_R & 1) << 4) | RRI8_T));
#endif
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
                    gen_helper_retw(tmp, cpu_env, tmp);
                    gen_jump(dc, tmp);
                    tcg_temp_free(tmp);
                }
                break;

            case 2: /*BREAK.Nn*/
                HAS_OPTION(XTENSA_OPTION_DEBUG);
                if (dc->debug) {
                    gen_debug_exception(dc, DEBUGCAUSE_BN);
                }
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

    if (dc->is_jmp == DISAS_NEXT) {
        gen_check_loop_end(dc, 0);
    }
    dc->pc = dc->next_pc;

    return;

invalid_opcode:
    qemu_log("INVALID(pc = %08x)\n", dc->pc);
    gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
#undef HAS_OPTION
}

static void check_breakpoint(CPUXtensaState *env, DisasContext *dc)
{
    CPUState *cs = CPU(xtensa_env_get_cpu(env));
    CPUBreakpoint *bp;

    if (unlikely(!QTAILQ_EMPTY(&cs->breakpoints))) {
        QTAILQ_FOREACH(bp, &cs->breakpoints, entry) {
            if (bp->pc == dc->pc) {
                tcg_gen_movi_i32(cpu_pc, dc->pc);
                gen_exception(dc, EXCP_DEBUG);
                dc->is_jmp = DISAS_UPDATE;
             }
        }
    }
}

static void gen_ibreak_check(CPUXtensaState *env, DisasContext *dc)
{
    unsigned i;

    for (i = 0; i < dc->config->nibreak; ++i) {
        if ((env->sregs[IBREAKENABLE] & (1 << i)) &&
                env->sregs[IBREAKA + i] == dc->pc) {
            gen_debug_exception(dc, DEBUGCAUSE_IB);
            break;
        }
    }
}

static inline
void gen_intermediate_code_internal(XtensaCPU *cpu,
                                    TranslationBlock *tb, bool search_pc)
{
    CPUState *cs = CPU(cpu);
    CPUXtensaState *env = &cpu->env;
    DisasContext dc;
    int insn_count = 0;
    int j, lj = -1;
    uint16_t *gen_opc_end = tcg_ctx.gen_opc_buf + OPC_MAX_SIZE;
    int max_insns = tb->cflags & CF_COUNT_MASK;
    uint32_t pc_start = tb->pc;
    uint32_t next_page_start =
        (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;

    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }

    dc.config = env->config;
    dc.singlestep_enabled = cs->singlestep_enabled;
    dc.tb = tb;
    dc.pc = pc_start;
    dc.ring = tb->flags & XTENSA_TBFLAG_RING_MASK;
    dc.cring = (tb->flags & XTENSA_TBFLAG_EXCM) ? 0 : dc.ring;
    dc.lbeg = env->sregs[LBEG];
    dc.lend = env->sregs[LEND];
    dc.is_jmp = DISAS_NEXT;
    dc.ccount_delta = 0;
    dc.debug = tb->flags & XTENSA_TBFLAG_DEBUG;
    dc.icount = tb->flags & XTENSA_TBFLAG_ICOUNT;
    dc.cpenable = (tb->flags & XTENSA_TBFLAG_CPENABLE_MASK) >>
        XTENSA_TBFLAG_CPENABLE_SHIFT;

    init_litbase(&dc);
    init_sar_tracker(&dc);
    reset_used_window(&dc);
    if (dc.icount) {
        dc.next_icount = tcg_temp_local_new_i32();
    }

    gen_tb_start();

    if (tb->flags & XTENSA_TBFLAG_EXCEPTION) {
        tcg_gen_movi_i32(cpu_pc, dc.pc);
        gen_exception(&dc, EXCP_DEBUG);
    }

    do {
        check_breakpoint(env, &dc);

        if (search_pc) {
            j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j) {
                    tcg_ctx.gen_opc_instr_start[lj++] = 0;
                }
            }
            tcg_ctx.gen_opc_pc[lj] = dc.pc;
            tcg_ctx.gen_opc_instr_start[lj] = 1;
            tcg_ctx.gen_opc_icount[lj] = insn_count;
        }

        if (unlikely(qemu_loglevel_mask(CPU_LOG_TB_OP | CPU_LOG_TB_OP_OPT))) {
            tcg_gen_debug_insn_start(dc.pc);
        }

        ++dc.ccount_delta;

        if (insn_count + 1 == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        if (dc.icount) {
            int label = gen_new_label();

            tcg_gen_addi_i32(dc.next_icount, cpu_SR[ICOUNT], 1);
            tcg_gen_brcondi_i32(TCG_COND_NE, dc.next_icount, 0, label);
            tcg_gen_mov_i32(dc.next_icount, cpu_SR[ICOUNT]);
            if (dc.debug) {
                gen_debug_exception(&dc, DEBUGCAUSE_IC);
            }
            gen_set_label(label);
        }

        if (dc.debug) {
            gen_ibreak_check(env, &dc);
        }

        disas_xtensa_insn(env, &dc);
        ++insn_count;
        if (dc.icount) {
            tcg_gen_mov_i32(cpu_SR[ICOUNT], dc.next_icount);
        }
        if (cs->singlestep_enabled) {
            tcg_gen_movi_i32(cpu_pc, dc.pc);
            gen_exception(&dc, EXCP_DEBUG);
            break;
        }
    } while (dc.is_jmp == DISAS_NEXT &&
            insn_count < max_insns &&
            dc.pc < next_page_start &&
            tcg_ctx.gen_opc_ptr < gen_opc_end);

    reset_litbase(&dc);
    reset_sar_tracker(&dc);
    if (dc.icount) {
        tcg_temp_free(dc.next_icount);
    }

    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }

    if (dc.is_jmp == DISAS_NEXT) {
        gen_jumpi(&dc, dc.pc, 0);
    }
    gen_tb_end(tb, insn_count);
    *tcg_ctx.gen_opc_ptr = INDEX_op_end;

#ifdef DEBUG_DISAS
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)) {
        qemu_log("----------------\n");
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(env, pc_start, dc.pc - pc_start, 0);
        qemu_log("\n");
    }
#endif
    if (search_pc) {
        j = tcg_ctx.gen_opc_ptr - tcg_ctx.gen_opc_buf;
        memset(tcg_ctx.gen_opc_instr_start + lj + 1, 0,
                (j - lj) * sizeof(tcg_ctx.gen_opc_instr_start[0]));
    } else {
        tb->size = dc.pc - pc_start;
        tb->icount = insn_count;
    }
}

void gen_intermediate_code(CPUXtensaState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(xtensa_env_get_cpu(env), tb, false);
}

void gen_intermediate_code_pc(CPUXtensaState *env, TranslationBlock *tb)
{
    gen_intermediate_code_internal(xtensa_env_get_cpu(env), tb, true);
}

void xtensa_cpu_dump_state(CPUState *cs, FILE *f,
                           fprintf_function cpu_fprintf, int flags)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;
    int i, j;

    cpu_fprintf(f, "PC=%08x\n\n", env->pc);

    for (i = j = 0; i < 256; ++i) {
        if (xtensa_option_bits_enabled(env->config, sregnames[i].opt_bits)) {
            cpu_fprintf(f, "%12s=%08x%c", sregnames[i].name, env->sregs[i],
                    (j++ % 4) == 3 ? '\n' : ' ');
        }
    }

    cpu_fprintf(f, (j % 4) == 0 ? "\n" : "\n\n");

    for (i = j = 0; i < 256; ++i) {
        if (xtensa_option_bits_enabled(env->config, uregnames[i].opt_bits)) {
            cpu_fprintf(f, "%s=%08x%c", uregnames[i].name, env->uregs[i],
                    (j++ % 4) == 3 ? '\n' : ' ');
        }
    }

    cpu_fprintf(f, (j % 4) == 0 ? "\n" : "\n\n");

    for (i = 0; i < 16; ++i) {
        cpu_fprintf(f, " A%02d=%08x%c", i, env->regs[i],
                (i % 4) == 3 ? '\n' : ' ');
    }

    cpu_fprintf(f, "\n");

    for (i = 0; i < env->config->nareg; ++i) {
        cpu_fprintf(f, "AR%02d=%08x%c", i, env->phys_regs[i],
                (i % 4) == 3 ? '\n' : ' ');
    }

    if (xtensa_option_enabled(env->config, XTENSA_OPTION_FP_COPROCESSOR)) {
        cpu_fprintf(f, "\n");

        for (i = 0; i < 16; ++i) {
            cpu_fprintf(f, "F%02d=%08x (%+10.8e)%c", i,
                    float32_val(env->fregs[i]),
                    *(float *)&env->fregs[i], (i % 2) == 1 ? '\n' : ' ');
        }
    }
}

void restore_state_to_opc(CPUXtensaState *env, TranslationBlock *tb, int pc_pos)
{
    env->pc = tcg_ctx.gen_opc_pc[pc_pos];
}
