/*
 *  Allwinner GPIO Tests
 *
 *  Copyright (C) 2026 Strahinja Jankovic <strahinja.p.jankovic@gmail.com>
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "libqtest-single.h"

#include "include/hw/gpio/allwinner-gpio-regs.h"

#define AW_A10_GPIO_BASE 0x01c20800
#define AW_A10_IRQ_LINE 28

/* Port n configure register 0 */
#define GPIO_Pn_CFG0_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_CFG0(n))
/* Port n configure register 1 */
#define GPIO_Pn_CFG1_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_CFG1(n))
/* Port n configure register 2 */
#define GPIO_Pn_CFG2_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_CFG2(n))
/* Port n configure register 3 */
#define GPIO_Pn_CFG3_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_CFG3(n))
/* Port n data register */
#define GPIO_Pn_DAT_ADDR(n)    (AW_A10_GPIO_BASE + GPIO_Pn_DAT(n))
/* Port n Multi-driving register 0 */
#define GPIO_Pn_DRV0_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_DRV0(n))
/* Port n Multi-driving register 1 */
#define GPIO_Pn_DRV1_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_DRV1(n))
/* Port n Pull register 0 */
#define GPIO_Pn_PUL0_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_PUL0(n))
/* Port n Pull register 1 */
#define GPIO_Pn_PUL1_ADDR(n)   (AW_A10_GPIO_BASE + GPIO_Pn_PUL1(n))
/* PIO interrupt configure register 0 */
#define GPIO_INT_CFG0_ADDR     (AW_A10_GPIO_BASE + GPIO_INT_CFG0)
/* PIO interrupt configure register 1 */
#define GPIO_INT_CFG1_ADDR     (AW_A10_GPIO_BASE + GPIO_INT_CFG1)
/* PIO interrupt configure register 2 */
#define GPIO_INT_CFG2_ADDR     (AW_A10_GPIO_BASE + GPIO_INT_CFG2)
/* PIO interrupt configure register 3 */
#define GPIO_INT_CFG3_ADDR     (AW_A10_GPIO_BASE + GPIO_INT_CFG3)
/* PIO interrupt control register */
#define GPIO_INT_CTL_ADDR      (AW_A10_GPIO_BASE + GPIO_INT_CTL)
/* PIO interrupt status register */
#define GPIO_INT_STA_ADDR      (AW_A10_GPIO_BASE + GPIO_INT_STA)
/* PIO interrupt debounce register */
#define GPIO_INT_DEB_ADDR      (AW_A10_GPIO_BASE + GPIO_INT_DEB)
/* SDRAM Pad Multi-driving register */
#define SDR_PAD_DRV_ADDR       (AW_A10_GPIO_BASE + SDR_PAD_DRV)
/* SDRAM Pad Pull register */
#define SDR_PAD_PUL_ADDR       (AW_A10_GPIO_BASE + SDR_PAD_PUL)

static inline uint32_t read_cfg_addr(QTestState *s, int port, int pin)
{
    uint32_t addr = 0;

    if (pin >= 24) {
        addr = GPIO_Pn_CFG3_ADDR(port);
    } else if (pin >= 16) {
        addr = GPIO_Pn_CFG2_ADDR(port);
    } else if (pin >= 8) {
        addr = GPIO_Pn_CFG1_ADDR(port);
    } else {
        addr = GPIO_Pn_CFG0_ADDR(port);
    }
    return addr;
}

static inline uint32_t read_int_cfg_addr(QTestState *s, int irq)
{
    uint32_t addr = 0;

    if (irq >= 24) {
        addr = GPIO_INT_CFG3_ADDR;
    } else if (irq >= 16) {
        addr = GPIO_INT_CFG2_ADDR;
    } else if (irq >= 8) {
        addr = GPIO_INT_CFG1_ADDR;
    } else {
        addr = GPIO_INT_CFG0_ADDR;
    }
    return addr;
}

static void update_cfg_reg(QTestState *s, int port, int pin, AWGPIOCfg val)
{
    uint32_t addr = read_cfg_addr(s, port, pin);
    uint32_t cfg = qtest_readl(s, addr);
    uint32_t offset = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;

    cfg = deposit32(cfg, offset, CFG_PIN_STRIDE - 1, val);
    qtest_writel(s, addr, cfg);
}

static void assert_cfg_regval(QTestState *s, int port, int pin,
                              AWGPIOCfg pin_choice)
{
    uint32_t addr = read_cfg_addr(s, port, pin);
    uint32_t cfg = qtest_readl(s, addr);
    uint32_t offset = (pin % CFG_PINS_PER_REG) * CFG_PIN_STRIDE;

    g_assert_cmphex(extract32(cfg,
                              offset, CFG_PIN_STRIDE - 1), ==, pin_choice);
}

static void update_data_reg(QTestState *s, int port, int pin, int val)
{
    uint32_t addr = GPIO_Pn_DAT_ADDR(port);
    uint32_t data = qtest_readl(s, addr);

    data = deposit32(data, pin, 1, val);
    qtest_writel(s, addr, data);
}

static void assert_data_reg(QTestState *s, int port, int pin, int val)
{
    uint32_t addr = GPIO_Pn_DAT_ADDR(port);
    uint32_t data = qtest_readl(s, addr);

    g_assert_cmphex(extract32(data, pin, 1), ==, val);
}

static void update_int_cfg_reg(QTestState *s, int irq, AWGPIOIrqCfg val)
{
    uint32_t addr = read_int_cfg_addr(s, irq);
    uint32_t cfg = qtest_readl(s, addr);
    uint32_t offset = (irq % INT_CFG_IRQ_PER_REG) * INT_CFG_IRQ_STRIDE;

    cfg = deposit32(cfg, offset, INT_CFG_IRQ_STRIDE - 1, val);
    qtest_writel(s, addr, cfg);
}

static void assert_int_cfg_regval(QTestState *s, int irq, AWGPIOIrqCfg irq_cfg)
{
    uint32_t addr = read_int_cfg_addr(s, irq);
    uint32_t cfg = qtest_readl(s, addr);
    uint32_t offset = (irq % INT_CFG_IRQ_PER_REG) * INT_CFG_IRQ_STRIDE;

    g_assert_cmphex(extract32(cfg,
                              offset, INT_CFG_IRQ_STRIDE - 1), ==, irq_cfg);
}

static void test_reset_values(void)
{
    QTestState *s = qtest_init("-machine cubieboard");

    for (int port = 0; port < AW_GPIO_PORTS_NUM; port++) {
        g_assert_cmphex(qtest_readl(s,
                                    GPIO_Pn_DRV0_ADDR(port)), ==,
                                    aw_gpio_port_reset[port].drv[0]);
        g_assert_cmphex(qtest_readl(s,
                                    GPIO_Pn_DRV1_ADDR(port)), ==,
                                    aw_gpio_port_reset[port].drv[1]);
        g_assert_cmphex(qtest_readl(s,
                                    GPIO_Pn_PUL0_ADDR(port)), ==,
                                    aw_gpio_port_reset[port].pul[0]);
        g_assert_cmphex(qtest_readl(s,
                                    GPIO_Pn_PUL1_ADDR(port)), ==,
                                    aw_gpio_port_reset[port].pul[1]);
    }

    qtest_quit(s);
}

typedef struct {
    int port;
    int pin;
} PortPinTestParam;

const PortPinTestParam in_out_test_parameters[] = {
    { GPIO_PA, 0 },
    { GPIO_PA, 17 },
    { GPIO_PB, 19 },
    { GPIO_PC, 23 },
    { GPIO_PD, 26 },
    { GPIO_PE, 11 },
    { GPIO_PF, 5 },
    { GPIO_PG, 10 },
    { GPIO_PH, 16 },
    { GPIO_PI, 21 },
};

const PortPinTestParam irq_test_parameters[] = {
    { GPIO_PH, 1 },
    { GPIO_PH, 10 },
    { GPIO_PH, 21 },
    { GPIO_PI, 10 },
    { GPIO_PI, 19 },
};

/* Set pin to output, check that it propagates */
static void test_set_output_pins(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    g_autofree char *out_name = portname_out(param->port);

    QTestState *s = qtest_init("-machine cubieboard");
    qtest_irq_intercept_out_named(s, "/machine/soc/gpio", out_name);

    /* Configure pin as output */
    update_cfg_reg(s, param->port, param->pin, AW_GPIO_CFG_OUT);
    /* Set value in register */
    update_data_reg(s, param->port, param->pin, AW_GPIO_LEVEL_HIGH);

    /* Check that the output pin is high */
    g_assert_true(qtest_get_irq(s, param->pin));

    /* Cleanup */
    /* Set value in register */
    update_data_reg(s, param->port, param->pin, AW_GPIO_LEVEL_LOW);
    /* Configure pin as input */
    update_cfg_reg(s, param->port, param->pin, AW_GPIO_CFG_IN);

    qtest_quit(s);
}

/*
 * Set pin to input, check that the value is written but it does not propagate
 */
static void test_set_input_pins(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    g_autofree char *out_name = portname_out(param->port);

    QTestState *s = qtest_init("-machine cubieboard");

    qtest_irq_intercept_out_named(s, "/machine/soc/gpio", out_name);

    /* Configure pin as input */
    update_cfg_reg(s, param->port, param->pin, AW_GPIO_CFG_IN);
    /* Set value in register */
    update_data_reg(s, param->port, param->pin, AW_GPIO_LEVEL_HIGH);
    /* Check that the output pin is still low */
    g_assert_false(qtest_get_irq(s, param->pin));

    qtest_quit(s);
}

/* Test rising edge interrupt */
static void test_irq_pin_rising(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    QTestState *s = qtest_init("-machine cubieboard");
    const int gpio_port = param->port;
    const int port_pin = param->pin;
    g_autofree char *in_name = portname_in(gpio_port);
    int irq_pin = irq_nr(gpio_port, port_pin);
    unsigned int irq_mask = 1u << irq_pin;

    qtest_irq_intercept_in(s, "/machine/soc");

    /* Configure as input and set pin low */
    update_cfg_reg(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    assert_cfg_regval(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);

    /* Enable interrupt */
    update_int_cfg_reg(s, irq_pin, AW_GPIO_IRQ_CFG_RISING_EDGE);
    assert_int_cfg_regval(s, irq_pin, AW_GPIO_IRQ_CFG_RISING_EDGE);
    qtest_writel(s, GPIO_INT_CTL_ADDR, irq_mask);

    /* Raise interrupt */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_HIGH);
    /* Check that value is written */
    assert_data_reg(s, gpio_port, port_pin, AW_GPIO_LEVEL_HIGH);
    /* Check interrupt status */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Clear interrupt */
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);

    /* Check that IRQ line is low */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR) & irq_mask, ==, 0);
    g_assert_false(qtest_get_irq(s, AW_A10_IRQ_LINE));

    qtest_quit(s);
}

/* Test falling edge interrupt */
static void test_irq_pin_falling(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    QTestState *s = qtest_init("-machine cubieboard");
    const int gpio_port = param->port;
    const int port_pin = param->pin;
    g_autofree char *in_name = portname_in(gpio_port);
    int irq_pin = irq_nr(gpio_port, port_pin);
    unsigned int irq_mask = 1u << irq_pin;

    qtest_irq_intercept_in(s, "/machine/soc");

    /* Configure as input and set pin high */
    update_cfg_reg(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    assert_cfg_regval(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_HIGH);

    /* Enable interrupt */
    update_int_cfg_reg(s, irq_pin, AW_GPIO_IRQ_CFG_FALLING_EDGE);
    assert_int_cfg_regval(s, irq_pin, AW_GPIO_IRQ_CFG_FALLING_EDGE);
    qtest_writel(s, GPIO_INT_CTL_ADDR, irq_mask);

    /* Raise interrupt */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);
    /* Check that value is written */
    assert_data_reg(s, gpio_port, port_pin, AW_GPIO_LEVEL_LOW);
    /* Check interrupt status */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Clear interrupt */
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);

    /* Check that IRQ line is low */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR) & irq_mask, ==, 0);
    g_assert_false(qtest_get_irq(s, AW_A10_IRQ_LINE));

    qtest_quit(s);
}

/* Test high level interrupt */
static void test_irq_pin_high_level(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    QTestState *s = qtest_init("-machine cubieboard");
    const int gpio_port = param->port;
    const int port_pin = param->pin;
    g_autofree char *in_name = portname_in(gpio_port);
    int irq_pin = irq_nr(gpio_port, port_pin);
    unsigned int irq_mask = 1u << irq_pin;

    qtest_irq_intercept_in(s, "/machine/soc");

    /* Configure as input and set pin low */
    update_cfg_reg(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    assert_cfg_regval(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);

    /* Enable interrupt */
    update_int_cfg_reg(s, irq_pin, AW_GPIO_IRQ_CFG_HIGH_LEVEL);
    assert_int_cfg_regval(s, irq_pin, AW_GPIO_IRQ_CFG_HIGH_LEVEL);
    qtest_writel(s, GPIO_INT_CTL_ADDR, irq_mask);

    /* Raise interrupt */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_HIGH);
    /* Check that value is written */
    assert_data_reg(s, gpio_port, port_pin, AW_GPIO_LEVEL_HIGH);
    /* Check interrupt status */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Try clearing interrupt, it should still be asserted */
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Set line low and clear interrupt, it should be cleared now */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);

    /* Check that IRQ line is low */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR) & irq_mask, ==, 0);
    g_assert_false(qtest_get_irq(s, AW_A10_IRQ_LINE));

    qtest_quit(s);
}

/* Test low level interrupt */
static void test_irq_pin_low_level(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    QTestState *s = qtest_init("-machine cubieboard");
    const int gpio_port = param->port;
    const int port_pin = param->pin;
    g_autofree char *in_name = portname_in(gpio_port);
    int irq_pin = irq_nr(gpio_port, port_pin);
    unsigned int irq_mask = 1u << irq_pin;

    qtest_irq_intercept_in(s, "/machine/soc");

    /* Configure as input and set pin high */
    update_cfg_reg(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    assert_cfg_regval(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_HIGH);

    /* Enable interrupt */
    update_int_cfg_reg(s, irq_pin, AW_GPIO_IRQ_CFG_LOW_LEVEL);
    assert_int_cfg_regval(s, irq_pin, AW_GPIO_IRQ_CFG_LOW_LEVEL);
    qtest_writel(s, GPIO_INT_CTL_ADDR, irq_mask);

    /* Raise interrupt */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);
    /* Check that value is written */
    assert_data_reg(s, gpio_port, port_pin, AW_GPIO_LEVEL_LOW);
    /* Check interrupt status */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Try clearing interrupt, it should still be asserted */
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Set line high and clear interrupt, it should be cleared now */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_HIGH);
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);

    /* Check that IRQ line is low */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR) & irq_mask, ==, 0);
    g_assert_false(qtest_get_irq(s, AW_A10_IRQ_LINE));

    qtest_quit(s);
}

/* Test both edge interrupt */
static void test_irq_pin_both_edge(const void *data)
{
    PortPinTestParam *param = (PortPinTestParam *)data;
    QTestState *s = qtest_init("-machine cubieboard");
    const int gpio_port = param->port;
    const int port_pin = param->pin;
    g_autofree char *in_name = portname_in(gpio_port);
    int irq_pin = irq_nr(gpio_port, port_pin);
    unsigned int irq_mask = 1u << irq_pin;

    qtest_irq_intercept_in(s, "/machine/soc");

    /* Configure as input and set pin low */
    update_cfg_reg(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    assert_cfg_regval(s, gpio_port, port_pin, AW_GPIO_CFG_IN);
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);

    /* Enable interrupt */
    update_int_cfg_reg(s, irq_pin, AW_GPIO_IRQ_CFG_BOTH_EDGE);
    assert_int_cfg_regval(s, irq_pin, AW_GPIO_IRQ_CFG_BOTH_EDGE);
    qtest_writel(s, GPIO_INT_CTL_ADDR, irq_mask);

    /* Raise interrupt - rising edge */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_HIGH);
    /* Check that value is written */
    assert_data_reg(s, gpio_port, port_pin, AW_GPIO_LEVEL_HIGH);
    /* Check interrupt status */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Clear interrupt */
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);

    /* Check that IRQ line is low */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR) & irq_mask, ==, 0);
    g_assert_false(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Raise interrupt - falling edge */
    qtest_set_irq_in(s,
                     "/machine/soc/gpio", in_name, port_pin,
                     AW_GPIO_LEVEL_LOW);
    /* Check that value is written */
    assert_data_reg(s, gpio_port, port_pin, AW_GPIO_LEVEL_LOW);
    /* Check interrupt status */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR), ==, irq_mask);
    g_assert_true(qtest_get_irq(s, AW_A10_IRQ_LINE));

    /* Clear interrupt */
    qtest_writel(s, GPIO_INT_STA_ADDR, irq_mask);

    /* Check that IRQ line is low */
    g_assert_cmphex(qtest_readl(s, GPIO_INT_STA_ADDR) & irq_mask, ==, 0);
    g_assert_false(qtest_get_irq(s, AW_A10_IRQ_LINE));

    qtest_quit(s);
}


static void add_test(const char *type, const PortPinTestParam *p,
                      void (*testfn)(const void *))
{
    g_autofree char *name =
        g_strdup_printf("/allwinner-cubieboard/gpio/%s/%s%d",
                        type, portname(p->port), p->pin);

    qtest_add_data_func(name, p, testfn);
}


int main(int argc, char **argv)
{
    int r;

    g_test_init(&argc, &argv, NULL);

    for (int i = 0; i < ARRAY_SIZE(in_out_test_parameters); i++) {
        const PortPinTestParam *p = &in_out_test_parameters[i];
        add_test("set_output_pins", p, test_set_output_pins);
        add_test("set_input_pins", p, test_set_input_pins);
    }
    qtest_add_func("/allwinner-cubieboard/gpio/reset_values",
                        test_reset_values);
    for (int i = 0; i < ARRAY_SIZE(irq_test_parameters); i++) {
        const PortPinTestParam *p = &irq_test_parameters[i];
        add_test("irq_pin_rising", p, test_irq_pin_rising);
        add_test("irq_pin_falling", p, test_irq_pin_falling);
        add_test("irq_pin_high_level", p, test_irq_pin_high_level);
        add_test("irq_pin_low_level", p, test_irq_pin_low_level);
        add_test("irq_pin_both_edge", p, test_irq_pin_both_edge);
    }
    r = g_test_run();

    return r;
}
