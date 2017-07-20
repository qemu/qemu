/*
 *  Moxie emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2009, 2013 Anthony Green
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* For information on the Moxie architecture, see
 *    http://moxielogic.org/wiki
 */

#include "qemu/osdep.h"

#include "cpu.h"
#include "exec/exec-all.h"
#include "disas/disas.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/log.h"

/* This is the state at translation time.  */
typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc, saved_pc;
    uint32_t opcode;
    uint32_t fp_status;
    /* Routine used to access memory */
    int memidx;
    int bstate;
    target_ulong btarget;
    int singlestep_enabled;
} DisasContext;

enum {
    BS_NONE     = 0, /* We go out of the TB without reaching a branch or an
                      * exception condition */
    BS_STOP     = 1, /* We want to stop translation for any reason */
    BS_BRANCH   = 2, /* We reached a branch condition     */
    BS_EXCP     = 3, /* We reached an exception condition */
};

static TCGv cpu_pc;
static TCGv cpu_gregs[16];
static TCGv_env cpu_env;
static TCGv cc_a, cc_b;

#include "exec/gen-icount.h"

#define REG(x) (cpu_gregs[x])

/* Extract the signed 10-bit offset from a 16-bit branch
   instruction.  */
static int extract_branch_offset(int opcode)
{
  return (((signed short)((opcode & ((1 << 10) - 1)) << 6)) >> 6) << 1;
}

void moxie_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                          int flags)
{
    MoxieCPU *cpu = MOXIE_CPU(cs);
    CPUMoxieState *env = &cpu->env;
    int i;
    cpu_fprintf(f, "pc=0x%08x\n", env->pc);
    cpu_fprintf(f, "$fp=0x%08x $sp=0x%08x $r0=0x%08x $r1=0x%08x\n",
                env->gregs[0], env->gregs[1], env->gregs[2], env->gregs[3]);
    for (i = 4; i < 16; i += 4) {
        cpu_fprintf(f, "$r%d=0x%08x $r%d=0x%08x $r%d=0x%08x $r%d=0x%08x\n",
                    i-2, env->gregs[i], i-1, env->gregs[i + 1],
                    i, env->gregs[i + 2], i+1, env->gregs[i + 3]);
    }
    for (i = 4; i < 16; i += 4) {
        cpu_fprintf(f, "sr%d=0x%08x sr%d=0x%08x sr%d=0x%08x sr%d=0x%08x\n",
                    i-2, env->sregs[i], i-1, env->sregs[i + 1],
                    i, env->sregs[i + 2], i+1, env->sregs[i + 3]);
    }
}

void moxie_translate_init(void)
{
    int i;
    static int done_init;
    static const char * const gregnames[16] = {
        "$fp", "$sp", "$r0", "$r1",
        "$r2", "$r3", "$r4", "$r5",
        "$r6", "$r7", "$r8", "$r9",
        "$r10", "$r11", "$r12", "$r13"
    };

    if (done_init) {
        return;
    }
    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    tcg_ctx.tcg_env = cpu_env;
    cpu_pc = tcg_global_mem_new_i32(cpu_env,
                                    offsetof(CPUMoxieState, pc), "$pc");
    for (i = 0; i < 16; i++)
        cpu_gregs[i] = tcg_global_mem_new_i32(cpu_env,
                                              offsetof(CPUMoxieState, gregs[i]),
                                              gregnames[i]);

    cc_a = tcg_global_mem_new_i32(cpu_env,
                                  offsetof(CPUMoxieState, cc_a), "cc_a");
    cc_b = tcg_global_mem_new_i32(cpu_env,
                                  offsetof(CPUMoxieState, cc_b), "cc_b");

    done_init = 1;
}

static inline bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    if (unlikely(ctx->singlestep_enabled)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    return (ctx->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static inline void gen_goto_tb(CPUMoxieState *env, DisasContext *ctx,
                               int n, target_ulong dest)
{
    if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(n);
        tcg_gen_movi_i32(cpu_pc, dest);
        tcg_gen_exit_tb((uintptr_t)ctx->tb + n);
    } else {
        tcg_gen_movi_i32(cpu_pc, dest);
        if (ctx->singlestep_enabled) {
            gen_helper_debug(cpu_env);
        }
        tcg_gen_exit_tb(0);
    }
}

static int decode_opc(MoxieCPU *cpu, DisasContext *ctx)
{
    CPUMoxieState *env = &cpu->env;

    /* Local cache for the instruction opcode.  */
    int opcode;
    /* Set the default instruction length.  */
    int length = 2;

    /* Examine the 16-bit opcode.  */
    opcode = ctx->opcode;

    /* Decode instruction.  */
    if (opcode & (1 << 15)) {
        if (opcode & (1 << 14)) {
            /* This is a Form 3 instruction.  */
            int inst = (opcode >> 10 & 0xf);

#define BRANCH(cond)                                                         \
    do {                                                                     \
        TCGLabel *l1 = gen_new_label();                                      \
        tcg_gen_brcond_i32(cond, cc_a, cc_b, l1);                            \
        gen_goto_tb(env, ctx, 1, ctx->pc+2);                                 \
        gen_set_label(l1);                                                   \
        gen_goto_tb(env, ctx, 0, extract_branch_offset(opcode) + ctx->pc+2); \
        ctx->bstate = BS_BRANCH;                                             \
    } while (0)

            switch (inst) {
            case 0x00: /* beq */
                BRANCH(TCG_COND_EQ);
                break;
            case 0x01: /* bne */
                BRANCH(TCG_COND_NE);
                break;
            case 0x02: /* blt */
                BRANCH(TCG_COND_LT);
                break;
            case 0x03: /* bgt */
                BRANCH(TCG_COND_GT);
                break;
            case 0x04: /* bltu */
                BRANCH(TCG_COND_LTU);
                break;
            case 0x05: /* bgtu */
                BRANCH(TCG_COND_GTU);
                break;
            case 0x06: /* bge */
                BRANCH(TCG_COND_GE);
                break;
            case 0x07: /* ble */
                BRANCH(TCG_COND_LE);
                break;
            case 0x08: /* bgeu */
                BRANCH(TCG_COND_GEU);
                break;
            case 0x09: /* bleu */
                BRANCH(TCG_COND_LEU);
                break;
            default:
                {
                    TCGv temp = tcg_temp_new_i32();
                    tcg_gen_movi_i32(cpu_pc, ctx->pc);
                    tcg_gen_movi_i32(temp, MOXIE_EX_BAD);
                    gen_helper_raise_exception(cpu_env, temp);
                    tcg_temp_free_i32(temp);
                }
                break;
            }
        } else {
            /* This is a Form 2 instruction.  */
            int inst = (opcode >> 12 & 0x3);
            switch (inst) {
            case 0x00: /* inc */
                {
                    int a = (opcode >> 8) & 0xf;
                    unsigned int v = (opcode & 0xff);
                    tcg_gen_addi_i32(REG(a), REG(a), v);
                }
                break;
            case 0x01: /* dec */
                {
                    int a = (opcode >> 8) & 0xf;
                    unsigned int v = (opcode & 0xff);
                    tcg_gen_subi_i32(REG(a), REG(a), v);
                }
                break;
            case 0x02: /* gsr */
                {
                    int a = (opcode >> 8) & 0xf;
                    unsigned v = (opcode & 0xff);
                    tcg_gen_ld_i32(REG(a), cpu_env,
                                   offsetof(CPUMoxieState, sregs[v]));
                }
                break;
            case 0x03: /* ssr */
                {
                    int a = (opcode >> 8) & 0xf;
                    unsigned v = (opcode & 0xff);
                    tcg_gen_st_i32(REG(a), cpu_env,
                                   offsetof(CPUMoxieState, sregs[v]));
                }
                break;
            default:
                {
                    TCGv temp = tcg_temp_new_i32();
                    tcg_gen_movi_i32(cpu_pc, ctx->pc);
                    tcg_gen_movi_i32(temp, MOXIE_EX_BAD);
                    gen_helper_raise_exception(cpu_env, temp);
                    tcg_temp_free_i32(temp);
                }
                break;
            }
        }
    } else {
        /* This is a Form 1 instruction.  */
        int inst = opcode >> 8;
        switch (inst) {
        case 0x00: /* nop */
            break;
        case 0x01: /* ldi.l (immediate) */
            {
                int reg = (opcode >> 4) & 0xf;
                int val = cpu_ldl_code(env, ctx->pc+2);
                tcg_gen_movi_i32(REG(reg), val);
                length = 6;
            }
            break;
        case 0x02: /* mov (register-to-register) */
            {
                int dest  = (opcode >> 4) & 0xf;
                int src = opcode & 0xf;
                tcg_gen_mov_i32(REG(dest), REG(src));
            }
            break;
        case 0x03: /* jsra */
            {
                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();

                tcg_gen_movi_i32(t1, ctx->pc + 6);

                /* Make space for the static chain and return address.  */
                tcg_gen_subi_i32(t2, REG(1), 8);
                tcg_gen_mov_i32(REG(1), t2);
                tcg_gen_qemu_st32(t1, REG(1), ctx->memidx);

                /* Push the current frame pointer.  */
                tcg_gen_subi_i32(t2, REG(1), 4);
                tcg_gen_mov_i32(REG(1), t2);
                tcg_gen_qemu_st32(REG(0), REG(1), ctx->memidx);

                /* Set the pc and $fp.  */
                tcg_gen_mov_i32(REG(0), REG(1));

                gen_goto_tb(env, ctx, 0, cpu_ldl_code(env, ctx->pc+2));

                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                ctx->bstate = BS_BRANCH;
                length = 6;
            }
            break;
        case 0x04: /* ret */
            {
                TCGv t1 = tcg_temp_new_i32();

                /* The new $sp is the old $fp.  */
                tcg_gen_mov_i32(REG(1), REG(0));

                /* Pop the frame pointer.  */
                tcg_gen_qemu_ld32u(REG(0), REG(1), ctx->memidx);
                tcg_gen_addi_i32(t1, REG(1), 4);
                tcg_gen_mov_i32(REG(1), t1);


                /* Pop the return address and skip over the static chain
                   slot.  */
                tcg_gen_qemu_ld32u(cpu_pc, REG(1), ctx->memidx);
                tcg_gen_addi_i32(t1, REG(1), 8);
                tcg_gen_mov_i32(REG(1), t1);

                tcg_temp_free_i32(t1);

                /* Jump... */
                tcg_gen_exit_tb(0);

                ctx->bstate = BS_BRANCH;
            }
            break;
        case 0x05: /* add.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_add_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x06: /* push */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                tcg_gen_subi_i32(t1, REG(a), 4);
                tcg_gen_mov_i32(REG(a), t1);
                tcg_gen_qemu_st32(REG(b), REG(a), ctx->memidx);
                tcg_temp_free_i32(t1);
            }
            break;
        case 0x07: /* pop */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;
                TCGv t1 = tcg_temp_new_i32();

                tcg_gen_qemu_ld32u(REG(b), REG(a), ctx->memidx);
                tcg_gen_addi_i32(t1, REG(a), 4);
                tcg_gen_mov_i32(REG(a), t1);
                tcg_temp_free_i32(t1);
            }
            break;
        case 0x08: /* lda.l */
            {
                int reg = (opcode >> 4) & 0xf;

                TCGv ptr = tcg_temp_new_i32();
                tcg_gen_movi_i32(ptr, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_ld32u(REG(reg), ptr, ctx->memidx);
                tcg_temp_free_i32(ptr);

                length = 6;
            }
            break;
        case 0x09: /* sta.l */
            {
                int val = (opcode >> 4) & 0xf;

                TCGv ptr = tcg_temp_new_i32();
                tcg_gen_movi_i32(ptr, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_st32(REG(val), ptr, ctx->memidx);
                tcg_temp_free_i32(ptr);

                length = 6;
            }
            break;
        case 0x0a: /* ld.l (register indirect) */
            {
                int src  = opcode & 0xf;
                int dest = (opcode >> 4) & 0xf;

                tcg_gen_qemu_ld32u(REG(dest), REG(src), ctx->memidx);
            }
            break;
        case 0x0b: /* st.l */
            {
                int dest = (opcode >> 4) & 0xf;
                int val  = opcode & 0xf;

                tcg_gen_qemu_st32(REG(val), REG(dest), ctx->memidx);
            }
            break;
        case 0x0c: /* ldo.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();
                tcg_gen_addi_i32(t1, REG(b), cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_ld32u(t2, t1, ctx->memidx);
                tcg_gen_mov_i32(REG(a), t2);

                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                length = 6;
            }
            break;
        case 0x0d: /* sto.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();
                tcg_gen_addi_i32(t1, REG(a), cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_st32(REG(b), t1, ctx->memidx);

                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                length = 6;
            }
            break;
        case 0x0e: /* cmp */
            {
                int a  = (opcode >> 4) & 0xf;
                int b  = opcode & 0xf;

                tcg_gen_mov_i32(cc_a, REG(a));
                tcg_gen_mov_i32(cc_b, REG(b));
            }
            break;
        case 0x19: /* jsr */
            {
                int fnreg = (opcode >> 4) & 0xf;

                /* Load the stack pointer into T0.  */
                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();

                tcg_gen_movi_i32(t1, ctx->pc+2);

                /* Make space for the static chain and return address.  */
                tcg_gen_subi_i32(t2, REG(1), 8);
                tcg_gen_mov_i32(REG(1), t2);
                tcg_gen_qemu_st32(t1, REG(1), ctx->memidx);

                /* Push the current frame pointer.  */
                tcg_gen_subi_i32(t2, REG(1), 4);
                tcg_gen_mov_i32(REG(1), t2);
                tcg_gen_qemu_st32(REG(0), REG(1), ctx->memidx);

                /* Set the pc and $fp.  */
                tcg_gen_mov_i32(REG(0), REG(1));
                tcg_gen_mov_i32(cpu_pc, REG(fnreg));
                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);
                tcg_gen_exit_tb(0);
                ctx->bstate = BS_BRANCH;
            }
            break;
        case 0x1a: /* jmpa */
            {
                tcg_gen_movi_i32(cpu_pc, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_exit_tb(0);
                ctx->bstate = BS_BRANCH;
                length = 6;
            }
            break;
        case 0x1b: /* ldi.b (immediate) */
            {
                int reg = (opcode >> 4) & 0xf;
                int val = cpu_ldl_code(env, ctx->pc+2);
                tcg_gen_movi_i32(REG(reg), val);
                length = 6;
            }
            break;
        case 0x1c: /* ld.b (register indirect) */
            {
                int src  = opcode & 0xf;
                int dest = (opcode >> 4) & 0xf;

                tcg_gen_qemu_ld8u(REG(dest), REG(src), ctx->memidx);
            }
            break;
        case 0x1d: /* lda.b */
            {
                int reg = (opcode >> 4) & 0xf;

                TCGv ptr = tcg_temp_new_i32();
                tcg_gen_movi_i32(ptr, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_ld8u(REG(reg), ptr, ctx->memidx);
                tcg_temp_free_i32(ptr);

                length = 6;
            }
            break;
        case 0x1e: /* st.b */
            {
                int dest = (opcode >> 4) & 0xf;
                int val  = opcode & 0xf;

                tcg_gen_qemu_st8(REG(val), REG(dest), ctx->memidx);
            }
            break;
        case 0x1f: /* sta.b */
            {
                int val = (opcode >> 4) & 0xf;

                TCGv ptr = tcg_temp_new_i32();
                tcg_gen_movi_i32(ptr, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_st8(REG(val), ptr, ctx->memidx);
                tcg_temp_free_i32(ptr);

                length = 6;
            }
            break;
        case 0x20: /* ldi.s (immediate) */
            {
                int reg = (opcode >> 4) & 0xf;
                int val = cpu_ldl_code(env, ctx->pc+2);
                tcg_gen_movi_i32(REG(reg), val);
                length = 6;
            }
            break;
        case 0x21: /* ld.s (register indirect) */
            {
                int src  = opcode & 0xf;
                int dest = (opcode >> 4) & 0xf;

                tcg_gen_qemu_ld16u(REG(dest), REG(src), ctx->memidx);
            }
            break;
        case 0x22: /* lda.s */
            {
                int reg = (opcode >> 4) & 0xf;

                TCGv ptr = tcg_temp_new_i32();
                tcg_gen_movi_i32(ptr, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_ld16u(REG(reg), ptr, ctx->memidx);
                tcg_temp_free_i32(ptr);

                length = 6;
            }
            break;
        case 0x23: /* st.s */
            {
                int dest = (opcode >> 4) & 0xf;
                int val  = opcode & 0xf;

                tcg_gen_qemu_st16(REG(val), REG(dest), ctx->memidx);
            }
            break;
        case 0x24: /* sta.s */
            {
                int val = (opcode >> 4) & 0xf;

                TCGv ptr = tcg_temp_new_i32();
                tcg_gen_movi_i32(ptr, cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_st16(REG(val), ptr, ctx->memidx);
                tcg_temp_free_i32(ptr);

                length = 6;
            }
            break;
        case 0x25: /* jmp */
            {
                int reg = (opcode >> 4) & 0xf;
                tcg_gen_mov_i32(cpu_pc, REG(reg));
                tcg_gen_exit_tb(0);
                ctx->bstate = BS_BRANCH;
            }
            break;
        case 0x26: /* and */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_and_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x27: /* lshr */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv sv = tcg_temp_new_i32();
                tcg_gen_andi_i32(sv, REG(b), 0x1f);
                tcg_gen_shr_i32(REG(a), REG(a), sv);
                tcg_temp_free_i32(sv);
            }
            break;
        case 0x28: /* ashl */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv sv = tcg_temp_new_i32();
                tcg_gen_andi_i32(sv, REG(b), 0x1f);
                tcg_gen_shl_i32(REG(a), REG(a), sv);
                tcg_temp_free_i32(sv);
            }
            break;
        case 0x29: /* sub.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_sub_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x2a: /* neg */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_neg_i32(REG(a), REG(b));
            }
            break;
        case 0x2b: /* or */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_or_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x2c: /* not */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_not_i32(REG(a), REG(b));
            }
            break;
        case 0x2d: /* ashr */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv sv = tcg_temp_new_i32();
                tcg_gen_andi_i32(sv, REG(b), 0x1f);
                tcg_gen_sar_i32(REG(a), REG(a), sv);
                tcg_temp_free_i32(sv);
            }
            break;
        case 0x2e: /* xor */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_xor_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x2f: /* mul.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                tcg_gen_mul_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x30: /* swi */
            {
                int val = cpu_ldl_code(env, ctx->pc+2);

                TCGv temp = tcg_temp_new_i32();
                tcg_gen_movi_i32(temp, val);
                tcg_gen_st_i32(temp, cpu_env,
                               offsetof(CPUMoxieState, sregs[3]));
                tcg_gen_movi_i32(cpu_pc, ctx->pc);
                tcg_gen_movi_i32(temp, MOXIE_EX_SWI);
                gen_helper_raise_exception(cpu_env, temp);
                tcg_temp_free_i32(temp);

                length = 6;
            }
            break;
        case 0x31: /* div.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;
                tcg_gen_movi_i32(cpu_pc, ctx->pc);
                gen_helper_div(REG(a), cpu_env, REG(a), REG(b));
            }
            break;
        case 0x32: /* udiv.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;
                tcg_gen_movi_i32(cpu_pc, ctx->pc);
                gen_helper_udiv(REG(a), cpu_env, REG(a), REG(b));
            }
            break;
        case 0x33: /* mod.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;
                tcg_gen_rem_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x34: /* umod.l */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;
                tcg_gen_remu_i32(REG(a), REG(a), REG(b));
            }
            break;
        case 0x35: /* brk */
            {
                TCGv temp = tcg_temp_new_i32();
                tcg_gen_movi_i32(cpu_pc, ctx->pc);
                tcg_gen_movi_i32(temp, MOXIE_EX_BREAK);
                gen_helper_raise_exception(cpu_env, temp);
                tcg_temp_free_i32(temp);
            }
            break;
        case 0x36: /* ldo.b */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();
                tcg_gen_addi_i32(t1, REG(b), cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_ld8u(t2, t1, ctx->memidx);
                tcg_gen_mov_i32(REG(a), t2);

                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                length = 6;
            }
            break;
        case 0x37: /* sto.b */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();
                tcg_gen_addi_i32(t1, REG(a), cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_st8(REG(b), t1, ctx->memidx);

                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                length = 6;
            }
            break;
        case 0x38: /* ldo.s */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();
                tcg_gen_addi_i32(t1, REG(b), cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_ld16u(t2, t1, ctx->memidx);
                tcg_gen_mov_i32(REG(a), t2);

                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                length = 6;
            }
            break;
        case 0x39: /* sto.s */
            {
                int a = (opcode >> 4) & 0xf;
                int b = opcode & 0xf;

                TCGv t1 = tcg_temp_new_i32();
                TCGv t2 = tcg_temp_new_i32();
                tcg_gen_addi_i32(t1, REG(a), cpu_ldl_code(env, ctx->pc+2));
                tcg_gen_qemu_st16(REG(b), t1, ctx->memidx);
                tcg_temp_free_i32(t1);
                tcg_temp_free_i32(t2);

                length = 6;
            }
            break;
        default:
            {
                TCGv temp = tcg_temp_new_i32();
                tcg_gen_movi_i32(cpu_pc, ctx->pc);
                tcg_gen_movi_i32(temp, MOXIE_EX_BAD);
                gen_helper_raise_exception(cpu_env, temp);
                tcg_temp_free_i32(temp);
             }
            break;
        }
    }

    return length;
}

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cs, struct TranslationBlock *tb)
{
    CPUMoxieState *env = cs->env_ptr;
    MoxieCPU *cpu = moxie_env_get_cpu(env);
    DisasContext ctx;
    target_ulong pc_start;
    int num_insns, max_insns;

    pc_start = tb->pc;
    ctx.pc = pc_start;
    ctx.saved_pc = -1;
    ctx.tb = tb;
    ctx.memidx = 0;
    ctx.singlestep_enabled = 0;
    ctx.bstate = BS_NONE;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    gen_tb_start(tb);
    do {
        tcg_gen_insn_start(ctx.pc);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, ctx.pc, BP_ANY))) {
            tcg_gen_movi_i32(cpu_pc, ctx.pc);
            gen_helper_debug(cpu_env);
            ctx.bstate = BS_EXCP;
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            ctx.pc += 2;
            goto done_generating;
        }

        ctx.opcode = cpu_lduw_code(env, ctx.pc);
        ctx.pc += decode_opc(cpu, &ctx);

        if (num_insns >= max_insns) {
            break;
        }
        if (cs->singlestep_enabled) {
            break;
        }
        if ((ctx.pc & (TARGET_PAGE_SIZE - 1)) == 0) {
            break;
        }
    } while (ctx.bstate == BS_NONE && !tcg_op_buf_full());

    if (cs->singlestep_enabled) {
        tcg_gen_movi_tl(cpu_pc, ctx.pc);
        gen_helper_debug(cpu_env);
    } else {
        switch (ctx.bstate) {
        case BS_STOP:
        case BS_NONE:
            gen_goto_tb(env, &ctx, 0, ctx.pc);
            break;
        case BS_EXCP:
            tcg_gen_exit_tb(0);
            break;
        case BS_BRANCH:
        default:
            break;
        }
    }
 done_generating:
    gen_tb_end(tb, num_insns);

    tb->size = ctx.pc - pc_start;
    tb->icount = num_insns;
}

void restore_state_to_opc(CPUMoxieState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->pc = data[0];
}
