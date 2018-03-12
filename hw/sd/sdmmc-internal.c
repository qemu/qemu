/*
 * SD/MMC cards common helpers
 *
 * Copyright (c) 2018  Philippe Mathieu-Daudé <f4bug@amsat.org>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "sdmmc-internal.h"

const char *sd_cmd_name(uint8_t cmd)
{
    static const char *cmd_abbrev[SDMMC_CMD_MAX] = {
         [0]    = "GO_IDLE_STATE",
         [2]    = "ALL_SEND_CID",            [3]    = "SEND_RELATIVE_ADDR",
         [4]    = "SET_DSR",                 [5]    = "IO_SEND_OP_COND",
         [6]    = "SWITCH_FUNC",             [7]    = "SELECT/DESELECT_CARD",
         [8]    = "SEND_IF_COND",            [9]    = "SEND_CSD",
        [10]    = "SEND_CID",               [11]    = "VOLTAGE_SWITCH",
        [12]    = "STOP_TRANSMISSION",      [13]    = "SEND_STATUS",
                                            [15]    = "GO_INACTIVE_STATE",
        [16]    = "SET_BLOCKLEN",           [17]    = "READ_SINGLE_BLOCK",
        [18]    = "READ_MULTIPLE_BLOCK",    [19]    = "SEND_TUNING_BLOCK",
        [20]    = "SPEED_CLASS_CONTROL",    [21]    = "DPS_spec",
                                            [23]    = "SET_BLOCK_COUNT",
        [24]    = "WRITE_BLOCK",            [25]    = "WRITE_MULTIPLE_BLOCK",
        [26]    = "MANUF_RSVD",             [27]    = "PROGRAM_CSD",
        [28]    = "SET_WRITE_PROT",         [29]    = "CLR_WRITE_PROT",
        [30]    = "SEND_WRITE_PROT",
        [32]    = "ERASE_WR_BLK_START",     [33]    = "ERASE_WR_BLK_END",
        [34]    = "SW_FUNC_RSVD",           [35]    = "SW_FUNC_RSVD",
        [36]    = "SW_FUNC_RSVD",           [37]    = "SW_FUNC_RSVD",
        [38]    = "ERASE",
        [40]    = "DPS_spec",
        [42]    = "LOCK_UNLOCK",            [43]    = "Q_MANAGEMENT",
        [44]    = "Q_TASK_INFO_A",          [45]    = "Q_TASK_INFO_B",
        [46]    = "Q_RD_TASK",              [47]    = "Q_WR_TASK",
        [48]    = "READ_EXTR_SINGLE",       [49]    = "WRITE_EXTR_SINGLE",
        [50]    = "SW_FUNC_RSVD",
        [52]    = "IO_RW_DIRECT",           [53]    = "IO_RW_EXTENDED",
        [54]    = "SDIO_RSVD",              [55]    = "APP_CMD",
        [56]    = "GEN_CMD",                [57]    = "SW_FUNC_RSVD",
        [58]    = "READ_EXTR_MULTI",        [59]    = "WRITE_EXTR_MULTI",
        [60]    = "MANUF_RSVD",             [61]    = "MANUF_RSVD",
        [62]    = "MANUF_RSVD",             [63]    = "MANUF_RSVD",
    };
    return cmd_abbrev[cmd] ? cmd_abbrev[cmd] : "UNKNOWN_CMD";
}

const char *sd_acmd_name(uint8_t cmd)
{
    static const char *acmd_abbrev[SDMMC_CMD_MAX] = {
         [6] = "SET_BUS_WIDTH",
        [13] = "SD_STATUS",
        [14] = "DPS_spec",                  [15] = "DPS_spec",
        [16] = "DPS_spec",
        [18] = "SECU_spec",
        [22] = "SEND_NUM_WR_BLOCKS",        [23] = "SET_WR_BLK_ERASE_COUNT",
        [41] = "SD_SEND_OP_COND",
        [42] = "SET_CLR_CARD_DETECT",
        [51] = "SEND_SCR",
        [52] = "SECU_spec",                 [53] = "SECU_spec",
        [54] = "SECU_spec",
        [56] = "SECU_spec",                 [57] = "SECU_spec",
        [58] = "SECU_spec",                 [59] = "SECU_spec",
    };

    return acmd_abbrev[cmd] ? acmd_abbrev[cmd] : "UNKNOWN_ACMD";
}
