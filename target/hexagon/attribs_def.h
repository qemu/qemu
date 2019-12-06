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

/* Keep this as the first attribute: */
DEF_ATTRIB(AA_DUMMY, "Dummy Zeroth Attribute", "", "")

/* Misc */
DEF_ATTRIB(FAKEINSN, "Not a real instruction", "", "")
DEF_ATTRIB(MAPPING, "Not real -- asm mapped", "", "")
DEF_ATTRIB(CONDMAPPING, "Not real -- mapped based on values", "", "")
DEF_ATTRIB(EXTENSION, "Extension instruction", "", "")
DEF_ATTRIB(SHARED_EXTENSION, "Shared extension instruction", "", "")

DEF_ATTRIB(PRIV, "Not available in user or guest mode", "", "")
DEF_ATTRIB(GUEST, "Not available in user mode", "", "")

DEF_ATTRIB(FPOP, "Floating Point Operation", "", "")
DEF_ATTRIB(FPDOUBLE, "Double-precision Floating Point Operation", "", "")
DEF_ATTRIB(FPSINGLE, "Single-precision Floating Point Operation", "", "")
DEF_ATTRIB(SFMAKE, "Single Float Make", "", "")
DEF_ATTRIB(DFMAKE, "Single Float Make", "", "")

DEF_ATTRIB(NO_TIMING_LOG, "Does not get logged to the timing model", "", "")

DEF_ATTRIB(EXTENDABLE, "Immediate may be extended", "", "")
DEF_ATTRIB(EXT_UPPER_IMMED, "Extend upper case immediate", "", "")
DEF_ATTRIB(EXT_LOWER_IMMED, "Extend lower case immediate", "", "")
DEF_ATTRIB(MUST_EXTEND, "Immediate must be extended", "", "")
DEF_ATTRIB(INVPRED, "The predicate is inverted for true/false sense", "", "")

DEF_ATTRIB(ARCHV2, "V2 architecture", "", "")
DEF_ATTRIB(ARCHV3, "V3 architecture", "", "")
DEF_ATTRIB(ARCHV4, "V4 architecture", "", "")
DEF_ATTRIB(ARCHV5, "V5 architecture", "", "")

DEF_ATTRIB(PACKED, "Packable instruction", "", "")
DEF_ATTRIB(SUBINSN, "sub-instruction", "", "")

DEF_ATTRIB(16BIT, "16-bit instruction", "", "")

/* Load and Store attributes */
DEF_ATTRIB(LOAD, "Loads from memory", "", "")
DEF_ATTRIB(STORE, "Stores to memory", "", "")
DEF_ATTRIB(STOREIMMED, "Stores immed to memory", "", "")
DEF_ATTRIB(MEMSIZE_1B, "Memory width is 1 byte", "", "")
DEF_ATTRIB(MEMSIZE_2B, "Memory width is 2 bytes", "", "")
DEF_ATTRIB(MEMSIZE_4B, "Memory width is 4 bytes", "", "")
DEF_ATTRIB(MEMSIZE_8B, "Memory width is 8 bytes", "", "")
DEF_ATTRIB(MEMLIKE, "Memory-like instruction", "", "")
DEF_ATTRIB(MEMLIKE_PACKET_RULES, "follows Memory-like packet rules", "", "")
DEF_ATTRIB(CACHEOP, "Cache operation", "", "")
DEF_ATTRIB(COPBYADDRESS, "Cache operation by address", "", "")
DEF_ATTRIB(COPBYIDX, "Cache operation by index", "", "")
DEF_ATTRIB(RELEASE, "Releases a lock", "", "")
DEF_ATTRIB(ACQUIRE, "Acquires a lock", "", "")


/* Load and Store Addressing Mode Attributes */
DEF_ATTRIB(EA_REG_ONLY, "EA = input register only", "", "")
DEF_ATTRIB(EA_IMM_ONLY, "EA = immediate only", "", "")
DEF_ATTRIB(EA_REG_PLUS_IMM, "EA = register plus immediate", "", "")
DEF_ATTRIB(EA_REG_PLUS_REGSCALED, "EA = register plus scaled register", "", "")
DEF_ATTRIB(EA_IMM_PLUS_REGSCALED, "EA = immediate plus scaled register", "", "")
DEF_ATTRIB(EA_BREV_REG, "EA = bit-reversed input register", "", "")
DEF_ATTRIB(EA_GP_IMM, "EA = GP plus immediate (unless extended)", "", "")
DEF_ATTRIB(EA_PAGECROSS, "EA calculation can have a Page Cross Stall", "", "")

DEF_ATTRIB(PM_ANY, "Post Modify", "", "")
DEF_ATTRIB(PM_I, "Post Modify by Immediate", "", "")
DEF_ATTRIB(PM_M, "Post Modify by M register", "", "")
DEF_ATTRIB(PM_CIRI, "Post Modify with Circular Addressing by immediate", "", "")
DEF_ATTRIB(PM_CIRR, "Post Modify with Circular Addressing by I field", "", "")

DEF_ATTRIB(VMEM, "VMEM-type", "", "")
DEF_ATTRIB(VBUF, "Touches the VBUF", "", "")
DEF_ATTRIB(VDBG, "Vector debugging instruction", "", "")

/* V6 Vector attributes */
DEF_ATTRIB(CVI, "Executes on the HVX extension", "", "")
DEF_ATTRIB(NT_VMEM, "Non-temporal memory access", "", "")
DEF_ATTRIB(VMEMU, "Unaligned memory access", "", "")

DEF_ATTRIB(CVI_NEW, "New value memory instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VM, "Memory instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VP, "Permute instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VP_VS, "Double vector permute/shft insn executes on HVX", "", "")
DEF_ATTRIB(CVI_VX, "Multiply instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VX_DV, "Double vector multiply insn executes on HVX", "", "")
DEF_ATTRIB(CVI_VS, "Shift instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VS_VX, "Permute/shift and multiply insn executes on HVX", "", "")
DEF_ATTRIB(CVI_VA, "ALU instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_VA_DV, "Double vector alu instruction executes on HVX", "", "")
DEF_ATTRIB(CVI_4SLOT, "Consumes all the vector execution resources", "", "")
DEF_ATTRIB(CVI_TMP, "Transient Memory Load not written to register", "", "")
DEF_ATTRIB(CVI_TMP_SRC, "Transient reassign", "", "")
DEF_ATTRIB(CVI_EXTRACT, "HVX Extract Instruction that goes through L2", "", "")
DEF_ATTRIB(CVI_EARLY, "HVX instructions that require early sources", "", "")
DEF_ATTRIB(CVI_LATE, "HVX insn that always require late sources", "", "")
DEF_ATTRIB(CVI_VV_LATE, "HVX insn that always require late Vv source", "", "")
DEF_ATTRIB(CVI_REQUIRES_TMPLOAD, ".tmp load must be included in packet", "", "")
DEF_ATTRIB(CVI_PUMP_2X, "Goes through the pipeline twice", "", "")
DEF_ATTRIB(CVI_PUMP_4X, "Goes through the pipeline four times", "", "")
DEF_ATTRIB(CVI_GATHER, "CVI Gather operation", "", "")
DEF_ATTRIB(CVI_SCATTER, "CVI Scatter operation", "", "")
DEF_ATTRIB(CVI_SCATTER_RELEASE, "CVI Store Release for scatter", "", "")
DEF_ATTRIB(CVI_GATHER_RELEASE, "CVI Store Release for gather", "", "")
DEF_ATTRIB(CVI_TMP_DST, "CVI instruction that doesn't write a register", "", "")
DEF_ATTRIB(CVI_SCATTER_WORD_ACC, "CVI Scatter Word Accum (second pass)", "", "")
DEF_ATTRIB(CVI_SCATTER_ACC, "CVI Scatter Accumulate", "", "")

DEF_ATTRIB(CVI_GATHER_ADDR_2B, "CVI Scatter/Gather address is halfword", "", "")
DEF_ATTRIB(CVI_GATHER_ADDR_4B, "CVI Scatter/Gather address is word", "", "")

DEF_ATTRIB(VFETCH, "memory fetch op to L2 for a single vector", "", "")

DEF_ATTRIB(CVI_SLOT23, "Can execute in slot 2 or slot 3 (HVX)", "", "")

DEF_ATTRIB(VTCM_ALLBANK_ACCESS, "Allocates in all VTCM schedulers", "", "")

/* Change-of-flow attributes */
DEF_ATTRIB(JUMP, "Jump-type instruction", "", "")
DEF_ATTRIB(DIRECT, "Uses an PC-relative immediate field", "", "")
DEF_ATTRIB(INDIRECT, "Absolute register jump", "", "")
DEF_ATTRIB(CJUMP, "Conditional jump", "", "")
DEF_ATTRIB(CALL, "Function call instruction", "", "")
DEF_ATTRIB(RET, "Function return instruction", "", "")
DEF_ATTRIB(PERM, "Permute instruction", "", "")
DEF_ATTRIB(COF, "Change-of-flow instruction", "", "")
DEF_ATTRIB(CONDEXEC, "May be cancelled by a predicate", "", "")
DEF_ATTRIB(DOTOLD, "Uses a predicate generated in a previous packet", "", "")
DEF_ATTRIB(DOTNEW, "Uses a predicate generated in the same packet", "", "")
DEF_ATTRIB(DOTNEWVALUE, "Uses a register value generated in this pkt", "", "")
DEF_ATTRIB(NEWCMPJUMP, "Compound compare and jump", "", "")
DEF_ATTRIB(NVSTORE, "New-value store", "", "")
DEF_ATTRIB(MEMOP, "memop", "", "")

DEF_ATTRIB(ROPS_2, "Compound instruction worth 2 wimpy RISC-ops", "", "")
DEF_ATTRIB(ROPS_3, "Compound instruction worth 3 wimpy RISC-ops", "", "")


/* access to implicit registers */
DEF_ATTRIB(IMPLICIT_WRITES_LR, "Writes the link register", "", "UREG.LR")
DEF_ATTRIB(IMPLICIT_READS_LR, "Reads the link register", "UREG.LR", "")
DEF_ATTRIB(IMPLICIT_READS_LC0, "Reads loop count for loop 0", "UREG.LC0", "")
DEF_ATTRIB(IMPLICIT_READS_LC1, "Reads loop count for loop 1", "UREG.LC1", "")
DEF_ATTRIB(IMPLICIT_READS_SA0, "Reads start address for loop 0", "UREG.SA0", "")
DEF_ATTRIB(IMPLICIT_READS_SA1, "Reads start address for loop 1", "UREG.SA1", "")
DEF_ATTRIB(IMPLICIT_WRITES_PC, "Writes the program counter", "", "UREG.PC")
DEF_ATTRIB(IMPLICIT_READS_PC, "Reads the program counter", "UREG.PC", "")
DEF_ATTRIB(IMPLICIT_WRITES_SP, "Writes the stack pointer", "", "UREG.SP")
DEF_ATTRIB(IMPLICIT_READS_SP, "Reads the stack pointer", "UREG.SP", "")
DEF_ATTRIB(IMPLICIT_WRITES_FP, "Writes the frame pointer", "", "UREG.FP")
DEF_ATTRIB(IMPLICIT_READS_FP, "Reads the frame pointer", "UREG.FP", "")
DEF_ATTRIB(IMPLICIT_WRITES_GP, "Writes the GP register", "", "UREG.GP")
DEF_ATTRIB(IMPLICIT_READS_GP, "Reads the GP register", "UREG.GP", "")
DEF_ATTRIB(IMPLICIT_WRITES_LC0, "Writes loop count for loop 0", "", "UREG.LC0")
DEF_ATTRIB(IMPLICIT_WRITES_LC1, "Writes loop count for loop 1", "", "UREG.LC1")
DEF_ATTRIB(IMPLICIT_WRITES_SA0, "Writes start addr for loop 0", "", "UREG.SA0")
DEF_ATTRIB(IMPLICIT_WRITES_SA1, "Writes start addr for loop 1", "", "UREG.SA1")
DEF_ATTRIB(IMPLICIT_WRITES_R00, "Writes Register 0", "", "UREG.R00")
DEF_ATTRIB(IMPLICIT_WRITES_P0, "Writes Predicate 0", "", "UREG.P0")
DEF_ATTRIB(IMPLICIT_WRITES_P1, "Writes Predicate 1", "", "UREG.P1")
DEF_ATTRIB(IMPLICIT_WRITES_P2, "Writes Predicate 1", "", "UREG.P2")
DEF_ATTRIB(IMPLICIT_WRITES_P3, "May write Predicate 3", "", "UREG.P3")
DEF_ATTRIB(IMPLICIT_READS_R00, "Reads Register 0", "UREG.R00", "")
DEF_ATTRIB(IMPLICIT_READS_P0, "Reads Predicate 0", "UREG.P0", "")
DEF_ATTRIB(IMPLICIT_READS_P1, "Reads Predicate 1", "UREG.P1", "")
DEF_ATTRIB(IMPLICIT_READS_P3, "Reads Predicate 3", "UREG.P3", "")
DEF_ATTRIB(IMPLICIT_READS_Q3, "Reads Vector Predicate 3", "UREG.Q3", "")
DEF_ATTRIB(IMPLICIT_READS_CS, "Reads the CS/M register", "UREG.CS", "")
DEF_ATTRIB(IMPLICIT_READS_FRAMEKEY, "Reads FRAMEKEY", "UREG.FRAMEKEY", "")
DEF_ATTRIB(IMPLICIT_READS_FRAMELIMIT, "Reads FRAMELIMIT", "UREG.FRAMELIMIT", "")
DEF_ATTRIB(IMPLICIT_READS_ELR, "Reads the ELR register", "MREG.ELR", "")
DEF_ATTRIB(IMPLICIT_READS_SGP0, "Reads the SGP0 register", "MREG.SGP0", "")
DEF_ATTRIB(IMPLICIT_READS_SGP1, "Reads the SGP1 register", "MREG.SGP1", "")
DEF_ATTRIB(IMPLICIT_WRITES_SGP0, "Reads the SGP0 register", "", "MREG.SGP0")
DEF_ATTRIB(IMPLICIT_WRITES_SGP1, "Reads the SGP1 register", "", "MREG.SGP1")
DEF_ATTRIB(IMPLICIT_WRITES_STID_PRIO_ANYTHREAD, "Reads", "", "MREG.STID.PRIO")
DEF_ATTRIB(IMPLICIT_WRITES_SRBIT, "Writes the OVF bit", "", "UREG.SR.OVF")
DEF_ATTRIB(IMPLICIT_WRITES_FPFLAGS, "May write FP flags", "", "UREG.SR.FPFLAGS")
DEF_ATTRIB(IMPLICIT_WRITES_LPCFG, "Writes the loop config", "", "UREG.SR.LPCFG")
DEF_ATTRIB(IMPLICIT_WRITES_CVBITS, "Writes the CV flags", "", "UREG.SR.CV")
DEF_ATTRIB(IMPLICIT_READS_FPRND, "May read FP rnd mode", "UREG.SR.FPRND", "")
DEF_ATTRIB(IMPLICIT_READS_SSR, "May read SSR values", "MREG.SSR", "")
DEF_ATTRIB(IMPLICIT_READS_CCR, "May read CCR values", "MREG.CCR", "")
DEF_ATTRIB(IMPLICIT_WRITES_CCR, "May write CCR values", "", "MREG.CCR")
DEF_ATTRIB(IMPLICIT_WRITES_SSR, "May write SSR values", "", "MREG.SSR")
DEF_ATTRIB(IMPLICIT_READS_GELR, "May read GELR values", "GREG.GELR", "")
DEF_ATTRIB(IMPLICIT_READS_GEVB, "May read GEVB values", "MREG.GEVB", "")
DEF_ATTRIB(IMPLICIT_READS_GSR, "May read GSR values", "GREG.GSR", "")
DEF_ATTRIB(IMPLICIT_READS_GOSP, "May read GOSP values", "GREG.GOSP", "")
DEF_ATTRIB(IMPLICIT_WRITES_GELR, "May write GELR values", "", "GREG.GELR")
DEF_ATTRIB(IMPLICIT_WRITES_GSR, "May write GSR values", "", "GREG.GSR")
DEF_ATTRIB(IMPLICIT_WRITES_GOSP, "May write GOSP values", "", "GREG.GOSP")
DEF_ATTRIB(IMPLICIT_READS_IPENDAD_IPEND, "May read", "MREG.IPENDAD.IPEND", "")
DEF_ATTRIB(IMPLICIT_WRITES_IPENDAD_IPEND, "May write", "", "MREG.IPENDAD.IPEND")
DEF_ATTRIB(IMPLICIT_READS_IPENDAD_IAD, "May read", "MREG.IPENDAD.IAD", "")
DEF_ATTRIB(IMPLICIT_WRITES_IPENDAD_IAD, "May write", "", "MREG.IPENDAD.IAD")
DEF_ATTRIB(IMPLICIT_WRITES_IMASK_ANYTHREAD, "May write", "", "MREG.IMASK")
DEF_ATTRIB(IMPLICIT_READS_IMASK_ANYTHREAD, "May read", "MREG.IMASK", "")
DEF_ATTRIB(IMPLICIT_READS_SYSCFG_K0LOCK, "May read", "MREG.SYSCFG.K0LOCK", "")
DEF_ATTRIB(IMPLICIT_WRITES_SYSCFG_K0LOCK, "May write", "", "MREG.SYSCFG.K0LOCK")
DEF_ATTRIB(IMPLICIT_READS_SYSCFG_TLBLOCK, "May read", "MREG.SYSCFG.TLBLOCK", "")
DEF_ATTRIB(IMPLICIT_WRITES_SYSCFG_TLBLOCK, "May wr", "", "MREG.SYSCFG.TLBLOCK")
DEF_ATTRIB(IMPLICIT_WRITES_SYSCFG_GCA, "May write", "", "MREG.SYSCFG.GCA")
DEF_ATTRIB(IMPLICIT_READS_SYSCFG_GCA, "May read", "MREG.SYSCFG.GCA", "")
DEF_ATTRIB(IMPLICIT_WRITES_USR_PFA, "May write USR_PFA", "", "UREG.SR.PFA")

/* Other things the instruction does */
DEF_ATTRIB(ACC, "Has a multiply", "", "")
DEF_ATTRIB(MPY, "Has a multiply", "", "")
DEF_ATTRIB(SATURATE, "Does signed saturation", "", "")
DEF_ATTRIB(USATURATE, "Does unsigned saturation", "", "")
DEF_ATTRIB(CIRCADDR, "Uses circular addressing mode", "", "")
DEF_ATTRIB(BREVADDR, "Uses bit reverse addressing mode", "", "")
DEF_ATTRIB(BIDIRSHIFTL, "Uses a bidirectional shift left", "", "")
DEF_ATTRIB(BIDIRSHIFTR, "Uses a bidirectional shift right", "", "")
DEF_ATTRIB(BRANCHADDER, "Contains a PC-plus-immediate operation.", "", "")
DEF_ATTRIB(CRSLOT23, "Can execute in slot 2 or slot 3 (CR)", "", "")
DEF_ATTRIB(COMMUTES, "The operation is communitive", "", "")
DEF_ATTRIB(DEALLOCRET, "dealloc_return", "", "")
DEF_ATTRIB(DEALLOCFRAME, "deallocframe", "", "")

/* Instruction Types */

DEF_ATTRIB(IT_ALU, "ALU type", "", "")
DEF_ATTRIB(IT_ALU_ADDSUB, "ALU add or subtract type", "", "")
DEF_ATTRIB(IT_ALU_MINMAX, "ALU MIN or MAX type", "", "")
DEF_ATTRIB(IT_ALU_MOVE, "ALU data movement type", "", "")
DEF_ATTRIB(IT_ALU_LOGICAL, "ALU logical operation type", "", "")
DEF_ATTRIB(IT_ALU_SHIFT, "ALU shift operation type", "", "")
DEF_ATTRIB(IT_ALU_SHIFT_AND_OP, "ALU shift and additional op type", "", "")
DEF_ATTRIB(IT_ALU_CMP, "ALU compare operation type", "", "")

DEF_ATTRIB(IT_LOAD, "Loads from memory", "", "")
DEF_ATTRIB(IT_STORE, "Stores to memory", "", "")

DEF_ATTRIB(IT_MPY, "Multiply type", "", "")
DEF_ATTRIB(IT_MPY_32, "32-bit Multiply type", "", "")

DEF_ATTRIB(IT_COF, "Change-of-flow type", "", "")
DEF_ATTRIB(IT_HWLOOP, "Sets up hardware loop registers", "", "")

DEF_ATTRIB(IT_MISC, "misc instruction type", "", "")

DEF_ATTRIB(IT_NOP, "nop instruction", "", "")
DEF_ATTRIB(IT_EXTENDER, "constant extender instruction", "", "")


/* Exceptions the instruction can generate */

DEF_ATTRIB(EXCEPTION_TLB, "Can generate a TLB Miss Exception", "", "")
DEF_ATTRIB(EXCEPTION_ACCESS, "Can generate Access Violation Exception", "", "")
DEF_ATTRIB(EXCEPTION_SWI, "Software Interrupt (trap) exception", "", "")


/* Documentation Notes */
DEF_ATTRIB(NOTE_ARCHV2, "Only available in the V2 architecture", "", "")

DEF_ATTRIB(NOTE_PACKET_PC, "The PC is the addr of the start of the pkt", "", "")

DEF_ATTRIB(NOTE_PACKET_NPC, "Next PC is the address following pkt", "", "")

DEF_ATTRIB(NOTE_CONDITIONAL, "can be conditionally executed", "", "")

DEF_ATTRIB(NOTE_NEWVAL_SLOT0, "New-value oprnd must execute on slot 0", "", "")

DEF_ATTRIB(NOTE_RELATIVE_ADDRESS, "A PC-relative address is formed", "", "")

DEF_ATTRIB(NOTE_LA_RESTRICT, "Cannot be in the last pkt of a HW loop", "", "")

DEF_ATTRIB(NOTE_OOBVSHIFT, "Possible shift overflow", "", "")
DEF_ATTRIB(NOTE_BIDIRSHIFT, "Bidirectional shift", "", "")

DEF_ATTRIB(NOTE_CVFLAGS, "Sets the Carry and Overflow flags in USR.", "", "")
DEF_ATTRIB(NOTE_SR_OVF_WHEN_SATURATING, "Might set OVF bit", "", "")
DEF_ATTRIB(NOTE_PRIV, "Monitor-level feature", "", "")
DEF_ATTRIB(NOTE_GUEST, "Guest-level feature", "", "")
DEF_ATTRIB(NOTE_NOPACKET, "solo instruction", "", "")
DEF_ATTRIB(NOTE_AXOK, "May only be grouped with ALU32 or non-FP XTYPE.", "", "")
DEF_ATTRIB(NOTE_NOSLOT1, "Packet with this insn must have slot 1 empty", "", "")
DEF_ATTRIB(NOTE_SLOT1_AOK, "Packet must have slot 1 empty or ALU32", "", "")
DEF_ATTRIB(NOTE_NOSLOT01, "Packet must have both slot 1 and 2 empty", "", "")
DEF_ATTRIB(NOTE_NEEDS_MEMLD, "Must be grouped with a memory load", "", "")
DEF_ATTRIB(NOTE_LATEPRED, "The predicate can not be used as a .new", "", "")
DEF_ATTRIB(NOTE_COMPAT_ACCURACY, "In the future accuracy may increase", "", "")
DEF_ATTRIB(NOTE_NVSLOT0, "Can execute only in slot 0 (ST)", "", "")
DEF_ATTRIB(NOTE_DEPRECATED, "Will be deprecated in a future version.", "", "")
DEF_ATTRIB(NOTE_NONAPALIV1, "may not work correctly in Napali V1.", "", "")
DEF_ATTRIB(NOTE_BADTAG_UNDEF, "Undefined if a tag is non-present", "", "")
DEF_ATTRIB(NOTE_NOSLOT2_MPY, "Packet cannot have a slot 2 multiply", "", "")
DEF_ATTRIB(NOTE_HVX_ONLY, "Only available on a core with HVX.", "", "")

DEF_ATTRIB(NOTE_NOCOF_RESTRICT, "Cannot be grouped with any COF", "", "")
DEF_ATTRIB(NOTE_BRANCHADDER_MAX1, "One PC-plus-offset calculation", "", "")

DEF_ATTRIB(NOTE_CRSLOT23, "Execute on either slot2 or slot3 (CR)", "", "")
DEF_ATTRIB(NOTE_EXTENSION_AUDIO, "Hexagon audio extensions", "", "")


/* V6 MMVector Notes for Documentation */
DEF_ATTRIB(NOTE_ANY_RESOURCE, "Can use any HVX resource.", "", "")
DEF_ATTRIB(NOTE_ANY2_RESOURCE, "Uses any pair of the HVX resources", "", "")
DEF_ATTRIB(NOTE_PERMUTE_RESOURCE, "Uses the HVX permute resource.", "", "")
DEF_ATTRIB(NOTE_SHIFT_RESOURCE, "Uses the HVX shift resource.", "", "")
DEF_ATTRIB(NOTE_MPY_RESOURCE, "Uses a HVX multiply resource.", "", "")
DEF_ATTRIB(NOTE_MPYDV_RESOURCE, "Uses both HVX multiply resources.", "", "")
DEF_ATTRIB(NOTE_NT_VMEM, "Non-temporal hint to the micro-architecture", "", "")
DEF_ATTRIB(NOTE_ALL_RESOURCE, "Uses all HVX resources.", "", "")
DEF_ATTRIB(NOTE_VMEM, "Immediates are in multiples of vector length.", "", "")
DEF_ATTRIB(NOTE_ANY_VS_VX_RESOURCE, "Consumes two resources", "", "")

DEF_ATTRIB(NOTE_RT8, "Input scalar register Rt is limited to R0-R7", "", "")

/* Restrictions to make note of */
DEF_ATTRIB(RESTRICT_LOOP_LA, "Cannot be in the last packet of a loop", "", "")
DEF_ATTRIB(RESTRICT_NEEDS_MEMLD, "Must be grouped with a load", "", "")
DEF_ATTRIB(RESTRICT_COF_MAX1, "One change-of-flow per packet", "", "")
DEF_ATTRIB(RESTRICT_NOPACKET, "Not allowed in a packet", "", "")
DEF_ATTRIB(RESTRICT_NOSRMOVE, "Do not write SR in the same packet", "", "")
DEF_ATTRIB(RESTRICT_SLOT0ONLY, "Must execute on slot0", "", "")
DEF_ATTRIB(RESTRICT_SLOT1ONLY, "Must execute on slot1", "", "")
DEF_ATTRIB(RESTRICT_SLOT2ONLY, "Must execute on slot2", "", "")
DEF_ATTRIB(RESTRICT_SLOT3ONLY, "Must execute on slot3", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT2_MPY, "A packet cannot have a slot 2 mpy", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT1, "No slot 1 instruction in parallel", "", "")
DEF_ATTRIB(RESTRICT_SLOT1_AOK, "Slot 1 insn must be empty or A-type", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT01, "No slot 0 or 1 instructions in parallel", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT1_STORE, "Packet must not have slot 1 store", "", "")
DEF_ATTRIB(RESTRICT_NOSLOT0_LOAD, "Packet must not have a slot 1 load", "", "")
DEF_ATTRIB(RESTRICT_NOCOF, "Cannot be grouped with any COF", "", "")
DEF_ATTRIB(RESTRICT_BRANCHADDER_MAX1, "One PC-plus-offset calculation", "", "")
DEF_ATTRIB(RESTRICT_PREFERSLOT0, "Try to encode into slot 0", "", "")
DEF_ATTRIB(RESTRICT_SINGLE_MEM_FIRST, "Single memory op must be last", "", "")
DEF_ATTRIB(RESTRICT_PACKET_AXOK, "May exist with A-type or X-type", "", "")
DEF_ATTRIB(RESTRICT_PACKET_SOMEREGS_OK, "Relaxed grouping rules", "", "")
DEF_ATTRIB(RESTRICT_LATEPRED, "Predicate can not be used as a .new.", "", "")

DEF_ATTRIB(PAIR_1OF2, "For assembler", "", "")
DEF_ATTRIB(PAIR_2OF2, "For assembler", "", "")

/* Performance based preferences */
DEF_ATTRIB(PREFER_SLOT3, "Complex XU prefering slot3", "", "")

DEF_ATTRIB(RELAX_COF_1ST, "COF can be fisrt in assembly order", "", "")
DEF_ATTRIB(RELAX_COF_2ND, "COF can be second in assembly order", "", "")

DEF_ATTRIB(ICOP, "Instruction cache op", "", "")

DEF_ATTRIB(INTRINSIC_RETURNS_UNSIGNED, "Intrinsic returns an unsigned", "", "")

DEF_ATTRIB(PRED_BIT_1, "The branch uses bit 1 as the prediction bit", "", "")
DEF_ATTRIB(PRED_BIT_4, "The branch uses bit 4 as the prediction bit", "", "")
DEF_ATTRIB(PRED_BIT_8, "The branch uses bit 8 as the prediction bit", "", "")
DEF_ATTRIB(PRED_BIT_12, "The branch uses bit 12 as the prediction bit", "", "")
DEF_ATTRIB(PRED_BIT_13, "The branch uses bit 13 as the prediction bit", "", "")
DEF_ATTRIB(PRED_BIT_7, "The branch uses bit 7 as the prediction bit", "", "")
DEF_ATTRIB(HWLOOP0_SETUP, "Sets up HW loop0", "", "")
DEF_ATTRIB(HWLOOP1_SETUP, "Sets up HW loop1", "", "")
DEF_ATTRIB(HWLOOP0_END, "Ends HW loop0", "", "")
DEF_ATTRIB(HWLOOP1_END, "Ends HW loop1", "", "")
DEF_ATTRIB(RET_TYPE, "return type", "", "")
DEF_ATTRIB(HINTJR, "hintjr type", "", "")
DEF_ATTRIB(DCZEROA, "dczeroa type", "", "")
DEF_ATTRIB(ICTAGOP, "ictag op type", "", "")
DEF_ATTRIB(ICFLUSHOP, "icflush op type", "", "")
DEF_ATTRIB(DCFLUSHOP, "dcflush op type", "", "")
DEF_ATTRIB(DCTAGOP, "dctag op type", "", "")
DEF_ATTRIB(L2FLUSHOP, "l2flush op type", "", "")
DEF_ATTRIB(L2TAGOP, "l2tag op type", "", "")
DEF_ATTRIB(DCFETCH, "dcfetch type", "", "")
DEF_ATTRIB(BIMODAL_BRANCH, "Updates the bimodal branch predictor", "", "")

DEF_ATTRIB(VECINSN, "Long Vector Instruction", "", "")
DEF_ATTRIB(MEMSIZE_32B, "Memory width is 32 bytes", "", "")
DEF_ATTRIB(FOUR_PHASE, "Four Phase Instruction", "", "")
DEF_ATTRIB(L2FETCH, "Instruction is l2fetch type", "", "")

DEF_ATTRIB(PREDUSE_BSB, "Instructions need back-skip-back scheduling", "", "")
DEF_ATTRIB(ICINVA, "icinva", "", "")
DEF_ATTRIB(DCCLEANINVA, "dccleaninva", "", "")

DEF_ATTRIB(EXTENSION_AUDIO, "audio extension", "", "")

DEF_ATTRIB(MEMCPY, "memcpy or dma-type instruction", "", "")
DEF_ATTRIB(NO_INTRINSIC, "Don't generate an intrisic", "", "")

DEF_ATTRIB(NO_XML, "Don't generate a XML docs for this instruction", "", "")

/* Keep this as the last attribute: */
DEF_ATTRIB(ZZ_LASTATTRIB, "Last attribute in the file", "", "")

