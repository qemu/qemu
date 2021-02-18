/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HEXAGON_REGS_H
#define HEXAGON_REGS_H

enum {
    HEX_REG_R00              = 0,
    HEX_REG_R01              = 1,
    HEX_REG_R02              = 2,
    HEX_REG_R03              = 3,
    HEX_REG_R04              = 4,
    HEX_REG_R05              = 5,
    HEX_REG_R06              = 6,
    HEX_REG_R07              = 7,
    HEX_REG_R08              = 8,
    HEX_REG_R09              = 9,
    HEX_REG_R10              = 10,
    HEX_REG_R11              = 11,
    HEX_REG_R12              = 12,
    HEX_REG_R13              = 13,
    HEX_REG_R14              = 14,
    HEX_REG_R15              = 15,
    HEX_REG_R16              = 16,
    HEX_REG_R17              = 17,
    HEX_REG_R18              = 18,
    HEX_REG_R19              = 19,
    HEX_REG_R20              = 20,
    HEX_REG_R21              = 21,
    HEX_REG_R22              = 22,
    HEX_REG_R23              = 23,
    HEX_REG_R24              = 24,
    HEX_REG_R25              = 25,
    HEX_REG_R26              = 26,
    HEX_REG_R27              = 27,
    HEX_REG_R28              = 28,
    HEX_REG_R29              = 29,
    HEX_REG_SP               = 29,
    HEX_REG_FP               = 30,
    HEX_REG_R30              = 30,
    HEX_REG_LR               = 31,
    HEX_REG_R31              = 31,
    HEX_REG_SA0              = 32,
    HEX_REG_LC0              = 33,
    HEX_REG_SA1              = 34,
    HEX_REG_LC1              = 35,
    HEX_REG_P3_0             = 36,
    HEX_REG_M0               = 38,
    HEX_REG_M1               = 39,
    HEX_REG_USR              = 40,
    HEX_REG_PC               = 41,
    HEX_REG_UGP              = 42,
    HEX_REG_GP               = 43,
    HEX_REG_CS0              = 44,
    HEX_REG_CS1              = 45,
    HEX_REG_UPCYCLELO        = 46,
    HEX_REG_UPCYCLEHI        = 47,
    HEX_REG_FRAMELIMIT       = 48,
    HEX_REG_FRAMEKEY         = 49,
    HEX_REG_PKTCNTLO         = 50,
    HEX_REG_PKTCNTHI         = 51,
    /* Use reserved control registers for qemu execution counts */
    HEX_REG_QEMU_PKT_CNT      = 52,
    HEX_REG_QEMU_INSN_CNT     = 53,
    HEX_REG_UTIMERLO          = 62,
    HEX_REG_UTIMERHI          = 63,
};

#endif
