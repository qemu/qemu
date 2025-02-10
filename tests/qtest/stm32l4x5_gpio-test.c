/*
 * QTest testcase for STM32L4x5_GPIO
 *
 * Copyright (c) 2024 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2024 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "stm32l4x5.h"

#define GPIO_BASE_ADDR 0x48000000
#define GPIO_SIZE      0x400
#define NUM_GPIOS      8
#define NUM_GPIO_PINS  16

#define GPIO_A 0x48000000
#define GPIO_B 0x48000400
#define GPIO_C 0x48000800
#define GPIO_D 0x48000C00
#define GPIO_E 0x48001000
#define GPIO_F 0x48001400
#define GPIO_G 0x48001800
#define GPIO_H 0x48001C00

#define MODER 0x00
#define OTYPER 0x04
#define PUPDR 0x0C
#define IDR 0x10
#define ODR 0x14
#define BSRR 0x18
#define BRR 0x28

#define MODER_INPUT 0
#define MODER_OUTPUT 1

#define PUPDR_NONE 0
#define PUPDR_PULLUP 1
#define PUPDR_PULLDOWN 2

#define OTYPER_PUSH_PULL 0
#define OTYPER_OPEN_DRAIN 1

/* SoC forwards GPIOs to SysCfg */
#define SYSCFG "/machine/soc"

const uint32_t moder_reset[NUM_GPIOS] = {
    0xABFFFFFF,
    0xFFFFFEBF,
    0xFFFFFFFF,
    0xFFFFFFFF,
    0xFFFFFFFF,
    0xFFFFFFFF,
    0xFFFFFFFF,
    0x0000000F
};

const uint32_t pupdr_reset[NUM_GPIOS] = {
    0x64000000,
    0x00000100,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
};

const uint32_t idr_reset[NUM_GPIOS] = {
    0x0000A000,
    0x00000010,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000,
    0x00000000
};

#define PIN_MASK        0xF
#define GPIO_ADDR_MASK  (~(GPIO_SIZE - 1))

static inline void *test_data(uint32_t gpio_addr, uint8_t pin)
{
    return (void *)(uintptr_t)((gpio_addr & GPIO_ADDR_MASK) | (pin & PIN_MASK));
}

#define test_gpio_addr(data)      ((uintptr_t)(data) & GPIO_ADDR_MASK)
#define test_pin(data)            ((uintptr_t)(data) & PIN_MASK)

static uint32_t gpio_readl(unsigned int gpio, unsigned int offset)
{
    return readl(gpio + offset);
}

static void gpio_writel(unsigned int gpio, unsigned int offset, uint32_t value)
{
    writel(gpio + offset, value);
}

static void gpio_set_bit(unsigned int gpio, unsigned int reg,
                         unsigned int pin, uint32_t value)
{
    uint32_t mask = 0xFFFFFFFF & ~(0x1 << pin);
    gpio_writel(gpio, reg, (gpio_readl(gpio, reg) & mask) | value << pin);
}

static void gpio_set_2bits(unsigned int gpio, unsigned int reg,
                           unsigned int pin, uint32_t value)
{
    uint32_t offset = 2 * pin;
    uint32_t mask = 0xFFFFFFFF & ~(0x3 << offset);
    gpio_writel(gpio, reg, (gpio_readl(gpio, reg) & mask) | value << offset);
}

static unsigned int get_gpio_id(uint32_t gpio_addr)
{
    return (gpio_addr - GPIO_BASE_ADDR) / GPIO_SIZE;
}

static void gpio_set_irq(unsigned int gpio, int num, int level)
{
    g_autofree char *name = g_strdup_printf("/machine/soc/gpio%c",
                                            get_gpio_id(gpio) + 'a');
    qtest_set_irq_in(global_qtest, name, NULL, num, level);
}

static void disconnect_all_pins(unsigned int gpio)
{
    g_autofree char *path = g_strdup_printf("/machine/soc/gpio%c",
                                            get_gpio_id(gpio) + 'a');
    QDict *r;

    r = qtest_qmp(global_qtest, "{ 'execute': 'qom-set', 'arguments': "
        "{ 'path': %s, 'property': 'disconnected-pins', 'value': %d } }",
        path, 0xFFFF);
    g_assert_false(qdict_haskey(r, "error"));
    qobject_unref(r);
}

static uint32_t get_disconnected_pins(unsigned int gpio)
{
    g_autofree char *path = g_strdup_printf("/machine/soc/gpio%c",
                                            get_gpio_id(gpio) + 'a');
    uint32_t disconnected_pins = 0;
    QDict *r;

    r = qtest_qmp(global_qtest, "{ 'execute': 'qom-get', 'arguments':"
        " { 'path': %s, 'property': 'disconnected-pins'} }", path);
    g_assert_false(qdict_haskey(r, "error"));
    disconnected_pins = qdict_get_int(r, "return");
    qobject_unref(r);
    return disconnected_pins;
}

static uint32_t reset(uint32_t gpio, unsigned int offset)
{
    switch (offset) {
    case MODER:
        return moder_reset[get_gpio_id(gpio)];
    case PUPDR:
        return pupdr_reset[get_gpio_id(gpio)];
    case IDR:
        return idr_reset[get_gpio_id(gpio)];
    }
    return 0x0;
}

static void test_idr_reset_value(void)
{
    /*
     * Checks that the values in MODER, OTYPER, PUPDR and ODR
     * after reset are correct, and that the value in IDR is
     * coherent.
     * Since AF and analog modes aren't implemented, IDR reset
     * values aren't the same as with a real board.
     *
     * Register IDR contains the actual values of all GPIO pins.
     * Its value depends on the pins' configuration
     * (intput/output/analog : register MODER, push-pull/open-drain :
     * register OTYPER, pull-up/pull-down/none : register PUPDR)
     * and on the values stored in register ODR
     * (in case the pin is in output mode).
     */

    gpio_writel(GPIO_A, MODER, 0xDEADBEEF);
    gpio_writel(GPIO_A, ODR, 0xDEADBEEF);
    gpio_writel(GPIO_A, OTYPER, 0xDEADBEEF);
    gpio_writel(GPIO_A, PUPDR, 0xDEADBEEF);

    gpio_writel(GPIO_B, MODER, 0xDEADBEEF);
    gpio_writel(GPIO_B, ODR, 0xDEADBEEF);
    gpio_writel(GPIO_B, OTYPER, 0xDEADBEEF);
    gpio_writel(GPIO_B, PUPDR, 0xDEADBEEF);

    gpio_writel(GPIO_C, MODER, 0xDEADBEEF);
    gpio_writel(GPIO_C, ODR, 0xDEADBEEF);
    gpio_writel(GPIO_C, OTYPER, 0xDEADBEEF);
    gpio_writel(GPIO_C, PUPDR, 0xDEADBEEF);

    gpio_writel(GPIO_H, MODER, 0xDEADBEEF);
    gpio_writel(GPIO_H, ODR, 0xDEADBEEF);
    gpio_writel(GPIO_H, OTYPER, 0xDEADBEEF);
    gpio_writel(GPIO_H, PUPDR, 0xDEADBEEF);

    qtest_system_reset(global_qtest);

    uint32_t moder = gpio_readl(GPIO_A, MODER);
    uint32_t odr = gpio_readl(GPIO_A, ODR);
    uint32_t otyper = gpio_readl(GPIO_A, OTYPER);
    uint32_t pupdr = gpio_readl(GPIO_A, PUPDR);
    uint32_t idr = gpio_readl(GPIO_A, IDR);
    /* 15: AF, 14: AF, 13: AF, 12: Analog ... */
    /* here AF is the same as Analog and Input mode */
    g_assert_cmphex(moder, ==, reset(GPIO_A, MODER));
    g_assert_cmphex(odr, ==, reset(GPIO_A, ODR));
    g_assert_cmphex(otyper, ==, reset(GPIO_A, OTYPER));
    /* 15: pull-up, 14: pull-down, 13: pull-up, 12: neither ... */
    g_assert_cmphex(pupdr, ==, reset(GPIO_A, PUPDR));
    /* 15 : 1, 14: 0, 13: 1, 12 : reset value ... */
    g_assert_cmphex(idr, ==, reset(GPIO_A, IDR));

    moder = gpio_readl(GPIO_B, MODER);
    odr = gpio_readl(GPIO_B, ODR);
    otyper = gpio_readl(GPIO_B, OTYPER);
    pupdr = gpio_readl(GPIO_B, PUPDR);
    idr = gpio_readl(GPIO_B, IDR);
    /* ... 5: Analog, 4: AF, 3: AF, 2: Analog ... */
    /* here AF is the same as Analog and Input mode */
    g_assert_cmphex(moder, ==, reset(GPIO_B, MODER));
    g_assert_cmphex(odr, ==, reset(GPIO_B, ODR));
    g_assert_cmphex(otyper, ==, reset(GPIO_B, OTYPER));
    /* ... 5: neither, 4: pull-up, 3: neither ... */
    g_assert_cmphex(pupdr, ==, reset(GPIO_B, PUPDR));
    /* ... 5 : reset value, 4 : 1, 3 : reset value ... */
    g_assert_cmphex(idr, ==, reset(GPIO_B, IDR));

    moder = gpio_readl(GPIO_C, MODER);
    odr = gpio_readl(GPIO_C, ODR);
    otyper = gpio_readl(GPIO_C, OTYPER);
    pupdr = gpio_readl(GPIO_C, PUPDR);
    idr = gpio_readl(GPIO_C, IDR);
    /* Analog, same as Input mode*/
    g_assert_cmphex(moder, ==, reset(GPIO_C, MODER));
    g_assert_cmphex(odr, ==, reset(GPIO_C, ODR));
    g_assert_cmphex(otyper, ==, reset(GPIO_C, OTYPER));
    /* no pull-up or pull-down */
    g_assert_cmphex(pupdr, ==, reset(GPIO_C, PUPDR));
    /* reset value */
    g_assert_cmphex(idr, ==, reset(GPIO_C, IDR));

    moder = gpio_readl(GPIO_H, MODER);
    odr = gpio_readl(GPIO_H, ODR);
    otyper = gpio_readl(GPIO_H, OTYPER);
    pupdr = gpio_readl(GPIO_H, PUPDR);
    idr = gpio_readl(GPIO_H, IDR);
    /* Analog, same as Input mode */
    g_assert_cmphex(moder, ==, reset(GPIO_H, MODER));
    g_assert_cmphex(odr, ==, reset(GPIO_H, ODR));
    g_assert_cmphex(otyper, ==, reset(GPIO_H, OTYPER));
    /* no pull-up or pull-down */
    g_assert_cmphex(pupdr, ==, reset(GPIO_H, PUPDR));
    /* reset value */
    g_assert_cmphex(idr, ==, reset(GPIO_H, IDR));
}

static void test_gpio_output_mode(const void *data)
{
    /*
     * Checks that setting a bit in ODR sets the corresponding
     * GPIO line high : it should set the right bit in IDR
     * and send an irq to syscfg.
     * Additionally, it checks that values written to ODR
     * when not in output mode are stored and not discarded.
     */
    unsigned int pin = test_pin(data);
    uint32_t gpio = test_gpio_addr(data);
    unsigned int gpio_id = get_gpio_id(gpio);

    qtest_irq_intercept_in(global_qtest, SYSCFG);

    /* Set a bit in ODR and check nothing happens */
    gpio_set_bit(gpio, ODR, pin, 1);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR));
    g_assert_false(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Configure the relevant line as output and check the pin is high */
    gpio_set_2bits(gpio, MODER, pin, MODER_OUTPUT);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) | (1 << pin));
    g_assert_true(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Reset the bit in ODR and check the pin is low */
    gpio_set_bit(gpio, ODR, pin, 0);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));
    g_assert_false(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Clean the test */
    gpio_writel(gpio, ODR, reset(gpio, ODR));
    gpio_writel(gpio, MODER, reset(gpio, MODER));
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR));
    g_assert_false(get_irq(gpio_id * NUM_GPIO_PINS + pin));
}

static void test_gpio_input_mode(const void *data)
{
    /*
     * Test that setting a line high/low externally sets the
     * corresponding GPIO line high/low : it should set the
     * right bit in IDR and send an irq to syscfg.
     */
    unsigned int pin = test_pin(data);
    uint32_t gpio = test_gpio_addr(data);
    unsigned int gpio_id = get_gpio_id(gpio);

    qtest_irq_intercept_in(global_qtest, SYSCFG);

    /* Configure a line as input, raise it, and check that the pin is high */
    gpio_set_2bits(gpio, MODER, pin, MODER_INPUT);
    gpio_set_irq(gpio, pin, 1);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) | (1 << pin));
    g_assert_true(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Lower the line and check that the pin is low */
    gpio_set_irq(gpio, pin, 0);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));
    g_assert_false(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Clean the test */
    gpio_writel(gpio, MODER, reset(gpio, MODER));
    disconnect_all_pins(gpio);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR));
}

static void test_pull_up_pull_down(const void *data)
{
    /*
     * Test that a floating pin with pull-up sets the pin
     * high and vice-versa.
     */
    unsigned int pin = test_pin(data);
    uint32_t gpio = test_gpio_addr(data);
    unsigned int gpio_id = get_gpio_id(gpio);

    qtest_irq_intercept_in(global_qtest, SYSCFG);

    /* Configure a line as input with pull-up, check the line is set high */
    gpio_set_2bits(gpio, MODER, pin, MODER_INPUT);
    gpio_set_2bits(gpio, PUPDR, pin, PUPDR_PULLUP);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) | (1 << pin));
    g_assert_true(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Configure the line with pull-down, check the line is low */
    gpio_set_2bits(gpio, PUPDR, pin, PUPDR_PULLDOWN);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));
    g_assert_false(get_irq(gpio_id * NUM_GPIO_PINS + pin));

    /* Clean the test */
    gpio_writel(gpio, MODER, reset(gpio, MODER));
    gpio_writel(gpio, PUPDR, reset(gpio, PUPDR));
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR));
}

static void test_push_pull(const void *data)
{
    /*
     * Test that configuring a line in push-pull output mode
     * disconnects the pin, that the pin can't be set or reset
     * externally afterwards.
     */
    unsigned int pin = test_pin(data);
    uint32_t gpio = test_gpio_addr(data);
    uint32_t gpio2 = GPIO_BASE_ADDR + (GPIO_H - gpio);

    qtest_irq_intercept_in(global_qtest, SYSCFG);

    /* Setting a line high externally, configuring it in push-pull output */
    /* And checking the pin was disconnected */
    gpio_set_irq(gpio, pin, 1);
    gpio_set_2bits(gpio, MODER, pin, MODER_OUTPUT);
    g_assert_cmphex(get_disconnected_pins(gpio), ==, 0xFFFF);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));

    /* Setting a line low externally, configuring it in push-pull output */
    /* And checking the pin was disconnected */
    gpio_set_irq(gpio2, pin, 0);
    gpio_set_bit(gpio2, ODR, pin, 1);
    gpio_set_2bits(gpio2, MODER, pin, MODER_OUTPUT);
    g_assert_cmphex(get_disconnected_pins(gpio2), ==, 0xFFFF);
    g_assert_cmphex(gpio_readl(gpio2, IDR), ==, reset(gpio2, IDR) | (1 << pin));

    /* Trying to set a push-pull output pin, checking it doesn't work */
    gpio_set_irq(gpio, pin, 1);
    g_assert_cmphex(get_disconnected_pins(gpio), ==, 0xFFFF);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));

    /* Trying to reset a push-pull output pin, checking it doesn't work */
    gpio_set_irq(gpio2, pin, 0);
    g_assert_cmphex(get_disconnected_pins(gpio2), ==, 0xFFFF);
    g_assert_cmphex(gpio_readl(gpio2, IDR), ==, reset(gpio2, IDR) | (1 << pin));

    /* Clean the test */
    gpio_writel(gpio, MODER, reset(gpio, MODER));
    gpio_writel(gpio2, ODR, reset(gpio2, ODR));
    gpio_writel(gpio2, MODER, reset(gpio2, MODER));
}

static void test_open_drain(const void *data)
{
    /*
     * Test that configuring a line in open-drain output mode
     * disconnects a pin set high externally and that the pin
     * can't be set high externally while configured in open-drain.
     *
     * However a pin set low externally shouldn't be disconnected,
     * and it can be set low externally when in open-drain mode.
     */
    unsigned int pin = test_pin(data);
    uint32_t gpio = test_gpio_addr(data);
    uint32_t gpio2 = GPIO_BASE_ADDR + (GPIO_H - gpio);

    qtest_irq_intercept_in(global_qtest, SYSCFG);

    /* Setting a line high externally, configuring it in open-drain output */
    /* And checking the pin was disconnected */
    gpio_set_irq(gpio, pin, 1);
    gpio_set_bit(gpio, OTYPER, pin, OTYPER_OPEN_DRAIN);
    gpio_set_2bits(gpio, MODER, pin, MODER_OUTPUT);
    g_assert_cmphex(get_disconnected_pins(gpio), ==, 0xFFFF);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));

    /* Setting a line low externally, configuring it in open-drain output */
    /* And checking the pin wasn't disconnected */
    gpio_set_irq(gpio2, pin, 0);
    gpio_set_bit(gpio2, ODR, pin, 1);
    gpio_set_bit(gpio2, OTYPER, pin, OTYPER_OPEN_DRAIN);
    gpio_set_2bits(gpio2, MODER, pin, MODER_OUTPUT);
    g_assert_cmphex(get_disconnected_pins(gpio2), ==, 0xFFFF & ~(1 << pin));
    g_assert_cmphex(gpio_readl(gpio2, IDR), ==,
                               reset(gpio2, IDR) & ~(1 << pin));

    /* Trying to set a open-drain output pin, checking it doesn't work */
    gpio_set_irq(gpio, pin, 1);
    g_assert_cmphex(get_disconnected_pins(gpio), ==, 0xFFFF);
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR) & ~(1 << pin));

    /* Trying to reset a open-drain output pin, checking it works */
    gpio_set_bit(gpio, ODR, pin, 1);
    gpio_set_irq(gpio, pin, 0);
    g_assert_cmphex(get_disconnected_pins(gpio2), ==, 0xFFFF & ~(1 << pin));
    g_assert_cmphex(gpio_readl(gpio2, IDR), ==,
                               reset(gpio2, IDR) & ~(1 << pin));

    /* Clean the test */
    disconnect_all_pins(gpio2);
    gpio_writel(gpio2, OTYPER, reset(gpio2, OTYPER));
    gpio_writel(gpio2, ODR, reset(gpio2, ODR));
    gpio_writel(gpio2, MODER, reset(gpio2, MODER));
    g_assert_cmphex(gpio_readl(gpio2, IDR), ==, reset(gpio2, IDR));
    disconnect_all_pins(gpio);
    gpio_writel(gpio, OTYPER, reset(gpio, OTYPER));
    gpio_writel(gpio, ODR, reset(gpio, ODR));
    gpio_writel(gpio, MODER, reset(gpio, MODER));
    g_assert_cmphex(gpio_readl(gpio, IDR), ==, reset(gpio, IDR));
}

static void test_bsrr_brr(const void *data)
{
    /*
     * Test that writing a '1' in BSS and BSRR
     * has the desired effect on ODR.
     * In BSRR, BSx has priority over BRx.
     */
    unsigned int pin = test_pin(data);
    uint32_t gpio = test_gpio_addr(data);

    gpio_writel(gpio, BSRR, (1 << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, reset(gpio, ODR) | (1 << pin));

    gpio_writel(gpio, BSRR, (1 << (pin + NUM_GPIO_PINS)));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, reset(gpio, ODR));

    gpio_writel(gpio, BSRR, (1 << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, reset(gpio, ODR) | (1 << pin));

    gpio_writel(gpio, BRR, (1 << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, reset(gpio, ODR));

    /* BSx should have priority over BRx */
    gpio_writel(gpio, BSRR, (1 << pin) | (1 << (pin + NUM_GPIO_PINS)));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, reset(gpio, ODR) | (1 << pin));

    gpio_writel(gpio, BRR, (1 << pin));
    g_assert_cmphex(gpio_readl(gpio, ODR), ==, reset(gpio, ODR));

    gpio_writel(gpio, ODR, reset(gpio, ODR));
}

static void test_clock_enable(void)
{
    /*
     * For each GPIO, enable its clock in RCC
     * and check that its clock period changes to SYSCLK_PERIOD
     */
    unsigned int gpio_id;

    for (uint32_t gpio = GPIO_A; gpio <= GPIO_H; gpio += GPIO_B - GPIO_A) {
        gpio_id = get_gpio_id(gpio);
        g_autofree char *path = g_strdup_printf("/machine/soc/gpio%c/clk",
                                                gpio_id + 'a');
        g_assert_cmpuint(get_clock_period(global_qtest, path), ==, 0);
        /* Enable the gpio clock */
        writel(RCC_AHB2ENR, readl(RCC_AHB2ENR) | (0x1 << gpio_id));
        g_assert_cmpuint(get_clock_period(global_qtest, path), ==,
                         SYSCLK_PERIOD);
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();
    qtest_add_func("stm32l4x5/gpio/test_idr_reset_value",
                   test_idr_reset_value);
    /*
     * The inputs for the tests (gpio and pin) can be changed,
     * but the tests don't work for pins that are high at reset
     * (GPIOA15, GPIO13 and GPIOB5).
     * Specifically, rising the pin then checking `get_irq()`
     * is problematic since the pin was already high.
     */
    qtest_add_data_func("stm32l4x5/gpio/test_gpioc5_output_mode",
                        test_data(GPIO_C, 5),
                        test_gpio_output_mode);
    qtest_add_data_func("stm32l4x5/gpio/test_gpioh3_output_mode",
                        test_data(GPIO_H, 3),
                        test_gpio_output_mode);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_input_mode1",
                        test_data(GPIO_D, 6),
                        test_gpio_input_mode);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_input_mode2",
                        test_data(GPIO_C, 10),
                        test_gpio_input_mode);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_pull_up_pull_down1",
                        test_data(GPIO_B, 5),
                        test_pull_up_pull_down);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_pull_up_pull_down2",
                        test_data(GPIO_F, 1),
                        test_pull_up_pull_down);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_push_pull1",
                        test_data(GPIO_G, 6),
                        test_push_pull);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_push_pull2",
                        test_data(GPIO_H, 3),
                        test_push_pull);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_open_drain1",
                        test_data(GPIO_C, 4),
                        test_open_drain);
    qtest_add_data_func("stm32l4x5/gpio/test_gpio_open_drain2",
                        test_data(GPIO_E, 11),
                        test_open_drain);
    qtest_add_data_func("stm32l4x5/gpio/test_bsrr_brr1",
                        test_data(GPIO_A, 12),
                        test_bsrr_brr);
    qtest_add_data_func("stm32l4x5/gpio/test_bsrr_brr2",
                        test_data(GPIO_D, 0),
                        test_bsrr_brr);
    qtest_add_func("stm32l4x5/gpio/test_clock_enable",
                   test_clock_enable);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
