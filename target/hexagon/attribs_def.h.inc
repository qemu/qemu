/*
 *  Copyright(c) 2019-2024 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

/* Keep this as the first attribute: */
DEF_ATTRIB(AA_DUMMY, "Dummy Zeroth Attribute", "", "")

/* Misc */
DEF_ATTRIB(EXTENSION, "Extension instruction", "", "")

DEF_ATTRIB(PRIV, "Not available in user or guest mode", "", "")
DEF_ATTRIB(GUEST, "Not available in user mode", "", "")

DEF_ATTRIB(FPOP, "Floating Point Operation", "", "")

DEF_ATTRIB(EXTENDABLE, "Immediate may be extended", "", "")

DEF_ATTRIB(ARCHV2, "V2 architecture", "", "")
DEF_ATTRIB(ARCHV3, "V3 architecture", "", "")
DEF_ATTRIB(ARCHV4, "V4 architecture", "", "")
DEF_ATTRIB(ARCHV5, "V5 architecture", "", "")

DEF_ATTRIB(SUBINSN, "sub-instruction", "", "")

/* Load and Store attributes */
DEF_ATTRIB(LOAD, "Loads from memory", "", "")
DEF_ATTRIB(STORE, "Stores to memory", "", "")
DEF_ATTRIB(STOREIMMED, "Stores immed to memory", "", "")
DEF_ATTRIB(MEMSIZE_0B, "Memory width is 0 byte", "", "")
DEF_ATTRIB(MEMSIZE_1B, "Memory width is 1 byte", "", "")
DEF_ATTRIB(MEMSIZE_2B, "Memory width is 2 bytes", "", "")
DEF_ATTRIB(MEMSIZE_4B, "Memory width is 4 bytes", "", "")
DEF_ATTRIB(MEMSIZE_8B, "Memory width is 8 bytes", "", "")
DEF_ATTRIB(SCALAR_LOAD, "Load is scalar", "", "")
DEF_ATTRIB(SCALAR_STORE, "Store is scalar", "", "")
DEF_ATTRIB(REGWRSIZE_1B, "Memory width is 1 byte", "", "")
DEF_ATTRIB(REGWRSIZE_2B, "Memory width is 2 bytes", "", "")
DEF_ATTRIB(REGWRSIZE_4B, "Memory width is 4 bytes", "", "")
DEF_ATTRIB(REGWRSIZE_8B, "Memory width is 8 bytes", "", "")
DEF_ATTRIB(MEMLIKE, "Memory-like instruction", "", "")
DEF_ATTRIB(MEMLIKE_PACKET_RULES, "follows Memory-like packet rules", "", "")
DEF_ATTRIB(RELEASE, "Releases a lock", "", "")
DEF_ATTRIB(ACQUIRE, "Acquires a lock", "", "")

DEF_ATTRIB(RLS_INNER, "Store release inner visibility", "", "")
DEF_ATTRIB(RLS_ALL_THREAD, "Store release among all threads", "", "")
DEF_ATTRIB(RLS_SAME_THREAD, "Store release with the same thread", "", "")

/* V6 Vector attributes */
DEF_ATTRIB(CVI, "Executes on the HVX extension", "", "")

DEF_ATTRIB(CVI_NEW, "New value memory instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VM, "Memory instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VP, "Permute instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VP_VS, "Double vector permute/shft insn executes on HVX", "", "")
DEF_ATTRIB(CVI_VX, "Multiply instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VX_DV, "Double vector multiply insn executes on HVX", "", "")
DEF_ATTRIB(CVI_VS, "Shift instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VS_3SRC, "This shift needs to borrow a source register", "", "")
DEF_ATTRIB(CVI_VS_VX, "Permute/shift and multiply insn executes on HVX", "", "")
DEF_ATTRIB(CVI_VA, "ALU instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VA_DV, "Double vector alu instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_4SLOT, "Consumes all the vector execution resources", "", "")
DEF_ATTRIB(CVI_TMP, "Transient Memory Load not written to register", "", "")
DEF_ATTRIB(CVI_REMAP, "Register Renaming not written to register file", "", "")
DEF_ATTRIB(CVI_GATHER, "CVI Gather operation", "", "")
DEF_ATTRIB(CVI_SCATTER, "CVI Scatter operation", "", "")
DEF_ATTRIB(CVI_SCATTER_RELEASE, "CVI Store Release for scatter", "", "")
DEF_ATTRIB(CVI_TMP_DST, "CVI instruction that doesn't write a register", "", "")
DEF_ATTRIB(CVI_SLOT23, "Can execute in slot 2 or slot 3 (HVX)", "", "")

DEF_ATTRIB(VTCM_ALLBANK_ACCESS, "Allocates in all VTCM schedulers.", "", "")

/* Change-of-flow attributes */
DEF_ATTRIB(JUMP, "Jump-type instruction", "", "")
DEF_ATTRIB(INDIRECT, "Absolute register jump", "", "")
DEF_ATTRIB(CALL, "Function call instruction", "", "")
DEF_ATTRIB(COF, "Change-of-flow instruction", "", "")
DEF_ATTRIB(HINTED_COF, "This instruction is a hinted change-of-flow", "", "")
DEF_ATTRIB(CONDEXEC, "May be cancelled by a predicate", "", "")
DEF_ATTRIB(DOTNEWVALUE, "Uses a register value generated in this pkt", "", "")
DEF_ATTRIB(NEWCMPJUMP, "Compound compare and jump", "", "")
DEF_ATTRIB(NVSTORE, "New-value store", "", "")
DEF_ATTRIB(MEMOP, "memop", "", "")

DEF_ATTRIB(ROPS_2, "Compound instruction worth 2 RISC-ops", "", "")
DEF_ATTRIB(ROPS_3, "Compound instruction worth 3 RISC-ops", "", "")

/* access to implicit registers */
DEF_ATTRIB(IMPLICIT_WRITES_LR, "Writes the link register", "", "UREG.LR")
DEF_ATTRIB(IMPLICIT_WRITES_SP, "Writes the stack pointer", "", "UREG.SP")
DEF_ATTRIB(IMPLICIT_WRITES_FP, "Writes the frame pointer", "", "UREG.FP")
DEF_ATTRIB(IMPLICIT_WRITES_LC0, "Writes loop count for loop 0", "", "UREG.LC0")
DEF_ATTRIB(IMPLICIT_WRITES_LC1, "Writes loop count for loop 1", "", "UREG.LC1")
DEF_ATTRIB(IMPLICIT_WRITES_SA0, "Writes start addr for loop 0", "", "UREG.SA0")
DEF_ATTRIB(IMPLICIT_WRITES_SA1, "Writes start addr for loop 1", "", "UREG.SA1")
DEF_ATTRIB(IMPLICIT_WRITES_P0, "Writes Predicate 0", "", "UREG.P0")
DEF_ATTRIB(IMPLICIT_WRITES_P1, "Writes Predicate 1", "", "UREG.P1")
DEF_ATTRIB(IMPLICIT_WRITES_P2, "Writes Predicate 1", "", "UREG.P2")
DEF_ATTRIB(IMPLICIT_WRITES_P3, "May write Predicate 3", "", "UREG.P3")
DEF_ATTRIB(IMPLICIT_READS_PC, "Reads the PC register", "", "")
DEF_ATTRIB(IMPLICIT_READS_P0, "Reads the P0 register", "", "")
DEF_ATTRIB(IMPLICIT_READS_P1, "Reads the P1 register", "", "")
DEF_ATTRIB(IMPLICIT_READS_P2, "Reads the P2 register", "", "")
DEF_ATTRIB(IMPLICIT_READS_P3, "Reads the P3 register", "", "")
DEF_ATTRIB(IMPLICIT_WRITES_USR, "May write USR", "", "")
DEF_ATTRIB(IMPLICIT_READS_SP, "Reads the SP register", "", "")
DEF_ATTRIB(COMMUTES, "The operation is communitive", "", "")
DEF_ATTRIB(DEALLOCRET, "dealloc_return", "", "")
DEF_ATTRIB(DEALLOCFRAME, "deallocframe", "", "")

DEF_ATTRIB(CRSLOT23, "Can execute in slot 2 or slot 3 (CR)", "", "")
DEF_ATTRIB(IT_NOP, "nop instruction", "", "")
DEF_ATTRIB(IT_EXTENDER, "constant extender instruction", "", "")


/* Restrictions to make note of */
DEF_ATTRIB(RESTRICT_COF_MAX1, "One change-of-flow per packet", "", "")
DEF_ATTRIB(RESTRICT_NOPACKET, "Not allowed in a packet", "", "")
DEF_ATTRIB(RESTRICT_SLOT0ONLY, "Must execute on slot0", "", "")
DEF_ATTRIB(RESTRICT_SLOT1ONLY, "Must execute on slot1", "", "")
DEF_ATTRIB(RESTRICT_SLOT2ONLY, "Must execute on slot2", "", "")
DEF_ATTRIB(RESTRICT_SLOT3ONLY, "Must execute on slot3", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT1, "No slot 1 instruction in parallel", "", "")
DEF_ATTRIB(RESTRICT_PREFERSLOT0, "Try to encode into slot 0", "", "")
DEF_ATTRIB(RESTRICT_PACKET_AXOK, "May exist with A-type or X-type", "", "")

DEF_ATTRIB(ICOP, "Instruction cache op", "", "")

DEF_ATTRIB(HWLOOP0_END, "Ends HW loop0", "", "")
DEF_ATTRIB(HWLOOP1_END, "Ends HW loop1", "", "")
DEF_ATTRIB(RET_TYPE, "return type", "", "")
DEF_ATTRIB(DCZEROA, "dczeroa type", "", "")
DEF_ATTRIB(ICFLUSHOP, "icflush op type", "", "")
DEF_ATTRIB(DCFLUSHOP, "dcflush op type", "", "")
DEF_ATTRIB(L2FLUSHOP, "l2flush op type", "", "")
DEF_ATTRIB(DCFETCH, "dcfetch type", "", "")

DEF_ATTRIB(L2FETCH, "Instruction is l2fetch type", "", "")

DEF_ATTRIB(ICINVA, "icinva", "", "")
DEF_ATTRIB(DCCLEANINVA, "dccleaninva", "", "")

DEF_ATTRIB(NO_INTRINSIC, "Don't generate an intrisic", "", "")

/* Documentation Notes */
DEF_ATTRIB(NOTE_CONDITIONAL, "can be conditionally executed", "", "")
DEF_ATTRIB(NOTE_NEWVAL_SLOT0, "New-value oprnd must execute on slot 0", "", "")
DEF_ATTRIB(NOTE_PRIV, "Monitor-level feature", "", "")
DEF_ATTRIB(NOTE_NOPACKET, "solo instruction", "", "")
DEF_ATTRIB(NOTE_AXOK, "May only be grouped with ALU32 or non-FP XTYPE.", "", "")
DEF_ATTRIB(NOTE_LATEPRED, "The predicate can not be used as a .new", "", "")
DEF_ATTRIB(NOTE_NVSLOT0, "Can execute only in slot 0 (ST)", "", "")
DEF_ATTRIB(NOTE_NOVP, "Cannot be paired with a HVX permute instruction", "", "")
DEF_ATTRIB(NOTE_VA_UNARY, "Combined with HVX ALU op (must be unary)", "", "")

/* V6 MMVector Notes for Documentation */
DEF_ATTRIB(NOTE_SHIFT_RESOURCE, "Uses the HVX shift resource.", "", "")
/* Restrictions to make note of */
DEF_ATTRIB(RESTRICT_NOSLOT1_STORE, "Packet must not have slot 1 store", "", "")
DEF_ATTRIB(RESTRICT_LATEPRED, "Predicate can not be used as a .new.", "", "")

/* Keep this as the last attribute: */
DEF_ATTRIB(ZZ_LASTATTRIB, "Last attribute in the file", "", "")
