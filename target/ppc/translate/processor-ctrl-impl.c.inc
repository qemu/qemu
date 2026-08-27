/*
 * Power ISA decode for Storage Control instructions
 *
 * Copyright (c) 2022 Instituto de Pesquisas Eldorado (eldorado.org.br)
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

/*
 * Processor Control Instructions
 */

static bool trans_MSGCLR(DisasContext *ctx, arg_X_rb *a)
{
    if (!(ctx->insns_flags2 & PPC2_ISA207)) {
        /*
         * Before Power ISA 2.07, processor control instructions were only
         * implemented in the "Embedded.Processor Control" category.
         */
        REQUIRE_INSNS_FLAGS2(ctx, PRCNTL);
    }

    REQUIRE_HV(ctx);

#if !defined(CONFIG_USER_ONLY)
    if (is_book3s_arch2x(ctx)) {
        gen_helper_book3s_msgclr(tcg_env, cpu_gpr[a->rb]);
    } else {
        gen_helper_msgclr(tcg_env, cpu_gpr[a->rb]);
    }
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MSGSND(DisasContext *ctx, arg_X_rb *a)
{
    if (!(ctx->insns_flags2 & PPC2_ISA207)) {
        /*
         * Before Power ISA 2.07, processor control instructions were only
         * implemented in the "Embedded.Processor Control" category.
         */
        REQUIRE_INSNS_FLAGS2(ctx, PRCNTL);
    }

    REQUIRE_HV(ctx);

#if !defined(CONFIG_USER_ONLY)
    if (is_book3s_arch2x(ctx)) {
        gen_helper_book3s_msgsnd(tcg_env, cpu_gpr[a->rb]);
    } else {
        gen_helper_msgsnd(cpu_gpr[a->rb]);
    }
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MSGCLRP(DisasContext *ctx, arg_X_rb *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA207);
    REQUIRE_SV(ctx);
#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_book3s_msgclrp(tcg_env, cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MSGSNDP(DisasContext *ctx, arg_X_rb *a)
{
    REQUIRE_64BIT(ctx);
    REQUIRE_INSNS_FLAGS2(ctx, ISA207);
    REQUIRE_SV(ctx);
#if !defined(CONFIG_USER_ONLY) && defined(TARGET_PPC64)
    gen_helper_book3s_msgsndp(tcg_env, cpu_gpr[a->rb]);
#else
    qemu_build_not_reached();
#endif
    return true;
}

static bool trans_MSGSYNC(DisasContext *ctx, arg_MSGSYNC *a)
{
    REQUIRE_INSNS_FLAGS2(ctx, ISA300);
    REQUIRE_HV(ctx);

    /* interpreted as no-op */
    return true;
}

/*
 * Helper to handle DOZE, NAP, SLEEP, RVWINKLE & STOP. Since none of them use
 * any arguments just use a placeholder arg_SLEEP.
 */
static bool do_sleep(DisasContext *ctx, arg_SLEEP *a, powerpc_pm_insn_t type)
{
    REQUIRE_64BIT(ctx);

#if defined(CONFIG_USER_ONLY)
    gen_priv_opc(ctx);
#elif defined(TARGET_PPC64)
    TCGv_i32 t;

    REQUIRE_HV(ctx);
    translator_io_start(&ctx->base);
    t = tcg_constant_i32(type);
    gen_helper_PMINSN(tcg_env, t);
    /* Stop translation, as the CPU is supposed to sleep from now */
    gen_exception_nip(ctx, EXCP_HLT, ctx->base.pc_next);
#else
    qemu_build_not_reached();
#endif
    return true;
}

TRANS_FLAGS2(PM_ISA206, DOZE, do_sleep, PPC_PM_DOZE);
TRANS_FLAGS2(PM_ISA206, NAP, do_sleep, PPC_PM_NAP);
TRANS_FLAGS2(PM_ISA206, SLEEP, do_sleep, PPC_PM_SLEEP);
TRANS_FLAGS2(PM_ISA206, RVWINKLE, do_sleep, PPC_PM_RVWINKLE);
TRANS_FLAGS2(ISA300, STOP, do_sleep, PPC_PM_STOP);
