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
DEF_ATTRIB(MEMLIKE, "Memory-like instruction", "", "")
DEF_ATTRIB(MEMLIKE_PACKET_RULES, "follows Memory-like packet rules", "", "")


/* Change-of-flow attributes */
DEF_ATTRIB(JUMP, "Jump-type instruction", "", "")
DEF_ATTRIB(INDIRECT, "Absolute register jump", "", "")
DEF_ATTRIB(CALL, "Function call instruction", "", "")
DEF_ATTRIB(COF, "Change-of-flow instruction", "", "")
DEF_ATTRIB(CONDEXEC, "May be cancelled by a predicate", "", "")
DEF_ATTRIB(DOTNEWVALUE, "Uses a register value generated in this pkt", "", "")
DEF_ATTRIB(NEWCMPJUMP, "Compound compare and jump", "", "")

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
DEF_ATTRIB(WRITES_PRED_REG, "Writes a predicate register", "", "")

DEF_ATTRIB(CRSLOT23, "Can execute in slot 2 or slot 3 (CR)", "", "")
DEF_ATTRIB(IT_NOP, "nop instruction", "", "")
DEF_ATTRIB(IT_EXTENDER, "constant extender instruction", "", "")


/* Restrictions to make note of */
DEF_ATTRIB(RESTRICT_SLOT0ONLY, "Must execute on slot0", "", "")
DEF_ATTRIB(RESTRICT_SLOT1ONLY, "Must execute on slot1", "", "")
DEF_ATTRIB(RESTRICT_SLOT2ONLY, "Must execute on slot2", "", "")
DEF_ATTRIB(RESTRICT_SLOT3ONLY, "Must execute on slot3", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT1, "No slot 1 instruction in parallel", "", "")
DEF_ATTRIB(RESTRICT_PREFERSLOT0, "Try to encode into slot 0", "", "")

DEF_ATTRIB(ICOP, "Instruction cache op", "", "")

DEF_ATTRIB(HWLOOP0_END, "Ends HW loop0", "", "")
DEF_ATTRIB(HWLOOP1_END, "Ends HW loop1", "", "")
DEF_ATTRIB(DCZEROA, "dczeroa type", "", "")
DEF_ATTRIB(ICFLUSHOP, "icflush op type", "", "")
DEF_ATTRIB(DCFLUSHOP, "dcflush op type", "", "")
DEF_ATTRIB(DCFETCH, "dcfetch type", "", "")

DEF_ATTRIB(L2FETCH, "Instruction is l2fetch type", "", "")

DEF_ATTRIB(ICINVA, "icinva", "", "")
DEF_ATTRIB(DCCLEANINVA, "dccleaninva", "", "")

/* Keep this as the last attribute: */
DEF_ATTRIB(ZZ_LASTATTRIB, "Last attribute in the file", "", "")
