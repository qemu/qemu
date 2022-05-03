/*
 * MMC Host Controller Commands
 *
 * Copyright (c) 2021 Google LLC
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
#include "sdhci-cmd.h"
#include "../libqtest.h"

static ssize_t read_fifo(QTestState *qts, uint64_t reg, char *msg, size_t count)
{
    uint32_t mask = 0xff;
    size_t index = 0;
    uint32_t msg_frag;
    int size;
    while (index < count) {
        size = count - index;
        if (size > 4) {
            size = 4;
        }
        msg_frag = qtest_readl(qts, reg);
        while (size > 0) {
            msg[index] = msg_frag & mask;
            if (msg[index++] == 0) {
                return index;
            }
            msg_frag >>= 8;
            --size;
        }
    }
    return index;
}

static void write_fifo(QTestState *qts, uint64_t reg, const char *msg,
                       size_t count)
{
    size_t index = 0;
    uint32_t msg_frag;
    int size;
    int frag_i;
    while (index < count) {
        size = count - index;
        if (size > 4) {
            size = 4;
        }
        msg_frag = 0;
        frag_i = 0;
        while (frag_i < size) {
            msg_frag |= ((uint32_t)msg[index++]) << (frag_i * 8);
            ++frag_i;
        }
        qtest_writel(qts, reg, msg_frag);
    }
}

static void fill_block(QTestState *qts, uint64_t reg, int count)
{
    while (--count >= 0) {
        qtest_writel(qts, reg, 0);
    }
}

void sdhci_cmd_regs(QTestState *qts, uint64_t base_addr, uint16_t blksize,
                    uint16_t blkcnt, uint32_t argument, uint16_t trnmod,
                    uint16_t cmdreg)
{
    qtest_writew(qts, base_addr + SDHC_BLKSIZE, blksize);
    qtest_writew(qts, base_addr + SDHC_BLKCNT, blkcnt);
    qtest_writel(qts, base_addr + SDHC_ARGUMENT, argument);
    qtest_writew(qts, base_addr + SDHC_TRNMOD, trnmod);
    qtest_writew(qts, base_addr + SDHC_CMDREG, cmdreg);
}

ssize_t sdhci_read_cmd(QTestState *qts, uint64_t base_addr, char *msg,
                       size_t count)
{
    sdhci_cmd_regs(qts, base_addr, count, 1, 0,
                   SDHC_TRNS_MULTI | SDHC_TRNS_READ | SDHC_TRNS_BLK_CNT_EN,
                   SDHC_READ_MULTIPLE_BLOCK | SDHC_CMD_DATA_PRESENT);

    /* read sd fifo_buffer */
    ssize_t bytes_read = read_fifo(qts, base_addr + SDHC_BDATA, msg, count);

    sdhci_cmd_regs(qts, base_addr, 0, 0, 0,
                   SDHC_TRNS_MULTI | SDHC_TRNS_READ | SDHC_TRNS_BLK_CNT_EN,
                   SDHC_STOP_TRANSMISSION);

    return bytes_read;
}

void sdhci_write_cmd(QTestState *qts, uint64_t base_addr, const char *msg,
                     size_t count, size_t blksize)
{
    sdhci_cmd_regs(qts, base_addr, blksize, 1, 0,
                   SDHC_TRNS_MULTI | SDHC_TRNS_WRITE | SDHC_TRNS_BLK_CNT_EN,
                   SDHC_WRITE_MULTIPLE_BLOCK | SDHC_CMD_DATA_PRESENT);

    /* write to sd fifo_buffer */
    write_fifo(qts, base_addr + SDHC_BDATA, msg, count);
    fill_block(qts, base_addr + SDHC_BDATA, (blksize - count) / 4);

    sdhci_cmd_regs(qts, base_addr, 0, 0, 0,
                   SDHC_TRNS_MULTI | SDHC_TRNS_WRITE | SDHC_TRNS_BLK_CNT_EN,
                   SDHC_STOP_TRANSMISSION);
}
