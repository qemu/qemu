/*
 * QTests for the Xilinx Zynq SDHCI controller.
 *
 * Copyright (c) 2026 Tao Ding <dingtao0430@163.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/sd/sdhci.h"
#include "hw/sd/sdhci-internal.h"

#include "libqtest.h"
#include "libqtest-single.h"
#include "libqos/sdhci-cmd.h"

#define XILINX_ZYNQ_MMC_BA             0xE0100000
#define XILINX_ZYNQ_BLK_SIZE           512
#define XILINX_ZYNQ_TEST_IMAGE_SIZE    (1 << 20)
#define XILINX_ZYNQ_SDMA_BOUNDARY_ARG  7
#define XILINX_ZYNQ_SDMA_BOUNDARY_SIZE (512 * 1024)
#define XILINX_ZYNQ_SDMA_BLOCK_COUNT \
    ((XILINX_ZYNQ_SDMA_BOUNDARY_SIZE / XILINX_ZYNQ_BLK_SIZE) + 1)
#define XILINX_ZYNQ_SDMA_TRANSFER_SIZE \
    (XILINX_ZYNQ_SDMA_BLOCK_COUNT * XILINX_ZYNQ_BLK_SIZE)
#define XILINX_ZYNQ_DMA_ADDR           0x00100000

#define SDHC_MAKE_BLKSZ(boundary, size) (((boundary) << 12) | (size))

static char *sd_path;

static QTestState *setup_sd_card(void)
{
    QTestState *qts;
    uint16_t rca;

    qts = qtest_initf(
        "-machine xilinx-zynq-a9 "
        "-drive if=sd,index=0,file=%s,format=raw,auto-read-only=off",
        sd_path);

    qtest_writew(qts, XILINX_ZYNQ_MMC_BA + SDHC_SWRST, SDHC_RESET_ALL);
    qtest_writew(qts, XILINX_ZYNQ_MMC_BA + SDHC_CLKCON,
                 SDHC_CLOCK_SDCLK_EN | SDHC_CLOCK_INT_STABLE |
                 SDHC_CLOCK_INT_EN);
    sdhci_cmd_regs(qts, XILINX_ZYNQ_MMC_BA, 0, 0, 0, 0, SDHC_APP_CMD);
    sdhci_cmd_regs(qts, XILINX_ZYNQ_MMC_BA, 0, 0, 0x41200000, 0,
                   (41 << 8));
    sdhci_cmd_regs(qts, XILINX_ZYNQ_MMC_BA, 0, 0, 0, 0, SDHC_ALL_SEND_CID);
    sdhci_cmd_regs(qts, XILINX_ZYNQ_MMC_BA, 0, 0, 0, 0,
                   SDHC_SEND_RELATIVE_ADDR | SDHC_CMD_RESPONSE);
    rca = qtest_readl(qts, XILINX_ZYNQ_MMC_BA + SDHC_RSPREG0) >> 16;
    sdhci_cmd_regs(qts, XILINX_ZYNQ_MMC_BA, 0, 0, rca << 16, 0,
                   SDHC_SELECT_DESELECT_CARD);

    return qts;
}

static void write_pattern_to_image(size_t offset, size_t len)
{
    gchar *buf = g_malloc(len);
    GError *err = NULL;
    gsize bytes_written;
    GIOChannel *chan;

    for (size_t i = 0; i < len; i++) {
        buf[i] = (i * 13 + 7) & 0xff;
    }

    chan = g_io_channel_new_file(sd_path, "w", &err);
    g_assert_no_error(err);
    g_io_channel_set_encoding(chan, NULL, &err);
    g_assert_no_error(err);
    g_io_channel_seek_position(chan, offset, G_SEEK_SET, &err);
    g_assert_no_error(err);
    g_io_channel_write_chars(chan, buf, len, &bytes_written, &err);
    g_assert_no_error(err);
    g_assert_cmpint(bytes_written, ==, len);
    g_io_channel_shutdown(chan, TRUE, &err);
    g_assert_no_error(err);

    g_free(buf);
}

static void wait_for_block_count(QTestState *qts, uint16_t expected)
{
    int i;

    for (i = 0; i < 1000; i++) {
        if (qtest_readw(qts, XILINX_ZYNQ_MMC_BA + SDHC_BLKCNT) == expected) {
            return;
        }
        g_usleep(100);
    }

    g_assert_cmpuint(qtest_readw(qts, XILINX_ZYNQ_MMC_BA + SDHC_BLKCNT),
                     ==, expected);
}

/*
 * SDMA stops at each boundary and requires software to update SYSAD
 * to continue. The bug this test covers leaves DOING_READ/WRITE asserted,
 * so the continuation SYSAD write is ignored and the transfer stalls.
 */
static void test_read_sdma_boundary_continue(void)
{
    QTestState *qts = setup_sd_card();
    uint8_t *actual = g_malloc0(XILINX_ZYNQ_SDMA_TRANSFER_SIZE);
    uint8_t *expected = g_malloc(XILINX_ZYNQ_SDMA_TRANSFER_SIZE);

    write_pattern_to_image(0, XILINX_ZYNQ_SDMA_TRANSFER_SIZE);

    for (size_t i = 0; i < XILINX_ZYNQ_SDMA_TRANSFER_SIZE; i++) {
        expected[i] = (i * 13 + 7) & 0xff;
    }

    qtest_writel(qts, XILINX_ZYNQ_DMA_ADDR, 0xdeadbeef);
    qtest_writel(qts, XILINX_ZYNQ_DMA_ADDR + XILINX_ZYNQ_SDMA_BOUNDARY_SIZE,
                 0xcafef00d);

    qtest_writel(qts, XILINX_ZYNQ_MMC_BA + SDHC_SYSAD, XILINX_ZYNQ_DMA_ADDR);
    sdhci_cmd_regs(qts, XILINX_ZYNQ_MMC_BA,
                   SDHC_MAKE_BLKSZ(XILINX_ZYNQ_SDMA_BOUNDARY_ARG,
                                   XILINX_ZYNQ_BLK_SIZE),
                   XILINX_ZYNQ_SDMA_BLOCK_COUNT, 0,
                   SDHC_TRNS_DMA | SDHC_TRNS_MULTI | SDHC_TRNS_READ |
                   SDHC_TRNS_BLK_CNT_EN,
                   SDHC_READ_MULTIPLE_BLOCK | SDHC_CMD_DATA_PRESENT);

    wait_for_block_count(qts, 1);

    qtest_writew(qts, XILINX_ZYNQ_MMC_BA + SDHC_NORINTSTS, 0xffff);
    qtest_writel(qts, XILINX_ZYNQ_MMC_BA + SDHC_SYSAD,
                 XILINX_ZYNQ_DMA_ADDR + XILINX_ZYNQ_SDMA_BOUNDARY_SIZE);

    wait_for_block_count(qts, 0);

    qtest_memread(qts, XILINX_ZYNQ_DMA_ADDR, actual,
                  XILINX_ZYNQ_SDMA_TRANSFER_SIZE);
    g_assert_cmpmem(actual, XILINX_ZYNQ_SDMA_TRANSFER_SIZE,
                    expected, XILINX_ZYNQ_SDMA_TRANSFER_SIZE);

    g_free(expected);
    g_free(actual);
    qtest_quit(qts);
}

static void drive_destroy(void)
{
    unlink(sd_path);
    g_free(sd_path);
}

static void drive_create(void)
{
    int fd;
    int ret;
    GError *error = NULL;

    fd = g_file_open_tmp("xilinx-zynq-sdhci.XXXXXX", &sd_path, &error);
    if (fd == -1) {
        fprintf(stderr, "unable to create sdhci file: %s\n", error->message);
        g_error_free(error);
    }
    g_assert_nonnull(sd_path);

    ret = ftruncate(fd, XILINX_ZYNQ_TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    close(fd);
}

int main(int argc, char **argv)
{
    int ret;

    drive_create();

    g_test_init(&argc, &argv, NULL);
    qtest_add_func("xilinx-zynq-sdhci/sdma/read-boundary-continue",
                   test_read_sdma_boundary_continue);

    ret = g_test_run();
    drive_destroy();
    return ret;
}
