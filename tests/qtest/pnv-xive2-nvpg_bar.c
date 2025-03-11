/*
 * QTest testcase for PowerNV 10 interrupt controller (xive2)
 *  - Test NVPG BAR MMIO operations
 *
 * Copyright (c) 2024, IBM Corporation.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include "qemu/osdep.h"
#include "libqtest.h"

#include "pnv-xive2-common.h"

#define NVPG_BACKLOG_OP_SHIFT   10
#define NVPG_BACKLOG_PRIO_SHIFT 4

#define XIVE_PRIORITY_MAX       7

enum NVx {
    NVP,
    NVG,
    NVC
};

typedef enum {
    INCR_STORE = 0b100,
    INCR_LOAD  = 0b000,
    DECR_STORE = 0b101,
    DECR_LOAD  = 0b001,
    READ_x     = 0b010,
    READ_y     = 0b011,
} backlog_op;

static uint32_t nvpg_backlog_op(QTestState *qts, backlog_op op,
                                enum NVx type, uint64_t index,
                                uint8_t priority, uint8_t delta)
{
    uint64_t addr, offset;
    uint32_t count = 0;

    switch (type) {
    case NVP:
        addr = XIVE_NVPG_ADDR + (index << (XIVE_PAGE_SHIFT + 1));
        break;
    case NVG:
        addr = XIVE_NVPG_ADDR + (index << (XIVE_PAGE_SHIFT + 1)) +
            (1 << XIVE_PAGE_SHIFT);
        break;
    case NVC:
        addr = XIVE_NVC_ADDR + (index << XIVE_PAGE_SHIFT);
        break;
    default:
        g_assert_not_reached();
    }

    offset = (op & 0b11) << NVPG_BACKLOG_OP_SHIFT;
    offset |= priority << NVPG_BACKLOG_PRIO_SHIFT;
    if (op >> 2) {
        qtest_writeb(qts, addr + offset, delta);
    } else {
        count = qtest_readw(qts, addr + offset);
    }
    return count;
}

void test_nvpg_bar(QTestState *qts)
{
    uint32_t nvp_target = 0x11;
    uint32_t group_target = 0x17; /* size 16 */
    uint32_t vp_irq = 33, group_irq = 47;
    uint32_t vp_end = 3, group_end = 97;
    uint32_t vp_irq_data = 0x33333333;
    uint32_t group_irq_data = 0x66666666;
    uint8_t vp_priority = 0, group_priority = 5;
    uint32_t vp_count[XIVE_PRIORITY_MAX + 1] = { 0 };
    uint32_t group_count[XIVE_PRIORITY_MAX + 1] = { 0 };
    uint32_t count, delta;
    uint8_t i;

    g_test_message("=========================================================");
    g_test_message("Testing NVPG BAR operations");

    set_nvg(qts, group_target, 0);
    set_nvp(qts, nvp_target, 0x04);
    set_nvp(qts, group_target, 0x04);

    /*
     * Setup: trigger a VP-specific interrupt and a group interrupt
     * so that the backlog counters are initialized to something else
     * than 0 for at least one priority level
     */
    set_eas(qts, vp_irq, vp_end, vp_irq_data);
    set_end(qts, vp_end, nvp_target, vp_priority, false /* group */);

    set_eas(qts, group_irq, group_end, group_irq_data);
    set_end(qts, group_end, group_target, group_priority, true /* group */);

    get_esb(qts, vp_irq, XIVE_EOI_PAGE, XIVE_ESB_SET_PQ_00);
    set_esb(qts, vp_irq, XIVE_TRIGGER_PAGE, 0, 0);
    vp_count[vp_priority]++;

    get_esb(qts, group_irq, XIVE_EOI_PAGE, XIVE_ESB_SET_PQ_00);
    set_esb(qts, group_irq, XIVE_TRIGGER_PAGE, 0, 0);
    group_count[group_priority]++;

    /* check the initial counters */
    for (i = 0; i <= XIVE_PRIORITY_MAX; i++) {
        count = nvpg_backlog_op(qts, READ_x, NVP, nvp_target, i, 0);
        g_assert_cmpuint(count, ==, vp_count[i]);

        count = nvpg_backlog_op(qts, READ_y, NVG, group_target, i, 0);
        g_assert_cmpuint(count, ==, group_count[i]);
    }

    /* do a few ops on the VP. Counter can only be 0 and 1 */
    vp_priority = 2;
    delta = 7;
    nvpg_backlog_op(qts, INCR_STORE, NVP, nvp_target, vp_priority, delta);
    vp_count[vp_priority] = 1;
    count = nvpg_backlog_op(qts, INCR_LOAD, NVP, nvp_target, vp_priority, 0);
    g_assert_cmpuint(count, ==, vp_count[vp_priority]);
    count = nvpg_backlog_op(qts, READ_y, NVP, nvp_target, vp_priority, 0);
    g_assert_cmpuint(count, ==, vp_count[vp_priority]);

    count = nvpg_backlog_op(qts, DECR_LOAD, NVP, nvp_target, vp_priority, 0);
    g_assert_cmpuint(count, ==, vp_count[vp_priority]);
    vp_count[vp_priority] = 0;
    nvpg_backlog_op(qts, DECR_STORE, NVP, nvp_target, vp_priority, delta);
    count = nvpg_backlog_op(qts, READ_x, NVP, nvp_target, vp_priority, 0);
    g_assert_cmpuint(count, ==, vp_count[vp_priority]);

    /* do a few ops on the group */
    group_priority = 2;
    delta = 9;
    /* can't go negative */
    nvpg_backlog_op(qts, DECR_STORE, NVG, group_target, group_priority, delta);
    count = nvpg_backlog_op(qts, READ_y, NVG, group_target, group_priority, 0);
    g_assert_cmpuint(count, ==, 0);
    nvpg_backlog_op(qts, INCR_STORE, NVG, group_target, group_priority, delta);
    group_count[group_priority] += delta;
    count = nvpg_backlog_op(qts, INCR_LOAD, NVG, group_target,
                            group_priority, delta);
    g_assert_cmpuint(count, ==, group_count[group_priority]);
    group_count[group_priority]++;

    count = nvpg_backlog_op(qts, DECR_LOAD, NVG, group_target,
                            group_priority, delta);
    g_assert_cmpuint(count, ==,  group_count[group_priority]);
    group_count[group_priority]--;
    count = nvpg_backlog_op(qts, READ_x, NVG, group_target, group_priority, 0);
    g_assert_cmpuint(count, ==, group_count[group_priority]);
}
