/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

DEF_REG(REG_R_REGS,          /* Tag */
        "General Registers", /* Name */
        "r%02d",             /* Symbol for register */
        32,                  /* How many are there */
        0)                   /* The position in register array of 1st element */

DEF_REG(REG_SA0,  "Start Address 0",         "sa0",  1, NUM_GEN_REGS + 0)
DEF_REG(REG_LC0,  "Loop Count 0",            "lc0",  1, NUM_GEN_REGS + 1)
DEF_REG(REG_SA1,  "Start Address 1",         "sa1",  1, NUM_GEN_REGS + 2)
DEF_REG(REG_LC1,  "Loop Count 1",            "lc1",  1, NUM_GEN_REGS + 3)
DEF_REG(REG_PQ,   "Predicate Quad",          "p3:0", 1, NUM_GEN_REGS + 4)
DEF_REG(REG_M,    "Modifier Register",       "m%d",  2, NUM_GEN_REGS + 6)
DEF_REG(REG_USR,  "User status register",    "usr",  1, NUM_GEN_REGS + 8)
DEF_REG(REG_PC,   "Program Counter",         "pc",   1, NUM_GEN_REGS + 9)
DEF_REG(REG_UGP,  "User general pointer",    "ugp",  1, NUM_GEN_REGS + 10)
DEF_REG(REG_GP,   "Global pointer",          "gp",   1, NUM_GEN_REGS + 11)
DEF_REG(REG_CSA,  "Circular Start",          "cs%d", 2, NUM_GEN_REGS + 12)

DEF_REG(REG_FRAMELIMIT, "Frame Limit", "framelimit", 1, NUM_GEN_REGS + 16)
DEF_REG(REG_FRAMEKEY,   "Frame Key",   "framekey",   1, NUM_GEN_REGS + 17)

/*
 * For registers that have individual fields, explain them here
 *   DEF_REG_FIELD(tag,
 *                 name,
 *                 bit start offset,
 *                 width,
 *                 description
 */

/* USR fields */
DEF_REG_FIELD(USR_OVF,
    "ovf", 0, 1,
    "Sticky Saturation Overflow - "
    "Set when saturation occurs while executing instruction that specifies "
    "optional saturation, remains set until explicitly cleared by a USR=Rs "
    "instruction.")
DEF_REG_FIELD(USR_FPINVF,
    "fpinvf", 1, 1,
    "Floating-point IEEE Invalid Sticky Flag.")
DEF_REG_FIELD(USR_FPDBZF,
    "fpdbzf", 2, 1,
    "Floating-point IEEE Divide-By-Zero Sticky Flag.")
DEF_REG_FIELD(USR_FPOVFF,
    "fpovff", 3, 1,
    "Floating-point IEEE Overflow Sticky Flag.")
DEF_REG_FIELD(USR_FPUNFF,
    "fpunff", 4, 1,
    "Floating-point IEEE Underflow Sticky Flag.")
DEF_REG_FIELD(USR_FPINPF,
    "fpinpf", 5, 1,
    "Floating-point IEEE Inexact Sticky Flag.")

DEF_REG_FIELD(USR_LPCFG,
    "lpcfg", 8, 2,
    "Hardware Loop Configuration: "
    "Number of loop iterations (0-3) remaining before pipeline predicate "
    "should be set.")
DEF_REG_FIELD(USR_PKTCNT_U,
    "pktcnt_u", 10, 1,
    "Enable packet counting in User mode.")
DEF_REG_FIELD(USR_PKTCNT_G,
    "pktcnt_g", 11, 1,
    "Enable packet counting in Guest mode.")
DEF_REG_FIELD(USR_PKTCNT_M,
    "pktcnt_m", 12, 1,
    "Enable packet counting in Monitor mode.")
DEF_REG_FIELD(USR_HFD,
    "hfd", 13, 2,
    "Two bits that let the user control the amount of L1 hardware data cache "
    "prefetching (up to 4 cache lines): "
    "00: No prefetching, "
    "01: Prefetch Loads with post-updating address mode when execution is "
        "within a hardware loop, "
    "10: Prefetch any hardware-detected striding Load when execution is within "
        "a hardware loop, "
    "11: Prefetch any hardware-detected striding Load.")
DEF_REG_FIELD(USR_HFI,
    "hfi", 15, 2,
    "Two bits that let the user control the amount of L1 instruction cache "
    "prefetching. "
    "00: No prefetching, "
    "01: Allow prefetching of at most 1 additional cache line, "
    "10: Allow prefetching of at most 2 additional cache lines.")

DEF_REG_FIELD(USR_FPRND,
    "fprnd", 22, 2,
    "Rounding Mode for Floating-Point Instructions: "
    "00: Round to nearest, ties to even (default), "
    "01: Toward zero, "
    "10: Downward (toward negative infinity), "
    "11: Upward (toward positive infinity).")

DEF_REG_FIELD(USR_FPINVE,
    "fpinve", 25, 1,
    "Enable trap on IEEE Invalid.")
DEF_REG_FIELD(USR_FPDBZE,
    "fpdbze", 26, 1, "Enable trap on IEEE Divide-By-Zero.")
DEF_REG_FIELD(USR_FPOVFE,
    "fpovfe", 27, 1,
    "Enable trap on IEEE Overflow.")
DEF_REG_FIELD(USR_FPUNFE,
    "fpunfe", 28, 1,
    "Enable trap on IEEE Underflow.")
DEF_REG_FIELD(USR_FPINPE,
    "fpinpe", 29, 1,
    "Enable trap on IEEE Inexact.")
DEF_REG_FIELD(USR_PFA,
    "pfa", 31, 1,
    "L2 Prefetch Active: Set when non-blocking l2fetch instruction is "
    "prefetching requested data, remains set until l2fetch prefetch operation "
    "is completed (or not active).") /* read-only */

