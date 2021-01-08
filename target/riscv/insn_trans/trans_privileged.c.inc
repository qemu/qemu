/*
 * RISC-V translation routines for the RISC-V privileged instructions.
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 * Copyright (c) 2018 Peer Adelt, peer.adelt@hni.uni-paderborn.de
 *                    Bastian Koppelmann, kbastian@mail.uni-paderborn.de
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

static bool trans_ecall(DisasContext *ctx, arg_ecall *a)
{
    /* always generates U-level ECALL, fixed in do_interrupt handler */
    generate_exception(ctx, RISCV_EXCP_U_ECALL);
    exit_tb(ctx); /* no chaining */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_ebreak(DisasContext *ctx, arg_ebreak *a)
{
    target_ulong    ebreak_addr = ctx->base.pc_next;
    target_ulong    pre_addr = ebreak_addr - 4;
    target_ulong    post_addr = ebreak_addr + 4;
    uint32_t pre    = 0;
    uint32_t ebreak = 0;
    uint32_t post   = 0;

    /*
     * The RISC-V semihosting spec specifies the following
     * three-instruction sequence to flag a semihosting call:
     *
     *      slli zero, zero, 0x1f       0x01f01013
     *      ebreak                      0x00100073
     *      srai zero, zero, 0x7        0x40705013
     *
     * The two shift operations on the zero register are no-ops, used
     * here to signify a semihosting exception, rather than a breakpoint.
     *
     * Uncompressed instructions are required so that the sequence is easy
     * to validate.
     *
     * The three instructions are required to lie in the same page so
     * that no exception will be raised when fetching them.
     */

    if ((pre_addr & TARGET_PAGE_MASK) == (post_addr & TARGET_PAGE_MASK)) {
        pre    = opcode_at(&ctx->base, pre_addr);
        ebreak = opcode_at(&ctx->base, ebreak_addr);
        post   = opcode_at(&ctx->base, post_addr);
    }

    if  (pre == 0x01f01013 && ebreak == 0x00100073 && post == 0x40705013) {
        generate_exception(ctx, RISCV_EXCP_SEMIHOST);
    } else {
        generate_exception(ctx, RISCV_EXCP_BREAKPOINT);
    }
    exit_tb(ctx); /* no chaining */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
}

static bool trans_uret(DisasContext *ctx, arg_uret *a)
{
    return false;
}

static bool trans_sret(DisasContext *ctx, arg_sret *a)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);

    if (has_ext(ctx, RVS)) {
        gen_helper_sret(cpu_pc, cpu_env, cpu_pc);
        exit_tb(ctx); /* no chaining */
        ctx->base.is_jmp = DISAS_NORETURN;
    } else {
        return false;
    }
    return true;
#else
    return false;
#endif
}

static bool trans_mret(DisasContext *ctx, arg_mret *a)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_tl(cpu_pc, ctx->base.pc_next);
    gen_helper_mret(cpu_pc, cpu_env, cpu_pc);
    exit_tb(ctx); /* no chaining */
    ctx->base.is_jmp = DISAS_NORETURN;
    return true;
#else
    return false;
#endif
}

static bool trans_wfi(DisasContext *ctx, arg_wfi *a)
{
#ifndef CONFIG_USER_ONLY
    tcg_gen_movi_tl(cpu_pc, ctx->pc_succ_insn);
    gen_helper_wfi(cpu_env);
    return true;
#else
    return false;
#endif
}

static bool trans_sfence_vma(DisasContext *ctx, arg_sfence_vma *a)
{
#ifndef CONFIG_USER_ONLY
    gen_helper_tlb_flush(cpu_env);
    return true;
#endif
    return false;
}

static bool trans_sfence_vm(DisasContext *ctx, arg_sfence_vm *a)
{
    return false;
}
