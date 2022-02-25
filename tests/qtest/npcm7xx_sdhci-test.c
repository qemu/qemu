/*
 * QTests for NPCM7xx SD-3.0 / MMC-4.51 Host Controller
 *
 * Copyright (c) 2022 Google LLC
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include "qemu/osdep.h"
#include "hw/sd/npcm7xx_sdhci.h"

#include "libqos/libqtest.h"
#include "libqtest-single.h"
#include "libqos/sdhci-cmd.h"

#define NPCM7XX_REG_SIZE 0x100
#define NPCM7XX_MMC_BA 0xF0842000
#define NPCM7XX_BLK_SIZE 512
#define NPCM7XX_TEST_IMAGE_SIZE (1 << 30)

char *sd_path;

static QTestState *setup_sd_card(void)
{
    QTestState *qts = qtest_initf(
        "-machine kudo-bmc "
        "-device sd-card,drive=drive0 "
        "-drive id=drive0,if=none,file=%s,format=raw,auto-read-only=off",
        sd_path);

    qtest_writew(qts, NPCM7XX_MMC_BA + SDHC_SWRST, SDHC_RESET_ALL);
    qtest_writew(qts, NPCM7XX_MMC_BA + SDHC_CLKCON,
                 SDHC_CLOCK_SDCLK_EN | SDHC_CLOCK_INT_STABLE |
                     SDHC_CLOCK_INT_EN);
    sdhci_cmd_regs(qts, NPCM7XX_MMC_BA, 0, 0, 0, 0, SDHC_APP_CMD);
    sdhci_cmd_regs(qts, NPCM7XX_MMC_BA, 0, 0, 0x41200000, 0, (41 << 8));
    sdhci_cmd_regs(qts, NPCM7XX_MMC_BA, 0, 0, 0, 0, SDHC_ALL_SEND_CID);
    sdhci_cmd_regs(qts, NPCM7XX_MMC_BA, 0, 0, 0, 0, SDHC_SEND_RELATIVE_ADDR);
    sdhci_cmd_regs(qts, NPCM7XX_MMC_BA, 0, 0, 0x45670000, 0,
                   SDHC_SELECT_DESELECT_CARD);

    return qts;
}

static void write_sdread(QTestState *qts, const char *msg)
{
    int fd, ret;
    size_t len = strlen(msg);
    char *rmsg = g_malloc(len);

    /* write message to sd */
    fd = open(sd_path, O_WRONLY);
    g_assert(fd >= 0);
    ret = write(fd, msg, len);
    close(fd);
    g_assert(ret == len);

    /* read message using sdhci */
    ret = sdhci_read_cmd(qts, NPCM7XX_MMC_BA, rmsg, len);
    g_assert(ret == len);
    g_assert(!memcmp(rmsg, msg, len));

    g_free(rmsg);
}

/* Check MMC can read values from sd */
static void test_read_sd(void)
{
    QTestState *qts = setup_sd_card();

    write_sdread(qts, "hello world");
    write_sdread(qts, "goodbye");

    qtest_quit(qts);
}

static void sdwrite_read(QTestState *qts, const char *msg)
{
    int fd, ret;
    size_t len = strlen(msg);
    char *rmsg = g_malloc(len);

    /* write message using sdhci */
    sdhci_write_cmd(qts, NPCM7XX_MMC_BA, msg, len, NPCM7XX_BLK_SIZE);

    /* read message from sd */
    fd = open(sd_path, O_RDONLY);
    g_assert(fd >= 0);
    ret = read(fd, rmsg, len);
    close(fd);
    g_assert(ret == len);

    g_assert(!memcmp(rmsg, msg, len));

    g_free(rmsg);
}

/* Check MMC can write values to sd */
static void test_write_sd(void)
{
    QTestState *qts = setup_sd_card();

    sdwrite_read(qts, "hello world");
    sdwrite_read(qts, "goodbye");

    qtest_quit(qts);
}

/* Check SDHCI has correct default values. */
static void test_reset(void)
{
    QTestState *qts = qtest_init("-machine kudo-bmc");
    uint64_t addr = NPCM7XX_MMC_BA;
    uint64_t end_addr = addr + NPCM7XX_REG_SIZE;
    uint16_t prstvals_resets[] = {NPCM7XX_PRSTVALS_0_RESET,
                                  NPCM7XX_PRSTVALS_1_RESET,
                                  0,
                                  NPCM7XX_PRSTVALS_3_RESET,
                                  0,
                                  0};
    int i;
    uint32_t mask;

    while (addr < end_addr) {
        switch (addr - NPCM7XX_MMC_BA) {
        case SDHC_PRNSTS:
            /*
             * ignores bits 20 to 24: they are changed when reading registers
             */
            mask = 0x1f00000;
            g_assert_cmphex(qtest_readl(qts, addr) | mask, ==,
                            NPCM7XX_PRSNTS_RESET | mask);
            addr += 4;
            break;
        case SDHC_BLKGAP:
            g_assert_cmphex(qtest_readb(qts, addr), ==, NPCM7XX_BLKGAP_RESET);
            addr += 1;
            break;
        case SDHC_CAPAB:
            g_assert_cmphex(qtest_readq(qts, addr), ==, NPCM7XX_CAPAB_RESET);
            addr += 8;
            break;
        case SDHC_MAXCURR:
            g_assert_cmphex(qtest_readq(qts, addr), ==, NPCM7XX_MAXCURR_RESET);
            addr += 8;
            break;
        case SDHC_HCVER:
            g_assert_cmphex(qtest_readw(qts, addr), ==, NPCM7XX_HCVER_RESET);
            addr += 2;
            break;
        case NPCM7XX_PRSTVALS:
            for (i = 0; i < NPCM7XX_PRSTVALS_SIZE; ++i) {
                g_assert_cmphex(qtest_readw(qts, addr + 2 * i), ==,
                                prstvals_resets[i]);
            }
            addr += NPCM7XX_PRSTVALS_SIZE * 2;
            break;
        default:
            g_assert_cmphex(qtest_readb(qts, addr), ==, 0);
            addr += 1;
        }
    }

    qtest_quit(qts);
}

static void drive_destroy(void)
{
    unlink(sd_path);
    g_free(sd_path);
}

static void drive_create(void)
{
    int fd, ret;
    GError *error = NULL;

    /* Create a temporary raw image */
    fd = g_file_open_tmp("sdhci_XXXXXX", &sd_path, &error);
    if (fd == -1) {
        fprintf(stderr, "unable to create sdhci file: %s\n", error->message);
        g_error_free(error);
    }
    g_assert(sd_path != NULL);

    ret = ftruncate(fd, NPCM7XX_TEST_IMAGE_SIZE);
    g_assert_cmpint(ret, ==, 0);
    g_message("%s", sd_path);
    close(fd);
}

int main(int argc, char **argv)
{
    int ret;

    drive_create();

    g_test_init(&argc, &argv, NULL);

    qtest_add_func("npcm7xx_sdhci/reset", test_reset);
    qtest_add_func("npcm7xx_sdhci/write_sd", test_write_sd);
    qtest_add_func("npcm7xx_sdhci/read_sd", test_read_sd);

    ret = g_test_run();
    drive_destroy();
    return ret;
}
