/*
 * QTest testcase for the STM32 Microcontroller
 *
 * Copyright 2013
 *
 * Authors:
 *  Andre Beckus
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#include "libqtest.h"

#include <stdio.h>
#include <glib.h>

// These are only needed for getpid
#include <sys/types.h>
#include <unistd.h>

#define GPIOA_BASE_ADDR 0x40010800
#define GPIOC_BASE_ADDR 0x40011000
#define RCC_BASE_ADDR 0x40021000

const char *dummy_kernel_path = "tests/test-stm32-dummy-kernel.bin";
const uint32_t dummy_kernel_data = 0x12345678;

//int uart_sock, uart_fd;

static void write_dummy_kernel_bin(void)
{
    FILE *kernel_file = fopen(dummy_kernel_path, "wb");
    g_assert(kernel_file);

    size_t write_size = fwrite(&dummy_kernel_data, 4, 1, kernel_file);
    g_assert(write_size == 1);

    int close_result = fclose(kernel_file);
    g_assert(close_result == 0);
}

static void enable_all_periph_clocks(void)
{
    writel(RCC_BASE_ADDR + 0x18, 0x0038fffd);
    writel(RCC_BASE_ADDR + 0x1c, 0x3afec9ff);
}

static void config_gpio(uint32_t gpio_base_addr,
                        uint32_t config_value_low,
                        uint32_t config_value_high)
{
    writel(gpio_base_addr + 0x00, config_value_low);
    writel(gpio_base_addr + 0x04, config_value_high);
}

static void test_flash_alias(void)
{
    g_assert_cmpint(readl(0), ==, dummy_kernel_data);
    g_assert_cmpint(readl(0x08000000), ==, dummy_kernel_data);

    g_assert_cmpint(readw(2), ==, dummy_kernel_data >> 16);
    g_assert_cmpint(readw(0x08000002), ==, dummy_kernel_data >> 16);
}

static void test_gpio_read(void)
{
    const uint32_t input_addr = GPIOA_BASE_ADDR + 0x08; // GPIO Port A Read Register
    uint32_t value;

    config_gpio(GPIOA_BASE_ADDR, 0x44444444, 0x44444444); // All inputs

    value = readl(input_addr);
    g_assert_cmpint(value, ==, 0);

    set_irq_in("/machine/stm32/gpio[a]", 0, 1);

    value = readl(input_addr);
    g_assert_cmpint(value, ==, 1);

    set_irq_in("/machine/stm32/gpio[a]", 7, 1);

    value = readl(input_addr);
    g_assert_cmpint(value, ==, 0x81);
}

static void test_gpio_write(void)
{
    const uint32_t output_addr = GPIOA_BASE_ADDR + 0x0c;

    config_gpio(GPIOA_BASE_ADDR, 0x33333333, 0x33333333); // All outputs

    writel(output_addr, 0x00000000);
    g_assert_cmpint(get_irq(0x0), ==, 0);
    g_assert_cmpint(get_irq(0xf), ==, 0);

    writel(output_addr, 0x0000ffff);
    g_assert_cmpint(get_irq(0x0), ==, 1);
    g_assert_cmpint(get_irq(0xf), ==, 1);


}

int main(int argc, char **argv)
{
    QTestState *s = NULL;
    int ret;

    g_test_init(&argc, &argv, NULL);

    write_dummy_kernel_bin();

    gchar *qemu_args = g_strdup_printf("-display none "
                                       "-machine stm32-p103 "
                                       "-kernel %s",
                                       dummy_kernel_path);
    s = qtest_start(qemu_args);

    enable_all_periph_clocks();

    qtest_irq_intercept_out(s, "/machine/stm32/gpio[a]");

    qtest_add_func("/stm32/flash_alias", test_flash_alias);
    qtest_add_func("/stm32/gpio/read", test_gpio_read);
    qtest_add_func("/stm32/gpio/write", test_gpio_write);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }

    unlink(qemu_args);
    g_free(qemu_args);

    return ret;
}
