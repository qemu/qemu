/*
 * QTest testcase for Microbit board using the Nordic Semiconductor nRF51 SoC.
 *
 * nRF51:
 * Reference Manual: http://infocenter.nordicsemi.com/pdf/nRF51_RM_v3.0.pdf
 * Product Spec: http://infocenter.nordicsemi.com/pdf/nRF51822_PS_v3.1.pdf
 *
 * Microbit Board: http://microbit.org/
 *
 * Copyright 2018 Steffen Görtz <contrib@steffen-goertz.de>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */


#include "qemu/osdep.h"
#include "exec/hwaddr.h"
#include "libqtest.h"

#include "hw/arm/nrf51.h"
#include "hw/char/nrf51_uart.h"
#include "hw/gpio/nrf51_gpio.h"
#include "hw/nvram/nrf51_nvm.h"
#include "hw/timer/nrf51_timer.h"
#include "hw/i2c/microbit_i2c.h"

static bool uart_wait_for_event(QTestState *qts, uint32_t event_addr)
{
    time_t now, start = time(NULL);

    while (true) {
        if (qtest_readl(qts, event_addr) == 1) {
            qtest_writel(qts, event_addr, 0x00);
            return true;
        }

        /* Wait at most 10 minutes */
        now = time(NULL);
        if (now - start > 600) {
            break;
        }
        g_usleep(10000);
    }

    return false;
}

static void uart_rw_to_rxd(QTestState *qts, int sock_fd, const char *in,
                           char *out)
{
    int i, in_len = strlen(in);

    g_assert_true(send(sock_fd, in, in_len, 0) == in_len);
    for (i = 0; i < in_len; i++) {
        g_assert_true(uart_wait_for_event(qts, NRF51_UART_BASE +
                                               A_UART_RXDRDY));
        out[i] = qtest_readl(qts, NRF51_UART_BASE + A_UART_RXD);
    }
    out[i] = '\0';
}

static void uart_w_to_txd(QTestState *qts, const char *in)
{
    int i, in_len = strlen(in);

    for (i = 0; i < in_len; i++) {
        qtest_writel(qts, NRF51_UART_BASE + A_UART_TXD, in[i]);
        g_assert_true(uart_wait_for_event(qts, NRF51_UART_BASE +
                                               A_UART_TXDRDY));
    }
}

static void test_nrf51_uart(void)
{
    int sock_fd;
    char s[10];
    QTestState *qts = qtest_init_with_serial("-M microbit", &sock_fd);

    g_assert_true(send(sock_fd, "c", 1, 0) == 1);
    g_assert_cmphex(qtest_readl(qts, NRF51_UART_BASE + A_UART_RXD), ==, 0x00);

    qtest_writel(qts, NRF51_UART_BASE + A_UART_ENABLE, 0x04);
    qtest_writel(qts, NRF51_UART_BASE + A_UART_STARTRX, 0x01);

    g_assert_true(uart_wait_for_event(qts, NRF51_UART_BASE + A_UART_RXDRDY));
    qtest_writel(qts, NRF51_UART_BASE + A_UART_RXDRDY, 0x00);
    g_assert_cmphex(qtest_readl(qts, NRF51_UART_BASE + A_UART_RXD), ==, 'c');

    qtest_writel(qts, NRF51_UART_BASE + A_UART_INTENSET, 0x04);
    g_assert_cmphex(qtest_readl(qts, NRF51_UART_BASE + A_UART_INTEN), ==, 0x04);
    qtest_writel(qts, NRF51_UART_BASE + A_UART_INTENCLR, 0x04);
    g_assert_cmphex(qtest_readl(qts, NRF51_UART_BASE + A_UART_INTEN), ==, 0x00);

    uart_rw_to_rxd(qts, sock_fd, "hello", s);
    g_assert_true(memcmp(s, "hello", 5) == 0);

    qtest_writel(qts, NRF51_UART_BASE + A_UART_STARTTX, 0x01);
    uart_w_to_txd(qts, "d");
    g_assert_true(recv(sock_fd, s, 10, 0) == 1);
    g_assert_cmphex(s[0], ==, 'd');

    qtest_writel(qts, NRF51_UART_BASE + A_UART_SUSPEND, 0x01);
    qtest_writel(qts, NRF51_UART_BASE + A_UART_TXD, 'h');
    qtest_writel(qts, NRF51_UART_BASE + A_UART_STARTTX, 0x01);
    uart_w_to_txd(qts, "world");
    g_assert_true(recv(sock_fd, s, 10, 0) == 5);
    g_assert_true(memcmp(s, "world", 5) == 0);

    close(sock_fd);

    qtest_quit(qts);
}

/* Read a byte from I2C device at @addr from register @reg */
static uint32_t i2c_read_byte(QTestState *qts, uint32_t addr, uint32_t reg)
{
    uint32_t val;

    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_REG_ADDRESS, addr);
    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_TASK_STARTTX, 1);
    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_REG_TXD, reg);
    val = qtest_readl(qts, NRF51_TWI_BASE + NRF51_TWI_EVENT_TXDSENT);
    g_assert_cmpuint(val, ==, 1);
    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_TASK_STOP, 1);

    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_TASK_STARTRX, 1);
    val = qtest_readl(qts, NRF51_TWI_BASE + NRF51_TWI_EVENT_RXDREADY);
    g_assert_cmpuint(val, ==, 1);
    val = qtest_readl(qts, NRF51_TWI_BASE + NRF51_TWI_REG_RXD);
    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_TASK_STOP, 1);

    return val;
}

static void test_microbit_i2c(void)
{
    uint32_t val;
    QTestState *qts = qtest_init("-M microbit");

    /* We don't program pins/irqs but at least enable the device */
    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_REG_ENABLE, 5);

    /* MMA8653 magnetometer detection */
    val = i2c_read_byte(qts, 0x3A, 0x0D);
    g_assert_cmpuint(val, ==, 0x5A);

    val = i2c_read_byte(qts, 0x3A, 0x0D);
    g_assert_cmpuint(val, ==, 0x5A);

    /* LSM303 accelerometer detection */
    val = i2c_read_byte(qts, 0x3C, 0x4F);
    g_assert_cmpuint(val, ==, 0x40);

    qtest_writel(qts, NRF51_TWI_BASE + NRF51_TWI_REG_ENABLE, 0);

    qtest_quit(qts);
}

#define FLASH_SIZE          (256 * NRF51_PAGE_SIZE)

static void fill_and_erase(QTestState *qts, hwaddr base, hwaddr size,
                           uint32_t address_reg)
{
    hwaddr i;

    /* Erase Page */
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    qtest_writel(qts, NRF51_NVMC_BASE + address_reg, base);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    /* Check memory */
    for (i = 0; i < size / 4; i++) {
        g_assert_cmpuint(qtest_readl(qts, base + i * 4), ==, 0xFFFFFFFF);
    }

    /* Fill memory */
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x01);
    for (i = 0; i < size / 4; i++) {
        qtest_writel(qts, base + i * 4, i);
        g_assert_cmpuint(qtest_readl(qts, base + i * 4), ==, i);
    }
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);
}

static void test_nrf51_nvmc(void)
{
    uint32_t value;
    hwaddr i;
    QTestState *qts = qtest_init("-M microbit");

    /* Test always ready */
    value = qtest_readl(qts, NRF51_NVMC_BASE + NRF51_NVMC_READY);
    g_assert_cmpuint(value & 0x01, ==, 0x01);

    /* Test write-read config register */
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x03);
    g_assert_cmpuint(qtest_readl(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG),
                     ==, 0x03);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);
    g_assert_cmpuint(qtest_readl(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG),
                     ==, 0x00);

    /* Test PCR0 */
    fill_and_erase(qts, NRF51_FLASH_BASE, NRF51_PAGE_SIZE,
                   NRF51_NVMC_ERASEPCR0);
    fill_and_erase(qts, NRF51_FLASH_BASE + NRF51_PAGE_SIZE,
                   NRF51_PAGE_SIZE, NRF51_NVMC_ERASEPCR0);

    /* Test PCR1 */
    fill_and_erase(qts, NRF51_FLASH_BASE, NRF51_PAGE_SIZE,
                   NRF51_NVMC_ERASEPCR1);
    fill_and_erase(qts, NRF51_FLASH_BASE + NRF51_PAGE_SIZE,
                   NRF51_PAGE_SIZE, NRF51_NVMC_ERASEPCR1);

    /* Erase all */
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_ERASEALL, 0x01);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x01);
    for (i = 0; i < FLASH_SIZE / 4; i++) {
        qtest_writel(qts, NRF51_FLASH_BASE + i * 4, i);
        g_assert_cmpuint(qtest_readl(qts, NRF51_FLASH_BASE + i * 4), ==, i);
    }
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_ERASEALL, 0x01);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    for (i = 0; i < FLASH_SIZE / 4; i++) {
        g_assert_cmpuint(qtest_readl(qts, NRF51_FLASH_BASE + i * 4),
                         ==, 0xFFFFFFFF);
    }

    /* Erase UICR */
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_ERASEUICR, 0x01);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    for (i = 0; i < NRF51_UICR_SIZE / 4; i++) {
        g_assert_cmpuint(qtest_readl(qts, NRF51_UICR_BASE + i * 4),
                         ==, 0xFFFFFFFF);
    }

    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x01);
    for (i = 0; i < NRF51_UICR_SIZE / 4; i++) {
        qtest_writel(qts, NRF51_UICR_BASE + i * 4, i);
        g_assert_cmpuint(qtest_readl(qts, NRF51_UICR_BASE + i * 4), ==, i);
    }
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x02);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_ERASEUICR, 0x01);
    qtest_writel(qts, NRF51_NVMC_BASE + NRF51_NVMC_CONFIG, 0x00);

    for (i = 0; i < NRF51_UICR_SIZE / 4; i++) {
        g_assert_cmpuint(qtest_readl(qts, NRF51_UICR_BASE + i * 4),
                         ==, 0xFFFFFFFF);
    }

    qtest_quit(qts);
}

static void test_nrf51_gpio(void)
{
    size_t i;
    uint32_t actual, expected;

    struct {
        hwaddr addr;
        uint32_t expected;
    } const reset_state[] = {
        {NRF51_GPIO_REG_OUT, 0x00000000}, {NRF51_GPIO_REG_OUTSET, 0x00000000},
        {NRF51_GPIO_REG_OUTCLR, 0x00000000}, {NRF51_GPIO_REG_IN, 0x00000000},
        {NRF51_GPIO_REG_DIR, 0x00000000}, {NRF51_GPIO_REG_DIRSET, 0x00000000},
        {NRF51_GPIO_REG_DIRCLR, 0x00000000}
    };

    QTestState *qts = qtest_init("-M microbit");

    /* Check reset state */
    for (i = 0; i < ARRAY_SIZE(reset_state); i++) {
        expected = reset_state[i].expected;
        actual = qtest_readl(qts, NRF51_GPIO_BASE + reset_state[i].addr);
        g_assert_cmpuint(actual, ==, expected);
    }

    for (i = 0; i < NRF51_GPIO_PINS; i++) {
        expected = 0x00000002;
        actual = qtest_readl(qts, NRF51_GPIO_BASE +
                                  NRF51_GPIO_REG_CNF_START + i * 4);
        g_assert_cmpuint(actual, ==, expected);
    }

    /* Check dir bit consistency between dir and cnf */
    /* Check set via DIRSET */
    expected = 0x80000001;
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIRSET, expected);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, expected);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START)
             & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    /* Check clear via DIRCLR */
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIRCLR, 0x80000001);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, 0x00000000);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START)
             & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);

    /* Check set via DIR */
    expected = 0x80000001;
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR, expected);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, expected);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START)
             & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    /* Reset DIR */
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR, 0x00000000);

    /* Check Input propagates */
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x00);
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, -1);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);

    /* Check pull-up working */
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0000);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b1110);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);

    /* Check pull-down working */
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0000);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0110);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, -1);

    /* Check Output propagates */
    qtest_irq_intercept_out(qts, "/machine/nrf51");
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0011);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTCLR, 0x01);
    g_assert_false(qtest_get_irq(qts, 0));

    /* Check self-stimulation */
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b01);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTCLR, 0x01);
    actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);

    /*
     * Check short-circuit - generates an guest_error which must be checked
     * manually as long as qtest can not scan qemu_log messages
     */
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b01);
    qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 0);

    qtest_quit(qts);
}

static void test_nrf51_gpio_detect(void)
{
    QTestState *qts = qtest_init("-M microbit");
    int i;

    /* Connect input buffer on pins 1-7, configure SENSE for high level */
    for (i = 1; i <= 7; i++) {
        qtest_writel(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START + i * 4,
                     deposit32(0, 16, 2, 2));
    }

    qtest_irq_intercept_out_named(qts, "/machine/nrf51/gpio", "detect");

    for (i = 1; i <= 7; i++) {
        /* Set pin high */
        qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", i, 1);
        uint32_t actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN);
        g_assert_cmpuint(actual, ==, 1 << i);

        /* Check that DETECT is high */
        g_assert_true(qtest_get_irq(qts, 0));

        /* Set pin low, check that DETECT goes low. */
        qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", i, 0);
        actual = qtest_readl(qts, NRF51_GPIO_BASE + NRF51_GPIO_REG_IN);
        g_assert_cmpuint(actual, ==, 0x0);
        g_assert_false(qtest_get_irq(qts, 0));
    }

    /* Set pin 0 high, check that DETECT doesn't fire */
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    g_assert_false(qtest_get_irq(qts, 0));
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 0, 0);

    /* Set pins 1, 2, and 3 high, then set 3 low. Check DETECT is still high */
    for (i = 1; i <= 3; i++) {
        qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", i, 1);
    }
    g_assert_true(qtest_get_irq(qts, 0));
    qtest_set_irq_in(qts, "/machine/nrf51", "unnamed-gpio-in", 3, 0);
    g_assert_true(qtest_get_irq(qts, 0));

    qtest_quit(qts);
}

static void timer_task(QTestState *qts, hwaddr task)
{
    qtest_writel(qts, NRF51_TIMER_BASE + task, NRF51_TRIGGER_TASK);
}

static void timer_clear_event(QTestState *qts, hwaddr event)
{
    qtest_writel(qts, NRF51_TIMER_BASE + event, NRF51_EVENT_CLEAR);
}

static void timer_set_bitmode(QTestState *qts, uint8_t mode)
{
    qtest_writel(qts, NRF51_TIMER_BASE + NRF51_TIMER_REG_BITMODE, mode);
}

static void timer_set_prescaler(QTestState *qts, uint8_t prescaler)
{
    qtest_writel(qts, NRF51_TIMER_BASE + NRF51_TIMER_REG_PRESCALER, prescaler);
}

static void timer_set_cc(QTestState *qts, size_t idx, uint32_t value)
{
    qtest_writel(qts, NRF51_TIMER_BASE + NRF51_TIMER_REG_CC0 + idx * 4, value);
}

static void timer_assert_events(QTestState *qts, uint32_t ev0, uint32_t ev1,
                                uint32_t ev2, uint32_t ev3)
{
    g_assert(qtest_readl(qts, NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_0)
             == ev0);
    g_assert(qtest_readl(qts, NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_1)
             == ev1);
    g_assert(qtest_readl(qts, NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_2)
             == ev2);
    g_assert(qtest_readl(qts, NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_3)
             == ev3);
}

static void test_nrf51_timer(void)
{
    uint32_t steps_to_overflow = 408;
    QTestState *qts = qtest_init("-M microbit");

    /* Compare Match */
    timer_task(qts, NRF51_TIMER_TASK_STOP);
    timer_task(qts, NRF51_TIMER_TASK_CLEAR);

    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_0);
    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_1);
    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_2);
    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_3);

    timer_set_bitmode(qts, NRF51_TIMER_WIDTH_16); /* 16 MHz Timer */
    timer_set_prescaler(qts, 0);
    /* Swept over, during the first step */
    timer_set_cc(qts, 0, 2);
    /* Barely miss, after the second step */
    timer_set_cc(qts, 1, 162);
    /* Spot on, after the third step */
    timer_set_cc(qts, 2, 480);

    timer_assert_events(qts, 0, 0, 0, 0);

    timer_task(qts, NRF51_TIMER_TASK_START);
    qtest_clock_step(qts, 10000);
    timer_assert_events(qts, 1, 0, 0, 0);

    /* Swept over on first overflow */
    timer_set_cc(qts, 3, 114);

    qtest_clock_step(qts, 10000);
    timer_assert_events(qts, 1, 1, 0, 0);

    qtest_clock_step(qts, 10000);
    timer_assert_events(qts, 1, 1, 1, 0);

    /* Wrap time until internal counter overflows */
    while (steps_to_overflow--) {
        timer_assert_events(qts, 1, 1, 1, 0);
        qtest_clock_step(qts, 10000);
    }

    timer_assert_events(qts, 1, 1, 1, 1);

    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_0);
    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_1);
    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_2);
    timer_clear_event(qts, NRF51_TIMER_EVENT_COMPARE_3);
    timer_assert_events(qts, 0, 0, 0, 0);

    timer_task(qts, NRF51_TIMER_TASK_STOP);

    /* Test Proposal: Stop/Shutdown */
    /* Test Proposal: Shortcut Compare -> Clear */
    /* Test Proposal: Shortcut Compare -> Stop */
    /* Test Proposal: Counter Mode */

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/microbit/nrf51/uart", test_nrf51_uart);
    qtest_add_func("/microbit/nrf51/gpio", test_nrf51_gpio);
    qtest_add_func("/microbit/nrf51/gpio_detect", test_nrf51_gpio_detect);
    qtest_add_func("/microbit/nrf51/nvmc", test_nrf51_nvmc);
    qtest_add_func("/microbit/nrf51/timer", test_nrf51_timer);
    qtest_add_func("/microbit/microbit/i2c", test_microbit_i2c);

    return g_test_run();
}
