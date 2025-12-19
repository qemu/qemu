/*
 * QTest testcase for the ASPEED AST2700 SGPIO Controller.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright (C) 2025 Google LLC.
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "qobject/qdict.h"
#include "libqtest-single.h"
#include "hw/core/registerfields.h"
#include "hw/gpio/aspeed_sgpio.h"

#define AST2700_SGPIO0_BASE 0x14C0C000
#define AST2700_SGPIO1_BASE 0x14C0D000

static void test_output_pins(const char *machine, const uint32_t base, int idx)
{
    QTestState *s = qtest_init(machine);
    char name[16];
    char qom_path[64];
    uint32_t offset = 0;
    uint32_t value = 0;
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR; i++) {
        /* Odd index is output port */
        sprintf(name, "sgpio%03d", i * 2 + 1);
        sprintf(qom_path, "/machine/soc/sgpio[%d]", idx);
        offset = base + (R_SGPIO_0_CONTROL + i) * 4;
        /* set serial output */
        qtest_writel(s, offset, 0x00000001);
        value = qtest_readl(s, offset);
        g_assert_cmphex(SHARED_FIELD_EX32(value, SGPIO_SERIAL_OUT_VAL), ==, 1);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, true);

        /* clear serial output */
        qtest_writel(s, offset, 0x00000000);
        value = qtest_readl(s, offset);
        g_assert_cmphex(SHARED_FIELD_EX32(value, SGPIO_SERIAL_OUT_VAL), ==, 0);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, false);
    }
    qtest_quit(s);
}

static void test_input_pins(const char *machine, const uint32_t base, int idx)
{
    QTestState *s = qtest_init(machine);
    char name[16];
    char qom_path[64];
    uint32_t offset = 0;
    uint32_t value = 0;
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR; i++) {
        /* Even index is input port */
        sprintf(name, "sgpio%03d", i * 2);
        sprintf(qom_path, "/machine/soc/sgpio[%d]", idx);
        offset = base + (R_SGPIO_0_CONTROL + i) * 4;
        /* set serial input */
        qtest_qom_set_bool(s, qom_path, name, true);
        value = qtest_readl(s, offset);
        g_assert_cmphex(SHARED_FIELD_EX32(value, SGPIO_SERIAL_IN_VAL), ==, 1);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, true);

        /* clear serial input */
        qtest_qom_set_bool(s, qom_path, name, false);
        value = qtest_readl(s, offset);
        g_assert_cmphex(SHARED_FIELD_EX32(value, SGPIO_SERIAL_IN_VAL), ==, 0);
        g_assert_cmphex(qtest_qom_get_bool(s, qom_path, name), ==, false);
    }
    qtest_quit(s);
}

static void test_irq_level_high(const char *machine,
                                const uint32_t base, int idx)
{
    QTestState *s = qtest_init(machine);
    char name[16];
    char qom_path[64];
    uint32_t ctrl_offset = 0;
    uint32_t int_offset = 0;
    uint32_t int_reg_idx = 0;
    uint32_t int_bit_idx = 0;
    uint32_t value = 0;
    for (int i = 0; i < ASPEED_SGPIO_MAX_PIN_PAIR; i++) {
        /* Even index is input port */
        sprintf(name, "sgpio%03d", i * 2);
        sprintf(qom_path, "/machine/soc/sgpio[%d]", idx);
        int_reg_idx = i / 32;
        int_bit_idx = i % 32;
        int_offset = base + (R_SGPIO_INT_STATUS_0 + int_reg_idx) * 4;
        ctrl_offset = base + (R_SGPIO_0_CONTROL + i) * 4;

        /* Enable the interrupt */
        value = SHARED_FIELD_DP32(value, SGPIO_INT_EN, 1);
        qtest_writel(s, ctrl_offset, value);

        /* Set the interrupt type to level-high trigger */
        value = SHARED_FIELD_DP32(qtest_readl(s, ctrl_offset),
                                              SGPIO_INT_TYPE, 3);
        qtest_writel(s, ctrl_offset, value);

        /* Set serial input high */
        qtest_qom_set_bool(s, qom_path, name, true);
        value = qtest_readl(s, ctrl_offset);
        g_assert_cmphex(SHARED_FIELD_EX32(value, SGPIO_SERIAL_IN_VAL), ==, 1);

        /* Interrupt status is set */
        value = qtest_readl(s, int_offset);
        g_assert_cmphex(extract32(value, int_bit_idx, 1), ==, 1);

        /* Clear Interrupt */
        value = SHARED_FIELD_DP32(qtest_readl(s, ctrl_offset),
                                              SGPIO_INT_STATUS, 1);
        qtest_writel(s, ctrl_offset, value);
        value = qtest_readl(s, int_offset);
        g_assert_cmphex(extract32(value, int_bit_idx, 1), ==, 0);

        /* Clear serial input */
        qtest_qom_set_bool(s, qom_path, name, false);
        value = qtest_readl(s, ctrl_offset);
        g_assert_cmphex(SHARED_FIELD_EX32(value, SGPIO_SERIAL_IN_VAL), ==, 0);
    }
    qtest_quit(s);
}

static void test_ast_2700_sgpio_input(void)
{
    test_input_pins("-machine ast2700-evb",
                    AST2700_SGPIO0_BASE, 0);
    test_input_pins("-machine ast2700-evb",
                    AST2700_SGPIO1_BASE, 1);
}

static void test_ast_2700_sgpio_output(void)
{
    test_output_pins("-machine ast2700-evb",
                    AST2700_SGPIO0_BASE, 0);
    test_output_pins("-machine ast2700-evb",
                    AST2700_SGPIO1_BASE, 1);
    test_irq_level_high("-machine ast2700-evb",
                    AST2700_SGPIO0_BASE, 0);
    test_irq_level_high("-machine ast2700-evb",
                    AST2700_SGPIO1_BASE, 1);
}

static void test_ast_2700_sgpio_irq(void)
{
    test_irq_level_high("-machine ast2700-evb",
                    AST2700_SGPIO0_BASE, 0);
    test_irq_level_high("-machine ast2700-evb",
                    AST2700_SGPIO1_BASE, 1);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/ast2700/sgpio/ast_2700_sgpio_input",
                   test_ast_2700_sgpio_input);
    qtest_add_func("/ast2700/sgpio/ast_2700_sgpio_output",
                   test_ast_2700_sgpio_output);
    qtest_add_func("/ast2700/sgpio/ast_2700_sgpio_irq",
                   test_ast_2700_sgpio_irq);

    return g_test_run();
}
