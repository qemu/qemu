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
#include "tcg/tcg-op.h"
#include "qemu/log.h"
#include "qemu/qemu-print.h"
#include "exec/cpu_ldst.h"
#include "semihosting/semihost.h"
#include "exec/translator.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"

#include "exec/log.h"


struct DisasContext {
    DisasContextBase base;
    const XtensaConfig *config;
    uint32_t pc;
    int cring;
    int ring;
    uint32_t lbeg_off;
    uint32_t lend;

    bool sar_5bit;
    bool sar_m32_5bit;
    TCGv_i32 sar_m32;

    unsigned window;
    unsigned callinc;
    bool cwoe;

    bool debug;
    bool icount;
    TCGv_i32 next_icount;

    unsigned cpenable;

    uint32_t op_flags;
    xtensa_insnbuf_word insnbuf[MAX_INSNBUF_LENGTH];
    xtensa_insnbuf_word slotbuf[MAX_INSNBUF_LENGTH];
};

static TCGv_i32 cpu_pc;
static TCGv_i32 cpu_R[16];
static TCGv_i32 cpu_FR[16];
static TCGv_i64 cpu_FRD[16];
static TCGv_i32 cpu_MR[4];
static TCGv_i32 cpu_BR[16];
static TCGv_i32 cpu_BR4[4];
static TCGv_i32 cpu_BR8[2];
static TCGv_i32 cpu_SR[256];
static TCGv_i32 cpu_UR[256];
static TCGv_i32 cpu_windowbase_next;
static TCGv_i32 cpu_exclusive_addr;
static TCGv_i32 cpu_exclusive_val;

static GHashTable *xtensa_regfile_table;

#include "exec/gen-icount.h"

static char *sr_name[256];
static char *ur_name[256];

void xtensa_collect_sr_names(const XtensaConfig *config)
{
    xtensa_isa isa = config->isa;
    int n = xtensa_isa_num_sysregs(isa);
    int i;

    for (i = 0; i < n; ++i) {
        int sr = xtensa_sysreg_number(isa, i);

        if (sr >= 0 && sr < 256) {
            const char *name = xtensa_sysreg_name(isa, i);
            char **pname =
                (xtensa_sysreg_is_user(isa, i) ? ur_name : sr_name) + sr;

            if (*pname) {
                if (strstr(*pname, name) == NULL) {
                    char *new_name =
                        malloc(strlen(*pname) + strlen(name) + 2);

                    strcpy(new_name, *pname);
                    strcat(new_name, "/");
                    strcat(new_name, name);
                    free(*pname);
                    *pname = new_name;
                }
            } else {
                *pname = strdup(name);
            }
        }
    }
}

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
    static const char * const mregnames[] = {
        "m0", "m1", "m2", "m3",
    };
    static const char * const bregnames[] = {
        "b0", "b1", "b2", "b3",
        "b4", "b5", "b6", "b7",
        "b8", "b9", "b10", "b11",
        "b12", "b13", "b14", "b15",
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
                                           offsetof(CPUXtensaState,
                                                    fregs[i].f32[FP_F32_LOW]),
                                           fregnames[i]);
    }

    for (i = 0; i < 16; i++) {
        cpu_FRD[i] = tcg_global_mem_new_i64(cpu_env,
                                            offsetof(CPUXtensaState,
                                                     fregs[i].f64),
                                            fregnames[i]);
    }

    for (i = 0; i < 4; i++) {
        cpu_MR[i] = tcg_global_mem_new_i32(cpu_env,
                                           offsetof(CPUXtensaState,
                                                    sregs[MR + i]),
                                           mregnames[i]);
    }

    for (i = 0; i < 16; i++) {
        cpu_BR[i] = tcg_global_mem_new_i32(cpu_env,
                                           offsetof(CPUXtensaState,
                                                    sregs[BR]),
                                           bregnames[i]);
        if (i % 4 == 0) {
            cpu_BR4[i / 4] = tcg_global_mem_new_i32(cpu_env,
                                                    offsetof(CPUXtensaState,
                                                             sregs[BR]),
                                                    bregnames[i]);
        }
        if (i % 8 == 0) {
            cpu_BR8[i / 8] = tcg_global_mem_new_i32(cpu_env,
                                                    offsetof(CPUXtensaState,
                                                             sregs[BR]),
                                                    bregnames[i]);
        }
    }

    for (i = 0; i < 256; ++i) {
        if (sr_name[i]) {
            cpu_SR[i] = tcg_global_mem_new_i32(cpu_env,
                                               offsetof(CPUXtensaState,
                                                        sregs[i]),
                                               sr_name[i]);
        }
    }

    for (i = 0; i < 256; ++i) {
        if (ur_name[i]) {
            cpu_UR[i] = tcg_global_mem_new_i32(cpu_env,
                                               offsetof(CPUXtensaState,
                                                        uregs[i]),
                                               ur_name[i]);
        }
    }

    cpu_windowbase_next =
        tcg_global_mem_new_i32(cpu_env,
                               offsetof(CPUXtensaState, windowbase_next),
                               "windowbase_next");
    cpu_exclusive_addr =
        tcg_global_mem_new_i32(cpu_env,
                               offsetof(CPUXtensaState, exclusive_addr),
                               "exclusive_addr");
    cpu_exclusive_val =
        tcg_global_mem_new_i32(cpu_env,
                               offsetof(CPUXtensaState, exclusive_val),
                               "exclusive_val");
}

void **xtensa_get_regfile_by_name(const char *name, int entries, int bits)
{
    char *geometry_name;
    void **res;

    if (xtensa_regfile_table == NULL) {
        xtensa_regfile_table = g_hash_table_new(g_str_hash, g_str_equal);
        /*
         * AR is special. Xtensa translator uses it as a current register
         * window, but configuration overlays represent it as a complete
         * physical register file.
         */
        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"AR 16x32", (void *)cpu_R);
        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"AR 32x32", (void *)cpu_R);
        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"AR 64x32", (void *)cpu_R);

        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"MR 4x32", (void *)cpu_MR);

        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"FR 16x32", (void *)cpu_FR);
        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"FR 16x64", (void *)cpu_FRD);

        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"BR 16x1", (void *)cpu_BR);
        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"BR4 4x4", (void *)cpu_BR4);
        g_hash_table_insert(xtensa_regfile_table,
                            (void *)"BR8 2x8", (void *)cpu_BR8);
    }

    geometry_name = g_strdup_printf("%s %dx%d", name, entries, bits);
    res = (void **)g_hash_table_lookup(xtensa_regfile_table, geometry_name);
    g_free(geometry_name);
    return res;
}

static inline bool option_enabled(DisasContext *dc, int opt)
{
    return xtensa_option_enabled(dc->config, opt);
}

static void init_sar_tracker(DisasContext *dc)
{
    dc->sar_5bit = false;
    dc->sar_m32_5bit = false;
    dc->sar_m32 = NULL;
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
    if (!dc->sar_m32) {
        dc->sar_m32 = tcg_temp_new_i32();
    }
    tcg_gen_andi_i32(dc->sar_m32, sa, 0x1f);
    tcg_gen_sub_i32(cpu_SR[SAR], tcg_constant_i32(32), dc->sar_m32);
    dc->sar_5bit = false;
    dc->sar_m32_5bit = true;
}

static void gen_exception(DisasContext *dc, int excp)
{
    gen_helper_exception(cpu_env, tcg_constant_i32(excp));
}

static void gen_exception_cause(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 pc = tcg_constant_i32(dc->pc);
    gen_helper_exception_cause(cpu_env, pc, tcg_constant_i32(cause));
    if (cause == ILLEGAL_INSTRUCTION_CAUSE ||
            cause == SYSCALL_CAUSE) {
        dc->base.is_jmp = DISAS_NORETURN;
    }
}

static void gen_debug_exception(DisasContext *dc, uint32_t cause)
{
    TCGv_i32 pc = tcg_constant_i32(dc->pc);
    gen_helper_debug_exception(cpu_env, pc, tcg_constant_i32(cause));
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

static int gen_postprocess(DisasContext *dc, int slot);

static void gen_jump_slot(DisasContext *dc, TCGv dest, int slot)
{
    tcg_gen_mov_i32(cpu_pc, dest);
    if (dc->icount) {
        tcg_gen_mov_i32(cpu_SR[ICOUNT], dc->next_icount);
    }
    if (dc->op_flags & XTENSA_OP_POSTPROCESS) {
        slot = gen_postprocess(dc, slot);
    }
    if (slot >= 0) {
        tcg_gen_goto_tb(slot);
        tcg_gen_exit_tb(dc->base.tb, slot);
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_jump(DisasContext *dc, TCGv dest)
{
    gen_jump_slot(dc, dest, -1);
}

static int adjust_jump_slot(DisasContext *dc, uint32_t dest, int slot)
{
    return translator_use_goto_tb(&dc->base, dest) ? slot : -1;
}

static void gen_jumpi(DisasContext *dc, uint32_t dest, int slot)
{
    gen_jump_slot(dc, tcg_constant_i32(dest),
                  adjust_jump_slot(dc, dest, slot));
}

static void gen_callw_slot(DisasContext *dc, int callinc, TCGv_i32 dest,
        int slot)
{
    tcg_gen_deposit_i32(cpu_SR[PS], cpu_SR[PS],
            tcg_constant_i32(callinc), PS_CALLINC_SHIFT, PS_CALLINC_LEN);
    tcg_gen_movi_i32(cpu_R[callinc << 2],
            (callinc << 30) | (dc->base.pc_next & 0x3fffffff));
    gen_jump_slot(dc, dest, slot);
}

static bool gen_check_loop_end(DisasContext *dc, int slot)
{
    if (dc->base.pc_next == dc->lend) {
        TCGLabel *label = gen_new_label();

        tcg_gen_brcondi_i32(TCG_COND_EQ, cpu_SR[LCOUNT], 0, label);
        tcg_gen_subi_i32(cpu_SR[LCOUNT], cpu_SR[LCOUNT], 1);
        if (dc->lbeg_off) {
            gen_jumpi(dc, dc->base.pc_next - dc->lbeg_off, slot);
        } else {
            gen_jump(dc, cpu_SR[LBEG]);
        }
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
    gen_brcond(dc, cond, t0, tcg_constant_i32(t1), addr);
}

static uint32_t test_exceptions_sr(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    return xtensa_option_enabled(dc->config, par[1]) ? 0 : XTENSA_OP_ILL;
}

static uint32_t test_exceptions_ccompare(DisasContext *dc,
                                         const OpcodeArg arg[],
                                         const uint32_t par[])
{
    unsigned n = par[0] - CCOMPARE;

    if (n >= dc->config->nccompare) {
        return XTENSA_OP_ILL;
    }
    return test_exceptions_sr(dc, arg, par);
}

static uint32_t test_exceptions_dbreak(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    unsigned n = MAX_NDBREAK;

    if (par[0] >= DBREAKA && par[0] < DBREAKA + MAX_NDBREAK) {
        n = par[0] - DBREAKA;
    }
    if (par[0] >= DBREAKC && par[0] < DBREAKC + MAX_NDBREAK) {
        n = par[0] - DBREAKC;
    }
    if (n >= dc->config->ndbreak) {
        return XTENSA_OP_ILL;
    }
    return test_exceptions_sr(dc, arg, par);
}

static uint32_t test_exceptions_ibreak(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
    unsigned n = par[0] - IBREAKA;

    if (n >= dc->config->nibreak) {
        return XTENSA_OP_ILL;
    }
    return test_exceptions_sr(dc, arg, par);
}

static uint32_t test_exceptions_hpi(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    unsigned n = MAX_NLEVEL + 1;

    if (par[0] >= EXCSAVE1 && par[0] < EXCSAVE1 + MAX_NLEVEL) {
        n = par[0] - EXCSAVE1 + 1;
    }
    if (par[0] >= EPC1 && par[0] < EPC1 + MAX_NLEVEL) {
        n = par[0] - EPC1 + 1;
    }
    if (par[0] >= EPS2 && par[0] < EPS2 + MAX_NLEVEL - 1) {
        n = par[0] - EPS2 + 2;
    }
    if (n > dc->config->nlevel) {
        return XTENSA_OP_ILL;
    }
    return test_exceptions_sr(dc, arg, par);
}

static MemOp gen_load_store_alignment(DisasContext *dc, MemOp mop,
                                      TCGv_i32 addr)
{
    if ((mop & MO_SIZE) == MO_8) {
        return mop;
    }
    if ((mop & MO_AMASK) == MO_UNALN &&
        !option_enabled(dc, XTENSA_OPTION_HW_ALIGNMENT)) {
        mop |= MO_ALIGN;
    }
    if (!option_enabled(dc, XTENSA_OPTION_UNALIGNED_EXCEPTION)) {
        tcg_gen_andi_i32(addr, addr, ~0 << get_alignment_bits(mop));
    }
    return mop;
}

static bool gen_window_check(DisasContext *dc, uint32_t mask)
{
    unsigned r = 31 - clz32(mask);

    if (r / 4 > dc->window) {
        TCGv_i32 pc = tcg_constant_i32(dc->pc);
        TCGv_i32 w = tcg_constant_i32(r / 4);

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

static void gen_zero_check(DisasContext *dc, const OpcodeArg arg[])
{
    TCGLabel *label = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, arg[2].in, 0, label);
    gen_exception_cause(dc, INTEGER_DIVIDE_BY_ZERO_CAUSE);
    gen_set_label(label);
}

static inline unsigned xtensa_op0_insn_len(DisasContext *dc, uint8_t op0)
{
    return xtensa_isa_length_from_chars(dc->config->isa, &op0);
}

static int gen_postprocess(DisasContext *dc, int slot)
{
    uint32_t op_flags = dc->op_flags;

#ifndef CONFIG_USER_ONLY
    if (op_flags & XTENSA_OP_CHECK_INTERRUPTS) {
        if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
            gen_io_start();
        }
        gen_helper_check_interrupts(cpu_env);
    }
#endif
    if (op_flags & XTENSA_OP_SYNC_REGISTER_WINDOW) {
        gen_helper_sync_windowbase(cpu_env);
    }
    if (op_flags & XTENSA_OP_EXIT_TB_M1) {
        slot = -1;
    }
    return slot;
}

struct opcode_arg_copy {
    uint32_t resource;
    void *temp;
    OpcodeArg *arg;
};

struct opcode_arg_info {
    uint32_t resource;
    int index;
};

struct slot_prop {
    XtensaOpcodeOps *ops;
    OpcodeArg arg[MAX_OPCODE_ARGS];
    struct opcode_arg_info in[MAX_OPCODE_ARGS];
    struct opcode_arg_info out[MAX_OPCODE_ARGS];
    unsigned n_in;
    unsigned n_out;
    uint32_t op_flags;
};

enum resource_type {
    RES_REGFILE,
    RES_STATE,
    RES_MAX,
};

static uint32_t encode_resource(enum resource_type r, unsigned g, unsigned n)
{
    assert(r < RES_MAX && g < 256 && n < 65536);
    return (r << 24) | (g << 16) | n;
}

static enum resource_type get_resource_type(uint32_t resource)
{
    return resource >> 24;
}

/*
 * a depends on b if b must be executed before a,
 * because a's side effects will destroy b's inputs.
 */
static bool op_depends_on(const struct slot_prop *a,
                          const struct slot_prop *b)
{
    unsigned i = 0;
    unsigned j = 0;

    if (a->op_flags & XTENSA_OP_CONTROL_FLOW) {
        return true;
    }
    if ((a->op_flags & XTENSA_OP_LOAD_STORE) <
        (b->op_flags & XTENSA_OP_LOAD_STORE)) {
        return true;
    }
    while (i < a->n_out && j < b->n_in) {
        if (a->out[i].resource < b->in[j].resource) {
            ++i;
        } else if (a->out[i].resource > b->in[j].resource) {
            ++j;
        } else {
            return true;
        }
    }
    return false;
}

/*
 * Try to break a dependency on b, append temporary register copy records
 * to the end of copy and update n_copy in case of success.
 * This is not always possible: e.g. control flow must always be the last,
 * load/store must be first and state dependencies are not supported yet.
 */
static bool break_dependency(struct slot_prop *a,
                             struct slot_prop *b,
                             struct opcode_arg_copy *copy,
                             unsigned *n_copy)
{
    unsigned i = 0;
    unsigned j = 0;
    unsigned n = *n_copy;
    bool rv = false;

    if (a->op_flags & XTENSA_OP_CONTROL_FLOW) {
        return false;
    }
    if ((a->op_flags & XTENSA_OP_LOAD_STORE) <
        (b->op_flags & XTENSA_OP_LOAD_STORE)) {
        return false;
    }
    while (i < a->n_out && j < b->n_in) {
        if (a->out[i].resource < b->in[j].resource) {
            ++i;
        } else if (a->out[i].resource > b->in[j].resource) {
            ++j;
        } else {
            int index = b->in[j].index;

            if (get_resource_type(a->out[i].resource) != RES_REGFILE ||
                index < 0) {
                return false;
            }
            copy[n].resource = b->in[j].resource;
            copy[n].arg = b->arg + index;
            ++n;
            ++j;
            rv = true;
        }
    }
    *n_copy = n;
    return rv;
}

/*
 * Calculate evaluation order for slot opcodes.
 * Build opcode order graph and output its nodes in topological sort order.
 * An edge a -> b in the graph means that opcode a must be followed by
 * opcode b.
 */
static bool tsort(struct slot_prop *slot,
                  struct slot_prop *sorted[],
                  unsigned n,
                  struct opcode_arg_copy *copy,
                  unsigned *n_copy)
{
    struct tsnode {
        unsigned n_in_edge;
        unsigned n_out_edge;
        unsigned out_edge[MAX_INSN_SLOTS];
    } node[MAX_INSN_SLOTS];

    unsigned in[MAX_INSN_SLOTS];
    unsigned i, j;
    unsigned n_in = 0;
    unsigned n_out = 0;
    unsigned n_edge = 0;
    unsigned in_idx = 0;
    unsigned node_idx = 0;

    for (i = 0; i < n; ++i) {
        node[i].n_in_edge = 0;
        node[i].n_out_edge = 0;
    }

    for (i = 0; i < n; ++i) {
        unsigned n_out_edge = 0;

        for (j = 0; j < n; ++j) {
            if (i != j && op_depends_on(slot + j, slot + i)) {
                node[i].out_edge[n_out_edge] = j;
                ++node[j].n_in_edge;
                ++n_out_edge;
                ++n_edge;
            }
        }
        node[i].n_out_edge = n_out_edge;
    }

    for (i = 0; i < n; ++i) {
        if (!node[i].n_in_edge) {
            in[n_in] = i;
            ++n_in;
        }
    }

again:
    for (; in_idx < n_in; ++in_idx) {
        i = in[in_idx];
        sorted[n_out] = slot + i;
        ++n_out;
        for (j = 0; j < node[i].n_out_edge; ++j) {
            --n_edge;
            if (--node[node[i].out_edge[j]].n_in_edge == 0) {
                in[n_in] = node[i].out_edge[j];
                ++n_in;
            }
        }
    }
    if (n_edge) {
        for (; node_idx < n; ++node_idx) {
            struct tsnode *cnode = node + node_idx;

            if (cnode->n_in_edge) {
                for (j = 0; j < cnode->n_out_edge; ++j) {
                    unsigned k = cnode->out_edge[j];

                    if (break_dependency(slot + k, slot + node_idx,
                                         copy, n_copy) &&
                        --node[k].n_in_edge == 0) {
                        in[n_in] = k;
                        ++n_in;
                        --n_edge;
                        cnode->out_edge[j] =
                            cnode->out_edge[cnode->n_out_edge - 1];
                        --cnode->n_out_edge;
                        goto again;
                    }
                }
            }
        }
    }
    return n_edge == 0;
}

static void opcode_add_resource(struct slot_prop *op,
                                uint32_t resource, char direction,
                                int index)
{
    switch (direction) {
    case 'm':
    case 'i':
        assert(op->n_in < ARRAY_SIZE(op->in));
        op->in[op->n_in].resource = resource;
        op->in[op->n_in].index = index;
        ++op->n_in;
        /* fall through */
    case 'o':
        if (direction == 'm' || direction == 'o') {
            assert(op->n_out < ARRAY_SIZE(op->out));
            op->out[op->n_out].resource = resource;
            op->out[op->n_out].index = index;
            ++op->n_out;
        }
        break;
    default:
        g_assert_not_reached();
    }
}

static int resource_compare(const void *a, const void *b)
{
    const struct opcode_arg_info *pa = a;
    const struct opcode_arg_info *pb = b;

    return pa->resource < pb->resource ?
        -1 : (pa->resource > pb->resource ? 1 : 0);
}

static int arg_copy_compare(const void *a, const void *b)
{
    const struct opcode_arg_copy *pa = a;
    const struct opcode_arg_copy *pb = b;

    return pa->resource < pb->resource ?
        -1 : (pa->resource > pb->resource ? 1 : 0);
}

static void disas_xtensa_insn(CPUXtensaState *env, DisasContext *dc)
{
    xtensa_isa isa = dc->config->isa;
    unsigned char b[MAX_INSN_LENGTH] = {translator_ldub(env, &dc->base,
                                                        dc->pc)};
    unsigned len = xtensa_op0_insn_len(dc, b[0]);
    xtensa_format fmt;
    int slot, slots;
    unsigned i;
    uint32_t op_flags = 0;
    struct slot_prop slot_prop[MAX_INSN_SLOTS];
    struct slot_prop *ordered[MAX_INSN_SLOTS];
    struct opcode_arg_copy arg_copy[MAX_INSN_SLOTS * MAX_OPCODE_ARGS];
    unsigned n_arg_copy = 0;
    uint32_t debug_cause = 0;
    uint32_t windowed_register = 0;
    uint32_t coprocessor = 0;

    if (len == XTENSA_UNDEFINED) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "unknown instruction length (pc = %08x)\n",
                      dc->pc);
        gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
        dc->base.pc_next = dc->pc + 1;
        return;
    }

    dc->base.pc_next = dc->pc + len;
    for (i = 1; i < len; ++i) {
        b[i] = translator_ldub(env, &dc->base, dc->pc + i);
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
        OpcodeArg *arg = slot_prop[slot].arg;
        XtensaOpcodeOps *ops;

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
            void **register_file = NULL;
            xtensa_regfile rf;

            if (xtensa_operand_is_register(isa, opc, opnd)) {
                rf = xtensa_operand_regfile(isa, opc, opnd);
                register_file = dc->config->regfile[rf];

                if (rf == dc->config->a_regfile) {
                    uint32_t v;

                    xtensa_operand_get_field(isa, opc, opnd, fmt, slot,
                                             dc->slotbuf, &v);
                    xtensa_operand_decode(isa, opc, opnd, &v);
                    windowed_register |= 1u << v;
                }
            }
            if (xtensa_operand_is_visible(isa, opc, opnd)) {
                uint32_t v;

                xtensa_operand_get_field(isa, opc, opnd, fmt, slot,
                                         dc->slotbuf, &v);
                xtensa_operand_decode(isa, opc, opnd, &v);
                arg[vopnd].raw_imm = v;
                if (xtensa_operand_is_PCrelative(isa, opc, opnd)) {
                    xtensa_operand_undo_reloc(isa, opc, opnd, &v, dc->pc);
                }
                arg[vopnd].imm = v;
                if (register_file) {
                    arg[vopnd].in = register_file[v];
                    arg[vopnd].out = register_file[v];
                    arg[vopnd].num_bits = xtensa_regfile_num_bits(isa, rf);
                } else {
                    arg[vopnd].num_bits = 32;
                }
                ++vopnd;
            }
        }
        ops = dc->config->opcode_ops[opc];
        slot_prop[slot].ops = ops;

        if (ops) {
            op_flags |= ops->op_flags;
            if (ops->test_exceptions) {
                op_flags |= ops->test_exceptions(dc, arg, ops->par);
            }
        } else {
            qemu_log_mask(LOG_UNIMP,
                          "unimplemented opcode '%s' in slot %d (pc = %08x)\n",
                          xtensa_opcode_name(isa, opc), slot, dc->pc);
            op_flags |= XTENSA_OP_ILL;
        }
        if (op_flags & XTENSA_OP_ILL) {
            gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
            return;
        }
        if (op_flags & XTENSA_OP_DEBUG_BREAK) {
            debug_cause |= ops->par[0];
        }
        if (ops->test_overflow) {
            windowed_register |= ops->test_overflow(dc, arg, ops->par);
        }
        coprocessor |= ops->coprocessor;

        if (slots > 1) {
            slot_prop[slot].n_in = 0;
            slot_prop[slot].n_out = 0;
            slot_prop[slot].op_flags = ops->op_flags & XTENSA_OP_LOAD_STORE;

            opnds = xtensa_opcode_num_operands(isa, opc);

            for (opnd = vopnd = 0; opnd < opnds; ++opnd) {
                bool visible = xtensa_operand_is_visible(isa, opc, opnd);

                if (xtensa_operand_is_register(isa, opc, opnd)) {
                    xtensa_regfile rf = xtensa_operand_regfile(isa, opc, opnd);
                    uint32_t v = 0;

                    xtensa_operand_get_field(isa, opc, opnd, fmt, slot,
                                             dc->slotbuf, &v);
                    xtensa_operand_decode(isa, opc, opnd, &v);
                    opcode_add_resource(slot_prop + slot,
                                        encode_resource(RES_REGFILE, rf, v),
                                        xtensa_operand_inout(isa, opc, opnd),
                                        visible ? vopnd : -1);
                }
                if (visible) {
                    ++vopnd;
                }
            }

            opnds = xtensa_opcode_num_stateOperands(isa, opc);

            for (opnd = 0; opnd < opnds; ++opnd) {
                xtensa_state state = xtensa_stateOperand_state(isa, opc, opnd);

                opcode_add_resource(slot_prop + slot,
                                    encode_resource(RES_STATE, 0, state),
                                    xtensa_stateOperand_inout(isa, opc, opnd),
                                    -1);
            }
            if (xtensa_opcode_is_branch(isa, opc) ||
                xtensa_opcode_is_jump(isa, opc) ||
                xtensa_opcode_is_loop(isa, opc) ||
                xtensa_opcode_is_call(isa, opc)) {
                slot_prop[slot].op_flags |= XTENSA_OP_CONTROL_FLOW;
            }

            qsort(slot_prop[slot].in, slot_prop[slot].n_in,
                  sizeof(slot_prop[slot].in[0]), resource_compare);
            qsort(slot_prop[slot].out, slot_prop[slot].n_out,
                  sizeof(slot_prop[slot].out[0]), resource_compare);
        }
    }

    if (slots > 1) {
        if (!tsort(slot_prop, ordered, slots, arg_copy, &n_arg_copy)) {
            qemu_log_mask(LOG_UNIMP,
                          "Circular resource dependencies (pc = %08x)\n",
                          dc->pc);
            gen_exception_cause(dc, ILLEGAL_INSTRUCTION_CAUSE);
            return;
        }
    } else {
        ordered[0] = slot_prop + 0;
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
        TCGv_i32 pc = tcg_constant_i32(dc->pc);

        gen_helper_test_underflow_retw(cpu_env, pc);
    }

    if (op_flags & XTENSA_OP_ALLOCA) {
        TCGv_i32 pc = tcg_constant_i32(dc->pc);

        gen_helper_movsp(cpu_env, pc);
    }

    if (coprocessor && !gen_check_cpenable(dc, coprocessor)) {
        return;
    }

    if (n_arg_copy) {
        uint32_t resource;
        void *temp;
        unsigned j;

        qsort(arg_copy, n_arg_copy, sizeof(*arg_copy), arg_copy_compare);
        for (i = j = 0; i < n_arg_copy; ++i) {
            if (i == 0 || arg_copy[i].resource != resource) {
                resource = arg_copy[i].resource;
                if (arg_copy[i].arg->num_bits <= 32) {
                    temp = tcg_temp_new_i32();
                    tcg_gen_mov_i32(temp, arg_copy[i].arg->in);
                } else if (arg_copy[i].arg->num_bits <= 64) {
                    temp = tcg_temp_new_i64();
                    tcg_gen_mov_i64(temp, arg_copy[i].arg->in);
                } else {
                    g_assert_not_reached();
                }
                arg_copy[i].temp = temp;

                if (i != j) {
                    arg_copy[j] = arg_copy[i];
                }
                ++j;
            }
            arg_copy[i].arg->in = temp;
        }
        n_arg_copy = j;
    }

    if (op_flags & XTENSA_OP_DIVIDE_BY_ZERO) {
        for (slot = 0; slot < slots; ++slot) {
            if (slot_prop[slot].ops->op_flags & XTENSA_OP_DIVIDE_BY_ZERO) {
                gen_zero_check(dc, slot_prop[slot].arg);
            }
        }
    }

    dc->op_flags = op_flags;

    for (slot = 0; slot < slots; ++slot) {
        struct slot_prop *pslot = ordered[slot];
        XtensaOpcodeOps *ops = pslot->ops;

        ops->translate(dc, pslot->arg, ops->par);
    }

    if (dc->base.is_jmp == DISAS_NEXT) {
        gen_postprocess(dc, 0);
        dc->op_flags = 0;
        if (op_flags & XTENSA_OP_EXIT_TB_M1) {
            /* Change in mmu index, memory mapping or tb->flags; exit tb */
            gen_jumpi_check_loop_end(dc, -1);
        } else if (op_flags & XTENSA_OP_EXIT_TB_0) {
            gen_jumpi_check_loop_end(dc, 0);
        } else {
            gen_check_loop_end(dc, 0);
        }
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
    dc->lbeg_off = (dc->base.tb->cs_base & XTENSA_CSBASE_LBEG_OFF_MASK) >>
        XTENSA_CSBASE_LBEG_OFF_SHIFT;
    dc->lend = (dc->base.tb->cs_base & XTENSA_CSBASE_LEND_MASK) +
        (dc->base.pc_first & TARGET_PAGE_MASK);
    dc->debug = tb_flags & XTENSA_TBFLAG_DEBUG;
    dc->icount = tb_flags & XTENSA_TBFLAG_ICOUNT;
    dc->cpenable = (tb_flags & XTENSA_TBFLAG_CPENABLE_MASK) >>
        XTENSA_TBFLAG_CPENABLE_SHIFT;
    dc->window = ((tb_flags & XTENSA_TBFLAG_WINDOW_MASK) >>
                 XTENSA_TBFLAG_WINDOW_SHIFT);
    dc->cwoe = tb_flags & XTENSA_TBFLAG_CWOE;
    dc->callinc = ((tb_flags & XTENSA_TBFLAG_CALLINC_MASK) >>
                   XTENSA_TBFLAG_CALLINC_SHIFT);
    init_sar_tracker(dc);
}

static void xtensa_tr_tb_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    if (dc->icount) {
        dc->next_icount = tcg_temp_new_i32();
    }
}

static void xtensa_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    tcg_gen_insn_start(dcbase->pc_next);
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
        dc->base.pc_next = dc->pc + 1;
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

    switch (dc->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        gen_jumpi(dc, dc->pc, 0);
        break;
    default:
        g_assert_not_reached();
    }
}

static void xtensa_tr_disas_log(const DisasContextBase *dcbase,
                                CPUState *cpu, FILE *logfile)
{
    fprintf(logfile, "IN: %s\n", lookup_symbol(dcbase->pc_first));
    target_disas(logfile, cpu, dcbase->pc_first, dcbase->tb->size);
}

static const TranslatorOps xtensa_translator_ops = {
    .init_disas_context = xtensa_tr_init_disas_context,
    .tb_start           = xtensa_tr_tb_start,
    .insn_start         = xtensa_tr_insn_start,
    .translate_insn     = xtensa_tr_translate_insn,
    .tb_stop            = xtensa_tr_tb_stop,
    .disas_log          = xtensa_tr_disas_log,
};

void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                           target_ulong pc, void *host_pc)
{
    DisasContext dc = {};
    translator_loop(cpu, tb, max_insns, pc, host_pc,
                    &xtensa_translator_ops, &dc.base);
}

void xtensa_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    XtensaCPU *cpu = XTENSA_CPU(cs);
    CPUXtensaState *env = &cpu->env;
    xtensa_isa isa = env->config->isa;
    int i, j;

    qemu_fprintf(f, "PC=%08x\n\n", env->pc);

    for (i = j = 0; i < xtensa_isa_num_sysregs(isa); ++i) {
        const uint32_t *reg =
            xtensa_sysreg_is_user(isa, i) ? env->uregs : env->sregs;
        int regno = xtensa_sysreg_number(isa, i);

        if (regno >= 0) {
            qemu_fprintf(f, "%12s=%08x%c",
                         xtensa_sysreg_name(isa, i),
                         reg[regno],
                         (j++ % 4) == 3 ? '\n' : ' ');
        }
    }

    qemu_fprintf(f, (j % 4) == 0 ? "\n" : "\n\n");

    for (i = 0; i < 16; ++i) {
        qemu_fprintf(f, " A%02d=%08x%c",
                     i, env->regs[i], (i % 4) == 3 ? '\n' : ' ');
    }

    xtensa_sync_phys_from_window(env);
    qemu_fprintf(f, "\n");

    for (i = 0; i < env->config->nareg; ++i) {
        qemu_fprintf(f, "AR%02d=%08x ", i, env->phys_regs[i]);
        if (i % 4 == 3) {
            bool ws = (env->sregs[WINDOW_START] & (1 << (i / 4))) != 0;
            bool cw = env->sregs[WINDOW_BASE] == i / 4;

            qemu_fprintf(f, "%c%c\n", ws ? '<' : ' ', cw ? '=' : ' ');
        }
    }

    if ((flags & CPU_DUMP_FPU) &&
        xtensa_option_enabled(env->config, XTENSA_OPTION_FP_COPROCESSOR)) {
        qemu_fprintf(f, "\n");

        for (i = 0; i < 16; ++i) {
            qemu_fprintf(f, "F%02d=%08x (%-+15.8e)%c", i,
                         float32_val(env->fregs[i].f32[FP_F32_LOW]),
                         *(float *)(env->fregs[i].f32 + FP_F32_LOW),
                         (i % 2) == 1 ? '\n' : ' ');
        }
    }

    if ((flags & CPU_DUMP_FPU) &&
        xtensa_option_enabled(env->config, XTENSA_OPTION_DFP_COPROCESSOR) &&
        !xtensa_option_enabled(env->config, XTENSA_OPTION_DFPU_SINGLE_ONLY)) {
        qemu_fprintf(f, "\n");

        for (i = 0; i < 16; ++i) {
            qemu_fprintf(f, "F%02d=%016"PRIx64" (%-+24.16le)%c", i,
                         float64_val(env->fregs[i].f64),
                         *(double *)(&env->fregs[i].f64),
                         (i % 2) == 1 ? '\n' : ' ');
        }
    }
}

static void translate_abs(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_abs_i32(arg[0].out, arg[1].in);
}

static void translate_add(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_add_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_addi(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_addi_i32(arg[0].out, arg[1].in, arg[2].imm);
}

static void translate_addx(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, arg[1].in, par[0]);
    tcg_gen_add_i32(arg[0].out, tmp, arg[2].in);
}

static void translate_all(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    uint32_t shift = par[1];
    TCGv_i32 mask = tcg_const_i32(((1 << shift) - 1) << arg[1].imm);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_and_i32(tmp, arg[1].in, mask);
    if (par[0]) {
        tcg_gen_addi_i32(tmp, tmp, 1 << arg[1].imm);
    } else {
        tcg_gen_add_i32(tmp, tmp, mask);
    }
    tcg_gen_shri_i32(tmp, tmp, arg[1].imm + shift);
    tcg_gen_deposit_i32(arg[0].out, arg[0].out,
                        tmp, arg[0].imm, 1);
}

static void translate_and(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_and_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_ball(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, arg[0].in, arg[1].in);
    gen_brcond(dc, par[0], tmp, arg[1].in, arg[2].imm);
}

static void translate_bany(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_and_i32(tmp, arg[0].in, arg[1].in);
    gen_brcondi(dc, par[0], tmp, 0, arg[2].imm);
}

static void translate_b(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
    gen_brcond(dc, par[0], arg[0].in, arg[1].in, arg[2].imm);
}

static void translate_bb(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, arg[1].in, 0x1f);
    if (TARGET_BIG_ENDIAN) {
        tcg_gen_shr_i32(tmp, tcg_constant_i32(0x80000000u), tmp);
    } else {
        tcg_gen_shl_i32(tmp, tcg_constant_i32(0x00000001u), tmp);
    }
    tcg_gen_and_i32(tmp, arg[0].in, tmp);
    gen_brcondi(dc, par[0], tmp, 0, arg[2].imm);
}

static void translate_bbi(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
#if TARGET_BIG_ENDIAN
    tcg_gen_andi_i32(tmp, arg[0].in, 0x80000000u >> arg[1].imm);
#else
    tcg_gen_andi_i32(tmp, arg[0].in, 0x00000001u << arg[1].imm);
#endif
    gen_brcondi(dc, par[0], tmp, 0, arg[2].imm);
}

static void translate_bi(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
    gen_brcondi(dc, par[0], arg[0].in, arg[1].imm, arg[2].imm);
}

static void translate_bz(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
    gen_brcondi(dc, par[0], arg[0].in, 0, arg[1].imm);
}

enum {
    BOOLEAN_AND,
    BOOLEAN_ANDC,
    BOOLEAN_OR,
    BOOLEAN_ORC,
    BOOLEAN_XOR,
};

static void translate_boolean(DisasContext *dc, const OpcodeArg arg[],
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

    tcg_gen_shri_i32(tmp1, arg[1].in, arg[1].imm);
    tcg_gen_shri_i32(tmp2, arg[2].in, arg[2].imm);
    op[par[0]](tmp1, tmp1, tmp2);
    tcg_gen_deposit_i32(arg[0].out, arg[0].out, tmp1, arg[0].imm, 1);
}

static void translate_bp(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, arg[0].in, 1 << arg[0].imm);
    gen_brcondi(dc, par[0], tmp, 0, arg[1].imm);
}

static void translate_call0(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    tcg_gen_movi_i32(cpu_R[0], dc->base.pc_next);
    gen_jumpi(dc, arg[0].imm, 0);
}

static void translate_callw(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 tmp = tcg_const_i32(arg[0].imm);
    gen_callw_slot(dc, par[0], tmp, adjust_jump_slot(dc, arg[0].imm, 0));
}

static void translate_callx0(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_mov_i32(tmp, arg[0].in);
    tcg_gen_movi_i32(cpu_R[0], dc->base.pc_next);
    gen_jump(dc, tmp);
}

static void translate_callxw(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_mov_i32(tmp, arg[0].in);
    gen_callw_slot(dc, par[0], tmp, -1);
}

static void translate_clamps(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp1 = tcg_constant_i32(-1u << arg[2].imm);
    TCGv_i32 tmp2 = tcg_constant_i32((1 << arg[2].imm) - 1);

    tcg_gen_smax_i32(arg[0].out, tmp1, arg[1].in);
    tcg_gen_smin_i32(arg[0].out, arg[0].out, tmp2);
}

static void translate_clrb_expstate(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_andi_i32(cpu_UR[EXPSTATE], cpu_UR[EXPSTATE], ~(1u << arg[0].imm));
}

static void translate_clrex(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    tcg_gen_movi_i32(cpu_exclusive_addr, -1);
}

static void translate_const16(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 c = tcg_const_i32(arg[1].imm);

    tcg_gen_deposit_i32(arg[0].out, c, arg[0].in, 16, 16);
}

static void translate_dcache(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 res = tcg_temp_new_i32();

    tcg_gen_addi_i32(addr, arg[0].in, arg[1].imm);
    tcg_gen_qemu_ld8u(res, addr, dc->cring);
}

static void translate_depbits(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    tcg_gen_deposit_i32(arg[1].out, arg[1].in, arg[0].in,
                        arg[2].imm, arg[3].imm);
}

static void translate_diwbuip(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    tcg_gen_addi_i32(arg[0].out, arg[0].in, dc->config->dcache_line_bytes);
}

static uint32_t test_exceptions_entry(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
    if (arg[0].imm > 3 || !dc->cwoe) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Illegal entry instruction(pc = %08x)\n", dc->pc);
        return XTENSA_OP_ILL;
    } else {
        return 0;
    }
}

static uint32_t test_overflow_entry(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    return 1 << (dc->callinc * 4);
}

static void translate_entry(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 pc = tcg_constant_i32(dc->pc);
    TCGv_i32 s = tcg_constant_i32(arg[0].imm);
    TCGv_i32 imm = tcg_constant_i32(arg[1].imm);
    gen_helper_entry(cpu_env, pc, s, imm);
}

static void translate_extui(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    int maskimm = (1 << arg[3].imm) - 1;

    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shri_i32(tmp, arg[1].in, arg[2].imm);
    tcg_gen_andi_i32(arg[0].out, tmp, maskimm);
}

static void translate_getex(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_extract_i32(tmp, cpu_SR[ATOMCTL], 8, 1);
    tcg_gen_deposit_i32(cpu_SR[ATOMCTL], cpu_SR[ATOMCTL], arg[0].in, 8, 1);
    tcg_gen_mov_i32(arg[0].out, tmp);
}

static void translate_icache(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 addr = tcg_temp_new_i32();

    tcg_gen_movi_i32(cpu_pc, dc->pc);
    tcg_gen_addi_i32(addr, arg[0].in, arg[1].imm);
    gen_helper_itlb_hit_test(cpu_env, addr);
#endif
}

static void translate_itlb(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 dtlb = tcg_constant_i32(par[0]);

    gen_helper_itlb(cpu_env, arg[0].in, dtlb);
#endif
}

static void translate_j(DisasContext *dc, const OpcodeArg arg[],
                        const uint32_t par[])
{
    gen_jumpi(dc, arg[0].imm, 0);
}

static void translate_jx(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
    gen_jump(dc, arg[0].in);
}

static void translate_l32e(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    mop = gen_load_store_alignment(dc, MO_TEUL, addr);
    tcg_gen_qemu_ld_tl(arg[0].out, addr, dc->ring, mop);
}

#ifdef CONFIG_USER_ONLY
static void gen_check_exclusive(DisasContext *dc, TCGv_i32 addr, bool is_write)
{
}
#else
static void gen_check_exclusive(DisasContext *dc, TCGv_i32 addr, bool is_write)
{
    if (!option_enabled(dc, XTENSA_OPTION_MPU)) {
        TCGv_i32 pc = tcg_constant_i32(dc->pc);

        gen_helper_check_exclusive(cpu_env, pc, addr,
                                   tcg_constant_i32(is_write));
    }
}
#endif

static void translate_l32ex(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_mov_i32(addr, arg[1].in);
    mop = gen_load_store_alignment(dc, MO_TEUL | MO_ALIGN, addr);
    gen_check_exclusive(dc, addr, false);
    tcg_gen_qemu_ld_i32(arg[0].out, addr, dc->cring, mop);
    tcg_gen_mov_i32(cpu_exclusive_addr, addr);
    tcg_gen_mov_i32(cpu_exclusive_val, arg[0].out);
}

static void translate_ldst(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    mop = gen_load_store_alignment(dc, par[0], addr);

    if (par[2]) {
        if (par[1]) {
            tcg_gen_mb(TCG_BAR_STRL | TCG_MO_ALL);
        }
        tcg_gen_qemu_st_tl(arg[0].in, addr, dc->cring, mop);
    } else {
        tcg_gen_qemu_ld_tl(arg[0].out, addr, dc->cring, mop);
        if (par[1]) {
            tcg_gen_mb(TCG_BAR_LDAQ | TCG_MO_ALL);
        }
    }
}

static void translate_lct(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_movi_i32(arg[0].out, 0);
}

static void translate_l32r(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp;

    if (dc->base.tb->flags & XTENSA_TBFLAG_LITBASE) {
        tmp = tcg_const_i32(arg[1].raw_imm - 1);
        tcg_gen_add_i32(tmp, cpu_SR[LITBASE], tmp);
    } else {
        tmp = tcg_const_i32(arg[1].imm);
    }
    tcg_gen_qemu_ld32u(arg[0].out, tmp, dc->cring);
}

static void translate_loop(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    uint32_t lend = arg[1].imm;

    tcg_gen_subi_i32(cpu_SR[LCOUNT], arg[0].in, 1);
    tcg_gen_movi_i32(cpu_SR[LBEG], dc->base.pc_next);
    tcg_gen_movi_i32(cpu_SR[LEND], lend);

    if (par[0] != TCG_COND_NEVER) {
        TCGLabel *label = gen_new_label();
        tcg_gen_brcondi_i32(par[0], arg[0].in, 0, label);
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

static void translate_mac16(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    int op = par[0];
    unsigned half = par[1];
    uint32_t ld_offset = par[2];
    unsigned off = ld_offset ? 2 : 0;
    TCGv_i32 vaddr = tcg_temp_new_i32();
    TCGv_i32 mem32 = tcg_temp_new_i32();

    if (ld_offset) {
        MemOp mop;

        tcg_gen_addi_i32(vaddr, arg[1].in, ld_offset);
        mop = gen_load_store_alignment(dc, MO_TEUL, vaddr);
        tcg_gen_qemu_ld_tl(mem32, vaddr, dc->cring, mop);
    }
    if (op != MAC16_NONE) {
        TCGv_i32 m1 = gen_mac16_m(arg[off].in,
                                  half & MAC16_HX, op == MAC16_UMUL);
        TCGv_i32 m2 = gen_mac16_m(arg[off + 1].in,
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
        }
    }
    if (ld_offset) {
        tcg_gen_mov_i32(arg[1].out, vaddr);
        tcg_gen_mov_i32(cpu_SR[MR + arg[0].imm], mem32);
    }
}

static void translate_memw(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_mb(TCG_BAR_SC | TCG_MO_ALL);
}

static void translate_smin(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_smin_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_umin(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_umin_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_smax(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_smax_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_umax(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_umax_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_mov(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_mov_i32(arg[0].out, arg[1].in);
}

static void translate_movcond(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 zero = tcg_constant_i32(0);

    tcg_gen_movcond_i32(par[0], arg[0].out,
                        arg[2].in, zero, arg[1].in, arg[0].in);
}

static void translate_movi(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_movi_i32(arg[0].out, arg[1].imm);
}

static void translate_movp(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_andi_i32(tmp, arg[2].in, 1 << arg[2].imm);
    tcg_gen_movcond_i32(par[0],
                        arg[0].out, tmp, zero,
                        arg[1].in, arg[0].in);
}

static void translate_movsp(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    tcg_gen_mov_i32(arg[0].out, arg[1].in);
}

static void translate_mul16(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 v1 = tcg_temp_new_i32();
    TCGv_i32 v2 = tcg_temp_new_i32();

    if (par[0]) {
        tcg_gen_ext16s_i32(v1, arg[1].in);
        tcg_gen_ext16s_i32(v2, arg[2].in);
    } else {
        tcg_gen_ext16u_i32(v1, arg[1].in);
        tcg_gen_ext16u_i32(v2, arg[2].in);
    }
    tcg_gen_mul_i32(arg[0].out, v1, v2);
}

static void translate_mull(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_mul_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_mulh(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 lo = tcg_temp_new();

    if (par[0]) {
        tcg_gen_muls2_i32(lo, arg[0].out, arg[1].in, arg[2].in);
    } else {
        tcg_gen_mulu2_i32(lo, arg[0].out, arg[1].in, arg[2].in);
    }
}

static void translate_neg(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_neg_i32(arg[0].out, arg[1].in);
}

static void translate_nop(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
}

static void translate_nsa(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_clrsb_i32(arg[0].out, arg[1].in);
}

static void translate_nsau(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_clzi_i32(arg[0].out, arg[1].in, 32);
}

static void translate_or(DisasContext *dc, const OpcodeArg arg[],
                         const uint32_t par[])
{
    tcg_gen_or_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_ptlb(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 dtlb = tcg_constant_i32(par[0]);

    tcg_gen_movi_i32(cpu_pc, dc->pc);
    gen_helper_ptlb(arg[0].out, cpu_env, arg[1].in, dtlb);
#endif
}

static void translate_pptlb(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_i32(cpu_pc, dc->pc);
    gen_helper_pptlb(arg[0].out, cpu_env, arg[1].in);
#endif
}

static void translate_quos(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGLabel *label1 = gen_new_label();
    TCGLabel *label2 = gen_new_label();

    tcg_gen_brcondi_i32(TCG_COND_NE, arg[1].in, 0x80000000,
                        label1);
    tcg_gen_brcondi_i32(TCG_COND_NE, arg[2].in, 0xffffffff,
                        label1);
    tcg_gen_movi_i32(arg[0].out,
                     par[0] ? 0x80000000 : 0);
    tcg_gen_br(label2);
    gen_set_label(label1);
    if (par[0]) {
        tcg_gen_div_i32(arg[0].out,
                        arg[1].in, arg[2].in);
    } else {
        tcg_gen_rem_i32(arg[0].out,
                        arg[1].in, arg[2].in);
    }
    gen_set_label(label2);
}

static void translate_quou(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_divu_i32(arg[0].out,
                     arg[1].in, arg[2].in);
}

static void translate_read_impwire(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_movi_i32(arg[0].out, 0);
}

static void translate_remu(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_remu_i32(arg[0].out,
                     arg[1].in, arg[2].in);
}

static void translate_rer(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    gen_helper_rer(arg[0].out, cpu_env, arg[1].in);
}

static void translate_ret(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    gen_jump(dc, cpu_R[0]);
}

static uint32_t test_exceptions_retw(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    if (!dc->cwoe) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "Illegal retw instruction(pc = %08x)\n", dc->pc);
        return XTENSA_OP_ILL;
    } else {
        TCGv_i32 pc = tcg_constant_i32(dc->pc);

        gen_helper_test_ill_retw(cpu_env, pc);
        return 0;
    }
}

static void translate_retw(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_const_i32(1);
    tcg_gen_shl_i32(tmp, tmp, cpu_SR[WINDOW_BASE]);
    tcg_gen_andc_i32(cpu_SR[WINDOW_START],
                     cpu_SR[WINDOW_START], tmp);
    tcg_gen_movi_i32(tmp, dc->pc);
    tcg_gen_deposit_i32(tmp, tmp, cpu_R[0], 0, 30);
    gen_helper_retw(cpu_env, cpu_R[0]);
    gen_jump(dc, tmp);
}

static void translate_rfde(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    gen_jump(dc, cpu_SR[dc->config->ndepc ? DEPC : EPC1]);
}

static void translate_rfe(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_EXCM);
    gen_jump(dc, cpu_SR[EPC1]);
}

static void translate_rfi(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_SR[PS], cpu_SR[EPS2 + arg[0].imm - 2]);
    gen_jump(dc, cpu_SR[EPC1 + arg[0].imm - 1]);
}

static void translate_rfw(DisasContext *dc, const OpcodeArg arg[],
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

    gen_helper_restore_owb(cpu_env);
    gen_jump(dc, cpu_SR[EPC1]);
}

static void translate_rotw(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_addi_i32(cpu_windowbase_next, cpu_SR[WINDOW_BASE], arg[0].imm);
}

static void translate_rsil(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_mov_i32(arg[0].out, cpu_SR[PS]);
    tcg_gen_andi_i32(cpu_SR[PS], cpu_SR[PS], ~PS_INTLEVEL);
    tcg_gen_ori_i32(cpu_SR[PS], cpu_SR[PS], arg[1].imm);
}

static void translate_rsr(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    if (sr_name[par[0]]) {
        tcg_gen_mov_i32(arg[0].out, cpu_SR[par[0]]);
    } else {
        tcg_gen_movi_i32(arg[0].out, 0);
    }
}

static void translate_rsr_ccount(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_update_ccount(cpu_env);
    tcg_gen_mov_i32(arg[0].out, cpu_SR[par[0]]);
#endif
}

static void translate_rsr_ptevaddr(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 tmp = tcg_temp_new_i32();

    tcg_gen_shri_i32(tmp, cpu_SR[EXCVADDR], 10);
    tcg_gen_or_i32(tmp, tmp, cpu_SR[PTEVADDR]);
    tcg_gen_andi_i32(arg[0].out, tmp, 0xfffffffc);
#endif
}

static void translate_rtlb(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    static void (* const helper[])(TCGv_i32 r, TCGv_env env, TCGv_i32 a1,
                                   TCGv_i32 a2) = {
        gen_helper_rtlb0,
        gen_helper_rtlb1,
    };
    TCGv_i32 dtlb = tcg_constant_i32(par[0]);

    helper[par[1]](arg[0].out, cpu_env, arg[1].in, dtlb);
#endif
}

static void translate_rptlb0(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_rptlb0(arg[0].out, cpu_env, arg[1].in);
#endif
}

static void translate_rptlb1(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_rptlb1(arg[0].out, cpu_env, arg[1].in);
#endif
}

static void translate_rur(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_mov_i32(arg[0].out, cpu_UR[par[0]]);
}

static void translate_setb_expstate(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_ori_i32(cpu_UR[EXPSTATE], cpu_UR[EXPSTATE], 1u << arg[0].imm);
}

#ifdef CONFIG_USER_ONLY
static void gen_check_atomctl(DisasContext *dc, TCGv_i32 addr)
{
}
#else
static void gen_check_atomctl(DisasContext *dc, TCGv_i32 addr)
{
    TCGv_i32 pc = tcg_constant_i32(dc->pc);

    gen_helper_check_atomctl(cpu_env, pc, addr);
}
#endif

static void translate_s32c1i(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_mov_i32(tmp, arg[0].in);
    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    mop = gen_load_store_alignment(dc, MO_TEUL | MO_ALIGN, addr);
    gen_check_atomctl(dc, addr);
    tcg_gen_atomic_cmpxchg_i32(arg[0].out, addr, cpu_SR[SCOMPARE1],
                               tmp, dc->cring, mop);
}

static void translate_s32e(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    mop = gen_load_store_alignment(dc, MO_TEUL, addr);
    tcg_gen_qemu_st_tl(arg[0].in, addr, dc->ring, mop);
}

static void translate_s32ex(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 prev = tcg_temp_new_i32();
    TCGv_i32 addr = tcg_temp_new_i32();
    TCGv_i32 res = tcg_temp_new_i32();
    TCGLabel *label = gen_new_label();
    MemOp mop;

    tcg_gen_movi_i32(res, 0);
    tcg_gen_mov_i32(addr, arg[1].in);
    mop = gen_load_store_alignment(dc, MO_TEUL | MO_ALIGN, addr);
    tcg_gen_brcond_i32(TCG_COND_NE, addr, cpu_exclusive_addr, label);
    gen_check_exclusive(dc, addr, true);
    tcg_gen_atomic_cmpxchg_i32(prev, cpu_exclusive_addr, cpu_exclusive_val,
                               arg[0].in, dc->cring, mop);
    tcg_gen_setcond_i32(TCG_COND_EQ, res, prev, cpu_exclusive_val);
    tcg_gen_movcond_i32(TCG_COND_EQ, cpu_exclusive_val,
                        prev, cpu_exclusive_val, prev, cpu_exclusive_val);
    tcg_gen_movi_i32(cpu_exclusive_addr, -1);
    gen_set_label(label);
    tcg_gen_extract_i32(arg[0].out, cpu_SR[ATOMCTL], 8, 1);
    tcg_gen_deposit_i32(cpu_SR[ATOMCTL], cpu_SR[ATOMCTL], res, 8, 1);
}

static void translate_salt(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_setcond_i32(par[0],
                        arg[0].out,
                        arg[1].in, arg[2].in);
}

static void translate_sext(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    int shift = 31 - arg[2].imm;

    if (shift == 24) {
        tcg_gen_ext8s_i32(arg[0].out, arg[1].in);
    } else if (shift == 16) {
        tcg_gen_ext16s_i32(arg[0].out, arg[1].in);
    } else {
        TCGv_i32 tmp = tcg_temp_new_i32();
        tcg_gen_shli_i32(tmp, arg[1].in, shift);
        tcg_gen_sari_i32(arg[0].out, tmp, shift);
    }
}

static uint32_t test_exceptions_simcall(DisasContext *dc,
                                        const OpcodeArg arg[],
                                        const uint32_t par[])
{
    bool is_semi = semihosting_enabled(dc->cring != 0);
#ifdef CONFIG_USER_ONLY
    bool ill = true;
#else
    /* Between RE.2 and RE.3 simcall opcode's become nop for the hardware. */
    bool ill = dc->config->hw_version <= 250002 && !is_semi;
#endif
    if (ill || !is_semi) {
        qemu_log_mask(LOG_GUEST_ERROR, "SIMCALL but semihosting is disabled\n");
    }
    return ill ? XTENSA_OP_ILL : 0;
}

static void translate_simcall(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    if (semihosting_enabled(dc->cring != 0)) {
        gen_helper_simcall(cpu_env);
    }
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
                    tcg_gen_extrl_i64_i32(arg[0].out, v); \
                } while (0)

#define gen_shift(cmd) gen_shift_reg(cmd, cpu_SR[SAR])

static void translate_sll(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    if (dc->sar_m32_5bit) {
        tcg_gen_shl_i32(arg[0].out, arg[1].in, dc->sar_m32);
    } else {
        TCGv_i64 v = tcg_temp_new_i64();
        TCGv_i32 s = tcg_const_i32(32);
        tcg_gen_sub_i32(s, s, cpu_SR[SAR]);
        tcg_gen_andi_i32(s, s, 0x3f);
        tcg_gen_extu_i32_i64(v, arg[1].in);
        gen_shift_reg(shl, s);
    }
}

static void translate_slli(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    if (arg[2].imm == 32) {
        qemu_log_mask(LOG_GUEST_ERROR, "slli a%d, a%d, 32 is undefined\n",
                      arg[0].imm, arg[1].imm);
    }
    tcg_gen_shli_i32(arg[0].out, arg[1].in, arg[2].imm & 0x1f);
}

static void translate_sra(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    if (dc->sar_m32_5bit) {
        tcg_gen_sar_i32(arg[0].out, arg[1].in, cpu_SR[SAR]);
    } else {
        TCGv_i64 v = tcg_temp_new_i64();
        tcg_gen_ext_i32_i64(v, arg[1].in);
        gen_shift(sar);
    }
}

static void translate_srai(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_sari_i32(arg[0].out, arg[1].in, arg[2].imm);
}

static void translate_src(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    TCGv_i64 v = tcg_temp_new_i64();
    tcg_gen_concat_i32_i64(v, arg[2].in, arg[1].in);
    gen_shift(shr);
}

static void translate_srl(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    if (dc->sar_m32_5bit) {
        tcg_gen_shr_i32(arg[0].out, arg[1].in, cpu_SR[SAR]);
    } else {
        TCGv_i64 v = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(v, arg[1].in);
        gen_shift(shr);
    }
}

#undef gen_shift
#undef gen_shift_reg

static void translate_srli(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    tcg_gen_shri_i32(arg[0].out, arg[1].in, arg[2].imm);
}

static void translate_ssa8b(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, arg[0].in, 3);
    gen_left_shift_sar(dc, tmp);
}

static void translate_ssa8l(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, arg[0].in, 3);
    gen_right_shift_sar(dc, tmp);
}

static void translate_ssai(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    gen_right_shift_sar(dc, tcg_constant_i32(arg[0].imm));
}

static void translate_ssl(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    gen_left_shift_sar(dc, arg[0].in);
}

static void translate_ssr(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    gen_right_shift_sar(dc, arg[0].in);
}

static void translate_sub(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_sub_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_subx(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
    TCGv_i32 tmp = tcg_temp_new_i32();
    tcg_gen_shli_i32(tmp, arg[1].in, par[0]);
    tcg_gen_sub_i32(arg[0].out, tmp, arg[2].in);
}

static void translate_waiti(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 pc = tcg_constant_i32(dc->base.pc_next);

    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_waiti(cpu_env, pc, tcg_constant_i32(arg[0].imm));
#endif
}

static void translate_wtlb(DisasContext *dc, const OpcodeArg arg[],
                           const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 dtlb = tcg_constant_i32(par[0]);

    gen_helper_wtlb(cpu_env, arg[0].in, arg[1].in, dtlb);
#endif
}

static void translate_wptlb(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_wptlb(cpu_env, arg[0].in, arg[1].in);
#endif
}

static void translate_wer(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    gen_helper_wer(cpu_env, arg[0].in, arg[1].in);
}

static void translate_wrmsk_expstate(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
    /* TODO: GPIO32 may be a part of coprocessor */
    tcg_gen_and_i32(cpu_UR[EXPSTATE], arg[0].in, arg[1].in);
}

static void translate_wsr(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    if (sr_name[par[0]]) {
        tcg_gen_mov_i32(cpu_SR[par[0]], arg[0].in);
    }
}

static void translate_wsr_mask(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    if (sr_name[par[0]]) {
        tcg_gen_andi_i32(cpu_SR[par[0]], arg[0].in, par[2]);
    }
}

static void translate_wsr_acchi(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    tcg_gen_ext8s_i32(cpu_SR[par[0]], arg[0].in);
}

static void translate_wsr_ccompare(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    uint32_t id = par[0] - CCOMPARE;

    assert(id < dc->config->nccompare);
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    tcg_gen_mov_i32(cpu_SR[par[0]], arg[0].in);
    gen_helper_update_ccompare(cpu_env, tcg_constant_i32(id));
#endif
}

static void translate_wsr_ccount(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }
    gen_helper_wsr_ccount(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_dbreaka(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    unsigned id = par[0] - DBREAKA;

    assert(id < dc->config->ndbreak);
    gen_helper_wsr_dbreaka(cpu_env, tcg_constant_i32(id), arg[0].in);
#endif
}

static void translate_wsr_dbreakc(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    unsigned id = par[0] - DBREAKC;

    assert(id < dc->config->ndbreak);
    gen_helper_wsr_dbreakc(cpu_env, tcg_constant_i32(id), arg[0].in);
#endif
}

static void translate_wsr_ibreaka(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    unsigned id = par[0] - IBREAKA;

    assert(id < dc->config->nibreak);
    gen_helper_wsr_ibreaka(cpu_env, tcg_constant_i32(id), arg[0].in);
#endif
}

static void translate_wsr_ibreakenable(DisasContext *dc, const OpcodeArg arg[],
                                       const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_wsr_ibreakenable(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_icount(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    if (dc->icount) {
        tcg_gen_mov_i32(dc->next_icount, arg[0].in);
    } else {
        tcg_gen_mov_i32(cpu_SR[par[0]], arg[0].in);
    }
#endif
}

static void translate_wsr_intclear(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_intclear(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_intset(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_intset(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_memctl(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_wsr_memctl(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_mpuenb(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_wsr_mpuenb(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_ps(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    uint32_t mask = PS_WOE | PS_CALLINC | PS_OWB |
        PS_UM | PS_EXCM | PS_INTLEVEL;

    if (option_enabled(dc, XTENSA_OPTION_MMU) ||
        option_enabled(dc, XTENSA_OPTION_MPU)) {
        mask |= PS_RING;
    }
    tcg_gen_andi_i32(cpu_SR[par[0]], arg[0].in, mask);
#endif
}

static void translate_wsr_rasid(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    gen_helper_wsr_rasid(cpu_env, arg[0].in);
#endif
}

static void translate_wsr_sar(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    tcg_gen_andi_i32(cpu_SR[par[0]], arg[0].in, 0x3f);
    if (dc->sar_m32_5bit) {
        tcg_gen_discard_i32(dc->sar_m32);
    }
    dc->sar_5bit = false;
    dc->sar_m32_5bit = false;
}

static void translate_wsr_windowbase(DisasContext *dc, const OpcodeArg arg[],
                                     const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_mov_i32(cpu_windowbase_next, arg[0].in);
#endif
}

static void translate_wsr_windowstart(DisasContext *dc, const OpcodeArg arg[],
                                      const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_andi_i32(cpu_SR[par[0]], arg[0].in,
                     (1 << dc->config->nareg / 4) - 1);
#endif
}

static void translate_wur(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_mov_i32(cpu_UR[par[0]], arg[0].in);
}

static void translate_xor(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    tcg_gen_xor_i32(arg[0].out, arg[1].in, arg[2].in);
}

static void translate_xsr(DisasContext *dc, const OpcodeArg arg[],
                          const uint32_t par[])
{
    if (sr_name[par[0]]) {
        TCGv_i32 tmp = tcg_temp_new_i32();

        tcg_gen_mov_i32(tmp, arg[0].in);
        tcg_gen_mov_i32(arg[0].out, cpu_SR[par[0]]);
        tcg_gen_mov_i32(cpu_SR[par[0]], tmp);
    } else {
        tcg_gen_movi_i32(arg[0].out, 0);
    }
}

static void translate_xsr_mask(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    if (sr_name[par[0]]) {
        TCGv_i32 tmp = tcg_temp_new_i32();

        tcg_gen_mov_i32(tmp, arg[0].in);
        tcg_gen_mov_i32(arg[0].out, cpu_SR[par[0]]);
        tcg_gen_andi_i32(cpu_SR[par[0]], tmp, par[2]);
    } else {
        tcg_gen_movi_i32(arg[0].out, 0);
    }
}

static void translate_xsr_ccount(DisasContext *dc, const OpcodeArg arg[],
                                 const uint32_t par[])
{
#ifndef CONFIG_USER_ONLY
    TCGv_i32 tmp = tcg_temp_new_i32();

    if (tb_cflags(dc->base.tb) & CF_USE_ICOUNT) {
        gen_io_start();
    }

    gen_helper_update_ccount(cpu_env);
    tcg_gen_mov_i32(tmp, cpu_SR[par[0]]);
    gen_helper_wsr_ccount(cpu_env, arg[0].in);
    tcg_gen_mov_i32(arg[0].out, tmp);

#endif
}

#define gen_translate_xsr(name) \
    static void translate_xsr_##name(DisasContext *dc, const OpcodeArg arg[], \
                                     const uint32_t par[]) \
{ \
    TCGv_i32 tmp = tcg_temp_new_i32(); \
 \
    if (sr_name[par[0]]) { \
        tcg_gen_mov_i32(tmp, cpu_SR[par[0]]); \
    } else { \
        tcg_gen_movi_i32(tmp, 0); \
    } \
    translate_wsr_##name(dc, arg, par); \
    tcg_gen_mov_i32(arg[0].out, tmp); \
}

gen_translate_xsr(acchi)
gen_translate_xsr(ccompare)
gen_translate_xsr(dbreaka)
gen_translate_xsr(dbreakc)
gen_translate_xsr(ibreaka)
gen_translate_xsr(ibreakenable)
gen_translate_xsr(icount)
gen_translate_xsr(memctl)
gen_translate_xsr(mpuenb)
gen_translate_xsr(ps)
gen_translate_xsr(rasid)
gen_translate_xsr(sar)
gen_translate_xsr(windowbase)
gen_translate_xsr(windowstart)

#undef gen_translate_xsr

static const XtensaOpcodeOps core_ops[] = {
    {
        .name = "abs",
        .translate = translate_abs,
    }, {
        .name = (const char * const[]) {
            "add", "add.n", NULL,
        },
        .translate = translate_add,
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "addi", "addi.n", NULL,
        },
        .translate = translate_addi,
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = "addmi",
        .translate = translate_addi,
    }, {
        .name = "addx2",
        .translate = translate_addx,
        .par = (const uint32_t[]){1},
    }, {
        .name = "addx4",
        .translate = translate_addx,
        .par = (const uint32_t[]){2},
    }, {
        .name = "addx8",
        .translate = translate_addx,
        .par = (const uint32_t[]){3},
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
        .name = (const char * const[]) {
            "ball", "ball.w15", "ball.w18", NULL,
        },
        .translate = translate_ball,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bany", "bany.w15", "bany.w18", NULL,
        },
        .translate = translate_bany,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bbc", "bbc.w15", "bbc.w18", NULL,
        },
        .translate = translate_bb,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bbci", "bbci.w15", "bbci.w18", NULL,
        },
        .translate = translate_bbi,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bbs", "bbs.w15", "bbs.w18", NULL,
        },
        .translate = translate_bb,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bbsi", "bbsi.w15", "bbsi.w18", NULL,
        },
        .translate = translate_bbi,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "beq", "beq.w15", "beq.w18", NULL,
        },
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "beqi", "beqi.w15", "beqi.w18", NULL,
        },
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "beqz", "beqz.n", "beqz.w15", "beqz.w18", NULL,
        },
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = "bf",
        .translate = translate_bp,
        .par = (const uint32_t[]){TCG_COND_EQ},
    }, {
        .name = (const char * const[]) {
            "bge", "bge.w15", "bge.w18", NULL,
        },
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_GE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bgei", "bgei.w15", "bgei.w18", NULL,
        },
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_GE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bgeu", "bgeu.w15", "bgeu.w18", NULL,
        },
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_GEU},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bgeui", "bgeui.w15", "bgeui.w18", NULL,
        },
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_GEU},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bgez", "bgez.w15", "bgez.w18", NULL,
        },
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_GE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "blt", "blt.w15", "blt.w18", NULL,
        },
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_LT},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "blti", "blti.w15", "blti.w18", NULL,
        },
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_LT},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bltu", "bltu.w15", "bltu.w18", NULL,
        },
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_LTU},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bltui", "bltui.w15", "bltui.w18", NULL,
        },
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_LTU},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bltz", "bltz.w15", "bltz.w18", NULL,
        },
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_LT},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bnall", "bnall.w15", "bnall.w18", NULL,
        },
        .translate = translate_ball,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bne", "bne.w15", "bne.w18", NULL,
        },
        .translate = translate_b,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bnei", "bnei.w15", "bnei.w18", NULL,
        },
        .translate = translate_bi,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bnez", "bnez.n", "bnez.w15", "bnez.w18", NULL,
        },
        .translate = translate_bz,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "bnone", "bnone.w15", "bnone.w18", NULL,
        },
        .translate = translate_bany,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .op_flags = XTENSA_OP_NAME_ARRAY,
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
        .par = (const uint32_t[]){3},
    }, {
        .name = "call4",
        .translate = translate_callw,
        .par = (const uint32_t[]){1},
    }, {
        .name = "call8",
        .translate = translate_callw,
        .par = (const uint32_t[]){2},
    }, {
        .name = "callx0",
        .translate = translate_callx0,
    }, {
        .name = "callx12",
        .translate = translate_callxw,
        .par = (const uint32_t[]){3},
    }, {
        .name = "callx4",
        .translate = translate_callxw,
        .par = (const uint32_t[]){1},
    }, {
        .name = "callx8",
        .translate = translate_callxw,
        .par = (const uint32_t[]){2},
    }, {
        .name = "clamps",
        .translate = translate_clamps,
    }, {
        .name = "clrb_expstate",
        .translate = translate_clrb_expstate,
    }, {
        .name = "clrex",
        .translate = translate_clrex,
    }, {
        .name = "const16",
        .translate = translate_const16,
    }, {
        .name = "depbits",
        .translate = translate_depbits,
    }, {
        .name = "dhi",
        .translate = translate_dcache,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "dhi.b",
        .translate = translate_nop,
    }, {
        .name = "dhu",
        .translate = translate_dcache,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "dhwb",
        .translate = translate_dcache,
    }, {
        .name = "dhwb.b",
        .translate = translate_nop,
    }, {
        .name = "dhwbi",
        .translate = translate_dcache,
    }, {
        .name = "dhwbi.b",
        .translate = translate_nop,
    }, {
        .name = "dii",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "diu",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "diwb",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "diwbi",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "diwbui.p",
        .translate = translate_diwbuip,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "dpfl",
        .translate = translate_dcache,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "dpfm.b",
        .translate = translate_nop,
    }, {
        .name = "dpfm.bf",
        .translate = translate_nop,
    }, {
        .name = "dpfr",
        .translate = translate_nop,
    }, {
        .name = "dpfr.b",
        .translate = translate_nop,
    }, {
        .name = "dpfr.bf",
        .translate = translate_nop,
    }, {
        .name = "dpfro",
        .translate = translate_nop,
    }, {
        .name = "dpfw",
        .translate = translate_nop,
    }, {
        .name = "dpfw.b",
        .translate = translate_nop,
    }, {
        .name = "dpfw.bf",
        .translate = translate_nop,
    }, {
        .name = "dpfwo",
        .translate = translate_nop,
    }, {
        .name = "dsync",
        .translate = translate_nop,
    }, {
        .name = "entry",
        .translate = translate_entry,
        .test_exceptions = test_exceptions_entry,
        .test_overflow = test_overflow_entry,
        .op_flags = XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_SYNC_REGISTER_WINDOW,
    }, {
        .name = "esync",
        .translate = translate_nop,
    }, {
        .name = "excw",
        .translate = translate_nop,
    }, {
        .name = "extui",
        .translate = translate_extui,
    }, {
        .name = "extw",
        .translate = translate_memw,
    }, {
        .name = "getex",
        .translate = translate_getex,
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
    }, {
        .name = "ihi",
        .translate = translate_icache,
    }, {
        .name = "ihu",
        .translate = translate_icache,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "iii",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "iitlb",
        .translate = translate_itlb,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "iiu",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = (const char * const[]) {
            "ill", "ill.n", NULL,
        },
        .op_flags = XTENSA_OP_ILL | XTENSA_OP_NAME_ARRAY,
    }, {
        .name = "ipf",
        .translate = translate_nop,
    }, {
        .name = "ipfl",
        .translate = translate_icache,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "isync",
        .translate = translate_nop,
    }, {
        .name = "j",
        .translate = translate_j,
    }, {
        .name = "jx",
        .translate = translate_jx,
    }, {
        .name = "l16si",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TESW, false, false},
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "l16ui",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUW, false, false},
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "l32ai",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL | MO_ALIGN, true, false},
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "l32e",
        .translate = translate_l32e,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_LOAD,
    }, {
        .name = "l32ex",
        .translate = translate_l32ex,
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = (const char * const[]) {
            "l32i", "l32i.n", NULL,
        },
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, false},
        .op_flags = XTENSA_OP_NAME_ARRAY | XTENSA_OP_LOAD,
    }, {
        .name = "l32r",
        .translate = translate_l32r,
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "l8ui",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_UB, false, false},
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "ldct",
        .translate = translate_lct,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "ldcw",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_NONE, 0, -4},
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_NONE, 0, 4},
        .op_flags = XTENSA_OP_LOAD,
    }, {
        .name = "ldpte",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "lict",
        .translate = translate_lct,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "licw",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = (const char * const[]) {
            "loop", "loop.w15", NULL,
        },
        .translate = translate_loop,
        .par = (const uint32_t[]){TCG_COND_NEVER},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "loopgtz", "loopgtz.w15", NULL,
        },
        .translate = translate_loop,
        .par = (const uint32_t[]){TCG_COND_GT},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "loopnez", "loopnez.w15", NULL,
        },
        .translate = translate_loop,
        .par = (const uint32_t[]){TCG_COND_NE},
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = "max",
        .translate = translate_smax,
    }, {
        .name = "maxu",
        .translate = translate_umax,
    }, {
        .name = "memw",
        .translate = translate_memw,
    }, {
        .name = "min",
        .translate = translate_smin,
    }, {
        .name = "minu",
        .translate = translate_umin,
    }, {
        .name = (const char * const[]) {
            "mov", "mov.n", NULL,
        },
        .translate = translate_mov,
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = "moveqz",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_EQ},
    }, {
        .name = "movf",
        .translate = translate_movp,
        .par = (const uint32_t[]){TCG_COND_EQ},
    }, {
        .name = "movgez",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_GE},
    }, {
        .name = "movi",
        .translate = translate_movi,
    }, {
        .name = "movi.n",
        .translate = translate_movi,
    }, {
        .name = "movltz",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_LT},
    }, {
        .name = "movnez",
        .translate = translate_movcond,
        .par = (const uint32_t[]){TCG_COND_NE},
    }, {
        .name = "movsp",
        .translate = translate_movsp,
        .op_flags = XTENSA_OP_ALLOCA,
    }, {
        .name = "movt",
        .translate = translate_movp,
        .par = (const uint32_t[]){TCG_COND_NE},
    }, {
        .name = "mul.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HH, 0},
    }, {
        .name = "mul.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HL, 0},
    }, {
        .name = "mul.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LH, 0},
    }, {
        .name = "mul.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LL, 0},
    }, {
        .name = "mul.ad.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HH, 0},
    }, {
        .name = "mul.ad.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HL, 0},
    }, {
        .name = "mul.ad.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LH, 0},
    }, {
        .name = "mul.ad.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LL, 0},
    }, {
        .name = "mul.da.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HH, 0},
    }, {
        .name = "mul.da.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HL, 0},
    }, {
        .name = "mul.da.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LH, 0},
    }, {
        .name = "mul.da.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LL, 0},
    }, {
        .name = "mul.dd.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HH, 0},
    }, {
        .name = "mul.dd.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_HL, 0},
    }, {
        .name = "mul.dd.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LH, 0},
    }, {
        .name = "mul.dd.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MUL, MAC16_LL, 0},
    }, {
        .name = "mul16s",
        .translate = translate_mul16,
        .par = (const uint32_t[]){true},
    }, {
        .name = "mul16u",
        .translate = translate_mul16,
        .par = (const uint32_t[]){false},
    }, {
        .name = "mula.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, 0},
    }, {
        .name = "mula.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, 0},
    }, {
        .name = "mula.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, 0},
    }, {
        .name = "mula.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, 0},
    }, {
        .name = "mula.ad.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, 0},
    }, {
        .name = "mula.ad.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, 0},
    }, {
        .name = "mula.ad.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, 0},
    }, {
        .name = "mula.ad.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, 0},
    }, {
        .name = "mula.da.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, 0},
    }, {
        .name = "mula.da.hh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, -4},
    }, {
        .name = "mula.da.hh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, 4},
    }, {
        .name = "mula.da.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, 0},
    }, {
        .name = "mula.da.hl.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, -4},
    }, {
        .name = "mula.da.hl.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, 4},
    }, {
        .name = "mula.da.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, 0},
    }, {
        .name = "mula.da.lh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, -4},
    }, {
        .name = "mula.da.lh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, 4},
    }, {
        .name = "mula.da.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, 0},
    }, {
        .name = "mula.da.ll.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, -4},
    }, {
        .name = "mula.da.ll.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, 4},
    }, {
        .name = "mula.dd.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, 0},
    }, {
        .name = "mula.dd.hh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, -4},
    }, {
        .name = "mula.dd.hh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HH, 4},
    }, {
        .name = "mula.dd.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, 0},
    }, {
        .name = "mula.dd.hl.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, -4},
    }, {
        .name = "mula.dd.hl.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_HL, 4},
    }, {
        .name = "mula.dd.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, 0},
    }, {
        .name = "mula.dd.lh.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, -4},
    }, {
        .name = "mula.dd.lh.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LH, 4},
    }, {
        .name = "mula.dd.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, 0},
    }, {
        .name = "mula.dd.ll.lddec",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, -4},
    }, {
        .name = "mula.dd.ll.ldinc",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULA, MAC16_LL, 4},
    }, {
        .name = "mull",
        .translate = translate_mull,
    }, {
        .name = "muls.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HH, 0},
    }, {
        .name = "muls.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HL, 0},
    }, {
        .name = "muls.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LH, 0},
    }, {
        .name = "muls.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LL, 0},
    }, {
        .name = "muls.ad.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HH, 0},
    }, {
        .name = "muls.ad.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HL, 0},
    }, {
        .name = "muls.ad.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LH, 0},
    }, {
        .name = "muls.ad.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LL, 0},
    }, {
        .name = "muls.da.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HH, 0},
    }, {
        .name = "muls.da.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HL, 0},
    }, {
        .name = "muls.da.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LH, 0},
    }, {
        .name = "muls.da.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LL, 0},
    }, {
        .name = "muls.dd.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HH, 0},
    }, {
        .name = "muls.dd.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_HL, 0},
    }, {
        .name = "muls.dd.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LH, 0},
    }, {
        .name = "muls.dd.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_MULS, MAC16_LL, 0},
    }, {
        .name = "mulsh",
        .translate = translate_mulh,
        .par = (const uint32_t[]){true},
    }, {
        .name = "muluh",
        .translate = translate_mulh,
        .par = (const uint32_t[]){false},
    }, {
        .name = "neg",
        .translate = translate_neg,
    }, {
        .name = (const char * const[]) {
            "nop", "nop.n", NULL,
        },
        .translate = translate_nop,
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = "nsa",
        .translate = translate_nsa,
    }, {
        .name = "nsau",
        .translate = translate_nsau,
    }, {
        .name = "or",
        .translate = translate_or,
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
    }, {
        .name = "pfend.a",
        .translate = translate_nop,
    }, {
        .name = "pfend.o",
        .translate = translate_nop,
    }, {
        .name = "pfnxt.f",
        .translate = translate_nop,
    }, {
        .name = "pfwait.a",
        .translate = translate_nop,
    }, {
        .name = "pfwait.r",
        .translate = translate_nop,
    }, {
        .name = "pitlb",
        .translate = translate_ptlb,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "pptlb",
        .translate = translate_pptlb,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "quos",
        .translate = translate_quos,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
    }, {
        .name = "quou",
        .translate = translate_quou,
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
    }, {
        .name = "rdtlb0",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){true, 0},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rdtlb1",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){true, 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "read_impwire",
        .translate = translate_read_impwire,
    }, {
        .name = "rems",
        .translate = translate_quos,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
    }, {
        .name = "remu",
        .translate = translate_remu,
        .op_flags = XTENSA_OP_DIVIDE_BY_ZERO,
    }, {
        .name = "rer",
        .translate = translate_rer,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = (const char * const[]) {
            "ret", "ret.n", NULL,
        },
        .translate = translate_ret,
        .op_flags = XTENSA_OP_NAME_ARRAY,
    }, {
        .name = (const char * const[]) {
            "retw", "retw.n", NULL,
        },
        .translate = translate_retw,
        .test_exceptions = test_exceptions_retw,
        .op_flags = XTENSA_OP_UNDERFLOW | XTENSA_OP_NAME_ARRAY,
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
    }, {
        .name = "ritlb1",
        .translate = translate_rtlb,
        .par = (const uint32_t[]){false, 1},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rptlb0",
        .translate = translate_rptlb0,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rptlb1",
        .translate = translate_rptlb1,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rotw",
        .translate = translate_rotw,
        .op_flags = XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_SYNC_REGISTER_WINDOW,
    }, {
        .name = "rsil",
        .translate = translate_rsil,
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "rsr.176",
        .translate = translate_rsr,
        .par = (const uint32_t[]){176},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.208",
        .translate = translate_rsr,
        .par = (const uint32_t[]){208},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.acchi",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ACCHI,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "rsr.acclo",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ACCLO,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "rsr.atomctl",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ATOMCTL,
            XTENSA_OPTION_ATOMCTL,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.br",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            BR,
            XTENSA_OPTION_BOOLEAN,
        },
    }, {
        .name = "rsr.cacheadrdis",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CACHEADRDIS,
            XTENSA_OPTION_MPU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.cacheattr",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CACHEATTR,
            XTENSA_OPTION_CACHEATTR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ccompare0",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ccompare1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE + 1,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ccompare2",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE + 2,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ccount",
        .translate = translate_rsr_ccount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CCOUNT,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "rsr.configid0",
        .translate = translate_rsr,
        .par = (const uint32_t[]){CONFIGID0},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.configid1",
        .translate = translate_rsr,
        .par = (const uint32_t[]){CONFIGID1},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.cpenable",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CPENABLE,
            XTENSA_OPTION_COPROCESSOR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.dbreaka0",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKA,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.dbreaka1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKA + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.dbreakc0",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKC,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.dbreakc1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKC + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ddr",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DDR,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.debugcause",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DEBUGCAUSE,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.depc",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DEPC,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.dtlbcfg",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DTLBCFG,
            XTENSA_OPTION_MMU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EPC1,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc2",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc3",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc4",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc5",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc6",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.epc7",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 6,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eps2",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eps3",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eps4",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eps5",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eps6",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eps7",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.eraccess",
        .translate = translate_rsr,
        .par = (const uint32_t[]){ERACCESS},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.exccause",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCCAUSE,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCSAVE1,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave2",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave3",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave4",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave5",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave6",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excsave7",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 6,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.excvaddr",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCVADDR,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ibreaka0",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_ibreak,
        .par = (const uint32_t[]){
            IBREAKA,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ibreaka1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_ibreak,
        .par = (const uint32_t[]){
            IBREAKA + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ibreakenable",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            IBREAKENABLE,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.icount",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ICOUNT,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.icountlevel",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ICOUNTLEVEL,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.intclear",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTCLEAR,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.intenable",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTENABLE,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.interrupt",
        .translate = translate_rsr_ccount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTSET,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "rsr.intset",
        .translate = translate_rsr_ccount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTSET,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "rsr.itlbcfg",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ITLBCFG,
            XTENSA_OPTION_MMU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.lbeg",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LBEG,
            XTENSA_OPTION_LOOP,
        },
    }, {
        .name = "rsr.lcount",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LCOUNT,
            XTENSA_OPTION_LOOP,
        },
    }, {
        .name = "rsr.lend",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LEND,
            XTENSA_OPTION_LOOP,
        },
    }, {
        .name = "rsr.litbase",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LITBASE,
            XTENSA_OPTION_EXTENDED_L32R,
        },
    }, {
        .name = "rsr.m0",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "rsr.m1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 1,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "rsr.m2",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 2,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "rsr.m3",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 3,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "rsr.memctl",
        .translate = translate_rsr,
        .par = (const uint32_t[]){MEMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mecr",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MECR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mepc",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MEPC,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.meps",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MEPS,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mesave",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESAVE,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mesr",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mevaddr",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.misc0",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.misc1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 1,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.misc2",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 2,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.misc3",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 3,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mpucfg",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MPUCFG,
            XTENSA_OPTION_MPU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.mpuenb",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MPUENB,
            XTENSA_OPTION_MPU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.prefctl",
        .translate = translate_rsr,
        .par = (const uint32_t[]){PREFCTL},
    }, {
        .name = "rsr.prid",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PRID,
            XTENSA_OPTION_PROCESSOR_ID,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ps",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PS,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.ptevaddr",
        .translate = translate_rsr_ptevaddr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PTEVADDR,
            XTENSA_OPTION_MMU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.rasid",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            RASID,
            XTENSA_OPTION_MMU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.sar",
        .translate = translate_rsr,
        .par = (const uint32_t[]){SAR},
    }, {
        .name = "rsr.scompare1",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            SCOMPARE1,
            XTENSA_OPTION_CONDITIONAL_STORE,
        },
    }, {
        .name = "rsr.vecbase",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            VECBASE,
            XTENSA_OPTION_RELOCATABLE_VECTOR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.windowbase",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            WINDOW_BASE,
            XTENSA_OPTION_WINDOWED_REGISTER,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsr.windowstart",
        .translate = translate_rsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            WINDOW_START,
            XTENSA_OPTION_WINDOWED_REGISTER,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "rsync",
        .translate = translate_nop,
    }, {
        .name = "rur.expstate",
        .translate = translate_rur,
        .par = (const uint32_t[]){EXPSTATE},
    }, {
        .name = "rur.threadptr",
        .translate = translate_rur,
        .par = (const uint32_t[]){THREADPTR},
    }, {
        .name = "s16i",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUW, false, true},
        .op_flags = XTENSA_OP_STORE,
    }, {
        .name = "s32c1i",
        .translate = translate_s32c1i,
        .op_flags = XTENSA_OP_LOAD | XTENSA_OP_STORE,
    }, {
        .name = "s32e",
        .translate = translate_s32e,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_STORE,
    }, {
        .name = "s32ex",
        .translate = translate_s32ex,
        .op_flags = XTENSA_OP_LOAD | XTENSA_OP_STORE,
    }, {
        .name = (const char * const[]) {
            "s32i", "s32i.n", "s32nb", NULL,
        },
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL, false, true},
        .op_flags = XTENSA_OP_NAME_ARRAY | XTENSA_OP_STORE,
    }, {
        .name = "s32ri",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_TEUL | MO_ALIGN, true, true},
        .op_flags = XTENSA_OP_STORE,
    }, {
        .name = "s8i",
        .translate = translate_ldst,
        .par = (const uint32_t[]){MO_UB, false, true},
        .op_flags = XTENSA_OP_STORE,
    }, {
        .name = "salt",
        .translate = translate_salt,
        .par = (const uint32_t[]){TCG_COND_LT},
    }, {
        .name = "saltu",
        .translate = translate_salt,
        .par = (const uint32_t[]){TCG_COND_LTU},
    }, {
        .name = "sdct",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "sdcw",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "setb_expstate",
        .translate = translate_setb_expstate,
    }, {
        .name = "sext",
        .translate = translate_sext,
    }, {
        .name = "sict",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "sicw",
        .translate = translate_nop,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "simcall",
        .translate = translate_simcall,
        .test_exceptions = test_exceptions_simcall,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "sll",
        .translate = translate_sll,
    }, {
        .name = "slli",
        .translate = translate_slli,
    }, {
        .name = "sra",
        .translate = translate_sra,
    }, {
        .name = "srai",
        .translate = translate_srai,
    }, {
        .name = "src",
        .translate = translate_src,
    }, {
        .name = "srl",
        .translate = translate_srl,
    }, {
        .name = "srli",
        .translate = translate_srli,
    }, {
        .name = "ssa8b",
        .translate = translate_ssa8b,
    }, {
        .name = "ssa8l",
        .translate = translate_ssa8l,
    }, {
        .name = "ssai",
        .translate = translate_ssai,
    }, {
        .name = "ssl",
        .translate = translate_ssl,
    }, {
        .name = "ssr",
        .translate = translate_ssr,
    }, {
        .name = "sub",
        .translate = translate_sub,
    }, {
        .name = "subx2",
        .translate = translate_subx,
        .par = (const uint32_t[]){1},
    }, {
        .name = "subx4",
        .translate = translate_subx,
        .par = (const uint32_t[]){2},
    }, {
        .name = "subx8",
        .translate = translate_subx,
        .par = (const uint32_t[]){3},
    }, {
        .name = "syscall",
        .op_flags = XTENSA_OP_SYSCALL,
    }, {
        .name = "umul.aa.hh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_HH, 0},
    }, {
        .name = "umul.aa.hl",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_HL, 0},
    }, {
        .name = "umul.aa.lh",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_LH, 0},
    }, {
        .name = "umul.aa.ll",
        .translate = translate_mac16,
        .par = (const uint32_t[]){MAC16_UMUL, MAC16_LL, 0},
    }, {
        .name = "waiti",
        .translate = translate_waiti,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wdtlb",
        .translate = translate_wtlb,
        .par = (const uint32_t[]){true},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wer",
        .translate = translate_wer,
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "witlb",
        .translate = translate_wtlb,
        .par = (const uint32_t[]){false},
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wptlb",
        .translate = translate_wptlb,
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wrmsk_expstate",
        .translate = translate_wrmsk_expstate,
    }, {
        .name = "wsr.176",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "wsr.208",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "wsr.acchi",
        .translate = translate_wsr_acchi,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ACCHI,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "wsr.acclo",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ACCLO,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "wsr.atomctl",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ATOMCTL,
            XTENSA_OPTION_ATOMCTL,
            0x3f,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.br",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            BR,
            XTENSA_OPTION_BOOLEAN,
            0xffff,
        },
    }, {
        .name = "wsr.cacheadrdis",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CACHEADRDIS,
            XTENSA_OPTION_MPU,
            0xff,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.cacheattr",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CACHEATTR,
            XTENSA_OPTION_CACHEATTR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.ccompare0",
        .translate = translate_wsr_ccompare,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.ccompare1",
        .translate = translate_wsr_ccompare,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE + 1,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.ccompare2",
        .translate = translate_wsr_ccompare,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE + 2,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.ccount",
        .translate = translate_wsr_ccount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CCOUNT,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.configid0",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "wsr.configid1",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "wsr.cpenable",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CPENABLE,
            XTENSA_OPTION_COPROCESSOR,
            0xff,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.dbreaka0",
        .translate = translate_wsr_dbreaka,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKA,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.dbreaka1",
        .translate = translate_wsr_dbreaka,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKA + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.dbreakc0",
        .translate = translate_wsr_dbreakc,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKC,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.dbreakc1",
        .translate = translate_wsr_dbreakc,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKC + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.ddr",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DDR,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.debugcause",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "wsr.depc",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DEPC,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.dtlbcfg",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DTLBCFG,
            XTENSA_OPTION_MMU,
            0x01130000,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc1",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EPC1,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc2",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc3",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc4",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc5",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc6",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.epc7",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 6,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eps2",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eps3",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eps4",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eps5",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eps6",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eps7",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.eraccess",
        .translate = translate_wsr_mask,
        .par = (const uint32_t[]){
            ERACCESS,
            0,
            0xffff,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.exccause",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCCAUSE,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave1",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCSAVE1,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave2",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave3",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave4",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave5",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave6",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excsave7",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 6,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.excvaddr",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCVADDR,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.ibreaka0",
        .translate = translate_wsr_ibreaka,
        .test_exceptions = test_exceptions_ibreak,
        .par = (const uint32_t[]){
            IBREAKA,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.ibreaka1",
        .translate = translate_wsr_ibreaka,
        .test_exceptions = test_exceptions_ibreak,
        .par = (const uint32_t[]){
            IBREAKA + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.ibreakenable",
        .translate = translate_wsr_ibreakenable,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            IBREAKENABLE,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "wsr.icount",
        .translate = translate_wsr_icount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ICOUNT,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.icountlevel",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ICOUNTLEVEL,
            XTENSA_OPTION_DEBUG,
            0xf,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.intclear",
        .translate = translate_wsr_intclear,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTCLEAR,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "wsr.intenable",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTENABLE,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "wsr.interrupt",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTSET,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "wsr.intset",
        .translate = translate_wsr_intset,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTSET,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "wsr.itlbcfg",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ITLBCFG,
            XTENSA_OPTION_MMU,
            0x01130000,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.lbeg",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LBEG,
            XTENSA_OPTION_LOOP,
        },
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.lcount",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LCOUNT,
            XTENSA_OPTION_LOOP,
        },
    }, {
        .name = "wsr.lend",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LEND,
            XTENSA_OPTION_LOOP,
        },
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.litbase",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LITBASE,
            XTENSA_OPTION_EXTENDED_L32R,
            0xfffff001,
        },
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.m0",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "wsr.m1",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 1,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "wsr.m2",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 2,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "wsr.m3",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 3,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "wsr.memctl",
        .translate = translate_wsr_memctl,
        .par = (const uint32_t[]){MEMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mecr",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MECR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mepc",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MEPC,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.meps",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MEPS,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mesave",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESAVE,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mesr",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mevaddr",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.misc0",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.misc1",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 1,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.misc2",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 2,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.misc3",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 3,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mmid",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MMID,
            XTENSA_OPTION_TRACE_PORT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.mpuenb",
        .translate = translate_wsr_mpuenb,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MPUENB,
            XTENSA_OPTION_MPU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.prefctl",
        .translate = translate_wsr,
        .par = (const uint32_t[]){PREFCTL},
    }, {
        .name = "wsr.prid",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "wsr.ps",
        .translate = translate_wsr_ps,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PS,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "wsr.ptevaddr",
        .translate = translate_wsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PTEVADDR,
            XTENSA_OPTION_MMU,
            0xffc00000,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.rasid",
        .translate = translate_wsr_rasid,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            RASID,
            XTENSA_OPTION_MMU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wsr.sar",
        .translate = translate_wsr_sar,
        .par = (const uint32_t[]){SAR},
    }, {
        .name = "wsr.scompare1",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            SCOMPARE1,
            XTENSA_OPTION_CONDITIONAL_STORE,
        },
    }, {
        .name = "wsr.vecbase",
        .translate = translate_wsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            VECBASE,
            XTENSA_OPTION_RELOCATABLE_VECTOR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "wsr.windowbase",
        .translate = translate_wsr_windowbase,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            WINDOW_BASE,
            XTENSA_OPTION_WINDOWED_REGISTER,
        },
        .op_flags = XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_SYNC_REGISTER_WINDOW,
    }, {
        .name = "wsr.windowstart",
        .translate = translate_wsr_windowstart,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            WINDOW_START,
            XTENSA_OPTION_WINDOWED_REGISTER,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "wur.expstate",
        .translate = translate_wur,
        .par = (const uint32_t[]){EXPSTATE},
    }, {
        .name = "wur.threadptr",
        .translate = translate_wur,
        .par = (const uint32_t[]){THREADPTR},
    }, {
        .name = "xor",
        .translate = translate_xor,
    }, {
        .name = "xorb",
        .translate = translate_boolean,
        .par = (const uint32_t[]){BOOLEAN_XOR},
    }, {
        .name = "xsr.176",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.208",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.acchi",
        .translate = translate_xsr_acchi,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ACCHI,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "xsr.acclo",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ACCLO,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "xsr.atomctl",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ATOMCTL,
            XTENSA_OPTION_ATOMCTL,
            0x3f,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.br",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            BR,
            XTENSA_OPTION_BOOLEAN,
            0xffff,
        },
    }, {
        .name = "xsr.cacheadrdis",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CACHEADRDIS,
            XTENSA_OPTION_MPU,
            0xff,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.cacheattr",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CACHEATTR,
            XTENSA_OPTION_CACHEATTR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.ccompare0",
        .translate = translate_xsr_ccompare,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.ccompare1",
        .translate = translate_xsr_ccompare,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE + 1,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.ccompare2",
        .translate = translate_xsr_ccompare,
        .test_exceptions = test_exceptions_ccompare,
        .par = (const uint32_t[]){
            CCOMPARE + 2,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.ccount",
        .translate = translate_xsr_ccount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CCOUNT,
            XTENSA_OPTION_TIMER_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.configid0",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.configid1",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.cpenable",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            CPENABLE,
            XTENSA_OPTION_COPROCESSOR,
            0xff,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.dbreaka0",
        .translate = translate_xsr_dbreaka,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKA,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.dbreaka1",
        .translate = translate_xsr_dbreaka,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKA + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.dbreakc0",
        .translate = translate_xsr_dbreakc,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKC,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.dbreakc1",
        .translate = translate_xsr_dbreakc,
        .test_exceptions = test_exceptions_dbreak,
        .par = (const uint32_t[]){
            DBREAKC + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.ddr",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DDR,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.debugcause",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.depc",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DEPC,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.dtlbcfg",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            DTLBCFG,
            XTENSA_OPTION_MMU,
            0x01130000,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc1",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EPC1,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc2",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc3",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc4",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc5",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc6",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.epc7",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPC1 + 6,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eps2",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eps3",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eps4",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eps5",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eps6",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eps7",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EPS2 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.eraccess",
        .translate = translate_xsr_mask,
        .par = (const uint32_t[]){
            ERACCESS,
            0,
            0xffff,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.exccause",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCCAUSE,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave1",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCSAVE1,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave2",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 1,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave3",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 2,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave4",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 3,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave5",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 4,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave6",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 5,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excsave7",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_hpi,
        .par = (const uint32_t[]){
            EXCSAVE1 + 6,
            XTENSA_OPTION_HIGH_PRIORITY_INTERRUPT,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.excvaddr",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            EXCVADDR,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.ibreaka0",
        .translate = translate_xsr_ibreaka,
        .test_exceptions = test_exceptions_ibreak,
        .par = (const uint32_t[]){
            IBREAKA,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.ibreaka1",
        .translate = translate_xsr_ibreaka,
        .test_exceptions = test_exceptions_ibreak,
        .par = (const uint32_t[]){
            IBREAKA + 1,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.ibreakenable",
        .translate = translate_xsr_ibreakenable,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            IBREAKENABLE,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_0,
    }, {
        .name = "xsr.icount",
        .translate = translate_xsr_icount,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ICOUNT,
            XTENSA_OPTION_DEBUG,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.icountlevel",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ICOUNTLEVEL,
            XTENSA_OPTION_DEBUG,
            0xf,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.intclear",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.intenable",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            INTENABLE,
            XTENSA_OPTION_INTERRUPT,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_0 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "xsr.interrupt",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.intset",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.itlbcfg",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            ITLBCFG,
            XTENSA_OPTION_MMU,
            0x01130000,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.lbeg",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LBEG,
            XTENSA_OPTION_LOOP,
        },
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.lcount",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LCOUNT,
            XTENSA_OPTION_LOOP,
        },
    }, {
        .name = "xsr.lend",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LEND,
            XTENSA_OPTION_LOOP,
        },
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.litbase",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            LITBASE,
            XTENSA_OPTION_EXTENDED_L32R,
            0xfffff001,
        },
        .op_flags = XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.m0",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "xsr.m1",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 1,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "xsr.m2",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 2,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "xsr.m3",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MR + 3,
            XTENSA_OPTION_MAC16,
        },
    }, {
        .name = "xsr.memctl",
        .translate = translate_xsr_memctl,
        .par = (const uint32_t[]){MEMCTL},
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.mecr",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MECR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.mepc",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MEPC,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.meps",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MEPS,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.mesave",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESAVE,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.mesr",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.mevaddr",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MESR,
            XTENSA_OPTION_MEMORY_ECC_PARITY,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.misc0",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.misc1",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 1,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.misc2",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 2,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.misc3",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MISC + 3,
            XTENSA_OPTION_MISC_SR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.mpuenb",
        .translate = translate_xsr_mpuenb,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            MPUENB,
            XTENSA_OPTION_MPU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.prefctl",
        .translate = translate_xsr,
        .par = (const uint32_t[]){PREFCTL},
    }, {
        .name = "xsr.prid",
        .op_flags = XTENSA_OP_ILL,
    }, {
        .name = "xsr.ps",
        .translate = translate_xsr_ps,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PS,
            XTENSA_OPTION_EXCEPTION,
        },
        .op_flags =
            XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_CHECK_INTERRUPTS,
    }, {
        .name = "xsr.ptevaddr",
        .translate = translate_xsr_mask,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            PTEVADDR,
            XTENSA_OPTION_MMU,
            0xffc00000,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.rasid",
        .translate = translate_xsr_rasid,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            RASID,
            XTENSA_OPTION_MMU,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    }, {
        .name = "xsr.sar",
        .translate = translate_xsr_sar,
        .par = (const uint32_t[]){SAR},
    }, {
        .name = "xsr.scompare1",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            SCOMPARE1,
            XTENSA_OPTION_CONDITIONAL_STORE,
        },
    }, {
        .name = "xsr.vecbase",
        .translate = translate_xsr,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            VECBASE,
            XTENSA_OPTION_RELOCATABLE_VECTOR,
        },
        .op_flags = XTENSA_OP_PRIVILEGED,
    }, {
        .name = "xsr.windowbase",
        .translate = translate_xsr_windowbase,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            WINDOW_BASE,
            XTENSA_OPTION_WINDOWED_REGISTER,
        },
        .op_flags = XTENSA_OP_PRIVILEGED |
            XTENSA_OP_EXIT_TB_M1 |
            XTENSA_OP_SYNC_REGISTER_WINDOW,
    }, {
        .name = "xsr.windowstart",
        .translate = translate_xsr_windowstart,
        .test_exceptions = test_exceptions_sr,
        .par = (const uint32_t[]){
            WINDOW_START,
            XTENSA_OPTION_WINDOWED_REGISTER,
        },
        .op_flags = XTENSA_OP_PRIVILEGED | XTENSA_OP_EXIT_TB_M1,
    },
};

const XtensaOpcodeTranslators xtensa_core_opcodes = {
    .num_opcodes = ARRAY_SIZE(core_ops),
    .opcode = core_ops,
};


static inline void get_f32_o1_i3(const OpcodeArg *arg, OpcodeArg *arg32,
                                 int o0, int i0, int i1, int i2)
{
    if ((i0 >= 0 && arg[i0].num_bits == 64) ||
        (o0 >= 0 && arg[o0].num_bits == 64)) {
        if (o0 >= 0) {
            arg32[o0].out = tcg_temp_new_i32();
        }
        if (i0 >= 0) {
            arg32[i0].in = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(arg32[i0].in, arg[i0].in);
        }
        if (i1 >= 0) {
            arg32[i1].in = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(arg32[i1].in, arg[i1].in);
        }
        if (i2 >= 0) {
            arg32[i2].in = tcg_temp_new_i32();
            tcg_gen_extrl_i64_i32(arg32[i2].in, arg[i2].in);
        }
    } else {
        if (o0 >= 0) {
            arg32[o0].out = arg[o0].out;
        }
        if (i0 >= 0) {
            arg32[i0].in = arg[i0].in;
        }
        if (i1 >= 0) {
            arg32[i1].in = arg[i1].in;
        }
        if (i2 >= 0) {
            arg32[i2].in = arg[i2].in;
        }
    }
}

static inline void put_f32_o1_i3(const OpcodeArg *arg, const OpcodeArg *arg32,
                                 int o0, int i0, int i1, int i2)
{
    if ((i0 >= 0 && arg[i0].num_bits == 64) ||
        (o0 >= 0 && arg[o0].num_bits == 64)) {
        if (o0 >= 0) {
            tcg_gen_extu_i32_i64(arg[o0].out, arg32[o0].out);
        }
    }
}

static inline void get_f32_o1_i2(const OpcodeArg *arg, OpcodeArg *arg32,
                                 int o0, int i0, int i1)
{
    get_f32_o1_i3(arg, arg32, o0, i0, i1, -1);
}

static inline void put_f32_o1_i2(const OpcodeArg *arg, const OpcodeArg *arg32,
                                 int o0, int i0, int i1)
{
    put_f32_o1_i3(arg, arg32, o0, i0, i1, -1);
}

static inline void get_f32_o1_i1(const OpcodeArg *arg, OpcodeArg *arg32,
                                 int o0, int i0)
{
    get_f32_o1_i2(arg, arg32, o0, i0, -1);
}

static inline void put_f32_o1_i1(const OpcodeArg *arg, const OpcodeArg *arg32,
                                 int o0, int i0)
{
    put_f32_o1_i2(arg, arg32, o0, i0, -1);
}

static inline void get_f32_o1(const OpcodeArg *arg, OpcodeArg *arg32,
                              int o0)
{
    get_f32_o1_i1(arg, arg32, o0, -1);
}

static inline void put_f32_o1(const OpcodeArg *arg, const OpcodeArg *arg32,
                              int o0)
{
    put_f32_o1_i1(arg, arg32, o0, -1);
}

static inline void get_f32_i2(const OpcodeArg *arg, OpcodeArg *arg32,
                              int i0, int i1)
{
    get_f32_o1_i2(arg, arg32, -1, i0, i1);
}

static inline void put_f32_i2(const OpcodeArg *arg, const OpcodeArg *arg32,
                              int i0, int i1)
{
    put_f32_o1_i2(arg, arg32, -1, i0, i1);
}

static inline void get_f32_i1(const OpcodeArg *arg, OpcodeArg *arg32,
                              int i0)
{
    get_f32_i2(arg, arg32, i0, -1);
}

static inline void put_f32_i1(const OpcodeArg *arg, const OpcodeArg *arg32,
                              int i0)
{
    put_f32_i2(arg, arg32, i0, -1);
}


static void translate_abs_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    gen_helper_abs_d(arg[0].out, arg[1].in);
}

static void translate_abs_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    OpcodeArg arg32[2];

    get_f32_o1_i1(arg, arg32, 0, 1);
    gen_helper_abs_s(arg32[0].out, arg32[1].in);
    put_f32_o1_i1(arg, arg32, 0, 1);
}

static void translate_fpu2k_add_s(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    gen_helper_fpu2k_add_s(arg[0].out, cpu_env,
                           arg[1].in, arg[2].in);
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

static void translate_compare_d(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    static void (* const helper[])(TCGv_i32 res, TCGv_env env,
                                   TCGv_i64 s, TCGv_i64 t) = {
        [COMPARE_UN] = gen_helper_un_d,
        [COMPARE_OEQ] = gen_helper_oeq_d,
        [COMPARE_UEQ] = gen_helper_ueq_d,
        [COMPARE_OLT] = gen_helper_olt_d,
        [COMPARE_ULT] = gen_helper_ult_d,
        [COMPARE_OLE] = gen_helper_ole_d,
        [COMPARE_ULE] = gen_helper_ule_d,
    };
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 res = tcg_temp_new_i32();
    TCGv_i32 set_br = tcg_temp_new_i32();
    TCGv_i32 clr_br = tcg_temp_new_i32();

    tcg_gen_ori_i32(set_br, arg[0].in, 1 << arg[0].imm);
    tcg_gen_andi_i32(clr_br, arg[0].in, ~(1 << arg[0].imm));

    helper[par[0]](res, cpu_env, arg[1].in, arg[2].in);
    tcg_gen_movcond_i32(TCG_COND_NE,
                        arg[0].out, res, zero,
                        set_br, clr_br);
}

static void translate_compare_s(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    static void (* const helper[])(TCGv_i32 res, TCGv_env env,
                                   TCGv_i32 s, TCGv_i32 t) = {
        [COMPARE_UN] = gen_helper_un_s,
        [COMPARE_OEQ] = gen_helper_oeq_s,
        [COMPARE_UEQ] = gen_helper_ueq_s,
        [COMPARE_OLT] = gen_helper_olt_s,
        [COMPARE_ULT] = gen_helper_ult_s,
        [COMPARE_OLE] = gen_helper_ole_s,
        [COMPARE_ULE] = gen_helper_ule_s,
    };
    OpcodeArg arg32[3];
    TCGv_i32 zero = tcg_constant_i32(0);
    TCGv_i32 res = tcg_temp_new_i32();
    TCGv_i32 set_br = tcg_temp_new_i32();
    TCGv_i32 clr_br = tcg_temp_new_i32();

    tcg_gen_ori_i32(set_br, arg[0].in, 1 << arg[0].imm);
    tcg_gen_andi_i32(clr_br, arg[0].in, ~(1 << arg[0].imm));

    get_f32_i2(arg, arg32, 1, 2);
    helper[par[0]](res, cpu_env, arg32[1].in, arg32[2].in);
    tcg_gen_movcond_i32(TCG_COND_NE,
                        arg[0].out, res, zero,
                        set_br, clr_br);
    put_f32_i2(arg, arg32, 1, 2);
}

static void translate_const_d(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    static const uint64_t v[] = {
        UINT64_C(0x0000000000000000),
        UINT64_C(0x3ff0000000000000),
        UINT64_C(0x4000000000000000),
        UINT64_C(0x3fe0000000000000),
    };

    tcg_gen_movi_i64(arg[0].out, v[arg[1].imm % ARRAY_SIZE(v)]);
    if (arg[1].imm >= ARRAY_SIZE(v)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "const.d f%d, #%d, immediate value is reserved\n",
                      arg[0].imm, arg[1].imm);
    }
}

static void translate_const_s(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    static const uint32_t v[] = {
        0x00000000,
        0x3f800000,
        0x40000000,
        0x3f000000,
    };

    if (arg[0].num_bits == 32) {
        tcg_gen_movi_i32(arg[0].out, v[arg[1].imm % ARRAY_SIZE(v)]);
    } else {
        tcg_gen_movi_i64(arg[0].out, v[arg[1].imm % ARRAY_SIZE(v)]);
    }
    if (arg[1].imm >= ARRAY_SIZE(v)) {
        qemu_log_mask(LOG_GUEST_ERROR,
                      "const.s f%d, #%d, immediate value is reserved\n",
                      arg[0].imm, arg[1].imm);
    }
}

static void translate_float_d(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 scale = tcg_constant_i32(-arg[2].imm);

    if (par[0]) {
        gen_helper_uitof_d(arg[0].out, cpu_env, arg[1].in, scale);
    } else {
        gen_helper_itof_d(arg[0].out, cpu_env, arg[1].in, scale);
    }
}

static void translate_float_s(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 scale = tcg_constant_i32(-arg[2].imm);
    OpcodeArg arg32[1];

    get_f32_o1(arg, arg32, 0);
    if (par[0]) {
        gen_helper_uitof_s(arg32[0].out, cpu_env, arg[1].in, scale);
    } else {
        gen_helper_itof_s(arg32[0].out, cpu_env, arg[1].in, scale);
    }
    put_f32_o1(arg, arg32, 0);
}

static void translate_ftoi_d(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 rounding_mode = tcg_constant_i32(par[0]);
    TCGv_i32 scale = tcg_constant_i32(arg[2].imm);

    if (par[1]) {
        gen_helper_ftoui_d(arg[0].out, cpu_env, arg[1].in,
                           rounding_mode, scale);
    } else {
        gen_helper_ftoi_d(arg[0].out, cpu_env, arg[1].in,
                          rounding_mode, scale);
    }
}

static void translate_ftoi_s(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 rounding_mode = tcg_constant_i32(par[0]);
    TCGv_i32 scale = tcg_constant_i32(arg[2].imm);
    OpcodeArg arg32[2];

    get_f32_i1(arg, arg32, 1);
    if (par[1]) {
        gen_helper_ftoui_s(arg[0].out, cpu_env, arg32[1].in,
                           rounding_mode, scale);
    } else {
        gen_helper_ftoi_s(arg[0].out, cpu_env, arg32[1].in,
                          rounding_mode, scale);
    }
    put_f32_i1(arg, arg32, 1);
}

static void translate_ldsti(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    mop = gen_load_store_alignment(dc, MO_TEUL, addr);
    if (par[0]) {
        tcg_gen_qemu_st_tl(arg[0].in, addr, dc->cring, mop);
    } else {
        tcg_gen_qemu_ld_tl(arg[0].out, addr, dc->cring, mop);
    }
    if (par[1]) {
        tcg_gen_mov_i32(arg[1].out, addr);
    }
}

static void translate_ldstx(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    TCGv_i32 addr = tcg_temp_new_i32();
    MemOp mop;

    tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    mop = gen_load_store_alignment(dc, MO_TEUL, addr);
    if (par[0]) {
        tcg_gen_qemu_st_tl(arg[0].in, addr, dc->cring, mop);
    } else {
        tcg_gen_qemu_ld_tl(arg[0].out, addr, dc->cring, mop);
    }
    if (par[1]) {
        tcg_gen_mov_i32(arg[1].out, addr);
    }
}

static void translate_fpu2k_madd_s(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    gen_helper_fpu2k_madd_s(arg[0].out, cpu_env,
                            arg[0].in, arg[1].in, arg[2].in);
}

static void translate_mov_d(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    tcg_gen_mov_i64(arg[0].out, arg[1].in);
}

static void translate_mov_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    if (arg[0].num_bits == 32) {
        tcg_gen_mov_i32(arg[0].out, arg[1].in);
    } else {
        tcg_gen_mov_i64(arg[0].out, arg[1].in);
    }
}

static void translate_movcond_d(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i64 arg2 = tcg_temp_new_i64();

    tcg_gen_ext_i32_i64(arg2, arg[2].in);
    tcg_gen_movcond_i64(par[0], arg[0].out,
                        arg2, zero,
                        arg[1].in, arg[0].in);
}

static void translate_movcond_s(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (arg[0].num_bits == 32) {
        TCGv_i32 zero = tcg_constant_i32(0);

        tcg_gen_movcond_i32(par[0], arg[0].out,
                            arg[2].in, zero,
                            arg[1].in, arg[0].in);
    } else {
        translate_movcond_d(dc, arg, par);
    }
}

static void translate_movp_d(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i64 zero = tcg_constant_i64(0);
    TCGv_i32 tmp1 = tcg_temp_new_i32();
    TCGv_i64 tmp2 = tcg_temp_new_i64();

    tcg_gen_andi_i32(tmp1, arg[2].in, 1 << arg[2].imm);
    tcg_gen_extu_i32_i64(tmp2, tmp1);
    tcg_gen_movcond_i64(par[0],
                        arg[0].out, tmp2, zero,
                        arg[1].in, arg[0].in);
}

static void translate_movp_s(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    if (arg[0].num_bits == 32) {
        TCGv_i32 zero = tcg_constant_i32(0);
        TCGv_i32 tmp = tcg_temp_new_i32();

        tcg_gen_andi_i32(tmp, arg[2].in, 1 << arg[2].imm);
        tcg_gen_movcond_i32(par[0],
                            arg[0].out, tmp, zero,
                            arg[1].in, arg[0].in);
    } else {
        translate_movp_d(dc, arg, par);
    }
}

static void translate_fpu2k_mul_s(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    gen_helper_fpu2k_mul_s(arg[0].out, cpu_env,
                           arg[1].in, arg[2].in);
}

static void translate_fpu2k_msub_s(DisasContext *dc, const OpcodeArg arg[],
                                   const uint32_t par[])
{
    gen_helper_fpu2k_msub_s(arg[0].out, cpu_env,
                            arg[0].in, arg[1].in, arg[2].in);
}

static void translate_neg_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    gen_helper_neg_d(arg[0].out, arg[1].in);
}

static void translate_neg_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    OpcodeArg arg32[2];

    get_f32_o1_i1(arg, arg32, 0, 1);
    gen_helper_neg_s(arg32[0].out, arg32[1].in);
    put_f32_o1_i1(arg, arg32, 0, 1);
}

static void translate_rfr_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    tcg_gen_extrh_i64_i32(arg[0].out, arg[1].in);
}

static void translate_rfr_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    if (arg[1].num_bits == 32) {
        tcg_gen_mov_i32(arg[0].out, arg[1].in);
    } else {
        tcg_gen_extrl_i64_i32(arg[0].out, arg[1].in);
    }
}

static void translate_fpu2k_sub_s(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    gen_helper_fpu2k_sub_s(arg[0].out, cpu_env,
                           arg[1].in, arg[2].in);
}

static void translate_wfr_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    tcg_gen_concat_i32_i64(arg[0].out, arg[2].in, arg[1].in);
}

static void translate_wfr_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    if (arg[0].num_bits == 32) {
        tcg_gen_mov_i32(arg[0].out, arg[1].in);
    } else {
        tcg_gen_ext_i32_i64(arg[0].out, arg[1].in);
    }
}

static void translate_wur_fpu2k_fcr(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    gen_helper_wur_fpu2k_fcr(cpu_env, arg[0].in);
}

static void translate_wur_fpu2k_fsr(DisasContext *dc, const OpcodeArg arg[],
                                    const uint32_t par[])
{
    tcg_gen_andi_i32(cpu_UR[par[0]], arg[0].in, 0xffffff80);
}

static const XtensaOpcodeOps fpu2000_ops[] = {
    {
        .name = "abs.s",
        .translate = translate_abs_s,
        .coprocessor = 0x1,
    }, {
        .name = "add.s",
        .translate = translate_fpu2k_add_s,
        .coprocessor = 0x1,
    }, {
        .name = "ceil.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_up, false},
        .coprocessor = 0x1,
    }, {
        .name = "float.s",
        .translate = translate_float_s,
        .par = (const uint32_t[]){false},
        .coprocessor = 0x1,
    }, {
        .name = "floor.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_down, false},
        .coprocessor = 0x1,
    }, {
        .name = "lsi",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){false, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsiu",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){false, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsx",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){false, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsxu",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){false, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "madd.s",
        .translate = translate_fpu2k_madd_s,
        .coprocessor = 0x1,
    }, {
        .name = "mov.s",
        .translate = translate_mov_s,
        .coprocessor = 0x1,
    }, {
        .name = "moveqz.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_EQ},
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
        .coprocessor = 0x1,
    }, {
        .name = "movltz.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_LT},
        .coprocessor = 0x1,
    }, {
        .name = "movnez.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "movt.s",
        .translate = translate_movp_s,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "msub.s",
        .translate = translate_fpu2k_msub_s,
        .coprocessor = 0x1,
    }, {
        .name = "mul.s",
        .translate = translate_fpu2k_mul_s,
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
        .coprocessor = 0x1,
    }, {
        .name = "round.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_nearest_even, false},
        .coprocessor = 0x1,
    }, {
        .name = "rur.fcr",
        .translate = translate_rur,
        .par = (const uint32_t[]){FCR},
        .coprocessor = 0x1,
    }, {
        .name = "rur.fsr",
        .translate = translate_rur,
        .par = (const uint32_t[]){FSR},
        .coprocessor = 0x1,
    }, {
        .name = "ssi",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){true, false},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssiu",
        .translate = translate_ldsti,
        .par = (const uint32_t[]){true, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssx",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){true, false},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssxu",
        .translate = translate_ldstx,
        .par = (const uint32_t[]){true, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sub.s",
        .translate = translate_fpu2k_sub_s,
        .coprocessor = 0x1,
    }, {
        .name = "trunc.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_to_zero, false},
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
        .coprocessor = 0x1,
    }, {
        .name = "wfr",
        .translate = translate_wfr_s,
        .coprocessor = 0x1,
    }, {
        .name = "wur.fcr",
        .translate = translate_wur_fpu2k_fcr,
        .par = (const uint32_t[]){FCR},
        .coprocessor = 0x1,
    }, {
        .name = "wur.fsr",
        .translate = translate_wur_fpu2k_fsr,
        .par = (const uint32_t[]){FSR},
        .coprocessor = 0x1,
    },
};

const XtensaOpcodeTranslators xtensa_fpu2000_opcodes = {
    .num_opcodes = ARRAY_SIZE(fpu2000_ops),
    .opcode = fpu2000_ops,
};

static void translate_add_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    gen_helper_add_d(arg[0].out, cpu_env, arg[1].in, arg[2].in);
}

static void translate_add_s(DisasContext *dc, const OpcodeArg arg[],
                                const uint32_t par[])
{
    if (option_enabled(dc, XTENSA_OPTION_DFPU_SINGLE_ONLY)) {
        gen_helper_fpu2k_add_s(arg[0].out, cpu_env,
                               arg[1].in, arg[2].in);
    } else {
        OpcodeArg arg32[3];

        get_f32_o1_i2(arg, arg32, 0, 1, 2);
        gen_helper_add_s(arg32[0].out, cpu_env, arg32[1].in, arg32[2].in);
        put_f32_o1_i2(arg, arg32, 0, 1, 2);
    }
}

static void translate_cvtd_s(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 v = tcg_temp_new_i32();

    tcg_gen_extrl_i64_i32(v, arg[1].in);
    gen_helper_cvtd_s(arg[0].out, cpu_env, v);
}

static void translate_cvts_d(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    TCGv_i32 v = tcg_temp_new_i32();

    gen_helper_cvts_d(v, cpu_env, arg[1].in);
    tcg_gen_extu_i32_i64(arg[0].out, v);
}

static void translate_ldsti_d(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 addr;
    MemOp mop;

    if (par[1]) {
        addr = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    } else {
        addr = arg[1].in;
    }
    mop = gen_load_store_alignment(dc, MO_TEUQ, addr);
    if (par[0]) {
        tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, mop);
    } else {
        tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, mop);
    }
    if (par[2]) {
        if (par[1]) {
            tcg_gen_mov_i32(arg[1].out, addr);
        } else {
            tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
        }
    }
}

static void translate_ldsti_s(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 addr;
    OpcodeArg arg32[1];
    MemOp mop;

    if (par[1]) {
        addr = tcg_temp_new_i32();
        tcg_gen_addi_i32(addr, arg[1].in, arg[2].imm);
    } else {
        addr = arg[1].in;
    }
    mop = gen_load_store_alignment(dc, MO_TEUL, addr);
    if (par[0]) {
        get_f32_i1(arg, arg32, 0);
        tcg_gen_qemu_st_tl(arg32[0].in, addr, dc->cring, mop);
        put_f32_i1(arg, arg32, 0);
    } else {
        get_f32_o1(arg, arg32, 0);
        tcg_gen_qemu_ld_tl(arg32[0].out, addr, dc->cring, mop);
        put_f32_o1(arg, arg32, 0);
    }
    if (par[2]) {
        if (par[1]) {
            tcg_gen_mov_i32(arg[1].out, addr);
        } else {
            tcg_gen_addi_i32(arg[1].out, arg[1].in, arg[2].imm);
        }
    }
}

static void translate_ldstx_d(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 addr;
    MemOp mop;

    if (par[1]) {
        addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    } else {
        addr = arg[1].in;
    }
    mop = gen_load_store_alignment(dc, MO_TEUQ, addr);
    if (par[0]) {
        tcg_gen_qemu_st_i64(arg[0].in, addr, dc->cring, mop);
    } else {
        tcg_gen_qemu_ld_i64(arg[0].out, addr, dc->cring, mop);
    }
    if (par[2]) {
        if (par[1]) {
            tcg_gen_mov_i32(arg[1].out, addr);
        } else {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

static void translate_ldstx_s(DisasContext *dc, const OpcodeArg arg[],
                              const uint32_t par[])
{
    TCGv_i32 addr;
    OpcodeArg arg32[1];
    MemOp mop;

    if (par[1]) {
        addr = tcg_temp_new_i32();
        tcg_gen_add_i32(addr, arg[1].in, arg[2].in);
    } else {
        addr = arg[1].in;
    }
    mop = gen_load_store_alignment(dc, MO_TEUL, addr);
    if (par[0]) {
        get_f32_i1(arg, arg32, 0);
        tcg_gen_qemu_st_tl(arg32[0].in, addr, dc->cring, mop);
        put_f32_i1(arg, arg32, 0);
    } else {
        get_f32_o1(arg, arg32, 0);
        tcg_gen_qemu_ld_tl(arg32[0].out, addr, dc->cring, mop);
        put_f32_o1(arg, arg32, 0);
    }
    if (par[2]) {
        if (par[1]) {
            tcg_gen_mov_i32(arg[1].out, addr);
        } else {
            tcg_gen_add_i32(arg[1].out, arg[1].in, arg[2].in);
        }
    }
}

static void translate_madd_d(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    gen_helper_madd_d(arg[0].out, cpu_env,
                      arg[0].in, arg[1].in, arg[2].in);
}

static void translate_madd_s(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    if (option_enabled(dc, XTENSA_OPTION_DFPU_SINGLE_ONLY)) {
        gen_helper_fpu2k_madd_s(arg[0].out, cpu_env,
                                arg[0].in, arg[1].in, arg[2].in);
    } else {
        OpcodeArg arg32[3];

        get_f32_o1_i3(arg, arg32, 0, 0, 1, 2);
        gen_helper_madd_s(arg32[0].out, cpu_env,
                          arg32[0].in, arg32[1].in, arg32[2].in);
        put_f32_o1_i3(arg, arg32, 0, 0, 1, 2);
    }
}

static void translate_mul_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    gen_helper_mul_d(arg[0].out, cpu_env, arg[1].in, arg[2].in);
}

static void translate_mul_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    if (option_enabled(dc, XTENSA_OPTION_DFPU_SINGLE_ONLY)) {
        gen_helper_fpu2k_mul_s(arg[0].out, cpu_env,
                               arg[1].in, arg[2].in);
    } else {
        OpcodeArg arg32[3];

        get_f32_o1_i2(arg, arg32, 0, 1, 2);
        gen_helper_mul_s(arg32[0].out, cpu_env, arg32[1].in, arg32[2].in);
        put_f32_o1_i2(arg, arg32, 0, 1, 2);
    }
}

static void translate_msub_d(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    gen_helper_msub_d(arg[0].out, cpu_env,
                      arg[0].in, arg[1].in, arg[2].in);
}

static void translate_msub_s(DisasContext *dc, const OpcodeArg arg[],
                             const uint32_t par[])
{
    if (option_enabled(dc, XTENSA_OPTION_DFPU_SINGLE_ONLY)) {
        gen_helper_fpu2k_msub_s(arg[0].out, cpu_env,
                                arg[0].in, arg[1].in, arg[2].in);
    } else {
        OpcodeArg arg32[3];

        get_f32_o1_i3(arg, arg32, 0, 0, 1, 2);
        gen_helper_msub_s(arg32[0].out, cpu_env,
                          arg32[0].in, arg32[1].in, arg32[2].in);
        put_f32_o1_i3(arg, arg32, 0, 0, 1, 2);
    }
}

static void translate_sub_d(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    gen_helper_sub_d(arg[0].out, cpu_env, arg[1].in, arg[2].in);
}

static void translate_sub_s(DisasContext *dc, const OpcodeArg arg[],
                            const uint32_t par[])
{
    if (option_enabled(dc, XTENSA_OPTION_DFPU_SINGLE_ONLY)) {
        gen_helper_fpu2k_sub_s(arg[0].out, cpu_env,
                               arg[1].in, arg[2].in);
    } else {
        OpcodeArg arg32[3];

        get_f32_o1_i2(arg, arg32, 0, 1, 2);
        gen_helper_sub_s(arg32[0].out, cpu_env, arg32[1].in, arg32[2].in);
        put_f32_o1_i2(arg, arg32, 0, 1, 2);
    }
}

static void translate_mkdadj_d(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    gen_helper_mkdadj_d(arg[0].out, cpu_env, arg[0].in, arg[1].in);
}

static void translate_mkdadj_s(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    OpcodeArg arg32[2];

    get_f32_o1_i2(arg, arg32, 0, 0, 1);
    gen_helper_mkdadj_s(arg32[0].out, cpu_env, arg32[0].in, arg32[1].in);
    put_f32_o1_i2(arg, arg32, 0, 0, 1);
}

static void translate_mksadj_d(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    gen_helper_mksadj_d(arg[0].out, cpu_env, arg[1].in);
}

static void translate_mksadj_s(DisasContext *dc, const OpcodeArg arg[],
                               const uint32_t par[])
{
    OpcodeArg arg32[2];

    get_f32_o1_i1(arg, arg32, 0, 1);
    gen_helper_mksadj_s(arg32[0].out, cpu_env, arg32[1].in);
    put_f32_o1_i1(arg, arg32, 0, 1);
}

static void translate_wur_fpu_fcr(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    gen_helper_wur_fpu_fcr(cpu_env, arg[0].in);
}

static void translate_rur_fpu_fsr(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    gen_helper_rur_fpu_fsr(arg[0].out, cpu_env);
}

static void translate_wur_fpu_fsr(DisasContext *dc, const OpcodeArg arg[],
                                  const uint32_t par[])
{
    gen_helper_wur_fpu_fsr(cpu_env, arg[0].in);
}

static const XtensaOpcodeOps fpu_ops[] = {
    {
        .name = "abs.d",
        .translate = translate_abs_d,
        .coprocessor = 0x1,
    }, {
        .name = "abs.s",
        .translate = translate_abs_s,
        .coprocessor = 0x1,
    }, {
        .name = "add.d",
        .translate = translate_add_d,
        .coprocessor = 0x1,
    }, {
        .name = "add.s",
        .translate = translate_add_s,
        .coprocessor = 0x1,
    }, {
        .name = "addexp.d",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "addexp.s",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "addexpm.d",
        .translate = translate_mov_s,
        .coprocessor = 0x1,
    }, {
        .name = "addexpm.s",
        .translate = translate_mov_s,
        .coprocessor = 0x1,
    }, {
        .name = "ceil.d",
        .translate = translate_ftoi_d,
        .par = (const uint32_t[]){float_round_up, false},
        .coprocessor = 0x1,
    }, {
        .name = "ceil.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_up, false},
        .coprocessor = 0x1,
    }, {
        .name = "const.d",
        .translate = translate_const_d,
        .coprocessor = 0x1,
    }, {
        .name = "const.s",
        .translate = translate_const_s,
        .coprocessor = 0x1,
    }, {
        .name = "cvtd.s",
        .translate = translate_cvtd_s,
        .coprocessor = 0x1,
    }, {
        .name = "cvts.d",
        .translate = translate_cvts_d,
        .coprocessor = 0x1,
    }, {
        .name = "div0.d",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "div0.s",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "divn.d",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "divn.s",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "float.d",
        .translate = translate_float_d,
        .par = (const uint32_t[]){false},
        .coprocessor = 0x1,
    }, {
        .name = "float.s",
        .translate = translate_float_s,
        .par = (const uint32_t[]){false},
        .coprocessor = 0x1,
    }, {
        .name = "floor.d",
        .translate = translate_ftoi_d,
        .par = (const uint32_t[]){float_round_down, false},
        .coprocessor = 0x1,
    }, {
        .name = "floor.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_down, false},
        .coprocessor = 0x1,
    }, {
        .name = "ldi",
        .translate = translate_ldsti_d,
        .par = (const uint32_t[]){false, true, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "ldip",
        .translate = translate_ldsti_d,
        .par = (const uint32_t[]){false, false, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "ldiu",
        .translate = translate_ldsti_d,
        .par = (const uint32_t[]){false, true, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "ldx",
        .translate = translate_ldstx_d,
        .par = (const uint32_t[]){false, true, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "ldxp",
        .translate = translate_ldstx_d,
        .par = (const uint32_t[]){false, false, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "ldxu",
        .translate = translate_ldstx_d,
        .par = (const uint32_t[]){false, true, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsi",
        .translate = translate_ldsti_s,
        .par = (const uint32_t[]){false, true, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsip",
        .translate = translate_ldsti_s,
        .par = (const uint32_t[]){false, false, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsiu",
        .translate = translate_ldsti_s,
        .par = (const uint32_t[]){false, true, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsx",
        .translate = translate_ldstx_s,
        .par = (const uint32_t[]){false, true, false},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsxp",
        .translate = translate_ldstx_s,
        .par = (const uint32_t[]){false, false, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "lsxu",
        .translate = translate_ldstx_s,
        .par = (const uint32_t[]){false, true, true},
        .op_flags = XTENSA_OP_LOAD,
        .coprocessor = 0x1,
    }, {
        .name = "madd.d",
        .translate = translate_madd_d,
        .coprocessor = 0x1,
    }, {
        .name = "madd.s",
        .translate = translate_madd_s,
        .coprocessor = 0x1,
    }, {
        .name = "maddn.d",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "maddn.s",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "mkdadj.d",
        .translate = translate_mkdadj_d,
        .coprocessor = 0x1,
    }, {
        .name = "mkdadj.s",
        .translate = translate_mkdadj_s,
        .coprocessor = 0x1,
    }, {
        .name = "mksadj.d",
        .translate = translate_mksadj_d,
        .coprocessor = 0x1,
    }, {
        .name = "mksadj.s",
        .translate = translate_mksadj_s,
        .coprocessor = 0x1,
    }, {
        .name = "mov.d",
        .translate = translate_mov_d,
        .coprocessor = 0x1,
    }, {
        .name = "mov.s",
        .translate = translate_mov_s,
        .coprocessor = 0x1,
    }, {
        .name = "moveqz.d",
        .translate = translate_movcond_d,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .coprocessor = 0x1,
    }, {
        .name = "moveqz.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .coprocessor = 0x1,
    }, {
        .name = "movf.d",
        .translate = translate_movp_d,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .coprocessor = 0x1,
    }, {
        .name = "movf.s",
        .translate = translate_movp_s,
        .par = (const uint32_t[]){TCG_COND_EQ},
        .coprocessor = 0x1,
    }, {
        .name = "movgez.d",
        .translate = translate_movcond_d,
        .par = (const uint32_t[]){TCG_COND_GE},
        .coprocessor = 0x1,
    }, {
        .name = "movgez.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_GE},
        .coprocessor = 0x1,
    }, {
        .name = "movltz.d",
        .translate = translate_movcond_d,
        .par = (const uint32_t[]){TCG_COND_LT},
        .coprocessor = 0x1,
    }, {
        .name = "movltz.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_LT},
        .coprocessor = 0x1,
    }, {
        .name = "movnez.d",
        .translate = translate_movcond_d,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "movnez.s",
        .translate = translate_movcond_s,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "movt.d",
        .translate = translate_movp_d,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "movt.s",
        .translate = translate_movp_s,
        .par = (const uint32_t[]){TCG_COND_NE},
        .coprocessor = 0x1,
    }, {
        .name = "msub.d",
        .translate = translate_msub_d,
        .coprocessor = 0x1,
    }, {
        .name = "msub.s",
        .translate = translate_msub_s,
        .coprocessor = 0x1,
    }, {
        .name = "mul.d",
        .translate = translate_mul_d,
        .coprocessor = 0x1,
    }, {
        .name = "mul.s",
        .translate = translate_mul_s,
        .coprocessor = 0x1,
    }, {
        .name = "neg.d",
        .translate = translate_neg_d,
        .coprocessor = 0x1,
    }, {
        .name = "neg.s",
        .translate = translate_neg_s,
        .coprocessor = 0x1,
    }, {
        .name = "nexp01.d",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "nexp01.s",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "oeq.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_OEQ},
        .coprocessor = 0x1,
    }, {
        .name = "oeq.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_OEQ},
        .coprocessor = 0x1,
    }, {
        .name = "ole.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_OLE},
        .coprocessor = 0x1,
    }, {
        .name = "ole.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_OLE},
        .coprocessor = 0x1,
    }, {
        .name = "olt.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_OLT},
        .coprocessor = 0x1,
    }, {
        .name = "olt.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_OLT},
        .coprocessor = 0x1,
    }, {
        .name = "rfr",
        .translate = translate_rfr_s,
        .coprocessor = 0x1,
    }, {
        .name = "rfrd",
        .translate = translate_rfr_d,
        .coprocessor = 0x1,
    }, {
        .name = "round.d",
        .translate = translate_ftoi_d,
        .par = (const uint32_t[]){float_round_nearest_even, false},
        .coprocessor = 0x1,
    }, {
        .name = "round.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_nearest_even, false},
        .coprocessor = 0x1,
    }, {
        .name = "rur.fcr",
        .translate = translate_rur,
        .par = (const uint32_t[]){FCR},
        .coprocessor = 0x1,
    }, {
        .name = "rur.fsr",
        .translate = translate_rur_fpu_fsr,
        .coprocessor = 0x1,
    }, {
        .name = "sdi",
        .translate = translate_ldsti_d,
        .par = (const uint32_t[]){true, true, false},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sdip",
        .translate = translate_ldsti_d,
        .par = (const uint32_t[]){true, false, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sdiu",
        .translate = translate_ldsti_d,
        .par = (const uint32_t[]){true, true, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sdx",
        .translate = translate_ldstx_d,
        .par = (const uint32_t[]){true, true, false},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sdxp",
        .translate = translate_ldstx_d,
        .par = (const uint32_t[]){true, false, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sdxu",
        .translate = translate_ldstx_d,
        .par = (const uint32_t[]){true, true, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sqrt0.d",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "sqrt0.s",
        .translate = translate_nop,
        .coprocessor = 0x1,
    }, {
        .name = "ssi",
        .translate = translate_ldsti_s,
        .par = (const uint32_t[]){true, true, false},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssip",
        .translate = translate_ldsti_s,
        .par = (const uint32_t[]){true, false, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssiu",
        .translate = translate_ldsti_s,
        .par = (const uint32_t[]){true, true, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssx",
        .translate = translate_ldstx_s,
        .par = (const uint32_t[]){true, true, false},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssxp",
        .translate = translate_ldstx_s,
        .par = (const uint32_t[]){true, false, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "ssxu",
        .translate = translate_ldstx_s,
        .par = (const uint32_t[]){true, true, true},
        .op_flags = XTENSA_OP_STORE,
        .coprocessor = 0x1,
    }, {
        .name = "sub.d",
        .translate = translate_sub_d,
        .coprocessor = 0x1,
    }, {
        .name = "sub.s",
        .translate = translate_sub_s,
        .coprocessor = 0x1,
    }, {
        .name = "trunc.d",
        .translate = translate_ftoi_d,
        .par = (const uint32_t[]){float_round_to_zero, false},
        .coprocessor = 0x1,
    }, {
        .name = "trunc.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_to_zero, false},
        .coprocessor = 0x1,
    }, {
        .name = "ueq.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_UEQ},
        .coprocessor = 0x1,
    }, {
        .name = "ueq.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_UEQ},
        .coprocessor = 0x1,
    }, {
        .name = "ufloat.d",
        .translate = translate_float_d,
        .par = (const uint32_t[]){true},
        .coprocessor = 0x1,
    }, {
        .name = "ufloat.s",
        .translate = translate_float_s,
        .par = (const uint32_t[]){true},
        .coprocessor = 0x1,
    }, {
        .name = "ule.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_ULE},
        .coprocessor = 0x1,
    }, {
        .name = "ule.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_ULE},
        .coprocessor = 0x1,
    }, {
        .name = "ult.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_ULT},
        .coprocessor = 0x1,
    }, {
        .name = "ult.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_ULT},
        .coprocessor = 0x1,
    }, {
        .name = "un.d",
        .translate = translate_compare_d,
        .par = (const uint32_t[]){COMPARE_UN},
        .coprocessor = 0x1,
    }, {
        .name = "un.s",
        .translate = translate_compare_s,
        .par = (const uint32_t[]){COMPARE_UN},
        .coprocessor = 0x1,
    }, {
        .name = "utrunc.d",
        .translate = translate_ftoi_d,
        .par = (const uint32_t[]){float_round_to_zero, true},
        .coprocessor = 0x1,
    }, {
        .name = "utrunc.s",
        .translate = translate_ftoi_s,
        .par = (const uint32_t[]){float_round_to_zero, true},
        .coprocessor = 0x1,
    }, {
        .name = "wfr",
        .translate = translate_wfr_s,
        .coprocessor = 0x1,
    }, {
        .name = "wfrd",
        .translate = translate_wfr_d,
        .coprocessor = 0x1,
    }, {
        .name = "wur.fcr",
        .translate = translate_wur_fpu_fcr,
        .par = (const uint32_t[]){FCR},
        .coprocessor = 0x1,
    }, {
        .name = "wur.fsr",
        .translate = translate_wur_fpu_fsr,
        .coprocessor = 0x1,
    },
};

const XtensaOpcodeTranslators xtensa_fpu_opcodes = {
    .num_opcodes = ARRAY_SIZE(fpu_ops),
    .opcode = fpu_ops,
};
