/*
 * QTest testcase for DM163
 *
 * Copyright (C) 2024 Samuel Tardieu <sam@rfc1149.net>
 * Copyright (C) 2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (C) 2024 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest.h"

enum DM163_INPUTS {
    SIN = 8,
    DCK = 9,
    RST_B = 10,
    LAT_B = 11,
    SELBK = 12,
    EN_B = 13
};

#define DEVICE_NAME "/machine/dm163"
#define GPIO_OUT(name, value) qtest_set_irq_in(qts, DEVICE_NAME, NULL, name,   \
                                               value)
#define GPIO_PULSE(name)                                                       \
  do {                                                                         \
    GPIO_OUT(name, 1);                                                         \
    GPIO_OUT(name, 0);                                                         \
  } while (0)


static void rise_gpio_pin_dck(QTestState *qts)
{
    /* Configure output mode for pin PB1 */
    qtest_writel(qts, 0x48000400, 0xFFFFFEB7);
    /* Write 1 in ODR for PB1 */
    qtest_writel(qts, 0x48000414, 0x00000002);
}

static void lower_gpio_pin_dck(QTestState *qts)
{
    /* Configure output mode for pin PB1 */
    qtest_writel(qts, 0x48000400, 0xFFFFFEB7);
    /* Write 0 in ODR for PB1 */
    qtest_writel(qts, 0x48000414, 0x00000000);
}

static void rise_gpio_pin_selbk(QTestState *qts)
{
    /* Configure output mode for pin PC5 */
    qtest_writel(qts, 0x48000800, 0xFFFFF7FF);
    /* Write 1 in ODR for PC5 */
    qtest_writel(qts, 0x48000814, 0x00000020);
}

static void lower_gpio_pin_selbk(QTestState *qts)
{
    /* Configure output mode for pin PC5 */
    qtest_writel(qts, 0x48000800, 0xFFFFF7FF);
    /* Write 0 in ODR for PC5 */
    qtest_writel(qts, 0x48000814, 0x00000000);
}

static void rise_gpio_pin_lat_b(QTestState *qts)
{
    /* Configure output mode for pin PC4 */
    qtest_writel(qts, 0x48000800, 0xFFFFFDFF);
    /* Write 1 in ODR for PC4 */
    qtest_writel(qts, 0x48000814, 0x00000010);
}

static void lower_gpio_pin_lat_b(QTestState *qts)
{
    /* Configure output mode for pin PC4 */
    qtest_writel(qts, 0x48000800, 0xFFFFFDFF);
    /* Write 0 in ODR for PC4 */
    qtest_writel(qts, 0x48000814, 0x00000000);
}

static void rise_gpio_pin_rst_b(QTestState *qts)
{
    /* Configure output mode for pin PC3 */
    qtest_writel(qts, 0x48000800, 0xFFFFFF7F);
    /* Write 1 in ODR for PC3 */
    qtest_writel(qts, 0x48000814, 0x00000008);
}

static void lower_gpio_pin_rst_b(QTestState *qts)
{
    /* Configure output mode for pin PC3 */
    qtest_writel(qts, 0x48000800, 0xFFFFFF7F);
    /* Write 0 in ODR for PC3 */
    qtest_writel(qts, 0x48000814, 0x00000000);
}

static void rise_gpio_pin_sin(QTestState *qts)
{
    /* Configure output mode for pin PA4 */
    qtest_writel(qts, 0x48000000, 0xFFFFFDFF);
    /* Write 1 in ODR for PA4 */
    qtest_writel(qts, 0x48000014, 0x00000010);
}

static void lower_gpio_pin_sin(QTestState *qts)
{
    /* Configure output mode for pin PA4 */
    qtest_writel(qts, 0x48000000, 0xFFFFFDFF);
    /* Write 0 in ODR for PA4 */
    qtest_writel(qts, 0x48000014, 0x00000000);
}

static void test_dm163_bank(const void *opaque)
{
    const unsigned bank = (uintptr_t) opaque;
    const int width = bank ? 192 : 144;

    QTestState *qts = qtest_initf("-M b-l475e-iot01a");
    qtest_irq_intercept_out_named(qts, DEVICE_NAME, "sout");
    GPIO_OUT(RST_B, 1);
    GPIO_OUT(EN_B, 0);
    GPIO_OUT(DCK, 0);
    GPIO_OUT(SELBK, bank);
    GPIO_OUT(LAT_B, 1);

    /* Fill bank with zeroes */
    GPIO_OUT(SIN, 0);
    for (int i = 0; i < width; i++) {
        GPIO_PULSE(DCK);
    }
    /* Fill bank with ones, check that we get the previous zeroes */
    GPIO_OUT(SIN, 1);
    for (int i = 0; i < width; i++) {
        GPIO_PULSE(DCK);
        g_assert(!qtest_get_irq(qts, 0));
    }

    /* Pulse one more bit in the bank, check that we get a one */
    GPIO_PULSE(DCK);
    g_assert(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

static void test_dm163_gpio_connection(void)
{
    QTestState *qts = qtest_init("-M b-l475e-iot01a");
    qtest_irq_intercept_in(qts, DEVICE_NAME);

    g_assert_false(qtest_get_irq(qts, SIN));
    g_assert_false(qtest_get_irq(qts, DCK));
    g_assert_false(qtest_get_irq(qts, RST_B));
    g_assert_false(qtest_get_irq(qts, LAT_B));
    g_assert_false(qtest_get_irq(qts, SELBK));

    rise_gpio_pin_dck(qts);
    g_assert_true(qtest_get_irq(qts, DCK));
    lower_gpio_pin_dck(qts);
    g_assert_false(qtest_get_irq(qts, DCK));

    rise_gpio_pin_lat_b(qts);
    g_assert_true(qtest_get_irq(qts, LAT_B));
    lower_gpio_pin_lat_b(qts);
    g_assert_false(qtest_get_irq(qts, LAT_B));

    rise_gpio_pin_selbk(qts);
    g_assert_true(qtest_get_irq(qts, SELBK));
    lower_gpio_pin_selbk(qts);
    g_assert_false(qtest_get_irq(qts, SELBK));

    rise_gpio_pin_rst_b(qts);
    g_assert_true(qtest_get_irq(qts, RST_B));
    lower_gpio_pin_rst_b(qts);
    g_assert_false(qtest_get_irq(qts, RST_B));

    rise_gpio_pin_sin(qts);
    g_assert_true(qtest_get_irq(qts, SIN));
    lower_gpio_pin_sin(qts);
    g_assert_false(qtest_get_irq(qts, SIN));

    g_assert_false(qtest_get_irq(qts, DCK));
    g_assert_false(qtest_get_irq(qts, LAT_B));
    g_assert_false(qtest_get_irq(qts, SELBK));
    g_assert_false(qtest_get_irq(qts, RST_B));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_data_func("/dm163/bank0", (void *)0, test_dm163_bank);
    qtest_add_data_func("/dm163/bank1", (void *)1, test_dm163_bank);
    qtest_add_func("/dm163/gpio_connection", test_dm163_gpio_connection);
    return g_test_run();
}
