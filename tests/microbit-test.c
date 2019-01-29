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
#include "hw/gpio/nrf51_gpio.h"
#include "hw/timer/nrf51_timer.h"
#include "hw/i2c/microbit_i2c.h"

/* Read a byte from I2C device at @addr from register @reg */
static uint32_t i2c_read_byte(uint32_t addr, uint32_t reg)
{
    uint32_t val;

    writel(NRF51_TWI_BASE + NRF51_TWI_REG_ADDRESS, addr);
    writel(NRF51_TWI_BASE + NRF51_TWI_TASK_STARTTX, 1);
    writel(NRF51_TWI_BASE + NRF51_TWI_REG_TXD, reg);
    val = readl(NRF51_TWI_BASE + NRF51_TWI_EVENT_TXDSENT);
    g_assert_cmpuint(val, ==, 1);
    writel(NRF51_TWI_BASE + NRF51_TWI_TASK_STOP, 1);

    writel(NRF51_TWI_BASE + NRF51_TWI_TASK_STARTRX, 1);
    val = readl(NRF51_TWI_BASE + NRF51_TWI_EVENT_RXDREADY);
    g_assert_cmpuint(val, ==, 1);
    val = readl(NRF51_TWI_BASE + NRF51_TWI_REG_RXD);
    writel(NRF51_TWI_BASE + NRF51_TWI_TASK_STOP, 1);

    return val;
}

static void test_microbit_i2c(void)
{
    uint32_t val;

    /* We don't program pins/irqs but at least enable the device */
    writel(NRF51_TWI_BASE + NRF51_TWI_REG_ENABLE, 5);

    /* MMA8653 magnetometer detection */
    val = i2c_read_byte(0x3A, 0x0D);
    g_assert_cmpuint(val, ==, 0x5A);

    val = i2c_read_byte(0x3A, 0x0D);
    g_assert_cmpuint(val, ==, 0x5A);

    /* LSM303 accelerometer detection */
    val = i2c_read_byte(0x3C, 0x4F);
    g_assert_cmpuint(val, ==, 0x40);

    writel(NRF51_TWI_BASE + NRF51_TWI_REG_ENABLE, 0);
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

    /* Check reset state */
    for (i = 0; i < ARRAY_SIZE(reset_state); i++) {
        expected = reset_state[i].expected;
        actual = readl(NRF51_GPIO_BASE + reset_state[i].addr);
        g_assert_cmpuint(actual, ==, expected);
    }

    for (i = 0; i < NRF51_GPIO_PINS; i++) {
        expected = 0x00000002;
        actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START + i * 4);
        g_assert_cmpuint(actual, ==, expected);
    }

    /* Check dir bit consistency between dir and cnf */
    /* Check set via DIRSET */
    expected = 0x80000001;
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIRSET, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    /* Check clear via DIRCLR */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIRCLR, 0x80000001);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, 0x00000000);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);

    /* Check set via DIR */
    expected = 0x80000001;
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR);
    g_assert_cmpuint(actual, ==, expected);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_END) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    /* Reset DIR */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_DIR, 0x00000000);

    /* Check Input propagates */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x00);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, -1);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);

    /* Check pull-up working */
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0000);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b1110);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);

    /* Check pull-down working */
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 1);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0000);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0110);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0x02);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, -1);

    /* Check Output propagates */
    irq_intercept_out("/machine/nrf51");
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b0011);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    g_assert_true(get_irq(0));
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTCLR, 0x01);
    g_assert_false(get_irq(0));

    /* Check self-stimulation */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x01);

    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTCLR, 0x01);
    actual = readl(NRF51_GPIO_BASE + NRF51_GPIO_REG_IN) & 0x01;
    g_assert_cmpuint(actual, ==, 0x00);

    /*
     * Check short-circuit - generates an guest_error which must be checked
     * manually as long as qtest can not scan qemu_log messages
     */
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_CNF_START, 0b01);
    writel(NRF51_GPIO_BASE + NRF51_GPIO_REG_OUTSET, 0x01);
    qtest_set_irq_in(global_qtest, "/machine/nrf51", "unnamed-gpio-in", 0, 0);
}

static void timer_task(hwaddr task)
{
    writel(NRF51_TIMER_BASE + task, NRF51_TRIGGER_TASK);
}

static void timer_clear_event(hwaddr event)
{
    writel(NRF51_TIMER_BASE + event, NRF51_EVENT_CLEAR);
}

static void timer_set_bitmode(uint8_t mode)
{
    writel(NRF51_TIMER_BASE + NRF51_TIMER_REG_BITMODE, mode);
}

static void timer_set_prescaler(uint8_t prescaler)
{
    writel(NRF51_TIMER_BASE + NRF51_TIMER_REG_PRESCALER, prescaler);
}

static void timer_set_cc(size_t idx, uint32_t value)
{
    writel(NRF51_TIMER_BASE + NRF51_TIMER_REG_CC0 + idx * 4, value);
}

static void timer_assert_events(uint32_t ev0, uint32_t ev1, uint32_t ev2,
                                uint32_t ev3)
{
    g_assert(readl(NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_0) == ev0);
    g_assert(readl(NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_1) == ev1);
    g_assert(readl(NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_2) == ev2);
    g_assert(readl(NRF51_TIMER_BASE + NRF51_TIMER_EVENT_COMPARE_3) == ev3);
}

static void test_nrf51_timer(void)
{
    uint32_t steps_to_overflow = 408;

    /* Compare Match */
    timer_task(NRF51_TIMER_TASK_STOP);
    timer_task(NRF51_TIMER_TASK_CLEAR);

    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_0);
    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_1);
    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_2);
    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_3);

    timer_set_bitmode(NRF51_TIMER_WIDTH_16); /* 16 MHz Timer */
    timer_set_prescaler(0);
    /* Swept over in first step */
    timer_set_cc(0, 2);
    /* Barely miss on first step */
    timer_set_cc(1, 162);
    /* Spot on on third step */
    timer_set_cc(2, 480);

    timer_assert_events(0, 0, 0, 0);

    timer_task(NRF51_TIMER_TASK_START);
    clock_step(10000);
    timer_assert_events(1, 0, 0, 0);

    /* Swept over on first overflow */
    timer_set_cc(3, 114);

    clock_step(10000);
    timer_assert_events(1, 1, 0, 0);

    clock_step(10000);
    timer_assert_events(1, 1, 1, 0);

    /* Wrap time until internal counter overflows */
    while (steps_to_overflow--) {
        timer_assert_events(1, 1, 1, 0);
        clock_step(10000);
    }

    timer_assert_events(1, 1, 1, 1);

    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_0);
    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_1);
    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_2);
    timer_clear_event(NRF51_TIMER_EVENT_COMPARE_3);
    timer_assert_events(0, 0, 0, 0);

    timer_task(NRF51_TIMER_TASK_STOP);

    /* Test Proposal: Stop/Shutdown */
    /* Test Proposal: Shortcut Compare -> Clear */
    /* Test Proposal: Shortcut Compare -> Stop */
    /* Test Proposal: Counter Mode */
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);

    global_qtest = qtest_initf("-machine microbit");

    qtest_add_func("/microbit/nrf51/gpio", test_nrf51_gpio);
    qtest_add_func("/microbit/nrf51/timer", test_nrf51_timer);
    qtest_add_func("/microbit/microbit/i2c", test_microbit_i2c);

    ret = g_test_run();

    qtest_quit(global_qtest);
    return ret;
}
