/*
 * QTest testcase for STM32L4x5_RCC
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/registerfields.h"
#include "libqtest-single.h"
#include "hw/misc/stm32l4x5_rcc_internals.h"

#define RCC_BASE_ADDR 0x40021000
#define NVIC_ISER 0xE000E100
#define NVIC_ISPR 0xE000E200
#define NVIC_ICPR 0xE000E280
#define RCC_IRQ 5

static void enable_nvic_irq(unsigned int n)
{
    writel(NVIC_ISER, 1 << n);
}

static void unpend_nvic_irq(unsigned int n)
{
    writel(NVIC_ICPR, 1 << n);
}

static bool check_nvic_pending(unsigned int n)
{
    return readl(NVIC_ISPR) & (1 << n);
}

static void rcc_writel(unsigned int offset, uint32_t value)
{
    writel(RCC_BASE_ADDR + offset, value);
}

static uint32_t rcc_readl(unsigned int offset)
{
    return readl(RCC_BASE_ADDR + offset);
}

static void test_init_msi(void)
{
    /* MSIRANGE can be set only when MSI is OFF or READY */
    rcc_writel(A_CR, R_CR_MSION_MASK);
    /* Wait until MSI is stable */
    g_assert_true((rcc_readl(A_CR) & R_CR_MSIRDY_MASK) == R_CR_MSIRDY_MASK);
    /* TODO find a way to test MSI value */
}

static void test_set_msi_as_sysclk(void)
{
    /* Clocking from MSI, in case MSI was not the default source */
    rcc_writel(A_CFGR, 0);
    /* Wait until MSI is selected and stable */
    g_assert_true((rcc_readl(A_CFGR) & R_CFGR_SWS_MASK) == 0);
}

static void test_init_pll(void)
{
    uint32_t value;

    /*
     * Update PLL and set MSI as the source clock.
     * PLLM = 1 --> 000
     * PLLN = 40 --> 40
     * PPLLR = 2 --> 00
     * PLLDIV = unused, PLLP = unused (SAI3), PLLQ = unused (48M1)
     * SRC = MSI --> 01
     */
    rcc_writel(A_PLLCFGR, R_PLLCFGR_PLLREN_MASK |
            (40 << R_PLLCFGR_PLLN_SHIFT) |
            (0b01 << R_PLLCFGR_PLLSRC_SHIFT));

    /* PLL activation */
    value = rcc_readl(A_CR);
    rcc_writel(A_CR, value | R_CR_PLLON_MASK);

    /* Waiting for PLL lock. */
    g_assert_true((rcc_readl(A_CR) & R_CR_PLLRDY_MASK) == R_CR_PLLRDY_MASK);

    /* Switches on the PLL clock source */
    value = rcc_readl(A_CFGR);
    rcc_writel(A_CFGR, (value & ~R_CFGR_SW_MASK) |
        (0b11 << R_CFGR_SW_SHIFT));

    /* Wait until SYSCLK is stable. */
    g_assert_true((rcc_readl(A_CFGR) & R_CFGR_SWS_MASK) ==
        (0b11 << R_CFGR_SWS_SHIFT));
}

static void test_activate_lse(void)
{
    /* LSE activation, no LSE Bypass */
    rcc_writel(A_BDCR, R_BDCR_LSEDRV_MASK | R_BDCR_LSEON_MASK);
    g_assert_true((rcc_readl(A_BDCR) & R_BDCR_LSERDY_MASK) == R_BDCR_LSERDY_MASK);
}

static void test_irq(void)
{
    enable_nvic_irq(RCC_IRQ);

    rcc_writel(A_CIER, R_CIER_LSIRDYIE_MASK);
    rcc_writel(A_CSR, R_CSR_LSION_MASK);
    g_assert_true(check_nvic_pending(RCC_IRQ));
    rcc_writel(A_CICR, R_CICR_LSIRDYC_MASK);
    unpend_nvic_irq(RCC_IRQ);

    rcc_writel(A_CIER, R_CIER_LSERDYIE_MASK);
    rcc_writel(A_BDCR, R_BDCR_LSEON_MASK);
    g_assert_true(check_nvic_pending(RCC_IRQ));
    rcc_writel(A_CICR, R_CICR_LSERDYC_MASK);
    unpend_nvic_irq(RCC_IRQ);

    /*
     * MSI has been enabled by previous tests,
     * shouln't generate an interruption.
     */
    rcc_writel(A_CIER, R_CIER_MSIRDYIE_MASK);
    rcc_writel(A_CR, R_CR_MSION_MASK);
    g_assert_false(check_nvic_pending(RCC_IRQ));

    rcc_writel(A_CIER, R_CIER_HSIRDYIE_MASK);
    rcc_writel(A_CR, R_CR_HSION_MASK);
    g_assert_true(check_nvic_pending(RCC_IRQ));
    rcc_writel(A_CICR, R_CICR_HSIRDYC_MASK);
    unpend_nvic_irq(RCC_IRQ);

    rcc_writel(A_CIER, R_CIER_HSERDYIE_MASK);
    rcc_writel(A_CR, R_CR_HSEON_MASK);
    g_assert_true(check_nvic_pending(RCC_IRQ));
    rcc_writel(A_CICR, R_CICR_HSERDYC_MASK);
    unpend_nvic_irq(RCC_IRQ);

    /*
     * PLL has been enabled by previous tests,
     * shouln't generate an interruption.
     */
    rcc_writel(A_CIER, R_CIER_PLLRDYIE_MASK);
    rcc_writel(A_CR, R_CR_PLLON_MASK);
    g_assert_false(check_nvic_pending(RCC_IRQ));

    rcc_writel(A_CIER, R_CIER_PLLSAI1RDYIE_MASK);
    rcc_writel(A_CR, R_CR_PLLSAI1ON_MASK);
    g_assert_true(check_nvic_pending(RCC_IRQ));
    rcc_writel(A_CICR, R_CICR_PLLSAI1RDYC_MASK);
    unpend_nvic_irq(RCC_IRQ);

    rcc_writel(A_CIER, R_CIER_PLLSAI2RDYIE_MASK);
    rcc_writel(A_CR, R_CR_PLLSAI2ON_MASK);
    g_assert_true(check_nvic_pending(RCC_IRQ));
    rcc_writel(A_CICR, R_CICR_PLLSAI2RDYC_MASK);
    unpend_nvic_irq(RCC_IRQ);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();
    /*
     * These test separately that we can enable the plls, change the sysclk,
     * and enable different devices.
     * They are dependent on one another.
     * We assume that all operations that would take some time to have an effect
     * (e.g. changing the PLL frequency) are done instantaneously.
     */
    qtest_add_func("stm32l4x5/rcc/init_msi", test_init_msi);
    qtest_add_func("stm32l4x5/rcc/set_msi_as_sysclk",
        test_set_msi_as_sysclk);
    qtest_add_func("stm32l4x5/rcc/activate_lse", test_activate_lse);
    qtest_add_func("stm32l4x5/rcc/init_pll", test_init_pll);

    qtest_add_func("stm32l4x5/rcc/irq", test_irq);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
