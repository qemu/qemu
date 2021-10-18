/*
 * PMU register read/write functions for TCG IBM POWER chips
 *
 * Copyright IBM Corp. 2021
 *
 * Authors:
 *  Daniel Henrique Barboza      <danielhb413@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#if defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY)

/*
 * Checks whether the Group A SPR (MMCR0, MMCR2, MMCRA, and the
 * PMCs) has problem state read access.
 *
 * Read acccess is granted for all PMCC values but 0b01, where a
 * Facility Unavailable Interrupt will occur.
 */
static bool spr_groupA_read_allowed(DisasContext *ctx)
{
    if (!ctx->mmcr0_pmcc0 && ctx->mmcr0_pmcc1) {
        gen_hvpriv_exception(ctx, POWERPC_EXCP_FU);
        return false;
    }

    return true;
}

/*
 * Checks whether the Group A SPR (MMCR0, MMCR2, MMCRA, and the
 * PMCs) has problem state write access.
 *
 * Write acccess is granted for PMCC values 0b10 and 0b11. Userspace
 * writing with PMCC 0b00 will generate a Hypervisor Emulation
 * Assistance Interrupt. Userspace writing with PMCC 0b01 will
 * generate a Facility Unavailable Interrupt.
 */
static bool spr_groupA_write_allowed(DisasContext *ctx)
{
    if (ctx->mmcr0_pmcc0) {
        return true;
    }

    if (ctx->mmcr0_pmcc1) {
        /* PMCC = 0b01 */
        gen_hvpriv_exception(ctx, POWERPC_EXCP_FU);
    } else {
        /* PMCC = 0b00 */
        gen_hvpriv_exception(ctx, POWERPC_EXCP_INVAL_SPR);
    }

    return false;
}

void spr_read_MMCR0_ureg(DisasContext *ctx, int gprn, int sprn)
{
    TCGv t0;

    if (!spr_groupA_read_allowed(ctx)) {
        return;
    }

    t0 = tcg_temp_new();

    /*
     * Filter out all bits but FC, PMAO, and PMAE, according
     * to ISA v3.1, in 10.4.4 Monitor Mode Control Register 0,
     * fourth paragraph.
     */
    gen_load_spr(t0, SPR_POWER_MMCR0);
    tcg_gen_andi_tl(t0, t0, MMCR0_UREG_MASK);
    tcg_gen_mov_tl(cpu_gpr[gprn], t0);

    tcg_temp_free(t0);
}

void spr_write_MMCR0_ureg(DisasContext *ctx, int sprn, int gprn)
{
    TCGv t0, t1;

    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    /*
     * Filter out all bits but FC, PMAO, and PMAE, according
     * to ISA v3.1, in 10.4.4 Monitor Mode Control Register 0,
     * fourth paragraph.
     */
    tcg_gen_andi_tl(t0, cpu_gpr[gprn], MMCR0_UREG_MASK);
    gen_load_spr(t1, SPR_POWER_MMCR0);
    tcg_gen_andi_tl(t1, t1, ~(MMCR0_UREG_MASK));
    /* Keep all other bits intact */
    tcg_gen_or_tl(t1, t1, t0);
    gen_store_spr(SPR_POWER_MMCR0, t1);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}
#else
void spr_read_MMCR0_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_write_MMCR0_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}
#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
