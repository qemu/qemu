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

#include "libqtest.h"

/* more details at hw/sd/sdhci-internal.h */
#define SDHC_BLKSIZE 0x04
#define SDHC_BLKCNT 0x06
#define SDHC_ARGUMENT 0x08
#define SDHC_TRNMOD 0x0C
#define SDHC_CMDREG 0x0E
#define SDHC_BDATA 0x20
#define SDHC_PRNSTS 0x24
#define SDHC_BLKGAP 0x2A
#define SDHC_CLKCON 0x2C
#define SDHC_SWRST 0x2F
#define SDHC_CAPAB 0x40
#define SDHC_MAXCURR 0x48
#define SDHC_HCVER 0xFE

/* TRNSMOD Reg */
#define SDHC_TRNS_BLK_CNT_EN 0x0002
#define SDHC_TRNS_READ 0x0010
#define SDHC_TRNS_WRITE 0x0000
#define SDHC_TRNS_MULTI 0x0020

/* CMD Reg */
#define SDHC_CMD_DATA_PRESENT (1 << 5)
#define SDHC_ALL_SEND_CID (2 << 8)
#define SDHC_SEND_RELATIVE_ADDR (3 << 8)
#define SDHC_SELECT_DESELECT_CARD (7 << 8)
#define SDHC_SEND_CSD (9 << 8)
#define SDHC_STOP_TRANSMISSION (12 << 8)
#define SDHC_READ_MULTIPLE_BLOCK (18 << 8)
#define SDHC_WRITE_MULTIPLE_BLOCK (25 << 8)
#define SDHC_APP_CMD (55 << 8)

/* SWRST Reg */
#define SDHC_RESET_ALL 0x01

/* CLKCTRL Reg */
#define SDHC_CLOCK_INT_EN 0x0001
#define SDHC_CLOCK_INT_STABLE 0x0002
#define SDHC_CLOCK_SDCLK_EN (1 << 2)

/* Set registers needed to send commands to SD */
void sdhci_cmd_regs(QTestState *qts, uint64_t base_addr, uint16_t blksize,
                    uint16_t blkcnt, uint32_t argument, uint16_t trnmod,
                    uint16_t cmdreg);

/* Read at most 1 block of SD using non-DMA  */
ssize_t sdhci_read_cmd(QTestState *qts, uint64_t base_addr, char *msg,
                       size_t count);

/* Write at most 1 block of SD using non-DMA  */
void sdhci_write_cmd(QTestState *qts, uint64_t base_addr, const char *msg,
                     size_t count, size_t blksize);
