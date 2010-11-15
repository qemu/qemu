/* Print mips instructions for GDB, the GNU debugger, or for objdump.
   Copyright 1989, 1991, 1992, 1993, 1994, 1995, 1996, 1997, 1998, 1999,
   2000, 2001, 2002, 2003
   Free Software Foundation, Inc.
   Contributed by Nobuyuki Hikichi(hikichi@sra.co.jp).

This file is part of GDB, GAS, and the GNU binutils.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>.  */

#include "dis-asm.h"

/* mips.h.  Mips opcode list for GDB, the GNU debugger.
   Copyright 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2000, 2001, 2002, 2003
   Free Software Foundation, Inc.
   Contributed by Ralph Campbell and OSF
   Commented and modified by Ian Lance Taylor, Cygnus Support

This file is part of GDB, GAS, and the GNU binutils.

GDB, GAS, and the GNU binutils are free software; you can redistribute
them and/or modify them under the terms of the GNU General Public
License as published by the Free Software Foundation; either version
1, or (at your option) any later version.

GDB, GAS, and the GNU binutils are distributed in the hope that they
will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See
the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this file; see the file COPYING.  If not,
see <http://www.gnu.org/licenses/>.  */

/* These are bit masks and shift counts to use to access the various
   fields of an instruction.  To retrieve the X field of an
   instruction, use the expression
	(i >> OP_SH_X) & OP_MASK_X
   To set the same field (to j), use
	i = (i &~ (OP_MASK_X << OP_SH_X)) | (j << OP_SH_X)

   Make sure you use fields that are appropriate for the instruction,
   of course.

   The 'i' format uses OP, RS, RT and IMMEDIATE.

   The 'j' format uses OP and TARGET.

   The 'r' format uses OP, RS, RT, RD, SHAMT and FUNCT.

   The 'b' format uses OP, RS, RT and DELTA.

   The floating point 'i' format uses OP, RS, RT and IMMEDIATE.

   The floating point 'r' format uses OP, FMT, FT, FS, FD and FUNCT.

   A breakpoint instruction uses OP, CODE and SPEC (10 bits of the
   breakpoint instruction are not defined; Kane says the breakpoint
   code field in BREAK is 20 bits; yet MIPS assemblers and debuggers
   only use ten bits).  An optional two-operand form of break/sdbbp
   allows the lower ten bits to be set too, and MIPS32 and later
   architectures allow 20 bits to be set with a signal operand
   (using CODE20).

   The syscall instruction uses CODE20.

   The general coprocessor instructions use COPZ.  */

#define OP_MASK_OP		0x3f
#define OP_SH_OP		26
#define OP_MASK_RS		0x1f
#define OP_SH_RS		21
#define OP_MASK_FR		0x1f
#define OP_SH_FR		21
#define OP_MASK_FMT		0x1f
#define OP_SH_FMT		21
#define OP_MASK_BCC		0x7
#define OP_SH_BCC		18
#define OP_MASK_CODE		0x3ff
#define OP_SH_CODE		16
#define OP_MASK_CODE2		0x3ff
#define OP_SH_CODE2		6
#define OP_MASK_RT		0x1f
#define OP_SH_RT		16
#define OP_MASK_FT		0x1f
#define OP_SH_FT		16
#define OP_MASK_CACHE		0x1f
#define OP_SH_CACHE		16
#define OP_MASK_RD		0x1f
#define OP_SH_RD		11
#define OP_MASK_FS		0x1f
#define OP_SH_FS		11
#define OP_MASK_PREFX		0x1f
#define OP_SH_PREFX		11
#define OP_MASK_CCC		0x7
#define OP_SH_CCC		8
#define OP_MASK_CODE20		0xfffff /* 20 bit syscall/breakpoint code.  */
#define OP_SH_CODE20		6
#define OP_MASK_SHAMT		0x1f
#define OP_SH_SHAMT		6
#define OP_MASK_FD		0x1f
#define OP_SH_FD		6
#define OP_MASK_TARGET		0x3ffffff
#define OP_SH_TARGET		0
#define OP_MASK_COPZ		0x1ffffff
#define OP_SH_COPZ		0
#define OP_MASK_IMMEDIATE	0xffff
#define OP_SH_IMMEDIATE		0
#define OP_MASK_DELTA		0xffff
#define OP_SH_DELTA		0
#define OP_MASK_FUNCT		0x3f
#define OP_SH_FUNCT		0
#define OP_MASK_SPEC		0x3f
#define OP_SH_SPEC		0
#define OP_SH_LOCC              8       /* FP condition code.  */
#define OP_SH_HICC              18      /* FP condition code.  */
#define OP_MASK_CC              0x7
#define OP_SH_COP1NORM          25      /* Normal COP1 encoding.  */
#define OP_MASK_COP1NORM        0x1     /* a single bit.  */
#define OP_SH_COP1SPEC          21      /* COP1 encodings.  */
#define OP_MASK_COP1SPEC        0xf
#define OP_MASK_COP1SCLR        0x4
#define OP_MASK_COP1CMP         0x3
#define OP_SH_COP1CMP           4
#define OP_SH_FORMAT            21      /* FP short format field.  */
#define OP_MASK_FORMAT          0x7
#define OP_SH_TRUE              16
#define OP_MASK_TRUE            0x1
#define OP_SH_GE                17
#define OP_MASK_GE              0x01
#define OP_SH_UNSIGNED          16
#define OP_MASK_UNSIGNED        0x1
#define OP_SH_HINT              16
#define OP_MASK_HINT            0x1f
#define OP_SH_MMI               0       /* Multimedia (parallel) op.  */
#define OP_MASK_MMI             0x3f
#define OP_SH_MMISUB            6
#define OP_MASK_MMISUB          0x1f
#define OP_MASK_PERFREG		0x1f	/* Performance monitoring.  */
#define OP_SH_PERFREG		1
#define OP_SH_SEL		0	/* Coprocessor select field.  */
#define OP_MASK_SEL		0x7	/* The sel field of mfcZ and mtcZ.  */
#define OP_SH_CODE19		6       /* 19 bit wait code.  */
#define OP_MASK_CODE19		0x7ffff
#define OP_SH_ALN		21
#define OP_MASK_ALN		0x7
#define OP_SH_VSEL		21
#define OP_MASK_VSEL		0x1f
#define OP_MASK_VECBYTE		0x7	/* Selector field is really 4 bits,
					   but 0x8-0xf don't select bytes.  */
#define OP_SH_VECBYTE		22
#define OP_MASK_VECALIGN	0x7	/* Vector byte-align (alni.ob) op.  */
#define OP_SH_VECALIGN		21
#define OP_MASK_INSMSB		0x1f	/* "ins" MSB.  */
#define OP_SH_INSMSB		11
#define OP_MASK_EXTMSBD		0x1f	/* "ext" MSBD.  */
#define OP_SH_EXTMSBD		11

#define	OP_OP_COP0		0x10
#define	OP_OP_COP1		0x11
#define	OP_OP_COP2		0x12
#define	OP_OP_COP3		0x13
#define	OP_OP_LWC1		0x31
#define	OP_OP_LWC2		0x32
#define	OP_OP_LWC3		0x33	/* a.k.a. pref */
#define	OP_OP_LDC1		0x35
#define	OP_OP_LDC2		0x36
#define	OP_OP_LDC3		0x37	/* a.k.a. ld */
#define	OP_OP_SWC1		0x39
#define	OP_OP_SWC2		0x3a
#define	OP_OP_SWC3		0x3b
#define	OP_OP_SDC1		0x3d
#define	OP_OP_SDC2		0x3e
#define	OP_OP_SDC3		0x3f	/* a.k.a. sd */

/* MIPS DSP ASE */
#define OP_SH_DSPACC		11
#define OP_MASK_DSPACC  	0x3
#define OP_SH_DSPACC_S  	21
#define OP_MASK_DSPACC_S	0x3
#define OP_SH_DSPSFT		20
#define OP_MASK_DSPSFT  	0x3f
#define OP_SH_DSPSFT_7  	19
#define OP_MASK_DSPSFT_7	0x7f
#define OP_SH_SA3		21
#define OP_MASK_SA3		0x7
#define OP_SH_SA4		21
#define OP_MASK_SA4		0xf
#define OP_SH_IMM8		16
#define OP_MASK_IMM8		0xff
#define OP_SH_IMM10		16
#define OP_MASK_IMM10		0x3ff
#define OP_SH_WRDSP		11
#define OP_MASK_WRDSP		0x3f
#define OP_SH_RDDSP		16
#define OP_MASK_RDDSP		0x3f
#define OP_SH_BP		11
#define OP_MASK_BP		0x3

/* MIPS MT ASE */
#define OP_SH_MT_U		5
#define OP_MASK_MT_U		0x1
#define OP_SH_MT_H		4
#define OP_MASK_MT_H		0x1
#define OP_SH_MTACC_T		18
#define OP_MASK_MTACC_T		0x3
#define OP_SH_MTACC_D		13
#define OP_MASK_MTACC_D		0x3

#define	OP_OP_COP0		0x10
#define	OP_OP_COP1		0x11
#define	OP_OP_COP2		0x12
#define	OP_OP_COP3		0x13
#define	OP_OP_LWC1		0x31
#define	OP_OP_LWC2		0x32
#define	OP_OP_LWC3		0x33	/* a.k.a. pref */
#define	OP_OP_LDC1		0x35
#define	OP_OP_LDC2		0x36
#define	OP_OP_LDC3		0x37	/* a.k.a. ld */
#define	OP_OP_SWC1		0x39
#define	OP_OP_SWC2		0x3a
#define	OP_OP_SWC3		0x3b
#define	OP_OP_SDC1		0x3d
#define	OP_OP_SDC2		0x3e
#define	OP_OP_SDC3		0x3f	/* a.k.a. sd */

/* Values in the 'VSEL' field.  */
#define MDMX_FMTSEL_IMM_QH	0x1d
#define MDMX_FMTSEL_IMM_OB	0x1e
#define MDMX_FMTSEL_VEC_QH	0x15
#define MDMX_FMTSEL_VEC_OB	0x16

/* UDI */
#define OP_SH_UDI1		6
#define OP_MASK_UDI1		0x1f
#define OP_SH_UDI2		6
#define OP_MASK_UDI2		0x3ff
#define OP_SH_UDI3		6
#define OP_MASK_UDI3		0x7fff
#define OP_SH_UDI4		6
#define OP_MASK_UDI4		0xfffff
/* This structure holds information for a particular instruction.  */

struct mips_opcode
{
  /* The name of the instruction.  */
  const char *name;
  /* A string describing the arguments for this instruction.  */
  const char *args;
  /* The basic opcode for the instruction.  When assembling, this
     opcode is modified by the arguments to produce the actual opcode
     that is used.  If pinfo is INSN_MACRO, then this is 0.  */
  unsigned long match;
  /* If pinfo is not INSN_MACRO, then this is a bit mask for the
     relevant portions of the opcode when disassembling.  If the
     actual opcode anded with the match field equals the opcode field,
     then we have found the correct instruction.  If pinfo is
     INSN_MACRO, then this field is the macro identifier.  */
  unsigned long mask;
  /* For a macro, this is INSN_MACRO.  Otherwise, it is a collection
     of bits describing the instruction, notably any relevant hazard
     information.  */
  unsigned long pinfo;
  /* A collection of additional bits describing the instruction. */
  unsigned long pinfo2;
  /* A collection of bits describing the instruction sets of which this
     instruction or macro is a member. */
  unsigned long membership;
};

/* These are the characters which may appear in the args field of an
   instruction.  They appear in the order in which the fields appear
   when the instruction is used.  Commas and parentheses in the args
   string are ignored when assembling, and written into the output
   when disassembling.

   Each of these characters corresponds to a mask field defined above.

   "<" 5 bit shift amount (OP_*_SHAMT)
   ">" shift amount between 32 and 63, stored after subtracting 32 (OP_*_SHAMT)
   "a" 26 bit target address (OP_*_TARGET)
   "b" 5 bit base register (OP_*_RS)
   "c" 10 bit breakpoint code (OP_*_CODE)
   "d" 5 bit destination register specifier (OP_*_RD)
   "h" 5 bit prefx hint (OP_*_PREFX)
   "i" 16 bit unsigned immediate (OP_*_IMMEDIATE)
   "j" 16 bit signed immediate (OP_*_DELTA)
   "k" 5 bit cache opcode in target register position (OP_*_CACHE)
       Also used for immediate operands in vr5400 vector insns.
   "o" 16 bit signed offset (OP_*_DELTA)
   "p" 16 bit PC relative branch target address (OP_*_DELTA)
   "q" 10 bit extra breakpoint code (OP_*_CODE2)
   "r" 5 bit same register used as both source and target (OP_*_RS)
   "s" 5 bit source register specifier (OP_*_RS)
   "t" 5 bit target register (OP_*_RT)
   "u" 16 bit upper 16 bits of address (OP_*_IMMEDIATE)
   "v" 5 bit same register used as both source and destination (OP_*_RS)
   "w" 5 bit same register used as both target and destination (OP_*_RT)
   "U" 5 bit same destination register in both OP_*_RD and OP_*_RT
       (used by clo and clz)
   "C" 25 bit coprocessor function code (OP_*_COPZ)
   "B" 20 bit syscall/breakpoint function code (OP_*_CODE20)
   "J" 19 bit wait function code (OP_*_CODE19)
   "x" accept and ignore register name
   "z" must be zero register
   "K" 5 bit Hardware Register (rdhwr instruction) (OP_*_RD)
   "+A" 5 bit ins/ext/dins/dext/dinsm/dextm position, which becomes
        LSB (OP_*_SHAMT).
	Enforces: 0 <= pos < 32.
   "+B" 5 bit ins/dins size, which becomes MSB (OP_*_INSMSB).
	Requires that "+A" or "+E" occur first to set position.
	Enforces: 0 < (pos+size) <= 32.
   "+C" 5 bit ext/dext size, which becomes MSBD (OP_*_EXTMSBD).
	Requires that "+A" or "+E" occur first to set position.
	Enforces: 0 < (pos+size) <= 32.
	(Also used by "dext" w/ different limits, but limits for
	that are checked by the M_DEXT macro.)
   "+E" 5 bit dinsu/dextu position, which becomes LSB-32 (OP_*_SHAMT).
	Enforces: 32 <= pos < 64.
   "+F" 5 bit "dinsm/dinsu" size, which becomes MSB-32 (OP_*_INSMSB).
	Requires that "+A" or "+E" occur first to set position.
	Enforces: 32 < (pos+size) <= 64.
   "+G" 5 bit "dextm" size, which becomes MSBD-32 (OP_*_EXTMSBD).
	Requires that "+A" or "+E" occur first to set position.
	Enforces: 32 < (pos+size) <= 64.
   "+H" 5 bit "dextu" size, which becomes MSBD (OP_*_EXTMSBD).
	Requires that "+A" or "+E" occur first to set position.
	Enforces: 32 < (pos+size) <= 64.

   Floating point instructions:
   "D" 5 bit destination register (OP_*_FD)
   "M" 3 bit compare condition code (OP_*_CCC) (only used for mips4 and up)
   "N" 3 bit branch condition code (OP_*_BCC) (only used for mips4 and up)
   "S" 5 bit fs source 1 register (OP_*_FS)
   "T" 5 bit ft source 2 register (OP_*_FT)
   "R" 5 bit fr source 3 register (OP_*_FR)
   "V" 5 bit same register used as floating source and destination (OP_*_FS)
   "W" 5 bit same register used as floating target and destination (OP_*_FT)

   Coprocessor instructions:
   "E" 5 bit target register (OP_*_RT)
   "G" 5 bit destination register (OP_*_RD)
   "H" 3 bit sel field for (d)mtc* and (d)mfc* (OP_*_SEL)
   "P" 5 bit performance-monitor register (OP_*_PERFREG)
   "e" 5 bit vector register byte specifier (OP_*_VECBYTE)
   "%" 3 bit immediate vr5400 vector alignment operand (OP_*_VECALIGN)
   see also "k" above
   "+D" Combined destination register ("G") and sel ("H") for CP0 ops,
	for pretty-printing in disassembly only.

   Macro instructions:
   "A" General 32 bit expression
   "I" 32 bit immediate (value placed in imm_expr).
   "+I" 32 bit immediate (value placed in imm2_expr).
   "F" 64 bit floating point constant in .rdata
   "L" 64 bit floating point constant in .lit8
   "f" 32 bit floating point constant
   "l" 32 bit floating point constant in .lit4

   MDMX instruction operands (note that while these use the FP register
   fields, they accept both $fN and $vN names for the registers):
   "O"	MDMX alignment offset (OP_*_ALN)
   "Q"	MDMX vector/scalar/immediate source (OP_*_VSEL and OP_*_FT)
   "X"	MDMX destination register (OP_*_FD)
   "Y"	MDMX source register (OP_*_FS)
   "Z"	MDMX source register (OP_*_FT)

   DSP ASE usage:
   "2" 2 bit unsigned immediate for byte align (OP_*_BP)
   "3" 3 bit unsigned immediate (OP_*_SA3)
   "4" 4 bit unsigned immediate (OP_*_SA4)
   "5" 8 bit unsigned immediate (OP_*_IMM8)
   "6" 5 bit unsigned immediate (OP_*_RS)
   "7" 2 bit dsp accumulator register (OP_*_DSPACC)
   "8" 6 bit unsigned immediate (OP_*_WRDSP)
   "9" 2 bit dsp accumulator register (OP_*_DSPACC_S)
   "0" 6 bit signed immediate (OP_*_DSPSFT)
   ":" 7 bit signed immediate (OP_*_DSPSFT_7)
   "'" 6 bit unsigned immediate (OP_*_RDDSP)
   "@" 10 bit signed immediate (OP_*_IMM10)

   MT ASE usage:
   "!" 1 bit usermode flag (OP_*_MT_U)
   "$" 1 bit load high flag (OP_*_MT_H)
   "*" 2 bit dsp/smartmips accumulator register (OP_*_MTACC_T)
   "&" 2 bit dsp/smartmips accumulator register (OP_*_MTACC_D)
   "g" 5 bit coprocessor 1 and 2 destination register (OP_*_RD)
   "+t" 5 bit coprocessor 0 destination register (OP_*_RT)
   "+T" 5 bit coprocessor 0 destination register (OP_*_RT) - disassembly only

   UDI immediates:
   "+1" UDI immediate bits 6-10
   "+2" UDI immediate bits 6-15
   "+3" UDI immediate bits 6-20
   "+4" UDI immediate bits 6-25

   Other:
   "()" parens surrounding optional value
   ","  separates operands
   "[]" brackets around index for vector-op scalar operand specifier (vr5400)
   "+"  Start of extension sequence.

   Characters used so far, for quick reference when adding more:
   "234567890"
   "%[]<>(),+:'@!$*&"
   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
   "abcdefghijklopqrstuvwxz"

   Extension character sequences used so far ("+" followed by the
   following), for quick reference when adding more:
   "1234"
   "ABCDEFGHIT"
   "t"
*/

/* These are the bits which may be set in the pinfo field of an
   instructions, if it is not equal to INSN_MACRO.  */

/* Modifies the general purpose register in OP_*_RD.  */
#define INSN_WRITE_GPR_D            0x00000001
/* Modifies the general purpose register in OP_*_RT.  */
#define INSN_WRITE_GPR_T            0x00000002
/* Modifies general purpose register 31.  */
#define INSN_WRITE_GPR_31           0x00000004
/* Modifies the floating point register in OP_*_FD.  */
#define INSN_WRITE_FPR_D            0x00000008
/* Modifies the floating point register in OP_*_FS.  */
#define INSN_WRITE_FPR_S            0x00000010
/* Modifies the floating point register in OP_*_FT.  */
#define INSN_WRITE_FPR_T            0x00000020
/* Reads the general purpose register in OP_*_RS.  */
#define INSN_READ_GPR_S             0x00000040
/* Reads the general purpose register in OP_*_RT.  */
#define INSN_READ_GPR_T             0x00000080
/* Reads the floating point register in OP_*_FS.  */
#define INSN_READ_FPR_S             0x00000100
/* Reads the floating point register in OP_*_FT.  */
#define INSN_READ_FPR_T             0x00000200
/* Reads the floating point register in OP_*_FR.  */
#define INSN_READ_FPR_R		    0x00000400
/* Modifies coprocessor condition code.  */
#define INSN_WRITE_COND_CODE        0x00000800
/* Reads coprocessor condition code.  */
#define INSN_READ_COND_CODE         0x00001000
/* TLB operation.  */
#define INSN_TLB                    0x00002000
/* Reads coprocessor register other than floating point register.  */
#define INSN_COP                    0x00004000
/* Instruction loads value from memory, requiring delay.  */
#define INSN_LOAD_MEMORY_DELAY      0x00008000
/* Instruction loads value from coprocessor, requiring delay.  */
#define INSN_LOAD_COPROC_DELAY	    0x00010000
/* Instruction has unconditional branch delay slot.  */
#define INSN_UNCOND_BRANCH_DELAY    0x00020000
/* Instruction has conditional branch delay slot.  */
#define INSN_COND_BRANCH_DELAY      0x00040000
/* Conditional branch likely: if branch not taken, insn nullified.  */
#define INSN_COND_BRANCH_LIKELY	    0x00080000
/* Moves to coprocessor register, requiring delay.  */
#define INSN_COPROC_MOVE_DELAY      0x00100000
/* Loads coprocessor register from memory, requiring delay.  */
#define INSN_COPROC_MEMORY_DELAY    0x00200000
/* Reads the HI register.  */
#define INSN_READ_HI		    0x00400000
/* Reads the LO register.  */
#define INSN_READ_LO		    0x00800000
/* Modifies the HI register.  */
#define INSN_WRITE_HI		    0x01000000
/* Modifies the LO register.  */
#define INSN_WRITE_LO		    0x02000000
/* Takes a trap (easier to keep out of delay slot).  */
#define INSN_TRAP                   0x04000000
/* Instruction stores value into memory.  */
#define INSN_STORE_MEMORY	    0x08000000
/* Instruction uses single precision floating point.  */
#define FP_S			    0x10000000
/* Instruction uses double precision floating point.  */
#define FP_D			    0x20000000
/* Instruction is part of the tx39's integer multiply family.    */
#define INSN_MULT                   0x40000000
/* Instruction synchronize shared memory.  */
#define INSN_SYNC		    0x80000000

/* These are the bits which may be set in the pinfo2 field of an
   instruction. */

/* Instruction is a simple alias (I.E. "move" for daddu/addu/or) */
#define	INSN2_ALIAS		    0x00000001
/* Instruction reads MDMX accumulator. */
#define INSN2_READ_MDMX_ACC	    0x00000002
/* Instruction writes MDMX accumulator. */
#define INSN2_WRITE_MDMX_ACC	    0x00000004

/* Instruction is actually a macro.  It should be ignored by the
   disassembler, and requires special treatment by the assembler.  */
#define INSN_MACRO                  0xffffffff

/* Masks used to mark instructions to indicate which MIPS ISA level
   they were introduced in.  ISAs, as defined below, are logical
   ORs of these bits, indicating that they support the instructions
   defined at the given level.  */

#define INSN_ISA_MASK		  0x00000fff
#define INSN_ISA1                 0x00000001
#define INSN_ISA2                 0x00000002
#define INSN_ISA3                 0x00000004
#define INSN_ISA4                 0x00000008
#define INSN_ISA5                 0x00000010
#define INSN_ISA32                0x00000020
#define INSN_ISA64                0x00000040
#define INSN_ISA32R2              0x00000080
#define INSN_ISA64R2              0x00000100

/* Masks used for MIPS-defined ASEs.  */
#define INSN_ASE_MASK		  0x0000f000

/* DSP ASE */
#define INSN_DSP                  0x00001000
#define INSN_DSP64                0x00002000
/* MIPS 16 ASE */
#define INSN_MIPS16               0x00004000
/* MIPS-3D ASE */
#define INSN_MIPS3D               0x00008000

/* Chip specific instructions.  These are bitmasks.  */

/* MIPS R4650 instruction.  */
#define INSN_4650                 0x00010000
/* LSI R4010 instruction.  */
#define INSN_4010                 0x00020000
/* NEC VR4100 instruction.  */
#define INSN_4100                 0x00040000
/* Toshiba R3900 instruction.  */
#define INSN_3900                 0x00080000
/* MIPS R10000 instruction.  */
#define INSN_10000                0x00100000
/* Broadcom SB-1 instruction.  */
#define INSN_SB1                  0x00200000
/* NEC VR4111/VR4181 instruction.  */
#define INSN_4111                 0x00400000
/* NEC VR4120 instruction.  */
#define INSN_4120                 0x00800000
/* NEC VR5400 instruction.  */
#define INSN_5400		  0x01000000
/* NEC VR5500 instruction.  */
#define INSN_5500		  0x02000000

/* MDMX ASE */
#define INSN_MDMX                 0x04000000
/* MT ASE */
#define INSN_MT                   0x08000000
/* SmartMIPS ASE  */
#define INSN_SMARTMIPS            0x10000000
/* DSP R2 ASE  */
#define INSN_DSPR2                0x20000000

/* ST Microelectronics Loongson 2E.  */
#define INSN_LOONGSON_2E          0x40000000
/* ST Microelectronics Loongson 2F.  */
#define INSN_LOONGSON_2F          0x80000000

/* MIPS ISA defines, use instead of hardcoding ISA level.  */

#define       ISA_UNKNOWN     0               /* Gas internal use.  */
#define       ISA_MIPS1       (INSN_ISA1)
#define       ISA_MIPS2       (ISA_MIPS1 | INSN_ISA2)
#define       ISA_MIPS3       (ISA_MIPS2 | INSN_ISA3)
#define       ISA_MIPS4       (ISA_MIPS3 | INSN_ISA4)
#define       ISA_MIPS5       (ISA_MIPS4 | INSN_ISA5)

#define       ISA_MIPS32      (ISA_MIPS2 | INSN_ISA32)
#define       ISA_MIPS64      (ISA_MIPS5 | INSN_ISA32 | INSN_ISA64)

#define       ISA_MIPS32R2    (ISA_MIPS32 | INSN_ISA32R2)
#define       ISA_MIPS64R2    (ISA_MIPS64 | INSN_ISA32R2 | INSN_ISA64R2)


/* CPU defines, use instead of hardcoding processor number. Keep this
   in sync with bfd/archures.c in order for machine selection to work.  */
#define CPU_UNKNOWN	0               /* Gas internal use.  */
#define CPU_R3000	3000
#define CPU_R3900	3900
#define CPU_R4000	4000
#define CPU_R4010	4010
#define CPU_VR4100	4100
#define CPU_R4111	4111
#define CPU_VR4120	4120
#define CPU_R4300	4300
#define CPU_R4400	4400
#define CPU_R4600	4600
#define CPU_R4650	4650
#define CPU_R5000	5000
#define CPU_VR5400	5400
#define CPU_VR5500	5500
#define CPU_R6000	6000
#define CPU_RM7000	7000
#define CPU_R8000	8000
#define CPU_R10000	10000
#define CPU_R12000	12000
#define CPU_MIPS16	16
#define CPU_MIPS32	32
#define CPU_MIPS32R2	33
#define CPU_MIPS5       5
#define CPU_MIPS64      64
#define CPU_MIPS64R2	65
#define CPU_SB1         12310201        /* octal 'SB', 01.  */

/* Test for membership in an ISA including chip specific ISAs.  INSN
   is pointer to an element of the opcode table; ISA is the specified
   ISA/ASE bitmask to test against; and CPU is the CPU specific ISA to
   test, or zero if no CPU specific ISA test is desired.  */

#if 0
#define OPCODE_IS_MEMBER(insn, isa, cpu)				\
    (((insn)->membership & isa) != 0					\
     || (cpu == CPU_R4650 && ((insn)->membership & INSN_4650) != 0)	\
     || (cpu == CPU_RM7000 && ((insn)->membership & INSN_4650) != 0)	\
     || (cpu == CPU_RM9000 && ((insn)->membership & INSN_4650) != 0)	\
     || (cpu == CPU_R4010 && ((insn)->membership & INSN_4010) != 0)	\
     || (cpu == CPU_VR4100 && ((insn)->membership & INSN_4100) != 0)	\
     || (cpu == CPU_R3900 && ((insn)->membership & INSN_3900) != 0)	\
     || ((cpu == CPU_R10000 || cpu == CPU_R12000)			\
	 && ((insn)->membership & INSN_10000) != 0)			\
     || (cpu == CPU_SB1 && ((insn)->membership & INSN_SB1) != 0)	\
     || (cpu == CPU_R4111 && ((insn)->membership & INSN_4111) != 0)	\
     || (cpu == CPU_VR4120 && ((insn)->membership & INSN_4120) != 0)	\
     || (cpu == CPU_VR5400 && ((insn)->membership & INSN_5400) != 0)	\
     || (cpu == CPU_VR5500 && ((insn)->membership & INSN_5500) != 0)	\
     || 0)	/* Please keep this term for easier source merging.  */
#else
#define OPCODE_IS_MEMBER(insn, isa, cpu)                               \
    (1 != 0)
#endif

/* This is a list of macro expanded instructions.

   _I appended means immediate
   _A appended means address
   _AB appended means address with base register
   _D appended means 64 bit floating point constant
   _S appended means 32 bit floating point constant.  */

enum
{
  M_ABS,
  M_ADD_I,
  M_ADDU_I,
  M_AND_I,
  M_BALIGN,
  M_BEQ,
  M_BEQ_I,
  M_BEQL_I,
  M_BGE,
  M_BGEL,
  M_BGE_I,
  M_BGEL_I,
  M_BGEU,
  M_BGEUL,
  M_BGEU_I,
  M_BGEUL_I,
  M_BGT,
  M_BGTL,
  M_BGT_I,
  M_BGTL_I,
  M_BGTU,
  M_BGTUL,
  M_BGTU_I,
  M_BGTUL_I,
  M_BLE,
  M_BLEL,
  M_BLE_I,
  M_BLEL_I,
  M_BLEU,
  M_BLEUL,
  M_BLEU_I,
  M_BLEUL_I,
  M_BLT,
  M_BLTL,
  M_BLT_I,
  M_BLTL_I,
  M_BLTU,
  M_BLTUL,
  M_BLTU_I,
  M_BLTUL_I,
  M_BNE,
  M_BNE_I,
  M_BNEL_I,
  M_CACHE_AB,
  M_DABS,
  M_DADD_I,
  M_DADDU_I,
  M_DDIV_3,
  M_DDIV_3I,
  M_DDIVU_3,
  M_DDIVU_3I,
  M_DEXT,
  M_DINS,
  M_DIV_3,
  M_DIV_3I,
  M_DIVU_3,
  M_DIVU_3I,
  M_DLA_AB,
  M_DLCA_AB,
  M_DLI,
  M_DMUL,
  M_DMUL_I,
  M_DMULO,
  M_DMULO_I,
  M_DMULOU,
  M_DMULOU_I,
  M_DREM_3,
  M_DREM_3I,
  M_DREMU_3,
  M_DREMU_3I,
  M_DSUB_I,
  M_DSUBU_I,
  M_DSUBU_I_2,
  M_J_A,
  M_JAL_1,
  M_JAL_2,
  M_JAL_A,
  M_L_DOB,
  M_L_DAB,
  M_LA_AB,
  M_LB_A,
  M_LB_AB,
  M_LBU_A,
  M_LBU_AB,
  M_LCA_AB,
  M_LD_A,
  M_LD_OB,
  M_LD_AB,
  M_LDC1_AB,
  M_LDC2_AB,
  M_LDC3_AB,
  M_LDL_AB,
  M_LDR_AB,
  M_LH_A,
  M_LH_AB,
  M_LHU_A,
  M_LHU_AB,
  M_LI,
  M_LI_D,
  M_LI_DD,
  M_LI_S,
  M_LI_SS,
  M_LL_AB,
  M_LLD_AB,
  M_LS_A,
  M_LW_A,
  M_LW_AB,
  M_LWC0_A,
  M_LWC0_AB,
  M_LWC1_A,
  M_LWC1_AB,
  M_LWC2_A,
  M_LWC2_AB,
  M_LWC3_A,
  M_LWC3_AB,
  M_LWL_A,
  M_LWL_AB,
  M_LWR_A,
  M_LWR_AB,
  M_LWU_AB,
  M_MOVE,
  M_MUL,
  M_MUL_I,
  M_MULO,
  M_MULO_I,
  M_MULOU,
  M_MULOU_I,
  M_NOR_I,
  M_OR_I,
  M_REM_3,
  M_REM_3I,
  M_REMU_3,
  M_REMU_3I,
  M_DROL,
  M_ROL,
  M_DROL_I,
  M_ROL_I,
  M_DROR,
  M_ROR,
  M_DROR_I,
  M_ROR_I,
  M_S_DA,
  M_S_DOB,
  M_S_DAB,
  M_S_S,
  M_SC_AB,
  M_SCD_AB,
  M_SD_A,
  M_SD_OB,
  M_SD_AB,
  M_SDC1_AB,
  M_SDC2_AB,
  M_SDC3_AB,
  M_SDL_AB,
  M_SDR_AB,
  M_SEQ,
  M_SEQ_I,
  M_SGE,
  M_SGE_I,
  M_SGEU,
  M_SGEU_I,
  M_SGT,
  M_SGT_I,
  M_SGTU,
  M_SGTU_I,
  M_SLE,
  M_SLE_I,
  M_SLEU,
  M_SLEU_I,
  M_SLT_I,
  M_SLTU_I,
  M_SNE,
  M_SNE_I,
  M_SB_A,
  M_SB_AB,
  M_SH_A,
  M_SH_AB,
  M_SW_A,
  M_SW_AB,
  M_SWC0_A,
  M_SWC0_AB,
  M_SWC1_A,
  M_SWC1_AB,
  M_SWC2_A,
  M_SWC2_AB,
  M_SWC3_A,
  M_SWC3_AB,
  M_SWL_A,
  M_SWL_AB,
  M_SWR_A,
  M_SWR_AB,
  M_SUB_I,
  M_SUBU_I,
  M_SUBU_I_2,
  M_TEQ_I,
  M_TGE_I,
  M_TGEU_I,
  M_TLT_I,
  M_TLTU_I,
  M_TNE_I,
  M_TRUNCWD,
  M_TRUNCWS,
  M_ULD,
  M_ULD_A,
  M_ULH,
  M_ULH_A,
  M_ULHU,
  M_ULHU_A,
  M_ULW,
  M_ULW_A,
  M_USH,
  M_USH_A,
  M_USW,
  M_USW_A,
  M_USD,
  M_USD_A,
  M_XOR_I,
  M_COP0,
  M_COP1,
  M_COP2,
  M_COP3,
  M_NUM_MACROS
};


/* The order of overloaded instructions matters.  Label arguments and
   register arguments look the same. Instructions that can have either
   for arguments must apear in the correct order in this table for the
   assembler to pick the right one. In other words, entries with
   immediate operands must apear after the same instruction with
   registers.

   Many instructions are short hand for other instructions (i.e., The
   jal <register> instruction is short for jalr <register>).  */

extern const struct mips_opcode mips_builtin_opcodes[];
extern const int bfd_mips_num_builtin_opcodes;
extern struct mips_opcode *mips_opcodes;
extern int bfd_mips_num_opcodes;
#define NUMOPCODES bfd_mips_num_opcodes


/* The rest of this file adds definitions for the mips16 TinyRISC
   processor.  */

/* These are the bitmasks and shift counts used for the different
   fields in the instruction formats.  Other than OP, no masks are
   provided for the fixed portions of an instruction, since they are
   not needed.

   The I format uses IMM11.

   The RI format uses RX and IMM8.

   The RR format uses RX, and RY.

   The RRI format uses RX, RY, and IMM5.

   The RRR format uses RX, RY, and RZ.

   The RRI_A format uses RX, RY, and IMM4.

   The SHIFT format uses RX, RY, and SHAMT.

   The I8 format uses IMM8.

   The I8_MOVR32 format uses RY and REGR32.

   The IR_MOV32R format uses REG32R and MOV32Z.

   The I64 format uses IMM8.

   The RI64 format uses RY and IMM5.
   */

#define MIPS16OP_MASK_OP	0x1f
#define MIPS16OP_SH_OP		11
#define MIPS16OP_MASK_IMM11	0x7ff
#define MIPS16OP_SH_IMM11	0
#define MIPS16OP_MASK_RX	0x7
#define MIPS16OP_SH_RX		8
#define MIPS16OP_MASK_IMM8	0xff
#define MIPS16OP_SH_IMM8	0
#define MIPS16OP_MASK_RY	0x7
#define MIPS16OP_SH_RY		5
#define MIPS16OP_MASK_IMM5	0x1f
#define MIPS16OP_SH_IMM5	0
#define MIPS16OP_MASK_RZ	0x7
#define MIPS16OP_SH_RZ		2
#define MIPS16OP_MASK_IMM4	0xf
#define MIPS16OP_SH_IMM4	0
#define MIPS16OP_MASK_REGR32	0x1f
#define MIPS16OP_SH_REGR32	0
#define MIPS16OP_MASK_REG32R	0x1f
#define MIPS16OP_SH_REG32R	3
#define MIPS16OP_EXTRACT_REG32R(i) ((((i) >> 5) & 7) | ((i) & 0x18))
#define MIPS16OP_MASK_MOVE32Z	0x7
#define MIPS16OP_SH_MOVE32Z	0
#define MIPS16OP_MASK_IMM6	0x3f
#define MIPS16OP_SH_IMM6	5

/* These are the characters which may appears in the args field of an
   instruction.  They appear in the order in which the fields appear
   when the instruction is used.  Commas and parentheses in the args
   string are ignored when assembling, and written into the output
   when disassembling.

   "y" 3 bit register (MIPS16OP_*_RY)
   "x" 3 bit register (MIPS16OP_*_RX)
   "z" 3 bit register (MIPS16OP_*_RZ)
   "Z" 3 bit register (MIPS16OP_*_MOVE32Z)
   "v" 3 bit same register as source and destination (MIPS16OP_*_RX)
   "w" 3 bit same register as source and destination (MIPS16OP_*_RY)
   "0" zero register ($0)
   "S" stack pointer ($sp or $29)
   "P" program counter
   "R" return address register ($ra or $31)
   "X" 5 bit MIPS register (MIPS16OP_*_REGR32)
   "Y" 5 bit MIPS register (MIPS16OP_*_REG32R)
   "6" 6 bit unsigned break code (MIPS16OP_*_IMM6)
   "a" 26 bit jump address
   "e" 11 bit extension value
   "l" register list for entry instruction
   "L" register list for exit instruction

   The remaining codes may be extended.  Except as otherwise noted,
   the full extended operand is a 16 bit signed value.
   "<" 3 bit unsigned shift count * 0 (MIPS16OP_*_RZ) (full 5 bit unsigned)
   ">" 3 bit unsigned shift count * 0 (MIPS16OP_*_RX) (full 5 bit unsigned)
   "[" 3 bit unsigned shift count * 0 (MIPS16OP_*_RZ) (full 6 bit unsigned)
   "]" 3 bit unsigned shift count * 0 (MIPS16OP_*_RX) (full 6 bit unsigned)
   "4" 4 bit signed immediate * 0 (MIPS16OP_*_IMM4) (full 15 bit signed)
   "5" 5 bit unsigned immediate * 0 (MIPS16OP_*_IMM5)
   "H" 5 bit unsigned immediate * 2 (MIPS16OP_*_IMM5)
   "W" 5 bit unsigned immediate * 4 (MIPS16OP_*_IMM5)
   "D" 5 bit unsigned immediate * 8 (MIPS16OP_*_IMM5)
   "j" 5 bit signed immediate * 0 (MIPS16OP_*_IMM5)
   "8" 8 bit unsigned immediate * 0 (MIPS16OP_*_IMM8)
   "V" 8 bit unsigned immediate * 4 (MIPS16OP_*_IMM8)
   "C" 8 bit unsigned immediate * 8 (MIPS16OP_*_IMM8)
   "U" 8 bit unsigned immediate * 0 (MIPS16OP_*_IMM8) (full 16 bit unsigned)
   "k" 8 bit signed immediate * 0 (MIPS16OP_*_IMM8)
   "K" 8 bit signed immediate * 8 (MIPS16OP_*_IMM8)
   "p" 8 bit conditional branch address (MIPS16OP_*_IMM8)
   "q" 11 bit branch address (MIPS16OP_*_IMM11)
   "A" 8 bit PC relative address * 4 (MIPS16OP_*_IMM8)
   "B" 5 bit PC relative address * 8 (MIPS16OP_*_IMM5)
   "E" 5 bit PC relative address * 4 (MIPS16OP_*_IMM5)
   */

/* Save/restore encoding for the args field when all 4 registers are
   either saved as arguments or saved/restored as statics.  */
#define MIPS16_ALL_ARGS    0xe
#define MIPS16_ALL_STATICS 0xb

/* For the mips16, we use the same opcode table format and a few of
   the same flags.  However, most of the flags are different.  */

/* Modifies the register in MIPS16OP_*_RX.  */
#define MIPS16_INSN_WRITE_X		    0x00000001
/* Modifies the register in MIPS16OP_*_RY.  */
#define MIPS16_INSN_WRITE_Y		    0x00000002
/* Modifies the register in MIPS16OP_*_RZ.  */
#define MIPS16_INSN_WRITE_Z		    0x00000004
/* Modifies the T ($24) register.  */
#define MIPS16_INSN_WRITE_T		    0x00000008
/* Modifies the SP ($29) register.  */
#define MIPS16_INSN_WRITE_SP		    0x00000010
/* Modifies the RA ($31) register.  */
#define MIPS16_INSN_WRITE_31		    0x00000020
/* Modifies the general purpose register in MIPS16OP_*_REG32R.  */
#define MIPS16_INSN_WRITE_GPR_Y		    0x00000040
/* Reads the register in MIPS16OP_*_RX.  */
#define MIPS16_INSN_READ_X		    0x00000080
/* Reads the register in MIPS16OP_*_RY.  */
#define MIPS16_INSN_READ_Y		    0x00000100
/* Reads the register in MIPS16OP_*_MOVE32Z.  */
#define MIPS16_INSN_READ_Z		    0x00000200
/* Reads the T ($24) register.  */
#define MIPS16_INSN_READ_T		    0x00000400
/* Reads the SP ($29) register.  */
#define MIPS16_INSN_READ_SP		    0x00000800
/* Reads the RA ($31) register.  */
#define MIPS16_INSN_READ_31		    0x00001000
/* Reads the program counter.  */
#define MIPS16_INSN_READ_PC		    0x00002000
/* Reads the general purpose register in MIPS16OP_*_REGR32.  */
#define MIPS16_INSN_READ_GPR_X		    0x00004000
/* Is a branch insn. */
#define MIPS16_INSN_BRANCH                  0x00010000

/* The following flags have the same value for the mips16 opcode
   table:
   INSN_UNCOND_BRANCH_DELAY
   INSN_COND_BRANCH_DELAY
   INSN_COND_BRANCH_LIKELY (never used)
   INSN_READ_HI
   INSN_READ_LO
   INSN_WRITE_HI
   INSN_WRITE_LO
   INSN_TRAP
   INSN_ISA3
   */

extern const struct mips_opcode mips16_opcodes[];
extern const int bfd_mips16_num_opcodes;

/* Short hand so the lines aren't too long.  */

#define LDD     INSN_LOAD_MEMORY_DELAY
#define LCD	INSN_LOAD_COPROC_DELAY
#define UBD     INSN_UNCOND_BRANCH_DELAY
#define CBD	INSN_COND_BRANCH_DELAY
#define COD     INSN_COPROC_MOVE_DELAY
#define CLD	INSN_COPROC_MEMORY_DELAY
#define CBL	INSN_COND_BRANCH_LIKELY
#define TRAP	INSN_TRAP
#define SM	INSN_STORE_MEMORY

#define WR_d    INSN_WRITE_GPR_D
#define WR_t    INSN_WRITE_GPR_T
#define WR_31   INSN_WRITE_GPR_31
#define WR_D    INSN_WRITE_FPR_D
#define WR_T	INSN_WRITE_FPR_T
#define WR_S	INSN_WRITE_FPR_S
#define RD_s    INSN_READ_GPR_S
#define RD_b    INSN_READ_GPR_S
#define RD_t    INSN_READ_GPR_T
#define RD_S    INSN_READ_FPR_S
#define RD_T    INSN_READ_FPR_T
#define RD_R	INSN_READ_FPR_R
#define WR_CC	INSN_WRITE_COND_CODE
#define RD_CC	INSN_READ_COND_CODE
#define RD_C0   INSN_COP
#define RD_C1	INSN_COP
#define RD_C2   INSN_COP
#define RD_C3   INSN_COP
#define WR_C0   INSN_COP
#define WR_C1	INSN_COP
#define WR_C2   INSN_COP
#define WR_C3   INSN_COP

#define WR_HI	INSN_WRITE_HI
#define RD_HI	INSN_READ_HI
#define MOD_HI  WR_HI|RD_HI

#define WR_LO	INSN_WRITE_LO
#define RD_LO	INSN_READ_LO
#define MOD_LO  WR_LO|RD_LO

#define WR_HILO WR_HI|WR_LO
#define RD_HILO RD_HI|RD_LO
#define MOD_HILO WR_HILO|RD_HILO

#define IS_M    INSN_MULT

#define WR_MACC INSN2_WRITE_MDMX_ACC
#define RD_MACC INSN2_READ_MDMX_ACC

#define I1	INSN_ISA1
#define I2	INSN_ISA2
#define I3	INSN_ISA3
#define I4	INSN_ISA4
#define I5	INSN_ISA5
#define I32	INSN_ISA32
#define I64     INSN_ISA64
#define I33	INSN_ISA32R2
#define I65	INSN_ISA64R2

/* MIPS64 MIPS-3D ASE support.  */
#define I16     INSN_MIPS16

/* MIPS32 SmartMIPS ASE support.  */
#define SMT	INSN_SMARTMIPS

/* MIPS64 MIPS-3D ASE support.  */
#define M3D     INSN_MIPS3D

/* MIPS64 MDMX ASE support.  */
#define MX      INSN_MDMX

#define IL2E	(INSN_LOONGSON_2E)
#define IL2F	(INSN_LOONGSON_2F)

#define P3	INSN_4650
#define L1	INSN_4010
#define V1	(INSN_4100 | INSN_4111 | INSN_4120)
#define T3      INSN_3900
#define M1	INSN_10000
#define SB1     INSN_SB1
#define N411	INSN_4111
#define N412	INSN_4120
#define N5	(INSN_5400 | INSN_5500)
#define N54	INSN_5400
#define N55	INSN_5500

#define G1      (T3             \
                 )

#define G2      (T3             \
                 )

#define G3      (I4             \
                 )

/* MIPS DSP ASE support.
   NOTE:
   1. MIPS DSP ASE includes 4 accumulators ($ac0 - $ac3).  $ac0 is the pair
   of original HI and LO.  $ac1, $ac2 and $ac3 are new registers, and have
   the same structure as $ac0 (HI + LO).  For DSP instructions that write or
   read accumulators (that may be $ac0), we add WR_a (WR_HILO) or RD_a
   (RD_HILO) attributes, such that HILO dependencies are maintained
   conservatively.

   2. For some mul. instructions that use integer registers as destinations
   but destroy HI+LO as side-effect, we add WR_HILO to their attributes.

   3. MIPS DSP ASE includes a new DSP control register, which has 6 fields
   (ccond, outflag, EFI, c, scount, pos).  Many DSP instructions read or write
   certain fields of the DSP control register.  For simplicity, we decide not
   to track dependencies of these fields.
   However, "bposge32" is a branch instruction that depends on the "pos"
   field.  In order to make sure that GAS does not reorder DSP instructions
   that writes the "pos" field and "bposge32", we add DSP_VOLA (INSN_TRAP)
   attribute to those instructions that write the "pos" field.  */

#define WR_a	WR_HILO	/* Write dsp accumulators (reuse WR_HILO)  */
#define RD_a	RD_HILO	/* Read dsp accumulators (reuse RD_HILO)  */
#define MOD_a	WR_a|RD_a
#define DSP_VOLA	INSN_TRAP
#define D32	INSN_DSP
#define D33	INSN_DSPR2
#define D64	INSN_DSP64

/* MIPS MT ASE support.  */
#define MT32	INSN_MT

/* The order of overloaded instructions matters.  Label arguments and
   register arguments look the same. Instructions that can have either
   for arguments must apear in the correct order in this table for the
   assembler to pick the right one. In other words, entries with
   immediate operands must apear after the same instruction with
   registers.

   Because of the lookup algorithm used, entries with the same opcode
   name must be contiguous.

   Many instructions are short hand for other instructions (i.e., The
   jal <register> instruction is short for jalr <register>).  */

const struct mips_opcode mips_builtin_opcodes[] =
{
/* These instructions appear first so that the disassembler will find
   them first.  The assemblers uses a hash table based on the
   instruction name anyhow.  */
/* name,    args,	match,	    mask,	pinfo,          	membership */
{"pref",    "k,o(b)",   0xcc000000, 0xfc000000, RD_b,           	0,		I4|I32|G3	},
{"prefx",   "h,t(b)",	0x4c00000f, 0xfc0007ff, RD_b|RD_t,		0,		I4|I33	},
{"nop",     "",         0x00000000, 0xffffffff, 0,              	INSN2_ALIAS,	I1      }, /* sll */
{"ssnop",   "",         0x00000040, 0xffffffff, 0,              	INSN2_ALIAS,	I32|N55	}, /* sll */
{"ehb",     "",         0x000000c0, 0xffffffff, 0,              	INSN2_ALIAS,	I33	}, /* sll */
{"li",      "t,j",      0x24000000, 0xffe00000, WR_t,			INSN2_ALIAS,	I1	}, /* addiu */
{"li",	    "t,i",	0x34000000, 0xffe00000, WR_t,			INSN2_ALIAS,	I1	}, /* ori */
{"li",      "t,I",	0,    (int) M_LI,	INSN_MACRO,		0,		I1	},
{"move",    "d,s",	0,    (int) M_MOVE,	INSN_MACRO,		0,		I1	},
{"move",    "d,s",	0x0000002d, 0xfc1f07ff, WR_d|RD_s,		INSN2_ALIAS,	I3	},/* daddu */
{"move",    "d,s",	0x00000021, 0xfc1f07ff, WR_d|RD_s,		INSN2_ALIAS,	I1	},/* addu */
{"move",    "d,s",	0x00000025, 0xfc1f07ff,	WR_d|RD_s,		INSN2_ALIAS,	I1	},/* or */
{"b",       "p",	0x10000000, 0xffff0000,	UBD,			INSN2_ALIAS,	I1	},/* beq 0,0 */
{"b",       "p",	0x04010000, 0xffff0000,	UBD,			INSN2_ALIAS,	I1	},/* bgez 0 */
{"bal",     "p",	0x04110000, 0xffff0000,	UBD|WR_31,		INSN2_ALIAS,	I1	},/* bgezal 0*/

{"abs",     "d,v",	0,    (int) M_ABS,	INSN_MACRO,		0,		I1	},
{"abs.s",   "D,V",	0x46000005, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I1	},
{"abs.d",   "D,V",	0x46200005, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I1	},
{"abs.ps",  "D,V",	0x46c00005, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I5|I33	},
{"add",     "d,v,t",	0x00000020, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"add",     "t,r,I",	0,    (int) M_ADD_I,	INSN_MACRO,		0,		I1	},
{"add.s",   "D,V,T",	0x46000000, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		I1	},
{"add.d",   "D,V,T",	0x46200000, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I1	},
{"add.ob",  "X,Y,Q",	0x7800000b, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"add.ob",  "D,S,T",	0x4ac0000b, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"add.ob",  "D,S,T[e]",	0x4800000b, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"add.ob",  "D,S,k",	0x4bc0000b, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"add.ps",  "D,V,T",	0x46c00000, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"add.qh",  "X,Y,Q",	0x7820000b, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"adda.ob", "Y,Q",	0x78000037, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"adda.qh", "Y,Q",	0x78200037, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"addi",    "t,r,j",	0x20000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
{"addiu",   "t,r,j",	0x24000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
{"addl.ob", "Y,Q",	0x78000437, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"addl.qh", "Y,Q",	0x78200437, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"addr.ps", "D,S,T",	0x46c00018, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		M3D	},
{"addu",    "d,v,t",	0x00000021, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"addu",    "t,r,I",	0,    (int) M_ADDU_I,	INSN_MACRO,		0,		I1	},
{"alni.ob", "X,Y,Z,O",	0x78000018, 0xff00003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"alni.ob", "D,S,T,%",	0x48000018, 0xff00003f,	WR_D|RD_S|RD_T, 	0,		N54	},
{"alni.qh", "X,Y,Z,O",	0x7800001a, 0xff00003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"alnv.ps", "D,V,T,s",	0x4c00001e, 0xfc00003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"alnv.ob", "X,Y,Z,s",	0x78000019, 0xfc00003f,	WR_D|RD_S|RD_T|RD_s|FP_D, 0,		MX|SB1	},
{"alnv.qh", "X,Y,Z,s",	0x7800001b, 0xfc00003f,	WR_D|RD_S|RD_T|RD_s|FP_D, 0,		MX	},
{"and",     "d,v,t",	0x00000024, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"and",     "t,r,I",	0,    (int) M_AND_I,	INSN_MACRO,		0,		I1	},
{"and.ob",  "X,Y,Q",	0x7800000c, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"and.ob",  "D,S,T",	0x4ac0000c, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"and.ob",  "D,S,T[e]",	0x4800000c, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"and.ob",  "D,S,k",	0x4bc0000c, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"and.qh",  "X,Y,Q",	0x7820000c, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"andi",    "t,r,i",	0x30000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
/* b is at the top of the table.  */
/* bal is at the top of the table.  */
/* bc0[tf]l? are at the bottom of the table.  */
{"bc1any2f", "N,p",	0x45200000, 0xffe30000,	CBD|RD_CC|FP_S,		0,		M3D	},
{"bc1any2t", "N,p",	0x45210000, 0xffe30000,	CBD|RD_CC|FP_S,		0,		M3D	},
{"bc1any4f", "N,p",	0x45400000, 0xffe30000,	CBD|RD_CC|FP_S,		0,		M3D	},
{"bc1any4t", "N,p",	0x45410000, 0xffe30000,	CBD|RD_CC|FP_S,		0,		M3D	},
{"bc1f",    "p",	0x45000000, 0xffff0000,	CBD|RD_CC|FP_S,		0,		I1	},
{"bc1f",    "N,p",      0x45000000, 0xffe30000, CBD|RD_CC|FP_S, 	0,		I4|I32	},
{"bc1fl",   "p",	0x45020000, 0xffff0000,	CBL|RD_CC|FP_S,		0,		I2|T3	},
{"bc1fl",   "N,p",      0x45020000, 0xffe30000, CBL|RD_CC|FP_S, 	0,		I4|I32	},
{"bc1t",    "p",	0x45010000, 0xffff0000,	CBD|RD_CC|FP_S,		0,		I1	},
{"bc1t",    "N,p",      0x45010000, 0xffe30000, CBD|RD_CC|FP_S, 	0,		I4|I32	},
{"bc1tl",   "p",	0x45030000, 0xffff0000,	CBL|RD_CC|FP_S,		0,		I2|T3	},
{"bc1tl",   "N,p",      0x45030000, 0xffe30000, CBL|RD_CC|FP_S, 	0,		I4|I32	},
/* bc2* are at the bottom of the table.  */
/* bc3* are at the bottom of the table.  */
{"beqz",    "s,p",	0x10000000, 0xfc1f0000,	CBD|RD_s,		0,		I1	},
{"beqzl",   "s,p",	0x50000000, 0xfc1f0000,	CBL|RD_s,		0,		I2|T3	},
{"beq",     "s,t,p",	0x10000000, 0xfc000000,	CBD|RD_s|RD_t,		0,		I1	},
{"beq",     "s,I,p",	0,    (int) M_BEQ_I,	INSN_MACRO,		0,		I1	},
{"beql",    "s,t,p",	0x50000000, 0xfc000000,	CBL|RD_s|RD_t,		0,		I2|T3	},
{"beql",    "s,I,p",	0,    (int) M_BEQL_I,	INSN_MACRO,		0,		I2|T3	},
{"bge",     "s,t,p",	0,    (int) M_BGE,	INSN_MACRO,		0,		I1	},
{"bge",     "s,I,p",	0,    (int) M_BGE_I,	INSN_MACRO,		0,		I1	},
{"bgel",    "s,t,p",	0,    (int) M_BGEL,	INSN_MACRO,		0,		I2|T3	},
{"bgel",    "s,I,p",	0,    (int) M_BGEL_I,	INSN_MACRO,		0,		I2|T3	},
{"bgeu",    "s,t,p",	0,    (int) M_BGEU,	INSN_MACRO,		0,		I1	},
{"bgeu",    "s,I,p",	0,    (int) M_BGEU_I,	INSN_MACRO,		0,		I1	},
{"bgeul",   "s,t,p",	0,    (int) M_BGEUL,	INSN_MACRO,		0,		I2|T3	},
{"bgeul",   "s,I,p",	0,    (int) M_BGEUL_I,	INSN_MACRO,		0,		I2|T3	},
{"bgez",    "s,p",	0x04010000, 0xfc1f0000,	CBD|RD_s,		0,		I1	},
{"bgezl",   "s,p",	0x04030000, 0xfc1f0000,	CBL|RD_s,		0,		I2|T3	},
{"bgezal",  "s,p",	0x04110000, 0xfc1f0000,	CBD|RD_s|WR_31,		0,		I1	},
{"bgezall", "s,p",	0x04130000, 0xfc1f0000,	CBL|RD_s|WR_31,		0,		I2|T3	},
{"bgt",     "s,t,p",	0,    (int) M_BGT,	INSN_MACRO,		0,		I1	},
{"bgt",     "s,I,p",	0,    (int) M_BGT_I,	INSN_MACRO,		0,		I1	},
{"bgtl",    "s,t,p",	0,    (int) M_BGTL,	INSN_MACRO,		0,		I2|T3	},
{"bgtl",    "s,I,p",	0,    (int) M_BGTL_I,	INSN_MACRO,		0,		I2|T3	},
{"bgtu",    "s,t,p",	0,    (int) M_BGTU,	INSN_MACRO,		0,		I1	},
{"bgtu",    "s,I,p",	0,    (int) M_BGTU_I,	INSN_MACRO,		0,		I1	},
{"bgtul",   "s,t,p",	0,    (int) M_BGTUL,	INSN_MACRO,		0,		I2|T3	},
{"bgtul",   "s,I,p",	0,    (int) M_BGTUL_I,	INSN_MACRO,		0,		I2|T3	},
{"bgtz",    "s,p",	0x1c000000, 0xfc1f0000,	CBD|RD_s,		0,		I1	},
{"bgtzl",   "s,p",	0x5c000000, 0xfc1f0000,	CBL|RD_s,		0,		I2|T3	},
{"ble",     "s,t,p",	0,    (int) M_BLE,	INSN_MACRO,		0,		I1	},
{"ble",     "s,I,p",	0,    (int) M_BLE_I,	INSN_MACRO,		0,		I1	},
{"blel",    "s,t,p",	0,    (int) M_BLEL,	INSN_MACRO,		0,		I2|T3	},
{"blel",    "s,I,p",	0,    (int) M_BLEL_I,	INSN_MACRO,		0,		I2|T3	},
{"bleu",    "s,t,p",	0,    (int) M_BLEU,	INSN_MACRO,		0,		I1	},
{"bleu",    "s,I,p",	0,    (int) M_BLEU_I,	INSN_MACRO,		0,		I1	},
{"bleul",   "s,t,p",	0,    (int) M_BLEUL,	INSN_MACRO,		0,		I2|T3	},
{"bleul",   "s,I,p",	0,    (int) M_BLEUL_I,	INSN_MACRO,		0,		I2|T3	},
{"blez",    "s,p",	0x18000000, 0xfc1f0000,	CBD|RD_s,		0,		I1	},
{"blezl",   "s,p",	0x58000000, 0xfc1f0000,	CBL|RD_s,		0,		I2|T3	},
{"blt",     "s,t,p",	0,    (int) M_BLT,	INSN_MACRO,		0,		I1	},
{"blt",     "s,I,p",	0,    (int) M_BLT_I,	INSN_MACRO,		0,		I1	},
{"bltl",    "s,t,p",	0,    (int) M_BLTL,	INSN_MACRO,		0,		I2|T3	},
{"bltl",    "s,I,p",	0,    (int) M_BLTL_I,	INSN_MACRO,		0,		I2|T3	},
{"bltu",    "s,t,p",	0,    (int) M_BLTU,	INSN_MACRO,		0,		I1	},
{"bltu",    "s,I,p",	0,    (int) M_BLTU_I,	INSN_MACRO,		0,		I1	},
{"bltul",   "s,t,p",	0,    (int) M_BLTUL,	INSN_MACRO,		0,		I2|T3	},
{"bltul",   "s,I,p",	0,    (int) M_BLTUL_I,	INSN_MACRO,		0,		I2|T3	},
{"bltz",    "s,p",	0x04000000, 0xfc1f0000,	CBD|RD_s,		0,		I1	},
{"bltzl",   "s,p",	0x04020000, 0xfc1f0000,	CBL|RD_s,		0,		I2|T3	},
{"bltzal",  "s,p",	0x04100000, 0xfc1f0000,	CBD|RD_s|WR_31,		0,		I1	},
{"bltzall", "s,p",	0x04120000, 0xfc1f0000,	CBL|RD_s|WR_31,		0,		I2|T3	},
{"bnez",    "s,p",	0x14000000, 0xfc1f0000,	CBD|RD_s,		0,		I1	},
{"bnezl",   "s,p",	0x54000000, 0xfc1f0000,	CBL|RD_s,		0,		I2|T3	},
{"bne",     "s,t,p",	0x14000000, 0xfc000000,	CBD|RD_s|RD_t,		0,		I1	},
{"bne",     "s,I,p",	0,    (int) M_BNE_I,	INSN_MACRO,		0,		I1	},
{"bnel",    "s,t,p",	0x54000000, 0xfc000000,	CBL|RD_s|RD_t, 		0,		I2|T3	},
{"bnel",    "s,I,p",	0,    (int) M_BNEL_I,	INSN_MACRO,		0,		I2|T3	},
{"break",   "",		0x0000000d, 0xffffffff,	TRAP,			0,		I1	},
{"break",   "c",	0x0000000d, 0xfc00ffff,	TRAP,			0,		I1	},
{"break",   "c,q",	0x0000000d, 0xfc00003f,	TRAP,			0,		I1	},
{"c.f.d",   "S,T",	0x46200030, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.f.d",   "M,S,T",    0x46200030, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.f.s",   "S,T",      0x46000030, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.f.s",   "M,S,T",    0x46000030, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.f.ps",  "S,T",	0x46c00030, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.f.ps",  "M,S,T",	0x46c00030, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.un.d",  "S,T",	0x46200031, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.un.d",  "M,S,T",    0x46200031, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.un.s",  "S,T",      0x46000031, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.un.s",  "M,S,T",    0x46000031, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.un.ps", "S,T",	0x46c00031, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.un.ps", "M,S,T",	0x46c00031, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.eq.d",  "S,T",	0x46200032, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.eq.d",  "M,S,T",    0x46200032, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.eq.s",  "S,T",      0x46000032, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.eq.s",  "M,S,T",    0x46000032, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.eq.ob", "Y,Q",	0x78000001, 0xfc2007ff,	WR_CC|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"c.eq.ob", "S,T",	0x4ac00001, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.eq.ob", "S,T[e]",	0x48000001, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.eq.ob", "S,k",	0x4bc00001, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.eq.ps", "S,T",	0x46c00032, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.eq.ps", "M,S,T",	0x46c00032, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.eq.qh", "Y,Q",	0x78200001, 0xfc2007ff,	WR_CC|RD_S|RD_T|FP_D,	0,		MX	},
{"c.ueq.d", "S,T",	0x46200033, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.ueq.d", "M,S,T",    0x46200033, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ueq.s", "S,T",      0x46000033, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ueq.s", "M,S,T",    0x46000033, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ueq.ps","S,T",	0x46c00033, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ueq.ps","M,S,T",	0x46c00033, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.olt.d", "S,T",      0x46200034, 0xffe007ff, RD_S|RD_T|WR_CC|FP_D,   0,		I1      },
{"c.olt.d", "M,S,T",    0x46200034, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.olt.s", "S,T",	0x46000034, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_S,	0,		I1	},
{"c.olt.s", "M,S,T",    0x46000034, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.olt.ps","S,T",	0x46c00034, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.olt.ps","M,S,T",	0x46c00034, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ult.d", "S,T",	0x46200035, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.ult.d", "M,S,T",    0x46200035, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ult.s", "S,T",      0x46000035, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ult.s", "M,S,T",    0x46000035, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ult.ps","S,T",	0x46c00035, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ult.ps","M,S,T",	0x46c00035, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ole.d", "S,T",      0x46200036, 0xffe007ff, RD_S|RD_T|WR_CC|FP_D,   0,		I1      },
{"c.ole.d", "M,S,T",    0x46200036, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ole.s", "S,T",      0x46000036, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ole.s", "M,S,T",    0x46000036, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ole.ps","S,T",	0x46c00036, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ole.ps","M,S,T",	0x46c00036, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ule.d", "S,T",	0x46200037, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.ule.d", "M,S,T",    0x46200037, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ule.s", "S,T",      0x46000037, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ule.s", "M,S,T",    0x46000037, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ule.ps","S,T",	0x46c00037, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ule.ps","M,S,T",	0x46c00037, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.sf.d",  "S,T",	0x46200038, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.sf.d",  "M,S,T",    0x46200038, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.sf.s",  "S,T",      0x46000038, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.sf.s",  "M,S,T",    0x46000038, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.sf.ps", "S,T",	0x46c00038, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.sf.ps", "M,S,T",	0x46c00038, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ngle.d","S,T",	0x46200039, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.ngle.d","M,S,T",    0x46200039, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ngle.s","S,T",      0x46000039, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ngle.s","M,S,T",    0x46000039, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ngle.ps","S,T",	0x46c00039, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ngle.ps","M,S,T",	0x46c00039, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.seq.d", "S,T",	0x4620003a, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.seq.d", "M,S,T",    0x4620003a, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.seq.s", "S,T",      0x4600003a, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.seq.s", "M,S,T",    0x4600003a, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.seq.ps","S,T",	0x46c0003a, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.seq.ps","M,S,T",	0x46c0003a, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ngl.d", "S,T",	0x4620003b, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.ngl.d", "M,S,T",    0x4620003b, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ngl.s", "S,T",      0x4600003b, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ngl.s", "M,S,T",    0x4600003b, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ngl.ps","S,T",	0x46c0003b, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ngl.ps","M,S,T",	0x46c0003b, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.lt.d",  "S,T",	0x4620003c, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.lt.d",  "M,S,T",    0x4620003c, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.lt.s",  "S,T",	0x4600003c, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_S,	0,		I1	},
{"c.lt.s",  "M,S,T",    0x4600003c, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.lt.ob", "Y,Q",	0x78000004, 0xfc2007ff,	WR_CC|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"c.lt.ob", "S,T",	0x4ac00004, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.lt.ob", "S,T[e]",	0x48000004, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.lt.ob", "S,k",	0x4bc00004, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.lt.ps", "S,T",	0x46c0003c, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.lt.ps", "M,S,T",	0x46c0003c, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.lt.qh", "Y,Q",	0x78200004, 0xfc2007ff,	WR_CC|RD_S|RD_T|FP_D,	0,		MX	},
{"c.nge.d", "S,T",	0x4620003d, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.nge.d", "M,S,T",    0x4620003d, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.nge.s", "S,T",      0x4600003d, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.nge.s", "M,S,T",    0x4600003d, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.nge.ps","S,T",	0x46c0003d, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.nge.ps","M,S,T",	0x46c0003d, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.le.d",  "S,T",	0x4620003e, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.le.d",  "M,S,T",    0x4620003e, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.le.s",  "S,T",	0x4600003e, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_S,	0,		I1	},
{"c.le.s",  "M,S,T",    0x4600003e, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.le.ob", "Y,Q",	0x78000005, 0xfc2007ff,	WR_CC|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"c.le.ob", "S,T",	0x4ac00005, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.le.ob", "S,T[e]",	0x48000005, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.le.ob", "S,k",	0x4bc00005, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"c.le.ps", "S,T",	0x46c0003e, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.le.ps", "M,S,T",	0x46c0003e, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.le.qh", "Y,Q",	0x78200005, 0xfc2007ff,	WR_CC|RD_S|RD_T|FP_D,	0,		MX	},
{"c.ngt.d", "S,T",	0x4620003f, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I1	},
{"c.ngt.d", "M,S,T",    0x4620003f, 0xffe000ff, RD_S|RD_T|WR_CC|FP_D,   0,		I4|I32	},
{"c.ngt.s", "S,T",      0x4600003f, 0xffe007ff, RD_S|RD_T|WR_CC|FP_S,   0,		I1      },
{"c.ngt.s", "M,S,T",    0x4600003f, 0xffe000ff, RD_S|RD_T|WR_CC|FP_S,   0,		I4|I32	},
{"c.ngt.ps","S,T",	0x46c0003f, 0xffe007ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"c.ngt.ps","M,S,T",	0x46c0003f, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		I5|I33	},
{"cabs.eq.d",  "M,S,T",	0x46200072, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.eq.ps", "M,S,T",	0x46c00072, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.eq.s",  "M,S,T",	0x46000072, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.f.d",   "M,S,T",	0x46200070, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.f.ps",  "M,S,T",	0x46c00070, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.f.s",   "M,S,T",	0x46000070, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.le.d",  "M,S,T",	0x4620007e, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.le.ps", "M,S,T",	0x46c0007e, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.le.s",  "M,S,T",	0x4600007e, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.lt.d",  "M,S,T",	0x4620007c, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.lt.ps", "M,S,T",	0x46c0007c, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.lt.s",  "M,S,T",	0x4600007c, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.nge.d", "M,S,T",	0x4620007d, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.nge.ps","M,S,T",	0x46c0007d, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.nge.s", "M,S,T",	0x4600007d, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ngl.d", "M,S,T",	0x4620007b, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ngl.ps","M,S,T",	0x46c0007b, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ngl.s", "M,S,T",	0x4600007b, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ngle.d","M,S,T",	0x46200079, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ngle.ps","M,S,T",0x46c00079, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ngle.s","M,S,T",	0x46000079, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ngt.d", "M,S,T",	0x4620007f, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ngt.ps","M,S,T",	0x46c0007f, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ngt.s", "M,S,T",	0x4600007f, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ole.d", "M,S,T",	0x46200076, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ole.ps","M,S,T",	0x46c00076, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ole.s", "M,S,T",	0x46000076, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.olt.d", "M,S,T",	0x46200074, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.olt.ps","M,S,T",	0x46c00074, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.olt.s", "M,S,T",	0x46000074, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.seq.d", "M,S,T",	0x4620007a, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.seq.ps","M,S,T",	0x46c0007a, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.seq.s", "M,S,T",	0x4600007a, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.sf.d",  "M,S,T",	0x46200078, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.sf.ps", "M,S,T",	0x46c00078, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.sf.s",  "M,S,T",	0x46000078, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ueq.d", "M,S,T",	0x46200073, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ueq.ps","M,S,T",	0x46c00073, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ueq.s", "M,S,T",	0x46000073, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ule.d", "M,S,T",	0x46200077, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ule.ps","M,S,T",	0x46c00077, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ule.s", "M,S,T",	0x46000077, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.ult.d", "M,S,T",	0x46200075, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ult.ps","M,S,T",	0x46c00075, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.ult.s", "M,S,T",	0x46000075, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
{"cabs.un.d",  "M,S,T",	0x46200071, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.un.ps", "M,S,T",	0x46c00071, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_D,	0,		M3D	},
{"cabs.un.s",  "M,S,T",	0x46000071, 0xffe000ff,	RD_S|RD_T|WR_CC|FP_S,	0,		M3D	},
/* CW4010 instructions which are aliases for the cache instruction.  */
{"flushi",  "",		0xbc010000, 0xffffffff, 0,			0,		L1	},
{"flushd",  "",		0xbc020000, 0xffffffff, 0, 			0,		L1	},
{"flushid", "",		0xbc030000, 0xffffffff, 0, 			0,		L1	},
{"wb", 	    "o(b)",	0xbc040000, 0xfc1f0000, SM|RD_b,		0,		L1	},
{"cache",   "k,o(b)",   0xbc000000, 0xfc000000, RD_b,           	0,		I3|I32|T3},
{"cache",   "k,A(b)",	0,    (int) M_CACHE_AB, INSN_MACRO,		0,		I3|I32|T3},
{"ceil.l.d", "D,S",	0x4620000a, 0xffff003f, WR_D|RD_S|FP_D,		0,		I3|I33	},
{"ceil.l.s", "D,S",	0x4600000a, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I3|I33	},
{"ceil.w.d", "D,S",	0x4620000e, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I2	},
{"ceil.w.s", "D,S",	0x4600000e, 0xffff003f, WR_D|RD_S|FP_S,		0,		I2	},
{"cfc0",    "t,G",	0x40400000, 0xffe007ff,	LCD|WR_t|RD_C0,		0,		I1	},
{"cfc1",    "t,G",	0x44400000, 0xffe007ff,	LCD|WR_t|RD_C1|FP_S,	0,		I1	},
{"cfc1",    "t,S",	0x44400000, 0xffe007ff,	LCD|WR_t|RD_C1|FP_S,	0,		I1	},
/* cfc2 is at the bottom of the table.  */
/* cfc3 is at the bottom of the table.  */
{"cftc1",   "d,E",	0x41000023, 0xffe007ff, TRAP|LCD|WR_d|RD_C1|FP_S, 0,		MT32	},
{"cftc1",   "d,T",	0x41000023, 0xffe007ff, TRAP|LCD|WR_d|RD_C1|FP_S, 0,		MT32	},
{"cftc2",   "d,E",	0x41000025, 0xffe007ff, TRAP|LCD|WR_d|RD_C2,	0,		MT32	},
{"clo",     "U,s",      0x70000021, 0xfc0007ff, WR_d|WR_t|RD_s, 	0,		I32|N55 },
{"clz",     "U,s",      0x70000020, 0xfc0007ff, WR_d|WR_t|RD_s, 	0,		I32|N55 },
{"ctc0",    "t,G",	0x40c00000, 0xffe007ff,	COD|RD_t|WR_CC,		0,		I1	},
{"ctc1",    "t,G",	0x44c00000, 0xffe007ff,	COD|RD_t|WR_CC|FP_S,	0,		I1	},
{"ctc1",    "t,S",	0x44c00000, 0xffe007ff,	COD|RD_t|WR_CC|FP_S,	0,		I1	},
/* ctc2 is at the bottom of the table.  */
/* ctc3 is at the bottom of the table.  */
{"cttc1",   "t,g",	0x41800023, 0xffe007ff, TRAP|COD|RD_t|WR_CC|FP_S, 0,		MT32	},
{"cttc1",   "t,S",	0x41800023, 0xffe007ff, TRAP|COD|RD_t|WR_CC|FP_S, 0,		MT32	},
{"cttc2",   "t,g",	0x41800025, 0xffe007ff, TRAP|COD|RD_t|WR_CC,	0,		MT32	},
{"cvt.d.l", "D,S",	0x46a00021, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I3|I33	},
{"cvt.d.s", "D,S",	0x46000021, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I1	},
{"cvt.d.w", "D,S",	0x46800021, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I1	},
{"cvt.l.d", "D,S",	0x46200025, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I3|I33	},
{"cvt.l.s", "D,S",	0x46000025, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I3|I33	},
{"cvt.s.l", "D,S",	0x46a00020, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I3|I33	},
{"cvt.s.d", "D,S",	0x46200020, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I1	},
{"cvt.s.w", "D,S",	0x46800020, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I1	},
{"cvt.s.pl","D,S",	0x46c00028, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I5|I33	},
{"cvt.s.pu","D,S",	0x46c00020, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I5|I33	},
{"cvt.w.d", "D,S",	0x46200024, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I1	},
{"cvt.w.s", "D,S",	0x46000024, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I1	},
{"cvt.ps.pw", "D,S",	0x46800026, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		M3D	},
{"cvt.ps.s","D,V,T",	0x46000026, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S|FP_D, 0,		I5|I33	},
{"cvt.pw.ps", "D,S",	0x46c00024, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		M3D	},
{"dabs",    "d,v",	0,    (int) M_DABS,	INSN_MACRO,		0,		I3	},
{"dadd",    "d,v,t",	0x0000002c, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		I3	},
{"dadd",    "t,r,I",	0,    (int) M_DADD_I,	INSN_MACRO,		0,		I3	},
{"daddi",   "t,r,j",	0x60000000, 0xfc000000, WR_t|RD_s,		0,		I3	},
{"daddiu",  "t,r,j",	0x64000000, 0xfc000000, WR_t|RD_s,		0,		I3	},
{"daddu",   "d,v,t",	0x0000002d, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		I3	},
{"daddu",   "t,r,I",	0,    (int) M_DADDU_I,	INSN_MACRO,		0,		I3	},
{"dbreak",  "",		0x7000003f, 0xffffffff,	0,			0,		N5	},
{"dclo",    "U,s",      0x70000025, 0xfc0007ff, RD_s|WR_d|WR_t, 	0,		I64|N55 },
{"dclz",    "U,s",      0x70000024, 0xfc0007ff, RD_s|WR_d|WR_t, 	0,		I64|N55 },
/* dctr and dctw are used on the r5000.  */
{"dctr",    "o(b)",	0xbc050000, 0xfc1f0000, RD_b,			0,		I3	},
{"dctw",    "o(b)",	0xbc090000, 0xfc1f0000, RD_b,			0,		I3	},
{"deret",   "",         0x4200001f, 0xffffffff, 0, 			0,		I32|G2	},
{"dext",    "t,r,I,+I",	0,    (int) M_DEXT,	INSN_MACRO,		0,		I65	},
{"dext",    "t,r,+A,+C", 0x7c000003, 0xfc00003f, WR_t|RD_s,    		0,		I65	},
{"dextm",   "t,r,+A,+G", 0x7c000001, 0xfc00003f, WR_t|RD_s,    		0,		I65	},
{"dextu",   "t,r,+E,+H", 0x7c000002, 0xfc00003f, WR_t|RD_s,    		0,		I65	},
/* For ddiv, see the comments about div.  */
{"ddiv",    "z,s,t",    0x0000001e, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I3      },
{"ddiv",    "d,v,t",	0,    (int) M_DDIV_3,	INSN_MACRO,		0,		I3	},
{"ddiv",    "d,v,I",	0,    (int) M_DDIV_3I,	INSN_MACRO,		0,		I3	},
/* For ddivu, see the comments about div.  */
{"ddivu",   "z,s,t",    0x0000001f, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I3      },
{"ddivu",   "d,v,t",	0,    (int) M_DDIVU_3,	INSN_MACRO,		0,		I3	},
{"ddivu",   "d,v,I",	0,    (int) M_DDIVU_3I,	INSN_MACRO,		0,		I3	},
{"di",      "",		0x41606000, 0xffffffff,	WR_t|WR_C0,		0,		I33	},
{"di",      "t",	0x41606000, 0xffe0ffff,	WR_t|WR_C0,		0,		I33	},
{"dins",    "t,r,I,+I",	0,    (int) M_DINS,	INSN_MACRO,		0,		I65	},
{"dins",    "t,r,+A,+B", 0x7c000007, 0xfc00003f, WR_t|RD_s,    		0,		I65	},
{"dinsm",   "t,r,+A,+F", 0x7c000005, 0xfc00003f, WR_t|RD_s,    		0,		I65	},
{"dinsu",   "t,r,+E,+F", 0x7c000006, 0xfc00003f, WR_t|RD_s,    		0,		I65	},
/* The MIPS assembler treats the div opcode with two operands as
   though the first operand appeared twice (the first operand is both
   a source and a destination).  To get the div machine instruction,
   you must use an explicit destination of $0.  */
{"div",     "z,s,t",    0x0000001a, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I1      },
{"div",     "z,t",      0x0000001a, 0xffe0ffff, RD_s|RD_t|WR_HILO,      0,		I1      },
{"div",     "d,v,t",	0,    (int) M_DIV_3,	INSN_MACRO,		0,		I1	},
{"div",     "d,v,I",	0,    (int) M_DIV_3I,	INSN_MACRO,		0,		I1	},
{"div.d",   "D,V,T",	0x46200003, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I1	},
{"div.s",   "D,V,T",	0x46000003, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		I1	},
{"div.ps",  "D,V,T",	0x46c00003, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		SB1	},
/* For divu, see the comments about div.  */
{"divu",    "z,s,t",    0x0000001b, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I1      },
{"divu",    "z,t",      0x0000001b, 0xffe0ffff, RD_s|RD_t|WR_HILO,      0,		I1      },
{"divu",    "d,v,t",	0,    (int) M_DIVU_3,	INSN_MACRO,		0,		I1	},
{"divu",    "d,v,I",	0,    (int) M_DIVU_3I,	INSN_MACRO,		0,		I1	},
{"dla",     "t,A(b)",	0,    (int) M_DLA_AB,	INSN_MACRO,		0,		I3	},
{"dlca",    "t,A(b)",	0,    (int) M_DLCA_AB,	INSN_MACRO,		0,		I3	},
{"dli",     "t,j",      0x24000000, 0xffe00000, WR_t,			0,		I3	}, /* addiu */
{"dli",	    "t,i",	0x34000000, 0xffe00000, WR_t,			0,		I3	}, /* ori */
{"dli",     "t,I",	0,    (int) M_DLI,	INSN_MACRO,		0,		I3	},
{"dmacc",   "d,s,t",	0x00000029, 0xfc0007ff,	RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmacchi", "d,s,t",	0x00000229, 0xfc0007ff, RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmacchis", "d,s,t",	0x00000629, 0xfc0007ff, RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmacchiu", "d,s,t",	0x00000269, 0xfc0007ff, RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmacchius", "d,s,t",	0x00000669, 0xfc0007ff, RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmaccs",  "d,s,t",	0x00000429, 0xfc0007ff,	RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmaccu",  "d,s,t",	0x00000069, 0xfc0007ff,	RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmaccus", "d,s,t",	0x00000469, 0xfc0007ff,	RD_s|RD_t|WR_LO|WR_d,	0,		N412	},
{"dmadd16", "s,t",      0x00000029, 0xfc00ffff, RD_s|RD_t|MOD_LO,       0,		N411    },
{"dmfc0",   "t,G",	0x40200000, 0xffe007ff, LCD|WR_t|RD_C0,		0,		I3	},
{"dmfc0",   "t,+D",     0x40200000, 0xffe007f8, LCD|WR_t|RD_C0, 	0,		I64     },
{"dmfc0",   "t,G,H",    0x40200000, 0xffe007f8, LCD|WR_t|RD_C0, 	0,		I64     },
{"dmt",     "",		0x41600bc1, 0xffffffff, TRAP,			0,		MT32	},
{"dmt",     "t",	0x41600bc1, 0xffe0ffff, TRAP|WR_t,		0,		MT32	},
{"dmtc0",   "t,G",	0x40a00000, 0xffe007ff, COD|RD_t|WR_C0|WR_CC,	0,		I3	},
{"dmtc0",   "t,+D",     0x40a00000, 0xffe007f8, COD|RD_t|WR_C0|WR_CC,   0,		I64     },
{"dmtc0",   "t,G,H",    0x40a00000, 0xffe007f8, COD|RD_t|WR_C0|WR_CC,   0,		I64     },
{"dmfc1",   "t,S",	0x44200000, 0xffe007ff, LCD|WR_t|RD_S|FP_D,	0,		I3	},
{"dmfc1",   "t,G",      0x44200000, 0xffe007ff, LCD|WR_t|RD_S|FP_D,     0,		I3      },
{"dmtc1",   "t,S",	0x44a00000, 0xffe007ff, COD|RD_t|WR_S|FP_D,	0,		I3	},
{"dmtc1",   "t,G",      0x44a00000, 0xffe007ff, COD|RD_t|WR_S|FP_D,     0,		I3      },
/* dmfc2 is at the bottom of the table.  */
/* dmtc2 is at the bottom of the table.  */
/* dmfc3 is at the bottom of the table.  */
/* dmtc3 is at the bottom of the table.  */
{"dmul",    "d,v,t",	0,    (int) M_DMUL,	INSN_MACRO,		0,		I3	},
{"dmul",    "d,v,I",	0,    (int) M_DMUL_I,	INSN_MACRO,		0,		I3	},
{"dmulo",   "d,v,t",	0,    (int) M_DMULO,	INSN_MACRO,		0,		I3	},
{"dmulo",   "d,v,I",	0,    (int) M_DMULO_I,	INSN_MACRO,		0,		I3	},
{"dmulou",  "d,v,t",	0,    (int) M_DMULOU,	INSN_MACRO,		0,		I3	},
{"dmulou",  "d,v,I",	0,    (int) M_DMULOU_I,	INSN_MACRO,		0,		I3	},
{"dmult",   "s,t",      0x0000001c, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I3	},
{"dmultu",  "s,t",      0x0000001d, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I3	},
{"dneg",    "d,w",	0x0000002e, 0xffe007ff,	WR_d|RD_t,		0,		I3	}, /* dsub 0 */
{"dnegu",   "d,w",	0x0000002f, 0xffe007ff,	WR_d|RD_t,		0,		I3	}, /* dsubu 0*/
{"drem",    "z,s,t",    0x0000001e, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I3      },
{"drem",    "d,v,t",	3,    (int) M_DREM_3,	INSN_MACRO,		0,		I3	},
{"drem",    "d,v,I",	3,    (int) M_DREM_3I,	INSN_MACRO,		0,		I3	},
{"dremu",   "z,s,t",    0x0000001f, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I3      },
{"dremu",   "d,v,t",	3,    (int) M_DREMU_3,	INSN_MACRO,		0,		I3	},
{"dremu",   "d,v,I",	3,    (int) M_DREMU_3I,	INSN_MACRO,		0,		I3	},
{"dret",    "",		0x7000003e, 0xffffffff,	0,			0,		N5	},
{"drol",    "d,v,t",	0,    (int) M_DROL,	INSN_MACRO,		0,		I3	},
{"drol",    "d,v,I",	0,    (int) M_DROL_I,	INSN_MACRO,		0,		I3	},
{"dror",    "d,v,t",	0,    (int) M_DROR,	INSN_MACRO,		0,		I3	},
{"dror",    "d,v,I",	0,    (int) M_DROR_I,	INSN_MACRO,		0,		I3	},
{"dror",    "d,w,<",	0x0020003a, 0xffe0003f,	WR_d|RD_t,		0,		N5|I65	},
{"drorv",   "d,t,s",	0x00000056, 0xfc0007ff,	RD_t|RD_s|WR_d,		0,		N5|I65	},
{"dror32",  "d,w,<",	0x0020003e, 0xffe0003f,	WR_d|RD_t,		0,		N5|I65	},
{"drotl",   "d,v,t",	0,    (int) M_DROL,	INSN_MACRO,		0,		I65	},
{"drotl",   "d,v,I",	0,    (int) M_DROL_I,	INSN_MACRO,		0,		I65	},
{"drotr",   "d,v,t",	0,    (int) M_DROR,	INSN_MACRO,		0,		I65	},
{"drotr",   "d,v,I",	0,    (int) M_DROR_I,	INSN_MACRO,		0,		I65	},
{"drotrv",  "d,t,s",	0x00000056, 0xfc0007ff,	RD_t|RD_s|WR_d,		0,		I65	},
{"drotr32", "d,w,<",	0x0020003e, 0xffe0003f,	WR_d|RD_t,		0,		I65	},
{"dsbh",    "d,w",	0x7c0000a4, 0xffe007ff,	WR_d|RD_t,		0,		I65	},
{"dshd",    "d,w",	0x7c000164, 0xffe007ff,	WR_d|RD_t,		0,		I65	},
{"dsllv",   "d,t,s",	0x00000014, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I3	},
{"dsll32",  "d,w,<",	0x0000003c, 0xffe0003f, WR_d|RD_t,		0,		I3	},
{"dsll",    "d,w,s",	0x00000014, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I3	}, /* dsllv */
{"dsll",    "d,w,>",	0x0000003c, 0xffe0003f, WR_d|RD_t,		0,		I3	}, /* dsll32 */
{"dsll",    "d,w,<",	0x00000038, 0xffe0003f,	WR_d|RD_t,		0,		I3	},
{"dsrav",   "d,t,s",	0x00000017, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I3	},
{"dsra32",  "d,w,<",	0x0000003f, 0xffe0003f, WR_d|RD_t,		0,		I3	},
{"dsra",    "d,w,s",	0x00000017, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I3	}, /* dsrav */
{"dsra",    "d,w,>",	0x0000003f, 0xffe0003f, WR_d|RD_t,		0,		I3	}, /* dsra32 */
{"dsra",    "d,w,<",	0x0000003b, 0xffe0003f,	WR_d|RD_t,		0,		I3	},
{"dsrlv",   "d,t,s",	0x00000016, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I3	},
{"dsrl32",  "d,w,<",	0x0000003e, 0xffe0003f, WR_d|RD_t,		0,		I3	},
{"dsrl",    "d,w,s",	0x00000016, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I3	}, /* dsrlv */
{"dsrl",    "d,w,>",	0x0000003e, 0xffe0003f, WR_d|RD_t,		0,		I3	}, /* dsrl32 */
{"dsrl",    "d,w,<",	0x0000003a, 0xffe0003f,	WR_d|RD_t,		0,		I3	},
{"dsub",    "d,v,t",	0x0000002e, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I3	},
{"dsub",    "d,v,I",	0,    (int) M_DSUB_I,	INSN_MACRO,		0,		I3	},
{"dsubu",   "d,v,t",	0x0000002f, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I3	},
{"dsubu",   "d,v,I",	0,    (int) M_DSUBU_I,	INSN_MACRO,		0,		I3	},
{"dvpe",    "",		0x41600001, 0xffffffff, TRAP,			0,		MT32	},
{"dvpe",    "t",	0x41600001, 0xffe0ffff, TRAP|WR_t,		0,		MT32	},
{"ei",      "",		0x41606020, 0xffffffff,	WR_t|WR_C0,		0,		I33	},
{"ei",      "t",	0x41606020, 0xffe0ffff,	WR_t|WR_C0,		0,		I33	},
{"emt",     "",		0x41600be1, 0xffffffff, TRAP,			0,		MT32	},
{"emt",     "t",	0x41600be1, 0xffe0ffff, TRAP|WR_t,		0,		MT32	},
{"eret",    "",         0x42000018, 0xffffffff, 0,      		0,		I3|I32	},
{"evpe",    "",		0x41600021, 0xffffffff, TRAP,			0,		MT32	},
{"evpe",    "t",	0x41600021, 0xffe0ffff, TRAP|WR_t,		0,		MT32	},
{"ext",     "t,r,+A,+C", 0x7c000000, 0xfc00003f, WR_t|RD_s,    		0,		I33	},
{"floor.l.d", "D,S",	0x4620000b, 0xffff003f, WR_D|RD_S|FP_D,		0,		I3|I33	},
{"floor.l.s", "D,S",	0x4600000b, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I3|I33	},
{"floor.w.d", "D,S",	0x4620000f, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I2	},
{"floor.w.s", "D,S",	0x4600000f, 0xffff003f, WR_D|RD_S|FP_S,		0,		I2	},
{"hibernate","",        0x42000023, 0xffffffff,	0, 			0,		V1	},
{"ins",     "t,r,+A,+B", 0x7c000004, 0xfc00003f, WR_t|RD_s,    		0,		I33	},
{"jr",      "s",	0x00000008, 0xfc1fffff,	UBD|RD_s,		0,		I1	},
/* jr.hb is officially MIPS{32,64}R2, but it works on R1 as jr with
   the same hazard barrier effect.  */
{"jr.hb",   "s",	0x00000408, 0xfc1fffff,	UBD|RD_s,		0,		I32	},
{"j",       "s",	0x00000008, 0xfc1fffff,	UBD|RD_s,		0,		I1	}, /* jr */
/* SVR4 PIC code requires special handling for j, so it must be a
   macro.  */
{"j",	    "a",	0,     (int) M_J_A,	INSN_MACRO,		0,		I1	},
/* This form of j is used by the disassembler and internally by the
   assembler, but will never match user input (because the line above
   will match first).  */
{"j",       "a",	0x08000000, 0xfc000000,	UBD,			0,		I1	},
{"jalr",    "s",	0x0000f809, 0xfc1fffff,	UBD|RD_s|WR_d,		0,		I1	},
{"jalr",    "d,s",	0x00000009, 0xfc1f07ff,	UBD|RD_s|WR_d,		0,		I1	},
/* jalr.hb is officially MIPS{32,64}R2, but it works on R1 as jalr
   with the same hazard barrier effect.  */
{"jalr.hb", "s",	0x0000fc09, 0xfc1fffff,	UBD|RD_s|WR_d,		0,		I32	},
{"jalr.hb", "d,s",	0x00000409, 0xfc1f07ff,	UBD|RD_s|WR_d,		0,		I32	},
/* SVR4 PIC code requires special handling for jal, so it must be a
   macro.  */
{"jal",     "d,s",	0,     (int) M_JAL_2,	INSN_MACRO,		0,		I1	},
{"jal",     "s",	0,     (int) M_JAL_1,	INSN_MACRO,		0,		I1	},
{"jal",     "a",	0,     (int) M_JAL_A,	INSN_MACRO,		0,		I1	},
/* This form of jal is used by the disassembler and internally by the
   assembler, but will never match user input (because the line above
   will match first).  */
{"jal",     "a",	0x0c000000, 0xfc000000,	UBD|WR_31,		0,		I1	},
{"jalx",    "a",	0x74000000, 0xfc000000, UBD|WR_31,		0,		I16     },
{"la",      "t,A(b)",	0,    (int) M_LA_AB,	INSN_MACRO,		0,		I1	},
{"lb",      "t,o(b)",	0x80000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lb",      "t,A(b)",	0,    (int) M_LB_AB,	INSN_MACRO,		0,		I1	},
{"lbu",     "t,o(b)",	0x90000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lbu",     "t,A(b)",	0,    (int) M_LBU_AB,	INSN_MACRO,		0,		I1	},
{"lca",     "t,A(b)",	0,    (int) M_LCA_AB,	INSN_MACRO,		0,		I1	},
{"ld",	    "t,o(b)",   0xdc000000, 0xfc000000, WR_t|RD_b,		0,		I3	},
{"ld",      "t,o(b)",	0,    (int) M_LD_OB,	INSN_MACRO,		0,		I1	},
{"ld",      "t,A(b)",	0,    (int) M_LD_AB,	INSN_MACRO,		0,		I1	},
{"ldc1",    "T,o(b)",	0xd4000000, 0xfc000000, CLD|RD_b|WR_T|FP_D,	0,		I2	},
{"ldc1",    "E,o(b)",	0xd4000000, 0xfc000000, CLD|RD_b|WR_T|FP_D,	0,		I2	},
{"ldc1",    "T,A(b)",	0,    (int) M_LDC1_AB,	INSN_MACRO,		0,		I2	},
{"ldc1",    "E,A(b)",	0,    (int) M_LDC1_AB,	INSN_MACRO,		0,		I2	},
{"l.d",     "T,o(b)",	0xd4000000, 0xfc000000, CLD|RD_b|WR_T|FP_D,	0,		I2	}, /* ldc1 */
{"l.d",     "T,o(b)",	0,    (int) M_L_DOB,	INSN_MACRO,		0,		I1	},
{"l.d",     "T,A(b)",	0,    (int) M_L_DAB,	INSN_MACRO,		0,		I1	},
{"ldc2",    "E,o(b)",	0xd8000000, 0xfc000000, CLD|RD_b|WR_CC,		0,		I2	},
{"ldc2",    "E,A(b)",	0,    (int) M_LDC2_AB,	INSN_MACRO,		0,		I2	},
{"ldc3",    "E,o(b)",	0xdc000000, 0xfc000000, CLD|RD_b|WR_CC,		0,		I2	},
{"ldc3",    "E,A(b)",	0,    (int) M_LDC3_AB,	INSN_MACRO,		0,		I2	},
{"ldl",	    "t,o(b)",	0x68000000, 0xfc000000, LDD|WR_t|RD_b,		0,		I3	},
{"ldl",	    "t,A(b)",	0,    (int) M_LDL_AB,	INSN_MACRO,		0,		I3	},
{"ldr",	    "t,o(b)",	0x6c000000, 0xfc000000, LDD|WR_t|RD_b,		0,		I3	},
{"ldr",     "t,A(b)",	0,    (int) M_LDR_AB,	INSN_MACRO,		0,		I3	},
{"ldxc1",   "D,t(b)",	0x4c000001, 0xfc00f83f, LDD|WR_D|RD_t|RD_b|FP_D, 0,		I4|I33	},
{"lh",      "t,o(b)",	0x84000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lh",      "t,A(b)",	0,    (int) M_LH_AB,	INSN_MACRO,		0,		I1	},
{"lhu",     "t,o(b)",	0x94000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lhu",     "t,A(b)",	0,    (int) M_LHU_AB,	INSN_MACRO,		0,		I1	},
/* li is at the start of the table.  */
{"li.d",    "t,F",	0,    (int) M_LI_D,	INSN_MACRO,		0,		I1	},
{"li.d",    "T,L",	0,    (int) M_LI_DD,	INSN_MACRO,		0,		I1	},
{"li.s",    "t,f",	0,    (int) M_LI_S,	INSN_MACRO,		0,		I1	},
{"li.s",    "T,l",	0,    (int) M_LI_SS,	INSN_MACRO,		0,		I1	},
{"ll",	    "t,o(b)",	0xc0000000, 0xfc000000, LDD|RD_b|WR_t,		0,		I2	},
{"ll",	    "t,A(b)",	0,    (int) M_LL_AB,	INSN_MACRO,		0,		I2	},
{"lld",	    "t,o(b)",	0xd0000000, 0xfc000000, LDD|RD_b|WR_t,		0,		I3	},
{"lld",     "t,A(b)",	0,    (int) M_LLD_AB,	INSN_MACRO,		0,		I3	},
{"lui",     "t,u",	0x3c000000, 0xffe00000,	WR_t,			0,		I1	},
{"luxc1",   "D,t(b)",	0x4c000005, 0xfc00f83f, LDD|WR_D|RD_t|RD_b|FP_D, 0,		I5|I33|N55},
{"lw",      "t,o(b)",	0x8c000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lw",      "t,A(b)",	0,    (int) M_LW_AB,	INSN_MACRO,		0,		I1	},
{"lwc0",    "E,o(b)",	0xc0000000, 0xfc000000,	CLD|RD_b|WR_CC,		0,		I1	},
{"lwc0",    "E,A(b)",	0,    (int) M_LWC0_AB,	INSN_MACRO,		0,		I1	},
{"lwc1",    "T,o(b)",	0xc4000000, 0xfc000000,	CLD|RD_b|WR_T|FP_S,	0,		I1	},
{"lwc1",    "E,o(b)",	0xc4000000, 0xfc000000,	CLD|RD_b|WR_T|FP_S,	0,		I1	},
{"lwc1",    "T,A(b)",	0,    (int) M_LWC1_AB,	INSN_MACRO,		0,		I1	},
{"lwc1",    "E,A(b)",	0,    (int) M_LWC1_AB,	INSN_MACRO,		0,		I1	},
{"l.s",     "T,o(b)",	0xc4000000, 0xfc000000,	CLD|RD_b|WR_T|FP_S,	0,		I1	}, /* lwc1 */
{"l.s",     "T,A(b)",	0,    (int) M_LWC1_AB,	INSN_MACRO,		0,		I1	},
{"lwc2",    "E,o(b)",	0xc8000000, 0xfc000000,	CLD|RD_b|WR_CC,		0,		I1	},
{"lwc2",    "E,A(b)",	0,    (int) M_LWC2_AB,	INSN_MACRO,		0,		I1	},
{"lwc3",    "E,o(b)",	0xcc000000, 0xfc000000,	CLD|RD_b|WR_CC,		0,		I1	},
{"lwc3",    "E,A(b)",	0,    (int) M_LWC3_AB,	INSN_MACRO,		0,		I1	},
{"lwl",     "t,o(b)",	0x88000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lwl",     "t,A(b)",	0,    (int) M_LWL_AB,	INSN_MACRO,		0,		I1	},
{"lcache",  "t,o(b)",	0x88000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I2	}, /* same */
{"lcache",  "t,A(b)",	0,    (int) M_LWL_AB,	INSN_MACRO,		0,		I2	}, /* as lwl */
{"lwr",     "t,o(b)",	0x98000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I1	},
{"lwr",     "t,A(b)",	0,    (int) M_LWR_AB,	INSN_MACRO,		0,		I1	},
{"flush",   "t,o(b)",	0x98000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I2	}, /* same */
{"flush",   "t,A(b)",	0,    (int) M_LWR_AB,	INSN_MACRO,		0,		I2	}, /* as lwr */
{"fork",    "d,s,t",	0x7c000008, 0xfc0007ff, TRAP|WR_d|RD_s|RD_t,	0,		MT32	},
{"lwu",     "t,o(b)",	0x9c000000, 0xfc000000,	LDD|RD_b|WR_t,		0,		I3	},
{"lwu",     "t,A(b)",	0,    (int) M_LWU_AB,	INSN_MACRO,		0,		I3	},
{"lwxc1",   "D,t(b)",	0x4c000000, 0xfc00f83f, LDD|WR_D|RD_t|RD_b|FP_D, 0,		I4|I33	},
{"lwxs",    "d,t(b)",	0x70000088, 0xfc0007ff,	LDD|RD_b|RD_t|WR_d,	0,		SMT	},
{"macc",    "d,s,t",	0x00000028, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"macc",    "d,s,t",	0x00000158, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d,	0,		N5      },
{"maccs",   "d,s,t",	0x00000428, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"macchi",  "d,s,t",	0x00000228, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"macchi",  "d,s,t",	0x00000358, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5      },
{"macchis", "d,s,t",	0x00000628, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"macchiu", "d,s,t",	0x00000268, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"macchiu", "d,s,t",	0x00000359, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d,	0,		N5      },
{"macchius","d,s,t",	0x00000668, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"maccu",   "d,s,t",	0x00000068, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"maccu",   "d,s,t",	0x00000159, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d,	0,		N5      },
{"maccus",  "d,s,t",	0x00000468, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d, 0,		N412    },
{"mad",     "s,t",      0x70000000, 0xfc00ffff, RD_s|RD_t|MOD_HILO,     0,		P3      },
{"madu",    "s,t",      0x70000001, 0xfc00ffff, RD_s|RD_t|MOD_HILO,     0,		P3      },
{"madd.d",  "D,R,S,T",	0x4c000021, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D,    0,		I4|I33	},
{"madd.s",  "D,R,S,T",	0x4c000020, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_S,    0,		I4|I33	},
{"madd.ps", "D,R,S,T",	0x4c000026, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D,    0,		I5|I33	},
{"madd",    "s,t",      0x0000001c, 0xfc00ffff, RD_s|RD_t|WR_HILO,           0,		L1	},
{"madd",    "s,t",      0x70000000, 0xfc00ffff, RD_s|RD_t|MOD_HILO,          0,		I32|N55	},
{"madd",    "s,t",      0x70000000, 0xfc00ffff, RD_s|RD_t|WR_HILO|IS_M,      0,		G1	},
{"madd",    "7,s,t",	0x70000000, 0xfc00e7ff, MOD_a|RD_s|RD_t,             0,         D33	},
{"madd",    "d,s,t",    0x70000000, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d|IS_M, 0,		G1	},
{"maddp",   "s,t",      0x70000441, 0xfc00ffff,	RD_s|RD_t|MOD_HILO,	     0,		SMT	},
{"maddu",   "s,t",      0x0000001d, 0xfc00ffff, RD_s|RD_t|WR_HILO,           0,		L1	},
{"maddu",   "s,t",      0x70000001, 0xfc00ffff, RD_s|RD_t|MOD_HILO,          0,		I32|N55	},
{"maddu",   "s,t",      0x70000001, 0xfc00ffff, RD_s|RD_t|WR_HILO|IS_M,      0,		G1	},
{"maddu",   "7,s,t",	0x70000001, 0xfc00e7ff, MOD_a|RD_s|RD_t,             0,         D33	},
{"maddu",   "d,s,t",    0x70000001, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d|IS_M, 0,		G1	},
{"madd16",  "s,t",      0x00000028, 0xfc00ffff, RD_s|RD_t|MOD_HILO,	0,		N411    },
{"max.ob",  "X,Y,Q",	0x78000007, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"max.ob",  "D,S,T",	0x4ac00007, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"max.ob",  "D,S,T[e]",	0x48000007, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"max.ob",  "D,S,k",	0x4bc00007, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"max.qh",  "X,Y,Q",	0x78200007, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"mfpc",    "t,P",	0x4000c801, 0xffe0ffc1,	LCD|WR_t|RD_C0,		0,		M1|N5	},
{"mfps",    "t,P",	0x4000c800, 0xffe0ffc1,	LCD|WR_t|RD_C0,		0,		M1|N5	},
{"mftacx",  "d",	0x41020021, 0xffff07ff, TRAP|WR_d|RD_a,		0,		MT32	},
{"mftacx",  "d,*",	0x41020021, 0xfff307ff, TRAP|WR_d|RD_a,		0,		MT32	},
{"mftc0",   "d,+t",	0x41000000, 0xffe007ff, TRAP|LCD|WR_d|RD_C0,	0,		MT32	},
{"mftc0",   "d,+T",	0x41000000, 0xffe007f8, TRAP|LCD|WR_d|RD_C0,	0,		MT32	},
{"mftc0",   "d,E,H",	0x41000000, 0xffe007f8, TRAP|LCD|WR_d|RD_C0,	0,		MT32	},
{"mftc1",   "d,T",	0x41000022, 0xffe007ff, TRAP|LCD|WR_d|RD_T|FP_S, 0,		MT32	},
{"mftc1",   "d,E",	0x41000022, 0xffe007ff, TRAP|LCD|WR_d|RD_T|FP_S, 0,		MT32	},
{"mftc2",   "d,E",	0x41000024, 0xffe007ff, TRAP|LCD|WR_d|RD_C2,	0,		MT32	},
{"mftdsp",  "d",	0x41100021, 0xffff07ff, TRAP|WR_d,		0,		MT32	},
{"mftgpr",  "d,t",	0x41000020, 0xffe007ff, TRAP|WR_d|RD_t,		0,		MT32	},
{"mfthc1",  "d,T",	0x41000032, 0xffe007ff, TRAP|LCD|WR_d|RD_T|FP_D, 0,		MT32	},
{"mfthc1",  "d,E",	0x41000032, 0xffe007ff, TRAP|LCD|WR_d|RD_T|FP_D, 0,		MT32	},
{"mfthc2",  "d,E",	0x41000034, 0xffe007ff, TRAP|LCD|WR_d|RD_C2,	0,		MT32	},
{"mfthi",   "d",	0x41010021, 0xffff07ff, TRAP|WR_d|RD_a,		0,		MT32	},
{"mfthi",   "d,*",	0x41010021, 0xfff307ff, TRAP|WR_d|RD_a,		0,		MT32	},
{"mftlo",   "d",	0x41000021, 0xffff07ff, TRAP|WR_d|RD_a,		0,		MT32	},
{"mftlo",   "d,*",	0x41000021, 0xfff307ff, TRAP|WR_d|RD_a,		0,		MT32	},
{"mftr",    "d,t,!,H,$", 0x41000000, 0xffe007c8, TRAP|WR_d,		0,		MT32	},
{"mfc0",    "t,G",	0x40000000, 0xffe007ff,	LCD|WR_t|RD_C0,		0,		I1	},
{"mfc0",    "t,+D",     0x40000000, 0xffe007f8, LCD|WR_t|RD_C0, 	0,		I32     },
{"mfc0",    "t,G,H",    0x40000000, 0xffe007f8, LCD|WR_t|RD_C0, 	0,		I32     },
{"mfc1",    "t,S",	0x44000000, 0xffe007ff,	LCD|WR_t|RD_S|FP_S,	0,		I1	},
{"mfc1",    "t,G",	0x44000000, 0xffe007ff,	LCD|WR_t|RD_S|FP_S,	0,		I1	},
{"mfhc1",   "t,S",	0x44600000, 0xffe007ff,	LCD|WR_t|RD_S|FP_D,	0,		I33	},
{"mfhc1",   "t,G",	0x44600000, 0xffe007ff,	LCD|WR_t|RD_S|FP_D,	0,		I33	},
/* mfc2 is at the bottom of the table.  */
/* mfhc2 is at the bottom of the table.  */
/* mfc3 is at the bottom of the table.  */
{"mfdr",    "t,G",	0x7000003d, 0xffe007ff,	LCD|WR_t|RD_C0,		0,		N5      },
{"mfhi",    "d",	0x00000010, 0xffff07ff,	WR_d|RD_HI,		0,		I1	},
{"mfhi",    "d,9",	0x00000010, 0xff9f07ff, WR_d|RD_HI,		0,		D32	},
{"mflo",    "d",	0x00000012, 0xffff07ff,	WR_d|RD_LO,		0,		I1	},
{"mflo",    "d,9",	0x00000012, 0xff9f07ff, WR_d|RD_LO,		0,		D32	},
{"mflhxu",  "d",	0x00000052, 0xffff07ff,	WR_d|MOD_HILO,		0,		SMT	},
{"min.ob",  "X,Y,Q",	0x78000006, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"min.ob",  "D,S,T",	0x4ac00006, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"min.ob",  "D,S,T[e]",	0x48000006, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"min.ob",  "D,S,k",	0x4bc00006, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"min.qh",  "X,Y,Q",	0x78200006, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"mov.d",   "D,S",	0x46200006, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I1	},
{"mov.s",   "D,S",	0x46000006, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I1	},
{"mov.ps",  "D,S",	0x46c00006, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I5|I33	},
{"movf",    "d,s,N",    0x00000001, 0xfc0307ff, WR_d|RD_s|RD_CC|FP_S|FP_D, 0,		I4|I32  },
{"movf.d",  "D,S,N",    0x46200011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,   0,		I4|I32	},
{"movf.l",  "D,S,N",	0x46a00011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,	0,		MX|SB1	},
{"movf.l",  "X,Y,N",	0x46a00011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,	0,		MX|SB1	},
{"movf.s",  "D,S,N",    0x46000011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_S,   0,		I4|I32	},
{"movf.ps", "D,S,N",	0x46c00011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,	0,		I5|I33	},
{"movn",    "d,v,t",    0x0000000b, 0xfc0007ff, WR_d|RD_s|RD_t, 	0,		I4|I32	},
{"ffc",     "d,v",	0x0000000b, 0xfc1f07ff,	WR_d|RD_s,		0,		L1	},
{"movn.d",  "D,S,t",    0x46200013, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		I4|I32	},
{"movn.l",  "D,S,t",    0x46a00013, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		MX|SB1	},
{"movn.l",  "X,Y,t",    0x46a00013, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		MX|SB1	},
{"movn.s",  "D,S,t",    0x46000013, 0xffe0003f, WR_D|RD_S|RD_t|FP_S,    0,		I4|I32	},
{"movn.ps", "D,S,t",    0x46c00013, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		I5|I33	},
{"movt",    "d,s,N",    0x00010001, 0xfc0307ff, WR_d|RD_s|RD_CC|FP_S|FP_D, 0,		I4|I32	},
{"movt.d",  "D,S,N",    0x46210011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,   0,		I4|I32	},
{"movt.l",  "D,S,N",    0x46a10011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,   0,		MX|SB1	},
{"movt.l",  "X,Y,N",    0x46a10011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,   0,		MX|SB1	},
{"movt.s",  "D,S,N",    0x46010011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_S,   0,		I4|I32	},
{"movt.ps", "D,S,N",	0x46c10011, 0xffe3003f, WR_D|RD_S|RD_CC|FP_D,	0,		I5|I33	},
{"movz",    "d,v,t",    0x0000000a, 0xfc0007ff, WR_d|RD_s|RD_t, 	0,		I4|I32	},
{"ffs",     "d,v",	0x0000000a, 0xfc1f07ff,	WR_d|RD_s,		0,		L1	},
{"movz.d",  "D,S,t",    0x46200012, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		I4|I32	},
{"movz.l",  "D,S,t",    0x46a00012, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		MX|SB1	},
{"movz.l",  "X,Y,t",    0x46a00012, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		MX|SB1	},
{"movz.s",  "D,S,t",    0x46000012, 0xffe0003f, WR_D|RD_S|RD_t|FP_S,    0,		I4|I32	},
{"movz.ps", "D,S,t",    0x46c00012, 0xffe0003f, WR_D|RD_S|RD_t|FP_D,    0,		I5|I33	},
{"msac",    "d,s,t",	0x000001d8, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"msacu",   "d,s,t",	0x000001d9, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"msachi",  "d,s,t",	0x000003d8, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"msachiu", "d,s,t",	0x000003d9, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
/* move is at the top of the table.  */
{"msgn.qh", "X,Y,Q",	0x78200000, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"msub.d",  "D,R,S,T",	0x4c000029, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D, 0,		I4|I33	},
{"msub.s",  "D,R,S,T",	0x4c000028, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_S, 0,		I4|I33	},
{"msub.ps", "D,R,S,T",	0x4c00002e, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D, 0,		I5|I33	},
{"msub",    "s,t",      0x0000001e, 0xfc00ffff, RD_s|RD_t|WR_HILO,	0,		L1    	},
{"msub",    "s,t",      0x70000004, 0xfc00ffff, RD_s|RD_t|MOD_HILO,     0,		I32|N55 },
{"msub",    "7,s,t",	0x70000004, 0xfc00e7ff, MOD_a|RD_s|RD_t,        0,              D33	},
{"msubu",   "s,t",      0x0000001f, 0xfc00ffff, RD_s|RD_t|WR_HILO,	0,		L1	},
{"msubu",   "s,t",      0x70000005, 0xfc00ffff, RD_s|RD_t|MOD_HILO,     0,		I32|N55	},
{"msubu",   "7,s,t",	0x70000005, 0xfc00e7ff, MOD_a|RD_s|RD_t,        0,              D33	},
{"mtpc",    "t,P",	0x4080c801, 0xffe0ffc1,	COD|RD_t|WR_C0,		0,		M1|N5	},
{"mtps",    "t,P",	0x4080c800, 0xffe0ffc1,	COD|RD_t|WR_C0,		0,		M1|N5	},
{"mtc0",    "t,G",	0x40800000, 0xffe007ff,	COD|RD_t|WR_C0|WR_CC,	0,		I1	},
{"mtc0",    "t,+D",     0x40800000, 0xffe007f8, COD|RD_t|WR_C0|WR_CC,   0,		I32     },
{"mtc0",    "t,G,H",    0x40800000, 0xffe007f8, COD|RD_t|WR_C0|WR_CC,   0,		I32     },
{"mtc1",    "t,S",	0x44800000, 0xffe007ff,	COD|RD_t|WR_S|FP_S,	0,		I1	},
{"mtc1",    "t,G",	0x44800000, 0xffe007ff,	COD|RD_t|WR_S|FP_S,	0,		I1	},
{"mthc1",   "t,S",	0x44e00000, 0xffe007ff,	COD|RD_t|WR_S|FP_D,	0,		I33	},
{"mthc1",   "t,G",	0x44e00000, 0xffe007ff,	COD|RD_t|WR_S|FP_D,	0,		I33	},
/* mtc2 is at the bottom of the table.  */
/* mthc2 is at the bottom of the table.  */
/* mtc3 is at the bottom of the table.  */
{"mtdr",    "t,G",	0x7080003d, 0xffe007ff,	COD|RD_t|WR_C0,		0,		N5	},
{"mthi",    "s",	0x00000011, 0xfc1fffff,	RD_s|WR_HI,		0,		I1	},
{"mthi",    "s,7",	0x00000011, 0xfc1fe7ff, RD_s|WR_HI,		0,		D32	},
{"mtlo",    "s",	0x00000013, 0xfc1fffff,	RD_s|WR_LO,		0,		I1	},
{"mtlo",    "s,7",	0x00000013, 0xfc1fe7ff, RD_s|WR_LO,		0,		D32	},
{"mtlhx",   "s",	0x00000053, 0xfc1fffff,	RD_s|MOD_HILO,		0,		SMT	},
{"mttc0",   "t,G",	0x41800000, 0xffe007ff, TRAP|COD|RD_t|WR_C0|WR_CC, 0,		MT32	},
{"mttc0",   "t,+D",	0x41800000, 0xffe007f8, TRAP|COD|RD_t|WR_C0|WR_CC, 0,		MT32	},
{"mttc0",   "t,G,H",	0x41800000, 0xffe007f8, TRAP|COD|RD_t|WR_C0|WR_CC, 0,		MT32	},
{"mttc1",   "t,S",	0x41800022, 0xffe007ff, TRAP|COD|RD_t|WR_S|FP_S, 0,		MT32	},
{"mttc1",   "t,G",	0x41800022, 0xffe007ff, TRAP|COD|RD_t|WR_S|FP_S, 0,		MT32	},
{"mttc2",   "t,g",	0x41800024, 0xffe007ff, TRAP|COD|RD_t|WR_C2|WR_CC, 0,		MT32	},
{"mttacx",  "t",	0x41801021, 0xffe0ffff, TRAP|WR_a|RD_t,		0,		MT32	},
{"mttacx",  "t,&",	0x41801021, 0xffe09fff, TRAP|WR_a|RD_t,		0,		MT32	},
{"mttdsp",  "t",	0x41808021, 0xffe0ffff, TRAP|RD_t,		0,		MT32	},
{"mttgpr",  "t,d",	0x41800020, 0xffe007ff, TRAP|WR_d|RD_t,		0,		MT32	},
{"mtthc1",  "t,S",	0x41800032, 0xffe007ff, TRAP|COD|RD_t|WR_S|FP_D, 0,		MT32	},
{"mtthc1",  "t,G",	0x41800032, 0xffe007ff, TRAP|COD|RD_t|WR_S|FP_D, 0,		MT32	},
{"mtthc2",  "t,g",	0x41800034, 0xffe007ff, TRAP|COD|RD_t|WR_C2|WR_CC, 0,		MT32	},
{"mtthi",   "t",	0x41800821, 0xffe0ffff, TRAP|WR_a|RD_t,		0,		MT32	},
{"mtthi",   "t,&",	0x41800821, 0xffe09fff, TRAP|WR_a|RD_t,		0,		MT32	},
{"mttlo",   "t",	0x41800021, 0xffe0ffff, TRAP|WR_a|RD_t,		0,		MT32	},
{"mttlo",   "t,&",	0x41800021, 0xffe09fff, TRAP|WR_a|RD_t,		0,		MT32	},
{"mttr",    "t,d,!,H,$", 0x41800000, 0xffe007c8, TRAP|RD_t,		0,		MT32	},
{"mul.d",   "D,V,T",	0x46200002, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I1	},
{"mul.s",   "D,V,T",	0x46000002, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		I1	},
{"mul.ob",  "X,Y,Q",	0x78000030, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"mul.ob",  "D,S,T",	0x4ac00030, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"mul.ob",  "D,S,T[e]",	0x48000030, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"mul.ob",  "D,S,k",	0x4bc00030, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"mul.ps",  "D,V,T",	0x46c00002, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"mul.qh",  "X,Y,Q",	0x78200030, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"mul",     "d,v,t",    0x70000002, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		I32|P3|N55},
{"mul",     "d,s,t",	0x00000058, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N54	},
{"mul",     "d,v,t",	0,    (int) M_MUL,	INSN_MACRO,		0,		I1	},
{"mul",     "d,v,I",	0,    (int) M_MUL_I,	INSN_MACRO,		0,		I1	},
{"mula.ob", "Y,Q",	0x78000033, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"mula.ob", "S,T",	0x4ac00033, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mula.ob", "S,T[e]",	0x48000033, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mula.ob", "S,k",	0x4bc00033, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mula.qh", "Y,Q",	0x78200033, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"mulhi",   "d,s,t",	0x00000258, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"mulhiu",  "d,s,t",	0x00000259, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"mull.ob", "Y,Q",	0x78000433, 0xfc2007ff,	RD_S|RD_T|FP_D, 	WR_MACC,	MX|SB1	},
{"mull.ob", "S,T",	0x4ac00433, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mull.ob", "S,T[e]",	0x48000433, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mull.ob", "S,k",	0x4bc00433, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mull.qh", "Y,Q",	0x78200433, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"mulo",    "d,v,t",	0,    (int) M_MULO,	INSN_MACRO,		0,		I1	},
{"mulo",    "d,v,I",	0,    (int) M_MULO_I,	INSN_MACRO,		0,		I1	},
{"mulou",   "d,v,t",	0,    (int) M_MULOU,	INSN_MACRO,		0,		I1	},
{"mulou",   "d,v,I",	0,    (int) M_MULOU_I,	INSN_MACRO,		0,		I1	},
{"mulr.ps", "D,S,T",	0x46c0001a, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		M3D	},
{"muls",    "d,s,t",	0x000000d8, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"mulsu",   "d,s,t",	0x000000d9, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"mulshi",  "d,s,t",	0x000002d8, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"mulshiu", "d,s,t",	0x000002d9, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"muls.ob", "Y,Q",	0x78000032, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"muls.ob", "S,T",	0x4ac00032, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"muls.ob", "S,T[e]",	0x48000032, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"muls.ob", "S,k",	0x4bc00032, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"muls.qh", "Y,Q",	0x78200032, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"mulsl.ob", "Y,Q",	0x78000432, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"mulsl.ob", "S,T",	0x4ac00432, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mulsl.ob", "S,T[e]",	0x48000432, 0xfe2007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mulsl.ob", "S,k",	0x4bc00432, 0xffe007ff,	WR_CC|RD_S|RD_T,	0,		N54	},
{"mulsl.qh", "Y,Q",	0x78200432, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"mult",    "s,t",      0x00000018, 0xfc00ffff, RD_s|RD_t|WR_HILO|IS_M, 0,		I1	},
{"mult",    "7,s,t",	0x00000018, 0xfc00e7ff, WR_a|RD_s|RD_t,         0,              D33	},
{"mult",    "d,s,t",    0x00000018, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d|IS_M, 0,		G1	},
{"multp",   "s,t",	0x00000459, 0xfc00ffff,	RD_s|RD_t|MOD_HILO,	0,		SMT	},
{"multu",   "s,t",      0x00000019, 0xfc00ffff, RD_s|RD_t|WR_HILO|IS_M, 0,		I1	},
{"multu",   "7,s,t",	0x00000019, 0xfc00e7ff, WR_a|RD_s|RD_t,         0,              D33	},
{"multu",   "d,s,t",    0x00000019, 0xfc0007ff, RD_s|RD_t|WR_HILO|WR_d|IS_M, 0,		G1	},
{"mulu",    "d,s,t",	0x00000059, 0xfc0007ff,	RD_s|RD_t|WR_HILO|WR_d,	0,		N5	},
{"neg",     "d,w",	0x00000022, 0xffe007ff,	WR_d|RD_t,		0,		I1	}, /* sub 0 */
{"negu",    "d,w",	0x00000023, 0xffe007ff,	WR_d|RD_t,		0,		I1	}, /* subu 0 */
{"neg.d",   "D,V",	0x46200007, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I1	},
{"neg.s",   "D,V",	0x46000007, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I1	},
{"neg.ps",  "D,V",	0x46c00007, 0xffff003f,	WR_D|RD_S|FP_D,		0,		I5|I33	},
{"nmadd.d", "D,R,S,T",	0x4c000031, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D, 0,		I4|I33	},
{"nmadd.s", "D,R,S,T",	0x4c000030, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_S, 0,		I4|I33	},
{"nmadd.ps","D,R,S,T",	0x4c000036, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D, 0,		I5|I33	},
{"nmsub.d", "D,R,S,T",	0x4c000039, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D, 0,		I4|I33	},
{"nmsub.s", "D,R,S,T",	0x4c000038, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_S, 0,		I4|I33	},
{"nmsub.ps","D,R,S,T",	0x4c00003e, 0xfc00003f, RD_R|RD_S|RD_T|WR_D|FP_D, 0,		I5|I33	},
/* nop is at the start of the table.  */
{"nor",     "d,v,t",	0x00000027, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"nor",     "t,r,I",	0,    (int) M_NOR_I,	INSN_MACRO,		0,		I1	},
{"nor.ob",  "X,Y,Q",	0x7800000f, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"nor.ob",  "D,S,T",	0x4ac0000f, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"nor.ob",  "D,S,T[e]",	0x4800000f, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"nor.ob",  "D,S,k",	0x4bc0000f, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"nor.qh",  "X,Y,Q",	0x7820000f, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"not",     "d,v",	0x00000027, 0xfc1f07ff,	WR_d|RD_s|RD_t,		0,		I1	},/*nor d,s,0*/
{"or",      "d,v,t",	0x00000025, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"or",      "t,r,I",	0,    (int) M_OR_I,	INSN_MACRO,		0,		I1	},
{"or.ob",   "X,Y,Q",	0x7800000e, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"or.ob",   "D,S,T",	0x4ac0000e, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"or.ob",   "D,S,T[e]",	0x4800000e, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"or.ob",   "D,S,k",	0x4bc0000e, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"or.qh",   "X,Y,Q",	0x7820000e, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"ori",     "t,r,i",	0x34000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
{"pabsdiff.ob", "X,Y,Q",0x78000009, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		SB1	},
{"pabsdiffc.ob", "Y,Q",	0x78000035, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	SB1	},
{"pavg.ob", "X,Y,Q",	0x78000008, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		SB1	},
{"pickf.ob", "X,Y,Q",	0x78000002, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"pickf.ob", "D,S,T",	0x4ac00002, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"pickf.ob", "D,S,T[e]",0x48000002, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"pickf.ob", "D,S,k",	0x4bc00002, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"pickf.qh", "X,Y,Q",	0x78200002, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"pickt.ob", "X,Y,Q",	0x78000003, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"pickt.ob", "D,S,T",	0x4ac00003, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"pickt.ob", "D,S,T[e]",0x48000003, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"pickt.ob", "D,S,k",	0x4bc00003, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"pickt.qh", "X,Y,Q",	0x78200003, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"pll.ps",  "D,V,T",	0x46c0002c, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"plu.ps",  "D,V,T",	0x46c0002d, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
  /* pref and prefx are at the start of the table.  */
{"pul.ps",  "D,V,T",	0x46c0002e, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"puu.ps",  "D,V,T",	0x46c0002f, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"pperm",   "s,t",	0x70000481, 0xfc00ffff,	MOD_HILO|RD_s|RD_t,	0,		SMT	},
{"rach.ob", "X",	0x7a00003f, 0xfffff83f,	WR_D|FP_D,		RD_MACC,	MX|SB1	},
{"rach.ob", "D",	0x4a00003f, 0xfffff83f,	WR_D,			0,		N54	},
{"rach.qh", "X",	0x7a20003f, 0xfffff83f,	WR_D|FP_D,		RD_MACC,	MX	},
{"racl.ob", "X",	0x7800003f, 0xfffff83f,	WR_D|FP_D,		RD_MACC,	MX|SB1	},
{"racl.ob", "D",	0x4800003f, 0xfffff83f,	WR_D,			0,		N54	},
{"racl.qh", "X",	0x7820003f, 0xfffff83f,	WR_D|FP_D,		RD_MACC,	MX	},
{"racm.ob", "X",	0x7900003f, 0xfffff83f,	WR_D|FP_D,		RD_MACC,	MX|SB1	},
{"racm.ob", "D",	0x4900003f, 0xfffff83f,	WR_D,			0,		N54	},
{"racm.qh", "X",	0x7920003f, 0xfffff83f,	WR_D|FP_D,		RD_MACC,	MX	},
{"recip.d", "D,S",	0x46200015, 0xffff003f, WR_D|RD_S|FP_D,		0,		I4|I33	},
{"recip.ps","D,S",	0x46c00015, 0xffff003f, WR_D|RD_S|FP_D,		0,		SB1	},
{"recip.s", "D,S",	0x46000015, 0xffff003f, WR_D|RD_S|FP_S,		0,		I4|I33	},
{"recip1.d",  "D,S",	0x4620001d, 0xffff003f,	WR_D|RD_S|FP_D,		0,		M3D	},
{"recip1.ps", "D,S",	0x46c0001d, 0xffff003f,	WR_D|RD_S|FP_S,		0,		M3D	},
{"recip1.s",  "D,S",	0x4600001d, 0xffff003f,	WR_D|RD_S|FP_S,		0,		M3D	},
{"recip2.d",  "D,S,T",	0x4620001c, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		M3D	},
{"recip2.ps", "D,S,T",	0x46c0001c, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		M3D	},
{"recip2.s",  "D,S,T",	0x4600001c, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		M3D	},
{"rem",     "z,s,t",    0x0000001a, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I1	},
{"rem",     "d,v,t",	0,    (int) M_REM_3,	INSN_MACRO,		0,		I1	},
{"rem",     "d,v,I",	0,    (int) M_REM_3I,	INSN_MACRO,		0,		I1	},
{"remu",    "z,s,t",    0x0000001b, 0xfc00ffff, RD_s|RD_t|WR_HILO,      0,		I1	},
{"remu",    "d,v,t",	0,    (int) M_REMU_3,	INSN_MACRO,		0,		I1	},
{"remu",    "d,v,I",	0,    (int) M_REMU_3I,	INSN_MACRO,		0,		I1	},
{"rdhwr",   "t,K",	0x7c00003b, 0xffe007ff, WR_t,			0,		I33	},
{"rdpgpr",  "d,w",	0x41400000, 0xffe007ff, WR_d,			0,		I33	},
{"rfe",     "",		0x42000010, 0xffffffff,	0,			0,		I1|T3	},
{"rnas.qh", "X,Q",	0x78200025, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX	},
{"rnau.ob", "X,Q",	0x78000021, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX|SB1	},
{"rnau.qh", "X,Q",	0x78200021, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX	},
{"rnes.qh", "X,Q",	0x78200026, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX	},
{"rneu.ob", "X,Q",	0x78000022, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX|SB1	},
{"rneu.qh", "X,Q",	0x78200022, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX	},
{"rol",     "d,v,t",	0,    (int) M_ROL,	INSN_MACRO,		0,		I1	},
{"rol",     "d,v,I",	0,    (int) M_ROL_I,	INSN_MACRO,		0,		I1	},
{"ror",     "d,v,t",	0,    (int) M_ROR,	INSN_MACRO,		0,		I1	},
{"ror",     "d,v,I",	0,    (int) M_ROR_I,	INSN_MACRO,		0,		I1	},
{"ror",	    "d,w,<",	0x00200002, 0xffe0003f,	WR_d|RD_t,		0,		N5|I33|SMT },
{"rorv",    "d,t,s",	0x00000046, 0xfc0007ff,	RD_t|RD_s|WR_d,		0,		N5|I33|SMT },
{"rotl",    "d,v,t",	0,    (int) M_ROL,	INSN_MACRO,		0,		I33|SMT	},
{"rotl",    "d,v,I",	0,    (int) M_ROL_I,	INSN_MACRO,		0,		I33|SMT	},
{"rotr",    "d,v,t",	0,    (int) M_ROR,	INSN_MACRO,		0,		I33|SMT	},
{"rotr",    "d,v,I",	0,    (int) M_ROR_I,	INSN_MACRO,		0,		I33|SMT	},
{"rotrv",   "d,t,s",	0x00000046, 0xfc0007ff,	RD_t|RD_s|WR_d,		0,		I33|SMT	},
{"round.l.d", "D,S",	0x46200008, 0xffff003f, WR_D|RD_S|FP_D,		0,		I3|I33	},
{"round.l.s", "D,S",	0x46000008, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I3|I33	},
{"round.w.d", "D,S",	0x4620000c, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I2	},
{"round.w.s", "D,S",	0x4600000c, 0xffff003f, WR_D|RD_S|FP_S,		0,		I2	},
{"rsqrt.d", "D,S",	0x46200016, 0xffff003f, WR_D|RD_S|FP_D,		0,		I4|I33	},
{"rsqrt.ps","D,S",	0x46c00016, 0xffff003f, WR_D|RD_S|FP_D,		0,		SB1	},
{"rsqrt.s", "D,S",	0x46000016, 0xffff003f, WR_D|RD_S|FP_S,		0,		I4|I33	},
{"rsqrt1.d",  "D,S",	0x4620001e, 0xffff003f,	WR_D|RD_S|FP_D,		0,		M3D	},
{"rsqrt1.ps", "D,S",	0x46c0001e, 0xffff003f,	WR_D|RD_S|FP_S,		0,		M3D	},
{"rsqrt1.s",  "D,S",	0x4600001e, 0xffff003f,	WR_D|RD_S|FP_S,		0,		M3D	},
{"rsqrt2.d",  "D,S,T",	0x4620001f, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		M3D	},
{"rsqrt2.ps", "D,S,T",	0x46c0001f, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		M3D	},
{"rsqrt2.s",  "D,S,T",	0x4600001f, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		M3D	},
{"rzs.qh",  "X,Q",	0x78200024, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX	},
{"rzu.ob",  "X,Q",	0x78000020, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX|SB1	},
{"rzu.ob",  "D,k",	0x4bc00020, 0xffe0f83f,	WR_D|RD_S|RD_T,		0,		N54	},
{"rzu.qh",  "X,Q",	0x78200020, 0xfc20f83f,	WR_D|RD_T|FP_D,		RD_MACC,	MX	},
{"sb",      "t,o(b)",	0xa0000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I1	},
{"sb",      "t,A(b)",	0,    (int) M_SB_AB,	INSN_MACRO,		0,		I1	},
{"sc",	    "t,o(b)",	0xe0000000, 0xfc000000, SM|RD_t|WR_t|RD_b,	0,		I2	},
{"sc",	    "t,A(b)",	0,    (int) M_SC_AB,	INSN_MACRO,		0,		I2	},
{"scd",	    "t,o(b)",	0xf0000000, 0xfc000000, SM|RD_t|WR_t|RD_b,	0,		I3	},
{"scd",	    "t,A(b)",	0,    (int) M_SCD_AB,	INSN_MACRO,		0,		I3	},
{"sd",	    "t,o(b)",	0xfc000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I3	},
{"sd",      "t,o(b)",	0,    (int) M_SD_OB,	INSN_MACRO,		0,		I1	},
{"sd",      "t,A(b)",	0,    (int) M_SD_AB,	INSN_MACRO,		0,		I1	},
{"sdbbp",   "",		0x0000000e, 0xffffffff,	TRAP,           	0,		G2	},
{"sdbbp",   "c",	0x0000000e, 0xfc00ffff,	TRAP,			0,		G2	},
{"sdbbp",   "c,q",	0x0000000e, 0xfc00003f,	TRAP,			0,		G2	},
{"sdbbp",   "",         0x7000003f, 0xffffffff, TRAP,           	0,		I32     },
{"sdbbp",   "B",        0x7000003f, 0xfc00003f, TRAP,           	0,		I32     },
{"sdc1",    "T,o(b)",	0xf4000000, 0xfc000000, SM|RD_T|RD_b|FP_D,	0,		I2	},
{"sdc1",    "E,o(b)",	0xf4000000, 0xfc000000, SM|RD_T|RD_b|FP_D,	0,		I2	},
{"sdc1",    "T,A(b)",	0,    (int) M_SDC1_AB,	INSN_MACRO,		0,		I2	},
{"sdc1",    "E,A(b)",	0,    (int) M_SDC1_AB,	INSN_MACRO,		0,		I2	},
{"sdc2",    "E,o(b)",	0xf8000000, 0xfc000000, SM|RD_C2|RD_b,		0,		I2	},
{"sdc2",    "E,A(b)",	0,    (int) M_SDC2_AB,	INSN_MACRO,		0,		I2	},
{"sdc3",    "E,o(b)",	0xfc000000, 0xfc000000, SM|RD_C3|RD_b,		0,		I2	},
{"sdc3",    "E,A(b)",	0,    (int) M_SDC3_AB,	INSN_MACRO,		0,		I2	},
{"s.d",     "T,o(b)",	0xf4000000, 0xfc000000, SM|RD_T|RD_b|FP_D,	0,		I2	},
{"s.d",     "T,o(b)",	0,    (int) M_S_DOB,	INSN_MACRO,		0,		I1	},
{"s.d",     "T,A(b)",	0,    (int) M_S_DAB,	INSN_MACRO,		0,		I1	},
{"sdl",     "t,o(b)",	0xb0000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I3	},
{"sdl",     "t,A(b)",	0,    (int) M_SDL_AB,	INSN_MACRO,		0,		I3	},
{"sdr",     "t,o(b)",	0xb4000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I3	},
{"sdr",     "t,A(b)",	0,    (int) M_SDR_AB,	INSN_MACRO,		0,		I3	},
{"sdxc1",   "S,t(b)",   0x4c000009, 0xfc0007ff, SM|RD_S|RD_t|RD_b|FP_D,	0,		I4|I33	},
{"seb",     "d,w",	0x7c000420, 0xffe007ff,	WR_d|RD_t,		0,		I33	},
{"seh",     "d,w",	0x7c000620, 0xffe007ff,	WR_d|RD_t,		0,		I33	},
{"selsl",   "d,v,t",	0x00000005, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		L1	},
{"selsr",   "d,v,t",	0x00000001, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		L1	},
{"seq",     "d,v,t",	0,    (int) M_SEQ,	INSN_MACRO,		0,		I1	},
{"seq",     "d,v,I",	0,    (int) M_SEQ_I,	INSN_MACRO,		0,		I1	},
{"sge",     "d,v,t",	0,    (int) M_SGE,	INSN_MACRO,		0,		I1	},
{"sge",     "d,v,I",	0,    (int) M_SGE_I,	INSN_MACRO,		0,		I1	},
{"sgeu",    "d,v,t",	0,    (int) M_SGEU,	INSN_MACRO,		0,		I1	},
{"sgeu",    "d,v,I",	0,    (int) M_SGEU_I,	INSN_MACRO,		0,		I1	},
{"sgt",     "d,v,t",	0,    (int) M_SGT,	INSN_MACRO,		0,		I1	},
{"sgt",     "d,v,I",	0,    (int) M_SGT_I,	INSN_MACRO,		0,		I1	},
{"sgtu",    "d,v,t",	0,    (int) M_SGTU,	INSN_MACRO,		0,		I1	},
{"sgtu",    "d,v,I",	0,    (int) M_SGTU_I,	INSN_MACRO,		0,		I1	},
{"sh",      "t,o(b)",	0xa4000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I1	},
{"sh",      "t,A(b)",	0,    (int) M_SH_AB,	INSN_MACRO,		0,		I1	},
{"shfl.bfla.qh", "X,Y,Z", 0x7a20001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"shfl.mixh.ob", "X,Y,Z", 0x7980001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"shfl.mixh.ob", "D,S,T", 0x4980001f, 0xffe0003f, WR_D|RD_S|RD_T, 	0,		N54	},
{"shfl.mixh.qh", "X,Y,Z", 0x7820001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"shfl.mixl.ob", "X,Y,Z", 0x79c0001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"shfl.mixl.ob", "D,S,T", 0x49c0001f, 0xffe0003f, WR_D|RD_S|RD_T, 	0,		N54	},
{"shfl.mixl.qh", "X,Y,Z", 0x78a0001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"shfl.pach.ob", "X,Y,Z", 0x7900001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"shfl.pach.ob", "D,S,T", 0x4900001f, 0xffe0003f, WR_D|RD_S|RD_T, 	0,		N54	},
{"shfl.pach.qh", "X,Y,Z", 0x7920001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"shfl.pacl.ob", "D,S,T", 0x4940001f, 0xffe0003f, WR_D|RD_S|RD_T, 	0,		N54	},
{"shfl.repa.qh", "X,Y,Z", 0x7b20001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"shfl.repb.qh", "X,Y,Z", 0x7ba0001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"shfl.upsl.ob", "X,Y,Z", 0x78c0001f, 0xffe0003f, WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"sle",     "d,v,t",	0,    (int) M_SLE,	INSN_MACRO,		0,		I1	},
{"sle",     "d,v,I",	0,    (int) M_SLE_I,	INSN_MACRO,		0,		I1	},
{"sleu",    "d,v,t",	0,    (int) M_SLEU,	INSN_MACRO,		0,		I1	},
{"sleu",    "d,v,I",	0,    (int) M_SLEU_I,	INSN_MACRO,		0,		I1	},
{"sllv",    "d,t,s",	0x00000004, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I1	},
{"sll",     "d,w,s",	0x00000004, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I1	}, /* sllv */
{"sll",     "d,w,<",	0x00000000, 0xffe0003f,	WR_d|RD_t,		0,		I1	},
{"sll.ob",  "X,Y,Q",	0x78000010, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"sll.ob",  "D,S,T[e]",	0x48000010, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"sll.ob",  "D,S,k",	0x4bc00010, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"sll.qh",  "X,Y,Q",	0x78200010, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"slt",     "d,v,t",	0x0000002a, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"slt",     "d,v,I",	0,    (int) M_SLT_I,	INSN_MACRO,		0,		I1	},
{"slti",    "t,r,j",	0x28000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
{"sltiu",   "t,r,j",	0x2c000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
{"sltu",    "d,v,t",	0x0000002b, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"sltu",    "d,v,I",	0,    (int) M_SLTU_I,	INSN_MACRO,		0,		I1	},
{"sne",     "d,v,t",	0,    (int) M_SNE,	INSN_MACRO,		0,		I1	},
{"sne",     "d,v,I",	0,    (int) M_SNE_I,	INSN_MACRO,		0,		I1	},
{"sqrt.d",  "D,S",	0x46200004, 0xffff003f, WR_D|RD_S|FP_D,		0,		I2	},
{"sqrt.s",  "D,S",	0x46000004, 0xffff003f, WR_D|RD_S|FP_S,		0,		I2	},
{"sqrt.ps", "D,S",	0x46c00004, 0xffff003f, WR_D|RD_S|FP_D,		0,		SB1	},
{"srav",    "d,t,s",	0x00000007, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I1	},
{"sra",     "d,w,s",	0x00000007, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I1	}, /* srav */
{"sra",     "d,w,<",	0x00000003, 0xffe0003f,	WR_d|RD_t,		0,		I1	},
{"sra.qh",  "X,Y,Q",	0x78200013, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"srlv",    "d,t,s",	0x00000006, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I1	},
{"srl",     "d,w,s",	0x00000006, 0xfc0007ff,	WR_d|RD_t|RD_s,		0,		I1	}, /* srlv */
{"srl",     "d,w,<",	0x00000002, 0xffe0003f,	WR_d|RD_t,		0,		I1	},
{"srl.ob",  "X,Y,Q",	0x78000012, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"srl.ob",  "D,S,T[e]",	0x48000012, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"srl.ob",  "D,S,k",	0x4bc00012, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"srl.qh",  "X,Y,Q",	0x78200012, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
/* ssnop is at the start of the table.  */
{"standby", "",         0x42000021, 0xffffffff,	0,			0,		V1	},
{"sub",     "d,v,t",	0x00000022, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"sub",     "d,v,I",	0,    (int) M_SUB_I,	INSN_MACRO,		0,		I1	},
{"sub.d",   "D,V,T",	0x46200001, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I1	},
{"sub.s",   "D,V,T",	0x46000001, 0xffe0003f,	WR_D|RD_S|RD_T|FP_S,	0,		I1	},
{"sub.ob",  "X,Y,Q",	0x7800000a, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"sub.ob",  "D,S,T",	0x4ac0000a, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"sub.ob",  "D,S,T[e]",	0x4800000a, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"sub.ob",  "D,S,k",	0x4bc0000a, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"sub.ps",  "D,V,T",	0x46c00001, 0xffe0003f,	WR_D|RD_S|RD_T|FP_D,	0,		I5|I33	},
{"sub.qh",  "X,Y,Q",	0x7820000a, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"suba.ob", "Y,Q",	0x78000036, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"suba.qh", "Y,Q",	0x78200036, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"subl.ob", "Y,Q",	0x78000436, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"subl.qh", "Y,Q",	0x78200436, 0xfc2007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"subu",    "d,v,t",	0x00000023, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"subu",    "d,v,I",	0,    (int) M_SUBU_I,	INSN_MACRO,		0,		I1	},
{"suspend", "",         0x42000022, 0xffffffff,	0,			0,		V1	},
{"suxc1",   "S,t(b)",   0x4c00000d, 0xfc0007ff, SM|RD_S|RD_t|RD_b,	0,		I5|I33|N55},
{"sw",      "t,o(b)",	0xac000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I1	},
{"sw",      "t,A(b)",	0,    (int) M_SW_AB,	INSN_MACRO,		0,		I1	},
{"swc0",    "E,o(b)",	0xe0000000, 0xfc000000,	SM|RD_C0|RD_b,		0,		I1	},
{"swc0",    "E,A(b)",	0,    (int) M_SWC0_AB,	INSN_MACRO,		0,		I1	},
{"swc1",    "T,o(b)",	0xe4000000, 0xfc000000,	SM|RD_T|RD_b|FP_S,	0,		I1	},
{"swc1",    "E,o(b)",	0xe4000000, 0xfc000000,	SM|RD_T|RD_b|FP_S,	0,		I1	},
{"swc1",    "T,A(b)",	0,    (int) M_SWC1_AB,	INSN_MACRO,		0,		I1	},
{"swc1",    "E,A(b)",	0,    (int) M_SWC1_AB,	INSN_MACRO,		0,		I1	},
{"s.s",     "T,o(b)",	0xe4000000, 0xfc000000,	SM|RD_T|RD_b|FP_S,	0,		I1	}, /* swc1 */
{"s.s",     "T,A(b)",	0,    (int) M_SWC1_AB,	INSN_MACRO,		0,		I1	},
{"swc2",    "E,o(b)",	0xe8000000, 0xfc000000,	SM|RD_C2|RD_b,		0,		I1	},
{"swc2",    "E,A(b)",	0,    (int) M_SWC2_AB,	INSN_MACRO,		0,		I1	},
{"swc3",    "E,o(b)",	0xec000000, 0xfc000000,	SM|RD_C3|RD_b,		0,		I1	},
{"swc3",    "E,A(b)",	0,    (int) M_SWC3_AB,	INSN_MACRO,		0,		I1	},
{"swl",     "t,o(b)",	0xa8000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I1	},
{"swl",     "t,A(b)",	0,    (int) M_SWL_AB,	INSN_MACRO,		0,		I1	},
{"scache",  "t,o(b)",	0xa8000000, 0xfc000000,	RD_t|RD_b,		0,		I2	}, /* same */
{"scache",  "t,A(b)",	0,    (int) M_SWL_AB,	INSN_MACRO,		0,		I2	}, /* as swl */
{"swr",     "t,o(b)",	0xb8000000, 0xfc000000,	SM|RD_t|RD_b,		0,		I1	},
{"swr",     "t,A(b)",	0,    (int) M_SWR_AB,	INSN_MACRO,		0,		I1	},
{"invalidate", "t,o(b)",0xb8000000, 0xfc000000,	RD_t|RD_b,		0,		I2	}, /* same */
{"invalidate", "t,A(b)",0,    (int) M_SWR_AB,	INSN_MACRO,		0,		I2	}, /* as swr */
{"swxc1",   "S,t(b)",   0x4c000008, 0xfc0007ff, SM|RD_S|RD_t|RD_b|FP_S,	0,		I4|I33	},
{"sync",    "",		0x0000000f, 0xffffffff,	INSN_SYNC,		0,		I2|G1	},
{"sync.p",  "",		0x0000040f, 0xffffffff,	INSN_SYNC,		0,		I2	},
{"sync.l",  "",		0x0000000f, 0xffffffff,	INSN_SYNC,		0,		I2	},
{"synci",   "o(b)",	0x041f0000, 0xfc1f0000,	SM|RD_b,		0,		I33	},
{"syscall", "",		0x0000000c, 0xffffffff,	TRAP,			0,		I1	},
{"syscall", "B",	0x0000000c, 0xfc00003f,	TRAP,			0,		I1	},
{"teqi",    "s,j",	0x040c0000, 0xfc1f0000, RD_s|TRAP,		0,		I2	},
{"teq",	    "s,t",	0x00000034, 0xfc00ffff, RD_s|RD_t|TRAP,		0,		I2	},
{"teq",	    "s,t,q",	0x00000034, 0xfc00003f, RD_s|RD_t|TRAP,		0,		I2	},
{"teq",     "s,j",	0x040c0000, 0xfc1f0000, RD_s|TRAP,		0,		I2	}, /* teqi */
{"teq",     "s,I",	0,    (int) M_TEQ_I,	INSN_MACRO,		0,		I2	},
{"tgei",    "s,j",	0x04080000, 0xfc1f0000, RD_s|TRAP,		0,		I2	},
{"tge",	    "s,t",	0x00000030, 0xfc00ffff,	RD_s|RD_t|TRAP,		0,		I2	},
{"tge",	    "s,t,q",	0x00000030, 0xfc00003f,	RD_s|RD_t|TRAP,		0,		I2	},
{"tge",     "s,j",	0x04080000, 0xfc1f0000, RD_s|TRAP,		0,		I2	}, /* tgei */
{"tge",	    "s,I",	0,    (int) M_TGE_I,    INSN_MACRO,		0,		I2	},
{"tgeiu",   "s,j",	0x04090000, 0xfc1f0000, RD_s|TRAP,		0,		I2	},
{"tgeu",    "s,t",	0x00000031, 0xfc00ffff, RD_s|RD_t|TRAP,		0,		I2	},
{"tgeu",    "s,t,q",	0x00000031, 0xfc00003f, RD_s|RD_t|TRAP,		0,		I2	},
{"tgeu",    "s,j",	0x04090000, 0xfc1f0000, RD_s|TRAP,		0,		I2	}, /* tgeiu */
{"tgeu",    "s,I",	0,    (int) M_TGEU_I,	INSN_MACRO,		0,		I2	},
{"tlbp",    "",         0x42000008, 0xffffffff, INSN_TLB,       	0,		I1   	},
{"tlbr",    "",         0x42000001, 0xffffffff, INSN_TLB,       	0,		I1   	},
{"tlbwi",   "",         0x42000002, 0xffffffff, INSN_TLB,       	0,		I1   	},
{"tlbwr",   "",         0x42000006, 0xffffffff, INSN_TLB,       	0,		I1   	},
{"tlti",    "s,j",	0x040a0000, 0xfc1f0000,	RD_s|TRAP,		0,		I2	},
{"tlt",     "s,t",	0x00000032, 0xfc00ffff, RD_s|RD_t|TRAP,		0,		I2	},
{"tlt",     "s,t,q",	0x00000032, 0xfc00003f, RD_s|RD_t|TRAP,		0,		I2	},
{"tlt",     "s,j",	0x040a0000, 0xfc1f0000,	RD_s|TRAP,		0,		I2	}, /* tlti */
{"tlt",     "s,I",	0,    (int) M_TLT_I,	INSN_MACRO,		0,		I2	},
{"tltiu",   "s,j",	0x040b0000, 0xfc1f0000, RD_s|TRAP,		0,		I2	},
{"tltu",    "s,t",	0x00000033, 0xfc00ffff, RD_s|RD_t|TRAP,		0,		I2	},
{"tltu",    "s,t,q",	0x00000033, 0xfc00003f, RD_s|RD_t|TRAP,		0,		I2	},
{"tltu",    "s,j",	0x040b0000, 0xfc1f0000, RD_s|TRAP,		0,		I2	}, /* tltiu */
{"tltu",    "s,I",	0,    (int) M_TLTU_I,	INSN_MACRO,		0,		I2	},
{"tnei",    "s,j",	0x040e0000, 0xfc1f0000, RD_s|TRAP,		0,		I2	},
{"tne",     "s,t",	0x00000036, 0xfc00ffff, RD_s|RD_t|TRAP,		0,		I2	},
{"tne",     "s,t,q",	0x00000036, 0xfc00003f, RD_s|RD_t|TRAP,		0,		I2	},
{"tne",     "s,j",	0x040e0000, 0xfc1f0000, RD_s|TRAP,		0,		I2	}, /* tnei */
{"tne",     "s,I",	0,    (int) M_TNE_I,	INSN_MACRO,		0,		I2	},
{"trunc.l.d", "D,S",	0x46200009, 0xffff003f, WR_D|RD_S|FP_D,		0,		I3|I33	},
{"trunc.l.s", "D,S",	0x46000009, 0xffff003f,	WR_D|RD_S|FP_S|FP_D,	0,		I3|I33	},
{"trunc.w.d", "D,S",	0x4620000d, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I2	},
{"trunc.w.d", "D,S,x",	0x4620000d, 0xffff003f, WR_D|RD_S|FP_S|FP_D,	0,		I2	},
{"trunc.w.d", "D,S,t",	0,    (int) M_TRUNCWD,	INSN_MACRO,		0,		I1	},
{"trunc.w.s", "D,S",	0x4600000d, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I2	},
{"trunc.w.s", "D,S,x",	0x4600000d, 0xffff003f,	WR_D|RD_S|FP_S,		0,		I2	},
{"trunc.w.s", "D,S,t",	0,    (int) M_TRUNCWS,	INSN_MACRO,		0,		I1	},
{"uld",     "t,o(b)",	0,    (int) M_ULD,	INSN_MACRO,		0,		I3	},
{"uld",     "t,A(b)",	0,    (int) M_ULD_A,	INSN_MACRO,		0,		I3	},
{"ulh",     "t,o(b)",	0,    (int) M_ULH,	INSN_MACRO,		0,		I1	},
{"ulh",     "t,A(b)",	0,    (int) M_ULH_A,	INSN_MACRO,		0,		I1	},
{"ulhu",    "t,o(b)",	0,    (int) M_ULHU,	INSN_MACRO,		0,		I1	},
{"ulhu",    "t,A(b)",	0,    (int) M_ULHU_A,	INSN_MACRO,		0,		I1	},
{"ulw",     "t,o(b)",	0,    (int) M_ULW,	INSN_MACRO,		0,		I1	},
{"ulw",     "t,A(b)",	0,    (int) M_ULW_A,	INSN_MACRO,		0,		I1	},
{"usd",     "t,o(b)",	0,    (int) M_USD,	INSN_MACRO,		0,		I3	},
{"usd",     "t,A(b)",	0,    (int) M_USD_A,	INSN_MACRO,		0,		I3	},
{"ush",     "t,o(b)",	0,    (int) M_USH,	INSN_MACRO,		0,		I1	},
{"ush",     "t,A(b)",	0,    (int) M_USH_A,	INSN_MACRO,		0,		I1	},
{"usw",     "t,o(b)",	0,    (int) M_USW,	INSN_MACRO,		0,		I1	},
{"usw",     "t,A(b)",	0,    (int) M_USW_A,	INSN_MACRO,		0,		I1	},
{"wach.ob", "Y",	0x7a00003e, 0xffff07ff,	RD_S|FP_D,		WR_MACC,	MX|SB1	},
{"wach.ob", "S",	0x4a00003e, 0xffff07ff,	RD_S,			0,		N54	},
{"wach.qh", "Y",	0x7a20003e, 0xffff07ff,	RD_S|FP_D,		WR_MACC,	MX	},
{"wacl.ob", "Y,Z",	0x7800003e, 0xffe007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX|SB1	},
{"wacl.ob", "S,T",	0x4800003e, 0xffe007ff,	RD_S|RD_T,		0,		N54	},
{"wacl.qh", "Y,Z",	0x7820003e, 0xffe007ff,	RD_S|RD_T|FP_D,		WR_MACC,	MX	},
{"wait",    "",         0x42000020, 0xffffffff, TRAP,   		0,		I3|I32	},
{"wait",    "J",        0x42000020, 0xfe00003f, TRAP,   		0,		I32|N55	},
{"waiti",   "",		0x42000020, 0xffffffff,	TRAP,			0,		L1	},
{"wrpgpr",  "d,w",	0x41c00000, 0xffe007ff, RD_t,			0,		I33	},
{"wsbh",    "d,w",	0x7c0000a0, 0xffe007ff,	WR_d|RD_t,		0,		I33	},
{"xor",     "d,v,t",	0x00000026, 0xfc0007ff,	WR_d|RD_s|RD_t,		0,		I1	},
{"xor",     "t,r,I",	0,    (int) M_XOR_I,	INSN_MACRO,		0,		I1	},
{"xor.ob",  "X,Y,Q",	0x7800000d, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX|SB1	},
{"xor.ob",  "D,S,T",	0x4ac0000d, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"xor.ob",  "D,S,T[e]",	0x4800000d, 0xfe20003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"xor.ob",  "D,S,k",	0x4bc0000d, 0xffe0003f,	WR_D|RD_S|RD_T,		0,		N54	},
{"xor.qh",  "X,Y,Q",	0x7820000d, 0xfc20003f,	WR_D|RD_S|RD_T|FP_D,	0,		MX	},
{"xori",    "t,r,i",	0x38000000, 0xfc000000,	WR_t|RD_s,		0,		I1	},
{"yield",   "s",	0x7c000009, 0xfc1fffff, TRAP|RD_s,		0,		MT32	},
{"yield",   "d,s",	0x7c000009, 0xfc1f07ff, TRAP|WR_d|RD_s,		0,		MT32	},

/* User Defined Instruction.  */
{"udi0",     "s,t,d,+1",0x70000010, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi0",     "s,t,+2",	0x70000010, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi0",     "s,+3",	0x70000010, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi0",     "+4",	0x70000010, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi1",     "s,t,d,+1",0x70000011, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi1",     "s,t,+2",	0x70000011, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi1",     "s,+3",	0x70000011, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi1",     "+4",	0x70000011, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi2",     "s,t,d,+1",0x70000012, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi2",     "s,t,+2",	0x70000012, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi2",     "s,+3",	0x70000012, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi2",     "+4",	0x70000012, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi3",     "s,t,d,+1",0x70000013, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi3",     "s,t,+2",	0x70000013, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi3",     "s,+3",	0x70000013, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi3",     "+4",	0x70000013, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi4",     "s,t,d,+1",0x70000014, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi4",     "s,t,+2",	0x70000014, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi4",     "s,+3",	0x70000014, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi4",     "+4",	0x70000014, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi5",     "s,t,d,+1",0x70000015, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi5",     "s,t,+2",	0x70000015, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi5",     "s,+3",	0x70000015, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi5",     "+4",	0x70000015, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi6",     "s,t,d,+1",0x70000016, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi6",     "s,t,+2",	0x70000016, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi6",     "s,+3",	0x70000016, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi6",     "+4",	0x70000016, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi7",     "s,t,d,+1",0x70000017, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi7",     "s,t,+2",	0x70000017, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi7",     "s,+3",	0x70000017, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi7",     "+4",	0x70000017, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi8",     "s,t,d,+1",0x70000018, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi8",     "s,t,+2",	0x70000018, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi8",     "s,+3",	0x70000018, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi8",     "+4",	0x70000018, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi9",     "s,t,d,+1",0x70000019, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi9",      "s,t,+2",	0x70000019, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi9",     "s,+3",	0x70000019, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi9",     "+4",	0x70000019, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi10",    "s,t,d,+1",0x7000001a, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi10",    "s,t,+2",	0x7000001a, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi10",    "s,+3",	0x7000001a, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi10",    "+4",	0x7000001a, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi11",    "s,t,d,+1",0x7000001b, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi11",    "s,t,+2",	0x7000001b, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi11",    "s,+3",	0x7000001b, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi11",    "+4",	0x7000001b, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi12",    "s,t,d,+1",0x7000001c, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi12",    "s,t,+2",	0x7000001c, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi12",    "s,+3",	0x7000001c, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi12",    "+4",	0x7000001c, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi13",    "s,t,d,+1",0x7000001d, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi13",    "s,t,+2",	0x7000001d, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi13",    "s,+3",	0x7000001d, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi13",    "+4",	0x7000001d, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi14",    "s,t,d,+1",0x7000001e, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi14",    "s,t,+2",	0x7000001e, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi14",    "s,+3",	0x7000001e, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi14",    "+4",	0x7000001e, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi15",    "s,t,d,+1",0x7000001f, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi15",    "s,t,+2",	0x7000001f, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi15",    "s,+3",	0x7000001f, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},
{"udi15",    "+4",	0x7000001f, 0xfc00003f,	WR_d|RD_s|RD_t,		0,		I33	},

/* Coprocessor 2 move/branch operations overlap with VR5400 .ob format
   instructions so they are here for the latters to take precedence.  */
{"bc2f",    "p",	0x49000000, 0xffff0000,	CBD|RD_CC,		0,		I1	},
{"bc2f",    "N,p",	0x49000000, 0xffe30000,	CBD|RD_CC,		0,		I32	},
{"bc2fl",   "p",	0x49020000, 0xffff0000,	CBL|RD_CC,		0,		I2|T3	},
{"bc2fl",   "N,p",	0x49020000, 0xffe30000,	CBL|RD_CC,		0,		I32	},
{"bc2t",    "p",	0x49010000, 0xffff0000,	CBD|RD_CC,		0,		I1	},
{"bc2t",    "N,p",	0x49010000, 0xffe30000,	CBD|RD_CC,		0,		I32	},
{"bc2tl",   "p",	0x49030000, 0xffff0000,	CBL|RD_CC,		0,		I2|T3	},
{"bc2tl",   "N,p",	0x49030000, 0xffe30000,	CBL|RD_CC,		0,		I32	},
{"cfc2",    "t,G",	0x48400000, 0xffe007ff,	LCD|WR_t|RD_C2,		0,		I1	},
{"ctc2",    "t,G",	0x48c00000, 0xffe007ff,	COD|RD_t|WR_CC,		0,		I1	},
{"dmfc2",   "t,G",	0x48200000, 0xffe007ff,	LCD|WR_t|RD_C2,		0,		I3	},
{"dmfc2",   "t,G,H",	0x48200000, 0xffe007f8,	LCD|WR_t|RD_C2,		0,		I64	},
{"dmtc2",   "t,G",	0x48a00000, 0xffe007ff,	COD|RD_t|WR_C2|WR_CC,	0,		I3	},
{"dmtc2",   "t,G,H",	0x48a00000, 0xffe007f8,	COD|RD_t|WR_C2|WR_CC,	0,		I64	},
{"mfc2",    "t,G",	0x48000000, 0xffe007ff,	LCD|WR_t|RD_C2,		0,		I1	},
{"mfc2",    "t,G,H",	0x48000000, 0xffe007f8,	LCD|WR_t|RD_C2,		0,		I32	},
{"mfhc2",   "t,G",	0x48600000, 0xffe007ff,	LCD|WR_t|RD_C2,		0,		I33	},
{"mfhc2",   "t,G,H",	0x48600000, 0xffe007f8,	LCD|WR_t|RD_C2,		0,		I33	},
{"mfhc2",   "t,i",	0x48600000, 0xffe00000,	LCD|WR_t|RD_C2,		0,		I33	},
{"mtc2",    "t,G",	0x48800000, 0xffe007ff,	COD|RD_t|WR_C2|WR_CC,	0,		I1	},
{"mtc2",    "t,G,H",	0x48800000, 0xffe007f8,	COD|RD_t|WR_C2|WR_CC,	0,		I32	},
{"mthc2",   "t,G",	0x48e00000, 0xffe007ff,	COD|RD_t|WR_C2|WR_CC,	0,		I33	},
{"mthc2",   "t,G,H",	0x48e00000, 0xffe007f8,	COD|RD_t|WR_C2|WR_CC,	0,		I33	},
{"mthc2",   "t,i",	0x48e00000, 0xffe00000,	COD|RD_t|WR_C2|WR_CC,	0,		I33	},

/* Coprocessor 3 move/branch operations overlap with MIPS IV COP1X
   instructions, so they are here for the latters to take precedence.  */
{"bc3f",    "p",	0x4d000000, 0xffff0000,	CBD|RD_CC,		0,		I1	},
{"bc3fl",   "p",	0x4d020000, 0xffff0000,	CBL|RD_CC,		0,		I2|T3	},
{"bc3t",    "p",	0x4d010000, 0xffff0000,	CBD|RD_CC,		0,		I1	},
{"bc3tl",   "p",	0x4d030000, 0xffff0000,	CBL|RD_CC,		0,		I2|T3	},
{"cfc3",    "t,G",	0x4c400000, 0xffe007ff,	LCD|WR_t|RD_C3,		0,		I1	},
{"ctc3",    "t,G",	0x4cc00000, 0xffe007ff,	COD|RD_t|WR_CC,		0,		I1	},
{"dmfc3",   "t,G",	0x4c200000, 0xffe007ff, LCD|WR_t|RD_C3, 	0,		I3	},
{"dmtc3",   "t,G",	0x4ca00000, 0xffe007ff, COD|RD_t|WR_C3|WR_CC,	0,		I3	},
{"mfc3",    "t,G",	0x4c000000, 0xffe007ff,	LCD|WR_t|RD_C3,		0,		I1	},
{"mfc3",    "t,G,H",    0x4c000000, 0xffe007f8, LCD|WR_t|RD_C3, 	0,		I32     },
{"mtc3",    "t,G",	0x4c800000, 0xffe007ff,	COD|RD_t|WR_C3|WR_CC,	0,		I1	},
{"mtc3",    "t,G,H",    0x4c800000, 0xffe007f8, COD|RD_t|WR_C3|WR_CC,   0,		I32     },

/* No hazard protection on coprocessor instructions--they shouldn't
   change the state of the processor and if they do it's up to the
   user to put in nops as necessary.  These are at the end so that the
   disassembler recognizes more specific versions first.  */
{"c0",      "C",	0x42000000, 0xfe000000,	0,			0,		I1	},
{"c1",      "C",	0x46000000, 0xfe000000,	0,			0,		I1	},
{"c2",      "C",	0x4a000000, 0xfe000000,	0,			0,		I1	},
{"c3",      "C",	0x4e000000, 0xfe000000,	0,			0,		I1	},
{"cop0",     "C",	0,    (int) M_COP0,	INSN_MACRO,		0,		I1	},
{"cop1",     "C",	0,    (int) M_COP1,	INSN_MACRO,		0,		I1	},
{"cop2",     "C",	0,    (int) M_COP2,	INSN_MACRO,		0,		I1	},
{"cop3",     "C",	0,    (int) M_COP3,	INSN_MACRO,		0,		I1	},
  /* Conflicts with the 4650's "mul" instruction.  Nobody's using the
     4010 any more, so move this insn out of the way.  If the object
     format gave us more info, we could do this right.  */
{"addciu",  "t,r,j",	0x70000000, 0xfc000000,	WR_t|RD_s,		0,		L1	},
/* MIPS DSP ASE */
{"absq_s.ph", "d,t",	0x7c000252, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"absq_s.pw", "d,t",	0x7c000456, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"absq_s.qh", "d,t",	0x7c000256, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"absq_s.w", "d,t",	0x7c000452, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"addq.ph", "d,s,t",	0x7c000290, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"addq.pw", "d,s,t",	0x7c000494, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"addq.qh", "d,s,t",	0x7c000294, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"addq_s.ph", "d,s,t",	0x7c000390, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"addq_s.pw", "d,s,t",	0x7c000594, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"addq_s.qh", "d,s,t",	0x7c000394, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"addq_s.w", "d,s,t",	0x7c000590, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"addsc",   "d,s,t",	0x7c000410, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"addu.ob", "d,s,t",	0x7c000014, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"addu.qb", "d,s,t",	0x7c000010, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"addu_s.ob", "d,s,t",	0x7c000114, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"addu_s.qb", "d,s,t",	0x7c000110, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"addwc",   "d,s,t",	0x7c000450, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"bitrev",  "d,t",	0x7c0006d2, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"bposge32", "p",	0x041c0000, 0xffff0000, CBD,			0,		D32	},
{"bposge64", "p",	0x041d0000, 0xffff0000, CBD,			0,		D64	},
{"cmp.eq.ph", "s,t",	0x7c000211, 0xfc00ffff, RD_s|RD_t,		0,		D32	},
{"cmp.eq.pw", "s,t",	0x7c000415, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmp.eq.qh", "s,t",	0x7c000215, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmpgu.eq.ob", "d,s,t", 0x7c000115, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"cmpgu.eq.qb", "d,s,t", 0x7c000111, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"cmpgu.le.ob", "d,s,t", 0x7c000195, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"cmpgu.le.qb", "d,s,t", 0x7c000191, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"cmpgu.lt.ob", "d,s,t", 0x7c000155, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"cmpgu.lt.qb", "d,s,t", 0x7c000151, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"cmp.le.ph", "s,t",	0x7c000291, 0xfc00ffff, RD_s|RD_t,		0,		D32	},
{"cmp.le.pw", "s,t",	0x7c000495, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmp.le.qh", "s,t",	0x7c000295, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmp.lt.ph", "s,t",	0x7c000251, 0xfc00ffff, RD_s|RD_t,		0,		D32	},
{"cmp.lt.pw", "s,t",	0x7c000455, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmp.lt.qh", "s,t",	0x7c000255, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmpu.eq.ob", "s,t",	0x7c000015, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmpu.eq.qb", "s,t",	0x7c000011, 0xfc00ffff, RD_s|RD_t,		0,		D32	},
{"cmpu.le.ob", "s,t",	0x7c000095, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmpu.le.qb", "s,t",	0x7c000091, 0xfc00ffff, RD_s|RD_t,		0,		D32	},
{"cmpu.lt.ob", "s,t",	0x7c000055, 0xfc00ffff, RD_s|RD_t,		0,		D64	},
{"cmpu.lt.qb", "s,t",	0x7c000051, 0xfc00ffff, RD_s|RD_t,		0,		D32	},
{"dextpdp", "t,7,6",	0x7c0002bc, 0xfc00e7ff, WR_t|RD_a|DSP_VOLA,	0,		D64	},
{"dextpdpv", "t,7,s",	0x7c0002fc, 0xfc00e7ff, WR_t|RD_a|RD_s|DSP_VOLA, 0,		D64	},
{"dextp",   "t,7,6",	0x7c0000bc, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextpv",  "t,7,s",	0x7c0000fc, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D64	},
{"dextr.l", "t,7,6",	0x7c00043c, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextr_r.l", "t,7,6",	0x7c00053c, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextr_rs.l", "t,7,6",	0x7c0005bc, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextr_rs.w", "t,7,6",	0x7c0001bc, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextr_r.w", "t,7,6",	0x7c00013c, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextr_s.h", "t,7,6",	0x7c0003bc, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dextrv.l", "t,7,s",	0x7c00047c, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D64	},
{"dextrv_r.l", "t,7,s",	0x7c00057c, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D64	},
{"dextrv_rs.l", "t,7,s", 0x7c0005fc, 0xfc00e7ff, WR_t|RD_a|RD_s,	0,		D64	},
{"dextrv_rs.w", "t,7,s", 0x7c0001fc, 0xfc00e7ff, WR_t|RD_a|RD_s,	0,		D64	},
{"dextrv_r.w", "t,7,s",	0x7c00017c, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D64	},
{"dextrv_s.h", "t,7,s",	0x7c0003fc, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D64	},
{"dextrv.w", "t,7,s",	0x7c00007c, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D64	},
{"dextr.w", "t,7,6",	0x7c00003c, 0xfc00e7ff, WR_t|RD_a,		0,		D64	},
{"dinsv",   "t,s",	0x7c00000d, 0xfc00ffff, WR_t|RD_s,		0,		D64	},
{"dmadd",   "7,s,t",	0x7c000674, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dmaddu",  "7,s,t",	0x7c000774, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dmsub",   "7,s,t",	0x7c0006f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dmsubu",  "7,s,t",	0x7c0007f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dmthlip", "s,7",	0x7c0007fc, 0xfc1fe7ff, RD_s|MOD_a|DSP_VOLA,	0,		D64	},
{"dpaq_sa.l.pw", "7,s,t", 0x7c000334, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpaq_sa.l.w", "7,s,t", 0x7c000330, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpaq_s.w.ph", "7,s,t", 0x7c000130, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpaq_s.w.qh", "7,s,t", 0x7c000134, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpau.h.obl", "7,s,t",	0x7c0000f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpau.h.obr", "7,s,t",	0x7c0001f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpau.h.qbl", "7,s,t",	0x7c0000f0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpau.h.qbr", "7,s,t",	0x7c0001f0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpsq_sa.l.pw", "7,s,t", 0x7c000374, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpsq_sa.l.w", "7,s,t", 0x7c000370, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpsq_s.w.ph", "7,s,t", 0x7c000170, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpsq_s.w.qh", "7,s,t", 0x7c000174, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpsu.h.obl", "7,s,t",	0x7c0002f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpsu.h.obr", "7,s,t",	0x7c0003f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"dpsu.h.qbl", "7,s,t",	0x7c0002f0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dpsu.h.qbr", "7,s,t",	0x7c0003f0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"dshilo",  "7,:",	0x7c0006bc, 0xfc07e7ff, MOD_a,			0,		D64	},
{"dshilov", "7,s",	0x7c0006fc, 0xfc1fe7ff, MOD_a|RD_s,		0,		D64	},
{"extpdp",  "t,7,6",	0x7c0002b8, 0xfc00e7ff, WR_t|RD_a|DSP_VOLA,	0,		D32	},
{"extpdpv", "t,7,s",	0x7c0002f8, 0xfc00e7ff, WR_t|RD_a|RD_s|DSP_VOLA, 0,		D32	},
{"extp",    "t,7,6",	0x7c0000b8, 0xfc00e7ff, WR_t|RD_a,		0,		D32	},
{"extpv",   "t,7,s",	0x7c0000f8, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D32	},
{"extr_rs.w", "t,7,6",	0x7c0001b8, 0xfc00e7ff, WR_t|RD_a,		0,		D32	},
{"extr_r.w", "t,7,6",	0x7c000138, 0xfc00e7ff, WR_t|RD_a,		0,		D32	},
{"extr_s.h", "t,7,6",	0x7c0003b8, 0xfc00e7ff, WR_t|RD_a,		0,		D32	},
{"extrv_rs.w", "t,7,s",	0x7c0001f8, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D32	},
{"extrv_r.w", "t,7,s",	0x7c000178, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D32	},
{"extrv_s.h", "t,7,s",	0x7c0003f8, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D32	},
{"extrv.w", "t,7,s",	0x7c000078, 0xfc00e7ff, WR_t|RD_a|RD_s,		0,		D32	},
{"extr.w",  "t,7,6",	0x7c000038, 0xfc00e7ff, WR_t|RD_a,		0,		D32	},
{"insv",    "t,s",	0x7c00000c, 0xfc00ffff, WR_t|RD_s,		0,		D32	},
{"lbux",    "d,t(b)",	0x7c00018a, 0xfc0007ff, LDD|WR_d|RD_t|RD_b,	0,		D32	},
{"ldx",     "d,t(b)",	0x7c00020a, 0xfc0007ff, LDD|WR_d|RD_t|RD_b,	0,		D64	},
{"lhx",     "d,t(b)",	0x7c00010a, 0xfc0007ff, LDD|WR_d|RD_t|RD_b,	0,		D32	},
{"lwx",     "d,t(b)",	0x7c00000a, 0xfc0007ff, LDD|WR_d|RD_t|RD_b,	0,		D32	},
{"maq_sa.w.phl", "7,s,t", 0x7c000430, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"maq_sa.w.phr", "7,s,t", 0x7c0004b0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"maq_sa.w.qhll", "7,s,t", 0x7c000434, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_sa.w.qhlr", "7,s,t", 0x7c000474, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_sa.w.qhrl", "7,s,t", 0x7c0004b4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_sa.w.qhrr", "7,s,t", 0x7c0004f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_s.l.pwl", "7,s,t", 0x7c000734, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_s.l.pwr", "7,s,t", 0x7c0007b4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_s.w.phl", "7,s,t", 0x7c000530, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"maq_s.w.phr", "7,s,t", 0x7c0005b0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"maq_s.w.qhll", "7,s,t", 0x7c000534, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_s.w.qhlr", "7,s,t", 0x7c000574, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_s.w.qhrl", "7,s,t", 0x7c0005b4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"maq_s.w.qhrr", "7,s,t", 0x7c0005f4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"modsub",  "d,s,t",	0x7c000490, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"mthlip",  "s,7",	0x7c0007f8, 0xfc1fe7ff, RD_s|MOD_a|DSP_VOLA,	0,		D32	},
{"muleq_s.pw.qhl", "d,s,t", 0x7c000714, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D64	},
{"muleq_s.pw.qhr", "d,s,t", 0x7c000754, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D64	},
{"muleq_s.w.phl", "d,s,t", 0x7c000710, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D32	},
{"muleq_s.w.phr", "d,s,t", 0x7c000750, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D32	},
{"muleu_s.ph.qbl", "d,s,t", 0x7c000190, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D32	},
{"muleu_s.ph.qbr", "d,s,t", 0x7c0001d0, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D32	},
{"muleu_s.qh.obl", "d,s,t", 0x7c000194, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D64	},
{"muleu_s.qh.obr", "d,s,t", 0x7c0001d4, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,		D64	},
{"mulq_rs.ph", "d,s,t",	0x7c0007d0, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO,	0,		D32	},
{"mulq_rs.qh", "d,s,t",	0x7c0007d4, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO,	0,		D64	},
{"mulsaq_s.l.pw", "7,s,t", 0x7c0003b4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"mulsaq_s.w.ph", "7,s,t", 0x7c0001b0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D32	},
{"mulsaq_s.w.qh", "7,s,t", 0x7c0001b4, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,		D64	},
{"packrl.ph", "d,s,t",	0x7c000391, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"packrl.pw", "d,s,t",	0x7c000395, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"pick.ob", "d,s,t",	0x7c0000d5, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"pick.ph", "d,s,t",	0x7c0002d1, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"pick.pw", "d,s,t",	0x7c0004d5, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"pick.qb", "d,s,t",	0x7c0000d1, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"pick.qh", "d,s,t",	0x7c0002d5, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"preceq.pw.qhla", "d,t", 0x7c000396, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceq.pw.qhl", "d,t", 0x7c000316, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceq.pw.qhra", "d,t", 0x7c0003d6, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceq.pw.qhr", "d,t", 0x7c000356, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceq.s.l.pwl", "d,t", 0x7c000516, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceq.s.l.pwr", "d,t", 0x7c000556, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"precequ.ph.qbla", "d,t", 0x7c000192, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"precequ.ph.qbl", "d,t", 0x7c000112, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"precequ.ph.qbra", "d,t", 0x7c0001d2, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"precequ.ph.qbr", "d,t", 0x7c000152, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"precequ.pw.qhla", "d,t", 0x7c000196, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"precequ.pw.qhl", "d,t", 0x7c000116, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"precequ.pw.qhra", "d,t", 0x7c0001d6, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"precequ.pw.qhr", "d,t", 0x7c000156, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceq.w.phl", "d,t",	0x7c000312, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"preceq.w.phr", "d,t",	0x7c000352, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"preceu.ph.qbla", "d,t", 0x7c000792, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"preceu.ph.qbl", "d,t", 0x7c000712, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"preceu.ph.qbra", "d,t", 0x7c0007d2, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"preceu.ph.qbr", "d,t", 0x7c000752, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"preceu.qh.obla", "d,t", 0x7c000796, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceu.qh.obl", "d,t", 0x7c000716, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceu.qh.obra", "d,t", 0x7c0007d6, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"preceu.qh.obr", "d,t", 0x7c000756, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"precrq.ob.qh", "d,s,t", 0x7c000315, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"precrq.ph.w", "d,s,t", 0x7c000511, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"precrq.pw.l", "d,s,t", 0x7c000715, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"precrq.qb.ph", "d,s,t", 0x7c000311, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"precrq.qh.pw", "d,s,t", 0x7c000515, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"precrq_rs.ph.w", "d,s,t", 0x7c000551, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"precrq_rs.qh.pw", "d,s,t", 0x7c000555, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"precrqu_s.ob.qh", "d,s,t", 0x7c0003d5, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D64	},
{"precrqu_s.qb.ph", "d,s,t", 0x7c0003d1, 0xfc0007ff, WR_d|RD_s|RD_t,	0,		D32	},
{"raddu.l.ob", "d,s",	0x7c000514, 0xfc1f07ff, WR_d|RD_s,		0,		D64	},
{"raddu.w.qb", "d,s",	0x7c000510, 0xfc1f07ff, WR_d|RD_s,		0,		D32	},
{"rddsp",   "d",	0x7fff04b8, 0xffff07ff, WR_d,			0,		D32	},
{"rddsp",   "d,'",	0x7c0004b8, 0xffc007ff, WR_d,			0,		D32	},
{"repl.ob", "d,5",	0x7c000096, 0xff0007ff, WR_d,			0,		D64	},
{"repl.ph", "d,@",	0x7c000292, 0xfc0007ff, WR_d,			0,		D32	},
{"repl.pw", "d,@",	0x7c000496, 0xfc0007ff, WR_d,			0,		D64	},
{"repl.qb", "d,5",	0x7c000092, 0xff0007ff, WR_d,			0,		D32	},
{"repl.qh", "d,@",	0x7c000296, 0xfc0007ff, WR_d,			0,		D64	},
{"replv.ob", "d,t",	0x7c0000d6, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"replv.ph", "d,t",	0x7c0002d2, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"replv.pw", "d,t",	0x7c0004d6, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"replv.qb", "d,t",	0x7c0000d2, 0xffe007ff, WR_d|RD_t,		0,		D32	},
{"replv.qh", "d,t",	0x7c0002d6, 0xffe007ff, WR_d|RD_t,		0,		D64	},
{"shilo",   "7,0",	0x7c0006b8, 0xfc0fe7ff, MOD_a,			0,		D32	},
{"shilov",  "7,s",	0x7c0006f8, 0xfc1fe7ff, MOD_a|RD_s,		0,		D32	},
{"shll.ob", "d,t,3",	0x7c000017, 0xff0007ff, WR_d|RD_t,		0,		D64	},
{"shll.ph", "d,t,4",	0x7c000213, 0xfe0007ff, WR_d|RD_t,		0,		D32	},
{"shll.pw", "d,t,6",	0x7c000417, 0xfc0007ff, WR_d|RD_t,		0,		D64	},
{"shll.qb", "d,t,3",	0x7c000013, 0xff0007ff, WR_d|RD_t,		0,		D32	},
{"shll.qh", "d,t,4",	0x7c000217, 0xfe0007ff, WR_d|RD_t,		0,		D64	},
{"shll_s.ph", "d,t,4",	0x7c000313, 0xfe0007ff, WR_d|RD_t,		0,		D32	},
{"shll_s.pw", "d,t,6",	0x7c000517, 0xfc0007ff, WR_d|RD_t,		0,		D64	},
{"shll_s.qh", "d,t,4",	0x7c000317, 0xfe0007ff, WR_d|RD_t,		0,		D64	},
{"shll_s.w", "d,t,6",	0x7c000513, 0xfc0007ff, WR_d|RD_t,		0,		D32	},
{"shllv.ob", "d,t,s",	0x7c000097, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shllv.ph", "d,t,s",	0x7c000293, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shllv.pw", "d,t,s",	0x7c000497, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shllv.qb", "d,t,s",	0x7c000093, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shllv.qh", "d,t,s",	0x7c000297, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shllv_s.ph", "d,t,s",	0x7c000393, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shllv_s.pw", "d,t,s",	0x7c000597, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shllv_s.qh", "d,t,s",	0x7c000397, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shllv_s.w", "d,t,s",	0x7c000593, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shra.ph", "d,t,4",	0x7c000253, 0xfe0007ff, WR_d|RD_t,		0,		D32	},
{"shra.pw", "d,t,6",	0x7c000457, 0xfc0007ff, WR_d|RD_t,		0,		D64	},
{"shra.qh", "d,t,4",	0x7c000257, 0xfe0007ff, WR_d|RD_t,		0,		D64	},
{"shra_r.ph", "d,t,4",	0x7c000353, 0xfe0007ff, WR_d|RD_t,		0,		D32	},
{"shra_r.pw", "d,t,6",	0x7c000557, 0xfc0007ff, WR_d|RD_t,		0,		D64	},
{"shra_r.qh", "d,t,4",	0x7c000357, 0xfe0007ff, WR_d|RD_t,		0,		D64	},
{"shra_r.w", "d,t,6",	0x7c000553, 0xfc0007ff, WR_d|RD_t,		0,		D32	},
{"shrav.ph", "d,t,s",	0x7c0002d3, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shrav.pw", "d,t,s",	0x7c0004d7, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shrav.qh", "d,t,s",	0x7c0002d7, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shrav_r.ph", "d,t,s",	0x7c0003d3, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shrav_r.pw", "d,t,s",	0x7c0005d7, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shrav_r.qh", "d,t,s",	0x7c0003d7, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shrav_r.w", "d,t,s",	0x7c0005d3, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"shrl.ob", "d,t,3",	0x7c000057, 0xff0007ff, WR_d|RD_t,		0,		D64	},
{"shrl.qb", "d,t,3",	0x7c000053, 0xff0007ff, WR_d|RD_t,		0,		D32	},
{"shrlv.ob", "d,t,s",	0x7c0000d7, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"shrlv.qb", "d,t,s",	0x7c0000d3, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"subq.ph", "d,s,t",	0x7c0002d0, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"subq.pw", "d,s,t",	0x7c0004d4, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"subq.qh", "d,s,t",	0x7c0002d4, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"subq_s.ph", "d,s,t",	0x7c0003d0, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"subq_s.pw", "d,s,t",	0x7c0005d4, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"subq_s.qh", "d,s,t",	0x7c0003d4, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"subq_s.w", "d,s,t",	0x7c0005d0, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"subu.ob", "d,s,t",	0x7c000054, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"subu.qb", "d,s,t",	0x7c000050, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"subu_s.ob", "d,s,t",	0x7c000154, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D64	},
{"subu_s.qb", "d,s,t",	0x7c000150, 0xfc0007ff, WR_d|RD_s|RD_t,		0,		D32	},
{"wrdsp",   "s",	0x7c1ffcf8, 0xfc1fffff, RD_s|DSP_VOLA,		0,		D32	},
{"wrdsp",   "s,8",	0x7c0004f8, 0xfc1e07ff, RD_s|DSP_VOLA,		0,		D32	},
/* MIPS DSP ASE Rev2 */
{"absq_s.qb", "d,t",	0x7c000052, 0xffe007ff, WR_d|RD_t,              0,              D33	},
{"addu.ph", "d,s,t",	0x7c000210, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"addu_s.ph", "d,s,t",	0x7c000310, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"adduh.qb", "d,s,t",	0x7c000018, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"adduh_r.qb", "d,s,t",	0x7c000098, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"append",  "t,s,h",	0x7c000031, 0xfc0007ff, WR_t|RD_t|RD_s,         0,              D33	},
{"balign",  "t,s,I",	0,    (int) M_BALIGN,	INSN_MACRO,             0,              D33	},
{"balign",  "t,s,2",	0x7c000431, 0xfc00e7ff, WR_t|RD_t|RD_s,         0,              D33	},
{"cmpgdu.eq.qb", "d,s,t", 0x7c000611, 0xfc0007ff, WR_d|RD_s|RD_t,       0,              D33	},
{"cmpgdu.lt.qb", "d,s,t", 0x7c000651, 0xfc0007ff, WR_d|RD_s|RD_t,       0,              D33	},
{"cmpgdu.le.qb", "d,s,t", 0x7c000691, 0xfc0007ff, WR_d|RD_s|RD_t,       0,              D33	},
{"dpa.w.ph", "7,s,t",	0x7c000030, 0xfc00e7ff, MOD_a|RD_s|RD_t,        0,              D33	},
{"dps.w.ph", "7,s,t",	0x7c000070, 0xfc00e7ff, MOD_a|RD_s|RD_t,        0,              D33	},
{"mul.ph",  "d,s,t",	0x7c000318, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,              D33	},
{"mul_s.ph", "d,s,t",	0x7c000398, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,              D33	},
{"mulq_rs.w", "d,s,t",	0x7c0005d8, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,              D33	},
{"mulq_s.ph", "d,s,t",	0x7c000790, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,              D33	},
{"mulq_s.w", "d,s,t",	0x7c000598, 0xfc0007ff, WR_d|RD_s|RD_t|WR_HILO, 0,              D33	},
{"mulsa.w.ph", "7,s,t",	0x7c0000b0, 0xfc00e7ff, MOD_a|RD_s|RD_t,        0,              D33	},
{"precr.qb.ph", "d,s,t", 0x7c000351, 0xfc0007ff, WR_d|RD_s|RD_t,        0,              D33	},
{"precr_sra.ph.w", "t,s,h", 0x7c000791, 0xfc0007ff, WR_t|RD_t|RD_s,     0,              D33	},
{"precr_sra_r.ph.w", "t,s,h", 0x7c0007d1, 0xfc0007ff, WR_t|RD_t|RD_s,   0,              D33	},
{"prepend", "t,s,h",	0x7c000071, 0xfc0007ff, WR_t|RD_t|RD_s,         0,              D33	},
{"shra.qb", "d,t,3",	0x7c000113, 0xff0007ff, WR_d|RD_t,              0,              D33	},
{"shra_r.qb", "d,t,3",	0x7c000153, 0xff0007ff, WR_d|RD_t,              0,              D33	},
{"shrav.qb", "d,t,s",	0x7c000193, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"shrav_r.qb", "d,t,s",	0x7c0001d3, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"shrl.ph", "d,t,4",	0x7c000653, 0xfe0007ff, WR_d|RD_t,              0,              D33	},
{"shrlv.ph", "d,t,s",	0x7c0006d3, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"subu.ph", "d,s,t",	0x7c000250, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"subu_s.ph", "d,s,t",	0x7c000350, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"subuh.qb", "d,s,t",	0x7c000058, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"subuh_r.qb", "d,s,t",	0x7c0000d8, 0xfc0007ff, WR_d|RD_s|RD_t,         0,              D33	},
{"addqh.ph", "d,s,t",	0x7c000218, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"addqh_r.ph", "d,s,t",	0x7c000298, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"addqh.w", "d,s,t",	0x7c000418, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"addqh_r.w", "d,s,t",	0x7c000498, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"subqh.ph", "d,s,t",	0x7c000258, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"subqh_r.ph", "d,s,t",	0x7c0002d8, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"subqh.w", "d,s,t",	0x7c000458, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"subqh_r.w", "d,s,t",	0x7c0004d8, 0xfc0007ff, WR_d|RD_s|RD_t,		0,              D33	},
{"dpax.w.ph", "7,s,t",	0x7c000230, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,              D33	},
{"dpsx.w.ph", "7,s,t",	0x7c000270, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,              D33	},
{"dpaqx_s.w.ph", "7,s,t", 0x7c000630, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,              D33	},
{"dpaqx_sa.w.ph", "7,s,t", 0x7c0006b0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,              D33	},
{"dpsqx_s.w.ph", "7,s,t", 0x7c000670, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,              D33	},
{"dpsqx_sa.w.ph", "7,s,t", 0x7c0006f0, 0xfc00e7ff, MOD_a|RD_s|RD_t,	0,              D33	},
/* Move bc0* after mftr and mttr to avoid opcode collision.  */
{"bc0f",    "p",	0x41000000, 0xffff0000,	CBD|RD_CC,		0,		I1	},
{"bc0fl",   "p",	0x41020000, 0xffff0000,	CBL|RD_CC,		0,		I2|T3	},
{"bc0t",    "p",	0x41010000, 0xffff0000,	CBD|RD_CC,		0,		I1	},
{"bc0tl",   "p",	0x41030000, 0xffff0000,	CBL|RD_CC,		0,		I2|T3	},
/* ST Microelectronics Loongson-2E and -2F.  */
{"mult.g",	"d,s,t",	0x7c000018,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"mult.g",	"d,s,t",	0x70000010,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"multu.g",	"d,s,t",	0x7c000019,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"multu.g",	"d,s,t",	0x70000012,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"dmult.g",	"d,s,t",	0x7c00001c,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"dmult.g",	"d,s,t",	0x70000011,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"dmultu.g",	"d,s,t",	0x7c00001d,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"dmultu.g",	"d,s,t",	0x70000013,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"div.g",	"d,s,t",	0x7c00001a,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"div.g",	"d,s,t",	0x70000014,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"divu.g",	"d,s,t",	0x7c00001b,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"divu.g",	"d,s,t",	0x70000016,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"ddiv.g",	"d,s,t",	0x7c00001e,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"ddiv.g",	"d,s,t",	0x70000015,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"ddivu.g",	"d,s,t",	0x7c00001f,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"ddivu.g",	"d,s,t",	0x70000017,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"mod.g",	"d,s,t",	0x7c000022,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"mod.g",	"d,s,t",	0x7000001c,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"modu.g",	"d,s,t",	0x7c000023,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"modu.g",	"d,s,t",	0x7000001e,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"dmod.g",	"d,s,t",	0x7c000026,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"dmod.g",	"d,s,t",	0x7000001d,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
{"dmodu.g",	"d,s,t",	0x7c000027,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2E	},
{"dmodu.g",	"d,s,t",	0x7000001f,	0xfc0007ff,	RD_s|RD_t|WR_d,	0,	IL2F	},
};

#define MIPS_NUM_OPCODES \
	((sizeof mips_builtin_opcodes) / (sizeof (mips_builtin_opcodes[0])))
const int bfd_mips_num_builtin_opcodes = MIPS_NUM_OPCODES;

/* const removed from the following to allow for dynamic extensions to the
 * built-in instruction set. */
struct mips_opcode *mips_opcodes =
  (struct mips_opcode *) mips_builtin_opcodes;
int bfd_mips_num_opcodes = MIPS_NUM_OPCODES;
#undef MIPS_NUM_OPCODES

/* Mips instructions are at maximum this many bytes long.  */
#define INSNLEN 4


/* FIXME: These should be shared with gdb somehow.  */

struct mips_cp0sel_name
{
  unsigned int cp0reg;
  unsigned int sel;
  const char * const name;
};

/* The mips16 registers.  */
static const unsigned int mips16_to_32_reg_map[] =
{
  16, 17, 2, 3, 4, 5, 6, 7
};

#define mips16_reg_names(rn)	mips_gpr_names[mips16_to_32_reg_map[rn]]


static const char * const mips_gpr_names_numeric[32] =
{
  "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

static const char * const mips_gpr_names_oldabi[32] =
{
  "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
  "t0",   "t1",   "t2",   "t3",   "t4",   "t5",   "t6",   "t7",
  "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
  "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"
};

static const char * const mips_gpr_names_newabi[32] =
{
  "zero", "at",   "v0",   "v1",   "a0",   "a1",   "a2",   "a3",
  "a4",   "a5",   "a6",   "a7",   "t0",   "t1",   "t2",   "t3",
  "s0",   "s1",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
  "t8",   "t9",   "k0",   "k1",   "gp",   "sp",   "s8",   "ra"
};

static const char * const mips_fpr_names_numeric[32] =
{
  "$f0",  "$f1",  "$f2",  "$f3",  "$f4",  "$f5",  "$f6",  "$f7",
  "$f8",  "$f9",  "$f10", "$f11", "$f12", "$f13", "$f14", "$f15",
  "$f16", "$f17", "$f18", "$f19", "$f20", "$f21", "$f22", "$f23",
  "$f24", "$f25", "$f26", "$f27", "$f28", "$f29", "$f30", "$f31"
};

static const char * const mips_fpr_names_32[32] =
{
  "fv0",  "fv0f", "fv1",  "fv1f", "ft0",  "ft0f", "ft1",  "ft1f",
  "ft2",  "ft2f", "ft3",  "ft3f", "fa0",  "fa0f", "fa1",  "fa1f",
  "ft4",  "ft4f", "ft5",  "ft5f", "fs0",  "fs0f", "fs1",  "fs1f",
  "fs2",  "fs2f", "fs3",  "fs3f", "fs4",  "fs4f", "fs5",  "fs5f"
};

static const char * const mips_fpr_names_n32[32] =
{
  "fv0",  "ft14", "fv1",  "ft15", "ft0",  "ft1",  "ft2",  "ft3",
  "ft4",  "ft5",  "ft6",  "ft7",  "fa0",  "fa1",  "fa2",  "fa3",
  "fa4",  "fa5",  "fa6",  "fa7",  "fs0",  "ft8",  "fs1",  "ft9",
  "fs2",  "ft10", "fs3",  "ft11", "fs4",  "ft12", "fs5",  "ft13"
};

static const char * const mips_fpr_names_64[32] =
{
  "fv0",  "ft12", "fv1",  "ft13", "ft0",  "ft1",  "ft2",  "ft3",
  "ft4",  "ft5",  "ft6",  "ft7",  "fa0",  "fa1",  "fa2",  "fa3",
  "fa4",  "fa5",  "fa6",  "fa7",  "ft8",  "ft9",  "ft10", "ft11",
  "fs0",  "fs1",  "fs2",  "fs3",  "fs4",  "fs5",  "fs6",  "fs7"
};

static const char * const mips_cp0_names_numeric[32] =
{
  "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

static const char * const mips_cp0_names_mips3264[32] =
{
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "$7",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "c0_lladdr",    "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "$22",          "c0_debug",
  "c0_depc",      "c0_perfcnt",   "c0_errctl",    "c0_cacheerr",
  "c0_taglo",     "c0_taghi",     "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_mips3264[] =
{
  {  4, 1, "c0_contextconfig"	},
  {  0, 1, "c0_mvpcontrol"	},
  {  0, 2, "c0_mvpconf0"	},
  {  0, 3, "c0_mvpconf1"	},
  {  1, 1, "c0_vpecontrol"	},
  {  1, 2, "c0_vpeconf0"	},
  {  1, 3, "c0_vpeconf1"	},
  {  1, 4, "c0_yqmask"		},
  {  1, 5, "c0_vpeschedule"	},
  {  1, 6, "c0_vpeschefback"	},
  {  2, 1, "c0_tcstatus"	},
  {  2, 2, "c0_tcbind"		},
  {  2, 3, "c0_tcrestart"	},
  {  2, 4, "c0_tchalt"		},
  {  2, 5, "c0_tccontext"	},
  {  2, 6, "c0_tcschedule"	},
  {  2, 7, "c0_tcschefback"	},
  {  5, 1, "c0_pagegrain"	},
  {  6, 1, "c0_srsconf0"	},
  {  6, 2, "c0_srsconf1"	},
  {  6, 3, "c0_srsconf2"	},
  {  6, 4, "c0_srsconf3"	},
  {  6, 5, "c0_srsconf4"	},
  { 12, 1, "c0_intctl"		},
  { 12, 2, "c0_srsctl"		},
  { 12, 3, "c0_srsmap"		},
  { 15, 1, "c0_ebase"		},
  { 16, 1, "c0_config1"		},
  { 16, 2, "c0_config2"		},
  { 16, 3, "c0_config3"		},
  { 18, 1, "c0_watchlo,1"	},
  { 18, 2, "c0_watchlo,2"	},
  { 18, 3, "c0_watchlo,3"	},
  { 18, 4, "c0_watchlo,4"	},
  { 18, 5, "c0_watchlo,5"	},
  { 18, 6, "c0_watchlo,6"	},
  { 18, 7, "c0_watchlo,7"	},
  { 19, 1, "c0_watchhi,1"	},
  { 19, 2, "c0_watchhi,2"	},
  { 19, 3, "c0_watchhi,3"	},
  { 19, 4, "c0_watchhi,4"	},
  { 19, 5, "c0_watchhi,5"	},
  { 19, 6, "c0_watchhi,6"	},
  { 19, 7, "c0_watchhi,7"	},
  { 23, 1, "c0_tracecontrol"	},
  { 23, 2, "c0_tracecontrol2"	},
  { 23, 3, "c0_usertracedata"	},
  { 23, 4, "c0_tracebpc"	},
  { 25, 1, "c0_perfcnt,1"	},
  { 25, 2, "c0_perfcnt,2"	},
  { 25, 3, "c0_perfcnt,3"	},
  { 25, 4, "c0_perfcnt,4"	},
  { 25, 5, "c0_perfcnt,5"	},
  { 25, 6, "c0_perfcnt,6"	},
  { 25, 7, "c0_perfcnt,7"	},
  { 27, 1, "c0_cacheerr,1"	},
  { 27, 2, "c0_cacheerr,2"	},
  { 27, 3, "c0_cacheerr,3"	},
  { 28, 1, "c0_datalo"		},
  { 28, 2, "c0_taglo1"		},
  { 28, 3, "c0_datalo1"		},
  { 28, 4, "c0_taglo2"		},
  { 28, 5, "c0_datalo2"		},
  { 28, 6, "c0_taglo3"		},
  { 28, 7, "c0_datalo3"		},
  { 29, 1, "c0_datahi"		},
  { 29, 2, "c0_taghi1"		},
  { 29, 3, "c0_datahi1"		},
  { 29, 4, "c0_taghi2"		},
  { 29, 5, "c0_datahi2"		},
  { 29, 6, "c0_taghi3"		},
  { 29, 7, "c0_datahi3"		},
};

static const char * const mips_cp0_names_mips3264r2[32] =
{
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "c0_hwrena",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "c0_lladdr",    "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "$22",          "c0_debug",
  "c0_depc",      "c0_perfcnt",   "c0_errctl",    "c0_cacheerr",
  "c0_taglo",     "c0_taghi",     "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_mips3264r2[] =
{
  {  4, 1, "c0_contextconfig"	},
  {  5, 1, "c0_pagegrain"	},
  { 12, 1, "c0_intctl"		},
  { 12, 2, "c0_srsctl"		},
  { 12, 3, "c0_srsmap"		},
  { 15, 1, "c0_ebase"		},
  { 16, 1, "c0_config1"		},
  { 16, 2, "c0_config2"		},
  { 16, 3, "c0_config3"		},
  { 18, 1, "c0_watchlo,1"	},
  { 18, 2, "c0_watchlo,2"	},
  { 18, 3, "c0_watchlo,3"	},
  { 18, 4, "c0_watchlo,4"	},
  { 18, 5, "c0_watchlo,5"	},
  { 18, 6, "c0_watchlo,6"	},
  { 18, 7, "c0_watchlo,7"	},
  { 19, 1, "c0_watchhi,1"	},
  { 19, 2, "c0_watchhi,2"	},
  { 19, 3, "c0_watchhi,3"	},
  { 19, 4, "c0_watchhi,4"	},
  { 19, 5, "c0_watchhi,5"	},
  { 19, 6, "c0_watchhi,6"	},
  { 19, 7, "c0_watchhi,7"	},
  { 23, 1, "c0_tracecontrol"	},
  { 23, 2, "c0_tracecontrol2"	},
  { 23, 3, "c0_usertracedata"	},
  { 23, 4, "c0_tracebpc"	},
  { 25, 1, "c0_perfcnt,1"	},
  { 25, 2, "c0_perfcnt,2"	},
  { 25, 3, "c0_perfcnt,3"	},
  { 25, 4, "c0_perfcnt,4"	},
  { 25, 5, "c0_perfcnt,5"	},
  { 25, 6, "c0_perfcnt,6"	},
  { 25, 7, "c0_perfcnt,7"	},
  { 27, 1, "c0_cacheerr,1"	},
  { 27, 2, "c0_cacheerr,2"	},
  { 27, 3, "c0_cacheerr,3"	},
  { 28, 1, "c0_datalo"		},
  { 28, 2, "c0_taglo1"		},
  { 28, 3, "c0_datalo1"		},
  { 28, 4, "c0_taglo2"		},
  { 28, 5, "c0_datalo2"		},
  { 28, 6, "c0_taglo3"		},
  { 28, 7, "c0_datalo3"		},
  { 29, 1, "c0_datahi"		},
  { 29, 2, "c0_taghi1"		},
  { 29, 3, "c0_datahi1"		},
  { 29, 4, "c0_taghi2"		},
  { 29, 5, "c0_datahi2"		},
  { 29, 6, "c0_taghi3"		},
  { 29, 7, "c0_datahi3"		},
};

/* SB-1: MIPS64 (mips_cp0_names_mips3264) with minor mods.  */
static const char * const mips_cp0_names_sb1[32] =
{
  "c0_index",     "c0_random",    "c0_entrylo0",  "c0_entrylo1",
  "c0_context",   "c0_pagemask",  "c0_wired",     "$7",
  "c0_badvaddr",  "c0_count",     "c0_entryhi",   "c0_compare",
  "c0_status",    "c0_cause",     "c0_epc",       "c0_prid",
  "c0_config",    "c0_lladdr",    "c0_watchlo",   "c0_watchhi",
  "c0_xcontext",  "$21",          "$22",          "c0_debug",
  "c0_depc",      "c0_perfcnt",   "c0_errctl",    "c0_cacheerr_i",
  "c0_taglo_i",   "c0_taghi_i",   "c0_errorepc",  "c0_desave",
};

static const struct mips_cp0sel_name mips_cp0sel_names_sb1[] =
{
  { 16, 1, "c0_config1"		},
  { 18, 1, "c0_watchlo,1"	},
  { 19, 1, "c0_watchhi,1"	},
  { 22, 0, "c0_perftrace"	},
  { 23, 3, "c0_edebug"		},
  { 25, 1, "c0_perfcnt,1"	},
  { 25, 2, "c0_perfcnt,2"	},
  { 25, 3, "c0_perfcnt,3"	},
  { 25, 4, "c0_perfcnt,4"	},
  { 25, 5, "c0_perfcnt,5"	},
  { 25, 6, "c0_perfcnt,6"	},
  { 25, 7, "c0_perfcnt,7"	},
  { 26, 1, "c0_buserr_pa"	},
  { 27, 1, "c0_cacheerr_d"	},
  { 27, 3, "c0_cacheerr_d_pa"	},
  { 28, 1, "c0_datalo_i"	},
  { 28, 2, "c0_taglo_d"		},
  { 28, 3, "c0_datalo_d"	},
  { 29, 1, "c0_datahi_i"	},
  { 29, 2, "c0_taghi_d"		},
  { 29, 3, "c0_datahi_d"	},
};

static const char * const mips_hwr_names_numeric[32] =
{
  "$0",   "$1",   "$2",   "$3",   "$4",   "$5",   "$6",   "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

static const char * const mips_hwr_names_mips3264r2[32] =
{
  "hwr_cpunum",   "hwr_synci_step", "hwr_cc",     "hwr_ccres",
  "$4",          "$5",            "$6",           "$7",
  "$8",   "$9",   "$10",  "$11",  "$12",  "$13",  "$14",  "$15",
  "$16",  "$17",  "$18",  "$19",  "$20",  "$21",  "$22",  "$23",
  "$24",  "$25",  "$26",  "$27",  "$28",  "$29",  "$30",  "$31"
};

struct mips_abi_choice
{
  const char *name;
  const char * const *gpr_names;
  const char * const *fpr_names;
};

static struct mips_abi_choice mips_abi_choices[] =
{
  { "numeric", mips_gpr_names_numeric, mips_fpr_names_numeric },
  { "32", mips_gpr_names_oldabi, mips_fpr_names_32 },
  { "n32", mips_gpr_names_newabi, mips_fpr_names_n32 },
  { "64", mips_gpr_names_newabi, mips_fpr_names_64 },
};

struct mips_arch_choice
{
  const char *name;
  int bfd_mach_valid;
  unsigned long bfd_mach;
  int processor;
  int isa;
  const char * const *cp0_names;
  const struct mips_cp0sel_name *cp0sel_names;
  unsigned int cp0sel_names_len;
  const char * const *hwr_names;
};

#define bfd_mach_mips3000              3000
#define bfd_mach_mips3900              3900
#define bfd_mach_mips4000              4000
#define bfd_mach_mips4010              4010
#define bfd_mach_mips4100              4100
#define bfd_mach_mips4111              4111
#define bfd_mach_mips4120              4120
#define bfd_mach_mips4300              4300
#define bfd_mach_mips4400              4400
#define bfd_mach_mips4600              4600
#define bfd_mach_mips4650              4650
#define bfd_mach_mips5000              5000
#define bfd_mach_mips5400              5400
#define bfd_mach_mips5500              5500
#define bfd_mach_mips6000              6000
#define bfd_mach_mips7000              7000
#define bfd_mach_mips8000              8000
#define bfd_mach_mips9000              9000
#define bfd_mach_mips10000             10000
#define bfd_mach_mips12000             12000
#define bfd_mach_mips16                16
#define bfd_mach_mips5                 5
#define bfd_mach_mips_sb1              12310201 /* octal 'SB', 01 */
#define bfd_mach_mipsisa32             32
#define bfd_mach_mipsisa32r2           33
#define bfd_mach_mipsisa64             64
#define bfd_mach_mipsisa64r2           65

static const struct mips_arch_choice mips_arch_choices[] =
{
  { "numeric",	0, 0, 0, 0,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },

  { "r3000",	1, bfd_mach_mips3000, CPU_R3000, ISA_MIPS1,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r3900",	1, bfd_mach_mips3900, CPU_R3900, ISA_MIPS1,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4000",	1, bfd_mach_mips4000, CPU_R4000, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4010",	1, bfd_mach_mips4010, CPU_R4010, ISA_MIPS2,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr4100",	1, bfd_mach_mips4100, CPU_VR4100, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr4111",	1, bfd_mach_mips4111, CPU_R4111, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr4120",	1, bfd_mach_mips4120, CPU_VR4120, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4300",	1, bfd_mach_mips4300, CPU_R4300, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4400",	1, bfd_mach_mips4400, CPU_R4400, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4600",	1, bfd_mach_mips4600, CPU_R4600, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r4650",	1, bfd_mach_mips4650, CPU_R4650, ISA_MIPS3,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r5000",	1, bfd_mach_mips5000, CPU_R5000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr5400",	1, bfd_mach_mips5400, CPU_VR5400, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "vr5500",	1, bfd_mach_mips5500, CPU_VR5500, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r6000",	1, bfd_mach_mips6000, CPU_R6000, ISA_MIPS2,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "rm7000",	1, bfd_mach_mips7000, CPU_RM7000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "rm9000",	1, bfd_mach_mips7000, CPU_RM7000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r8000",	1, bfd_mach_mips8000, CPU_R8000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r10000",	1, bfd_mach_mips10000, CPU_R10000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "r12000",	1, bfd_mach_mips12000, CPU_R12000, ISA_MIPS4,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
  { "mips5",	1, bfd_mach_mips5, CPU_MIPS5, ISA_MIPS5,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },

  /* For stock MIPS32, disassemble all applicable MIPS-specified ASEs.
     Note that MIPS-3D and MDMX are not applicable to MIPS32.  (See
     _MIPS32 Architecture For Programmers Volume I: Introduction to the
     MIPS32 Architecture_ (MIPS Document Number MD00082, Revision 0.95),
     page 1.  */
  { "mips32",	1, bfd_mach_mipsisa32, CPU_MIPS32,
    ISA_MIPS32 | INSN_MIPS16 | INSN_SMARTMIPS,
    mips_cp0_names_mips3264,
    mips_cp0sel_names_mips3264, ARRAY_SIZE (mips_cp0sel_names_mips3264),
    mips_hwr_names_numeric },

  { "mips32r2",	1, bfd_mach_mipsisa32r2, CPU_MIPS32R2,
    (ISA_MIPS32R2 | INSN_MIPS16 | INSN_SMARTMIPS | INSN_DSP | INSN_DSPR2
     | INSN_MIPS3D | INSN_MT),
    mips_cp0_names_mips3264r2,
    mips_cp0sel_names_mips3264r2, ARRAY_SIZE (mips_cp0sel_names_mips3264r2),
    mips_hwr_names_mips3264r2 },

  /* For stock MIPS64, disassemble all applicable MIPS-specified ASEs.  */
  { "mips64",	1, bfd_mach_mipsisa64, CPU_MIPS64,
    ISA_MIPS64 | INSN_MIPS16 | INSN_MIPS3D | INSN_MDMX,
    mips_cp0_names_mips3264,
    mips_cp0sel_names_mips3264, ARRAY_SIZE (mips_cp0sel_names_mips3264),
    mips_hwr_names_numeric },

  { "mips64r2",	1, bfd_mach_mipsisa64r2, CPU_MIPS64R2,
    (ISA_MIPS64R2 | INSN_MIPS16 | INSN_MIPS3D | INSN_DSP | INSN_DSPR2
     | INSN_DSP64 | INSN_MT | INSN_MDMX),
    mips_cp0_names_mips3264r2,
    mips_cp0sel_names_mips3264r2, ARRAY_SIZE (mips_cp0sel_names_mips3264r2),
    mips_hwr_names_mips3264r2 },

  { "sb1",	1, bfd_mach_mips_sb1, CPU_SB1,
    ISA_MIPS64 | INSN_MIPS3D | INSN_SB1,
    mips_cp0_names_sb1,
    mips_cp0sel_names_sb1, ARRAY_SIZE (mips_cp0sel_names_sb1),
    mips_hwr_names_numeric },

  /* This entry, mips16, is here only for ISA/processor selection; do
     not print its name.  */
  { "",		1, bfd_mach_mips16, CPU_MIPS16, ISA_MIPS3 | INSN_MIPS16,
    mips_cp0_names_numeric, NULL, 0, mips_hwr_names_numeric },
};

/* ISA and processor type to disassemble for, and register names to use.
   set_default_mips_dis_options and parse_mips_dis_options fill in these
   values.  */
static int mips_processor;
static int mips_isa;
static const char * const *mips_gpr_names;
static const char * const *mips_fpr_names;
static const char * const *mips_cp0_names;
static const struct mips_cp0sel_name *mips_cp0sel_names;
static int mips_cp0sel_names_len;
static const char * const *mips_hwr_names;

/* Other options */
static int no_aliases;	/* If set disassemble as most general inst.  */

static const struct mips_abi_choice *
choose_abi_by_name (const char *name, unsigned int namelen)
{
  const struct mips_abi_choice *c;
  unsigned int i;

  for (i = 0, c = NULL; i < ARRAY_SIZE (mips_abi_choices) && c == NULL; i++)
    if (strncmp (mips_abi_choices[i].name, name, namelen) == 0
	&& strlen (mips_abi_choices[i].name) == namelen)
      c = &mips_abi_choices[i];

  return c;
}

static const struct mips_arch_choice *
choose_arch_by_name (const char *name, unsigned int namelen)
{
  const struct mips_arch_choice *c = NULL;
  unsigned int i;

  for (i = 0, c = NULL; i < ARRAY_SIZE (mips_arch_choices) && c == NULL; i++)
    if (strncmp (mips_arch_choices[i].name, name, namelen) == 0
	&& strlen (mips_arch_choices[i].name) == namelen)
      c = &mips_arch_choices[i];

  return c;
}

static const struct mips_arch_choice *
choose_arch_by_number (unsigned long mach)
{
  static unsigned long hint_bfd_mach;
  static const struct mips_arch_choice *hint_arch_choice;
  const struct mips_arch_choice *c;
  unsigned int i;

  /* We optimize this because even if the user specifies no
     flags, this will be done for every instruction!  */
  if (hint_bfd_mach == mach
      && hint_arch_choice != NULL
      && hint_arch_choice->bfd_mach == hint_bfd_mach)
    return hint_arch_choice;

  for (i = 0, c = NULL; i < ARRAY_SIZE (mips_arch_choices) && c == NULL; i++)
    {
      if (mips_arch_choices[i].bfd_mach_valid
	  && mips_arch_choices[i].bfd_mach == mach)
	{
	  c = &mips_arch_choices[i];
	  hint_bfd_mach = mach;
	  hint_arch_choice = c;
	}
    }
  return c;
}

static void
set_default_mips_dis_options (struct disassemble_info *info)
{
  const struct mips_arch_choice *chosen_arch;

  /* Defaults: mipsIII/r3000 (?!), (o)32-style ("oldabi") GPR names,
     and numeric FPR, CP0 register, and HWR names.  */
  mips_isa = ISA_MIPS3;
  mips_processor =  CPU_R3000;
  mips_gpr_names = mips_gpr_names_oldabi;
  mips_fpr_names = mips_fpr_names_numeric;
  mips_cp0_names = mips_cp0_names_numeric;
  mips_cp0sel_names = NULL;
  mips_cp0sel_names_len = 0;
  mips_hwr_names = mips_hwr_names_numeric;
  no_aliases = 0;

  /* If an ELF "newabi" binary, use the n32/(n)64 GPR names.  */
#if 0
  if (info->flavour == bfd_target_elf_flavour && info->section != NULL)
    {
      Elf_Internal_Ehdr *header;

      header = elf_elfheader (info->section->owner);
      if (is_newabi (header))
	mips_gpr_names = mips_gpr_names_newabi;
    }
#endif

  /* Set ISA, architecture, and cp0 register names as best we can.  */
#if !defined(SYMTAB_AVAILABLE) && 0
  /* This is running out on a target machine, not in a host tool.
     FIXME: Where does mips_target_info come from?  */
  target_processor = mips_target_info.processor;
  mips_isa = mips_target_info.isa;
#else
  chosen_arch = choose_arch_by_number (info->mach);
  if (chosen_arch != NULL)
    {
      mips_processor = chosen_arch->processor;
      mips_isa = chosen_arch->isa;
      mips_cp0_names = chosen_arch->cp0_names;
      mips_cp0sel_names = chosen_arch->cp0sel_names;
      mips_cp0sel_names_len = chosen_arch->cp0sel_names_len;
      mips_hwr_names = chosen_arch->hwr_names;
    }
#endif
}

static void
parse_mips_dis_option (const char *option, unsigned int len)
{
  unsigned int i, optionlen, vallen;
  const char *val;
  const struct mips_abi_choice *chosen_abi;
  const struct mips_arch_choice *chosen_arch;

  /* Look for the = that delimits the end of the option name.  */
  for (i = 0; i < len; i++)
    {
      if (option[i] == '=')
	break;
    }
  if (i == 0)		/* Invalid option: no name before '='.  */
    return;
  if (i == len)		/* Invalid option: no '='.  */
    return;
  if (i == (len - 1))	/* Invalid option: no value after '='.  */
    return;

  optionlen = i;
  val = option + (optionlen + 1);
  vallen = len - (optionlen + 1);

  if (strncmp("gpr-names", option, optionlen) == 0
      && strlen("gpr-names") == optionlen)
    {
      chosen_abi = choose_abi_by_name (val, vallen);
      if (chosen_abi != NULL)
	mips_gpr_names = chosen_abi->gpr_names;
      return;
    }

  if (strncmp("fpr-names", option, optionlen) == 0
      && strlen("fpr-names") == optionlen)
    {
      chosen_abi = choose_abi_by_name (val, vallen);
      if (chosen_abi != NULL)
	mips_fpr_names = chosen_abi->fpr_names;
      return;
    }

  if (strncmp("cp0-names", option, optionlen) == 0
      && strlen("cp0-names") == optionlen)
    {
      chosen_arch = choose_arch_by_name (val, vallen);
      if (chosen_arch != NULL)
	{
	  mips_cp0_names = chosen_arch->cp0_names;
	  mips_cp0sel_names = chosen_arch->cp0sel_names;
	  mips_cp0sel_names_len = chosen_arch->cp0sel_names_len;
	}
      return;
    }

  if (strncmp("hwr-names", option, optionlen) == 0
      && strlen("hwr-names") == optionlen)
    {
      chosen_arch = choose_arch_by_name (val, vallen);
      if (chosen_arch != NULL)
	mips_hwr_names = chosen_arch->hwr_names;
      return;
    }

  if (strncmp("reg-names", option, optionlen) == 0
      && strlen("reg-names") == optionlen)
    {
      /* We check both ABI and ARCH here unconditionally, so
	 that "numeric" will do the desirable thing: select
	 numeric register names for all registers.  Other than
	 that, a given name probably won't match both.  */
      chosen_abi = choose_abi_by_name (val, vallen);
      if (chosen_abi != NULL)
	{
	  mips_gpr_names = chosen_abi->gpr_names;
	  mips_fpr_names = chosen_abi->fpr_names;
	}
      chosen_arch = choose_arch_by_name (val, vallen);
      if (chosen_arch != NULL)
	{
	  mips_cp0_names = chosen_arch->cp0_names;
	  mips_cp0sel_names = chosen_arch->cp0sel_names;
	  mips_cp0sel_names_len = chosen_arch->cp0sel_names_len;
	  mips_hwr_names = chosen_arch->hwr_names;
	}
      return;
    }

  /* Invalid option.  */
}

static void
parse_mips_dis_options (const char *options)
{
  const char *option_end;

  if (options == NULL)
    return;

  while (*options != '\0')
    {
      /* Skip empty options.  */
      if (*options == ',')
	{
	  options++;
	  continue;
	}

      /* We know that *options is neither NUL or a comma.  */
      option_end = options + 1;
      while (*option_end != ',' && *option_end != '\0')
	option_end++;

      parse_mips_dis_option (options, option_end - options);

      /* Go on to the next one.  If option_end points to a comma, it
	 will be skipped above.  */
      options = option_end;
    }
}

static const struct mips_cp0sel_name *
lookup_mips_cp0sel_name (const struct mips_cp0sel_name *names,
			 unsigned int len,
			 unsigned int cp0reg,
			 unsigned int sel)
{
  unsigned int i;

  for (i = 0; i < len; i++)
    if (names[i].cp0reg == cp0reg && names[i].sel == sel)
      return &names[i];
  return NULL;
}

/* Print insn arguments for 32/64-bit code.  */

static void
print_insn_args (const char *d,
		 register unsigned long int l,
		 bfd_vma pc,
		 struct disassemble_info *info,
		 const struct mips_opcode *opp)
{
  int op, delta;
  unsigned int lsb, msb, msbd;

  lsb = 0;

  for (; *d != '\0'; d++)
    {
      switch (*d)
	{
	case ',':
	case '(':
	case ')':
	case '[':
	case ']':
	  (*info->fprintf_func) (info->stream, "%c", *d);
	  break;

	case '+':
	  /* Extension character; switch for second char.  */
	  d++;
	  switch (*d)
	    {
	    case '\0':
	      /* xgettext:c-format */
	      (*info->fprintf_func) (info->stream,
				     _("# internal error, incomplete extension sequence (+)"));
	      return;

	    case 'A':
	      lsb = (l >> OP_SH_SHAMT) & OP_MASK_SHAMT;
	      (*info->fprintf_func) (info->stream, "0x%x", lsb);
	      break;

	    case 'B':
	      msb = (l >> OP_SH_INSMSB) & OP_MASK_INSMSB;
	      (*info->fprintf_func) (info->stream, "0x%x", msb - lsb + 1);
	      break;

	    case '1':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI1) & OP_MASK_UDI1);
	      break;

	    case '2':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI2) & OP_MASK_UDI2);
	      break;

	    case '3':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI3) & OP_MASK_UDI3);
	      break;

	    case '4':
	      (*info->fprintf_func) (info->stream, "0x%lx",
				     (l >> OP_SH_UDI4) & OP_MASK_UDI4);
	      break;

	    case 'C':
	    case 'H':
	      msbd = (l >> OP_SH_EXTMSBD) & OP_MASK_EXTMSBD;
	      (*info->fprintf_func) (info->stream, "0x%x", msbd + 1);
	      break;

	    case 'D':
	      {
		const struct mips_cp0sel_name *n;
		unsigned int cp0reg, sel;

		cp0reg = (l >> OP_SH_RD) & OP_MASK_RD;
		sel = (l >> OP_SH_SEL) & OP_MASK_SEL;

		/* CP0 register including 'sel' code for mtcN (et al.), to be
		   printed textually if known.  If not known, print both
		   CP0 register name and sel numerically since CP0 register
		   with sel 0 may have a name unrelated to register being
		   printed.  */
		n = lookup_mips_cp0sel_name(mips_cp0sel_names,
					    mips_cp0sel_names_len, cp0reg, sel);
		if (n != NULL)
		  (*info->fprintf_func) (info->stream, "%s", n->name);
		else
		  (*info->fprintf_func) (info->stream, "$%d,%d", cp0reg, sel);
		break;
	      }

	    case 'E':
	      lsb = ((l >> OP_SH_SHAMT) & OP_MASK_SHAMT) + 32;
	      (*info->fprintf_func) (info->stream, "0x%x", lsb);
	      break;

	    case 'F':
	      msb = ((l >> OP_SH_INSMSB) & OP_MASK_INSMSB) + 32;
	      (*info->fprintf_func) (info->stream, "0x%x", msb - lsb + 1);
	      break;

	    case 'G':
	      msbd = ((l >> OP_SH_EXTMSBD) & OP_MASK_EXTMSBD) + 32;
	      (*info->fprintf_func) (info->stream, "0x%x", msbd + 1);
	      break;

	    case 't': /* Coprocessor 0 reg name */
	      (*info->fprintf_func) (info->stream, "%s",
				     mips_cp0_names[(l >> OP_SH_RT) &
						     OP_MASK_RT]);
	      break;

	    case 'T': /* Coprocessor 0 reg name */
	      {
		const struct mips_cp0sel_name *n;
		unsigned int cp0reg, sel;

		cp0reg = (l >> OP_SH_RT) & OP_MASK_RT;
		sel = (l >> OP_SH_SEL) & OP_MASK_SEL;

		/* CP0 register including 'sel' code for mftc0, to be
		   printed textually if known.  If not known, print both
		   CP0 register name and sel numerically since CP0 register
		   with sel 0 may have a name unrelated to register being
		   printed.  */
		n = lookup_mips_cp0sel_name(mips_cp0sel_names,
					    mips_cp0sel_names_len, cp0reg, sel);
		if (n != NULL)
		  (*info->fprintf_func) (info->stream, "%s", n->name);
		else
		  (*info->fprintf_func) (info->stream, "$%d,%d", cp0reg, sel);
		break;
	      }

	    default:
	      /* xgettext:c-format */
	      (*info->fprintf_func) (info->stream,
				     _("# internal error, undefined extension sequence (+%c)"),
				     *d);
	      return;
	    }
	  break;

	case '2':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_BP) & OP_MASK_BP);
	  break;

	case '3':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_SA3) & OP_MASK_SA3);
	  break;

	case '4':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_SA4) & OP_MASK_SA4);
	  break;

	case '5':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_IMM8) & OP_MASK_IMM8);
	  break;

	case '6':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_RS) & OP_MASK_RS);
	  break;

	case '7':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_DSPACC) & OP_MASK_DSPACC);
	  break;

	case '8':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_WRDSP) & OP_MASK_WRDSP);
	  break;

	case '9':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_DSPACC_S) & OP_MASK_DSPACC_S);
	  break;

	case '0': /* dsp 6-bit signed immediate in bit 20 */
	  delta = ((l >> OP_SH_DSPSFT) & OP_MASK_DSPSFT);
	  if (delta & 0x20) /* test sign bit */
	    delta |= ~OP_MASK_DSPSFT;
	  (*info->fprintf_func) (info->stream, "%d", delta);
	  break;

	case ':': /* dsp 7-bit signed immediate in bit 19 */
	  delta = ((l >> OP_SH_DSPSFT_7) & OP_MASK_DSPSFT_7);
	  if (delta & 0x40) /* test sign bit */
	    delta |= ~OP_MASK_DSPSFT_7;
	  (*info->fprintf_func) (info->stream, "%d", delta);
	  break;

	case '\'':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_RDDSP) & OP_MASK_RDDSP);
	  break;

	case '@': /* dsp 10-bit signed immediate in bit 16 */
	  delta = ((l >> OP_SH_IMM10) & OP_MASK_IMM10);
	  if (delta & 0x200) /* test sign bit */
	    delta |= ~OP_MASK_IMM10;
	  (*info->fprintf_func) (info->stream, "%d", delta);
	  break;

	case '!':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_MT_U) & OP_MASK_MT_U);
	  break;

	case '$':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_MT_H) & OP_MASK_MT_H);
	  break;

	case '*':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_MTACC_T) & OP_MASK_MTACC_T);
	  break;

	case '&':
	  (*info->fprintf_func) (info->stream, "$ac%ld",
				 (l >> OP_SH_MTACC_D) & OP_MASK_MTACC_D);
	  break;

	case 'g':
	  /* Coprocessor register for CTTC1, MTTC2, MTHC2, CTTC2.  */
	  (*info->fprintf_func) (info->stream, "$%ld",
				 (l >> OP_SH_RD) & OP_MASK_RD);
	  break;

	case 's':
	case 'b':
	case 'r':
	case 'v':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_gpr_names[(l >> OP_SH_RS) & OP_MASK_RS]);
	  break;

	case 't':
	case 'w':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_gpr_names[(l >> OP_SH_RT) & OP_MASK_RT]);
	  break;

	case 'i':
	case 'u':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_IMMEDIATE) & OP_MASK_IMMEDIATE);
	  break;

	case 'j': /* Same as i, but sign-extended.  */
	case 'o':
	  delta = (l >> OP_SH_DELTA) & OP_MASK_DELTA;
	  if (delta & 0x8000)
	    delta |= ~0xffff;
	  (*info->fprintf_func) (info->stream, "%d",
				 delta);
	  break;

	case 'h':
	  (*info->fprintf_func) (info->stream, "0x%x",
				 (unsigned int) ((l >> OP_SH_PREFX)
						 & OP_MASK_PREFX));
	  break;

	case 'k':
	  (*info->fprintf_func) (info->stream, "0x%x",
				 (unsigned int) ((l >> OP_SH_CACHE)
						 & OP_MASK_CACHE));
	  break;

	case 'a':
	  info->target = (((pc + 4) & ~(bfd_vma) 0x0fffffff)
			  | (((l >> OP_SH_TARGET) & OP_MASK_TARGET) << 2));
	  /* For gdb disassembler, force odd address on jalx.  */
	  if (info->flavour == bfd_target_unknown_flavour
	      && strcmp (opp->name, "jalx") == 0)
	    info->target |= 1;
	  (*info->print_address_func) (info->target, info);
	  break;

	case 'p':
	  /* Sign extend the displacement.  */
	  delta = (l >> OP_SH_DELTA) & OP_MASK_DELTA;
	  if (delta & 0x8000)
	    delta |= ~0xffff;
	  info->target = (delta << 2) + pc + INSNLEN;
	  (*info->print_address_func) (info->target, info);
	  break;

	case 'd':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_gpr_names[(l >> OP_SH_RD) & OP_MASK_RD]);
	  break;

	case 'U':
	  {
	    /* First check for both rd and rt being equal.  */
	    unsigned int reg = (l >> OP_SH_RD) & OP_MASK_RD;
	    if (reg == ((l >> OP_SH_RT) & OP_MASK_RT))
	      (*info->fprintf_func) (info->stream, "%s",
				     mips_gpr_names[reg]);
	    else
	      {
		/* If one is zero use the other.  */
		if (reg == 0)
		  (*info->fprintf_func) (info->stream, "%s",
					 mips_gpr_names[(l >> OP_SH_RT) & OP_MASK_RT]);
		else if (((l >> OP_SH_RT) & OP_MASK_RT) == 0)
		  (*info->fprintf_func) (info->stream, "%s",
					 mips_gpr_names[reg]);
		else /* Bogus, result depends on processor.  */
		  (*info->fprintf_func) (info->stream, "%s or %s",
					 mips_gpr_names[reg],
					 mips_gpr_names[(l >> OP_SH_RT) & OP_MASK_RT]);
	      }
	  }
	  break;

	case 'z':
	  (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[0]);
	  break;

	case '<':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_SHAMT) & OP_MASK_SHAMT);
	  break;

	case 'c':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_CODE) & OP_MASK_CODE);
	  break;

	case 'q':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_CODE2) & OP_MASK_CODE2);
	  break;

	case 'C':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_COPZ) & OP_MASK_COPZ);
	  break;

	case 'B':
	  (*info->fprintf_func) (info->stream, "0x%lx",

				 (l >> OP_SH_CODE20) & OP_MASK_CODE20);
	  break;

	case 'J':
	  (*info->fprintf_func) (info->stream, "0x%lx",
				 (l >> OP_SH_CODE19) & OP_MASK_CODE19);
	  break;

	case 'S':
	case 'V':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FS) & OP_MASK_FS]);
	  break;

	case 'T':
	case 'W':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FT) & OP_MASK_FT]);
	  break;

	case 'D':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FD) & OP_MASK_FD]);
	  break;

	case 'R':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_fpr_names[(l >> OP_SH_FR) & OP_MASK_FR]);
	  break;

	case 'E':
	  /* Coprocessor register for lwcN instructions, et al.

	     Note that there is no load/store cp0 instructions, and
	     that FPU (cp1) instructions disassemble this field using
	     'T' format.  Therefore, until we gain understanding of
	     cp2 register names, we can simply print the register
	     numbers.  */
	  (*info->fprintf_func) (info->stream, "$%ld",
				 (l >> OP_SH_RT) & OP_MASK_RT);
	  break;

	case 'G':
	  /* Coprocessor register for mtcN instructions, et al.  Note
	     that FPU (cp1) instructions disassemble this field using
	     'S' format.  Therefore, we only need to worry about cp0,
	     cp2, and cp3.  */
	  op = (l >> OP_SH_OP) & OP_MASK_OP;
	  if (op == OP_OP_COP0)
	    (*info->fprintf_func) (info->stream, "%s",
				   mips_cp0_names[(l >> OP_SH_RD) & OP_MASK_RD]);
	  else
	    (*info->fprintf_func) (info->stream, "$%ld",
				   (l >> OP_SH_RD) & OP_MASK_RD);
	  break;

	case 'K':
	  (*info->fprintf_func) (info->stream, "%s",
				 mips_hwr_names[(l >> OP_SH_RD) & OP_MASK_RD]);
	  break;

	case 'N':
	  (*info->fprintf_func) (info->stream,
				 ((opp->pinfo & (FP_D | FP_S)) != 0
				  ? "$fcc%ld" : "$cc%ld"),
				 (l >> OP_SH_BCC) & OP_MASK_BCC);
	  break;

	case 'M':
	  (*info->fprintf_func) (info->stream, "$fcc%ld",
				 (l >> OP_SH_CCC) & OP_MASK_CCC);
	  break;

	case 'P':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_PERFREG) & OP_MASK_PERFREG);
	  break;

	case 'e':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_VECBYTE) & OP_MASK_VECBYTE);
	  break;

	case '%':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_VECALIGN) & OP_MASK_VECALIGN);
	  break;

	case 'H':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_SEL) & OP_MASK_SEL);
	  break;

	case 'O':
	  (*info->fprintf_func) (info->stream, "%ld",
				 (l >> OP_SH_ALN) & OP_MASK_ALN);
	  break;

	case 'Q':
	  {
	    unsigned int vsel = (l >> OP_SH_VSEL) & OP_MASK_VSEL;

	    if ((vsel & 0x10) == 0)
	      {
		int fmt;

		vsel &= 0x0f;
		for (fmt = 0; fmt < 3; fmt++, vsel >>= 1)
		  if ((vsel & 1) == 0)
		    break;
		(*info->fprintf_func) (info->stream, "$v%ld[%d]",
				       (l >> OP_SH_FT) & OP_MASK_FT,
				       vsel >> 1);
	      }
	    else if ((vsel & 0x08) == 0)
	      {
		(*info->fprintf_func) (info->stream, "$v%ld",
				       (l >> OP_SH_FT) & OP_MASK_FT);
	      }
	    else
	      {
		(*info->fprintf_func) (info->stream, "0x%lx",
				       (l >> OP_SH_FT) & OP_MASK_FT);
	      }
	  }
	  break;

	case 'X':
	  (*info->fprintf_func) (info->stream, "$v%ld",
				 (l >> OP_SH_FD) & OP_MASK_FD);
	  break;

	case 'Y':
	  (*info->fprintf_func) (info->stream, "$v%ld",
				 (l >> OP_SH_FS) & OP_MASK_FS);
	  break;

	case 'Z':
	  (*info->fprintf_func) (info->stream, "$v%ld",
				 (l >> OP_SH_FT) & OP_MASK_FT);
	  break;

	default:
	  /* xgettext:c-format */
	  (*info->fprintf_func) (info->stream,
				 _("# internal error, undefined modifier(%c)"),
				 *d);
	  return;
	}
    }
}

/* Check if the object uses NewABI conventions.  */
#if 0
static int
is_newabi (header)
     Elf_Internal_Ehdr *header;
{
  /* There are no old-style ABIs which use 64-bit ELF.  */
  if (header->e_ident[EI_CLASS] == ELFCLASS64)
    return 1;

  /* If a 32-bit ELF file, n32 is a new-style ABI.  */
  if ((header->e_flags & EF_MIPS_ABI2) != 0)
    return 1;

  return 0;
}
#endif

/* Print the mips instruction at address MEMADDR in debugged memory,
   on using INFO.  Returns length of the instruction, in bytes, which is
   always INSNLEN.  BIGENDIAN must be 1 if this is big-endian code, 0 if
   this is little-endian code.  */

static int
print_insn_mips (bfd_vma memaddr,
		 unsigned long int word,
		 struct disassemble_info *info)
{
  const struct mips_opcode *op;
  static bfd_boolean init = 0;
  static const struct mips_opcode *mips_hash[OP_MASK_OP + 1];

  /* Build a hash table to shorten the search time.  */
  if (! init)
    {
      unsigned int i;

      for (i = 0; i <= OP_MASK_OP; i++)
	{
	  for (op = mips_opcodes; op < &mips_opcodes[NUMOPCODES]; op++)
	    {
	      if (op->pinfo == INSN_MACRO
		  || (no_aliases && (op->pinfo2 & INSN2_ALIAS)))
		continue;
	      if (i == ((op->match >> OP_SH_OP) & OP_MASK_OP))
		{
		  mips_hash[i] = op;
		  break;
		}
	    }
	}

      init = 1;
    }

  info->bytes_per_chunk = INSNLEN;
  info->display_endian = info->endian;
  info->insn_info_valid = 1;
  info->branch_delay_insns = 0;
  info->data_size = 0;
  info->insn_type = dis_nonbranch;
  info->target = 0;
  info->target2 = 0;

  op = mips_hash[(word >> OP_SH_OP) & OP_MASK_OP];
  if (op != NULL)
    {
      for (; op < &mips_opcodes[NUMOPCODES]; op++)
	{
	  if (op->pinfo != INSN_MACRO
	      && !(no_aliases && (op->pinfo2 & INSN2_ALIAS))
	      && (word & op->mask) == op->match)
	    {
	      const char *d;

	      /* We always allow to disassemble the jalx instruction.  */
	      if (! OPCODE_IS_MEMBER (op, mips_isa, mips_processor)
		  && strcmp (op->name, "jalx"))
		continue;

	      /* Figure out instruction type and branch delay information.  */
	      if ((op->pinfo & INSN_UNCOND_BRANCH_DELAY) != 0)
	        {
		  if ((info->insn_type & INSN_WRITE_GPR_31) != 0)
		    info->insn_type = dis_jsr;
		  else
		    info->insn_type = dis_branch;
		  info->branch_delay_insns = 1;
		}
	      else if ((op->pinfo & (INSN_COND_BRANCH_DELAY
				     | INSN_COND_BRANCH_LIKELY)) != 0)
		{
		  if ((info->insn_type & INSN_WRITE_GPR_31) != 0)
		    info->insn_type = dis_condjsr;
		  else
		    info->insn_type = dis_condbranch;
		  info->branch_delay_insns = 1;
		}
	      else if ((op->pinfo & (INSN_STORE_MEMORY
				     | INSN_LOAD_MEMORY_DELAY)) != 0)
		info->insn_type = dis_dref;

	      (*info->fprintf_func) (info->stream, "%s", op->name);

	      d = op->args;
	      if (d != NULL && *d != '\0')
		{
		  (*info->fprintf_func) (info->stream, "\t");
		  print_insn_args (d, word, memaddr, info, op);
		}

	      return INSNLEN;
	    }
	}
    }

  /* Handle undefined instructions.  */
  info->insn_type = dis_noninsn;
  (*info->fprintf_func) (info->stream, "0x%lx", word);
  return INSNLEN;
}

/* In an environment where we do not know the symbol type of the
   instruction we are forced to assume that the low order bit of the
   instructions' address may mark it as a mips16 instruction.  If we
   are single stepping, or the pc is within the disassembled function,
   this works.  Otherwise, we need a clue.  Sometimes.  */

static int
_print_insn_mips (bfd_vma memaddr,
		  struct disassemble_info *info,
		  enum bfd_endian endianness)
{
  bfd_byte buffer[INSNLEN];
  int status;

  set_default_mips_dis_options (info);
  parse_mips_dis_options (info->disassembler_options);

#if 0
#if 1
  /* FIXME: If odd address, this is CLEARLY a mips 16 instruction.  */
  /* Only a few tools will work this way.  */
  if (memaddr & 0x01)
    return print_insn_mips16 (memaddr, info);
#endif

#if SYMTAB_AVAILABLE
  if (info->mach == bfd_mach_mips16
      || (info->flavour == bfd_target_elf_flavour
	  && info->symbols != NULL
	  && ((*(elf_symbol_type **) info->symbols)->internal_elf_sym.st_other
	      == STO_MIPS16)))
    return print_insn_mips16 (memaddr, info);
#endif
#endif

  status = (*info->read_memory_func) (memaddr, buffer, INSNLEN, info);
  if (status == 0)
    {
      unsigned long insn;

      if (endianness == BFD_ENDIAN_BIG)
	insn = (unsigned long) bfd_getb32 (buffer);
      else
	insn = (unsigned long) bfd_getl32 (buffer);

      return print_insn_mips (memaddr, insn, info);
    }
  else
    {
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }
}

int
print_insn_big_mips (bfd_vma memaddr, struct disassemble_info *info)
{
  return _print_insn_mips (memaddr, info, BFD_ENDIAN_BIG);
}

int
print_insn_little_mips (bfd_vma memaddr, struct disassemble_info *info)
{
  return _print_insn_mips (memaddr, info, BFD_ENDIAN_LITTLE);
}

/* Disassemble mips16 instructions.  */
#if 0
static int
print_insn_mips16 (bfd_vma memaddr, struct disassemble_info *info)
{
  int status;
  bfd_byte buffer[2];
  int length;
  int insn;
  bfd_boolean use_extend;
  int extend = 0;
  const struct mips_opcode *op, *opend;

  info->bytes_per_chunk = 2;
  info->display_endian = info->endian;
  info->insn_info_valid = 1;
  info->branch_delay_insns = 0;
  info->data_size = 0;
  info->insn_type = dis_nonbranch;
  info->target = 0;
  info->target2 = 0;

  status = (*info->read_memory_func) (memaddr, buffer, 2, info);
  if (status != 0)
    {
      (*info->memory_error_func) (status, memaddr, info);
      return -1;
    }

  length = 2;

  if (info->endian == BFD_ENDIAN_BIG)
    insn = bfd_getb16 (buffer);
  else
    insn = bfd_getl16 (buffer);

  /* Handle the extend opcode specially.  */
  use_extend = FALSE;
  if ((insn & 0xf800) == 0xf000)
    {
      use_extend = TRUE;
      extend = insn & 0x7ff;

      memaddr += 2;

      status = (*info->read_memory_func) (memaddr, buffer, 2, info);
      if (status != 0)
	{
	  (*info->fprintf_func) (info->stream, "extend 0x%x",
				 (unsigned int) extend);
	  (*info->memory_error_func) (status, memaddr, info);
	  return -1;
	}

      if (info->endian == BFD_ENDIAN_BIG)
	insn = bfd_getb16 (buffer);
      else
	insn = bfd_getl16 (buffer);

      /* Check for an extend opcode followed by an extend opcode.  */
      if ((insn & 0xf800) == 0xf000)
	{
	  (*info->fprintf_func) (info->stream, "extend 0x%x",
				 (unsigned int) extend);
	  info->insn_type = dis_noninsn;
	  return length;
	}

      length += 2;
    }

  /* FIXME: Should probably use a hash table on the major opcode here.  */

  opend = mips16_opcodes + bfd_mips16_num_opcodes;
  for (op = mips16_opcodes; op < opend; op++)
    {
      if (op->pinfo != INSN_MACRO
	  && !(no_aliases && (op->pinfo2 & INSN2_ALIAS))
	  && (insn & op->mask) == op->match)
	{
	  const char *s;

	  if (strchr (op->args, 'a') != NULL)
	    {
	      if (use_extend)
		{
		  (*info->fprintf_func) (info->stream, "extend 0x%x",
					 (unsigned int) extend);
		  info->insn_type = dis_noninsn;
		  return length - 2;
		}

	      use_extend = FALSE;

	      memaddr += 2;

	      status = (*info->read_memory_func) (memaddr, buffer, 2,
						  info);
	      if (status == 0)
		{
		  use_extend = TRUE;
		  if (info->endian == BFD_ENDIAN_BIG)
		    extend = bfd_getb16 (buffer);
		  else
		    extend = bfd_getl16 (buffer);
		  length += 2;
		}
	    }

	  (*info->fprintf_func) (info->stream, "%s", op->name);
	  if (op->args[0] != '\0')
	    (*info->fprintf_func) (info->stream, "\t");

	  for (s = op->args; *s != '\0'; s++)
	    {
	      if (*s == ','
		  && s[1] == 'w'
		  && (((insn >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX)
		      == ((insn >> MIPS16OP_SH_RY) & MIPS16OP_MASK_RY)))
		{
		  /* Skip the register and the comma.  */
		  ++s;
		  continue;
		}
	      if (*s == ','
		  && s[1] == 'v'
		  && (((insn >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ)
		      == ((insn >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX)))
		{
		  /* Skip the register and the comma.  */
		  ++s;
		  continue;
		}
	      print_mips16_insn_arg (*s, op, insn, use_extend, extend, memaddr,
				     info);
	    }

	  if ((op->pinfo & INSN_UNCOND_BRANCH_DELAY) != 0)
	    {
	      info->branch_delay_insns = 1;
	      if (info->insn_type != dis_jsr)
		info->insn_type = dis_branch;
	    }

	  return length;
	}
    }

  if (use_extend)
    (*info->fprintf_func) (info->stream, "0x%x", extend | 0xf000);
  (*info->fprintf_func) (info->stream, "0x%x", insn);
  info->insn_type = dis_noninsn;

  return length;
}

/* Disassemble an operand for a mips16 instruction.  */

static void
print_mips16_insn_arg (char type,
		       const struct mips_opcode *op,
		       int l,
		       bfd_boolean use_extend,
		       int extend,
		       bfd_vma memaddr,
		       struct disassemble_info *info)
{
  switch (type)
    {
    case ',':
    case '(':
    case ')':
      (*info->fprintf_func) (info->stream, "%c", type);
      break;

    case 'y':
    case 'w':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_RY)
					       & MIPS16OP_MASK_RY)));
      break;

    case 'x':
    case 'v':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_RX)
					       & MIPS16OP_MASK_RX)));
      break;

    case 'z':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_RZ)
					       & MIPS16OP_MASK_RZ)));
      break;

    case 'Z':
      (*info->fprintf_func) (info->stream, "%s",
			     mips16_reg_names(((l >> MIPS16OP_SH_MOVE32Z)
					       & MIPS16OP_MASK_MOVE32Z)));
      break;

    case '0':
      (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[0]);
      break;

    case 'S':
      (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[29]);
      break;

    case 'P':
      (*info->fprintf_func) (info->stream, "$pc");
      break;

    case 'R':
      (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[31]);
      break;

    case 'X':
      (*info->fprintf_func) (info->stream, "%s",
			     mips_gpr_names[((l >> MIPS16OP_SH_REGR32)
					    & MIPS16OP_MASK_REGR32)]);
      break;

    case 'Y':
      (*info->fprintf_func) (info->stream, "%s",
			     mips_gpr_names[MIPS16OP_EXTRACT_REG32R (l)]);
      break;

    case '<':
    case '>':
    case '[':
    case ']':
    case '4':
    case '5':
    case 'H':
    case 'W':
    case 'D':
    case 'j':
    case '6':
    case '8':
    case 'V':
    case 'C':
    case 'U':
    case 'k':
    case 'K':
    case 'p':
    case 'q':
    case 'A':
    case 'B':
    case 'E':
      {
	int immed, nbits, shift, signedp, extbits, pcrel, extu, branch;

	shift = 0;
	signedp = 0;
	extbits = 16;
	pcrel = 0;
	extu = 0;
	branch = 0;
	switch (type)
	  {
	  case '<':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ;
	    extbits = 5;
	    extu = 1;
	    break;
	  case '>':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX;
	    extbits = 5;
	    extu = 1;
	    break;
	  case '[':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RZ) & MIPS16OP_MASK_RZ;
	    extbits = 6;
	    extu = 1;
	    break;
	  case ']':
	    nbits = 3;
	    immed = (l >> MIPS16OP_SH_RX) & MIPS16OP_MASK_RX;
	    extbits = 6;
	    extu = 1;
	    break;
	  case '4':
	    nbits = 4;
	    immed = (l >> MIPS16OP_SH_IMM4) & MIPS16OP_MASK_IMM4;
	    signedp = 1;
	    extbits = 15;
	    break;
	  case '5':
	    nbits = 5;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    info->insn_type = dis_dref;
	    info->data_size = 1;
	    break;
	  case 'H':
	    nbits = 5;
	    shift = 1;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    info->insn_type = dis_dref;
	    info->data_size = 2;
	    break;
	  case 'W':
	    nbits = 5;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    if ((op->pinfo & MIPS16_INSN_READ_PC) == 0
		&& (op->pinfo & MIPS16_INSN_READ_SP) == 0)
	      {
		info->insn_type = dis_dref;
		info->data_size = 4;
	      }
	    break;
	  case 'D':
	    nbits = 5;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    info->insn_type = dis_dref;
	    info->data_size = 8;
	    break;
	  case 'j':
	    nbits = 5;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    signedp = 1;
	    break;
	  case '6':
	    nbits = 6;
	    immed = (l >> MIPS16OP_SH_IMM6) & MIPS16OP_MASK_IMM6;
	    break;
	  case '8':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    break;
	  case 'V':
	    nbits = 8;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    /* FIXME: This might be lw, or it might be addiu to $sp or
               $pc.  We assume it's load.  */
	    info->insn_type = dis_dref;
	    info->data_size = 4;
	    break;
	  case 'C':
	    nbits = 8;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    info->insn_type = dis_dref;
	    info->data_size = 8;
	    break;
	  case 'U':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    extu = 1;
	    break;
	  case 'k':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    signedp = 1;
	    break;
	  case 'K':
	    nbits = 8;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    signedp = 1;
	    break;
	  case 'p':
	    nbits = 8;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    signedp = 1;
	    pcrel = 1;
	    branch = 1;
	    info->insn_type = dis_condbranch;
	    break;
	  case 'q':
	    nbits = 11;
	    immed = (l >> MIPS16OP_SH_IMM11) & MIPS16OP_MASK_IMM11;
	    signedp = 1;
	    pcrel = 1;
	    branch = 1;
	    info->insn_type = dis_branch;
	    break;
	  case 'A':
	    nbits = 8;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM8) & MIPS16OP_MASK_IMM8;
	    pcrel = 1;
	    /* FIXME: This can be lw or la.  We assume it is lw.  */
	    info->insn_type = dis_dref;
	    info->data_size = 4;
	    break;
	  case 'B':
	    nbits = 5;
	    shift = 3;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    pcrel = 1;
	    info->insn_type = dis_dref;
	    info->data_size = 8;
	    break;
	  case 'E':
	    nbits = 5;
	    shift = 2;
	    immed = (l >> MIPS16OP_SH_IMM5) & MIPS16OP_MASK_IMM5;
	    pcrel = 1;
	    break;
	  default:
	    abort ();
	  }

	if (! use_extend)
	  {
	    if (signedp && immed >= (1 << (nbits - 1)))
	      immed -= 1 << nbits;
	    immed <<= shift;
	    if ((type == '<' || type == '>' || type == '[' || type == ']')
		&& immed == 0)
	      immed = 8;
	  }
	else
	  {
	    if (extbits == 16)
	      immed |= ((extend & 0x1f) << 11) | (extend & 0x7e0);
	    else if (extbits == 15)
	      immed |= ((extend & 0xf) << 11) | (extend & 0x7f0);
	    else
	      immed = ((extend >> 6) & 0x1f) | (extend & 0x20);
	    immed &= (1 << extbits) - 1;
	    if (! extu && immed >= (1 << (extbits - 1)))
	      immed -= 1 << extbits;
	  }

	if (! pcrel)
	  (*info->fprintf_func) (info->stream, "%d", immed);
	else
	  {
	    bfd_vma baseaddr;

	    if (branch)
	      {
		immed *= 2;
		baseaddr = memaddr + 2;
	      }
	    else if (use_extend)
	      baseaddr = memaddr - 2;
	    else
	      {
		int status;
		bfd_byte buffer[2];

		baseaddr = memaddr;

		/* If this instruction is in the delay slot of a jr
                   instruction, the base address is the address of the
                   jr instruction.  If it is in the delay slot of jalr
                   instruction, the base address is the address of the
                   jalr instruction.  This test is unreliable: we have
                   no way of knowing whether the previous word is
                   instruction or data.  */
		status = (*info->read_memory_func) (memaddr - 4, buffer, 2,
						    info);
		if (status == 0
		    && (((info->endian == BFD_ENDIAN_BIG
			  ? bfd_getb16 (buffer)
			  : bfd_getl16 (buffer))
			 & 0xf800) == 0x1800))
		  baseaddr = memaddr - 4;
		else
		  {
		    status = (*info->read_memory_func) (memaddr - 2, buffer,
							2, info);
		    if (status == 0
			&& (((info->endian == BFD_ENDIAN_BIG
			      ? bfd_getb16 (buffer)
			      : bfd_getl16 (buffer))
			     & 0xf81f) == 0xe800))
		      baseaddr = memaddr - 2;
		  }
	      }
	    info->target = (baseaddr & ~((1 << shift) - 1)) + immed;
	    if (pcrel && branch
		&& info->flavour == bfd_target_unknown_flavour)
	      /* For gdb disassembler, maintain odd address.  */
	      info->target |= 1;
	    (*info->print_address_func) (info->target, info);
	  }
      }
      break;

    case 'a':
      {
	int jalx = l & 0x400;

	if (! use_extend)
	  extend = 0;
	l = ((l & 0x1f) << 23) | ((l & 0x3e0) << 13) | (extend << 2);
	if (!jalx && info->flavour == bfd_target_unknown_flavour)
	  /* For gdb disassembler, maintain odd address.  */
	  l |= 1;
      }
      info->target = ((memaddr + 4) & ~(bfd_vma) 0x0fffffff) | l;
      (*info->print_address_func) (info->target, info);
      info->insn_type = dis_jsr;
      info->branch_delay_insns = 1;
      break;

    case 'l':
    case 'L':
      {
	int need_comma, amask, smask;

	need_comma = 0;

	l = (l >> MIPS16OP_SH_IMM6) & MIPS16OP_MASK_IMM6;

	amask = (l >> 3) & 7;

	if (amask > 0 && amask < 5)
	  {
	    (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[4]);
	    if (amask > 1)
	      (*info->fprintf_func) (info->stream, "-%s",
				     mips_gpr_names[amask + 3]);
	    need_comma = 1;
	  }

	smask = (l >> 1) & 3;
	if (smask == 3)
	  {
	    (*info->fprintf_func) (info->stream, "%s??",
				   need_comma ? "," : "");
	    need_comma = 1;
	  }
	else if (smask > 0)
	  {
	    (*info->fprintf_func) (info->stream, "%s%s",
				   need_comma ? "," : "",
				   mips_gpr_names[16]);
	    if (smask > 1)
	      (*info->fprintf_func) (info->stream, "-%s",
				     mips_gpr_names[smask + 15]);
	    need_comma = 1;
	  }

	if (l & 1)
	  {
	    (*info->fprintf_func) (info->stream, "%s%s",
				   need_comma ? "," : "",
				   mips_gpr_names[31]);
	    need_comma = 1;
	  }

	if (amask == 5 || amask == 6)
	  {
	    (*info->fprintf_func) (info->stream, "%s$f0",
				   need_comma ? "," : "");
	    if (amask == 6)
	      (*info->fprintf_func) (info->stream, "-$f1");
	  }
      }
      break;

    case 'm':
    case 'M':
      /* MIPS16e save/restore.  */
      {
      int need_comma = 0;
      int amask, args, statics;
      int nsreg, smask;
      int framesz;
      int i, j;

      l = l & 0x7f;
      if (use_extend)
        l |= extend << 16;

      amask = (l >> 16) & 0xf;
      if (amask == MIPS16_ALL_ARGS)
        {
          args = 4;
          statics = 0;
        }
      else if (amask == MIPS16_ALL_STATICS)
        {
          args = 0;
          statics = 4;
        }
      else
        {
          args = amask >> 2;
          statics = amask & 3;
        }

      if (args > 0) {
          (*info->fprintf_func) (info->stream, "%s", mips_gpr_names[4]);
          if (args > 1)
            (*info->fprintf_func) (info->stream, "-%s",
                                   mips_gpr_names[4 + args - 1]);
          need_comma = 1;
      }

      framesz = (((l >> 16) & 0xf0) | (l & 0x0f)) * 8;
      if (framesz == 0 && !use_extend)
        framesz = 128;

      (*info->fprintf_func) (info->stream, "%s%d",
                             need_comma ? "," : "",
                             framesz);

      if (l & 0x40)                   /* $ra */
        (*info->fprintf_func) (info->stream, ",%s", mips_gpr_names[31]);

      nsreg = (l >> 24) & 0x7;
      smask = 0;
      if (l & 0x20)                   /* $s0 */
        smask |= 1 << 0;
      if (l & 0x10)                   /* $s1 */
        smask |= 1 << 1;
      if (nsreg > 0)                  /* $s2-$s8 */
        smask |= ((1 << nsreg) - 1) << 2;

      /* Find first set static reg bit.  */
      for (i = 0; i < 9; i++)
        {
          if (smask & (1 << i))
            {
              (*info->fprintf_func) (info->stream, ",%s",
                                     mips_gpr_names[i == 8 ? 30 : (16 + i)]);
              /* Skip over string of set bits.  */
              for (j = i; smask & (2 << j); j++)
                continue;
              if (j > i)
                (*info->fprintf_func) (info->stream, "-%s",
                                       mips_gpr_names[j == 8 ? 30 : (16 + j)]);
              i = j + 1;
            }
        }

      /* Statics $ax - $a3.  */
      if (statics == 1)
        (*info->fprintf_func) (info->stream, ",%s", mips_gpr_names[7]);
      else if (statics > 0)
        (*info->fprintf_func) (info->stream, ",%s-%s",
                               mips_gpr_names[7 - statics + 1],
                               mips_gpr_names[7]);
      }
      break;

    default:
      /* xgettext:c-format */
      (*info->fprintf_func)
	(info->stream,
	 _("# internal disassembler error, unrecognised modifier (%c)"),
	 type);
      abort ();
    }
}

void
print_mips_disassembler_options (FILE *stream)
{
  unsigned int i;

  fprintf (stream, _("\n\
The following MIPS specific disassembler options are supported for use\n\
with the -M switch (multiple options should be separated by commas):\n"));

  fprintf (stream, _("\n\
  gpr-names=ABI            Print GPR names according to  specified ABI.\n\
                           Default: based on binary being disassembled.\n"));

  fprintf (stream, _("\n\
  fpr-names=ABI            Print FPR names according to specified ABI.\n\
                           Default: numeric.\n"));

  fprintf (stream, _("\n\
  cp0-names=ARCH           Print CP0 register names according to\n\
                           specified architecture.\n\
                           Default: based on binary being disassembled.\n"));

  fprintf (stream, _("\n\
  hwr-names=ARCH           Print HWR names according to specified \n\
			   architecture.\n\
                           Default: based on binary being disassembled.\n"));

  fprintf (stream, _("\n\
  reg-names=ABI            Print GPR and FPR names according to\n\
                           specified ABI.\n"));

  fprintf (stream, _("\n\
  reg-names=ARCH           Print CP0 register and HWR names according to\n\
                           specified architecture.\n"));

  fprintf (stream, _("\n\
  For the options above, the following values are supported for \"ABI\":\n\
   "));
  for (i = 0; i < ARRAY_SIZE (mips_abi_choices); i++)
    fprintf (stream, " %s", mips_abi_choices[i].name);
  fprintf (stream, _("\n"));

  fprintf (stream, _("\n\
  For the options above, The following values are supported for \"ARCH\":\n\
   "));
  for (i = 0; i < ARRAY_SIZE (mips_arch_choices); i++)
    if (*mips_arch_choices[i].name != '\0')
      fprintf (stream, " %s", mips_arch_choices[i].name);
  fprintf (stream, _("\n"));

  fprintf (stream, _("\n"));
}
#endif
