/*
 * OMAP clocks.
 *
 * Copyright (C) 2006-2008 Andrzej Zaborowski  <balrog@zabor.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, see <http://www.gnu.org/licenses/>.
 */
#include "qemu/osdep.h"
#include "hw/hw.h"
#include "hw/arm/omap.h"

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
#define CLOCK_IN_OMAP242X	(1 << 14)
#define CLOCK_IN_OMAP243X	(1 << 15)
#define CLOCK_IN_OMAP343X	(1 << 16)
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
    .flags	= CLOCK_IN_OMAP1510 | CLOCK_IN_OMAP16XX | CLOCK_IN_OMAP310 |
            CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
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
            CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .parent	= &xtal_osc32k,
};

static struct clk ref_clk = {
    .name	= "ref_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .rate	= 12000000,	/* 12 MHz or 13 MHz or 19.2 MHz */
    /*.parent	= sys.xtalin */
};

static struct clk apll_96m = {
    .name	= "apll_96m",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .rate	= 96000000,
    /*.parent	= ref_clk */
};

static struct clk apll_54m = {
    .name	= "apll_54m",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .rate	= 54000000,
    /*.parent	= ref_clk */
};

static struct clk sys_clk = {
    .name	= "sys_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .rate	= 32768,
    /*.parent	= sys.xtalin */
};

static struct clk sleep_clk = {
    .name	= "sleep_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .rate	= 32768,
    /*.parent	= sys.xtalin */
};

static struct clk dpll_ck = {
    .name	= "dpll",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .parent	= &ref_clk,
};

static struct clk dpll_x2_ck = {
    .name	= "dpll_x2",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .parent	= &ref_clk,
};

static struct clk wdt1_sys_clk = {
    .name	= "wdt1_sys_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X | ALWAYS_ENABLED,
    .rate	= 32768,
    /*.parent	= sys.xtalin */
};

static struct clk func_96m_clk = {
    .name	= "func_96m_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .divisor	= 1,
    .parent	= &apll_96m,
};

static struct clk func_48m_clk = {
    .name	= "func_48m_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .divisor	= 2,
    .parent	= &apll_96m,
};

static struct clk func_12m_clk = {
    .name	= "func_12m_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .divisor	= 8,
    .parent	= &apll_96m,
};

static struct clk func_54m_clk = {
    .name	= "func_54m_clk",
    .flags	= CLOCK_IN_OMAP242X,
    .divisor	= 1,
    .parent	= &apll_54m,
};

static struct clk sys_clkout = {
    .name	= "clkout",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk sys_clkout2 = {
    .name	= "clkout2",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_clk = {
    .name	= "core_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &dpll_x2_ck,	/* Switchable between dpll_ck and clk32k */
};

static struct clk l3_clk = {
    .name	= "l3_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_clk,
};

static struct clk core_l4_iclk = {
    .name	= "core_l4_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &l3_clk,
};

static struct clk wu_l4_iclk = {
    .name	= "wu_l4_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &l3_clk,
};

static struct clk core_l3_iclk = {
    .name	= "core_l3_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_clk,
};

static struct clk core_l4_usb_clk = {
    .name	= "core_l4_usb_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &l3_clk,
};

static struct clk wu_gpt1_clk = {
    .name	= "wu_gpt1_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk wu_32k_clk = {
    .name	= "wu_32k_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk uart1_fclk = {
    .name	= "uart1_fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_48m_clk,
};

static struct clk uart1_iclk = {
    .name	= "uart1_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l4_iclk,
};

static struct clk uart2_fclk = {
    .name	= "uart2_fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_48m_clk,
};

static struct clk uart2_iclk = {
    .name	= "uart2_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l4_iclk,
};

static struct clk uart3_fclk = {
    .name	= "uart3_fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_48m_clk,
};

static struct clk uart3_iclk = {
    .name	= "uart3_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l4_iclk,
};

static struct clk mpu_fclk = {
    .name	= "mpu_fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_clk,
};

static struct clk mpu_iclk = {
    .name	= "mpu_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_clk,
};

static struct clk int_m_fclk = {
    .name	= "int_m_fclk",
    .alias	= "mpu_intc_fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_clk,
};

static struct clk int_m_iclk = {
    .name	= "int_m_iclk",
    .alias	= "mpu_intc_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_clk,
};

static struct clk core_gpt2_clk = {
    .name	= "core_gpt2_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt3_clk = {
    .name	= "core_gpt3_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt4_clk = {
    .name	= "core_gpt4_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt5_clk = {
    .name	= "core_gpt5_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt6_clk = {
    .name	= "core_gpt6_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt7_clk = {
    .name	= "core_gpt7_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt8_clk = {
    .name	= "core_gpt8_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt9_clk = {
    .name	= "core_gpt9_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt10_clk = {
    .name	= "core_gpt10_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt11_clk = {
    .name	= "core_gpt11_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk core_gpt12_clk = {
    .name	= "core_gpt12_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &sys_clk,
};

static struct clk mcbsp1_clk = {
    .name	= "mcbsp1_cg",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .divisor	= 2,
    .parent	= &func_96m_clk,
};

static struct clk mcbsp2_clk = {
    .name	= "mcbsp2_cg",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .divisor	= 2,
    .parent	= &func_96m_clk,
};

static struct clk emul_clk = {
    .name	= "emul_ck",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_54m_clk,
};

static struct clk sdma_fclk = {
    .name	= "sdma_fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &l3_clk,
};

static struct clk sdma_iclk = {
    .name	= "sdma_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l3_iclk, /* core_l4_iclk for the configuration port */
};

static struct clk i2c1_fclk = {
    .name	= "i2c1.fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_12m_clk,
    .divisor	= 1,
};

static struct clk i2c1_iclk = {
    .name	= "i2c1.iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l4_iclk,
};

static struct clk i2c2_fclk = {
    .name	= "i2c2.fclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_12m_clk,
    .divisor	= 1,
};

static struct clk i2c2_iclk = {
    .name	= "i2c2.iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l4_iclk,
};

static struct clk gpio_dbclk[5] = {
    {
        .name	= "gpio1_dbclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &wu_32k_clk,
    }, {
        .name	= "gpio2_dbclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &wu_32k_clk,
    }, {
        .name	= "gpio3_dbclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &wu_32k_clk,
    }, {
        .name	= "gpio4_dbclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &wu_32k_clk,
    }, {
        .name   = "gpio5_dbclk",
        .flags  = CLOCK_IN_OMAP243X,
        .parent = &wu_32k_clk,
    },
};

static struct clk gpio_iclk = {
    .name	= "gpio_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &wu_l4_iclk,
};

static struct clk mmc_fck = {
    .name	= "mmc_fclk",
    .flags	= CLOCK_IN_OMAP242X,
    .parent	= &func_96m_clk,
};

static struct clk mmc_ick = {
    .name	= "mmc_iclk",
    .flags	= CLOCK_IN_OMAP242X,
    .parent	= &core_l4_iclk,
};

static struct clk spi_fclk[3] = {
    {
        .name	= "spi1_fclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &func_48m_clk,
    }, {
        .name	= "spi2_fclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &func_48m_clk,
    }, {
        .name	= "spi3_fclk",
        .flags	= CLOCK_IN_OMAP243X,
        .parent	= &func_48m_clk,
    },
};

static struct clk dss_clk[2] = {
    {
        .name	= "dss_clk1",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &core_clk,
    }, {
        .name	= "dss_clk2",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &sys_clk,
    },
};

static struct clk dss_54m_clk = {
    .name	= "dss_54m_clk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &func_54m_clk,
};

static struct clk dss_l3_iclk = {
    .name	= "dss_l3_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l3_iclk,
};

static struct clk dss_l4_iclk = {
    .name	= "dss_l4_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    .parent	= &core_l4_iclk,
};

static struct clk spi_iclk[3] = {
    {
        .name	= "spi1_iclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &core_l4_iclk,
    }, {
        .name	= "spi2_iclk",
        .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
        .parent	= &core_l4_iclk,
    }, {
        .name	= "spi3_iclk",
        .flags	= CLOCK_IN_OMAP243X,
        .parent	= &core_l4_iclk,
    },
};

static struct clk omapctrl_clk = {
    .name	= "omapctrl_iclk",
    .flags	= CLOCK_IN_OMAP242X | CLOCK_IN_OMAP243X,
    /* XXX Should be in WKUP domain */
    .parent	= &core_l4_iclk,
};

static struct clk *onchip_clks[] = {
    /* OMAP 1 */

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

    /* OMAP 2 */

    &ref_clk,
    &apll_96m,
    &apll_54m,
    &sys_clk,
    &sleep_clk,
    &dpll_ck,
    &dpll_x2_ck,
    &wdt1_sys_clk,
    &func_96m_clk,
    &func_48m_clk,
    &func_12m_clk,
    &func_54m_clk,
    &sys_clkout,
    &sys_clkout2,
    &core_clk,
    &l3_clk,
    &core_l4_iclk,
    &wu_l4_iclk,
    &core_l3_iclk,
    &core_l4_usb_clk,
    &wu_gpt1_clk,
    &wu_32k_clk,
    &uart1_fclk,
    &uart1_iclk,
    &uart2_fclk,
    &uart2_iclk,
    &uart3_fclk,
    &uart3_iclk,
    &mpu_fclk,
    &mpu_iclk,
    &int_m_fclk,
    &int_m_iclk,
    &core_gpt2_clk,
    &core_gpt3_clk,
    &core_gpt4_clk,
    &core_gpt5_clk,
    &core_gpt6_clk,
    &core_gpt7_clk,
    &core_gpt8_clk,
    &core_gpt9_clk,
    &core_gpt10_clk,
    &core_gpt11_clk,
    &core_gpt12_clk,
    &mcbsp1_clk,
    &mcbsp2_clk,
    &emul_clk,
    &sdma_fclk,
    &sdma_iclk,
    &i2c1_fclk,
    &i2c1_iclk,
    &i2c2_fclk,
    &i2c2_iclk,
    &gpio_dbclk[0],
    &gpio_dbclk[1],
    &gpio_dbclk[2],
    &gpio_dbclk[3],
    &gpio_iclk,
    &mmc_fck,
    &mmc_ick,
    &spi_fclk[0],
    &spi_iclk[0],
    &spi_fclk[1],
    &spi_iclk[1],
    &spi_fclk[2],
    &spi_iclk[2],
    &dss_clk[0],
    &dss_clk[1],
    &dss_54m_clk,
    &dss_l3_iclk,
    &dss_l4_iclk,
    &omapctrl_clk,

    NULL
};

void omap_clk_adduser(struct clk *clk, qemu_irq user)
{
    qemu_irq *i;

    for (i = clk->users; *i; i ++);
    *i = user;
}

struct clk *omap_findclk(struct omap_mpu_state_s *mpu, const char *name)
{
    struct clk *i;

    for (i = mpu->clks; i->name; i ++)
        if (!strcmp(i->name, name) || (i->alias && !strcmp(i->alias, name)))
            return i;
    hw_error("%s: %s not found\n", __func__, name);
}

void omap_clk_get(struct clk *clk)
{
    clk->usecount ++;
}

void omap_clk_put(struct clk *clk)
{
    if (!(clk->usecount --))
        hw_error("%s: %s is not in use\n", __func__, clk->name);
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
        clk->sibling = NULL;
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
    else if (cpu_is_omap2410(mpu) || cpu_is_omap2420(mpu))
        flag = CLOCK_IN_OMAP242X;
    else if (cpu_is_omap2430(mpu))
        flag = CLOCK_IN_OMAP243X;
    else if (cpu_is_omap3430(mpu))
        flag = CLOCK_IN_OMAP243X;
    else
        return;

    for (i = onchip_clks, count = 0; *i; i ++)
        if ((*i)->flags & flag)
            count ++;
    mpu->clks = g_new0(struct clk, count + 1);
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
