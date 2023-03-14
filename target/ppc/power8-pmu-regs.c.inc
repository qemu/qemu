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
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
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
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
    } else {
        /* PMCC = 0b00 */
        gen_hvpriv_exception(ctx, POWERPC_EXCP_PRIV_REG);
    }

    return false;
}

/*
 * Helper function to avoid code repetition between MMCR0 and
 * MMCR2 problem state write functions.
 */
static TCGv masked_gprn_for_spr_write(int gprn, int sprn,
                                      uint64_t spr_mask)
{
    TCGv ret = tcg_temp_new();
    TCGv t0 = tcg_temp_new();

    /* 'ret' starts with all mask bits cleared */
    gen_load_spr(ret, sprn);
    tcg_gen_andi_tl(ret, ret, ~(spr_mask));

    /* Apply the mask into 'gprn' in a temp var */
    tcg_gen_andi_tl(t0, cpu_gpr[gprn], spr_mask);

    /* Add the masked gprn bits into 'ret' */
    tcg_gen_or_tl(ret, ret, t0);

    return ret;
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
}

static void write_MMCR0_common(DisasContext *ctx, TCGv val)
{
    /*
     * helper_store_mmcr0 will make clock based operations that
     * will cause 'bad icount read' errors if we do not execute
     * gen_icount_io_start() beforehand.
     */
    gen_icount_io_start(ctx);
    gen_helper_store_mmcr0(cpu_env, val);

    /*
     * End the translation block because MMCR0 writes can change
     * ctx->pmu_insn_cnt.
     */
    ctx->base.is_jmp = DISAS_EXIT_UPDATE;
}

void spr_write_MMCR0_ureg(DisasContext *ctx, int sprn, int gprn)
{
    TCGv masked_gprn;

    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    /*
     * Filter out all bits but FC, PMAO, and PMAE, according
     * to ISA v3.1, in 10.4.4 Monitor Mode Control Register 0,
     * fourth paragraph.
     */
    masked_gprn = masked_gprn_for_spr_write(gprn, SPR_POWER_MMCR0,
                                            MMCR0_UREG_MASK);
    write_MMCR0_common(ctx, masked_gprn);
}

void spr_read_MMCR2_ureg(DisasContext *ctx, int gprn, int sprn)
{
    TCGv t0;

    if (!spr_groupA_read_allowed(ctx)) {
        return;
    }

    t0 = tcg_temp_new();

    /*
     * On read, filter out all bits that are not FCnP0 bits.
     * When MMCR0[PMCC] is set to 0b10 or 0b11, providing
     * problem state programs read/write access to MMCR2,
     * only the FCnP0 bits can be accessed. All other bits are
     * not changed when mtspr is executed in problem state, and
     * all other bits return 0s when mfspr is executed in problem
     * state, according to ISA v3.1, section 10.4.6 Monitor Mode
     * Control Register 2, p. 1316, third paragraph.
     */
    gen_load_spr(t0, SPR_POWER_MMCR2);
    tcg_gen_andi_tl(t0, t0, MMCR2_UREG_MASK);
    tcg_gen_mov_tl(cpu_gpr[gprn], t0);
}

void spr_write_MMCR2_ureg(DisasContext *ctx, int sprn, int gprn)
{
    TCGv masked_gprn;

    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    /*
     * Filter the bits that can be written using MMCR2_UREG_MASK,
     * similar to what is done in spr_write_MMCR0_ureg().
     */
    masked_gprn = masked_gprn_for_spr_write(gprn, SPR_POWER_MMCR2,
                                            MMCR2_UREG_MASK);
    gen_store_spr(SPR_POWER_MMCR2, masked_gprn);
}

void spr_read_PMC(DisasContext *ctx, int gprn, int sprn)
{
    TCGv_i32 t_sprn = tcg_constant_i32(sprn);

    gen_icount_io_start(ctx);
    gen_helper_read_pmc(cpu_gpr[gprn], cpu_env, t_sprn);
}

void spr_read_PMC14_ureg(DisasContext *ctx, int gprn, int sprn)
{
    if (!spr_groupA_read_allowed(ctx)) {
        return;
    }

    spr_read_PMC(ctx, gprn, sprn + 0x10);
}

void spr_read_PMC56_ureg(DisasContext *ctx, int gprn, int sprn)
{
    /*
     * If PMCC = 0b11, PMC5 and PMC6 aren't included in the Performance
     * Monitor, and a read attempt results in a Facility Unavailable
     * Interrupt.
     */
    if (ctx->mmcr0_pmcc0 && ctx->mmcr0_pmcc1) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
        return;
    }

    /* The remaining steps are similar to PMCs 1-4 userspace read */
    spr_read_PMC14_ureg(ctx, gprn, sprn);
}

void spr_write_PMC(DisasContext *ctx, int sprn, int gprn)
{
    TCGv_i32 t_sprn = tcg_constant_i32(sprn);

    gen_icount_io_start(ctx);
    gen_helper_store_pmc(cpu_env, t_sprn, cpu_gpr[gprn]);
}

void spr_write_PMC14_ureg(DisasContext *ctx, int sprn, int gprn)
{
    if (!spr_groupA_write_allowed(ctx)) {
        return;
    }

    spr_write_PMC(ctx, sprn + 0x10, gprn);
}

void spr_write_PMC56_ureg(DisasContext *ctx, int sprn, int gprn)
{
    /*
     * If PMCC = 0b11, PMC5 and PMC6 aren't included in the Performance
     * Monitor, and a write attempt results in a Facility Unavailable
     * Interrupt.
     */
    if (ctx->mmcr0_pmcc0 && ctx->mmcr0_pmcc1) {
        gen_exception_err(ctx, POWERPC_EXCP_FU, FSCR_IC_PMU);
        return;
    }

    /* The remaining steps are similar to PMCs 1-4 userspace write */
    spr_write_PMC14_ureg(ctx, sprn, gprn);
}

void spr_write_MMCR0(DisasContext *ctx, int sprn, int gprn)
{
    write_MMCR0_common(ctx, cpu_gpr[gprn]);
}

void spr_write_MMCR1(DisasContext *ctx, int sprn, int gprn)
{
    gen_icount_io_start(ctx);
    gen_helper_store_mmcr1(cpu_env, cpu_gpr[gprn]);
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

void spr_read_MMCR2_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_write_MMCR2_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_read_PMC14_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_read_PMC56_ureg(DisasContext *ctx, int gprn, int sprn)
{
    spr_read_ureg(ctx, gprn, sprn);
}

void spr_write_PMC14_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_write_PMC56_ureg(DisasContext *ctx, int sprn, int gprn)
{
    spr_noaccess(ctx, gprn, sprn);
}

void spr_write_MMCR0(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);
}

void spr_write_MMCR1(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);
}

void spr_write_PMC(DisasContext *ctx, int sprn, int gprn)
{
    spr_write_generic(ctx, sprn, gprn);
}
#endif /* defined(TARGET_PPC64) && !defined(CONFIG_USER_ONLY) */
