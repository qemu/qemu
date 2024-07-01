/*
 * QTest testcase for STM32L4x5_EXTI
 *
 * Copyright (c) 2023 Arnaud Minier <arnaud.minier@telecom-paris.fr>
 * Copyright (c) 2023 Inès Varhol <ines.varhol@telecom-paris.fr>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

#define EXTI_BASE_ADDR 0x40010400
#define EXTI_IMR1 0x00
#define EXTI_EMR1 0x04
#define EXTI_RTSR1 0x08
#define EXTI_FTSR1 0x0C
#define EXTI_SWIER1 0x10
#define EXTI_PR1 0x14
#define EXTI_IMR2 0x20
#define EXTI_EMR2 0x24
#define EXTI_RTSR2 0x28
#define EXTI_FTSR2 0x2C
#define EXTI_SWIER2 0x30
#define EXTI_PR2 0x34

#define NVIC_ISER 0xE000E100
#define NVIC_ISPR 0xE000E200
#define NVIC_ICPR 0xE000E280

#define EXTI0_IRQ 6
#define EXTI1_IRQ 7
#define EXTI5_9_IRQ 23
#define EXTI35_IRQ 1

static void enable_nvic_irq(unsigned int n)
{
    writel(NVIC_ISER, 1 << n);
}

static void unpend_nvic_irq(unsigned int n)
{
    writel(NVIC_ICPR, 1 << n);
}

static bool check_nvic_pending(unsigned int n)
{
    return readl(NVIC_ISPR) & (1 << n);
}

static void exti_writel(unsigned int offset, uint32_t value)
{
    writel(EXTI_BASE_ADDR + offset, value);
}

static uint32_t exti_readl(unsigned int offset)
{
    return readl(EXTI_BASE_ADDR + offset);
}

static void exti_set_irq(int num, int level)
{
   qtest_set_irq_in(global_qtest, "/machine/soc/exti", NULL,
                    num, level);
}

static void test_reg_write_read(void)
{
    /* Test that non-reserved bits in xMR and xTSR can be set and cleared */

    exti_writel(EXTI_IMR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_IMR1), ==, 0xFFFFFFFF);
    exti_writel(EXTI_IMR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_IMR1), ==, 0x00000000);

    exti_writel(EXTI_EMR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_EMR1), ==, 0xFFFFFFFF);
    exti_writel(EXTI_EMR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_EMR1), ==, 0x00000000);

    exti_writel(EXTI_RTSR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_RTSR1), ==, 0x007DFFFF);
    exti_writel(EXTI_RTSR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_RTSR1), ==, 0x00000000);

    exti_writel(EXTI_FTSR1, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_FTSR1), ==, 0x007DFFFF);
    exti_writel(EXTI_FTSR1, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_FTSR1), ==, 0x00000000);

    exti_writel(EXTI_IMR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_IMR2), ==, 0x000000FF);
    exti_writel(EXTI_IMR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_IMR2), ==, 0x00000000);

    exti_writel(EXTI_EMR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_EMR2), ==, 0x000000FF);
    exti_writel(EXTI_EMR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_EMR2), ==, 0x00000000);

    exti_writel(EXTI_RTSR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000078);
    exti_writel(EXTI_RTSR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000000);

    exti_writel(EXTI_FTSR2, 0xFFFFFFFF);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000078);
    exti_writel(EXTI_FTSR2, 0x00000000);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000000);
}

static void test_direct_lines_write(void)
{
    /* Test that direct lines reserved bits are not written to */

    exti_writel(EXTI_RTSR1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_RTSR1), ==, 0x00000000);

    exti_writel(EXTI_FTSR1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_FTSR1), ==, 0x00000000);

    exti_writel(EXTI_SWIER1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000000);

    exti_writel(EXTI_PR1, 0xFF820000);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);

    exti_writel(EXTI_RTSR2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000000);

    exti_writel(EXTI_FTSR2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000000);

    exti_writel(EXTI_SWIER2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000000);

    exti_writel(EXTI_PR2, 0x00000087);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
}

static void test_reserved_bits_write(void)
{
    /* Test that reserved bits stay are not written to */

    exti_writel(EXTI_IMR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_IMR2), ==, 0x00000000);

    exti_writel(EXTI_EMR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_EMR2), ==, 0x00000000);

    exti_writel(EXTI_RTSR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_RTSR2), ==, 0x00000000);

    exti_writel(EXTI_FTSR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_FTSR2), ==, 0x00000000);

    exti_writel(EXTI_SWIER2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000000);

    exti_writel(EXTI_PR2, 0xFFFFFF00);
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
}

static void test_software_interrupt(void)
{
    /*
     * Test that we can launch a software irq by :
     * - enabling its line in IMR
     * - and then setting a bit from '0' to '1' in SWIER
     *
     * And that the interruption stays pending in NVIC
     * even after clearing the pending bit in PR.
     */

    /*
     * Testing interrupt line EXTI0
     * Bit 0 in EXTI_*1 registers (EXTI0) corresponds to GPIO Px_0
     */

    enable_nvic_irq(EXTI0_IRQ);
    /* Check that there are no interrupts already pending in PR */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that this specific interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Enable interrupt line EXTI0 */
    exti_writel(EXTI_IMR1, 0x00000001);
    /* Set the right SWIER bit from '0' to '1' */
    exti_writel(EXTI_SWIER1, 0x00000000);
    exti_writel(EXTI_SWIER1, 0x00000001);

    /* Check that the write in SWIER was effective */
    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000001);
    /* Check that the corresponding pending bit in PR is set */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    /* Check that the corresponding interrupt is pending in the NVIC */
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    /* Clear the pending bit in PR */
    exti_writel(EXTI_PR1, 0x00000001);

    /* Check that the write in PR was effective */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that the corresponding bit in SWIER was cleared */
    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000000);
    /* Check that the interrupt is still pending in the NVIC */
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    /*
     * Testing interrupt line EXTI35
     * Bit 3 in EXTI_*2 registers (EXTI35) corresponds to PVM 1 Wakeup
     */

    enable_nvic_irq(EXTI35_IRQ);
    /* Check that there are no interrupts already pending */
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));

    /* Enable interrupt line EXTI0 */
    exti_writel(EXTI_IMR2, 0x00000008);
    /* Set the right SWIER bit from '0' to '1' */
    exti_writel(EXTI_SWIER2, 0x00000000);
    exti_writel(EXTI_SWIER2, 0x00000008);

    /* Check that the write in SWIER was effective */
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000008);
    /* Check that the corresponding pending bit in PR is set */
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000008);
    /* Check that the corresponding interrupt is pending in the NVIC */
    g_assert_true(check_nvic_pending(EXTI35_IRQ));

    /* Clear the pending bit in PR */
    exti_writel(EXTI_PR2, 0x00000008);

    /* Check that the write in PR was effective */
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    /* Check that the corresponding bit in SWIER was cleared */
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000000);
    /* Check that the interrupt is still pending in the NVIC */
    g_assert_true(check_nvic_pending(EXTI35_IRQ));

    /* Clean NVIC */
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));
    unpend_nvic_irq(EXTI35_IRQ);
    g_assert_false(check_nvic_pending(EXTI35_IRQ));
}

static void test_edge_selector(void)
{
    enable_nvic_irq(EXTI0_IRQ);

    /* Configure EXTI line 0 irq on rising edge */
    exti_set_irq(0, 1);
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);
    exti_writel(EXTI_FTSR1, 0x00000000);

    /* Test that an irq is raised on rising edge only */
    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    /* Clean the test */
    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Configure EXTI line 0 irq on falling edge */
    exti_set_irq(0, 0);
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000000);
    exti_writel(EXTI_FTSR1, 0x00000001);

    /* Test that an irq is raised on falling edge only */
    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    /* Clean the test */
    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Configure EXTI line 0 irq on falling and rising edge */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000001);
    exti_writel(EXTI_FTSR1, 0x00000001);

    /* Test that an irq is raised on rising edge */
    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    /* Clean the test */
    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Test that an irq is raised on falling edge */
    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000001);
    g_assert_true(check_nvic_pending(EXTI0_IRQ));

    /* Clean the test */
    exti_writel(EXTI_PR1, 0x00000001);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    unpend_nvic_irq(EXTI0_IRQ);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Configure EXTI line 0 irq without selecting an edge trigger */
    exti_writel(EXTI_IMR1, 0x00000001);
    exti_writel(EXTI_RTSR1, 0x00000000);
    exti_writel(EXTI_FTSR1, 0x00000000);

    /* Test that no irq is raised */
    exti_set_irq(0, 1);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    exti_set_irq(0, 0);
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    g_assert_false(check_nvic_pending(EXTI0_IRQ));
}

static void test_no_software_interrupt(void)
{
    /*
     * Test that software irq doesn't happen when :
     * - corresponding bit in IMR isn't set
     * - SWIER is set to 1 before IMR is set to 1
     */

    /*
     * Testing interrupt line EXTI0
     * Bit 0 in EXTI_*1 registers (EXTI0) corresponds to GPIO Px_0
     */

    enable_nvic_irq(EXTI0_IRQ);
    /* Check that there are no interrupts already pending in PR */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that this specific interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Mask interrupt line EXTI0 */
    exti_writel(EXTI_IMR1, 0x00000000);
    /* Set the corresponding SWIER bit from '0' to '1' */
    exti_writel(EXTI_SWIER1, 0x00000000);
    exti_writel(EXTI_SWIER1, 0x00000001);

    /* Check that the write in SWIER was effective */
    g_assert_cmphex(exti_readl(EXTI_SWIER1), ==, 0x00000001);
    /* Check that the pending bit in PR wasn't set */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that the interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /* Enable interrupt line EXTI0 */
    exti_writel(EXTI_IMR1, 0x00000001);

    /* Check that the pending bit in PR wasn't set */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that the interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI0_IRQ));

    /*
     * Testing interrupt line EXTI35
     * Bit 3 in EXTI_*2 registers (EXTI35) corresponds to PVM 1 Wakeup
     */

    enable_nvic_irq(EXTI35_IRQ);
    /* Check that there are no interrupts already pending in PR */
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    /* Check that this specific interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI35_IRQ));

    /* Mask interrupt line EXTI35 */
    exti_writel(EXTI_IMR2, 0x00000000);
    /* Set the corresponding SWIER bit from '0' to '1' */
    exti_writel(EXTI_SWIER2, 0x00000000);
    exti_writel(EXTI_SWIER2, 0x00000008);

    /* Check that the write in SWIER was effective */
    g_assert_cmphex(exti_readl(EXTI_SWIER2), ==, 0x00000008);
    /* Check that the pending bit in PR wasn't set */
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    /* Check that the interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI35_IRQ));

    /* Enable interrupt line EXTI35 */
    exti_writel(EXTI_IMR2, 0x00000008);

    /* Check that the pending bit in PR wasn't set */
    g_assert_cmphex(exti_readl(EXTI_PR2), ==, 0x00000000);
    /* Check that the interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI35_IRQ));
}

static void test_masked_interrupt(void)
{
    /*
     * Test that irq doesn't happen when :
     * - corresponding bit in IMR isn't set
     * - SWIER is set to 1 before IMR is set to 1
     */

    /*
     * Testing interrupt line EXTI1
     * with rising edge from GPIOx pin 1
     */

    enable_nvic_irq(EXTI1_IRQ);
    /* Check that there are no interrupts already pending in PR */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that this specific interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    /* Mask interrupt line EXTI1 */
    exti_writel(EXTI_IMR1, 0x00000000);

    /* Configure interrupt on rising edge */
    exti_writel(EXTI_RTSR1, 0x00000002);

    /* Simulate rising edge from GPIO line 1 */
    exti_set_irq(1, 1);

    /* Check that the pending bit in PR wasn't set */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that the interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    /* Enable interrupt line EXTI1 */
    exti_writel(EXTI_IMR1, 0x00000002);

    /* Check that the pending bit in PR wasn't set */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that the interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    /* Clean EXTI */
    exti_set_irq(1, 0);
}

static void test_interrupt(void)
{
    /*
     * Test that we can launch an irq by :
     * - enabling its line in IMR
     * - configuring interrupt on rising edge
     * - and then setting the input line from '0' to '1'
     *
     * And that the interruption stays pending in NVIC
     * even after clearing the pending bit in PR.
     */

    /*
     * Testing interrupt line EXTI1
     * with rising edge from GPIOx pin 1
     */

    enable_nvic_irq(EXTI1_IRQ);
    /* Check that there are no interrupts already pending in PR */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that this specific interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    /* Enable interrupt line EXTI1 */
    exti_writel(EXTI_IMR1, 0x00000002);

    /* Configure interrupt on rising edge */
    exti_writel(EXTI_RTSR1, 0x00000002);

    /* Simulate rising edge from GPIO line 1 */
    exti_set_irq(1, 1);

    /* Check that the pending bit in PR was set */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000002);
    /* Check that the interrupt is pending in NVIC */
    g_assert_true(check_nvic_pending(EXTI1_IRQ));

    /* Clear the pending bit in PR */
    exti_writel(EXTI_PR1, 0x00000002);

    /* Check that the write in PR was effective */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that the interrupt is still pending in the NVIC */
    g_assert_true(check_nvic_pending(EXTI1_IRQ));

    /* Clean NVIC */
    unpend_nvic_irq(EXTI1_IRQ);
    g_assert_false(check_nvic_pending(EXTI1_IRQ));

    /* Clean EXTI */
    exti_set_irq(1, 0);
}

static void test_orred_interrupts(void)
{
    /*
     * For lines EXTI5..9 (fanned-in to NVIC irq 23),
     * test that raising the line pends interrupt
     * 23 in NVIC.
     */
    enable_nvic_irq(EXTI5_9_IRQ);
    /* Check that there are no interrupts already pending in PR */
    g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
    /* Check that this specific interrupt isn't pending in NVIC */
    g_assert_false(check_nvic_pending(EXTI5_9_IRQ));

    /* Enable interrupt lines EXTI[5..9] */
    exti_writel(EXTI_IMR1, (0x1F << 5));

    /* Configure interrupt on rising edge */
    exti_writel(EXTI_RTSR1, (0x1F << 5));

    /* Raise GPIO line i, check that the interrupt is pending */
    for (unsigned i = 5; i < 10; i++) {
        exti_set_irq(i, 1);
        g_assert_cmphex(exti_readl(EXTI_PR1), ==, 1 << i);
        g_assert_true(check_nvic_pending(EXTI5_9_IRQ));

        exti_writel(EXTI_PR1, 1 << i);
        g_assert_cmphex(exti_readl(EXTI_PR1), ==, 0x00000000);
        g_assert_true(check_nvic_pending(EXTI5_9_IRQ));

        unpend_nvic_irq(EXTI5_9_IRQ);
        g_assert_false(check_nvic_pending(EXTI5_9_IRQ));

        exti_set_irq(i, 0);
    }
}

int main(int argc, char **argv)
{
    int ret;

    g_test_init(&argc, &argv, NULL);
    g_test_set_nonfatal_assertions();
    qtest_add_func("stm32l4x5/exti/direct_lines", test_direct_lines_write);
    qtest_add_func("stm32l4x5/exti/reserved_bits", test_reserved_bits_write);
    qtest_add_func("stm32l4x5/exti/reg_write_read", test_reg_write_read);
    qtest_add_func("stm32l4x5/exti/no_software_interrupt",
                   test_no_software_interrupt);
    qtest_add_func("stm32l4x5/exti/software_interrupt",
                   test_software_interrupt);
    qtest_add_func("stm32l4x5/exti/masked_interrupt", test_masked_interrupt);
    qtest_add_func("stm32l4x5/exti/interrupt", test_interrupt);
    qtest_add_func("stm32l4x5/exti/test_edge_selector", test_edge_selector);
    qtest_add_func("stm32l4x5/exti/test_orred_interrupts",
                   test_orred_interrupts);

    qtest_start("-machine b-l475e-iot01a");
    ret = g_test_run();
    qtest_end();

    return ret;
}
