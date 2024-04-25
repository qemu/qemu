/*
 * QTest testcases for IBM's Flexible Service Interface (FSI)
 *
 * Copyright (c) 2023 IBM Corporation
 *
 * Authors:
 *   Ninad Palsule <ninad@linux.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <glib/gstdio.h>

#include "qemu/module.h"
#include "libqtest-single.h"

/* Registers from ast2600 specifications */
#define ASPEED_FSI_ENGINER_TRIGGER   0x04
#define ASPEED_FSI_OPB0_BUS_SELECT   0x10
#define ASPEED_FSI_OPB1_BUS_SELECT   0x28
#define ASPEED_FSI_OPB0_RW_DIRECTION 0x14
#define ASPEED_FSI_OPB1_RW_DIRECTION 0x2c
#define ASPEED_FSI_OPB0_XFER_SIZE    0x18
#define ASPEED_FSI_OPB1_XFER_SIZE    0x30
#define ASPEED_FSI_OPB0_BUS_ADDR     0x1c
#define ASPEED_FSI_OPB1_BUS_ADDR     0x34
#define ASPEED_FSI_INTRRUPT_CLEAR    0x40
#define ASPEED_FSI_INTRRUPT_STATUS   0x48
#define ASPEED_FSI_OPB0_BUS_STATUS   0x80
#define ASPEED_FSI_OPB1_BUS_STATUS   0x8c
#define ASPEED_FSI_OPB0_READ_DATA    0x84
#define ASPEED_FSI_OPB1_READ_DATA    0x90

/*
 * FSI Base addresses from the ast2600 specifications.
 */
#define AST2600_OPB_FSI0_BASE_ADDR 0x1e79b000
#define AST2600_OPB_FSI1_BASE_ADDR 0x1e79b100

static uint32_t aspeed_fsi_base_addr;

static uint32_t aspeed_fsi_readl(QTestState *s, uint32_t reg)
{
    return qtest_readl(s, aspeed_fsi_base_addr + reg);
}

static void aspeed_fsi_writel(QTestState *s, uint32_t reg, uint32_t val)
{
    qtest_writel(s, aspeed_fsi_base_addr + reg, val);
}

/* Setup base address and select register */
static void test_fsi_setup(QTestState *s, uint32_t base_addr)
{
    uint32_t curval;

    aspeed_fsi_base_addr = base_addr;

    /* Set the base select register */
    if (base_addr == AST2600_OPB_FSI0_BASE_ADDR) {
        /* Unselect FSI1 */
        aspeed_fsi_writel(s, ASPEED_FSI_OPB1_BUS_SELECT, 0x0);
        curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB1_BUS_SELECT);
        g_assert_cmphex(curval, ==, 0x0);

        /* Select FSI0 */
        aspeed_fsi_writel(s, ASPEED_FSI_OPB0_BUS_SELECT, 0x1);
        curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB0_BUS_SELECT);
        g_assert_cmphex(curval, ==, 0x1);
    } else if (base_addr == AST2600_OPB_FSI1_BASE_ADDR) {
        /* Unselect FSI0 */
        aspeed_fsi_writel(s, ASPEED_FSI_OPB0_BUS_SELECT, 0x0);
        curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB0_BUS_SELECT);
        g_assert_cmphex(curval, ==, 0x0);

        /* Select FSI1 */
        aspeed_fsi_writel(s, ASPEED_FSI_OPB1_BUS_SELECT, 0x1);
        curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB1_BUS_SELECT);
        g_assert_cmphex(curval, ==, 0x1);
    } else {
        g_assert_not_reached();
    }
}

static void test_fsi_reg_change(QTestState *s, uint32_t reg, uint32_t newval)
{
    uint32_t base;
    uint32_t curval;

    base = aspeed_fsi_readl(s, reg);
    aspeed_fsi_writel(s, reg, newval);
    curval = aspeed_fsi_readl(s, reg);
    g_assert_cmpuint(curval, ==, newval);
    aspeed_fsi_writel(s, reg, base);
    curval = aspeed_fsi_readl(s, reg);
    g_assert_cmpuint(curval, ==, base);
}

static void test_fsi0_master_regs(const void *data)
{
    QTestState *s = (QTestState *)data;

    test_fsi_setup(s, AST2600_OPB_FSI0_BASE_ADDR);

    test_fsi_reg_change(s, ASPEED_FSI_OPB0_RW_DIRECTION, 0xF3F4F514);
    test_fsi_reg_change(s, ASPEED_FSI_OPB0_XFER_SIZE, 0xF3F4F518);
    test_fsi_reg_change(s, ASPEED_FSI_OPB0_BUS_ADDR, 0xF3F4F51c);
    test_fsi_reg_change(s, ASPEED_FSI_INTRRUPT_CLEAR, 0xF3F4F540);
    test_fsi_reg_change(s, ASPEED_FSI_INTRRUPT_STATUS, 0xF3F4F548);
    test_fsi_reg_change(s, ASPEED_FSI_OPB0_BUS_STATUS, 0xF3F4F580);
    test_fsi_reg_change(s, ASPEED_FSI_OPB0_READ_DATA, 0xF3F4F584);
}

static void test_fsi1_master_regs(const void *data)
{
    QTestState *s = (QTestState *)data;

    test_fsi_setup(s, AST2600_OPB_FSI1_BASE_ADDR);

    test_fsi_reg_change(s, ASPEED_FSI_OPB1_RW_DIRECTION, 0xF3F4F514);
    test_fsi_reg_change(s, ASPEED_FSI_OPB1_XFER_SIZE, 0xF3F4F518);
    test_fsi_reg_change(s, ASPEED_FSI_OPB1_BUS_ADDR, 0xF3F4F51c);
    test_fsi_reg_change(s, ASPEED_FSI_INTRRUPT_CLEAR, 0xF3F4F540);
    test_fsi_reg_change(s, ASPEED_FSI_INTRRUPT_STATUS, 0xF3F4F548);
    test_fsi_reg_change(s, ASPEED_FSI_OPB1_BUS_STATUS, 0xF3F4F580);
    test_fsi_reg_change(s, ASPEED_FSI_OPB1_READ_DATA, 0xF3F4F584);
}

static void test_fsi0_getcfam_addr0(const void *data)
{
    QTestState *s = (QTestState *)data;
    uint32_t curval;

    test_fsi_setup(s, AST2600_OPB_FSI0_BASE_ADDR);

    /* Master access direction read */
    aspeed_fsi_writel(s, ASPEED_FSI_OPB0_RW_DIRECTION, 0x1);
    /* word */
    aspeed_fsi_writel(s, ASPEED_FSI_OPB0_XFER_SIZE, 0x3);
    /* Address */
    aspeed_fsi_writel(s, ASPEED_FSI_OPB0_BUS_ADDR, 0xa0000000);
    aspeed_fsi_writel(s, ASPEED_FSI_INTRRUPT_CLEAR, 0x1);
    aspeed_fsi_writel(s, ASPEED_FSI_ENGINER_TRIGGER, 0x1);

    curval = aspeed_fsi_readl(s, ASPEED_FSI_INTRRUPT_STATUS);
    g_assert_cmphex(curval, ==, 0x10000);
    curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB0_BUS_STATUS);
    g_assert_cmphex(curval, ==, 0x0);
    curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB0_READ_DATA);
    g_assert_cmphex(curval, ==, 0x152d02c0);
}

static void test_fsi1_getcfam_addr0(const void *data)
{
    QTestState *s = (QTestState *)data;
    uint32_t curval;

    test_fsi_setup(s, AST2600_OPB_FSI1_BASE_ADDR);

    /* Master access direction read */
    aspeed_fsi_writel(s, ASPEED_FSI_OPB1_RW_DIRECTION, 0x1);

    aspeed_fsi_writel(s, ASPEED_FSI_OPB1_XFER_SIZE, 0x3);
    aspeed_fsi_writel(s, ASPEED_FSI_OPB1_BUS_ADDR, 0xa0000000);
    aspeed_fsi_writel(s, ASPEED_FSI_INTRRUPT_CLEAR, 0x1);
    aspeed_fsi_writel(s, ASPEED_FSI_ENGINER_TRIGGER, 0x1);

    curval = aspeed_fsi_readl(s, ASPEED_FSI_INTRRUPT_STATUS);
    g_assert_cmphex(curval, ==, 0x20000);
    curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB1_BUS_STATUS);
    g_assert_cmphex(curval, ==, 0x0);
    curval = aspeed_fsi_readl(s, ASPEED_FSI_OPB1_READ_DATA);
    g_assert_cmphex(curval, ==, 0x152d02c0);
}

int main(int argc, char **argv)
{
    int ret = -1;
    QTestState *s;

    g_test_init(&argc, &argv, NULL);

    s = qtest_init("-machine ast2600-evb ");

    /* Tests for OPB/FSI0 */
    qtest_add_data_func("/aspeed-fsi-test/test_fsi0_master_regs", s,
                        test_fsi0_master_regs);

    qtest_add_data_func("/aspeed-fsi-test/test_fsi0_getcfam_addr0", s,
                        test_fsi0_getcfam_addr0);

    /* Tests for OPB/FSI1 */
    qtest_add_data_func("/aspeed-fsi-test/test_fsi1_master_regs", s,
                        test_fsi1_master_regs);

    qtest_add_data_func("/aspeed-fsi-test/test_fsi1_getcfam_addr0", s,
                        test_fsi1_getcfam_addr0);

    ret = g_test_run();
    qtest_quit(s);

    return ret;
}
