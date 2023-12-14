/*
 * QTest testcase for BCM283x DMA engine (on Raspberry Pi 3)
 * and its interrupts coming to Interrupt Controller.
 *
 * Copyright (c) 2022 Auriga LLC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "libqtest-single.h"

/* Offsets in raspi3b platform: */
#define RASPI3_DMA_BASE 0x3f007000
#define RASPI3_IC_BASE  0x3f00b200

/* Used register/fields definitions */

/* DMA engine registers: */
#define BCM2708_DMA_CS         0
#define BCM2708_DMA_ACTIVE     (1 << 0)
#define BCM2708_DMA_INT        (1 << 2)

#define BCM2708_DMA_ADDR       0x04

#define BCM2708_DMA_INT_STATUS 0xfe0

/* DMA Transfer Info fields: */
#define BCM2708_DMA_INT_EN     (1 << 0)
#define BCM2708_DMA_D_INC      (1 << 4)
#define BCM2708_DMA_S_INC      (1 << 8)

/* Interrupt controller registers: */
#define IRQ_PENDING_BASIC      0x00
#define IRQ_GPU_PENDING1_AGGR  (1 << 8)
#define IRQ_PENDING_1          0x04
#define IRQ_ENABLE_1           0x10

/* Data for the test: */
#define SCB_ADDR   256
#define S_ADDR     32
#define D_ADDR     64
#define TXFR_LEN   32
const uint32_t check_data = 0x12345678;

static void bcm2835_dma_test_interrupt(int dma_c, int irq_line)
{
    uint64_t dma_base = RASPI3_DMA_BASE + dma_c * 0x100;
    int gpu_irq_line = 16 + irq_line;

    /* Check that interrupts are silent by default: */
    writel(RASPI3_IC_BASE + IRQ_ENABLE_1, 1 << gpu_irq_line);
    int isr = readl(dma_base + BCM2708_DMA_INT_STATUS);
    g_assert_cmpint(isr, ==, 0);
    uint32_t reg0 = readl(dma_base + BCM2708_DMA_CS);
    g_assert_cmpint(reg0, ==, 0);
    uint32_t ic_pending = readl(RASPI3_IC_BASE + IRQ_PENDING_BASIC);
    g_assert_cmpint(ic_pending, ==, 0);
    uint32_t gpu_pending1 = readl(RASPI3_IC_BASE + IRQ_PENDING_1);
    g_assert_cmpint(gpu_pending1, ==, 0);

    /* Prepare Control Block: */
    writel(SCB_ADDR + 0, BCM2708_DMA_S_INC | BCM2708_DMA_D_INC |
                         BCM2708_DMA_INT_EN); /* transfer info */
    writel(SCB_ADDR + 4, S_ADDR);             /* source address */
    writel(SCB_ADDR + 8, D_ADDR);             /* destination address */
    writel(SCB_ADDR + 12, TXFR_LEN);          /* transfer length */
    writel(dma_base + BCM2708_DMA_ADDR, SCB_ADDR);

    writel(S_ADDR, check_data);
    for (int word = S_ADDR + 4; word < S_ADDR + TXFR_LEN; word += 4) {
        writel(word, ~check_data);
    }
    /* Perform the transfer: */
    writel(dma_base + BCM2708_DMA_CS, BCM2708_DMA_ACTIVE);

    /* Check that destination == source: */
    uint32_t data = readl(D_ADDR);
    g_assert_cmpint(data, ==, check_data);
    for (int word = D_ADDR + 4; word < D_ADDR + TXFR_LEN; word += 4) {
        data = readl(word);
        g_assert_cmpint(data, ==, ~check_data);
    }

    /* Check that interrupt status is set both in DMA and IC controllers: */
    isr = readl(RASPI3_DMA_BASE + BCM2708_DMA_INT_STATUS);
    g_assert_cmpint(isr, ==, 1 << dma_c);

    ic_pending = readl(RASPI3_IC_BASE + IRQ_PENDING_BASIC);
    g_assert_cmpint(ic_pending, ==, IRQ_GPU_PENDING1_AGGR);

    gpu_pending1 = readl(RASPI3_IC_BASE + IRQ_PENDING_1);
    g_assert_cmpint(gpu_pending1, ==, 1 << gpu_irq_line);

    /* Clean up, clear interrupt: */
    writel(dma_base + BCM2708_DMA_CS, BCM2708_DMA_INT);
}

static void bcm2835_dma_test_interrupts(void)
{
    /* DMA engines 0--10 have separate IRQ lines, 11--14 - only one: */
    bcm2835_dma_test_interrupt(0,  0);
    bcm2835_dma_test_interrupt(10, 10);
    bcm2835_dma_test_interrupt(11, 11);
    bcm2835_dma_test_interrupt(14, 11);
}

int main(int argc, char **argv)
{
    int ret;
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/bcm2835/dma/test_interrupts",
                   bcm2835_dma_test_interrupts);
    qtest_start("-machine raspi3b");
    ret = g_test_run();
    qtest_end();
    return ret;
}
