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
DEF_ATTRIB(AA_DUMMY, "Dummy Zeroth Attribute")

/* Misc */
DEF_ATTRIB(FAKEINSN, "Not a real instruction")
DEF_ATTRIB(MAPPING, "Not a real instruction -- asm mapped to real insn")
DEF_ATTRIB(CONDMAPPING,
    "Not a real instruction -- asm mapped to real insns -- "
    "mapping based on values")
DEF_ATTRIB(EXTENSION, "This instruction is an extension instruction")
DEF_ATTRIB(SHARED_EXTENSION,
    "This instruction is a shared extension instruction")

DEF_ATTRIB(PRIV, "Instruction not available in user or guest mode")
DEF_ATTRIB(GUEST, "Instruction not available in user mode")

DEF_ATTRIB(FPOP, "Instruction is a Floating Point Operation")
DEF_ATTRIB(FPDOUBLE,
    "Instruction is a double-precision Floating Point Operation")
DEF_ATTRIB(FPSINGLE,
    "Instruction is a single-precision Floating Point Operation")
DEF_ATTRIB(SFMAKE, "Instruction is a Single Float Make")
DEF_ATTRIB(DFMAKE, "Instruction is a Single Float Make")

DEF_ATTRIB(NO_TIMING_LOG,
    "Instruction does not get logged to the timing model")

DEF_ATTRIB(EXTENDABLE, "Immediate may be extended")
DEF_ATTRIB(EXT_UPPER_IMMED, "Extend upper case immediate")
DEF_ATTRIB(EXT_LOWER_IMMED, "Extend lower case immediate")
DEF_ATTRIB(MUST_EXTEND, "Immediate must be extended")
DEF_ATTRIB(INVPRED, "The predicate is inverted for true/false sense")

DEF_ATTRIB(ARCHV2, "V2 architecture")
DEF_ATTRIB(ARCHV3, "V3 architecture")
DEF_ATTRIB(ARCHV4, "V4 architecture")
DEF_ATTRIB(ARCHV5, "V5 architecture")

DEF_ATTRIB(PACKED, "Packable instruction")
DEF_ATTRIB(SUBINSN, "sub-instruction")

DEF_ATTRIB(16BIT, "16-bit instruction")

/* Load and Store attributes */
DEF_ATTRIB(LOAD, "Instruction loads from memory")
DEF_ATTRIB(STORE, "Instruction stores to memory")
DEF_ATTRIB(STOREIMMED, "Instruction stores immed to memory")
DEF_ATTRIB(MEMSIZE_1B, "Memory width is 1 byte")
DEF_ATTRIB(MEMSIZE_2B, "Memory width is 2 bytes")
DEF_ATTRIB(MEMSIZE_4B, "Memory width is 4 bytes")
DEF_ATTRIB(MEMSIZE_8B, "Memory width is 8 bytes")
DEF_ATTRIB(MEMLIKE, "Memory-like instruction")
DEF_ATTRIB(MEMLIKE_PACKET_RULES,
    "Not a Memory-like instruction but "
    "follows Memory-like instruction packet rules")
DEF_ATTRIB(CACHEOP, "Instruction is a cache operation")
DEF_ATTRIB(COPBYADDRESS, "Instruction is a cache operation by address")
DEF_ATTRIB(COPBYIDX, "Instruction is a cache operation by index")
DEF_ATTRIB(RELEASE, "Instruction releases a lock")
DEF_ATTRIB(ACQUIRE, "Instruction acquires a lock")


/* Load and Store Addressing Mode Attributes */
DEF_ATTRIB(EA_REG_ONLY,
    "Effective Address calculated by input register only")
DEF_ATTRIB(EA_IMM_ONLY,
    "Effective Address calculated by immediate only")
DEF_ATTRIB(EA_REG_PLUS_IMM,
    "Effective Address calculated by register plus immediate")
DEF_ATTRIB(EA_REG_PLUS_REGSCALED,
    "Effective Address calculated by register plus scaled register")
DEF_ATTRIB(EA_IMM_PLUS_REGSCALED,
    "Effective Address calculated by immediate plus scaled register")
DEF_ATTRIB(EA_BREV_REG,
    "Effective Address calculated by bit-reversed input register")
DEF_ATTRIB(EA_GP_IMM,
    "Effective address caluclated by Glogbal Pointer plus immediate "
    "(unless extended)")
DEF_ATTRIB(EA_PAGECROSS,
    "Effective Address Calculation can have a Page Cross Stall")

DEF_ATTRIB(PM_ANY, "Post Modify")
DEF_ATTRIB(PM_I, "Post Modify by Immediate")
DEF_ATTRIB(PM_M, "Post Modify by M register")
DEF_ATTRIB(PM_CIRI, "Post Modify with Circular Addressing by immediate")
DEF_ATTRIB(PM_CIRR, "Post Modify with Circular Addressing by I field")

DEF_ATTRIB(VMEM, "This instruction is VMEM-type")
DEF_ATTRIB(VBUF, "This instruction touches the VBUF")
DEF_ATTRIB(VDBG, "Vector debugging instruction")

/* V6 Vector attributes */
DEF_ATTRIB(CVI, "This instruction executes on the HVX extension")
DEF_ATTRIB(NT_VMEM, "Non-temporal memory access")
DEF_ATTRIB(VMEMU, "This instruction is an unaligned memory access")

DEF_ATTRIB(CVI_NEW,
    "This new value memory instruction executes on multimedia vector engine",
    "", "")
DEF_ATTRIB(CVI_VM,
    "This memory instruction executes on multimedia vector engine")
DEF_ATTRIB(CVI_VP,
    "This permute instruction executes on multimedia vector engine")
DEF_ATTRIB(CVI_VP_VS,
    "This double vector permute/shift instruction executes on multimedia "
    "vector engine")
DEF_ATTRIB(CVI_VX,
    "This multiply instruction executes on multimedia vector engine")
DEF_ATTRIB(CVI_VX_DV,
    "This double vector multiply instruction executes on multimedia "
    "vector engine")
DEF_ATTRIB(CVI_VS,
    "This shift instruction executes on multimedia vector engine")
DEF_ATTRIB(CVI_VS_VX,
    "This permute/shift and multiply instruction executes on multimedia "
    "vector engine")
DEF_ATTRIB(CVI_VA,
    "This alu instruction executes on multimedia vector engine")
DEF_ATTRIB(CVI_VA_DV,
    "This double vector alu instruction executes on multimedia vector engine",
    "", "")
DEF_ATTRIB(CVI_4SLOT,
    "This instruction consumes all the vector execution resources")
DEF_ATTRIB(CVI_TMP,
    "Transient Memory Load not written to register file")
DEF_ATTRIB(CVI_TMP_SRC,
    "Transient reassign")
DEF_ATTRIB(CVI_EXTRACT, "HVX Extract Instruction that goes through L2")
DEF_ATTRIB(CVI_EARLY,
    "HVX instructions that always require early sources")
DEF_ATTRIB(CVI_LATE,
    "HVX instructions that always require late sources")
DEF_ATTRIB(CVI_VV_LATE,
    "HVX instructions that always require late Vv source")
DEF_ATTRIB(CVI_REQUIRES_TMPLOAD,
    ".tmp load must be included in packet")
DEF_ATTRIB(CVI_PUMP_2X,
    "Goes through the pipeline twice")
DEF_ATTRIB(CVI_PUMP_4X,
    "Goes through the pipeline four times")
DEF_ATTRIB(CVI_GATHER,
    "CVI Gather operation")
DEF_ATTRIB(CVI_SCATTER,
    "CVI Scatter operation")
DEF_ATTRIB(CVI_SCATTER_RELEASE,
    "CVI Store Release for syncing of scatter instructions")
DEF_ATTRIB(CVI_GATHER_RELEASE,
    "CVI Store Release for syncing of gather instructions")
DEF_ATTRIB(CVI_TMP_DST,
    "CVI instruction that doesn't write a destination register")
DEF_ATTRIB(CVI_SCATTER_WORD_ACC,
    "CVI Scatter Word Accumulate (second pass through network)")
DEF_ATTRIB(CVI_SCATTER_ACC,
    "CVI Scatter Accumulate")

DEF_ATTRIB(CVI_GATHER_ADDR_2B, "CVI Scatter/Gather address is halfword")
DEF_ATTRIB(CVI_GATHER_ADDR_4B, "CVI Scatter/Gather address is word")

DEF_ATTRIB(VFETCH, "memory fetch op to L2 for a single vector")

DEF_ATTRIB(CVI_SLOT23,
    "This instruction can execute in slot 2 or slot 3<comma> "
    "even though it is a HVX instruction.")

DEF_ATTRIB(VTCM_ALLBANK_ACCESS,
    "This instruction allocates in all VTCM schedulers due to a region access.")

/* Change-of-flow attributes */
DEF_ATTRIB(JUMP, "This instruction is a Jump-type instruction")
DEF_ATTRIB(DIRECT, "This instruction uses an PC-relative immediate field")
DEF_ATTRIB(INDIRECT, "This instruction is a absolute register jump")
DEF_ATTRIB(CJUMP, "This is a conditional jump")
DEF_ATTRIB(CALL, "This instruction is a function call instruction")
DEF_ATTRIB(RET, "This instruction is a function return instruction")
DEF_ATTRIB(PERM, "This instruction is a permute instruction")
DEF_ATTRIB(COF, "This instruction is a change-of-flow instruction")
DEF_ATTRIB(CONDEXEC, "This instruction may be cancelled by a predicate")
DEF_ATTRIB(DOTOLD,
    "This instruction uses a predicate generated in a previous packet")
DEF_ATTRIB(DOTNEW,
    "This instruction uses a predicate generated in the same packet")
DEF_ATTRIB(DOTNEWVALUE,
    "This instruction uses a register value generated in the same packet")
DEF_ATTRIB(NEWCMPJUMP, "This instruction is a compound compare and jump")
DEF_ATTRIB(NVSTORE, "This instruction is a new-value store")
DEF_ATTRIB(MEMOP, "This instruction is a memop")

DEF_ATTRIB(ROPS_2, "Compound instruction worth 2 wimpy RISC-ops")
DEF_ATTRIB(ROPS_3, "Compound instruction worth 3 wimpy RISC-ops")


/* access to implicit registers */
DEF_ATTRIB(IMPLICIT_WRITES_LR, "This instruction writes the link register")
DEF_ATTRIB(IMPLICIT_READS_LR, "This instruction reads the link register")
DEF_ATTRIB(IMPLICIT_READS_LC0,
    "This instruction reads the loop count register for loop 0")
DEF_ATTRIB(IMPLICIT_READS_LC1,
    "This instruction reads the loop count register for loop 1")
DEF_ATTRIB(IMPLICIT_READS_SA0,
    "This instruction reads the start address register for loop 0")
DEF_ATTRIB(IMPLICIT_READS_SA1,
    "This instruction reads the start address register for loop 1")
DEF_ATTRIB(IMPLICIT_WRITES_PC,
    "This instruction writes the program counter")
DEF_ATTRIB(IMPLICIT_READS_PC,
    "This instruction reads the program counter")
DEF_ATTRIB(IMPLICIT_WRITES_SP,
    "This instruction writes the stack pointer register")
DEF_ATTRIB(IMPLICIT_READS_SP,
    "This instruction reads the stack pointer register")
DEF_ATTRIB(IMPLICIT_WRITES_FP,
    "This instruction writes the frame pointer register")
DEF_ATTRIB(IMPLICIT_READS_FP,
    "This instruction reads the frame pointer register")
DEF_ATTRIB(IMPLICIT_WRITES_GP,
    "This instruction writes the global pointer register")
DEF_ATTRIB(IMPLICIT_READS_GP,
    "This instruction reads the global pointer register")
DEF_ATTRIB(IMPLICIT_WRITES_LC0,
    "This instruction writes the Loop Count for loop 0")
DEF_ATTRIB(IMPLICIT_WRITES_LC1,
    "This instruction writes the Loop Count for loop 1")
DEF_ATTRIB(IMPLICIT_WRITES_EA,
    "This instruction writes the End Address")
DEF_ATTRIB(IMPLICIT_WRITES_SA0,
    "This instruction writes the Start Address for loop 0")
DEF_ATTRIB(IMPLICIT_WRITES_SA1,
    "This instruction writes the Start Address for loop 1")
DEF_ATTRIB(IMPLICIT_WRITES_R00, "This instruction writes Register 0")
DEF_ATTRIB(IMPLICIT_WRITES_P0, "This instruction writes Predicate 0")
DEF_ATTRIB(IMPLICIT_WRITES_P1, "This instruction writes Predicate 1")
DEF_ATTRIB(IMPLICIT_WRITES_P2, "This instruction writes Predicate 1")
DEF_ATTRIB(IMPLICIT_WRITES_P3, "This instruction may write Predicate 3")
DEF_ATTRIB(IMPLICIT_READS_R00, "This instruction reads Register 0")
DEF_ATTRIB(IMPLICIT_READS_P0, "This instruction reads Predicate 0")
DEF_ATTRIB(IMPLICIT_READS_P1, "This instruction reads Predicate 1")
DEF_ATTRIB(IMPLICIT_READS_P3, "This instruction reads Predicate 3")
DEF_ATTRIB(IMPLICIT_READS_Q3, "This instruction reads Vector Predicate 3")
DEF_ATTRIB(IMPLICIT_READS_CS,
    "This instruction reads the CS register corresponding with the specified "
    "M register")
DEF_ATTRIB(IMPLICIT_READS_FRAMEKEY,
    "This instruction reads the FRAMEKEY register")
DEF_ATTRIB(IMPLICIT_READS_FRAMELIMIT,
    "This instruction reads the FRAMELIMIT register")
DEF_ATTRIB(IMPLICIT_READS_ELR, "This instruction reads the ELR register")
DEF_ATTRIB(IMPLICIT_READS_SGP0, "This instruction reads the SGP0 register")
DEF_ATTRIB(IMPLICIT_READS_SGP1, "This instruction reads the SGP1 register")
DEF_ATTRIB(IMPLICIT_WRITES_SGP0, "This instruction reads the SGP0 register")
DEF_ATTRIB(IMPLICIT_WRITES_SGP1, "This instruction reads the SGP1 register")
DEF_ATTRIB(IMPLICIT_WRITES_STID_PRIO_ANYTHREAD,
    "This instruction reads the STID PRIO register on any thread")
DEF_ATTRIB(IMPLICIT_WRITES_SRBIT,
    "This instruction writes the OVerFlow bit in the Status Register")
DEF_ATTRIB(IMPLICIT_WRITES_FPFLAGS,
    "This instruction may write the Floating Point Flags in SR")
DEF_ATTRIB(IMPLICIT_WRITES_LPCFG,
    "This instruction writes the Loop Config bits in the Status Register")
DEF_ATTRIB(IMPLICIT_WRITES_CVBITS,
    "This instruction writes the Carry and Signed Overflow flags "
    "in the Status Register")
DEF_ATTRIB(IMPLICIT_READS_FPRND,
    "This instruction may read the Floating Point Rounding Mode configuration")
DEF_ATTRIB(IMPLICIT_READS_SSR, "This instruction may read SSR values")
DEF_ATTRIB(IMPLICIT_READS_CCR, "This instruction may read CCR values")
DEF_ATTRIB(IMPLICIT_WRITES_CCR, "This instruction may write CCR values")
DEF_ATTRIB(IMPLICIT_WRITES_SSR, "This instruction may write SSR values")
DEF_ATTRIB(IMPLICIT_READS_GELR, "This instruction may read GELR values")
DEF_ATTRIB(IMPLICIT_READS_GEVB, "This instruction may read GEVB values")
DEF_ATTRIB(IMPLICIT_READS_GSR, "This instruction may read GSR values")
DEF_ATTRIB(IMPLICIT_READS_GOSP, "This instruction may read GOSP values")
DEF_ATTRIB(IMPLICIT_WRITES_GELR, "This instruction may write GELR values")
DEF_ATTRIB(IMPLICIT_WRITES_GSR, "This instruction may write GSR values")
DEF_ATTRIB(IMPLICIT_WRITES_GOSP, "This instruction may write GOSP values")
DEF_ATTRIB(IMPLICIT_READS_IPENDAD_IPEND,
    "This instruction may read the IPEND field")
DEF_ATTRIB(IMPLICIT_WRITES_IPENDAD_IPEND,
    "This instruction may write the IPEND field")
DEF_ATTRIB(IMPLICIT_READS_IPENDAD_IAD,
    "This instruction may read the IAD field")
DEF_ATTRIB(IMPLICIT_WRITES_IPENDAD_IAD,
    "This instruction may read write IAD field")
DEF_ATTRIB(IMPLICIT_WRITES_IMASK_ANYTHREAD,
    "This instruction may write IMASK on any thread")
DEF_ATTRIB(IMPLICIT_READS_IMASK_ANYTHREAD,
    "This instruction may write IMASK on any thread")
DEF_ATTRIB(IMPLICIT_READS_SYSCFG_K0LOCK,
    "This instruction may reads SYSCFG.K0LOCK value")
DEF_ATTRIB(IMPLICIT_WRITES_SYSCFG_K0LOCK,
    "This instruction may write SYSCFG.K0LOCK value")
DEF_ATTRIB(IMPLICIT_READS_SYSCFG_TLBLOCK,
    "This instruction may reads SYSCFG.TLBLOCK value")
DEF_ATTRIB(IMPLICIT_WRITES_SYSCFG_TLBLOCK,
    "This instruction may write SYSCFG.TLBLOCK value")
DEF_ATTRIB(IMPLICIT_WRITES_SYSCFG_GCA,
    "This instruction may write SYSCFG.GCA value")
DEF_ATTRIB(IMPLICIT_READS_SYSCFG_GCA,
    "This instruction may read SYSCFG.GCA value")
DEF_ATTRIB(IMPLICIT_WRITES_USR_PFA, "This instruction may write USR_PFA value")

/* Other things the instruction does */
DEF_ATTRIB(ACC, "This instruction has a multiply")
DEF_ATTRIB(MPY, "This instruction has a multiply")
DEF_ATTRIB(SATURATE, "This instruction does signed saturation")
DEF_ATTRIB(USATURATE, "This instruction does unsigned saturation")
DEF_ATTRIB(CIRCADDR, "This instruction uses circular addressing mode")
DEF_ATTRIB(BREVADDR, "This instruction uses bit reverse addressing mode")
DEF_ATTRIB(BIDIRSHIFTL,
    "This instruction uses a bidirectional shift left. If the shift amount "
    "is positive<comma> shift left by the shift amount.  If the shift amount "
    "is negative<comma> shift right by the negation of the shift amount.")
DEF_ATTRIB(BIDIRSHIFTR,
    "This instruction uses a bidirectional shift right. If the shift amount "
    "is positive<comma> shift right by the shift amount.  If the shift amount "
    "is negative<comma> shift left by the negation of the shift amount.")
DEF_ATTRIB(BRANCHADDER,
    "This instruction contains a PC-plus-immediate operation.")
DEF_ATTRIB(CRSLOT23,
    "This instruction can execute in slot 2 or slot 3<comma> "
    "even though it is a CR instruction.")
DEF_ATTRIB(COMMUTES, "The operation is communitive")
DEF_ATTRIB(DEALLOCRET, "This instruction is a dealloc_return")
DEF_ATTRIB(DEALLOCFRAME, "This instruction is a deallocframe")

/* Instruction Types */

DEF_ATTRIB(IT_ALU, "This instruction is an ALU type")
DEF_ATTRIB(IT_ALU_ADDSUB, "This instruction is an ALU add or subtract type")
DEF_ATTRIB(IT_ALU_MINMAX, "This instruction is an ALU MIN or MAX type")
DEF_ATTRIB(IT_ALU_MOVE, "This instruction is an ALU data movement type")
DEF_ATTRIB(IT_ALU_LOGICAL, "This instruction is an ALU logical operation type")
DEF_ATTRIB(IT_ALU_SHIFT, "This instruction is an ALU shift operation type")
DEF_ATTRIB(IT_ALU_SHIFT_AND_OP,
    "This instruction is an ALU shift and additional operation type")
DEF_ATTRIB(IT_ALU_CMP, "This instruction is an ALU compare operation type")

DEF_ATTRIB(IT_LOAD, "This instruction loads from memory")
DEF_ATTRIB(IT_STORE, "This instruction loads from memory")

DEF_ATTRIB(IT_MPY, "This instruction is a Multiply type")
DEF_ATTRIB(IT_MPY_32, "This instruction is a 32-bit Multiply type")

DEF_ATTRIB(IT_COF, "This instruction is a Change-of-flow type")
DEF_ATTRIB(IT_HWLOOP, "This instruction sets up hardware loop registers")

DEF_ATTRIB(IT_MISC, "This instruction is a misc instruction type")

DEF_ATTRIB(IT_NOP, "This instruction is a nop instruction")
DEF_ATTRIB(IT_EXTENDER, "This instruction is a constant extender instruction")


/* Exceptions the instruction can generate */

DEF_ATTRIB(EXCEPTION_TLB,
    "This instruction can generate a TLB Miss Exception")
DEF_ATTRIB(EXCEPTION_ACCESS,
    "This instruction can generate an Access Rights Violation Exception")
DEF_ATTRIB(EXCEPTION_SWI,
    "This instruction generates a Software Interrupt (trap) type exception")


/* Documentation Notes */
DEF_ATTRIB(NOTE_ARCHV2,
    "This instruction is only available in the V2 architecture")

DEF_ATTRIB(NOTE_PACKET_PC,
    "The PC value is the address of the start of the packet")

DEF_ATTRIB(NOTE_PACKET_NPC,
    "The Next PC value is the address immediately following the last "
    "instruction in the packet containing this instruction.")

DEF_ATTRIB(NOTE_CONDITIONAL,
    "This instruction can be conditionally executed based on the value of "
    "a predicate register. If the instruction is preceded by 'if Pn'<comma> "
    "then the instruction only executes if the least-significant bit of the "
    "predicate register is 1. Similarly<comma> if the instruction is preceded "
    "by 'if !Pn'<comma> then the instruction is executed only if the least-"
    "significant bit of Pn is 0.")

DEF_ATTRIB(NOTE_NEWVAL_SLOT0,
    "Forms of this instruction which use a new-value operand produced in "
    "the packet must execute on slot 0.")

DEF_ATTRIB(NOTE_RELATIVE_ADDRESS,
    "A PC-relative address is formed by taking the decoded immediate value "
    "and adding it to the current PC value.")

DEF_ATTRIB(NOTE_LA_RESTRICT,
    "This instruction cannot execute in the last address of a hardware loop.")

DEF_ATTRIB(NOTE_OOBVSHIFT,
    "If the number of bits to be shifted is greater than the width of the "
    "vector element<comma> the result is either all sign-bits (for arithmetic "
    "right shifts) or all zeros for logical and left shifts.")
DEF_ATTRIB(NOTE_BIDIRSHIFT,
    "This instruction uses a bidirectional shift. If the shift amount is "
    "negative<comma> the direction of the shift is reversed.")

DEF_ATTRIB(NOTE_CVFLAGS,
    "This instruction sets the Carry and Overflow flags in USR.")
DEF_ATTRIB(NOTE_SR_OVF_WHEN_SATURATING,
    "If saturation occurs during execution of this instruction (a result is "
    "clamped to either maximum or minimum values)<comma> then the OVF bit in "
    "the Status Register is set. OVF will remain set until explicitly cleared "
    "by a transfer to SR.")
DEF_ATTRIB(NOTE_PRIV,
    "This is a monitor-level feature. If performed in User or Guest "
    "mode<comma> a privilege error exception will occur.")
DEF_ATTRIB(NOTE_GUEST,
    "This is a guest-level feature. If performed in User mode<comma> a "
    "privilege error exception will occur.")
DEF_ATTRIB(NOTE_NOPACKET,
    "This is a solo instruction. It must not be grouped with other "
    "instructions in a packet.")
DEF_ATTRIB(NOTE_AXOK,
    "This instruction may only be grouped with ALU32 or non-floating-point "
    "XTYPE instructions.")
DEF_ATTRIB(NOTE_NOSLOT1,
    "A packet containing this instruction must have slot 1 either empty or "
    "executing a NOP.")
DEF_ATTRIB(NOTE_SLOT1_AOK,
    "A packet containing this instruction must have slot 1 either empty or "
    "executing an ALU32 instruction.")
DEF_ATTRIB(NOTE_NOSLOT01,
    "A packet containing this instruction must have both slot 1 and slot 2 "
    "either empty or executing a NOP.")
DEF_ATTRIB(NOTE_NEEDS_MEMLD, "Must be grouped with a memory load instruction.")
DEF_ATTRIB(NOTE_LATEPRED,
    "The predicate generated by this instruction can not be used as a .new "
    "predicate<comma> nor can it be automatically ANDed with another "
    "predicate.")
DEF_ATTRIB(NOTE_COMPAT_ACCURACY,
     "This instruction provides a certain amount of accuracy. In future "
     "versions the accuracy may increase. For future compatibility<comma> "
     "dependence on exact values must be avoided.")
DEF_ATTRIB(NOTE_NVSLOT0,
    "This instruction can execute only in slot 0<comma> even though it is an "
    "ST instruction.")
DEF_ATTRIB(NOTE_DEPRECATED,
    "This instruction will be deprecated in a future version.")
DEF_ATTRIB(NOTE_NOISTARIV1, "This may not work correctly in Istari V1.")
DEF_ATTRIB(NOTE_NONAPALIV1, "This may not work correctly in Napali V1.")
DEF_ATTRIB(NOTE_BADTAG_UNDEF,
    "Results are undefined if a tag read or write addresses a non-present "
    "set or way.")
DEF_ATTRIB(NOTE_NOSLOT2_MPY,
    "A packet with this instruction cannot have a slot 2 multiply instruction.")
DEF_ATTRIB(NOTE_HVX_ONLY,
    "This instruction is only available on a core with HVX.")

DEF_ATTRIB(NOTE_NOCOF_RESTRICT,
    "This instruction cannot be grouped in a packet with any program flow "
    "instructions.")
DEF_ATTRIB(NOTE_BRANCHADDER_MAX1,
    "A maximum of one PC-plus-offset calculation is allowed per packet. "
    "Loop instructions count towards this maximum.")

DEF_ATTRIB(NOTE_CRSLOT23,
    "This instruction may execute on either slot2 or slot3<comma> even though "
    "it is a CR-type")
DEF_ATTRIB(NOTE_EXTENSION_AUDIO,
    "This instruction can only execute on a core with the Hexagon audio "
    "extensions")


/* V6 MMVector Notes for Documentation */
DEF_ATTRIB(NOTE_ANY_RESOURCE, "This instruction can use any HVX resource.")
DEF_ATTRIB(NOTE_ANY2_RESOURCE,
    "This instruction uses any pair of the HVX resources (both multiply or "
    "shift/permute).")
DEF_ATTRIB(NOTE_PERMUTE_RESOURCE,
    "This instruction uses the HVX permute resource.")
DEF_ATTRIB(NOTE_SHIFT_RESOURCE,
    "This instruction uses the HVX shift resource.")
DEF_ATTRIB(NOTE_MPY_RESOURCE,
    "This instruction uses a HVX multiply resource.")
DEF_ATTRIB(NOTE_MPYDV_RESOURCE,
    "This instruction uses both HVX multiply resources.")
DEF_ATTRIB(NOTE_NT_VMEM,
    "An optional \"non-temporal\" hint to the micro-architecture can be "
    "specified to indicate the data has no reuse.")
DEF_ATTRIB(NOTE_ALL_RESOURCE, "This instruction uses all HVX resources.")
DEF_ATTRIB(NOTE_VMEM,
    "Immediates used in address computation are specificed in multiples of "
    "vector length.")
DEF_ATTRIB(NOTE_ANY_VS_VX_RESOURCE,
    "A double capacity variation of the instruction that consumes two "
    "resources: multiply and shift or multiply and permute.")

DEF_ATTRIB(NOTE_RT8,
    "Input scalar register Rt is limited to registers 0 through 7")

DEF_ATTRIB(NOTE_MX, "This is in-memory matrix multiply instruction.")

/* Restrictions to make note of */
DEF_ATTRIB(RESTRICT_LOOP_LA, "Cannot be in the last packet of a loop")
DEF_ATTRIB(RESTRICT_NEEDS_MEMLD,
    "Must be grouped with a memory load instruction")
DEF_ATTRIB(RESTRICT_COF_MAX1,
    "A maximum of one change-of-flow is allowed per packet")
DEF_ATTRIB(RESTRICT_NOPACKET, "This instruction is not allowed in a packet")
DEF_ATTRIB(RESTRICT_NOSRMOVE,
    "Do not write SR in the same packet as this instruction")
DEF_ATTRIB(RESTRICT_SLOT0ONLY, "This instruction must execute on slot0")
DEF_ATTRIB(RESTRICT_SLOT1ONLY, "This instruction must execute on slot1")
DEF_ATTRIB(RESTRICT_SLOT2ONLY, "This instruction must execute on slot2")
DEF_ATTRIB(RESTRICT_SLOT3ONLY, "This instruction must execute on slot3")
DEF_ATTRIB(RESTRICT_NOSLOT2_MPY,
    "A packet with this instruction cannot have a slot 2 multiply")
DEF_ATTRIB(RESTRICT_NOSLOT1, "No slot 1 instruction allowed in parallel")
DEF_ATTRIB(RESTRICT_SLOT1_AOK, "Slot 1 instruction must be empty or an A-type")
DEF_ATTRIB(RESTRICT_NOSLOT01, "No slot 0 or 1 instructions allowed in parallel")
DEF_ATTRIB(RESTRICT_NOSLOT1_STORE,
    "A packet containing this instruction must not have a store in slot 1.")
DEF_ATTRIB(RESTRICT_NOSLOT0_LOAD,
    "A packet containing this instruction must not have a store in slot 1.")
DEF_ATTRIB(RESTRICT_NOCOF,
    "This instruction cannot be grouped in a packet with any program flow "
    "instructions.")
DEF_ATTRIB(RESTRICT_BRANCHADDER_MAX1,
    "A maximum of one PC-plus-offset calculation is allowed per packet. Loop "
    "setup instructions count towards this maximum.")
DEF_ATTRIB(RESTRICT_PREFERSLOT0,
    "Try to encode this instruction into slot 0 if possible.")
DEF_ATTRIB(RESTRICT_SINGLE_MEM_FIRST,
    "Packets with single memory operations must have the memory operation "
    "encoded last. The assembler will automatically do this by default.")
DEF_ATTRIB(RESTRICT_PACKET_AXOK,
    "This instruction may exist in a packet where the other instructions are "
    "A-type or X-type")
DEF_ATTRIB(RESTRICT_PACKET_SOMEREGS_OK,
    "This instruction has relaxed grouping rules for some registers")
DEF_ATTRIB(RESTRICT_LATEPRED,
    "The predicate generated by this instruction can not be used as a .new "
    "predicate<comma> nor can it be automatically ANDed with another "
    "predicate.")

DEF_ATTRIB(PAIR_1OF2,
    "For assembler. First instruction of pair that must be in packet.")
DEF_ATTRIB(PAIR_2OF2,
    "For assembler. Second instruction of pair that must be in packet.")

/* Performance based preferences */
DEF_ATTRIB(PREFER_SLOT3,
    "Complex XU: prevent xu competition by prefering slot3")

DEF_ATTRIB(RELAX_COF_1ST,
    "This branch instruction can be paired with certain other branch "
    "instructions<comma> and can be the first in assembly order")
DEF_ATTRIB(RELAX_COF_2ND,
    "This branch instruction can be paired with certain other branch "
    "instructions<comma> and can be the second in assembly order")

DEF_ATTRIB(ICOP, "This instruction is an instruction cache op")

DEF_ATTRIB(INTRINSIC_RETURNS_UNSIGNED,
    "The intrinsic for this instruction returns an unsigned result.")

DEF_ATTRIB(PRED_BIT_1, "The branch uses bit 1 as the prediction bit")
DEF_ATTRIB(PRED_BIT_4, "The branch uses bit 4 as the prediction bit")
DEF_ATTRIB(PRED_BIT_8, "The branch uses bit 8 as the prediction bit")
DEF_ATTRIB(PRED_BIT_12, "The branch uses bit 12 as the prediction bit")
DEF_ATTRIB(PRED_BIT_13, "The branch uses bit 13 as the prediction bit")
DEF_ATTRIB(PRED_BIT_7, "The branch uses bit 7 as the prediction bit")
DEF_ATTRIB(HWLOOP0_SETUP, "Instruction sets up HW loop0")
DEF_ATTRIB(HWLOOP1_SETUP, "Instruction sets up HW loop1")
DEF_ATTRIB(HWLOOP0_END, "Instruction ends HW loop0")
DEF_ATTRIB(HWLOOP1_END, "Instruction ends HW loop1")
DEF_ATTRIB(RET_TYPE, "Instruction is a return type")
DEF_ATTRIB(HINTJR, "Instruction is a hintjr type")
DEF_ATTRIB(DCZEROA, "Instruction is dczeroa type")
DEF_ATTRIB(ICTAGOP, "Instruction is ictag op type ictagr ictagw icdatar")
DEF_ATTRIB(ICFLUSHOP, "Instruction is icflush op type icinva icinvidx ickill")
DEF_ATTRIB(DCFLUSHOP,
    "Instruction is dcflush op type dckill dccleana dccleanidx dccleaninva "
    "dccleaninvidx dcinva dcinvidx")
DEF_ATTRIB(DCTAGOP, "Instruction is dctag op type dctagr dctagw")
DEF_ATTRIB(L2FLUSHOP,
    "Instruction is l2flush op type l2kill l2cleaninvidx l2cleanidx l2invidx")
DEF_ATTRIB(L2TAGOP, "Instruction is l2tag op type l2tagr l2tagw")
DEF_ATTRIB(DCFETCH, "Instruction is dcfetch type")
DEF_ATTRIB(BIMODAL_BRANCH, "Updates the bimodal branch predictor")

DEF_ATTRIB(VECINSN, "Long Vector Instruction")
DEF_ATTRIB(MEMSIZE_32B, "Memory width is 32 bytes")
DEF_ATTRIB(FOUR_PHASE, "Four Phase Instruction")
DEF_ATTRIB(L2FETCH, "Instruction is l2fetch type")

DEF_ATTRIB(PREDUSE_BSB,
    "Instructions that consume predicates and need to go BSB due to late "
    "predicate generation by the previous instruction")
DEF_ATTRIB(ICINVA, "Instruction is icinva")
DEF_ATTRIB(DCCLEANINVA, "Instruction is dccleaninva")

DEF_ATTRIB(EXTENSION_AUDIO, "Instruction is an audio extension")

DEF_ATTRIB(MEMCPY, "This instruction is a memcpy or dma-type instruction")
DEF_ATTRIB(NO_INTRINSIC, "Don't generate an intrisic for this instruciton")

DEF_ATTRIB(NO_XML, "Don't generate a XML docs for this instruction")

DEF_ATTRIB(DMA,  "User-DMA instruction")

/* Keep this as the last attribute: */
DEF_ATTRIB(ZZ_LASTATTRIB, "Last attribute in the file")

