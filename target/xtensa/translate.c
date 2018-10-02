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

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "qemu/log.h"
#include "sysemu/sysemu.h"
#include "exec/cpu_ldst.h"
#include "exec/semihost.h"
#include "exec/translator.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "trace-tcg.h"
#include "exec/log.h"


struct DisasContext {
    DisasContextBase base;
    const XtensaConfig *config;
    uint32_t pc;
    int cring;
    int ring;
    uint32_t lbeg;
    uint32_t lend;

    bool sar_5bit;
    bool sar_m32_5bit;
    bool sar_m32_allocated;
    TCGv_i32 sar_m32;

    unsigned window;
    unsigned callinc;
    bool cwoe;

    bool debug;
    bool icount;
    TCGv_i32 next_icount;

    unsigned cpenable;

    uint32_t *raw_arg;
    xtensa_insnbuf insnbuf;
    xtensa_insnbuf slotbuf;
};

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
    [MMID] = XTENSA_REG_BITS("MMID", XTENSA_OPTION_ALL),
    [RASID] = XTENSA_REG("RASID", XTENSA_OPTION_MMU),
    [ITLBCFG] = XTENSA_REG("ITLBCFG", XTENSA_OPTION_MMU),
    [DTLBCFG] = XTENSA_REG("DTLBCFG", XTENSA_OPTION_MMU),
    [IBREAKENABLE] = XTENSA_REG("IBREAKENABLE", XTENSA_OPTION_DEBUG),
    [MEMCTL] = XTENSA_REG_BITS("MEMCTL", XTENSA_OPTION_ALL),
    [CACHEATTR] = XTENSA_REG("CACHEATTR", XTENSA_OPTION_CACHEATTR),
    [ATOMCTL] = XTENSA_REG("ATOMCTL", XTENSA_OPTION_ATOMCTL),
    [DDR] = XTENSA_REG("DDR", XTENSA_OPTION_DEBUG),
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
    [EXPSTATE] = XTENSA_REG_BITS("EXPSTATE", XTENSA_OPTION_ALL),
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

    cpu_pc = tcg_global_mem_new_i32(cpu_env,
            offsetof(CPUXtensaState, pc), "pc");

    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new_i32(cpu_env,
                offsetof(CPUXtensaState, regs[i]),
                regnames[i]);
    }

    for (i = 0; i < 16; i++) {
        cpu_FR[i] = tcg_global_mem_new_i32(cpu_env,
                offsetof(CPUXtensaState, fregs[i].f32[FP_F32_LOW]),
                fregnames[i]);
    }

    for (i = 0; i < 256; ++i) {
        if (sregnames[i].name) {
            cpu_SR[i] = tcg_global_mem_new_i32(cpu_env,
                    offsetof(CPUXtensaState, sregs[i]),
                    sregnames[i].name);
        }
    }

    for (i = 0; i < 256; ++i) {
        if (uregnames[i].name) {
            cpu_UR[i] = tcg_global_mem_new_i32(cpu_env,
                    offsetof(CPUXtensaState, uregs[i]),
                    uregnames[i].name);
        }
    }
}

static inline bool option_enabled(DisasContext *dc, int opt)
{
    return xtensa_option_enabled(dc->config, opt);
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

static void gen_exception(DisasContext *dc, int excp)
{
    TCGv_i32 tmp = tcg_const_i32(excp);
    gen_helper_exception(cpu_env, tmp);
    tcg_temp_free(tmp);
}

static void gen_exception_cause(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_helper_exception_cause(cpu_env, tpc, tcause);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
    if (cause == ILLEGAL_INSTRUCTION_CAUSE ||
            cause == SYSCALL_CAUSE) {
        dc->base.is_jmp = DISAS_NORETURN;
    }
}

static void gen_exception_cause_vaddr(DisasContext *dc, uint32_t cause,
        TCGv_i32 vaddr)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_helper_exception_cause_vaddr(cpu_env, tpc, tcause, vaddr);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
}

static void gen_debug_exception(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);
    TCGv_i32 tcause = tcg_const_i32(cause);
    gen_helper_debug_exception(cpu_env, tpc, tcause);
    tcg_temp_free(tpc);
    tcg_temp_free(tcause);
    if (cause & (DEBUGCAUSE_IB | DEBUGCAUSE_BI | DEBUGCAUSE_BN)) {
        dc->base.is_jmp = DISAS_NORETURN;
    }
}

static bool gen_check_privilege(DisasContext *dc)
{
#ifndef CONFIG_USER_ONLY
    if (!dc->cring) {
        return true;
    }
#endif
    gen_exception_cause(dc, PRIVILEGED_CAUSE);
    dc->base.is_jmp = DISAS_NORETURN;
    return false;
}

static bool gen_check_cpenable(DisasContext *dc, uint32_t cp_mask)
{
    cp_mask &= ~dc->cpenable;

    if (option_enabled(dc, XTENSA_OPTION_COPROCESSOR) && cp_mask) {
        gen_exception_cause(dc, COPROCESSOR0_DISABLED + ctz32(cp_mask));
        dc->base.is_jmp = DISAS_NORETURN;
        return false;
    }
    return true;
}

static void gen_jump_slot(DisasContext *dc, TCGv dest, int slot)
{
    tcg_gen_mov_i32(cpu_pc, dest);
    if (dc->icount) {
        tcg_gen_mov_i32(cpu_SR[ICOUNT], dc->next_icount);
    }
    if (dc->base.singlestep_enabled) {
        gen_exception(dc, EXCP_DEBUG);
    } else {
        if (slot >= 0) {
            tcg_gen_goto_tb(slot);
            tcg_gen_exit_tb(dc->base.tb, slot);
        } else {
            tcg_gen_exit_tb(NULL, 0);
        }
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_jump(DisasContext *dc, TCGv dest)
{
    gen_jump_slot(dc, dest, -1);
}

static void gen_jumpi(DisasContext *dc, uint32_t dest, int slot)
{
    TCGv_i32 tmp = tcg_const_i32(dest);
#ifndef CONFIG_USER_ONLY
    if (((dc->base.pc_first ^ dest) & TARGET_PAGE_MASK) != 0) {
        slot = -1;
    }
#endif
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
            (callinc << 30) | (dc->base.pc_next & 0x3fffffff));
    gen_jump_slot(dc, dest, slot);
}

static void gen_callw(DisasContext *dc, int callinc, TCGv_i32 dest)
{
    gen_callw_slot(dc, callinc, dest, -1);
}

static void gen_callwi(DisasContext *dc, int callinc, uint32_t dest, int slot)
{
    TCGv_i32 tmp = tcg_const_i32(dest);
#ifndef CONFIG_USER_ONLY
    if (((dc->base.pc_first ^ dest) & TARGET_PAGE_MASK) != 0) {
        slot = -1;
    }
#endif
    gen_callw_slot(dc, callinc, tmp, slot);
    tcg_temp_free(tmp);
}

static bool gen_check_loop_end(DisasContext *dc, int slot)
{
    if (option_enabled(dc, XTENSA_OPTION_LOOP) &&
            !(dc->base.tb->flags & XTENSA_TBFLAG_EXCM) &&
            dc->base.pc_next == dc->lend) {
        TCGLabel *label = gen_new_label();

        tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_SR[LCOUNT], 0, label);
        tcg_gen_subi_i32(cpu_SR[LCOUNT], cpu_SR[LCOUNT], 1);
        gen_jumpi(dc, dc->lbeg, slot);
        gen_set_label(label);
        gen_jumpi(dc, dc->base.pc_next, -1);
        return true;
    }
    return false;
}

static void gen_jumpi_check_loop_end(DisasContext *dc, int slot)
{
    if (!gen_check_loop_end(dc, slot)) {
        gen_jumpi(dc, dc->base.pc_next, slot);
    }
}

static void gen_brcond(DisasContext *dc, TCGCond cond,
                       TCGv_i32 t0, TCGv_i32 t1, uint32_t addr)
{
    TCGLabel *label = gen_new_label();

    tcg_gen_brcond_i32(cond, t0, t1, label);
    gen_jumpi_check_loop_end(dc, 0);
    gen_set_label(label);
    gen_jumpi(dc, addr, 1);
}

static void gen_brcondi(DisasContext *dc, TCGCond cond,
                        TCGv_i32 t0, uint32_t t1, uint32_t addr)
{
    TCGv_i32 tmp = tcg_const_i32(t1);
    gen_brcond(dc, cond, t0, tmp, addr);
    tcg_temp_free(tmp);
}

static bool check_sr(DisasContext *dc, uint32_t sr, unsigned access)
{
    if (!xtensa_option_bits_enabled(dc->config, sregnames[sr].opt_bits)) {
        if (sregnames[sr].name) {
            qemu_log_mask(LOG_GUEST_ERROR, "SR %s is not configured\n", sregnames[sr].name);
        } else {
            qemu_log_mask(LOG_UNIMP, "SR %d is not implemented\n", sr);
        }
        return false;
    } else if (!(sregnames[sr].access & access)) {
        static const char * const access_text[] = {
            [SR_R] = "rsr",
            [SR_W] = "wsr",
            [SR_X] = "xsr",
        };
        assert(access < ARRAY_SIZE(access_text) && access_text[access]);
        qemu_log_mask(LOG_GUEST_ERROR, "SR %s is not available for %s\n", sregnames[sr].name,
                      access_text[access]);
        return false;
    }
    return true;
}

#ifndef CONFIG_USER_ONLY
static void gen_rsr_ccount(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_update_ccount(cpu_env);
    tcg_gen_mov_i32(d, cpu_SR[sr]);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_end();
    }
}

static void gen_rsr_ptevaddr(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    tcg_gen_shri_i32(d, cpu_SR[EXCVADDR], 10);
    tcg_gen_or_i32(d, d, cpu_SR[sr]);
    tcg_gen_andi_i32(d, d, 0xfffffffc);
}
#endif

static void gen_rsr(DisasContext *dc, TCGv_i32 d, uint32_t sr)
{
    static void (* const rsr_handler[256])(DisasContext *dc,
                                           TCGv_i32 d, uint32_t sr) = {
#ifndef CONFIG_USER_ONLY
        [CCOUNT] = gen_rsr_ccount,
        [INTSET] = gen_rsr_ccount,
        [PTEVADDR] = gen_rsr_ptevaddr,
#endif
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
}

static void gen_wsr_lend(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    gen_helper_wsr_lend(cpu_env, s);
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
}

static void gen_wsr_acchi(DisasContext *dc, uint32_t sr, TCGv_i32 s)
{
    tcg_gen_ext8s_i32(cpu_SR[sr], s);
}

#ifndef CONFIG_USER_ONLY
static void gen_wsr_windowbase(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_windowbase(cpu_env, v);
}

static void gen_wsr_windowstart(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, (1 << dc->config->nareg / 4) - 1);
}

static void gen_wsr_ptevaddr(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0xffc00000);
}

static void gen_wsr_rasid(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_rasid(cpu_env, v);
}

static void gen_wsr_tlbcfg(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0x01130000);
}

static void gen_wsr_ibreakenable(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_ibreakenable(cpu_env, v);
}

static void gen_wsr_memctl(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    gen_helper_wsr_memctl(cpu_env, v);
}

static void gen_wsr_atomctl(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0x3f);
}

static void gen_wsr_ibreaka(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    unsigned id = sr - IBREAKA;
    TCGv_i32 tmp = tcg_const_i32(id);

    assert(id < dc->config->nibreak);
    gen_helper_wsr_ibreaka(cpu_env, tmp, v);
    tcg_temp_free(tmp);
}

static void gen_wsr_dbreaka(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    unsigned id = sr - DBREAKA;
    TCGv_i32 tmp = tcg_const_i32(id);

    assert(id < dc->config->ndbreak);
    gen_helper_wsr_dbreaka(cpu_env, tmp, v);
    tcg_temp_free(tmp);
}

static void gen_wsr_dbreakc(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    unsigned id = sr - DBREAKC;
    TCGv_i32 tmp = tcg_const_i32(id);

    assert(id < dc->config->ndbreak);
    gen_helper_wsr_dbreakc(cpu_env, tmp, v);
    tcg_temp_free(tmp);
}

static void gen_wsr_cpenable(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v, 0xff);
}

static void gen_check_interrupts(DisasContext *dc)
{
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_check_interrupts(cpu_env);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_end();
    }
}

static void gen_wsr_intset(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_andi_i32(cpu_SR[sr], v,
            dc->config->inttype_mask[INTTYPE_SOFTWARE]);
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
}

static void gen_wsr_intenable(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    tcg_gen_mov_i32(cpu_SR[sr], v);
}

static void gen_wsr_ps(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    uint32_t mask = PS_WOE | PS_CALLINC | PS_OWB |
        PS_UM | PS_EXCM | PS_INTLEVEL;

    if (option_enabled(dc, XTENSA_OPTION_MMU)) {
        mask |= PS_RING;
    }
    tcg_gen_andi_i32(cpu_SR[sr], v, mask);
}

static void gen_wsr_ccount(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_wsr_ccount(cpu_env, v);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_end();
    }
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
}

static void gen_wsr_ccompare(DisasContext *dc, uint32_t sr, TCGv_i32 v)
{
    uint32_t id = sr - CCOMPARE;
    uint32_t int_bit = 1 << dc->config->timerint[id];
    TCGv_i32 tmp = tcg_const_i32(id);

    assert(id < dc->config->nccompare);
    tcg_gen_mov_i32(cpu_SR[sr], v);
    tcg_gen_andi_i32(cpu_SR[INTSET], cpu_SR[INTSET], ~int_bit);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_update_ccompare(cpu_env, tmp);
    tcg_temp_free(tmp);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_end();
    }
}
#else
static void gen_check_interrupts(DisasContext *dc)
{
}
#endif

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
#ifndef CONFIG_USER_ONLY
        [WINDOW_BASE] = gen_wsr_windowbase,
        [WINDOW_START] = gen_wsr_windowstart,
        [PTEVADDR] = gen_wsr_ptevaddr,
        [RASID] = gen_wsr_rasid,
        [ITLBCFG] = gen_wsr_tlbcfg,
        [DTLBCFG] = gen_wsr_tlbcfg,
        [IBREAKENABLE] = gen_wsr_ibreakenable,
        [MEMCTL] = gen_wsr_memctl,
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
        [CCOUNT] = gen_wsr_ccount,
        [ICOUNT] = gen_wsr_icount,
        [ICOUNTLEVEL] = gen_wsr_icountlevel,
        [CCOMPARE] = gen_wsr_ccompare,
        [CCOMPARE + 1] = gen_wsr_ccompare,
        [CCOMPARE + 2] = gen_wsr_ccompare,
#endif
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
        TCGLabel *label = gen_new_label();
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_andi_i32(tmp, addr, ~(~0 << shift));
        tcg_gen_brcondi_i32(TCG_COND_EQ, tmp, 0, label);
        gen_exception_cause_vaddr(dc, LOAD_STORE_ALIGNMENT_CAUSE, addr);
        gen_set_label(label);
        tcg_temp_free(tmp);
    }
}

#ifndef CONFIG_USER_ONLY
static void gen_waiti(DisasContext *dc, uint32_t imm4)
{
    TCGv_i32 pc = tcg_const_i32(dc->base.pc_next);
    TCGv_i32 intlevel = tcg_const_i32(imm4);

    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_waiti(cpu_env, pc, intlevel);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_end();
    }
    tcg_temp_free(pc);
    tcg_temp_free(intlevel);
}
#endif

static bool gen_window_check(DisasContext *dc, uint32_t mask)
{
    unsigned r = 31 - clz32(mask);

    if (r / 4 > dc->window) {
        TCGv_i32 pc = tcg_const_i32(dc->pc);
        TCGv_i32 w = tcg_const_i32(r / 4);

        gen_helper_window_check(cpu_env, pc, w);
        dc->base.is_jmp = DISAS_NORETURN;
        return false;
    }
    return true;
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

static void gen_zero_check(DisasContext *dc, const uint32_t arg[])
{
    TCGLabel *label = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[arg[2]], 0, label);
    gen_exception_cause(dc, INTEGER_DIVIDE_BY_ZERO_CAUSE);
    gen_set_label(label);
}

static inline unsigned xtensa_op0_insn_len(DisasContext *dc, uint8_t op0)
{
    return xtensa_isa_length_from_chars(dc->config->isa, &op0);
}

static void disas_xtensa_insn(CPUXtensaState *env, DisasContext *dc)
{
    xtensa_isa isa = dc->config->isa;
    unsigned char b[MAX_INSN_LENGTH] = {cpu_ldub_code(env, dc->pc)};
    unsigned len = xtensa_op0_insn_len(dc, b[0]);
    xtensa_format fmt;
    int slot, slots;
    unsigned i;
    uint32_t op_flags = 0;
    struct {
        XtensaOpcodeOps *ops;
        uint32_t arg[MAX_OPCODE_ARGS];
        uint32_t raw_arg[MAX_OPCODE_ARGS];
    } slot_prop[MAX_INSN_SLOTS];
    uint32_t debug_cause = 0;
    uint32_t windowed_register = 0;
    uint32_t coprocessor = 0;

    if (len == XTENSA_UNDEFINED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown instruction length (pc = %08x)\n",
                      dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
        return;
    }

    dc->base.pc_next = dc->pc + len;
    if (xtensa_option_enabled(dc->config, XTENSA_OPTION_LOOP) &&
        dc->lbeg == dc->pc &&
        ((dc->pc ^ (dc->base.pc_next - 1)) & -dc->config->inst_fetch_width)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unaligned first instruction of a loop (pc = %08x)\n",
                      dc->pc);
    }
    for (i = 1; i < len; ++i) {
        b[i] = cpu_ldub_code(env, dc->pc + i);
    }
    xtensa_insnbuf_from_chars(isa, dc->insnbuf, b, len);
    fmt = xtensa_format_decode(isa, dc->insnbuf);
    if (fmt == XTENSA_UNDEFINED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unrecognized instruction format (pc = %08x)\n",
                      dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
        return;
    }
    slots = xtensa_format_num_slots(isa, fmt);
    for (slot = 0; slot < slots; ++slot) {
        xtensa_opcode opc;
        int opnd, vopnd, opnds;
        uint32_t *raw_arg = slot_prop[slot].raw_arg;
        uint32_t *arg = slot_prop[slot].arg;
        XtensaOpcodeOps *ops;

        dc->raw_arg = raw_arg;

        xtensa_format_get_slot(isa, fmt, slot, dc->insnbuf, dc->slotbuf);
        opc = xtensa_opcode_decode(isa, fmt, slot, dc->slotbuf);
        if (opc == XTENSA_UNDEFINED) {
            qemu_log_mask(LOG_GUEST_ERROR,
                          "unrecognized opcode in slot %d (pc = %08x)\n",
                          slot, dc->pc);
            gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
            return;
        }
        opnds = xtensa_opcode_num_operands(isa, opc);

        for (opnd = vopnd = 0; opnd < opnds; ++opnd) {
            if (xtensa_operand_is_visible(isa, opc, opnd)) {
                uint32_t v;

                xtensa_operand_get_field(isa, opc, opnd, fmt, slot,
                                         dc->slotbuf, &v);
                xtensa_operand_decode(isa, opc, opnd, &v);
                raw_arg[vopnd] = v;
                if (xtensa_operand_is_PCrelative(isa, opc, opnd)) {
                    xtensa_operand_undo_reloc(isa, opc, opnd, &v, dc->pc);
                }
                arg[vopnd] = v;
                ++vopnd;
            }
        }
        ops = dc->config->opcode_ops[opc];
        slot_prop[slot].ops = ops;

        if (ops) {
            op_flags |= ops->op_flags;
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "unimplemented opcode '%s' in slot %d (pc = %08x)\n",
                          xtensa_opcode_name(isa, opc), slot, dc->pc);
            op_flags |= XTENSA_OP_ILL;
        }
        if ((op_flags & XTENSA_OP_ILL) ||
            (ops && ops->test_ill && ops->test_ill(dc, arg, ops->par))) {
            gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
            return;
        }
        if (ops->op_flags & XTENSA_OP_DEBUG_BREAK) {
            debug_cause |= ops->par[0];
        }
        if (ops->test_overflow) {
            windowed_register |= ops->test_overflow(dc, arg, ops->par);
        }
        if (ops->windowed_register_op) {
            uint32_t reg_opnd = ops->windowed_register_op;

            while (reg_opnd) {
                unsigned i = ctz32(reg_opnd);

                windowed_register |= 1 << arg[i];
                reg_opnd ^= 1 << i;
            }
        }
        coprocessor |= ops->coprocessor;
    }

    if ((op_flags & XTENSA_OP_PRIVILEGED) &&
        !gen_check_privilege(dc)) {
        return;
    }

    if (op_flags & XTENSA_OP_SYSCALL) {
        gen_exception_cause(dc, SYSCALL_CAUSE);
        return;
    }

    if ((op_flags & XTENSA_OP_DEBUG_BREAK) && dc->debug) {
        gen_debug_exception(dc, debug_cause);
        return;
    }

    if (windowed_register && !gen_window_check(dc, windowed_register)) {
        return;
    }

    if (op_flags & XTENSA_OP_UNDERFLOW) {
        TCGv_i32 tmp = tcg_const_i32(dc->pc);

        gen_helper_test_underflow_retw(cpu_env, tmp);
        tcg_temp_free(tmp);
    }

    if (op_flags & XTENSA_OP_ALLOCA) {
        TCGv_i32 tmp = tcg_const_i32(dc->pc);

        gen_helper_movsp(cpu_env, tmp);
        tcg_temp_free(tmp);
    }

    if (coprocessor && !gen_check_cpenable(dc, coprocessor)) {
        return;
    }

    if (op_flags & XTENSA_OP_DIVIDE_BY_ZERO) {
        for (slot = 0; slot < slots; ++slot) {
            if (slot_prop[slot].ops->op_flags & XTENSA_OP_DIVIDE_BY_ZERO) {
                gen_zero_check(dc, slot_prop[slot].arg);
            }
        }
    }

    for (slot = 0; slot < slots; ++slot) {
        XtensaOpcodeOps *ops = slot_prop[slot].ops;

        dc->raw_arg = slot_prop[slot].raw_arg;
        ops->translate(dc, slot_prop[slot].arg, ops->par);
    }

    if (dc->base.is_jmp == DISAS_NEXT) {
        if (op_flags & XTENSA_OP_CHECK_INTERRUPTS) {
            gen_check_interrupts(dc);
        }

        if (op_flags & XTENSA_OP_EXIT_TB_M1) {
            /* Change in mmu index, memory mapping or tb->flags; exit tb */
            gen_jumpi_check_loop_end(dc, -1);
        } else if (op_flags & XTENSA_OP_EXIT_TB_0) {
            gen_jumpi_check_loop_end(dc, 0);
        }
    }

    if (dc->base.is_jmp == DISAS_NEXT) {
        gen_check_loop_end(dc, 0);
    }
    dc->pc = dc->base.pc_next;
}

static inline unsigned xtensa_insn_len(CPUXtensaState *env, DisasContext *dc)
{
    uint8_t b0 = cpu_ldub_code(env, dc->pc);
    return xtensa_op0_insn_len(dc, b0);
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

static void xtensa_tr_init_disas_context(DisasContextBase *dcbase,
                                         CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUXtensaState *env = cpu->env_ptr;
    uint32_t tb_flags = dc->base.tb->flags;

    dc->config = env->config;
    dc->pc = dc->base.pc_first;
    dc->ring = tb_flags & XTENSA_TBFLAG_RING_MASK;
    dc->cring = (tb_flags & XTENSA_TBFLAG_EXCM) ? 0 : dc->ring;
    dc->lbeg = env->sregs[LBEG];
    dc->lend = env->sregs[LEND];
    dc->debug = tb_flags & XTENSA_TBFLAG_DEBUG;
    dc->icount = tb_flags & XTENSA_TBFLAG_ICOUNT;
    dc->cpenable = (tb_flags & XTENSA_TBFLAG_CPENABLE_MASK) >>
        XTENSA_TBFLAG_CPENABLE_SHIFT;
    dc->window = ((tb_flags & XTENSA_TBFLAG_WINDOW_MASK) >>
                 XTENSA_TBFLAG_WINDOW_SHIFT);
    dc->cwoe = tb_flags & XTENSA_TBFLAG_CWOE;
    dc->callinc = ((tb_flags & XTENSA_TBFLAG_CALLINC_MASK) >>
                   XTENSA_TBFLAG_CALLINC_SHIFT);

    if (dc->config->isa) {
        dc->insnbuf = xtensa_insnbuf_alloc(dc->config->isa);
        dc->slotbuf = xtensa_insnbuf_alloc(dc->config->isa);
    }
    init_sar_tracker(dc);
}

static void xtensa_tr_tb_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (dc->icount) {
        dc->next_icount = tcg_temp_local_new_i32();
    }
}

static void xtensa_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    tcg_gen_insn_start(dcbase->pc_next);
}

static bool xtensa_tr_breakpoint_check(DisasContextBase *dcbase, CPUState *cpu,
                                       const CPUBreakpoint *bp)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    tcg_gen_movi_i32(cpu_pc, dc->base.pc_next);
    gen_exception(dc, EXCP_DEBUG);
    dc->base.is_jmp = DISAS_NORETURN;
    /* The address covered by the breakpoint must be included in
       [tb->pc, tb->pc + tb->size) in order to for it to be
       properly cleared -- thus we increment the PC here so that
       the logic setting tb->size below does the right thing.  */
    dc->base.pc_next += 2;
    return true;
}

static void xtensa_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUXtensaState *env = cpu->env_ptr;
    target_ulong page_start;

    /* These two conditions only apply to the first insn in the TB,
       but this is the first TranslateOps hook that allows exiting.  */
    if ((tb_cflags(dc->base.tb) & CF_USE_ICOUNT)
        && (dc->base.tb->flags & XTENSA_TBFLAG_YIELD)) {
        gen_exception(dc, EXCP_YIELD);
        dc->base.is_jmp = DISAS_NORETURN;
        return;
    }
    if (dc->base.tb->flags & XTENSA_TBFLAG_EXCEPTION) {
        gen_exception(dc, EXCP_DEBUG);
        dc->base.is_jmp = DISAS_NORETURN;
        return;
    }

    if (dc->icount) {
        TCGLabel *label = gen_new_label();

        tcg_gen_addi_i32(dc->next_icount, cpu_SR[ICOUNT], 1);
        tcg_gen_brcondi_i32(TCG_COND_NE, dc->next_icount, 0, label);
        tcg_gen_mov_i32(dc->next_icount, cpu_SR[ICOUNT]);
        if (dc->debug) {
            gen_debug_exception(dc, DEBUGCAUSE_IC);
        }
        gen_set_label(label);
    }

    if (dc->debug) {
        gen_ibreak_check(env, dc);
    }

    disas_xtensa_insn(env, dc);

    if (dc->icount) {
        tcg_gen_mov_i32(cpu_SR[ICOUNT], dc->next_icount);
    }

    /* End the TB if the next insn will cross into the next page.  */
    page_start = dc->base.pc_first & TARGET_PAGE_MASK;
    if (dc->base.is_jmp == DISAS_NEXT &&
        (dc->pc - page_start >= TARGET_PAGE_SIZE ||
         dc->pc - page_start + xtensa_insn_len(env, dc) > TARGET_PAGE_SIZE)) {
        dc->base.is_jmp = DISAS_TOO_MANY;
    }
}

static void xtensa_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    reset_sar_tracker(dc);
    if (dc->config->isa) {
        xtensa_insnbuf_free(dc->config->isa, dc->insnbuf);
        xtensa_insnbuf_free(dc->config->isa, dc->slotbuf);
    }
    if (dc->icount) {
        tcg_temp_free(dc->next_icount);
    }

    switch (dc->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        if (dc->base.singlestep_enabled) {
            tcg_gen_movi_i32(cpu_pc, dc->pc);
            gen_exception(dc, EXCP_DEBUG);
        } else {
            gen_jumpi(dc, dc->pc, 0);
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static void xtensa_tr_disas_log(const DisasContextBase *dcbase, CPUState *cpu)
{
    qemu_log("IN: %s\n", lookup_symbol(dcbase->pc_first));
    log_target_disas(cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps xtensa_translator_ops = {
    .init_disas_context = xtensa_tr_init_disas_context,
    .tb_start           = xtensa_tr_tb_start,
    .insn_start         = xtensa_tr_insn_start,
    .breakpoint_check   = xtensa_tr_breakpoint_check,
    .translate_insn     = xtensa_tr_translate_insn,
    .tb_stop            = xtensa_tr_tb_stop,
    .disas_log          = xtensa_tr_disas_log,
};

void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb)
{
    DisasContext dc = {};
    translator_loop(&xtensa_translator_ops, &dc.base, cpu, tb);
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

    xtensa_sync_phys_from_window(env);
    cpu_fprintf(f, "\n");

    for (i = 0; i < env->config->nareg; ++i) {
        cpu_fprintf(f, "AR%02d=%08x ", i, env->phys_regs[i]);
        if (i % 4 == 3) {
            bool ws = (env->sregs[WINDOW_START] & (1 << (i / 4))) != 0;
            bool cw = env->sregs[WINDOW_BASE] == i / 4;

            cpu_fprintf(f, "%c%c\n", ws ? '<' : ' ', cw ? '=' : ' ');
        }
    }

    if ((flags & CPU_DUMP_FPU) &&
        xtensa_option_enabled(env->config, XTENSA_OPTION_FP_COPROCESSOR)) {
        cpu_fprintf(f, "\n");

        for (i = 0; i < 16; ++i) {
            cpu_fprintf(f, "F%02d=%08x (%+10.8e)%c", i,
                    float32_val(env->fregs[i].f32[FP_F32_LOW]),
                    *(float *)(env->fregs[i].f32 + FP_F32_LOW),
                    (i % 2) == 1 ? '\n' : ' ');
        }
    }
}

void restore_state_to_opc(CPUXtensaState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}

static int compare_opcode_ops(const void *a, const void *b)
{
    return strcmp((const char *)a,
                  ((const XtensaOpcodeOps *)b)->name);
}

XtensaOpcodeOps *
xtensa_find_opcode_ops(const XtensaOpcodeTranslators *t,
                       const char *name)
{
    return bsearch(name, t->opcode, t->num_opcodes,
                   sizeof(XtensaOpcodeOps), compare_opcode_ops);
}

static void translate_abs(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 neg = tcg_temp_new_i32();

    tcg_gen_neg_i32(neg, cpu_R[arg[1]]);
    tcg_gen_movcond_i32(TCG_COND_GE, cpu_R[arg[0]],
                        cpu_R[arg[1]], zero, cpu_R[arg[1]], neg);
    tcg_temp_free(neg);
    tcg_temp_free(zero);
}

static void translate_add(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_add_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_addi(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_addi_i32(cpu_R[arg[0]], cpu_R[arg[1]], arg[2]);
}

static void translate_addx(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, cpu_R[arg[1]], par[0]);
    tcg_gen_add_i32(cpu_R[arg[0]], tmp, cpu_R[arg[2]]);
    tcg_temp_free(tmp);
}

static void translate_all(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    uint32_t shift = par[1];
    TCGv_i32 mask = tcg_const_i32(((1 << shift) - 1) << arg[1]);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_and_i32(tmp, cpu_SR[BR], mask);
    if (par[0]) {
        tcg_gen_addi_i32(tmp, tmp, 1 << arg[1]);
    } else {
        tcg_gen_add_i32(tmp, tmp, mask);
    }
    tcg_gen_shri_i32(tmp, tmp, arg[1] + shift);
    tcg_gen_deposit_i32(cpu_SR[BR], cpu_SR[BR],
                        tmp, arg[0], 1);
    tcg_temp_free(mask);
    tcg_temp_free(tmp);
}

static void translate_and(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_and_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_ball(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_R[arg[0]], cpu_R[arg[1]]);
    gen_brcond(dc, par[0], tmp, cpu_R[arg[1]], arg[2]);
    tcg_temp_free(tmp);
}

static void translate_bany(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, cpu_R[arg[0]], cpu_R[arg[1]]);
    gen_brcondi(dc, par[0], tmp, 0, arg[2]);
    tcg_temp_free(tmp);
}

static void translate_b(DisasContext *dc, const uint32_t arg[],
                        const uint32_t par[])
{
    gen_brcond(dc, par[0], cpu_R[arg[0]], cpu_R[arg[1]], arg[2]);
}

static void translate_bb(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
#ifdef TARGET_WORDS_BIGENDIAN
    TCGv_i32 bit = tcg_const_i32(0x80000000u);
#else
    TCGv_i32 bit = tcg_const_i32(0x00000001u);
#endif
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_andi_i32(tmp, cpu_R[arg[1]], 0x1f);
#ifdef TARGET_WORDS_BIGENDIAN
    tcg_gen_shr_i32(bit, bit, tmp);
#else
    tcg_gen_shl_i32(bit, bit, tmp);
#endif
    tcg_gen_and_i32(tmp, cpu_R[arg[0]], bit);
    gen_brcondi(dc, par[0], tmp, 0, arg[2]);
    tcg_temp_free(tmp);
    tcg_temp_free(bit);
}

static void translate_bbi(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
#ifdef TARGET_WORDS_BIGENDIAN
    tcg_gen_andi_i32(tmp, cpu_R[arg[0]], 0x80000000u >> arg[1]);
#else
    tcg_gen_andi_i32(tmp, cpu_R[arg[0]], 0x00000001u << arg[1]);
#endif
    gen_brcondi(dc, par[0], tmp, 0, arg[2]);
    tcg_temp_free(tmp);
}

static void translate_bi(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    gen_brcondi(dc, par[0], cpu_R[arg[0]], arg[1], arg[2]);
}

static void translate_bz(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    gen_brcondi(dc, par[0], cpu_R[arg[0]], 0, arg[1]);
}

enum {
    BOOLEAN_AND,
    BOOLEAN_ANDC,
    BOOLEAN_OR,
    BOOLEAN_ORC,
    BOOLEAN_XOR,
};

static void translate_boolean(DisasContext *dc, const uint32_t arg[],
                              const uint32_t par[])
{
    static void (* const op[])(TCGv_i32, TCGv_i32, TCGv_i32) = {
        [BOOLEAN_AND] = tcg_gen_and_i32,
        [BOOLEAN_ANDC] = tcg_gen_andc_i32,
        [BOOLEAN_OR] = tcg_gen_or_i32,
        [BOOLEAN_ORC] = tcg_gen_orc_i32,
        [BOOLEAN_XOR] = tcg_gen_xor_i32,
    };

    TCGv_i32 tmp1 = tcg_temp_new_i32();
    TCGv_i32 tmp2 = tcg_temp_new_i32();

    tcg_gen_shri_i32(tmp1, cpu_SR[BR], arg[1]);
    tcg_gen_shri_i32(tmp2, cpu_SR[BR], arg[2]);
    op[par[0]](tmp1, tmp1, tmp2);
    tcg_gen_deposit_i32(cpu_SR[BR], cpu_SR[BR], tmp1, arg[0], 1);
    tcg_temp_free(tmp1);
    tcg_temp_free(tmp2);
}

static void translate_bp(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, cpu_SR[BR], 1 << arg[0]);
    gen_brcondi(dc, par[0], tmp, 0, arg[1]);
    tcg_temp_free(tmp);
}

static void translate_call0(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    tcg_gen_movi_i32(cpu_R[0], dc->base.pc_next);
    gen_jumpi(dc, arg[0], 0);
}

static uint32_t test_overflow_callw(DisasContext *dc, const uint32_t arg[],
                                    const uint32_t par[])
{
    return 1 << (par[0] * 4);
}

static void translate_callw(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    gen_callwi(dc, par[0], arg[0], 0);
}

static void translate_callx0(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_mov_i32(tmp, cpu_R[arg[0]]);
    tcg_gen_movi_i32(cpu_R[0], dc->base.pc_next);
    gen_jump(dc, tmp);
    tcg_temp_free(tmp);
}

static void translate_callxw(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_mov_i32(tmp, cpu_R[arg[0]]);
    gen_callw(dc, par[0], tmp);
    tcg_temp_free(tmp);
}

static void translate_clamps(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp1 = tcg_const_i32(-1u << arg[2]);
    TCGv_i32 tmp2 = tcg_const_i32((1 << arg[2]) - 1);

    tcg_gen_smax_i32(tmp1, tmp1, cpu_R[arg[1]]);
    tcg_gen_smin_i32(cpu_R[arg[0]], tmp1, tmp2);
    tcg_temp_free(tmp1);
    tcg_temp_free(tmp2);
}

static void translate_clrb_expstate(DisasContext *dc, const uint32_t arg[],
                                    const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_andi_i32(cpu_UR[EXPSTATE], cpu_UR[EXPSTATE], ~(1u << arg[0]));
}

static void translate_const16(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 c = tcg_const_i32(arg[1]);

    tcg_gen_deposit_i32(cpu_R[arg[0]], c, cpu_R[arg[0]], 16, 16);
    tcg_temp_free(c);
}

static void translate_dcache(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 res = tcg_temp_new_i32();

    tcg_gen_addi_i32(addr, cpu_R[arg[0]], arg[1]);
    tcg_gen_qemu_ld8u(res, addr, dc->cring);
    tcg_temp_free(addr);
    tcg_temp_free(res);
}

static void translate_depbits(DisasContext *dc, const uint32_t arg[],
                              const uint32_t par[])
{
    tcg_gen_deposit_i32(cpu_R[arg[1]], cpu_R[arg[1]], cpu_R[arg[0]],
                        arg[2], arg[3]);
}

static bool test_ill_entry(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    if (arg[0] > 3 || !dc->cwoe) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Illegal entry instruction(pc = %08x)\n", dc->pc);
        return true;
    } else {
        return false;
    }
}

static uint32_t test_overflow_entry(DisasContext *dc, const uint32_t arg[],
                                    const uint32_t par[])
{
    return 1 << (dc->callinc * 4);
}

static void translate_entry(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    TCGv_i32 pc = tcg_const_i32(dc->pc);
    TCGv_i32 s = tcg_const_i32(arg[0]);
    TCGv_i32 imm = tcg_const_i32(arg[1]);
    gen_helper_entry(cpu_env, pc, s, imm);
    tcg_temp_free(imm);
    tcg_temp_free(s);
    tcg_temp_free(pc);
}

static void translate_extui(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    int maskimm = (1 << arg[3]) - 1;

    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shri_i32(tmp, cpu_R[arg[1]], arg[2]);
    tcg_gen_andi_i32(cpu_R[arg[0]], tmp, maskimm);
    tcg_temp_free(tmp);
}

static void translate_icache(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_movi_i32(cpu_pc, dc->pc);
    tcg_gen_addi_i32(addr, cpu_R[arg[0]], arg[1]);
    gen_helper_itlb_hit_test(cpu_env, addr);
    tcg_temp_free(addr);
#endif
}

static void translate_itlb(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 dtlb = tcg_const_i32(par[0]);

    gen_helper_itlb(cpu_env, cpu_R[arg[0]], dtlb);
    tcg_temp_free(dtlb);
#endif
}

static void translate_j(DisasContext *dc, const uint32_t arg[],
                        const uint32_t par[])
{
    gen_jumpi(dc, arg[0], 0);
}

static void translate_jx(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    gen_jump(dc, cpu_R[arg[0]]);
}

static void translate_l32e(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_addi_i32(addr, cpu_R[arg[1]], arg[2]);
    gen_load_store_alignment(dc, 2, addr, false);
    tcg_gen_qemu_ld_tl(cpu_R[arg[0]], addr, dc->ring, MO_TEUL);
    tcg_temp_free(addr);
}

static void translate_ldst(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_addi_i32(addr, cpu_R[arg[1]], arg[2]);
    if (par[0] & MO_SIZE) {
        gen_load_store_alignment(dc, par[0] & MO_SIZE, addr, par[1]);
    }
    if (par[2]) {
        if (par[1]) {
            tcg_gen_mb(TCG_BAR_STRL | TCG_MO_ALL);
        }
        tcg_gen_qemu_st_tl(cpu_R[arg[0]], addr, dc->cring, par[0]);
    } else {
        tcg_gen_qemu_ld_tl(cpu_R[arg[0]], addr, dc->cring, par[0]);
        if (par[1]) {
            tcg_gen_mb(TCG_BAR_LDAQ | TCG_MO_ALL);
        }
    }
    tcg_temp_free(addr);
}

static void translate_l32r(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp;

    if (dc->base.tb->flags & XTENSA_TBFLAG_LITBASE) {
        tmp = tcg_const_i32(dc->raw_arg[1] - 1);
        tcg_gen_add_i32(tmp, cpu_SR[LITBASE], tmp);
    } else {
        tmp = tcg_const_i32(arg[1]);
    }
    tcg_gen_qemu_ld32u(cpu_R[arg[0]], tmp, dc->cring);
    tcg_temp_free(tmp);
}

static void translate_loop(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    uint32_t lend = arg[1];
    TCGv_i32 tmp = tcg_const_i32(lend);

    tcg_gen_subi_i32(cpu_SR[LCOUNT], cpu_R[arg[0]], 1);
    tcg_gen_movi_i32(cpu_SR[LBEG], dc->base.pc_next);
    gen_helper_wsr_lend(cpu_env, tmp);
    tcg_temp_free(tmp);

    if (par[0] != TCG_COND_NEVER) {
        TCGLabel *label = gen_new_label();
        tcg_gen_brcondi_i32(par[0], cpu_R[arg[0]], 0, label);
        gen_jumpi(dc, lend, 1);
        gen_set_label(label);
    }

    gen_jumpi(dc, dc->base.pc_next, 0);
}

enum {
    MAC16_UMUL,
    MAC16_MUL,
    MAC16_MULA,
    MAC16_MULS,
    MAC16_NONE,
};

enum {
    MAC16_LL,
    MAC16_HL,
    MAC16_LH,
    MAC16_HH,

    MAC16_HX = 0x1,
    MAC16_XH = 0x2,
};

enum {
    MAC16_AA,
    MAC16_AD,
    MAC16_DA,
    MAC16_DD,

    MAC16_XD = 0x1,
    MAC16_DX = 0x2,
};

static void translate_mac16(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    int op = par[0];
    bool is_m1_sr = par[1] & MAC16_DX;
    bool is_m2_sr = par[1] & MAC16_XD;
    unsigned half = par[2];
    uint32_t ld_offset = par[3];
    unsigned off = ld_offset ? 2 : 0;
    TCGv_i32 vaddr = tcg_temp_new_i32();
    TCGv_i32 mem32 = tcg_temp_new_i32();

    if (ld_offset) {
        tcg_gen_addi_i32(vaddr, cpu_R[arg[1]], ld_offset);
        gen_load_store_alignment(dc, 2, vaddr, false);
        tcg_gen_qemu_ld32u(mem32, vaddr, dc->cring);
    }
    if (op != MAC16_NONE) {
        TCGv_i32 m1 = gen_mac16_m(is_m1_sr ?
                                  cpu_SR[MR + arg[off]] :
                                  cpu_R[arg[off]],
                                  half & MAC16_HX, op == MAC16_UMUL);
        TCGv_i32 m2 = gen_mac16_m(is_m2_sr ?
                                  cpu_SR[MR + arg[off + 1]] :
                                  cpu_R[arg[off + 1]],
                                  half & MAC16_XH, op == MAC16_UMUL);

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
        tcg_gen_mov_i32(cpu_R[arg[1]], vaddr);
        tcg_gen_mov_i32(cpu_SR[MR + arg[0]], mem32);
    }
    tcg_temp_free(vaddr);
    tcg_temp_free(mem32);
}

static void translate_memw(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
}

static void translate_smin(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_smin_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_umin(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_umin_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_smax(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_smax_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_umax(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_umax_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_mov(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_R[arg[0]], cpu_R[arg[1]]);
}

static void translate_movcond(DisasContext *dc, const uint32_t arg[],
                              const uint32_t par[])
{
    TCGv_i32 zero = tcg_const_i32(0);

    tcg_gen_movcond_i32(par[0], cpu_R[arg[0]],
                        cpu_R[arg[2]], zero, cpu_R[arg[1]], cpu_R[arg[0]]);
    tcg_temp_free(zero);
}

static void translate_movi(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_movi_i32(cpu_R[arg[0]], arg[1]);
}

static void translate_movp(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, cpu_SR[BR], 1 << arg[2]);
    tcg_gen_movcond_i32(par[0],
                        cpu_R[arg[0]], tmp, zero,
                        cpu_R[arg[1]], cpu_R[arg[0]]);
    tcg_temp_free(tmp);
    tcg_temp_free(zero);
}

static void translate_movsp(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_R[arg[0]], cpu_R[arg[1]]);
}

static void translate_mul16(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    TCGv_i32 v1 = tcg_temp_new_i32();
    TCGv_i32 v2 = tcg_temp_new_i32();

    if (par[0]) {
        tcg_gen_ext16s_i32(v1, cpu_R[arg[1]]);
        tcg_gen_ext16s_i32(v2, cpu_R[arg[2]]);
    } else {
        tcg_gen_ext16u_i32(v1, cpu_R[arg[1]]);
        tcg_gen_ext16u_i32(v2, cpu_R[arg[2]]);
    }
    tcg_gen_mul_i32(cpu_R[arg[0]], v1, v2);
    tcg_temp_free(v2);
    tcg_temp_free(v1);
}

static void translate_mull(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_mul_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_mulh(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 lo = tcg_temp_new();

    if (par[0]) {
        tcg_gen_muls2_i32(lo, cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
    } else {
        tcg_gen_mulu2_i32(lo, cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
    }
    tcg_temp_free(lo);
}

static void translate_neg(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_neg_i32(cpu_R[arg[0]], cpu_R[arg[1]]);
}

static void translate_nop(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
}

static void translate_nsa(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_clrsb_i32(cpu_R[arg[0]], cpu_R[arg[1]]);
}

static void translate_nsau(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_clzi_i32(cpu_R[arg[0]], cpu_R[arg[1]], 32);
}

static void translate_or(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    tcg_gen_or_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_ptlb(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 dtlb = tcg_const_i32(par[0]);

    tcg_gen_movi_i32(cpu_pc, dc->pc);
    gen_helper_ptlb(cpu_R[arg[0]], cpu_env, cpu_R[arg[1]], dtlb);
    tcg_temp_free(dtlb);
#endif
}

static void translate_quos(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGLabel *label1 = gen_new_label();
    TCGLabel *label2 = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[arg[1]], 0x80000000,
                        label1);
    tcg_gen_brcondi_i32(TCG_COND_NE, cpu_R[arg[2]], 0xffffffff,
                        label1);
    tcg_gen_movi_i32(cpu_R[arg[0]],
                     par[0] ? 0x80000000 : 0);
    tcg_gen_br(label2);
    gen_set_label(label1);
    if (par[0]) {
        tcg_gen_div_i32(cpu_R[arg[0]],
                        cpu_R[arg[1]], cpu_R[arg[2]]);
    } else {
        tcg_gen_rem_i32(cpu_R[arg[0]],
                        cpu_R[arg[1]], cpu_R[arg[2]]);
    }
    gen_set_label(label2);
}

static void translate_quou(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_divu_i32(cpu_R[arg[0]],
                     cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_read_impwire(DisasContext *dc, const uint32_t arg[],
                                   const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_movi_i32(cpu_R[arg[0]], 0);
}

static void translate_remu(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_remu_i32(cpu_R[arg[0]],
                     cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_rer(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_helper_rer(cpu_R[arg[0]], cpu_env, cpu_R[arg[1]]);
}

static void translate_ret(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_jump(dc, cpu_R[0]);
}

static bool test_ill_retw(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    if (!dc->cwoe) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Illegal retw instruction(pc = %08x)\n", dc->pc);
        return true;
    } else {
        TCGv_i32 tmp = tcg_const_i32(dc->pc);

        gen_helper_test_ill_retw(cpu_env, tmp);
        tcg_temp_free(tmp);
        return false;
    }
}

static void translate_retw(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_const_i32(dc->pc);
    gen_helper_retw(tmp, cpu_env, tmp);
    gen_jump(dc, tmp);
    tcg_temp_free(tmp);
}

static void translate_rfde(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    gen_jump(dc, cpu_SR[dc->config->ndepc ? DEPC : EPC1]);
}

static void translate_rfe(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_EXCM);
    gen_jump(dc, cpu_SR[EPC1]);
}

static void translate_rfi(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_SR[PS], cpu_SR[EPS2 + arg[0] - 2]);
    gen_jump(dc, cpu_SR[EPC1 + arg[0] - 1]);
}

static void translate_rfw(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    TCGv_i32 tmp = tcg_const_i32(1);

    tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_EXCM);
    tcg_gen_shl_i32(tmp, tmp, cpu_SR[WINDOW_BASE]);

    if (par[0]) {
        tcg_gen_andc_i32(cpu_SR[WINDOW_START],
                         cpu_SR[WINDOW_START], tmp);
    } else {
        tcg_gen_or_i32(cpu_SR[WINDOW_START],
                       cpu_SR[WINDOW_START], tmp);
    }

    tcg_temp_free(tmp);
    gen_helper_restore_owb(cpu_env);
    gen_jump(dc, cpu_SR[EPC1]);
}

static void translate_rotw(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_const_i32(arg[0]);
    gen_helper_rotw(cpu_env, tmp);
    tcg_temp_free(tmp);
}

static void translate_rsil(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_R[arg[0]], cpu_SR[PS]);
    tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_INTLEVEL);
    tcg_gen_ori_i32(cpu_SR[PS], cpu_SR[PS], arg[1]);
}

static bool test_ill_rsr(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    return !check_sr(dc, par[0], SR_R);
}

static void translate_rsr(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_rsr(dc, cpu_R[arg[0]], par[0]);
}

static void translate_rtlb(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    static void (* const helper[])(TCGv_i32 r, TCGv_env env, TCGv_i32 a1,
                                   TCGv_i32 a2) = {
        gen_helper_rtlb0,
        gen_helper_rtlb1,
    };
    TCGv_i32 dtlb = tcg_const_i32(par[0]);

    helper[par[1]](cpu_R[arg[0]], cpu_env, cpu_R[arg[1]], dtlb);
    tcg_temp_free(dtlb);
#endif
}

static void translate_rur(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    if (uregnames[par[0]].name) {
        tcg_gen_mov_i32(cpu_R[arg[0]], cpu_UR[par[0]]);
    } else {
        qemu_log_mask(LOG_UNIMP, "RUR %d not implemented\n", par[0]);
    }
}

static void translate_setb_expstate(DisasContext *dc, const uint32_t arg[],
                                    const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_ori_i32(cpu_UR[EXPSTATE], cpu_UR[EXPSTATE], 1u << arg[0]);
}

#ifdef CONFIG_USER_ONLY
static void gen_check_atomctl(DisasContext *dc, TCGv_i32 addr)
{
}
#else
static void gen_check_atomctl(DisasContext *dc, TCGv_i32 addr)
{
    TCGv_i32 tpc = tcg_const_i32(dc->pc);

    gen_helper_check_atomctl(cpu_env, tpc, addr);
    tcg_temp_free(tpc);
}
#endif

static void translate_s32c1i(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_local_new_i32();
    TCGv_i32 addr = tcg_temp_local_new_i32();

    tcg_gen_mov_i32(tmp, cpu_R[arg[0]]);
    tcg_gen_addi_i32(addr, cpu_R[arg[1]], arg[2]);
    gen_load_store_alignment(dc, 2, addr, true);
    gen_check_atomctl(dc, addr);
    tcg_gen_atomic_cmpxchg_i32(cpu_R[arg[0]], addr, cpu_SR[SCOMPARE1],
                               tmp, dc->cring, MO_TEUL);
    tcg_temp_free(addr);
    tcg_temp_free(tmp);
}

static void translate_s32e(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_addi_i32(addr, cpu_R[arg[1]], arg[2]);
    gen_load_store_alignment(dc, 2, addr, false);
    tcg_gen_qemu_st_tl(cpu_R[arg[0]], addr, dc->ring, MO_TEUL);
    tcg_temp_free(addr);
}

static void translate_salt(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_setcond_i32(par[0],
                        cpu_R[arg[0]],
                        cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_sext(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    int shift = 31 - arg[2];

    if (shift == 24) {
        tcg_gen_ext8s_i32(cpu_R[arg[0]], cpu_R[arg[1]]);
    } else if (shift == 16) {
        tcg_gen_ext16s_i32(cpu_R[arg[0]], cpu_R[arg[1]]);
    } else {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_shli_i32(tmp, cpu_R[arg[1]], shift);
        tcg_gen_sari_i32(cpu_R[arg[0]], tmp, shift);
        tcg_temp_free(tmp);
    }
}

static bool test_ill_simcall(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
#ifdef CONFIG_USER_ONLY
    bool ill = true;
#else
    bool ill = !semihosting_enabled();
#endif
    if (ill) {
        qemu_log_mask(LOG_GUEST_ERROR, "SIMCALL but semihosting is disabled\n");
    }
    return ill;
}

static void translate_simcall(DisasContext *dc, const uint32_t arg[],
                              const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_simcall(cpu_env);
#endif
}

/*
 * Note: 64 bit ops are used here solely because SAR values
 * have range 0..63
 */
#define gen_shift_reg(cmd, reg) do { \
                    TCGv_i64 tmp = tcg_temp_new_i64(); \
                    tcg_gen_extu_i32_i64(tmp, reg); \
                    tcg_gen_##cmd##_i64(v, v, tmp); \
                    tcg_gen_extrl_i64_i32(cpu_R[arg[0]], v); \
                    tcg_temp_free_i64(v); \
                    tcg_temp_free_i64(tmp); \
                } while (0)

#define gen_shift(cmd) gen_shift_reg(cmd, cpu_SR[SAR])

static void translate_sll(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    if (dc->sar_m32_5bit) {
        tcg_gen_shl_i32(cpu_R[arg[0]], cpu_R[arg[1]], dc->sar_m32);
    } else {
        TCGv_i64 v = tcg_temp_new_i64();
        TCGv_i32 s = tcg_const_i32(32);
        tcg_gen_sub_i32(s, s, cpu_SR[SAR]);
        tcg_gen_andi_i32(s, s, 0x3f);
        tcg_gen_extu_i32_i64(v, cpu_R[arg[1]]);
        gen_shift_reg(shl, s);
        tcg_temp_free(s);
    }
}

static void translate_slli(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    if (arg[2] == 32) {
        qemu_log_mask(LOG_GUEST_ERROR, "slli a%d, a%d, 32 is undefined\n",
                      arg[0], arg[1]);
    }
    tcg_gen_shli_i32(cpu_R[arg[0]], cpu_R[arg[1]], arg[2] & 0x1f);
}

static void translate_sra(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    if (dc->sar_m32_5bit) {
        tcg_gen_sar_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_SR[SAR]);
    } else {
        TCGv_i64 v = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(v, cpu_R[arg[1]]);
        gen_shift(sar);
    }
}

static void translate_srai(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_sari_i32(cpu_R[arg[0]], cpu_R[arg[1]], arg[2]);
}

static void translate_src(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    TCGv_i64 v = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(v, cpu_R[arg[2]], cpu_R[arg[1]]);
    gen_shift(shr);
}

static void translate_srl(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    if (dc->sar_m32_5bit) {
        tcg_gen_shr_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_SR[SAR]);
    } else {
        TCGv_i64 v = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(v, cpu_R[arg[1]]);
        gen_shift(shr);
    }
}

#undef gen_shift
#undef gen_shift_reg

static void translate_srli(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    tcg_gen_shri_i32(cpu_R[arg[0]], cpu_R[arg[1]], arg[2]);
}

static void translate_ssa8b(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, cpu_R[arg[0]], 3);
    gen_left_shift_sar(dc, tmp);
    tcg_temp_free(tmp);
}

static void translate_ssa8l(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, cpu_R[arg[0]], 3);
    gen_right_shift_sar(dc, tmp);
    tcg_temp_free(tmp);
}

static void translate_ssai(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_const_i32(arg[0]);
    gen_right_shift_sar(dc, tmp);
    tcg_temp_free(tmp);
}

static void translate_ssl(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_left_shift_sar(dc, cpu_R[arg[0]]);
}

static void translate_ssr(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_right_shift_sar(dc, cpu_R[arg[0]]);
}

static void translate_sub(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_sub_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static void translate_subx(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, cpu_R[arg[1]], par[0]);
    tcg_gen_sub_i32(cpu_R[arg[0]], tmp, cpu_R[arg[2]]);
    tcg_temp_free(tmp);
}

static void translate_waiti(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_waiti(dc, arg[0]);
#endif
}

static void translate_wtlb(DisasContext *dc, const uint32_t arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 dtlb = tcg_const_i32(par[0]);

    gen_helper_wtlb(cpu_env, cpu_R[arg[0]], cpu_R[arg[1]], dtlb);
    tcg_temp_free(dtlb);
#endif
}

static void translate_wer(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_helper_wer(cpu_env, cpu_R[arg[0]], cpu_R[arg[1]]);
}

static void translate_wrmsk_expstate(DisasContext *dc, const uint32_t arg[],
                                     const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_and_i32(cpu_UR[EXPSTATE], cpu_R[arg[0]], cpu_R[arg[1]]);
}

static bool test_ill_wsr(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    return !check_sr(dc, par[0], SR_W);
}

static void translate_wsr(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    gen_wsr(dc, par[0], cpu_R[arg[0]]);
}

static void translate_wur(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    if (uregnames[par[0]].name) {
        gen_wur(par[0], cpu_R[arg[0]]);
    } else {
        qemu_log_mask(LOG_UNIMP, "WUR %d not implemented\n", par[0]);
    }
}

static void translate_xor(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    tcg_gen_xor_i32(cpu_R[arg[0]], cpu_R[arg[1]], cpu_R[arg[2]]);
}

static bool test_ill_xsr(DisasContext *dc, const uint32_t arg[],
                         const uint32_t par[])
{
    return !check_sr(dc, par[0], SR_X);
}

static void translate_xsr(DisasContext *dc, const uint32_t arg[],
                          const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_mov_i32(tmp, cpu_R[arg[0]]);
    gen_rsr(dc, cpu_R[arg[0]], par[0]);
    gen_wsr(dc, par[0], tmp);
    tcg_temp_free(tmp);
}

static const XtensaOpcodeOps core_ops[] = {
    {
        .name = "abs",
        .translate = translate_abs,
        .windowed_register_op = 0x3,
    }, {
        .name = "add",
        .translate = translate_add,
        .windowed_register_op = 0x7,
    }, {
        .name = "add.n",
        .translate = translate_add,
        .windowed_register_op = 0x7,
    }, {
        .name = "addi",
        .translate = translate_addi,
        .windowed_register_op = 0x3,
    }, {
        .name = "addi.n",
        .translate = translate_addi,
        .windowed_register_op = 0x3,
    }, {
        .name = "addmi",
        .translate = translate_addi,
        .windowed_register_op = 0x3,
    }, {
        .name = "addx2",
        .translate = translate_addx,
        .par = (const uint32_t[]){1},
        .windowed_register_op = 0x7,
    }, {
        .name = "addx4",
        .translate = translate_addx,
        .par = (const uint32_t[]){2},
        .windowed_register_op = 0x7,
    }, {
        .name = "addx8",
        .translate = translate_addx,
        .par = (const uint32_t[]){3},
        .windowed_register_op = 0x7,
    }, {
        .name = "all4",
        .translate = translate_all,
        .par = (const uint32_t[]){true, 4},
    }, {
        .name = "all8",
        .translate = translate_all,
        .par = (const uint32_t[]){true, 8},
    }, {
        .name = "and",
        .translate = translate_and,
        .windowed_register_op = 0x7,
    }, {
        .name = "andb",
        .translate = translate_boolean,
        .par = (const uint32_t[]){BOOLEAN_AND},
    }, {
        .name = "andbc",
        .translate = translate_boolean,
        .par = (const uint32_t[]){BOOLEAN_ANDC},
    }, {
        .name = "any4",
        .translate = translate_all,
        .par = (const uint32_t[]){false, 4},
    }, {
        .name = "any8",
        .translate = translate_all,
        .par = (const uint32_t[]){false, 8},
    }, {
        .name = "ball",
        .translate = translate_ball,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x3,
    }, {
        .name = "bany",
        .translate = translate_bany,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x3,
    }, {
        .name = "bbc",
        .translate = translate_bb,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x3,
    }, {
        .name = "bbci",
        .translate = translate_bbi,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x1,
    }, {
        .name = "bbs",
        .translate = translate_bb,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x3,
    }, {
        .name = "bbsi",
        .translate = translate_bbi,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x1,
    }, {
        .name = "beq",
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x3,
    }, {
        .name = "beqi",
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x1,
    }, {
        .name = "beqz",
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x1,
    }, {
        .name = "beqz.n",
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x1,
    }, {
        .name = "bf",
        .translate = translate_bp,
        .par = (const uint32_t[]){TCG_COND_EQ},
    }, {
        .name = "bge",
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_GE},
        .windowed_register_op = 0x3,
    }, {
        .name = "bgei",
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_GE},
        .windowed_register_op = 0x1,
    }, {
        .name = "bgeu",
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_GEU},
        .windowed_register_op = 0x3,
    }, {
        .name = "bgeui",
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_GEU},
        .windowed_register_op = 0x1,
    }, {
        .name = "bgez",
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_GE},
        .windowed_register_op = 0x1,
    }, {
        .name = "blt",
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_LT},
        .windowed_register_op = 0x3,
    }, {
        .name = "blti",
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_LT},
        .windowed_register_op = 0x1,
    }, {
        .name = "bltu",
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_LTU},
        .windowed_register_op = 0x3,
    }, {
        .name = "bltui",
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_LTU},
        .windowed_register_op = 0x1,
    }, {
        .name = "bltz",
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_LT},
        .windowed_register_op = 0x1,
    }, {
        .name = "bnall",
        .translate = translate_ball,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x3,
    }, {
        .name = "bne",
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x3,
    }, {
        .name = "bnei",
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x1,
    }, {
        .name = "bnez",
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x1,
    }, {
        .name = "bnez.n",
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x1,
    }, {
        .name = "bnone",
        .translate = translate_bany,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x3,
    }, {
        .name = "break",
        .translate = translate_nop,
        .par = (const uint32_t[]){DEBUGCAUSE_BI},
        .op_flags = XTENSA_OP_DEBUG_BREAK,
    }, {
        .name = "break.n",
        .translate = translate_nop,
        .par = (const uint32_t[]){DEBUGCAUSE_BN},
        .op_flags = XTENSA_OP_DEBUG_BREAK,
    }, {
        .name = "bt",
        .translate = translate_bp,
        .par = (const uint32_t[]){TCG_COND_NE},
    }, {
        .name = "call0",
        .translate = translate_call0,
    }, {
        .name = "call12",
        .translate = translate_callw,
        .test_overflow = test_overflow_callw,
        .par = (const uint32_t[]){3},
    }, {
        .name = "call4",
        .translate = translate_callw,
        .test_overflow = test_overflow_callw,
        .par = (const uint32_t[]){1},
    }, {
        .name = "call8",
        .translate = translate_callw,
        .test_overflow = test_overflow_callw,
        .par = (const uint32_t[]){2},
    }, {
        .name = "callx0",
        .translate = translate_callx0,
        .windowed_register_op = 0x1,
    }, {
        .name = "callx12",
        .translate = translate_callxw,
        .test_overflow = test_overflow_callw,
        .par = (const uint32_t[]){3},
        .windowed_register_op = 0x1,
    }, {
        .name = "callx4",
        .translate = translate_callxw,
        .test_overflow = test_overflow_callw,
        .par = (const uint32_t[]){1},
        .windowed_register_op = 0x1,
    }, {
        .name = "callx8",
        .translate = translate_callxw,
        .test_overflow = test_overflow_callw,
        .par = (const uint32_t[]){2},
        .windowed_register_op = 0x1,
    }, {
        .name = "clamps",
        .translate = translate_clamps,
        .windowed_register_op = 0x3,
    }, {
        .name = "clrb_expstate",
        .translate = translate_clrb_expstate,
    }, {
        .name = "const16",
        .translate = translate_const16,
        .windowed_register_op = 0x1,
    }, {
        .name = "depbits",
        .translate = translate_depbits,
        .windowed_register_op = 0x3,
    }, {
        .name = "dhi",
        .translate = translate_dcache,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "dhu",
        .translate = translate_dcache,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "dhwb",
        .translate = translate_dcache,
        .windowed_register_op = 0x1,
    }, {
        .name = "dhwbi",
        .translate = translate_dcache,
        .windowed_register_op = 0x1,
    }, {
        .name = "dii",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "diu",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "diwb",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "diwbi",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "dpfl",
        .translate = translate_dcache,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "dpfr",
        .translate = translate_nop,
        .windowed_register_op = 0x1,
    }, {
        .name = "dpfro",
        .translate = translate_nop,
        .windowed_register_op = 0x1,
    }, {
        .name = "dpfw",
        .translate = translate_nop,
        .windowed_register_op = 0x1,
    }, {
        .name = "dpfwo",
        .translate = translate_nop,
        .windowed_register_op = 0x1,
    }, {
        .name = "dsync",
        .translate = translate_nop,
    }, {
        .name = "entry",
        .translate = translate_entry,
        .test_ill = test_ill_entry,
        .test_overflow = test_overflow_entry,
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "esync",
        .translate = translate_nop,
    }, {
        .name = "excw",
        .translate = translate_nop,
    }, {
        .name = "extui",
        .translate = translate_extui,
        .windowed_register_op = 0x3,
    }, {
        .name = "extw",
        .translate = translate_memw,
    }, {
        .name = "hwwdtlba",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "hwwitlba",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "idtlb",
        .translate = translate_itlb,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "ihi",
        .translate = translate_icache,
        .windowed_register_op = 0x1,
    }, {
        .name = "ihu",
        .translate = translate_icache,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "iii",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "iitlb",
        .translate = translate_itlb,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "iiu",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "ill",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "ill.n",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "ipf",
        .translate = translate_nop,
        .windowed_register_op = 0x1,
    }, {
        .name = "ipfl",
        .translate = translate_icache,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "isync",
        .translate = translate_nop,
    }, {
        .name = "j",
        .translate = translate_j,
    }, {
        .name = "jx",
        .translate = translate_jx,
        .windowed_register_op = 0x1,
    }, {
        .name = "l16si",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TESW, false, false},
        .windowed_register_op = 0x3,
    }, {
        .name = "l16ui",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUW, false, false},
        .windowed_register_op = 0x3,
    }, {
        .name = "l32ai",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, true, false},
        .windowed_register_op = 0x3,
    }, {
        .name = "l32e",
        .translate = translate_l32e,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "l32i",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, false},
        .windowed_register_op = 0x3,
    }, {
        .name = "l32i.n",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, false},
        .windowed_register_op = 0x3,
    }, {
        .name = "l32r",
        .translate = translate_l32r,
        .windowed_register_op = 0x1,
    }, {
        .name = "l8ui",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_UB, false, false},
        .windowed_register_op = 0x3,
    }, {
        .name = "lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_NONE, 0, 0, -4},
        .windowed_register_op = 0x2,
    }, {
        .name = "ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_NONE, 0, 0, 4},
        .windowed_register_op = 0x2,
    }, {
        .name = "ldpte",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "loop",
        .translate = translate_loop,
        .par = (const uint32_t[]){TCG_COND_NEVER},
        .windowed_register_op = 0x1,
    }, {
        .name = "loopgtz",
        .translate = translate_loop,
        .par = (const uint32_t[]){TCG_COND_GT},
        .windowed_register_op = 0x1,
    }, {
        .name = "loopnez",
        .translate = translate_loop,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x1,
    }, {
        .name = "max",
        .translate = translate_smax,
        .windowed_register_op = 0x7,
    }, {
        .name = "maxu",
        .translate = translate_umax,
        .windowed_register_op = 0x7,
    }, {
        .name = "memw",
        .translate = translate_memw,
    }, {
        .name = "min",
        .translate = translate_smin,
        .windowed_register_op = 0x7,
    }, {
        .name = "minu",
        .translate = translate_umin,
        .windowed_register_op = 0x7,
    }, {
        .name = "mov",
        .translate = translate_mov,
        .windowed_register_op = 0x3,
    }, {
        .name = "mov.n",
        .translate = translate_mov,
        .windowed_register_op = 0x3,
    }, {
        .name = "moveqz",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x7,
    }, {
        .name = "movf",
        .translate = translate_movp,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x3,
    }, {
        .name = "movgez",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_GE},
        .windowed_register_op = 0x7,
    }, {
        .name = "movi",
        .translate = translate_movi,
        .windowed_register_op = 0x1,
    }, {
        .name = "movi.n",
        .translate = translate_movi,
        .windowed_register_op = 0x1,
    }, {
        .name = "movltz",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_LT},
        .windowed_register_op = 0x7,
    }, {
        .name = "movnez",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x7,
    }, {
        .name = "movsp",
        .translate = translate_movsp,
        .windowed_register_op = 0x3,
        .op_flags = XTENSA_OP_ALLOCA,
    }, {
        .name = "movt",
        .translate = translate_movp,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x3,
    }, {
        .name = "mul.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AA, MAC16_HH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mul.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AA, MAC16_HL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mul.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AA, MAC16_LH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mul.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AA, MAC16_LL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mul.ad.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AD, MAC16_HH, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mul.ad.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AD, MAC16_HL, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mul.ad.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AD, MAC16_LH, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mul.ad.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_AD, MAC16_LL, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mul.da.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DA, MAC16_HH, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mul.da.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DA, MAC16_HL, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mul.da.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DA, MAC16_LH, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mul.da.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DA, MAC16_LL, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mul.dd.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DD, MAC16_HH, 0},
    }, {
        .name = "mul.dd.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DD, MAC16_HL, 0},
    }, {
        .name = "mul.dd.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DD, MAC16_LH, 0},
    }, {
        .name = "mul.dd.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_DD, MAC16_LL, 0},
    }, {
        .name = "mul16s",
        .translate = translate_mul16,
        .par = (const uint32_t[]){true},
        .windowed_register_op = 0x7,
    }, {
        .name = "mul16u",
        .translate = translate_mul16,
        .par = (const uint32_t[]){false},
        .windowed_register_op = 0x7,
    }, {
        .name = "mula.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AA, MAC16_HH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mula.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AA, MAC16_HL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mula.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AA, MAC16_LH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mula.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AA, MAC16_LL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "mula.ad.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AD, MAC16_HH, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mula.ad.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AD, MAC16_HL, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mula.ad.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AD, MAC16_LH, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mula.ad.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_AD, MAC16_LL, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "mula.da.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_HH, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.da.hh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_HH, -4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.hh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_HH, 4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_HL, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.da.hl.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_HL, -4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.hl.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_HL, 4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_LH, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.da.lh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_LH, -4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.lh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_LH, 4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_LL, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.da.ll.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_LL, -4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.da.ll.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DA, MAC16_LL, 4},
        .windowed_register_op = 0xa,
    }, {
        .name = "mula.dd.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_HH, 0},
    }, {
        .name = "mula.dd.hh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_HH, -4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.hh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_HH, 4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_HL, 0},
    }, {
        .name = "mula.dd.hl.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_HL, -4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.hl.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_HL, 4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_LH, 0},
    }, {
        .name = "mula.dd.lh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_LH, -4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.lh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_LH, 4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_LL, 0},
    }, {
        .name = "mula.dd.ll.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_LL, -4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mula.dd.ll.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_DD, MAC16_LL, 4},
        .windowed_register_op = 0x2,
    }, {
        .name = "mull",
        .translate = translate_mull,
        .windowed_register_op = 0x7,
    }, {
        .name = "muls.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AA, MAC16_HH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "muls.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AA, MAC16_HL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "muls.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AA, MAC16_LH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "muls.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AA, MAC16_LL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "muls.ad.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AD, MAC16_HH, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "muls.ad.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AD, MAC16_HL, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "muls.ad.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AD, MAC16_LH, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "muls.ad.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_AD, MAC16_LL, 0},
        .windowed_register_op = 0x1,
    }, {
        .name = "muls.da.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DA, MAC16_HH, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "muls.da.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DA, MAC16_HL, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "muls.da.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DA, MAC16_LH, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "muls.da.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DA, MAC16_LL, 0},
        .windowed_register_op = 0x2,
    }, {
        .name = "muls.dd.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DD, MAC16_HH, 0},
    }, {
        .name = "muls.dd.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DD, MAC16_HL, 0},
    }, {
        .name = "muls.dd.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DD, MAC16_LH, 0},
    }, {
        .name = "muls.dd.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_DD, MAC16_LL, 0},
    }, {
        .name = "mulsh",
        .translate = translate_mulh,
        .par = (const uint32_t[]){true},
        .windowed_register_op = 0x7,
    }, {
        .name = "muluh",
        .translate = translate_mulh,
        .par = (const uint32_t[]){false},
        .windowed_register_op = 0x7,
    }, {
        .name = "neg",
        .translate = translate_neg,
        .windowed_register_op = 0x3,
    }, {
        .name = "nop",
        .translate = translate_nop,
    }, {
        .name = "nop.n",
        .translate = translate_nop,
    }, {
        .name = "nsa",
        .translate = translate_nsa,
        .windowed_register_op = 0x3,
    }, {
        .name = "nsau",
        .translate = translate_nsau,
        .windowed_register_op = 0x3,
    }, {
        .name = "or",
        .translate = translate_or,
        .windowed_register_op = 0x7,
    }, {
        .name = "orb",
        .translate = translate_boolean,
        .par = (const uint32_t[]){BOOLEAN_OR},
    }, {
        .name = "orbc",
        .translate = translate_boolean,
        .par = (const uint32_t[]){BOOLEAN_ORC},
    }, {
        .name = "pdtlb",
        .translate = translate_ptlb,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "pitlb",
        .translate = translate_ptlb,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "quos",
        .translate = translate_quos,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
        .windowed_register_op = 0x7,
    }, {
        .name = "quou",
        .translate = translate_quou,
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
        .windowed_register_op = 0x7,
    }, {
        .name = "rdtlb0",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){true, 0},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "rdtlb1",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){true, 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "read_impwire",
        .translate = translate_read_impwire,
        .windowed_register_op = 0x1,
    }, {
        .name = "rems",
        .translate = translate_quos,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
        .windowed_register_op = 0x7,
    }, {
        .name = "remu",
        .translate = translate_remu,
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
        .windowed_register_op = 0x7,
    }, {
        .name = "rer",
        .translate = translate_rer,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "ret",
        .translate = translate_ret,
    }, {
        .name = "ret.n",
        .translate = translate_ret,
    }, {
        .name = "retw",
        .translate = translate_retw,
        .test_ill = test_ill_retw,
        .op_flags = XTENSA_OP_UNDERFLOW,
    }, {
        .name = "retw.n",
        .translate = translate_retw,
        .test_ill = test_ill_retw,
        .op_flags = XTENSA_OP_UNDERFLOW,
    }, {
        .name = "rfdd",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "rfde",
        .translate = translate_rfde,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rfdo",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "rfe",
        .translate = translate_rfe,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "rfi",
        .translate = translate_rfi,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "rfwo",
        .translate = translate_rfw,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "rfwu",
        .translate = translate_rfw,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "ritlb0",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){false, 0},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "ritlb1",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){false, 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "rotw",
        .translate = translate_rotw,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "rsil",
        .translate = translate_rsil,
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.176",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){176},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.208",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){208},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.acchi",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){ACCHI},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.acclo",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){ACCLO},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.atomctl",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){ATOMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.br",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){BR},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.cacheattr",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CACHEATTR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ccompare0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CCOMPARE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ccompare1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CCOMPARE + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ccompare2",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CCOMPARE + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ccount",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CCOUNT},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.configid0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CONFIGID0},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.configid1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CONFIGID1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.cpenable",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){CPENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.dbreaka0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DBREAKA},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.dbreaka1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DBREAKA + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.dbreakc0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DBREAKC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.dbreakc1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DBREAKC + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ddr",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.debugcause",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DEBUGCAUSE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.depc",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DEPC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.dtlbcfg",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){DTLBCFG},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc2",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc3",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc4",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc5",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc6",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.epc7",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPC1 + 6},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.eps2",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPS2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.eps3",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPS2 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.eps4",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPS2 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.eps5",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPS2 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.eps6",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPS2 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.eps7",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EPS2 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.exccause",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCCAUSE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave2",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave3",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave4",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave5",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave6",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excsave7",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCSAVE1 + 6},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.excvaddr",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){EXCVADDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ibreaka0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){IBREAKA},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ibreaka1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){IBREAKA + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ibreakenable",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){IBREAKENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.icount",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){ICOUNT},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.icountlevel",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){ICOUNTLEVEL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.intclear",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){INTCLEAR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.intenable",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){INTENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.interrupt",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){INTSET},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.intset",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){INTSET},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.itlbcfg",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){ITLBCFG},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.lbeg",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){LBEG},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.lcount",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){LCOUNT},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.lend",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){LEND},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.litbase",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){LITBASE},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.m0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MR},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.m1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MR + 1},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.m2",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MR + 2},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.m3",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MR + 3},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.memctl",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MEMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.misc0",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MISC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.misc1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MISC + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.misc2",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MISC + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.misc3",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){MISC + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.prid",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){PRID},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ps",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){PS},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.ptevaddr",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){PTEVADDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.rasid",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){RASID},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.sar",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){SAR},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.scompare1",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){SCOMPARE1},
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.vecbase",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){VECBASE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.windowbase",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){WINDOW_BASE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsr.windowstart",
        .translate = translate_rsr,
        .test_ill = test_ill_rsr,
        .par = (const uint32_t[]){WINDOW_START},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "rsync",
        .translate = translate_nop,
    }, {
        .name = "rur.expstate",
        .translate = translate_rur,
        .par = (const uint32_t[]){EXPSTATE},
        .windowed_register_op = 0x1,
    }, {
        .name = "rur.fcr",
        .translate = translate_rur,
        .par = (const uint32_t[]){FCR},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "rur.fsr",
        .translate = translate_rur,
        .par = (const uint32_t[]){FSR},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "rur.threadptr",
        .translate = translate_rur,
        .par = (const uint32_t[]){THREADPTR},
        .windowed_register_op = 0x1,
    }, {
        .name = "s16i",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUW, false, true},
        .windowed_register_op = 0x3,
    }, {
        .name = "s32c1i",
        .translate = translate_s32c1i,
        .windowed_register_op = 0x3,
    }, {
        .name = "s32e",
        .translate = translate_s32e,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "s32i",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, true},
        .windowed_register_op = 0x3,
    }, {
        .name = "s32i.n",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, true},
        .windowed_register_op = 0x3,
    }, {
        .name = "s32nb",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, true},
        .windowed_register_op = 0x3,
    }, {
        .name = "s32ri",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, true, true},
        .windowed_register_op = 0x3,
    }, {
        .name = "s8i",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_UB, false, true},
        .windowed_register_op = 0x3,
    }, {
        .name = "salt",
        .translate = translate_salt,
        .par = (const uint32_t[]){TCG_COND_LT},
        .windowed_register_op = 0x7,
    }, {
        .name = "saltu",
        .translate = translate_salt,
        .par = (const uint32_t[]){TCG_COND_LTU},
        .windowed_register_op = 0x7,
    }, {
        .name = "setb_expstate",
        .translate = translate_setb_expstate,
    }, {
        .name = "sext",
        .translate = translate_sext,
        .windowed_register_op = 0x3,
    }, {
        .name = "simcall",
        .translate = translate_simcall,
        .test_ill = test_ill_simcall,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "sll",
        .translate = translate_sll,
        .windowed_register_op = 0x3,
    }, {
        .name = "slli",
        .translate = translate_slli,
        .windowed_register_op = 0x3,
    }, {
        .name = "sra",
        .translate = translate_sra,
        .windowed_register_op = 0x3,
    }, {
        .name = "srai",
        .translate = translate_srai,
        .windowed_register_op = 0x3,
    }, {
        .name = "src",
        .translate = translate_src,
        .windowed_register_op = 0x7,
    }, {
        .name = "srl",
        .translate = translate_srl,
        .windowed_register_op = 0x3,
    }, {
        .name = "srli",
        .translate = translate_srli,
        .windowed_register_op = 0x3,
    }, {
        .name = "ssa8b",
        .translate = translate_ssa8b,
        .windowed_register_op = 0x1,
    }, {
        .name = "ssa8l",
        .translate = translate_ssa8l,
        .windowed_register_op = 0x1,
    }, {
        .name = "ssai",
        .translate = translate_ssai,
    }, {
        .name = "ssl",
        .translate = translate_ssl,
        .windowed_register_op = 0x1,
    }, {
        .name = "ssr",
        .translate = translate_ssr,
        .windowed_register_op = 0x1,
    }, {
        .name = "sub",
        .translate = translate_sub,
        .windowed_register_op = 0x7,
    }, {
        .name = "subx2",
        .translate = translate_subx,
        .par = (const uint32_t[]){1},
        .windowed_register_op = 0x7,
    }, {
        .name = "subx4",
        .translate = translate_subx,
        .par = (const uint32_t[]){2},
        .windowed_register_op = 0x7,
    }, {
        .name = "subx8",
        .translate = translate_subx,
        .par = (const uint32_t[]){3},
        .windowed_register_op = 0x7,
    }, {
        .name = "syscall",
        .op_flags = XTENSA_OP_SYSCALL,
    }, {
        .name = "umul.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_AA, MAC16_HH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "umul.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_AA, MAC16_HL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "umul.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_AA, MAC16_LH, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "umul.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_AA, MAC16_LL, 0},
        .windowed_register_op = 0x3,
    }, {
        .name = "waiti",
        .translate = translate_waiti,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wdtlb",
        .translate = translate_wtlb,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x3,
    }, {
        .name = "wer",
        .translate = translate_wer,
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x3,
    }, {
        .name = "witlb",
        .translate = translate_wtlb,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x3,
    }, {
        .name = "wrmsk_expstate",
        .translate = translate_wrmsk_expstate,
        .windowed_register_op = 0x3,
    }, {
        .name = "wsr.176",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){176},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.208",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){208},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.acchi",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){ACCHI},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.acclo",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){ACCLO},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.atomctl",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){ATOMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.br",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){BR},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.cacheattr",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CACHEATTR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ccompare0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CCOMPARE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ccompare1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CCOMPARE + 1},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ccompare2",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CCOMPARE + 2},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ccount",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CCOUNT},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.configid0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CONFIGID0},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.configid1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CONFIGID1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.cpenable",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){CPENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.dbreaka0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DBREAKA},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.dbreaka1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DBREAKA + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.dbreakc0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DBREAKC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.dbreakc1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DBREAKC + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ddr",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.debugcause",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DEBUGCAUSE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.depc",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DEPC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.dtlbcfg",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){DTLBCFG},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc2",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc3",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc4",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc5",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc6",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.epc7",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPC1 + 6},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.eps2",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPS2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.eps3",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPS2 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.eps4",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPS2 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.eps5",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPS2 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.eps6",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPS2 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.eps7",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EPS2 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.exccause",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCCAUSE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave2",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave3",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave4",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave5",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave6",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excsave7",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCSAVE1 + 6},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.excvaddr",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){EXCVADDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ibreaka0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){IBREAKA},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ibreaka1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){IBREAKA + 1},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ibreakenable",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){IBREAKENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.icount",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){ICOUNT},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.icountlevel",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){ICOUNTLEVEL},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.intclear",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){INTCLEAR},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.intenable",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){INTENABLE},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.interrupt",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){INTSET},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.intset",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){INTSET},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.itlbcfg",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){ITLBCFG},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.lbeg",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){LBEG},
        .op_flags = XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.lcount",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){LCOUNT},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.lend",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){LEND},
        .op_flags = XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.litbase",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){LITBASE},
        .op_flags = XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.m0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MR},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.m1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MR + 1},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.m2",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MR + 2},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.m3",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MR + 3},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.memctl",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MEMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.misc0",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MISC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.misc1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MISC + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.misc2",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MISC + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.misc3",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MISC + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.mmid",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){MMID},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.prid",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){PRID},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ps",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){PS},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.ptevaddr",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){PTEVADDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.rasid",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){RASID},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.sar",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){SAR},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.scompare1",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){SCOMPARE1},
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.vecbase",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){VECBASE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.windowbase",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){WINDOW_BASE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "wsr.windowstart",
        .translate = translate_wsr,
        .test_ill = test_ill_wsr,
        .par = (const uint32_t[]){WINDOW_START},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "wur.expstate",
        .translate = translate_wur,
        .par = (const uint32_t[]){EXPSTATE},
        .windowed_register_op = 0x1,
    }, {
        .name = "wur.fcr",
        .translate = translate_wur,
        .par = (const uint32_t[]){FCR},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "wur.fsr",
        .translate = translate_wur,
        .par = (const uint32_t[]){FSR},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "wur.threadptr",
        .translate = translate_wur,
        .par = (const uint32_t[]){THREADPTR},
        .windowed_register_op = 0x1,
    }, {
        .name = "xor",
        .translate = translate_xor,
        .windowed_register_op = 0x7,
    }, {
        .name = "xorb",
        .translate = translate_boolean,
        .par = (const uint32_t[]){BOOLEAN_XOR},
    }, {
        .name = "xsr.176",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){176},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.208",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){208},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.acchi",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){ACCHI},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.acclo",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){ACCLO},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.atomctl",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){ATOMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.br",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){BR},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.cacheattr",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CACHEATTR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ccompare0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CCOMPARE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ccompare1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CCOMPARE + 1},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ccompare2",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CCOMPARE + 2},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ccount",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CCOUNT},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.configid0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CONFIGID0},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.configid1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CONFIGID1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.cpenable",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){CPENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.dbreaka0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DBREAKA},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.dbreaka1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DBREAKA + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.dbreakc0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DBREAKC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.dbreakc1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DBREAKC + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ddr",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.debugcause",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DEBUGCAUSE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.depc",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DEPC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.dtlbcfg",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){DTLBCFG},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc2",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc3",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc4",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc5",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc6",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.epc7",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPC1 + 6},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.eps2",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPS2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.eps3",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPS2 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.eps4",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPS2 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.eps5",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPS2 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.eps6",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPS2 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.eps7",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EPS2 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.exccause",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCCAUSE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave2",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1 + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave3",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1 + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave4",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1 + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave5",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1 + 4},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave6",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1 + 5},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excsave7",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCSAVE1 + 6},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.excvaddr",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){EXCVADDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ibreaka0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){IBREAKA},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ibreaka1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){IBREAKA + 1},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ibreakenable",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){IBREAKENABLE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.icount",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){ICOUNT},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.icountlevel",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){ICOUNTLEVEL},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.intclear",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){INTCLEAR},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.intenable",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){INTENABLE},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.interrupt",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){INTSET},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.intset",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){INTSET},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.itlbcfg",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){ITLBCFG},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.lbeg",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){LBEG},
        .op_flags = XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.lcount",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){LCOUNT},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.lend",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){LEND},
        .op_flags = XTENSA_OP_EXIT_TB_0,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.litbase",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){LITBASE},
        .op_flags = XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.m0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MR},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.m1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MR + 1},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.m2",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MR + 2},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.m3",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MR + 3},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.memctl",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MEMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.misc0",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MISC},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.misc1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MISC + 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.misc2",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MISC + 2},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.misc3",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){MISC + 3},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.prid",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){PRID},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ps",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){PS},
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_CHECK_INTERRUPTS,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.ptevaddr",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){PTEVADDR},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.rasid",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){RASID},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.sar",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){SAR},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.scompare1",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){SCOMPARE1},
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.vecbase",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){VECBASE},
        .op_flags = XTENSA_OP_PRIVILEGED,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.windowbase",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){WINDOW_BASE},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    }, {
        .name = "xsr.windowstart",
        .translate = translate_xsr,
        .test_ill = test_ill_xsr,
        .par = (const uint32_t[]){WINDOW_START},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
        .windowed_register_op = 0x1,
    },
};

const XtensaOpcodeTranslators xtensa_core_opcodes = {
    .num_opcodes = ARRAY_SIZE(core_ops),
    .opcode = core_ops,
};


static void translate_abs_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    gen_helper_abs_s(cpu_FR[arg[0]], cpu_FR[arg[1]]);
}

static void translate_add_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    gen_helper_add_s(cpu_FR[arg[0]], cpu_env,
                     cpu_FR[arg[1]], cpu_FR[arg[2]]);
}

enum {
    COMPARE_UN,
    COMPARE_OEQ,
    COMPARE_UEQ,
    COMPARE_OLT,
    COMPARE_ULT,
    COMPARE_OLE,
    COMPARE_ULE,
};

static void translate_compare_s(DisasContext *dc, const uint32_t arg[],
                                const uint32_t par[])
{
    static void (* const helper[])(TCGv_env env, TCGv_i32 bit,
                                   TCGv_i32 s, TCGv_i32 t) = {
        [COMPARE_UN] = gen_helper_un_s,
        [COMPARE_OEQ] = gen_helper_oeq_s,
        [COMPARE_UEQ] = gen_helper_ueq_s,
        [COMPARE_OLT] = gen_helper_olt_s,
        [COMPARE_ULT] = gen_helper_ult_s,
        [COMPARE_OLE] = gen_helper_ole_s,
        [COMPARE_ULE] = gen_helper_ule_s,
    };
    TCGv_i32 bit = tcg_const_i32(1 << arg[0]);

    helper[par[0]](cpu_env, bit, cpu_FR[arg[1]], cpu_FR[arg[2]]);
    tcg_temp_free(bit);
}

static void translate_float_s(DisasContext *dc, const uint32_t arg[],
                              const uint32_t par[])
{
    TCGv_i32 scale = tcg_const_i32(-arg[2]);

    if (par[0]) {
        gen_helper_uitof(cpu_FR[arg[0]], cpu_env, cpu_R[arg[1]], scale);
    } else {
        gen_helper_itof(cpu_FR[arg[0]], cpu_env, cpu_R[arg[1]], scale);
    }
    tcg_temp_free(scale);
}

static void translate_ftoi_s(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 rounding_mode = tcg_const_i32(par[0]);
    TCGv_i32 scale = tcg_const_i32(arg[2]);

    if (par[1]) {
        gen_helper_ftoui(cpu_R[arg[0]], cpu_FR[arg[1]],
                         rounding_mode, scale);
    } else {
        gen_helper_ftoi(cpu_R[arg[0]], cpu_FR[arg[1]],
                        rounding_mode, scale);
    }
    tcg_temp_free(rounding_mode);
    tcg_temp_free(scale);
}

static void translate_ldsti(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_addi_i32(addr, cpu_R[arg[1]], arg[2]);
    gen_load_store_alignment(dc, 2, addr, false);
    if (par[0]) {
        tcg_gen_qemu_st32(cpu_FR[arg[0]], addr, dc->cring);
    } else {
        tcg_gen_qemu_ld32u(cpu_FR[arg[0]], addr, dc->cring);
    }
    if (par[1]) {
        tcg_gen_mov_i32(cpu_R[arg[1]], addr);
    }
    tcg_temp_free(addr);
}

static void translate_ldstx(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_add_i32(addr, cpu_R[arg[1]], cpu_R[arg[2]]);
    gen_load_store_alignment(dc, 2, addr, false);
    if (par[0]) {
        tcg_gen_qemu_st32(cpu_FR[arg[0]], addr, dc->cring);
    } else {
        tcg_gen_qemu_ld32u(cpu_FR[arg[0]], addr, dc->cring);
    }
    if (par[1]) {
        tcg_gen_mov_i32(cpu_R[arg[1]], addr);
    }
    tcg_temp_free(addr);
}

static void translate_madd_s(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    gen_helper_madd_s(cpu_FR[arg[0]], cpu_env,
                      cpu_FR[arg[0]], cpu_FR[arg[1]], cpu_FR[arg[2]]);
}

static void translate_mov_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_FR[arg[0]], cpu_FR[arg[1]]);
}

static void translate_movcond_s(DisasContext *dc, const uint32_t arg[],
                                const uint32_t par[])
{
    TCGv_i32 zero = tcg_const_i32(0);

    tcg_gen_movcond_i32(par[0], cpu_FR[arg[0]],
                        cpu_R[arg[2]], zero,
                        cpu_FR[arg[1]], cpu_FR[arg[0]]);
    tcg_temp_free(zero);
}

static void translate_movp_s(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    TCGv_i32 zero = tcg_const_i32(0);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, cpu_SR[BR], 1 << arg[2]);
    tcg_gen_movcond_i32(par[0],
                        cpu_FR[arg[0]], tmp, zero,
                        cpu_FR[arg[1]], cpu_FR[arg[0]]);
    tcg_temp_free(tmp);
    tcg_temp_free(zero);
}

static void translate_mul_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    gen_helper_mul_s(cpu_FR[arg[0]], cpu_env,
                     cpu_FR[arg[1]], cpu_FR[arg[2]]);
}

static void translate_msub_s(DisasContext *dc, const uint32_t arg[],
                             const uint32_t par[])
{
    gen_helper_msub_s(cpu_FR[arg[0]], cpu_env,
                      cpu_FR[arg[0]], cpu_FR[arg[1]], cpu_FR[arg[2]]);
}

static void translate_neg_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    gen_helper_neg_s(cpu_FR[arg[0]], cpu_FR[arg[1]]);
}

static void translate_rfr_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_R[arg[0]], cpu_FR[arg[1]]);
}

static void translate_sub_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    gen_helper_sub_s(cpu_FR[arg[0]], cpu_env,
                     cpu_FR[arg[1]], cpu_FR[arg[2]]);
}

static void translate_wfr_s(DisasContext *dc, const uint32_t arg[],
                            const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_FR[arg[0]], cpu_R[arg[1]]);
}

static const XtensaOpcodeOps fpu2000_ops[] = {
    {
        .name = "abs.s",
        .translate = translate_abs_s,
        .coprocessor = 0x1,
    }, {
        .name = "add.s",
        .translate = translate_add_s,
        .coprocessor = 0x1,
    }, {
        .name = "ceil.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_up, false},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "float.s",
        .translate = translate_float_s,
        .par = (const uint32_t[]){false},
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    }, {
        .name = "floor.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_down, false},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "lsi",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){false, false},
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    }, {
        .name = "lsiu",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){false, true},
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    }, {
        .name = "lsx",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){false, false},
        .windowed_register_op = 0x6,
        .coprocessor = 0x1,
    }, {
        .name = "lsxu",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){false, true},
        .windowed_register_op = 0x6,
        .coprocessor = 0x1,
    }, {
        .name = "madd.s",
        .translate = translate_madd_s,
        .coprocessor = 0x1,
    }, {
        .name = "mov.s",
        .translate = translate_mov_s,
        .coprocessor = 0x1,
    }, {
        .name = "moveqz.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .windowed_register_op = 0x4,
        .coprocessor = 0x1,
    }, {
        .name = "movf.s",
        .translate = translate_movp_s,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .coprocessor = 0x1,
    }, {
        .name = "movgez.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_GE},
        .windowed_register_op = 0x4,
        .coprocessor = 0x1,
    }, {
        .name = "movltz.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_LT},
        .windowed_register_op = 0x4,
        .coprocessor = 0x1,
    }, {
        .name = "movnez.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_NE},
        .windowed_register_op = 0x4,
        .coprocessor = 0x1,
    }, {
        .name = "movt.s",
        .translate = translate_movp_s,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "msub.s",
        .translate = translate_msub_s,
        .coprocessor = 0x1,
    }, {
        .name = "mul.s",
        .translate = translate_mul_s,
        .coprocessor = 0x1,
    }, {
        .name = "neg.s",
        .translate = translate_neg_s,
        .coprocessor = 0x1,
    }, {
        .name = "oeq.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_OEQ},
        .coprocessor = 0x1,
    }, {
        .name = "ole.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_OLE},
        .coprocessor = 0x1,
    }, {
        .name = "olt.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_OLT},
        .coprocessor = 0x1,
    }, {
        .name = "rfr",
        .translate = translate_rfr_s,
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "round.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_nearest_even, false},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "ssi",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){true, false},
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    }, {
        .name = "ssiu",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){true, true},
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    }, {
        .name = "ssx",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){true, false},
        .windowed_register_op = 0x6,
        .coprocessor = 0x1,
    }, {
        .name = "ssxu",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){true, true},
        .windowed_register_op = 0x6,
        .coprocessor = 0x1,
    }, {
        .name = "sub.s",
        .translate = translate_sub_s,
        .coprocessor = 0x1,
    }, {
        .name = "trunc.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_to_zero, false},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "ueq.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_UEQ},
        .coprocessor = 0x1,
    }, {
        .name = "ufloat.s",
        .translate = translate_float_s,
        .par = (const uint32_t[]){true},
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    }, {
        .name = "ule.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_ULE},
        .coprocessor = 0x1,
    }, {
        .name = "ult.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_ULT},
        .coprocessor = 0x1,
    }, {
        .name = "un.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_UN},
        .coprocessor = 0x1,
    }, {
        .name = "utrunc.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_to_zero, true},
        .windowed_register_op = 0x1,
        .coprocessor = 0x1,
    }, {
        .name = "wfr",
        .translate = translate_wfr_s,
        .windowed_register_op = 0x2,
        .coprocessor = 0x1,
    },
};

const XtensaOpcodeTranslators xtensa_fpu2000_opcodes = {
    .num_opcodes = ARRAY_SIZE(fpu2000_ops),
    .opcode = fpu2000_ops,
};
