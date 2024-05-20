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

#ifndef HEXAGON_HEX_REGS_H
#define HEXAGON_HEX_REGS_H

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
    HEX_REG_P3_0_ALIASED     = 36,
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
    HEX_REG_QEMU_HVX_CNT      = 54,
    HEX_REG_UTIMERLO          = 62,
    HEX_REG_UTIMERHI          = 63,
};

#ifndef CONFIG_USER_ONLY

#define HEX_GREG_VALUES \
  DECL_HEX_GREG(G0,         0) \
  DECL_HEX_GREG(GELR,       0) \
  DECL_HEX_GREG(G1,         1) \
  DECL_HEX_GREG(GSR,        1) \
  DECL_HEX_GREG(G2,         2) \
  DECL_HEX_GREG(GOSP,       2) \
  DECL_HEX_GREG(G3,         3) \
  DECL_HEX_GREG(GBADVA,     3) \
  DECL_HEX_GREG(GCYCLE_1T,  10) \
  DECL_HEX_GREG(GCYCLE_2T,  11) \
  DECL_HEX_GREG(GCYCLE_3T,  12) \
  DECL_HEX_GREG(GCYCLE_4T,  13) \
  DECL_HEX_GREG(GCYCLE_5T,  14) \
  DECL_HEX_GREG(GCYCLE_6T,  15) \
  DECL_HEX_GREG(GPMUCNT4,   16) \
  DECL_HEX_GREG(GPMUCNT5,   17) \
  DECL_HEX_GREG(GPMUCNT6,   18) \
  DECL_HEX_GREG(GPMUCNT7,   19) \
  DECL_HEX_GREG(GPCYCLELO,  24) \
  DECL_HEX_GREG(GPCYCLEHI,  25) \
  DECL_HEX_GREG(GPMUCNT0,   26) \
  DECL_HEX_GREG(GPMUCNT1,   27) \
  DECL_HEX_GREG(GPMUCNT2,   28) \
  DECL_HEX_GREG(GPMUCNT3,   29) \
  DECL_HEX_GREG_DONE

#define DECL_HEX_GREG_DONE
#define DECL_HEX_GREG(name, val) HEX_GREG_ ##name = val,
enum hex_greg {
    HEX_GREG_VALUES
};
#undef DECL_HEX_GREG
#undef DECL_HEX_GREG_DONE

#define DECL_HEX_GREG_DONE 0
#define DECL_HEX_GREG(_, val) (1 << val) |
static inline bool greg_implemented(enum hex_greg greg)
{
#if NUM_GREGS > 32
#error "NUM_GREGS too large for greg_implemented(): update `impl_bitmap`"
#endif
    static int32_t impl_bitmap = HEX_GREG_VALUES;
    return impl_bitmap & (1 << greg);
}
#undef DECL_HEX_GREG
#undef DECL_HEX_GREG_DONE

#endif /* CONFIG_USER_ONLY */

enum {
    HEX_SREG_SGP0 = 0,
    HEX_SREG_SGP1 = 1,
    HEX_SREG_STID = 2,
    HEX_SREG_ELR = 3,
    HEX_SREG_BADVA0 = 4,
    HEX_SREG_BADVA1 = 5,
    HEX_SREG_SSR = 6,
    HEX_SREG_CCR = 7,
    HEX_SREG_HTID = 8,
    HEX_SREG_BADVA = 9,
    HEX_SREG_IMASK = 10,
    HEX_SREG_GEVB  = 11,
    HEX_SREG_GLB_START = 16,
    HEX_SREG_EVB = 16,
    HEX_SREG_MODECTL = 17,
    HEX_SREG_SYSCFG = 18,
    HEX_SREG_IPENDAD = 20,
    HEX_SREG_VID = 21,
    HEX_SREG_VID1 = 22,
    HEX_SREG_BESTWAIT = 23,
    HEX_SREG_IEL = 24,
    HEX_SREG_SCHEDCFG = 25,
    HEX_SREG_IAHL = 26,
    HEX_SREG_CFGBASE = 27,
    HEX_SREG_DIAG = 28,
    HEX_SREG_REV = 29,
    HEX_SREG_PCYCLELO = 30,
    HEX_SREG_PCYCLEHI = 31,
    HEX_SREG_ISDBST = 32,
    HEX_SREG_ISDBCFG0 = 33,
    HEX_SREG_ISDBCFG1 = 34,
    HEX_SREG_LIVELOCK = 35,
    HEX_SREG_BRKPTPC0 = 36,
    HEX_SREG_BRKPTCFG0 = 37,
    HEX_SREG_BRKPTPC1 = 38,
    HEX_SREG_BRKPTCFG1 = 39,
    HEX_SREG_ISDBMBXIN = 40,
    HEX_SREG_ISDBMBXOUT = 41,
    HEX_SREG_ISDBEN = 42,
    HEX_SREG_ISDBGPR = 43,
    HEX_SREG_PMUCNT4 = 44,
    HEX_SREG_PMUCNT5 = 45,
    HEX_SREG_PMUCNT6 = 46,
    HEX_SREG_PMUCNT7 = 47,
    HEX_SREG_PMUCNT0 = 48,
    HEX_SREG_PMUCNT1 = 49,
    HEX_SREG_PMUCNT2 = 50,
    HEX_SREG_PMUCNT3 = 51,
    HEX_SREG_PMUEVTCFG = 52,
    HEX_SREG_PMUSTID0 = 53,
    HEX_SREG_PMUEVTCFG1 = 54,
    HEX_SREG_PMUSTID1 = 55,
    HEX_SREG_TIMERLO = 56,
    HEX_SREG_TIMERHI = 57,
    HEX_SREG_PMUCFG = 58,
    HEX_SREG_S59 = 59,
    HEX_SREG_S60 = 60,
    HEX_SREG_S61 = 61,
    HEX_SREG_S62 = 62,
    HEX_SREG_S63 = 63,
};

#endif
