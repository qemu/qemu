/*
 * OMAP clocks.
 *
 * Copyright (C) 2006-2007 Andrzej Zaborowski  <balrog@zabor.org>
 *
 * Clocks data comes in part from arch/arm/mach-omap1/clock.h in Linux.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */
#include "hw.h"
#include "omap.h"

struct clk {
    const char *name;
    const char *alias;
    struct clk *parent;
    struct clk *child1;
    struct clk *sibling;
#define ALWAYS_ENABLED		(1 << 0)
#define CLOCK_IN_OMAP310	(1 << 10)
#define CLOCK_IN_OMAP730	(1 << 11)
#define CLOCK_IN_OMAP1510	(1 << 12)
#define CLOCK_IN_OMAP16XX	(1 << 13)
    uint32_t flags;
    int id;

    int running;		/* Is currently ticking */
    int enabled;		/* Is enabled, regardless of its input clk */
    unsigned long rate;		/* Current rate (if .running) */
    unsigned int divisor;	/* Rate relative to input (if .enabled) */
    unsigned int multiplier;	/* Rate relative to input (if .enabled) */
    qemu_irq users[16];		/* Who to notify on change */
    int usecount;		/* Automatically idle when unused */
};

static struct clk xtal_osc12m = {
    .name	= "xtal_osc_12m",
    .rate	= 12000000,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk xtal_osc32k = {
    .name	= "xtal_osc_32k",
    .rate	= 32768,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk ck_ref = {
    .name	= "ck_ref",
    .alias	= "clkin",
    .parent	= &xtal_osc12m,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

/* If a dpll is disabled it becomes a bypass, child clocks don't stop */
static struct clk dpll1 = {
    .name	= "dpll1",
    .parent	= &ck_ref,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk dpll2 = {
    .name	= "dpll2",
    .parent	= &ck_ref,
    .flags	= CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
};

static struct clk dpll3 = {
    .name	= "dpll3",
    .parent	= &ck_ref,
    .flags	= CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
};

static struct clk dpll4 = {
    .name	= "dpll4",
    .parent	= &ck_ref,
    .multiplier	= 4,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk apll = {
    .name	= "apll",
    .parent	= &ck_ref,
    .multiplier	= 48,
    .divisor	= 12,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk ck_48m = {
    .name	= "ck_48m",
    .parent	= &dpll4,	/* either dpll4 or apll */
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk ck_dpll1out = {
    .name	= "ck_dpll1out",
    .parent	= &dpll1,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk sossi_ck = {
    .name	= "ck_sossi",
    .parent	= &ck_dpll1out,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk clkm1 = {
    .name	= "clkm1",
    .alias	= "ck_gen1",
    .parent	= &dpll1,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk clkm2 = {
    .name	= "clkm2",
    .alias	= "ck_gen2",
    .parent	= &dpll1,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk clkm3 = {
    .name	= "clkm3",
    .alias	= "ck_gen3",
    .parent	= &dpll1,	/* either dpll1 or ck_ref */
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk arm_ck = {
    .name	= "arm_ck",
    .alias	= "mpu_ck",
    .parent	= &clkm1,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk armper_ck = {
    .name	= "armper_ck",
    .alias	= "mpuper_ck",
    .parent	= &clkm1,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk arm_gpio_ck = {
    .name	= "arm_gpio_ck",
    .alias	= "mpu_gpio_ck",
    .parent	= &clkm1,
    .divisor	= 1,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
};

static struct clk armxor_ck = {
    .name	= "armxor_ck",
    .alias	= "mpuxor_ck",
    .parent	= &ck_ref,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk armtim_ck = {
    .name	= "armtim_ck",
    .alias	= "mputim_ck",
    .parent	= &ck_ref,	/* either CLKIN or DPLL1 */
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk armwdt_ck = {
    .name	= "armwdt_ck",
    .alias	= "mpuwd_ck",
    .parent	= &clkm1,
    .divisor	= 14,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk arminth_ck16xx = {
    .name	= "arminth_ck",
    .parent	= &arm_ck,
    .flags	= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
    /* Note: On 16xx the frequency can be divided by 2 by programming
     * ARM_CKCTL:ARM_INTHCK_SEL(14) to 1
     *
     * 1510 version is in TC clocks.
     */
};

static struct clk dsp_ck = {
    .name	= "dsp_ck",
    .parent	= &clkm2,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
};

static struct clk dspmmu_ck = {
    .name	= "dspmmu_ck",
    .parent	= &clkm2,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
            ALWAYS_ENABLED,
};

static struct clk dspper_ck = {
    .name	= "dspper_ck",
    .parent	= &clkm2,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
};

static struct clk dspxor_ck = {
    .name	= "dspxor_ck",
    .parent	= &ck_ref,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
};

static struct clk dsptim_ck = {
    .name	= "dsptim_ck",
    .parent	= &ck_ref,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
};

static struct clk tc_ck = {
    .name	= "tc_ck",
    .parent	= &clkm3,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
            CLOCK_IN_OMAP730 | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk arminth_ck15xx = {
    .name	= "arminth_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
    /* Note: On 1510 the frequency follows TC_CK
     *
     * 16xx version is in MPU clocks.
     */
};

static struct clk tipb_ck = {
    /* No-idle controlled by "tc_ck" */
    .name	= "tipb_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
};

static struct clk l3_ocpi_ck = {
    /* No-idle controlled by "tc_ck" */
    .name	= "l3_ocpi_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk tc1_ck = {
    .name	= "tc1_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk tc2_ck = {
    .name	= "tc2_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk dma_ck = {
    /* No-idle controlled by "tc_ck" */
    .name	= "dma_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk dma_lcdfree_ck = {
    .name	= "dma_lcdfree_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
};

static struct clk api_ck = {
    .name	= "api_ck",
    .alias	= "mpui_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk lb_ck = {
    .name	= "lb_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
};

static struct clk lbfree_ck = {
    .name	= "lbfree_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
};

static struct clk hsab_ck = {
    .name	= "hsab_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
};

static struct clk rhea1_ck = {
    .name	= "rhea1_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
};

static struct clk rhea2_ck = {
    .name	= "rhea2_ck",
    .parent	= &tc_ck,
    .flags	= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
};

static struct clk lcd_ck_16xx = {
    .name	= "lcd_ck",
    .parent	= &clkm3,
    .flags	= CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP730,
};

static struct clk lcd_ck_1510 = {
    .name	= "lcd_ck",
    .parent	= &clkm3,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
};

static struct clk uart1_1510 = {
    .name	= "uart1_ck",
    /* Direct from ULPD, no real parent */
    .parent	= &armper_ck,	/* either armper_ck or dpll4 */
    .rate	= 12000000,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
};

static struct clk uart1_16xx = {
    .name	= "uart1_ck",
    /* Direct from ULPD, no real parent */
    .parent	= &armper_ck,
    .rate	= 48000000,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk uart2_ck = {
    .name	= "uart2_ck",
    /* Direct from ULPD, no real parent */
    .parent	= &armper_ck,	/* either armper_ck or dpll4 */
    .rate	= 12000000,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            ALWAYS_ENABLED,
};

static struct clk uart3_1510 = {
    .name	= "uart3_ck",
    /* Direct from ULPD, no real parent */
    .parent	= &armper_ck,	/* either armper_ck or dpll4 */
    .rate	= 12000000,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310 | ALWAYS_ENABLED,
};

static struct clk uart3_16xx = {
    .name	= "uart3_ck",
    /* Direct from ULPD, no real parent */
    .parent	= &armper_ck,
    .rate	= 48000000,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk usb_clk0 = {	/* 6 MHz output on W4_USB_CLK0 */
    .name	= "usb_clk0",
    .alias	= "usb.clko",
    /* Direct from ULPD, no parent */
    .rate	= 6000000,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk usb_hhc_ck1510 = {
    .name	= "usb_hhc_ck",
    /* Direct from ULPD, no parent */
    .rate	= 48000000, /* Actually 2 clocks, 12MHz and 48MHz */
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP310,
};

static struct clk usb_hhc_ck16xx = {
    .name	= "usb_hhc_ck",
    /* Direct from ULPD, no parent */
    .rate	= 48000000,
    /* OTG_SYSCON_2.OTG_PADEN == 0 (not 1510-compatible) */
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk usb_w2fc_mclk = {
    .name	= "usb_w2fc_mclk",
    .alias	= "usb_w2fc_ck",
    .parent	= &ck_48m,
    .rate	= 48000000,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
};

static struct clk mclk_1510 = {
    .name	= "mclk",
    /* Direct from ULPD, no parent. May be enabled by ext hardware. */
    .rate	= 12000000,
    .flags	= CLOCK_IN_OMAP1510,
};

static struct clk bclk_310 = {
    .name	= "bt_mclk_out",	/* Alias midi_mclk_out? */
    .parent	= &armper_ck,
    .flags	= CLOCK_IN_OMAP310,
};

static struct clk mclk_310 = {
    .name	= "com_mclk_out",
    .parent	= &armper_ck,
    .flags	= CLOCK_IN_OMAP310,
};

static struct clk mclk_16xx = {
    .name	= "mclk",
    /* Direct from ULPD, no parent. May be enabled by ext hardware. */
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk bclk_1510 = {
    .name	= "bclk",
    /* Direct from ULPD, no parent. May be enabled by ext hardware. */
    .rate	= 12000000,
    .flags	= CLOCK_IN_OMAP1510,
};

static struct clk bclk_16xx = {
    .name	= "bclk",
    /* Direct from ULPD, no parent. May be enabled by ext hardware. */
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk mmc1_ck = {
    .name	= "mmc_ck",
    .id		= 1,
    /* Functional clock is direct from ULPD, interface clock is ARMPER */
    .parent	= &armper_ck,	/* either armper_ck or dpll4 */
    .rate	= 48000000,
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310,
};

static struct clk mmc2_ck = {
    .name	= "mmc_ck",
    .id		= 2,
    /* Functional clock is direct from ULPD, interface clock is ARMPER */
    .parent	= &armper_ck,
    .rate	= 48000000,
    .flags	= CLOCK_IN_OMAP16XX,
};

static struct clk cam_mclk = {
    .name	= "cam.mclk",
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
    .rate	= 12000000,
};

static struct clk cam_exclk = {
    .name	= "cam.exclk",
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
    /* Either 12M from cam.mclk or 48M from dpll4 */
    .parent	= &cam_mclk,
};

static struct clk cam_lclk = {
    .name	= "cam.lclk",
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX,
};

static struct clk i2c_fck = {
    .name	= "i2c_fck",
    .id		= 1,
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
            ALWAYS_ENABLED,
    .parent	= &armxor_ck,
};

static struct clk i2c_ick = {
    .name	= "i2c_ick",
    .id		= 1,
    .flags	= CLOCK_IN_OMAP16XX | ALWAYS_ENABLED,
    .parent	= &armper_ck,
};

static struct clk clk32k = {
    .name	= "clk32-kHz",
    .flags	= CLOCK_IN_OMAP310 | CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX |
            ALWAYS_ENABLED,
    .parent     = &xtal_osc32k,
};

static struct clk *onchip_clks[] = {
    /* non-ULPD clocks */
    &xtal_osc12m,
    &xtal_osc32k,
    &ck_ref,
    &dpll1,
    &dpll2,
    &dpll3,
    &dpll4,
    &apll,
    &ck_48m,
    /* CK_GEN1 clocks */
    &clkm1,
    &ck_dpll1out,
    &sossi_ck,
    &arm_ck,
    &armper_ck,
    &arm_gpio_ck,
    &armxor_ck,
    &armtim_ck,
    &armwdt_ck,
    &arminth_ck15xx,  &arminth_ck16xx,
    /* CK_GEN2 clocks */
    &clkm2,
    &dsp_ck,
    &dspmmu_ck,
    &dspper_ck,
    &dspxor_ck,
    &dsptim_ck,
    /* CK_GEN3 clocks */
    &clkm3,
    &tc_ck,
    &tipb_ck,
    &l3_ocpi_ck,
    &tc1_ck,
    &tc2_ck,
    &dma_ck,
    &dma_lcdfree_ck,
    &api_ck,
    &lb_ck,
    &lbfree_ck,
    &hsab_ck,
    &rhea1_ck,
    &rhea2_ck,
    &lcd_ck_16xx,
    &lcd_ck_1510,
    /* ULPD clocks */
    &uart1_1510,
    &uart1_16xx,
    &uart2_ck,
    &uart3_1510,
    &uart3_16xx,
    &usb_clk0,
    &usb_hhc_ck1510, &usb_hhc_ck16xx,
    &mclk_1510,  &mclk_16xx, &mclk_310,
    &bclk_1510,  &bclk_16xx, &bclk_310,
    &mmc1_ck,
    &mmc2_ck,
    &cam_mclk,
    &cam_exclk,
    &cam_lclk,
    &clk32k,
    &usb_w2fc_mclk,
    /* Virtual clocks */
    &i2c_fck,
    &i2c_ick,
    0
};

void omap_clk_adduser(struct clk *clk, qemu_irq user)
{
    qemu_irq *i;

    for (i = clk->users; *i; i ++);
    *i = user;
}

/* If a clock is allowed to idle, it is disabled automatically when
 * all of clock domains using it are disabled.  */
int omap_clk_is_idle(struct clk *clk)
{
    struct clk *chld;

    if (!clk->enabled && (!clk->usecount || !(clk->flags && ALWAYS_ENABLED)))
        return 1;
    if (clk->usecount)
        return 0;

    for (chld = clk->child1; chld; chld = chld->sibling)
        if (!omap_clk_is_idle(chld))
            return 0;
    return 1;
}

struct clk *omap_findclk(struct omap_mpu_state_s *mpu, const char *name)
{
    struct clk *i;

    for (i = mpu->clks; i->name; i ++)
        if (!strcmp(i->name, name) || (i->alias && !strcmp(i->alias, name)))
            return i;
    cpu_abort(mpu->env, "%s: %s not found\n", __FUNCTION__, name);
}

void omap_clk_get(struct clk *clk)
{
    clk->usecount ++;
}

void omap_clk_put(struct clk *clk)
{
    if (!(clk->usecount --))
        cpu_abort(cpu_single_env, "%s: %s is not in use\n",
                        __FUNCTION__, clk->name);
}

static void omap_clk_update(struct clk *clk)
{
    int parent, running;
    qemu_irq *user;
    struct clk *i;

    if (clk->parent)
        parent = clk->parent->running;
    else
        parent = 1;

    running = parent && (clk->enabled ||
                    ((clk->flags & ALWAYS_ENABLED) && clk->usecount));
    if (clk->running != running) {
        clk->running = running;
        for (user = clk->users; *user; user ++)
            qemu_set_irq(*user, running);
        for (i = clk->child1; i; i = i->sibling)
            omap_clk_update(i);
    }
}

static void omap_clk_rate_update_full(struct clk *clk, unsigned long int rate,
                unsigned long int div, unsigned long int mult)
{
    struct clk *i;
    qemu_irq *user;

    clk->rate = muldiv64(rate, mult, div);
    if (clk->running)
        for (user = clk->users; *user; user ++)
            qemu_irq_raise(*user);
    for (i = clk->child1; i; i = i->sibling)
        omap_clk_rate_update_full(i, rate,
                        div * i->divisor, mult * i->multiplier);
}

static void omap_clk_rate_update(struct clk *clk)
{
    struct clk *i;
    unsigned long int div, mult = div = 1;

    for (i = clk; i->parent; i = i->parent) {
        div *= i->divisor;
        mult *= i->multiplier;
    }

    omap_clk_rate_update_full(clk, i->rate, div, mult);
}

void omap_clk_reparent(struct clk *clk, struct clk *parent)
{
    struct clk **p;

    if (clk->parent) {
        for (p = &clk->parent->child1; *p != clk; p = &(*p)->sibling);
        *p = clk->sibling;
    }

    clk->parent = parent;
    if (parent) {
        clk->sibling = parent->child1;
        parent->child1 = clk;
        omap_clk_update(clk);
        omap_clk_rate_update(clk);
    } else
        clk->sibling = 0;
}

void omap_clk_onoff(struct clk *clk, int on)
{
    clk->enabled = on;
    omap_clk_update(clk);
}

void omap_clk_canidle(struct clk *clk, int can)
{
    if (can)
        omap_clk_put(clk);
    else
        omap_clk_get(clk);
}

void omap_clk_setrate(struct clk *clk, int divide, int multiply)
{
    clk->divisor = divide;
    clk->multiplier = multiply;
    omap_clk_rate_update(clk);
}

int64_t omap_clk_getrate(omap_clk clk)
{
    return clk->rate;
}

void omap_clk_init(struct omap_mpu_state_s *mpu)
{
    struct clk **i, *j, *k;
    int count;
    int flag;

    if (cpu_is_omap310(mpu))
        flag = CLOCK_IN_OMAP310;
    else if (cpu_is_omap1510(mpu))
        flag = CLOCK_IN_OMAP1510;
    else
        return;

    for (i = onchip_clks, count = 0; *i; i ++)
        if ((*i)->flags & flag)
            count ++;
    mpu->clks = (struct clk *) qemu_mallocz(sizeof(struct clk) * (count + 1));
    for (i = onchip_clks, j = mpu->clks; *i; i ++)
        if ((*i)->flags & flag) {
            memcpy(j, *i, sizeof(struct clk));
            for (k = mpu->clks; k < j; k ++)
                if (j->parent && !strcmp(j->parent->name, k->name)) {
                    j->parent = k;
                    j->sibling = k->child1;
                    k->child1 = j;
                } else if (k->parent && !strcmp(k->parent->name, j->name)) {
                    k->parent = j;
                    k->sibling = j->child1;
                    j->child1 = k;
                }
            j->divisor = j->divisor ?: 1;
            j->multiplier = j->multiplier ?: 1;
            j ++;
        }
    for (j = mpu->clks; count --; j ++) {
        omap_clk_update(j);
        omap_clk_rate_update(j);
    }
}
