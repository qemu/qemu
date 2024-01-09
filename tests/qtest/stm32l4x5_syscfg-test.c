/*
 * QTest testcase for STM32L4x5_SYSCFG
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define SYSCFG_BASE_ADDR 0x40010000
#define SYSCFG_MEMRMP 0x00
#define SYSCFG_CFGR1 0x04
#define SYSCFG_EXTICR1 0x08
#define SYSCFG_EXTICR2 0x0C
#define SYSCFG_EXTICR3 0x10
#define SYSCFG_EXTICR4 0x14
#define SYSCFG_SCSR 0x18
#define SYSCFG_CFGR2 0x1C
#define SYSCFG_SWPR 0x20
#define SYSCFG_SKR 0x24
#define SYSCFG_SWPR2 0x28
#define INVALID_ADDR 0x2C

static void syscfg_writel(unsigned int offset, uint32_t value)
{
    writel(SYSCFG_BASE_ADDR + offset, value);
}

static uint32_t syscfg_readl(unsigned int offset)
{
    return readl(SYSCFG_BASE_ADDR + offset);
}

static void syscfg_set_irq(int num, int level)
{
   qtest_set_irq_in(global_qtest, "/machine/soc/syscfg",
                    NULL, num, level);
}

static void system_reset(void)
{
    QDict *response;
    response = qtest_qmp(global_qtest, "{'execute': 'system_reset'}");
    g_assert(qdict_haskey(response, "return"));
    qobject_unref(response);
}

static void test_reset(void)
{
    /*
     * Test that registers are initialized at the correct values
     */
    g_assert_cmpuint(syscfg_readl(SYSCFG_MEMRMP), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR1), ==, 0x7C000001);

    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR1), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR2), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR3), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR4), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_SCSR), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR2), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_SWPR), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_SKR), ==, 0x00000000);

    g_assert_cmpuint(syscfg_readl(SYSCFG_SWPR2), ==, 0x00000000);
}

static void test_reserved_bits(void)
{
    /*
     * Test that reserved bits stay at reset value
     * (which is 0 for all of them) by writing '1'
     * in all reserved bits (keeping reset value for
     * other bits) and checking that the
     * register is still at reset value
     */
    syscfg_writel(SYSCFG_MEMRMP, 0xFFFFFEF8);
    g_assert_cmpuint(syscfg_readl(SYSCFG_MEMRMP), ==, 0x00000000);

    syscfg_writel(SYSCFG_CFGR1, 0x7F00FEFF);
    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR1), ==, 0x7C000001);

    syscfg_writel(SYSCFG_EXTICR1, 0xFFFF0000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR1), ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR2, 0xFFFF0000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR2), ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR3, 0xFFFF0000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR3), ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR4, 0xFFFF0000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR4), ==, 0x00000000);

    syscfg_writel(SYSCFG_SKR, 0xFFFFFF00);
    g_assert_cmpuint(syscfg_readl(SYSCFG_SKR), ==, 0x00000000);
}

static void test_set_and_clear(void)
{
    /*
     * Test that regular bits can be set and cleared
     */
    syscfg_writel(SYSCFG_MEMRMP, 0x00000107);
    g_assert_cmpuint(syscfg_readl(SYSCFG_MEMRMP), ==, 0x00000107);
    syscfg_writel(SYSCFG_MEMRMP, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_MEMRMP), ==, 0x00000000);

    /* cfgr1 bit 0 is clear only so we keep it set */
    syscfg_writel(SYSCFG_CFGR1, 0xFCFF0101);
    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR1), ==, 0xFCFF0101);
    syscfg_writel(SYSCFG_CFGR1, 0x00000001);
    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR1), ==, 0x00000001);

    syscfg_writel(SYSCFG_EXTICR1, 0x0000FFFF);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR1), ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR1, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR1), ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR2, 0x0000FFFF);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR2), ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR2, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR2), ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR3, 0x0000FFFF);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR3), ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR3, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR3), ==, 0x00000000);

    syscfg_writel(SYSCFG_EXTICR4, 0x0000FFFF);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR4), ==, 0x0000FFFF);
    syscfg_writel(SYSCFG_EXTICR4, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_EXTICR4), ==, 0x00000000);

    syscfg_writel(SYSCFG_SKR, 0x000000FF);
    g_assert_cmpuint(syscfg_readl(SYSCFG_SKR), ==, 0x000000FF);
    syscfg_writel(SYSCFG_SKR, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_SKR), ==, 0x00000000);
}

static void test_clear_by_writing_1(void)
{
    /*
     * Test that writing '1' doesn't set the bit
     */
    syscfg_writel(SYSCFG_CFGR2, 0x00000100);
    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR2), ==, 0x00000000);
}

static void test_set_only_bits(void)
{
    /*
     * Test that set only bits stay can't be cleared
     */
    syscfg_writel(SYSCFG_CFGR2, 0x0000000F);
    syscfg_writel(SYSCFG_CFGR2, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR2), ==, 0x0000000F);

    syscfg_writel(SYSCFG_SWPR, 0xFFFFFFFF);
    syscfg_writel(SYSCFG_SWPR, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_SWPR), ==, 0xFFFFFFFF);

    syscfg_writel(SYSCFG_SWPR2, 0xFFFFFFFF);
    syscfg_writel(SYSCFG_SWPR2, 0x00000000);
    g_assert_cmpuint(syscfg_readl(SYSCFG_SWPR2), ==, 0xFFFFFFFF);

    system_reset();
}

static void test_clear_only_bits(void)
{
    /*
     * Test that clear only bits stay can't be set
     */
    syscfg_writel(SYSCFG_CFGR1, 0x00000000);
    syscfg_writel(SYSCFG_CFGR1, 0x00000001);
    g_assert_cmpuint(syscfg_readl(SYSCFG_CFGR1), ==, 0x00000000);

    system_reset();
}

static void test_interrupt(void)
{
    /*
     * Test that GPIO rising lines result in an irq
     * with the right configuration
     */
    qtest_irq_intercept_in(global_qtest, "/machine/soc/exti");

    /* GPIOA is the default source for EXTI lines 0 to 15 */

    syscfg_set_irq(0, 1);

    g_assert_true(get_irq(0));


    syscfg_set_irq(15, 1);

    g_assert_true(get_irq(15));

    /* Configure GPIOB[1] as the source input for EXTI1 */
    syscfg_writel(SYSCFG_EXTICR1, 0x00000010);

    syscfg_set_irq(17, 1);

    g_assert_true(get_irq(1));

    /* Clean the test */
    syscfg_writel(SYSCFG_EXTICR1, 0x00000000);
    syscfg_set_irq(0, 0);
    syscfg_set_irq(15, 0);
    syscfg_set_irq(17, 0);
}

static void test_irq_pin_multiplexer(void)
{
    /*
     * Test that syscfg irq sets the right exti irq
     */

    qtest_irq_intercept_in(global_qtest, "/machine/soc/exti");

    syscfg_set_irq(0, 1);

    /* Check that irq 0 was set and irq 15 wasn't */
    g_assert_true(get_irq(0));
    g_assert_false(get_irq(15));

    /* Clean the test */
    syscfg_set_irq(0, 0);

    syscfg_set_irq(15, 1);

    /* Check that irq 15 was set and irq 0 wasn't */
    g_assert_true(get_irq(15));
    g_assert_false(get_irq(0));

    /* Clean the test */
    syscfg_set_irq(15, 0);
}

static void test_irq_gpio_multiplexer(void)
{
    /*
     * Test that an irq is generated only by the right GPIO
     */

    qtest_irq_intercept_in(global_qtest, "/machine/soc/exti");

    /* GPIOA is the default source for EXTI lines 0 to 15 */

    /* Check that setting rising pin GPIOA[0] generates an irq */
    syscfg_set_irq(0, 1);

    g_assert_true(get_irq(0));

    /* Clean the test */
    syscfg_set_irq(0, 0);

    /* Check that setting rising pin GPIOB[0] doesn't generate an irq */
    syscfg_set_irq(16, 1);

    g_assert_false(get_irq(0));

    /* Clean the test */
    syscfg_set_irq(16, 0);

    /* Configure GPIOB[0] as the source input for EXTI0 */
    syscfg_writel(SYSCFG_EXTICR1, 0x00000001);

    /* Check that setting rising pin GPIOA[0] doesn't generate an irq */
    syscfg_set_irq(0, 1);

    g_assert_false(get_irq(0));

    /* Clean the test */
    syscfg_set_irq(0, 0);

    /* Check that setting rising pin GPIOB[0] generates an irq */
    syscfg_set_irq(16, 1);

    g_assert_true(get_irq(0));

    /* Clean the test */
    syscfg_set_irq(16, 0);
    syscfg_writel(SYSCFG_EXTICR1, 0x00000000);
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();

    qtest_add_func("stm32l4x5/syscfg/test_reset", test_reset);
    qtest_add_func("stm32l4x5/syscfg/test_reserved_bits",
                   test_reserved_bits);
    qtest_add_func("stm32l4x5/syscfg/test_set_and_clear",
                   test_set_and_clear);
    qtest_add_func("stm32l4x5/syscfg/test_clear_by_writing_1",
                   test_clear_by_writing_1);
    qtest_add_func("stm32l4x5/syscfg/test_set_only_bits",
                   test_set_only_bits);
    qtest_add_func("stm32l4x5/syscfg/test_clear_only_bits",
                   test_clear_only_bits);
    qtest_add_func("stm32l4x5/syscfg/test_interrupt",
                   test_interrupt);
    qtest_add_func("stm32l4x5/syscfg/test_irq_pin_multiplexer",
                   test_irq_pin_multiplexer);
    qtest_add_func("stm32l4x5/syscfg/test_irq_gpio_multiplexer",
                   test_irq_gpio_multiplexer);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
