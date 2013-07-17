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
#define GPIOB_BASE_ADDR 0x40010c00
#define GPIOC_BASE_ADDR 0x40011000
#define RCC_BASE_ADDR 0x40021000
#define AFIO_BASE_ADDR 0x40010000
#define EXTI_BASE_ADDR 0x40010400
#define UART2_BASE_ADDR 0x40004400

const char *dummy_kernel_path = "tests/test-stm32-dummy-kernel.bin";
const uint32_t dummy_kernel_data = 0x12345678;

const int uart2_socket_num = 0;

gpio_id gpio_a_out_id;
gpio_id nvic_in_id;
//gpio_id gpio_b_out_id;

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
                        uint32_t config_value_high,
                        uint32_t config_value_low)
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
    const uint32_t addr_idr = GPIOA_BASE_ADDR + 0x08; // Input Data Register
    uint32_t value;

    config_gpio(GPIOA_BASE_ADDR, 0x44444444, 0x44444444); // All inputs

    value = readl(addr_idr);
    g_assert_cmpint(value, ==, 0);

    set_irq_in("/machine/stm32/gpio[a]", 0, 1);

    value = readl(addr_idr);
    g_assert_cmpint(value, ==, 1);

    set_irq_in("/machine/stm32/gpio[a]", 7, 1);

    value = readl(addr_idr);
    g_assert_cmpint(value, ==, 0x81);
}

static void test_gpio_write(void)
{
    const uint32_t addr_odr = GPIOA_BASE_ADDR + 0x0c; // Output Data Register
    const uint32_t addr_bsrr = GPIOA_BASE_ADDR + 0x10; // Bit Set Reset Register
    const uint32_t addr_brr = GPIOA_BASE_ADDR + 0x14; // Bit Reset Register

    config_gpio(GPIOA_BASE_ADDR, 0x33333333, 0x33333333); // All outputs

    writel(addr_odr, 0x00000000);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0x0), ==, 0);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0xf), ==, 0);

    writel(addr_odr, 0x0000ffff);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0x0), ==, 1);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0xf), ==, 1);

    writel(addr_brr, 0x00008001);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0x0), ==, 0);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0x1), ==, 1);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0xf), ==, 0);

    writel(addr_bsrr, 0x00028001);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0x0), ==, 1);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0x1), ==, 0);
    g_assert_cmpint(get_irq_for_gpio(gpio_a_out_id, 0xf), ==, 1);

    writel(addr_bsrr, 0x0000ffef);


    //config_gpio(GPIOB_BASE_ADDR, 0x33333333, 0x33333333); // All outputs

    //writel(GPIOB_BASE_ADDR + 0x0c, 0x00000000);
    //g_assert_cmpint(get_irq_for_gpio(gpio_b_out_id, 0x0), ==, 0);
    //g_assert_cmpint(get_irq_for_gpio(gpio_b_out_id, 0xf), ==, 0);

    //writel(GPIOB_BASE_ADDR + 0x0c, 0x0000ffff);
    //g_assert_cmpint(get_irq_for_gpio(gpio_b_out_id, 0x0), ==, 1);
    //g_assert_cmpint(get_irq_for_gpio(gpio_b_out_id, 0xf), ==, 1);
}

static void test_gpio_interrupt(void)
{
    config_gpio(GPIOA_BASE_ADDR, 0x44444444, 0x44444444); // All inputs

    set_irq_in("/machine/stm32/gpio[a]", 0, 0);

    writel(EXTI_BASE_ADDR + 0x00, 0x000fffff); // All interrupts enabled
    writel(EXTI_BASE_ADDR + 0x08, 0x000fffff); // All rising triggers
    writel(EXTI_BASE_ADDR + 0x0c, 0x000fffff); // All falling triggers

    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);
    set_irq_in("/machine/stm32/gpio[a]", 0, 1);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 1);
    set_irq_in("/machine/stm32/gpio[a]", 0, 0);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 1);

    writel(EXTI_BASE_ADDR + 0x14, 0x000fffff); // Clear all interrupts

    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);
    set_irq_in("/machine/stm32/gpio[a]", 0, 1);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 1);
    writel(EXTI_BASE_ADDR + 0x14, 0x000fffff); // Clear all interrupts
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);
    set_irq_in("/machine/stm32/gpio[a]", 0, 0);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 1);
    writel(EXTI_BASE_ADDR + 0x14, 0x000fffff); // Clear all interrupts

    set_irq_in("/machine/stm32/gpio[b]", 0, 0);
    set_irq_in("/machine/stm32/gpio[b]", 0, 1);
    set_irq_in("/machine/stm32/gpio[b]", 0, 0);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);

    writel(AFIO_BASE_ADDR + 0x08, 0x00000001); // Attach EXTI0 to Port B

    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);
    set_irq_in("/machine/stm32/gpio[b]", 0, 1);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 1);
    writel(EXTI_BASE_ADDR + 0x14, 0x000fffff); // Clear all interrupts
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);

    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x7), ==, 0);
    writel(EXTI_BASE_ADDR + 0x10, 0x00000003); // Clear all interrupts
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 1);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x7), ==, 1);
    g_assert_cmpint(readl(EXTI_BASE_ADDR + 0x10), ==, 0x00000003);
    writel(EXTI_BASE_ADDR + 0x14, 0x000fffff); // Clear all interrupts
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x6), ==, 0);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 0x7), ==, 0);
    g_assert_cmpint(readl(EXTI_BASE_ADDR + 0x10), ==, 0x00000000);
}

static void test_uart(void)
{
    uint32_t status;
    uint8_t recv_byte;
    uint8_t sent_byte;
    uint32_t cr1_enabled = 0x0000200c;

    /* TODO: Clean this up and modularize. */

    // All inputs except pin 2 (which is the transmit pin).
    config_gpio(GPIOA_BASE_ADDR, 0x44444444, 0x44444b44);

    writel(UART2_BASE_ADDR + 0x0c, cr1_enabled); // Enable UART, Transmit, and Receive

    status = readl(UART2_BASE_ADDR + 0x00);
    g_assert_cmpint(status & 0x020, ==, 0);

    write_serial_port(uart2_socket_num, "A");
    //TODO: Add a timeout to avoid infinite loop
    do {
        status = readl(UART2_BASE_ADDR + 0x00);
    } while((status & 0x020) == 0);
    recv_byte = readl(UART2_BASE_ADDR + 0x04);
    g_assert_cmpint(recv_byte, ==, 'A');

    write_serial_port(uart2_socket_num, "B");
    //TODO: Add a timeout to avoid infinite loop
    do {
        status = readl(UART2_BASE_ADDR + 0x00);
    } while((status & 0x020) == 0);
    recv_byte = readl(UART2_BASE_ADDR + 0x04);
    g_assert_cmpint(recv_byte, ==, 'B');

    writel(UART2_BASE_ADDR + 0x04, 'C'); // Transmit a character
    sent_byte = read_serial_port_byte(uart2_socket_num);
    g_assert_cmpint(sent_byte, ==, 'C');

    writel(UART2_BASE_ADDR + 0x04, 'D'); // Transmit a character
    sent_byte = read_serial_port_byte(uart2_socket_num);
    g_assert_cmpint(sent_byte, ==, 'D');

    writel(UART2_BASE_ADDR + 0x0c, cr1_enabled | 0x0020); // Receive interrupt
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 38), ==, 0);
    write_serial_port(uart2_socket_num, "E");
    //TODO: Add a timeout to avoid infinite loop
    do {
        status = readl(UART2_BASE_ADDR + 0x00);
    } while((status & 0x020) == 0);
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 38), ==, 1);
    recv_byte = readl(UART2_BASE_ADDR + 0x04);
    g_assert_cmpint(recv_byte, ==, 'E');
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 38), ==, 0);

    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 38), ==, 0);
    writel(UART2_BASE_ADDR + 0x0c, cr1_enabled | 0x0040); // transmit complete interrupt
    writel(UART2_BASE_ADDR + 0x04, 'F'); // Transmit a character
    //TODO: Add a timeout to avoid infinite loop
    do {
        status = readl(UART2_BASE_ADDR + 0x00);
    } while((status & 0x040) == 0); // Loop until transmit complete
    g_assert_cmpint(get_irq_for_gpio(nvic_in_id, 38), ==, 1);
    sent_byte = read_serial_port_byte(uart2_socket_num);
    g_assert_cmpint(sent_byte, ==, 'F');

    // To do: test TXE interrupt, overflow, simulated delays, TC clearing
    // These get tough to do because they involve race conditions.
    // Maybe we can add a property for testing to tell the uart to pause
    // when sending a character so that the unit tests can check things
    // out and then change the property back to resume operations.
    // Of course, this interferes with and complicates the unit under test, making
    // it more prone to defects.
    // Maybe if timers were objects, they could have the test properties instead
    // so that we did not need to add test logic into the UART itself.....
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
    s = qtest_start_with_serial(qemu_args, 1);

    enable_all_periph_clocks();

    gpio_a_out_id = qtest_irq_intercept_out(s, "/machine/stm32/gpio[a]");
    nvic_in_id = qtest_irq_intercept_in(s, "/machine/stm32/nvic");
    //gpio_b_out_id = qtest_irq_intercept_out(s, "/machine/stm32/gpio[b]");

    qtest_add_func("/stm32/flash/alias", test_flash_alias);
    qtest_add_func("/stm32/gpio/read", test_gpio_read);
    qtest_add_func("/stm32/gpio/write", test_gpio_write);
    qtest_add_func("/stm32/gpio/interrupt", test_gpio_interrupt);
    qtest_add_func("/stm32/uart", test_uart);

    ret = g_test_run();

    if (s) {
        qtest_quit(s);
    }

    unlink(qemu_args);
    g_free(qemu_args);

    return ret;
}
