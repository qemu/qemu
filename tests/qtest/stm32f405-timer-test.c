/*
 * STM32F405 timer tests
 *
 * Copyright (c) 2026 Manuele Conti <manuele.conti@archimede-energia.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define TIM2_BASE       0x40000000
#define TIM_CR1         (TIM2_BASE + 0x00)
#define TIM_DIER        (TIM2_BASE + 0x0c)
#define TIM_SR          (TIM2_BASE + 0x10)
#define TIM_EGR         (TIM2_BASE + 0x14)
#define TIM_CNT         (TIM2_BASE + 0x24)
#define TIM_PSC         (TIM2_BASE + 0x28)
#define TIM_ARR         (TIM2_BASE + 0x2c)

#define TIM_CR1_CEN     (1U << 0)
#define TIM_DIER_UIE    (1U << 0)
#define TIM_SR_UIF      (1U << 0)
#define TIM_EGR_UG      (1U << 0)

static void timer_reset(void)
{
    qtest_system_reset(global_qtest);
}

static void test_period_and_update(void)
{
    timer_reset();
    writel(TIM_PSC, 0);
    writel(TIM_ARR, 4);
    writel(TIM_CNT, 0);
    writel(TIM_DIER, TIM_DIER_UIE);
    writel(TIM_CR1, TIM_CR1_CEN);

    clock_step(4);
    g_assert_cmphex(readl(TIM_CNT), ==, 4);
    g_assert_cmphex(readl(TIM_SR), ==, 0);

    clock_step(1);
    g_assert_cmphex(readl(TIM_CNT), ==, 0);
    g_assert_cmphex(readl(TIM_SR), ==, TIM_SR_UIF);

    writel(TIM_SR, 0);
    g_assert_cmphex(readl(TIM_SR), ==, 0);
}

static void test_prescaler(void)
{
    timer_reset();
    writel(TIM_PSC, 9);
    writel(TIM_ARR, 4);
    writel(TIM_CNT, 0);
    writel(TIM_CR1, TIM_CR1_CEN);

    clock_step(40);
    g_assert_cmphex(readl(TIM_CNT), ==, 4);
    clock_step(10);
    g_assert_cmphex(readl(TIM_CNT), ==, 0);
}

static void test_stopped_counter(void)
{
    timer_reset();
    writel(TIM_ARR, 100);
    writel(TIM_CNT, 200);
    g_assert_cmphex(readl(TIM_CNT), ==, 200);

    writel(TIM_CNT, 37);
    writel(TIM_CR1, TIM_CR1_CEN);
    clock_step(10);
    g_assert_cmphex(readl(TIM_CNT), ==, 47);

    writel(TIM_CR1, 0);
    clock_step(100);
    g_assert_cmphex(readl(TIM_CNT), ==, 47);

    writel(TIM_CR1, TIM_CR1_CEN);
    clock_step(1);
    g_assert_cmphex(readl(TIM_CNT), ==, 48);
}

static void test_arr_zero(void)
{
    timer_reset();
    writel(TIM_ARR, 0);
    writel(TIM_CNT, 0);
    writel(TIM_DIER, TIM_DIER_UIE);
    writel(TIM_CR1, TIM_CR1_CEN);

    clock_step(1);
    g_assert_cmphex(readl(TIM_CNT), ==, 0);
    g_assert_cmphex(readl(TIM_SR), ==, TIM_SR_UIF);
}

static void test_update_and_reset(void)
{
    timer_reset();
    writel(TIM_ARR, 20);
    writel(TIM_CNT, 12);
    writel(TIM_EGR, TIM_EGR_UG);
    g_assert_cmphex(readl(TIM_CNT), ==, 0);

    writel(TIM_DIER, TIM_DIER_UIE);
    writel(TIM_CR1, TIM_CR1_CEN);
    qtest_system_reset(global_qtest);
    clock_step(100);
    g_assert_cmphex(readl(TIM_CR1), ==, 0);
    g_assert_cmphex(readl(TIM_SR), ==, 0);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/stm32f405/timer/period-and-update",
                   test_period_and_update);
    qtest_add_func("/stm32f405/timer/prescaler", test_prescaler);
    qtest_add_func("/stm32f405/timer/stopped-counter",
                   test_stopped_counter);
    qtest_add_func("/stm32f405/timer/arr-zero", test_arr_zero);
    qtest_add_func("/stm32f405/timer/update-and-reset",
                   test_update_and_reset);

    qtest_start("-machine olimex-stm32-h405");
    ret = g_test_run();
    qtest_end();

    return ret;
}
