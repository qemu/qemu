/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * QTest testcase for the L2VIC Interrupt Controller
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"
#include "hw/hexagon/hexagon.h"

#include "hw/hexagon/machine_cfg_v66g_1024.h.inc"
#include "hw/hexagon/machine_cfg_v68n_1024.h.inc"

/* L2VIC register offsets exercised by this test */
#define L2VIC_INT_ENABLEn 0x100 /* Read/Write */
#define L2VIC_INT_ENABLE_CLEARn 0x180 /* Write */
#define L2VIC_INT_ENABLE_SETn 0x200 /* Write */
#define L2VIC_INT_TYPEn 0x280 /* Read/Write */
#define L2VIC_INT_STATUSn 0x380 /* Read */
#define L2VIC_INT_CLEARn 0x400 /* Write */
#define L2VIC_SOFT_INTn 0x480 /* Write */
#define L2VIC_INT_PENDINGn 0x500 /* Read */
#define L2VIC_INT_GRPn_0 0x600 /* Read/Write */
#define L2VIC_INT_GRPn_1 0x680 /* Read/Write */
#define L2VIC_INT_GRPn_2 0x700 /* Read/Write */
#define L2VIC_INT_GRPn_3 0x780 /* Read/Write */

/*
 * VID group readback: records which irq last fired through each VID
 * group.  Outputs themselves are momentary pulses (see l2vic_update()),
 * so these registers -- not the qtest IRQ level snapshot -- are how the
 * test observes VID steering.
 */
#define L2VIC_VID_GRP_0 0x0
#define L2VIC_VID_GRP_1 0x4
#define L2VIC_VID_GRP_2 0x8
#define L2VIC_VID_GRP_3 0xC

typedef struct {
    const char *machine;
    const struct hexagon_machine_config *cfg;
} L2VICMachineCfg;

static const L2VICMachineCfg l2vic_machines[] = {
    { "virt",      &v68n_1024 },
    { "V66G_1024", &v66g_1024 },
};

static uint32_t l2vic_read32(uint64_t base, uint32_t offset)
{
    return readl(base + offset);
}

static void l2vic_write32(uint64_t base, uint32_t offset, uint32_t value)
{
    writel(base + offset, value);
}

static void test_l2vic_register_access(uint64_t base)
{
    uint32_t val;

    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x1);

    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);
}

static void test_l2vic_interrupt_enable(uint64_t base)
{
    uint32_t val;

    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val, ==, 0);

    /* Enable IRQ 0 and 2 */
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x5);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x5, ==, 0x5);

    /* Disable IRQ 0, leaving IRQ 2 enabled */
    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_ENABLEn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);
    g_assert_cmpuint(val & 0x4, ==, 0x4);
}

static void test_l2vic_basic_functionality(uint64_t base)
{
    l2vic_read32(base, L2VIC_INT_ENABLEn);
    l2vic_read32(base, L2VIC_INT_PENDINGn);
    l2vic_read32(base, L2VIC_INT_STATUSn);
    l2vic_read32(base, L2VIC_INT_TYPEn);

    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0);
    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0);
}

/*
 * IRQs 0-7 pack their group-enable/VID-select nibbles into
 * L2VIC_INT_GRPn_0 (int_group_n[0]), 4 bits per irq: bit 3 enables
 * VID steering, bits 0-2 select the VID group (0-3), which pulses
 * output line vid+2.
 */
static void l2vic_set_vid_group(uint64_t base, int irq, int vid)
{
    uint32_t val = l2vic_read32(base, L2VIC_INT_GRPn_0);
    uint32_t nibble = 0x8 | (vid & 0x7);

    val &= ~(0xFu << (irq * 4));
    val |= nibble << (irq * 4);
    l2vic_write32(base, L2VIC_INT_GRPn_0, val);
}

static void test_l2vic_irq_outputs(uint64_t base)
{
    uint32_t val;

    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_TYPEn, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_0, 0);

    /* Group 0 / IRQ2: soft interrupts require edge-triggered config */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x1);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x1);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x1);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x1, ==, 0x1);
    /* Default VID group (0) records the delivering irq, output line 2 */
    g_assert_cmpuint(l2vic_read32(base, L2VIC_VID_GRP_0), ==, 0);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x1);
    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x1, ==, 0x0);

    /* IRQ1 steered to VID group 1 -> output line 3 */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x2);
    l2vic_set_vid_group(base, 1, 1);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x2);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x2);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x2, ==, 0x2);
    g_assert_cmpuint(l2vic_read32(base, L2VIC_VID_GRP_1), ==, 1);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x2);

    /* IRQ4 steered to VID group 2 -> output line 4 */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x10);
    l2vic_set_vid_group(base, 4, 2);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x10);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x10);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x10, ==, 0x10);
    g_assert_cmpuint(l2vic_read32(base, L2VIC_VID_GRP_2), ==, 4);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x10);

    /* IRQ5 steered to VID group 3 -> output line 5 */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x20);
    l2vic_set_vid_group(base, 5, 3);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x20);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x20);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x20, ==, 0x20);
    g_assert_cmpuint(l2vic_read32(base, L2VIC_VID_GRP_3), ==, 5);

    l2vic_write32(base, L2VIC_INT_CLEARn, 0x20);

    /* Restore defaults; the block below reuses IRQ 3-5 without VID steering */
    l2vic_write32(base, L2VIC_INT_GRPn_0, 0);
    l2vic_write32(base, L2VIC_INT_TYPEn, 0);

    /* Multiple pending: at most one active at a time */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0xF);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0xF);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0xF);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0xF, !=, 0x0);

    /*
     * Only one irq becomes active per delivery; each clear unblocks the
     * next pending one, so drain until all four have been delivered.
     */
    while ((val = l2vic_read32(base, L2VIC_INT_STATUSn)) & 0xF) {
        l2vic_write32(base, L2VIC_INT_CLEARn, val & 0xF);
    }

    /* Level-triggered sources ignore soft interrupts */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x0);
    l2vic_write32(base, L2VIC_INT_ENABLE_SETn, 0x20);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x20);

    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x20, ==, 0x0);

    /* Same source, now edge-triggered, does fire */
    l2vic_write32(base, L2VIC_INT_TYPEn, 0x20);
    l2vic_write32(base, L2VIC_SOFT_INTn, 0x20);
    val = l2vic_read32(base, L2VIC_INT_STATUSn);
    g_assert_cmpuint(val & 0x20, ==, 0x20);

    l2vic_write32(base, L2VIC_INT_ENABLE_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_CLEARn, 0xFFFFFFFF);
    l2vic_write32(base, L2VIC_INT_GRPn_0, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_1, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_2, 0);
    l2vic_write32(base, L2VIC_INT_GRPn_3, 0);
}

static void test_l2vic_on_machine(gconstpointer data)
{
    const L2VICMachineCfg *mc = data;
    g_autofree char *args = g_strdup_printf("-machine %s", mc->machine);
    uint64_t base = mc->cfg->l2vic_base;

    qtest_start(args);

    test_l2vic_register_access(base);
    test_l2vic_interrupt_enable(base);
    test_l2vic_basic_functionality(base);
    test_l2vic_irq_outputs(base);

    qtest_end();
}

int main(int argc, char **argv)
{
    size_t i;

    g_test_init(&argc, &argv, NULL);

    for (i = 0; i < ARRAY_SIZE(l2vic_machines); i++) {
        g_autofree char *path = g_strdup_printf("/l2vic/%s/all-tests",
                                                l2vic_machines[i].machine);
        qtest_add_data_func(path, &l2vic_machines[i], test_l2vic_on_machine);
    }

    return g_test_run();
}
