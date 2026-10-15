/*
 * STM32F405 RCC tests
 *
 * Copyright (c) 2026 Manuele Conti <manuele.conti@archimede-energia.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define RCC_BASE        0x40023800
#define RCC_CR          (RCC_BASE + 0x00)
#define RCC_CFGR        (RCC_BASE + 0x08)

#define CR_HSION        (1U << 0)
#define CR_HSIRDY       (1U << 1)
#define CR_HSEON        (1U << 16)
#define CR_HSERDY       (1U << 17)
#define CR_PLLON        (1U << 24)
#define CR_PLLRDY       (1U << 25)
#define CR_PLLI2SON     (1U << 26)
#define CR_PLLI2SRDY    (1U << 27)

static void test_cr_ready_bits(void)
{
    uint32_t on = CR_HSION | CR_HSEON | CR_PLLON | CR_PLLI2SON;
    uint32_t ready = CR_HSIRDY | CR_HSERDY | CR_PLLRDY | CR_PLLI2SRDY;

    writel(RCC_CR, on);
    g_assert_cmphex(readl(RCC_CR), ==, on | ready);

    writel(RCC_CR, 0);
    g_assert_cmphex(readl(RCC_CR), ==, 0);
}

static void test_cfgr_switch_status(void)
{
    unsigned int sw;

    for (sw = 0; sw < 4; sw++) {
        writel(RCC_CFGR, sw);
        g_assert_cmphex(readl(RCC_CFGR), ==, sw | (sw << 2));
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/stm32f405/rcc/cr-ready-bits", test_cr_ready_bits);
    qtest_add_func("/stm32f405/rcc/cfgr-switch-status",
                   test_cfgr_switch_status);

    qtest_start("-machine olimex-stm32-h405");
    ret = g_test_run();
    qtest_end();

    return ret;
}
