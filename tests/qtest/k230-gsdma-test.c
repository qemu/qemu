/*
 * QTest testcase for K230 GSDMA
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/bitops.h"
#include "exec/hwaddr.h"
#include "hw/dma/k230_gsdma.h"
#include "libqtest.h"

#define K230_GSDMA_BASE              0x80800000
#define TEST_LLT_ADDR0               0x10000
#define TEST_LLT_ADDR1               0x10040
#define TEST_LLT_ADDR2               0x10080

#define TEST_SRC_ADDR0               0x20000
#define TEST_SRC_ADDR1               0x22000
#define TEST_SRC_ADDR2               0x23000
#define TEST_DST_ADDR0               0x30000
#define TEST_DST_ADDR1               0x32000
#define TEST_DST_ADDR2               0x33000

static inline uint64_t gsdma_reg(hwaddr off)
{
    return K230_GSDMA_BASE + off;
}

static inline uint64_t gsdma_ch_reg(unsigned int ch, hwaddr off)
{
    return K230_GSDMA_BASE + K230_GSDMA_CH_BASE +
           ch * K230_GSDMA_CH_STRIDE + off;
}

static void write_sdma_llt_node_full(QTestState *qts, hwaddr addr,
                                     uint32_t cfg, hwaddr src,
                                     uint32_t size, hwaddr dst,
                                     hwaddr next)
{
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, cfg), cfg);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, src_addr), src);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, line_size), size);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, line_cfg), 0x1);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, dst_addr), dst);
    qtest_writel(qts, addr + offsetof(K230GSDMALLT, next_llt_addr), next);
}

static void write_sdma_llt_node(QTestState *qts, hwaddr addr, uint32_t cfg,
                                uint32_t next)
{
    write_sdma_llt_node_full(qts, addr, cfg, 0x11110000, 0x200,
                             0x22220000, next);
}

static void fill_pattern(uint8_t *buf, size_t size, uint8_t seed)
{
    for (size_t i = 0; i < size; i++) {
        buf[i] = seed + i * 37;
    }
}

static void test_global_registers(void)
{
    QTestState *qts = qtest_init("-machine k230");

    g_assert_cmphex(qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_CFG)), ==,
                    K230_GSDMA_DMA_CFG_RESET);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_CH_EN), 0xff);
    g_assert_cmphex(qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_CH_EN)), ==,
                    K230_GSDMA_DMA_CH_EN_MASK);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_INT_MASK), 0x12345);
    g_assert_cmphex(qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_INT_MASK)), ==,
                    0x12345);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_CFG), 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_CFG)), ==,
                    0xa5a5a5a5);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_WEIGHT), 0xff55aa55);
    g_assert_cmphex(qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_WEIGHT)), ==,
                    0x55aa55);

    qtest_quit(qts);
}

static void test_sdma_pause_resume(void)
{
    uint32_t int_stat;
    QTestState *qts = qtest_init("-machine k230");

    write_sdma_llt_node(qts, TEST_LLT_ADDR0, K230_GSDMA_LLT_PAUSE,
                        TEST_LLT_ADDR1);
    write_sdma_llt_node(qts, TEST_LLT_ADDR1, K230_GSDMA_LLT_NODE_INTR, 0);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_CH_EN), BIT(0));
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_LLT_SADDR), TEST_LLT_ADDR0);
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CTL), K230_GSDMA_CTL_START);

    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_STATUS)), ==,
                    K230_GSDMA_SDMA_STATUS_PAUSE);
    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CURRENT_LLT)),
                    ==, TEST_LLT_ADDR0);

    int_stat = qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_INT_STAT));
    g_assert_cmphex(int_stat & K230_GSDMA_SDMA_PAUSE_INT(0), ==,
                    K230_GSDMA_SDMA_PAUSE_INT(0));
    g_assert_cmphex(int_stat & K230_GSDMA_SDMA_DONE_INT(0), ==, 0);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_INT_STAT), int_stat);
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CTL), K230_GSDMA_CTL_RESUME);

    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_STATUS)), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CURRENT_LLT)),
                    ==, TEST_LLT_ADDR1);

    int_stat = qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_INT_STAT));
    g_assert_cmphex(int_stat & K230_GSDMA_SDMA_DONE_INT(0), ==,
                    K230_GSDMA_SDMA_DONE_INT(0));
    g_assert_cmphex(int_stat & K230_GSDMA_SDMA_ITEM_INT(0), ==,
                    K230_GSDMA_SDMA_ITEM_INT(0));

    qtest_quit(qts);
}

static void test_sdma_three_llt_copy(void)
{
    uint8_t src0[0x1000];
    uint8_t src1[1];
    uint8_t src2[511];
    uint8_t dst0[sizeof(src0)];
    uint8_t dst1[sizeof(src1)];
    uint8_t dst2[sizeof(src2)];
    uint32_t int_stat;
    QTestState *qts = qtest_init("-machine k230");

    fill_pattern(src0, sizeof(src0), 0x10);
    fill_pattern(src1, sizeof(src1), 0x31);
    fill_pattern(src2, sizeof(src2), 0x52);
    memset(dst0, 0xa5, sizeof(dst0));
    memset(dst1, 0xa5, sizeof(dst1));
    memset(dst2, 0xa5, sizeof(dst2));

    qtest_memwrite(qts, TEST_SRC_ADDR0, src0, sizeof(src0));
    qtest_memwrite(qts, TEST_SRC_ADDR1, src1, sizeof(src1));
    qtest_memwrite(qts, TEST_SRC_ADDR2, src2, sizeof(src2));
    qtest_memwrite(qts, TEST_DST_ADDR0, dst0, sizeof(dst0));
    qtest_memwrite(qts, TEST_DST_ADDR1, dst1, sizeof(dst1));
    qtest_memwrite(qts, TEST_DST_ADDR2, dst2, sizeof(dst2));

    write_sdma_llt_node_full(qts, TEST_LLT_ADDR0, 0, TEST_SRC_ADDR0,
                             sizeof(src0), TEST_DST_ADDR0, TEST_LLT_ADDR1);
    write_sdma_llt_node_full(qts, TEST_LLT_ADDR1, 0, TEST_SRC_ADDR1,
                             sizeof(src1), TEST_DST_ADDR1, TEST_LLT_ADDR2);
    write_sdma_llt_node_full(qts, TEST_LLT_ADDR2, 0, TEST_SRC_ADDR2,
                             sizeof(src2), TEST_DST_ADDR2, 0);

    qtest_writel(qts, gsdma_reg(K230_GSDMA_DMA_CH_EN), BIT(0));
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_LLT_SADDR), TEST_LLT_ADDR0);
    qtest_writel(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CTL), K230_GSDMA_CTL_START);

    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_STATUS)), ==,
                    0);
    g_assert_cmphex(qtest_readl(qts, gsdma_ch_reg(0, K230_GSDMA_CH_CURRENT_LLT)),
                    ==, TEST_LLT_ADDR2);

    int_stat = qtest_readl(qts, gsdma_reg(K230_GSDMA_DMA_INT_STAT));
    g_assert_cmphex(int_stat & K230_GSDMA_SDMA_DONE_INT(0), ==,
                    K230_GSDMA_SDMA_DONE_INT(0));
    g_assert_cmphex(int_stat & K230_GSDMA_SDMA_PAUSE_INT(0), ==, 0);

    memset(dst0, 0, sizeof(dst0));
    memset(dst1, 0, sizeof(dst1));
    memset(dst2, 0, sizeof(dst2));
    qtest_memread(qts, TEST_DST_ADDR0, dst0, sizeof(dst0));
    qtest_memread(qts, TEST_DST_ADDR1, dst1, sizeof(dst1));
    qtest_memread(qts, TEST_DST_ADDR2, dst2, sizeof(dst2));

    g_assert_cmpmem(dst0, sizeof(dst0), src0, sizeof(src0));
    g_assert_cmpmem(dst1, sizeof(dst1), src1, sizeof(src1));
    g_assert_cmpmem(dst2, sizeof(dst2), src2, sizeof(src2));

    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/k230-gsdma/global-registers", test_global_registers);
    qtest_add_func("/k230-gsdma/sdma-pause-resume", test_sdma_pause_resume);
    qtest_add_func("/k230-gsdma/sdma-three-llt-copy",
                   test_sdma_three_llt_copy);

    return g_test_run();
}
