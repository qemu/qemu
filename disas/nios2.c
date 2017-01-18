/* Nios II opcode library for QEMU.
   Copyright (C) 2012-2016 Free Software Foundation, Inc.
   Contributed by Nigel Gray (ngray@altera.com).
   Contributed by Mentor Graphics, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street, Fifth Floor,
   Boston, MA  02110-1301, USA.  */

/* This file resembles a concatenation of the following files from
   binutils:

   include/opcode/nios2.h
   include/opcode/nios2r1.h
   include/opcode/nios2r2.h
   opcodes/nios2-opc.c
   opcodes/nios2-dis.c

   It has been derived from the original patches which have been
   relicensed by the contributors as GPL version 2 for inclusion
   in QEMU.  */

#ifndef _NIOS2_H_
#define _NIOS2_H_

/*#include "bfd.h"*/
#include "qemu/osdep.h"
#include "disas/bfd.h"


/****************************************************************************
 * This file contains structures, bit masks and shift counts used
 * by the GNU toolchain to define the Nios II instruction set and
 * access various opcode fields.
 ****************************************************************************/

/* Instruction encoding formats.  */
enum iw_format_type {
  /* R1 formats.  */
  iw_i_type,
  iw_r_type,
  iw_j_type,
  iw_custom_type,

  /* 32-bit R2 formats.  */
  iw_L26_type,
  iw_F2I16_type,
  iw_F2X4I12_type,
  iw_F1X4I12_type,
  iw_F1X4L17_type,
  iw_F3X6L5_type,
  iw_F2X6L10_type,
  iw_F3X6_type,
  iw_F3X8_type,

  /* 16-bit R2 formats.  */
  iw_I10_type,
  iw_T1I7_type,
  iw_T2I4_type,
  iw_T1X1I6_type,
  iw_X1I7_type,
  iw_L5I4X1_type,
  iw_T2X1L3_type,
  iw_T2X1I3_type,
  iw_T3X1_type,
  iw_T2X3_type,
  iw_F1X1_type,
  iw_X2L5_type,
  iw_F1I5_type,
  iw_F2_type
};

/* Identify different overflow situations for error messages.  */
enum overflow_type
{
  call_target_overflow = 0,
  branch_target_overflow,
  address_offset_overflow,
  signed_immed16_overflow,
  unsigned_immed16_overflow,
  unsigned_immed5_overflow,
  signed_immed12_overflow,
  custom_opcode_overflow,
  enumeration_overflow,
  no_overflow
};

/* This structure holds information for a particular instruction. 

   The args field is a string describing the operands.  The following
   letters can appear in the args:
     c - a 5-bit control register index
     d - a 5-bit destination register index
     s - a 5-bit left source register index
     t - a 5-bit right source register index
     D - a 3-bit encoded destination register
     S - a 3-bit encoded left source register
     T - a 3-bit encoded right source register
     i - a 16-bit signed immediate
     j - a 5-bit unsigned immediate
     k - a (second) 5-bit unsigned immediate
     l - a 8-bit custom instruction constant
     m - a 26-bit unsigned immediate
     o - a 16-bit signed pc-relative offset
     u - a 16-bit unsigned immediate
     I - a 12-bit signed immediate
     M - a 6-bit unsigned immediate
     N - a 6-bit unsigned immediate with 2-bit shift
     O - a 10-bit signed pc-relative offset with 1-bit shift
     P - a 7-bit signed pc-relative offset with 1-bit shift
     U - a 7-bit unsigned immediate with 2-bit shift
     V - a 5-bit unsigned immediate with 2-bit shift
     W - a 4-bit unsigned immediate with 2-bit shift
     X - a 4-bit unsigned immediate with 1-bit shift
     Y - a 4-bit unsigned immediate
     e - an immediate coded as an enumeration for addi.n/subi.n
     f - an immediate coded as an enumeration for slli.n/srli.n
     g - an immediate coded as an enumeration for andi.n
     h - an immediate coded as an enumeration for movi.n
     R - a reglist for ldwm/stwm or push.n/pop.n
     B - a base register specifier and option list for ldwm/stwm
   Literal ',', '(', and ')' characters may also appear in the args as
   delimiters.

   Note that the args describe the semantics and assembly-language syntax
   of the operands, not their encoding into the instruction word.

   The pinfo field is INSN_MACRO for a macro.  Otherwise, it is a collection
   of bits describing the instruction, notably any relevant hazard
   information.

   When assembling, the match field contains the opcode template, which
   is modified by the arguments to produce the actual opcode
   that is emitted.  If pinfo is INSN_MACRO, then this is 0.

   If pinfo is INSN_MACRO, the mask field stores the macro identifier.
   Otherwise this is a bit mask for the relevant portions of the opcode
   when disassembling.  If the actual opcode anded with the match field
   equals the opcode field, then we have found the correct instruction.  */

struct nios2_opcode
{
  const char *name;		/* The name of the instruction.  */
  const char *args;		/* A string describing the arguments for this 
				   instruction.  */
  const char *args_test;	/* Like args, but with an extra argument for 
				   the expected opcode.  */
  unsigned long num_args;	/* The number of arguments the instruction 
				   takes.  */
  unsigned size;		/* Size in bytes of the instruction.  */
  enum iw_format_type format;	/* Instruction format.  */
  unsigned long match;		/* The basic opcode for the instruction.  */
  unsigned long mask;		/* Mask for the opcode field of the 
				   instruction.  */
  unsigned long pinfo;		/* Is this a real instruction or instruction 
				   macro?  */
  enum overflow_type overflow_msg;  /* Used to generate informative 
				       message when fixup overflows.  */
};

/* This value is used in the nios2_opcode.pinfo field to indicate that the 
   instruction is a macro or pseudo-op.  This requires special treatment by 
   the assembler, and is used by the disassembler to determine whether to 
   check for a nop.  */
#define NIOS2_INSN_MACRO	0x80000000
#define NIOS2_INSN_MACRO_MOV	0x80000001
#define NIOS2_INSN_MACRO_MOVI	0x80000002
#define NIOS2_INSN_MACRO_MOVIA	0x80000004

#define NIOS2_INSN_RELAXABLE	0x40000000
#define NIOS2_INSN_UBRANCH	0x00000010
#define NIOS2_INSN_CBRANCH	0x00000020
#define NIOS2_INSN_CALL		0x00000040

#define NIOS2_INSN_OPTARG	0x00000080

/* Register attributes.  */
#define REG_NORMAL	(1<<0)	/* Normal registers.  */
#define REG_CONTROL	(1<<1)  /* Control registers.  */
#define REG_COPROCESSOR	(1<<2)  /* For custom instructions.  */
#define REG_3BIT	(1<<3)  /* For R2 CDX instructions.  */
#define REG_LDWM	(1<<4)  /* For R2 ldwm/stwm.  */
#define REG_POP		(1<<5)  /* For R2 pop.n/push.n.  */

struct nios2_reg
{
  const char *name;
  const int index;
  unsigned long regtype;
};

/* Pull in the instruction field accessors, opcodes, and masks.  */
/*#include "nios2r1.h"*/

#ifndef _NIOS2R1_H_
#define _NIOS2R1_H_

/* R1 fields.  */
#define IW_R1_OP_LSB 0 
#define IW_R1_OP_SIZE 6 
#define IW_R1_OP_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R1_OP_SIZE)) 
#define IW_R1_OP_SHIFTED_MASK (IW_R1_OP_UNSHIFTED_MASK << IW_R1_OP_LSB) 
#define GET_IW_R1_OP(W) (((W) >> IW_R1_OP_LSB) & IW_R1_OP_UNSHIFTED_MASK) 
#define SET_IW_R1_OP(V) (((V) & IW_R1_OP_UNSHIFTED_MASK) << IW_R1_OP_LSB) 

#define IW_I_A_LSB 27 
#define IW_I_A_SIZE 5 
#define IW_I_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_I_A_SIZE)) 
#define IW_I_A_SHIFTED_MASK (IW_I_A_UNSHIFTED_MASK << IW_I_A_LSB) 
#define GET_IW_I_A(W) (((W) >> IW_I_A_LSB) & IW_I_A_UNSHIFTED_MASK) 
#define SET_IW_I_A(V) (((V) & IW_I_A_UNSHIFTED_MASK) << IW_I_A_LSB) 

#define IW_I_B_LSB 22 
#define IW_I_B_SIZE 5 
#define IW_I_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_I_B_SIZE)) 
#define IW_I_B_SHIFTED_MASK (IW_I_B_UNSHIFTED_MASK << IW_I_B_LSB) 
#define GET_IW_I_B(W) (((W) >> IW_I_B_LSB) & IW_I_B_UNSHIFTED_MASK) 
#define SET_IW_I_B(V) (((V) & IW_I_B_UNSHIFTED_MASK) << IW_I_B_LSB) 

#define IW_I_IMM16_LSB 6 
#define IW_I_IMM16_SIZE 16 
#define IW_I_IMM16_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_I_IMM16_SIZE)) 
#define IW_I_IMM16_SHIFTED_MASK (IW_I_IMM16_UNSHIFTED_MASK << IW_I_IMM16_LSB) 
#define GET_IW_I_IMM16(W) (((W) >> IW_I_IMM16_LSB) & IW_I_IMM16_UNSHIFTED_MASK) 
#define SET_IW_I_IMM16(V) (((V) & IW_I_IMM16_UNSHIFTED_MASK) << IW_I_IMM16_LSB) 

#define IW_R_A_LSB 27 
#define IW_R_A_SIZE 5 
#define IW_R_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R_A_SIZE)) 
#define IW_R_A_SHIFTED_MASK (IW_R_A_UNSHIFTED_MASK << IW_R_A_LSB) 
#define GET_IW_R_A(W) (((W) >> IW_R_A_LSB) & IW_R_A_UNSHIFTED_MASK) 
#define SET_IW_R_A(V) (((V) & IW_R_A_UNSHIFTED_MASK) << IW_R_A_LSB) 

#define IW_R_B_LSB 22 
#define IW_R_B_SIZE 5 
#define IW_R_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R_B_SIZE)) 
#define IW_R_B_SHIFTED_MASK (IW_R_B_UNSHIFTED_MASK << IW_R_B_LSB) 
#define GET_IW_R_B(W) (((W) >> IW_R_B_LSB) & IW_R_B_UNSHIFTED_MASK) 
#define SET_IW_R_B(V) (((V) & IW_R_B_UNSHIFTED_MASK) << IW_R_B_LSB) 

#define IW_R_C_LSB 17 
#define IW_R_C_SIZE 5 
#define IW_R_C_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R_C_SIZE)) 
#define IW_R_C_SHIFTED_MASK (IW_R_C_UNSHIFTED_MASK << IW_R_C_LSB) 
#define GET_IW_R_C(W) (((W) >> IW_R_C_LSB) & IW_R_C_UNSHIFTED_MASK) 
#define SET_IW_R_C(V) (((V) & IW_R_C_UNSHIFTED_MASK) << IW_R_C_LSB) 

#define IW_R_OPX_LSB 11 
#define IW_R_OPX_SIZE 6 
#define IW_R_OPX_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R_OPX_SIZE)) 
#define IW_R_OPX_SHIFTED_MASK (IW_R_OPX_UNSHIFTED_MASK << IW_R_OPX_LSB) 
#define GET_IW_R_OPX(W) (((W) >> IW_R_OPX_LSB) & IW_R_OPX_UNSHIFTED_MASK) 
#define SET_IW_R_OPX(V) (((V) & IW_R_OPX_UNSHIFTED_MASK) << IW_R_OPX_LSB) 

#define IW_R_IMM5_LSB 6 
#define IW_R_IMM5_SIZE 5 
#define IW_R_IMM5_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R_IMM5_SIZE)) 
#define IW_R_IMM5_SHIFTED_MASK (IW_R_IMM5_UNSHIFTED_MASK << IW_R_IMM5_LSB) 
#define GET_IW_R_IMM5(W) (((W) >> IW_R_IMM5_LSB) & IW_R_IMM5_UNSHIFTED_MASK) 
#define SET_IW_R_IMM5(V) (((V) & IW_R_IMM5_UNSHIFTED_MASK) << IW_R_IMM5_LSB) 

#define IW_J_IMM26_LSB 6 
#define IW_J_IMM26_SIZE 26 
#define IW_J_IMM26_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_J_IMM26_SIZE)) 
#define IW_J_IMM26_SHIFTED_MASK (IW_J_IMM26_UNSHIFTED_MASK << IW_J_IMM26_LSB) 
#define GET_IW_J_IMM26(W) (((W) >> IW_J_IMM26_LSB) & IW_J_IMM26_UNSHIFTED_MASK) 
#define SET_IW_J_IMM26(V) (((V) & IW_J_IMM26_UNSHIFTED_MASK) << IW_J_IMM26_LSB) 

#define IW_CUSTOM_A_LSB 27 
#define IW_CUSTOM_A_SIZE 5 
#define IW_CUSTOM_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_A_SIZE)) 
#define IW_CUSTOM_A_SHIFTED_MASK (IW_CUSTOM_A_UNSHIFTED_MASK << IW_CUSTOM_A_LSB) 
#define GET_IW_CUSTOM_A(W) (((W) >> IW_CUSTOM_A_LSB) & IW_CUSTOM_A_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_A(V) (((V) & IW_CUSTOM_A_UNSHIFTED_MASK) << IW_CUSTOM_A_LSB) 

#define IW_CUSTOM_B_LSB 22 
#define IW_CUSTOM_B_SIZE 5 
#define IW_CUSTOM_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_B_SIZE)) 
#define IW_CUSTOM_B_SHIFTED_MASK (IW_CUSTOM_B_UNSHIFTED_MASK << IW_CUSTOM_B_LSB) 
#define GET_IW_CUSTOM_B(W) (((W) >> IW_CUSTOM_B_LSB) & IW_CUSTOM_B_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_B(V) (((V) & IW_CUSTOM_B_UNSHIFTED_MASK) << IW_CUSTOM_B_LSB) 

#define IW_CUSTOM_C_LSB 17 
#define IW_CUSTOM_C_SIZE 5 
#define IW_CUSTOM_C_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_C_SIZE)) 
#define IW_CUSTOM_C_SHIFTED_MASK (IW_CUSTOM_C_UNSHIFTED_MASK << IW_CUSTOM_C_LSB) 
#define GET_IW_CUSTOM_C(W) (((W) >> IW_CUSTOM_C_LSB) & IW_CUSTOM_C_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_C(V) (((V) & IW_CUSTOM_C_UNSHIFTED_MASK) << IW_CUSTOM_C_LSB) 

#define IW_CUSTOM_READA_LSB 16 
#define IW_CUSTOM_READA_SIZE 1 
#define IW_CUSTOM_READA_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_READA_SIZE)) 
#define IW_CUSTOM_READA_SHIFTED_MASK (IW_CUSTOM_READA_UNSHIFTED_MASK << IW_CUSTOM_READA_LSB) 
#define GET_IW_CUSTOM_READA(W) (((W) >> IW_CUSTOM_READA_LSB) & IW_CUSTOM_READA_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_READA(V) (((V) & IW_CUSTOM_READA_UNSHIFTED_MASK) << IW_CUSTOM_READA_LSB) 

#define IW_CUSTOM_READB_LSB 15 
#define IW_CUSTOM_READB_SIZE 1 
#define IW_CUSTOM_READB_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_READB_SIZE)) 
#define IW_CUSTOM_READB_SHIFTED_MASK (IW_CUSTOM_READB_UNSHIFTED_MASK << IW_CUSTOM_READB_LSB) 
#define GET_IW_CUSTOM_READB(W) (((W) >> IW_CUSTOM_READB_LSB) & IW_CUSTOM_READB_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_READB(V) (((V) & IW_CUSTOM_READB_UNSHIFTED_MASK) << IW_CUSTOM_READB_LSB) 

#define IW_CUSTOM_READC_LSB 14 
#define IW_CUSTOM_READC_SIZE 1 
#define IW_CUSTOM_READC_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_READC_SIZE)) 
#define IW_CUSTOM_READC_SHIFTED_MASK (IW_CUSTOM_READC_UNSHIFTED_MASK << IW_CUSTOM_READC_LSB) 
#define GET_IW_CUSTOM_READC(W) (((W) >> IW_CUSTOM_READC_LSB) & IW_CUSTOM_READC_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_READC(V) (((V) & IW_CUSTOM_READC_UNSHIFTED_MASK) << IW_CUSTOM_READC_LSB) 

#define IW_CUSTOM_N_LSB 6 
#define IW_CUSTOM_N_SIZE 8 
#define IW_CUSTOM_N_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_CUSTOM_N_SIZE)) 
#define IW_CUSTOM_N_SHIFTED_MASK (IW_CUSTOM_N_UNSHIFTED_MASK << IW_CUSTOM_N_LSB) 
#define GET_IW_CUSTOM_N(W) (((W) >> IW_CUSTOM_N_LSB) & IW_CUSTOM_N_UNSHIFTED_MASK) 
#define SET_IW_CUSTOM_N(V) (((V) & IW_CUSTOM_N_UNSHIFTED_MASK) << IW_CUSTOM_N_LSB) 

/* R1 opcodes.  */
#define R1_OP_CALL 0
#define R1_OP_JMPI 1
#define R1_OP_LDBU 3
#define R1_OP_ADDI 4
#define R1_OP_STB 5
#define R1_OP_BR 6
#define R1_OP_LDB 7
#define R1_OP_CMPGEI 8
#define R1_OP_LDHU 11
#define R1_OP_ANDI 12
#define R1_OP_STH 13
#define R1_OP_BGE 14
#define R1_OP_LDH 15
#define R1_OP_CMPLTI 16
#define R1_OP_INITDA 19
#define R1_OP_ORI 20
#define R1_OP_STW 21
#define R1_OP_BLT 22
#define R1_OP_LDW 23
#define R1_OP_CMPNEI 24
#define R1_OP_FLUSHDA 27
#define R1_OP_XORI 28
#define R1_OP_BNE 30
#define R1_OP_CMPEQI 32
#define R1_OP_LDBUIO 35
#define R1_OP_MULI 36
#define R1_OP_STBIO 37
#define R1_OP_BEQ 38
#define R1_OP_LDBIO 39
#define R1_OP_CMPGEUI 40
#define R1_OP_LDHUIO 43
#define R1_OP_ANDHI 44
#define R1_OP_STHIO 45
#define R1_OP_BGEU 46
#define R1_OP_LDHIO 47
#define R1_OP_CMPLTUI 48
#define R1_OP_CUSTOM 50
#define R1_OP_INITD 51
#define R1_OP_ORHI 52
#define R1_OP_STWIO 53
#define R1_OP_BLTU 54
#define R1_OP_LDWIO 55
#define R1_OP_RDPRS 56
#define R1_OP_OPX 58
#define R1_OP_FLUSHD 59
#define R1_OP_XORHI 60

#define R1_OPX_ERET 1
#define R1_OPX_ROLI 2
#define R1_OPX_ROL 3
#define R1_OPX_FLUSHP 4
#define R1_OPX_RET 5
#define R1_OPX_NOR 6
#define R1_OPX_MULXUU 7
#define R1_OPX_CMPGE 8
#define R1_OPX_BRET 9
#define R1_OPX_ROR 11
#define R1_OPX_FLUSHI 12
#define R1_OPX_JMP 13
#define R1_OPX_AND 14
#define R1_OPX_CMPLT 16
#define R1_OPX_SLLI 18
#define R1_OPX_SLL 19
#define R1_OPX_WRPRS 20
#define R1_OPX_OR 22
#define R1_OPX_MULXSU 23
#define R1_OPX_CMPNE 24
#define R1_OPX_SRLI 26
#define R1_OPX_SRL 27
#define R1_OPX_NEXTPC 28
#define R1_OPX_CALLR 29
#define R1_OPX_XOR 30
#define R1_OPX_MULXSS 31
#define R1_OPX_CMPEQ 32
#define R1_OPX_DIVU 36
#define R1_OPX_DIV 37
#define R1_OPX_RDCTL 38
#define R1_OPX_MUL 39
#define R1_OPX_CMPGEU 40
#define R1_OPX_INITI 41
#define R1_OPX_TRAP 45
#define R1_OPX_WRCTL 46
#define R1_OPX_CMPLTU 48
#define R1_OPX_ADD 49
#define R1_OPX_BREAK 52
#define R1_OPX_SYNC 54
#define R1_OPX_SUB 57
#define R1_OPX_SRAI 58
#define R1_OPX_SRA 59

/* Some convenience macros for R1 encodings, for use in instruction tables.
   MATCH_R1_OPX0(NAME) and MASK_R1_OPX0 are used for R-type instructions
   with 3 register operands and constant 0 in the immediate field.
   The general forms are MATCH_R1_OPX(NAME, A, B, C) where the arguments specify
   constant values and MASK_R1_OPX(A, B, C, N) where the arguments are booleans
   that are true if the field should be included in the mask.
 */
#define MATCH_R1_OP(NAME) \
  (SET_IW_R1_OP (R1_OP_##NAME))
#define MASK_R1_OP \
  IW_R1_OP_SHIFTED_MASK

#define MATCH_R1_OPX0(NAME) \
  (SET_IW_R1_OP (R1_OP_OPX) | SET_IW_R_OPX (R1_OPX_##NAME))
#define MASK_R1_OPX0 \
  (IW_R1_OP_SHIFTED_MASK | IW_R_OPX_SHIFTED_MASK | IW_R_IMM5_SHIFTED_MASK)

#define MATCH_R1_OPX(NAME, A, B, C)				\
  (MATCH_R1_OPX0 (NAME) | SET_IW_R_A (A) | SET_IW_R_B (B) | SET_IW_R_C (C))
#define MASK_R1_OPX(A, B, C, N)				\
  (IW_R1_OP_SHIFTED_MASK | IW_R_OPX_SHIFTED_MASK	\
   | (A ? IW_R_A_SHIFTED_MASK : 0)			\
   | (B ? IW_R_B_SHIFTED_MASK : 0)			\
   | (C ? IW_R_C_SHIFTED_MASK : 0)			\
   | (N ? IW_R_IMM5_SHIFTED_MASK : 0))

/* And here's the match/mask macros for the R1 instruction set.  */
#define MATCH_R1_ADD	MATCH_R1_OPX0 (ADD)
#define MASK_R1_ADD	MASK_R1_OPX0
#define MATCH_R1_ADDI	MATCH_R1_OP (ADDI)
#define MASK_R1_ADDI	MASK_R1_OP
#define MATCH_R1_AND	MATCH_R1_OPX0 (AND)
#define MASK_R1_AND	MASK_R1_OPX0
#define MATCH_R1_ANDHI	MATCH_R1_OP (ANDHI)
#define MASK_R1_ANDHI	MASK_R1_OP
#define MATCH_R1_ANDI	MATCH_R1_OP (ANDI)
#define MASK_R1_ANDI	MASK_R1_OP
#define MATCH_R1_BEQ	MATCH_R1_OP (BEQ)
#define MASK_R1_BEQ	MASK_R1_OP
#define MATCH_R1_BGE	MATCH_R1_OP (BGE)
#define MASK_R1_BGE	MASK_R1_OP
#define MATCH_R1_BGEU	MATCH_R1_OP (BGEU)
#define MASK_R1_BGEU	MASK_R1_OP
#define MATCH_R1_BGT	MATCH_R1_OP (BLT)
#define MASK_R1_BGT	MASK_R1_OP
#define MATCH_R1_BGTU	MATCH_R1_OP (BLTU)
#define MASK_R1_BGTU	MASK_R1_OP
#define MATCH_R1_BLE	MATCH_R1_OP (BGE)
#define MASK_R1_BLE	MASK_R1_OP
#define MATCH_R1_BLEU	MATCH_R1_OP (BGEU)
#define MASK_R1_BLEU	MASK_R1_OP
#define MATCH_R1_BLT	MATCH_R1_OP (BLT)
#define MASK_R1_BLT	MASK_R1_OP
#define MATCH_R1_BLTU	MATCH_R1_OP (BLTU)
#define MASK_R1_BLTU	MASK_R1_OP
#define MATCH_R1_BNE	MATCH_R1_OP (BNE)
#define MASK_R1_BNE	MASK_R1_OP
#define MATCH_R1_BR	MATCH_R1_OP (BR)
#define MASK_R1_BR	MASK_R1_OP | IW_I_A_SHIFTED_MASK | IW_I_B_SHIFTED_MASK
#define MATCH_R1_BREAK	MATCH_R1_OPX (BREAK, 0, 0, 0x1e)
#define MASK_R1_BREAK	MASK_R1_OPX (1, 1, 1, 0)
#define MATCH_R1_BRET	MATCH_R1_OPX (BRET, 0x1e, 0, 0)
#define MASK_R1_BRET	MASK_R1_OPX (1, 1, 1, 1)
#define MATCH_R1_CALL	MATCH_R1_OP (CALL)
#define MASK_R1_CALL	MASK_R1_OP
#define MATCH_R1_CALLR	MATCH_R1_OPX (CALLR, 0, 0, 0x1f)
#define MASK_R1_CALLR	MASK_R1_OPX (0, 1, 1, 1)
#define MATCH_R1_CMPEQ	MATCH_R1_OPX0 (CMPEQ)
#define MASK_R1_CMPEQ	MASK_R1_OPX0
#define MATCH_R1_CMPEQI	MATCH_R1_OP (CMPEQI)
#define MASK_R1_CMPEQI	MASK_R1_OP
#define MATCH_R1_CMPGE	MATCH_R1_OPX0 (CMPGE)
#define MASK_R1_CMPGE	MASK_R1_OPX0
#define MATCH_R1_CMPGEI	MATCH_R1_OP (CMPGEI)
#define MASK_R1_CMPGEI	MASK_R1_OP
#define MATCH_R1_CMPGEU	MATCH_R1_OPX0 (CMPGEU)
#define MASK_R1_CMPGEU	MASK_R1_OPX0
#define MATCH_R1_CMPGEUI	MATCH_R1_OP (CMPGEUI)
#define MASK_R1_CMPGEUI	MASK_R1_OP
#define MATCH_R1_CMPGT	MATCH_R1_OPX0 (CMPLT)
#define MASK_R1_CMPGT	MASK_R1_OPX0
#define MATCH_R1_CMPGTI	MATCH_R1_OP (CMPGEI)
#define MASK_R1_CMPGTI	MASK_R1_OP
#define MATCH_R1_CMPGTU	MATCH_R1_OPX0 (CMPLTU)
#define MASK_R1_CMPGTU	MASK_R1_OPX0
#define MATCH_R1_CMPGTUI	MATCH_R1_OP (CMPGEUI)
#define MASK_R1_CMPGTUI	MASK_R1_OP
#define MATCH_R1_CMPLE	MATCH_R1_OPX0 (CMPGE)
#define MASK_R1_CMPLE	MASK_R1_OPX0
#define MATCH_R1_CMPLEI	MATCH_R1_OP (CMPLTI)
#define MASK_R1_CMPLEI	MASK_R1_OP
#define MATCH_R1_CMPLEU	MATCH_R1_OPX0 (CMPGEU)
#define MASK_R1_CMPLEU	MASK_R1_OPX0
#define MATCH_R1_CMPLEUI	MATCH_R1_OP (CMPLTUI)
#define MASK_R1_CMPLEUI	MASK_R1_OP
#define MATCH_R1_CMPLT	MATCH_R1_OPX0 (CMPLT)
#define MASK_R1_CMPLT	MASK_R1_OPX0
#define MATCH_R1_CMPLTI	MATCH_R1_OP (CMPLTI)
#define MASK_R1_CMPLTI	MASK_R1_OP
#define MATCH_R1_CMPLTU	MATCH_R1_OPX0 (CMPLTU)
#define MASK_R1_CMPLTU	MASK_R1_OPX0
#define MATCH_R1_CMPLTUI	MATCH_R1_OP (CMPLTUI)
#define MASK_R1_CMPLTUI	MASK_R1_OP
#define MATCH_R1_CMPNE	MATCH_R1_OPX0 (CMPNE)
#define MASK_R1_CMPNE	MASK_R1_OPX0
#define MATCH_R1_CMPNEI	MATCH_R1_OP (CMPNEI)
#define MASK_R1_CMPNEI	MASK_R1_OP
#define MATCH_R1_CUSTOM	MATCH_R1_OP (CUSTOM)
#define MASK_R1_CUSTOM	MASK_R1_OP
#define MATCH_R1_DIV	MATCH_R1_OPX0 (DIV)
#define MASK_R1_DIV	MASK_R1_OPX0
#define MATCH_R1_DIVU	MATCH_R1_OPX0 (DIVU)
#define MASK_R1_DIVU	MASK_R1_OPX0
#define MATCH_R1_ERET	MATCH_R1_OPX (ERET, 0x1d, 0x1e, 0)
#define MASK_R1_ERET	MASK_R1_OPX (1, 1, 1, 1)
#define MATCH_R1_FLUSHD	MATCH_R1_OP (FLUSHD) | SET_IW_I_B (0)
#define MASK_R1_FLUSHD	MASK_R1_OP | IW_I_B_SHIFTED_MASK
#define MATCH_R1_FLUSHDA	MATCH_R1_OP (FLUSHDA) | SET_IW_I_B (0)
#define MASK_R1_FLUSHDA	MASK_R1_OP | IW_I_B_SHIFTED_MASK
#define MATCH_R1_FLUSHI	MATCH_R1_OPX (FLUSHI, 0, 0, 0)
#define MASK_R1_FLUSHI	MASK_R1_OPX (0, 1, 1, 1)
#define MATCH_R1_FLUSHP	MATCH_R1_OPX (FLUSHP, 0, 0, 0)
#define MASK_R1_FLUSHP	MASK_R1_OPX (1, 1, 1, 1)
#define MATCH_R1_INITD	MATCH_R1_OP (INITD) | SET_IW_I_B (0)
#define MASK_R1_INITD	MASK_R1_OP | IW_I_B_SHIFTED_MASK
#define MATCH_R1_INITDA	MATCH_R1_OP (INITDA) | SET_IW_I_B (0)
#define MASK_R1_INITDA	MASK_R1_OP | IW_I_B_SHIFTED_MASK
#define MATCH_R1_INITI	MATCH_R1_OPX (INITI, 0, 0, 0)
#define MASK_R1_INITI	MASK_R1_OPX (0, 1, 1, 1)
#define MATCH_R1_JMP	MATCH_R1_OPX (JMP, 0, 0, 0)
#define MASK_R1_JMP	MASK_R1_OPX (0, 1, 1, 1)
#define MATCH_R1_JMPI	MATCH_R1_OP (JMPI)
#define MASK_R1_JMPI	MASK_R1_OP
#define MATCH_R1_LDB	MATCH_R1_OP (LDB)
#define MASK_R1_LDB	MASK_R1_OP
#define MATCH_R1_LDBIO	MATCH_R1_OP (LDBIO)
#define MASK_R1_LDBIO	MASK_R1_OP
#define MATCH_R1_LDBU	MATCH_R1_OP (LDBU)
#define MASK_R1_LDBU	MASK_R1_OP
#define MATCH_R1_LDBUIO	MATCH_R1_OP (LDBUIO)
#define MASK_R1_LDBUIO	MASK_R1_OP
#define MATCH_R1_LDH	MATCH_R1_OP (LDH)
#define MASK_R1_LDH	MASK_R1_OP
#define MATCH_R1_LDHIO	MATCH_R1_OP (LDHIO)
#define MASK_R1_LDHIO	MASK_R1_OP
#define MATCH_R1_LDHU	MATCH_R1_OP (LDHU)
#define MASK_R1_LDHU	MASK_R1_OP
#define MATCH_R1_LDHUIO	MATCH_R1_OP (LDHUIO)
#define MASK_R1_LDHUIO	MASK_R1_OP
#define MATCH_R1_LDW	MATCH_R1_OP (LDW)
#define MASK_R1_LDW	MASK_R1_OP
#define MATCH_R1_LDWIO	MATCH_R1_OP (LDWIO)
#define MASK_R1_LDWIO	MASK_R1_OP
#define MATCH_R1_MOV	MATCH_R1_OPX (ADD, 0, 0, 0)
#define MASK_R1_MOV	MASK_R1_OPX (0, 1, 0, 1)
#define MATCH_R1_MOVHI	MATCH_R1_OP (ORHI) | SET_IW_I_A (0)
#define MASK_R1_MOVHI	MASK_R1_OP | IW_I_A_SHIFTED_MASK
#define MATCH_R1_MOVI	MATCH_R1_OP (ADDI) | SET_IW_I_A (0)
#define MASK_R1_MOVI	MASK_R1_OP | IW_I_A_SHIFTED_MASK
#define MATCH_R1_MOVUI	MATCH_R1_OP (ORI) | SET_IW_I_A (0)
#define MASK_R1_MOVUI	MASK_R1_OP | IW_I_A_SHIFTED_MASK
#define MATCH_R1_MUL	MATCH_R1_OPX0 (MUL)
#define MASK_R1_MUL	MASK_R1_OPX0
#define MATCH_R1_MULI	MATCH_R1_OP (MULI)
#define MASK_R1_MULI	MASK_R1_OP
#define MATCH_R1_MULXSS	MATCH_R1_OPX0 (MULXSS)
#define MASK_R1_MULXSS	MASK_R1_OPX0
#define MATCH_R1_MULXSU	MATCH_R1_OPX0 (MULXSU)
#define MASK_R1_MULXSU	MASK_R1_OPX0
#define MATCH_R1_MULXUU	MATCH_R1_OPX0 (MULXUU)
#define MASK_R1_MULXUU	MASK_R1_OPX0
#define MATCH_R1_NEXTPC	MATCH_R1_OPX (NEXTPC, 0, 0, 0)
#define MASK_R1_NEXTPC	MASK_R1_OPX (1, 1, 0, 1)
#define MATCH_R1_NOP	MATCH_R1_OPX (ADD, 0, 0, 0)
#define MASK_R1_NOP	MASK_R1_OPX (1, 1, 1, 1)
#define MATCH_R1_NOR	MATCH_R1_OPX0 (NOR)
#define MASK_R1_NOR	MASK_R1_OPX0
#define MATCH_R1_OR	MATCH_R1_OPX0 (OR)
#define MASK_R1_OR	MASK_R1_OPX0
#define MATCH_R1_ORHI	MATCH_R1_OP (ORHI)
#define MASK_R1_ORHI	MASK_R1_OP
#define MATCH_R1_ORI	MATCH_R1_OP (ORI)
#define MASK_R1_ORI	MASK_R1_OP
#define MATCH_R1_RDCTL	MATCH_R1_OPX (RDCTL, 0, 0, 0)
#define MASK_R1_RDCTL	MASK_R1_OPX (1, 1, 0, 0)
#define MATCH_R1_RDPRS	MATCH_R1_OP (RDPRS)
#define MASK_R1_RDPRS	MASK_R1_OP
#define MATCH_R1_RET	MATCH_R1_OPX (RET, 0x1f, 0, 0)
#define MASK_R1_RET	MASK_R1_OPX (1, 1, 1, 1)
#define MATCH_R1_ROL	MATCH_R1_OPX0 (ROL)
#define MASK_R1_ROL	MASK_R1_OPX0
#define MATCH_R1_ROLI	MATCH_R1_OPX (ROLI, 0, 0, 0)
#define MASK_R1_ROLI	MASK_R1_OPX (0, 1, 0, 0)
#define MATCH_R1_ROR	MATCH_R1_OPX0 (ROR)
#define MASK_R1_ROR	MASK_R1_OPX0
#define MATCH_R1_SLL	MATCH_R1_OPX0 (SLL)
#define MASK_R1_SLL	MASK_R1_OPX0
#define MATCH_R1_SLLI	MATCH_R1_OPX (SLLI, 0, 0, 0)
#define MASK_R1_SLLI	MASK_R1_OPX (0, 1, 0, 0)
#define MATCH_R1_SRA	MATCH_R1_OPX0 (SRA)
#define MASK_R1_SRA	MASK_R1_OPX0
#define MATCH_R1_SRAI	MATCH_R1_OPX (SRAI, 0, 0, 0)
#define MASK_R1_SRAI	MASK_R1_OPX (0, 1, 0, 0)
#define MATCH_R1_SRL	MATCH_R1_OPX0 (SRL)
#define MASK_R1_SRL	MASK_R1_OPX0
#define MATCH_R1_SRLI	MATCH_R1_OPX (SRLI, 0, 0, 0)
#define MASK_R1_SRLI	MASK_R1_OPX (0, 1, 0, 0)
#define MATCH_R1_STB	MATCH_R1_OP (STB)
#define MASK_R1_STB	MASK_R1_OP
#define MATCH_R1_STBIO	MATCH_R1_OP (STBIO)
#define MASK_R1_STBIO	MASK_R1_OP
#define MATCH_R1_STH	MATCH_R1_OP (STH)
#define MASK_R1_STH	MASK_R1_OP
#define MATCH_R1_STHIO	MATCH_R1_OP (STHIO)
#define MASK_R1_STHIO	MASK_R1_OP
#define MATCH_R1_STW	MATCH_R1_OP (STW)
#define MASK_R1_STW	MASK_R1_OP
#define MATCH_R1_STWIO	MATCH_R1_OP (STWIO)
#define MASK_R1_STWIO	MASK_R1_OP
#define MATCH_R1_SUB	MATCH_R1_OPX0 (SUB)
#define MASK_R1_SUB	MASK_R1_OPX0
#define MATCH_R1_SUBI	MATCH_R1_OP (ADDI)
#define MASK_R1_SUBI	MASK_R1_OP
#define MATCH_R1_SYNC	MATCH_R1_OPX (SYNC, 0, 0, 0)
#define MASK_R1_SYNC	MASK_R1_OPX (1, 1, 1, 1)
#define MATCH_R1_TRAP	MATCH_R1_OPX (TRAP, 0, 0, 0x1d)
#define MASK_R1_TRAP	MASK_R1_OPX (1, 1, 1, 0)
#define MATCH_R1_WRCTL	MATCH_R1_OPX (WRCTL, 0, 0, 0)
#define MASK_R1_WRCTL	MASK_R1_OPX (0, 1, 1, 0)
#define MATCH_R1_WRPRS	MATCH_R1_OPX (WRPRS, 0, 0, 0)
#define MASK_R1_WRPRS	MASK_R1_OPX (0, 1, 0, 1)
#define MATCH_R1_XOR	MATCH_R1_OPX0 (XOR)
#define MASK_R1_XOR	MASK_R1_OPX0
#define MATCH_R1_XORHI	MATCH_R1_OP (XORHI)
#define MASK_R1_XORHI	MASK_R1_OP
#define MATCH_R1_XORI	MATCH_R1_OP (XORI)
#define MASK_R1_XORI	MASK_R1_OP

#endif /* _NIOS2R1_H */

/*#include "nios2r2.h"*/

#ifndef _NIOS2R2_H_
#define _NIOS2R2_H_

/* Fields for 32-bit R2 instructions.  */

#define IW_R2_OP_LSB 0
#define IW_R2_OP_SIZE 6
#define IW_R2_OP_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R2_OP_SIZE))
#define IW_R2_OP_SHIFTED_MASK (IW_R2_OP_UNSHIFTED_MASK << IW_R2_OP_LSB)
#define GET_IW_R2_OP(W) (((W) >> IW_R2_OP_LSB) & IW_R2_OP_UNSHIFTED_MASK)
#define SET_IW_R2_OP(V) (((V) & IW_R2_OP_UNSHIFTED_MASK) << IW_R2_OP_LSB)

#define IW_L26_IMM26_LSB 6
#define IW_L26_IMM26_SIZE 26
#define IW_L26_IMM26_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_L26_IMM26_SIZE))
#define IW_L26_IMM26_SHIFTED_MASK (IW_L26_IMM26_UNSHIFTED_MASK << IW_L26_IMM26_LSB)
#define GET_IW_L26_IMM26(W) (((W) >> IW_L26_IMM26_LSB) & IW_L26_IMM26_UNSHIFTED_MASK)
#define SET_IW_L26_IMM26(V) (((V) & IW_L26_IMM26_UNSHIFTED_MASK) << IW_L26_IMM26_LSB)

#define IW_F2I16_A_LSB 6
#define IW_F2I16_A_SIZE 5
#define IW_F2I16_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2I16_A_SIZE))
#define IW_F2I16_A_SHIFTED_MASK (IW_F2I16_A_UNSHIFTED_MASK << IW_F2I16_A_LSB)
#define GET_IW_F2I16_A(W) (((W) >> IW_F2I16_A_LSB) & IW_F2I16_A_UNSHIFTED_MASK)
#define SET_IW_F2I16_A(V) (((V) & IW_F2I16_A_UNSHIFTED_MASK) << IW_F2I16_A_LSB)

#define IW_F2I16_B_LSB 11
#define IW_F2I16_B_SIZE 5
#define IW_F2I16_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2I16_B_SIZE))
#define IW_F2I16_B_SHIFTED_MASK (IW_F2I16_B_UNSHIFTED_MASK << IW_F2I16_B_LSB)
#define GET_IW_F2I16_B(W) (((W) >> IW_F2I16_B_LSB) & IW_F2I16_B_UNSHIFTED_MASK)
#define SET_IW_F2I16_B(V) (((V) & IW_F2I16_B_UNSHIFTED_MASK) << IW_F2I16_B_LSB)

#define IW_F2I16_IMM16_LSB 16
#define IW_F2I16_IMM16_SIZE 16
#define IW_F2I16_IMM16_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2I16_IMM16_SIZE))
#define IW_F2I16_IMM16_SHIFTED_MASK (IW_F2I16_IMM16_UNSHIFTED_MASK << IW_F2I16_IMM16_LSB)
#define GET_IW_F2I16_IMM16(W) (((W) >> IW_F2I16_IMM16_LSB) & IW_F2I16_IMM16_UNSHIFTED_MASK)
#define SET_IW_F2I16_IMM16(V) (((V) & IW_F2I16_IMM16_UNSHIFTED_MASK) << IW_F2I16_IMM16_LSB)

/* Common to all three I12-group formats F2X4I12, F1X4I12, F1X4L17.  */
#define IW_I12_X_LSB 28
#define IW_I12_X_SIZE 4
#define IW_I12_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_I12_X_SIZE))
#define IW_I12_X_SHIFTED_MASK (IW_I12_X_UNSHIFTED_MASK << IW_I12_X_LSB)
#define GET_IW_I12_X(W) (((W) >> IW_I12_X_LSB) & IW_I12_X_UNSHIFTED_MASK)
#define SET_IW_I12_X(V) (((V) & IW_I12_X_UNSHIFTED_MASK) << IW_I12_X_LSB)

#define IW_F2X4I12_A_LSB 6
#define IW_F2X4I12_A_SIZE 5
#define IW_F2X4I12_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X4I12_A_SIZE))
#define IW_F2X4I12_A_SHIFTED_MASK (IW_F2X4I12_A_UNSHIFTED_MASK << IW_F2X4I12_A_LSB)
#define GET_IW_F2X4I12_A(W) (((W) >> IW_F2X4I12_A_LSB) & IW_F2X4I12_A_UNSHIFTED_MASK)
#define SET_IW_F2X4I12_A(V) (((V) & IW_F2X4I12_A_UNSHIFTED_MASK) << IW_F2X4I12_A_LSB)

#define IW_F2X4I12_B_LSB 11
#define IW_F2X4I12_B_SIZE 5
#define IW_F2X4I12_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X4I12_B_SIZE))
#define IW_F2X4I12_B_SHIFTED_MASK (IW_F2X4I12_B_UNSHIFTED_MASK << IW_F2X4I12_B_LSB)
#define GET_IW_F2X4I12_B(W) (((W) >> IW_F2X4I12_B_LSB) & IW_F2X4I12_B_UNSHIFTED_MASK)
#define SET_IW_F2X4I12_B(V) (((V) & IW_F2X4I12_B_UNSHIFTED_MASK) << IW_F2X4I12_B_LSB)

#define IW_F2X4I12_IMM12_LSB 16
#define IW_F2X4I12_IMM12_SIZE 12
#define IW_F2X4I12_IMM12_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X4I12_IMM12_SIZE))
#define IW_F2X4I12_IMM12_SHIFTED_MASK (IW_F2X4I12_IMM12_UNSHIFTED_MASK << IW_F2X4I12_IMM12_LSB)
#define GET_IW_F2X4I12_IMM12(W) (((W) >> IW_F2X4I12_IMM12_LSB) & IW_F2X4I12_IMM12_UNSHIFTED_MASK)
#define SET_IW_F2X4I12_IMM12(V) (((V) & IW_F2X4I12_IMM12_UNSHIFTED_MASK) << IW_F2X4I12_IMM12_LSB)

#define IW_F1X4I12_A_LSB 6
#define IW_F1X4I12_A_SIZE 5
#define IW_F1X4I12_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4I12_A_SIZE))
#define IW_F1X4I12_A_SHIFTED_MASK (IW_F1X4I12_A_UNSHIFTED_MASK << IW_F1X4I12_A_LSB)
#define GET_IW_F1X4I12_A(W) (((W) >> IW_F1X4I12_A_LSB) & IW_F1X4I12_A_UNSHIFTED_MASK)
#define SET_IW_F1X4I12_A(V) (((V) & IW_F1X4I12_A_UNSHIFTED_MASK) << IW_F1X4I12_A_LSB)

#define IW_F1X4I12_X_LSB 11
#define IW_F1X4I12_X_SIZE 5
#define IW_F1X4I12_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4I12_X_SIZE))
#define IW_F1X4I12_X_SHIFTED_MASK (IW_F1X4I12_X_UNSHIFTED_MASK << IW_F1X4I12_X_LSB)
#define GET_IW_F1X4I12_X(W) (((W) >> IW_F1X4I12_X_LSB) & IW_F1X4I12_X_UNSHIFTED_MASK)
#define SET_IW_F1X4I12_X(V) (((V) & IW_F1X4I12_X_UNSHIFTED_MASK) << IW_F1X4I12_X_LSB)

#define IW_F1X4I12_IMM12_LSB 16
#define IW_F1X4I12_IMM12_SIZE 12
#define IW_F1X4I12_IMM12_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4I12_IMM12_SIZE))
#define IW_F1X4I12_IMM12_SHIFTED_MASK (IW_F1X4I12_IMM12_UNSHIFTED_MASK << IW_F1X4I12_IMM12_LSB)
#define GET_IW_F1X4I12_IMM12(W) (((W) >> IW_F1X4I12_IMM12_LSB) & IW_F1X4I12_IMM12_UNSHIFTED_MASK)
#define SET_IW_F1X4I12_IMM12(V) (((V) & IW_F1X4I12_IMM12_UNSHIFTED_MASK) << IW_F1X4I12_IMM12_LSB)

#define IW_F1X4L17_A_LSB 6
#define IW_F1X4L17_A_SIZE 5
#define IW_F1X4L17_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_A_SIZE))
#define IW_F1X4L17_A_SHIFTED_MASK (IW_F1X4L17_A_UNSHIFTED_MASK << IW_F1X4L17_A_LSB)
#define GET_IW_F1X4L17_A(W) (((W) >> IW_F1X4L17_A_LSB) & IW_F1X4L17_A_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_A(V) (((V) & IW_F1X4L17_A_UNSHIFTED_MASK) << IW_F1X4L17_A_LSB)

#define IW_F1X4L17_ID_LSB 11
#define IW_F1X4L17_ID_SIZE 1
#define IW_F1X4L17_ID_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_ID_SIZE))
#define IW_F1X4L17_ID_SHIFTED_MASK (IW_F1X4L17_ID_UNSHIFTED_MASK << IW_F1X4L17_ID_LSB)
#define GET_IW_F1X4L17_ID(W) (((W) >> IW_F1X4L17_ID_LSB) & IW_F1X4L17_ID_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_ID(V) (((V) & IW_F1X4L17_ID_UNSHIFTED_MASK) << IW_F1X4L17_ID_LSB)

#define IW_F1X4L17_WB_LSB 12
#define IW_F1X4L17_WB_SIZE 1
#define IW_F1X4L17_WB_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_WB_SIZE))
#define IW_F1X4L17_WB_SHIFTED_MASK (IW_F1X4L17_WB_UNSHIFTED_MASK << IW_F1X4L17_WB_LSB)
#define GET_IW_F1X4L17_WB(W) (((W) >> IW_F1X4L17_WB_LSB) & IW_F1X4L17_WB_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_WB(V) (((V) & IW_F1X4L17_WB_UNSHIFTED_MASK) << IW_F1X4L17_WB_LSB)

#define IW_F1X4L17_RS_LSB 13
#define IW_F1X4L17_RS_SIZE 1
#define IW_F1X4L17_RS_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_RS_SIZE))
#define IW_F1X4L17_RS_SHIFTED_MASK (IW_F1X4L17_RS_UNSHIFTED_MASK << IW_F1X4L17_RS_LSB)
#define GET_IW_F1X4L17_RS(W) (((W) >> IW_F1X4L17_RS_LSB) & IW_F1X4L17_RS_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_RS(V) (((V) & IW_F1X4L17_RS_UNSHIFTED_MASK) << IW_F1X4L17_RS_LSB)

#define IW_F1X4L17_PC_LSB 14
#define IW_F1X4L17_PC_SIZE 1
#define IW_F1X4L17_PC_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_PC_SIZE))
#define IW_F1X4L17_PC_SHIFTED_MASK (IW_F1X4L17_PC_UNSHIFTED_MASK << IW_F1X4L17_PC_LSB)
#define GET_IW_F1X4L17_PC(W) (((W) >> IW_F1X4L17_PC_LSB) & IW_F1X4L17_PC_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_PC(V) (((V) & IW_F1X4L17_PC_UNSHIFTED_MASK) << IW_F1X4L17_PC_LSB)

#define IW_F1X4L17_RSV_LSB 15
#define IW_F1X4L17_RSV_SIZE 1
#define IW_F1X4L17_RSV_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_RSV_SIZE))
#define IW_F1X4L17_RSV_SHIFTED_MASK (IW_F1X4L17_RSV_UNSHIFTED_MASK << IW_F1X4L17_RSV_LSB)
#define GET_IW_F1X4L17_RSV(W) (((W) >> IW_F1X4L17_RSV_LSB) & IW_F1X4L17_RSV_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_RSV(V) (((V) & IW_F1X4L17_RSV_UNSHIFTED_MASK) << IW_F1X4L17_RSV_LSB)

#define IW_F1X4L17_REGMASK_LSB 16
#define IW_F1X4L17_REGMASK_SIZE 12
#define IW_F1X4L17_REGMASK_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X4L17_REGMASK_SIZE))
#define IW_F1X4L17_REGMASK_SHIFTED_MASK (IW_F1X4L17_REGMASK_UNSHIFTED_MASK << IW_F1X4L17_REGMASK_LSB)
#define GET_IW_F1X4L17_REGMASK(W) (((W) >> IW_F1X4L17_REGMASK_LSB) & IW_F1X4L17_REGMASK_UNSHIFTED_MASK)
#define SET_IW_F1X4L17_REGMASK(V) (((V) & IW_F1X4L17_REGMASK_UNSHIFTED_MASK) << IW_F1X4L17_REGMASK_LSB)

/* Shared by OPX-group formats F3X6L5, F2X6L10, F3X6.  */
#define IW_OPX_X_LSB 26
#define IW_OPX_X_SIZE 6
#define IW_OPX_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_OPX_X_SIZE))
#define IW_OPX_X_SHIFTED_MASK (IW_OPX_X_UNSHIFTED_MASK << IW_OPX_X_LSB)
#define GET_IW_OPX_X(W) (((W) >> IW_OPX_X_LSB) & IW_OPX_X_UNSHIFTED_MASK)
#define SET_IW_OPX_X(V) (((V) & IW_OPX_X_UNSHIFTED_MASK) << IW_OPX_X_LSB)

/* F3X6L5 accessors are also used for F3X6 formats.  */
#define IW_F3X6L5_A_LSB 6
#define IW_F3X6L5_A_SIZE 5
#define IW_F3X6L5_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X6L5_A_SIZE))
#define IW_F3X6L5_A_SHIFTED_MASK (IW_F3X6L5_A_UNSHIFTED_MASK << IW_F3X6L5_A_LSB)
#define GET_IW_F3X6L5_A(W) (((W) >> IW_F3X6L5_A_LSB) & IW_F3X6L5_A_UNSHIFTED_MASK)
#define SET_IW_F3X6L5_A(V) (((V) & IW_F3X6L5_A_UNSHIFTED_MASK) << IW_F3X6L5_A_LSB)

#define IW_F3X6L5_B_LSB 11
#define IW_F3X6L5_B_SIZE 5
#define IW_F3X6L5_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X6L5_B_SIZE))
#define IW_F3X6L5_B_SHIFTED_MASK (IW_F3X6L5_B_UNSHIFTED_MASK << IW_F3X6L5_B_LSB)
#define GET_IW_F3X6L5_B(W) (((W) >> IW_F3X6L5_B_LSB) & IW_F3X6L5_B_UNSHIFTED_MASK)
#define SET_IW_F3X6L5_B(V) (((V) & IW_F3X6L5_B_UNSHIFTED_MASK) << IW_F3X6L5_B_LSB)

#define IW_F3X6L5_C_LSB 16
#define IW_F3X6L5_C_SIZE 5
#define IW_F3X6L5_C_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X6L5_C_SIZE))
#define IW_F3X6L5_C_SHIFTED_MASK (IW_F3X6L5_C_UNSHIFTED_MASK << IW_F3X6L5_C_LSB)
#define GET_IW_F3X6L5_C(W) (((W) >> IW_F3X6L5_C_LSB) & IW_F3X6L5_C_UNSHIFTED_MASK)
#define SET_IW_F3X6L5_C(V) (((V) & IW_F3X6L5_C_UNSHIFTED_MASK) << IW_F3X6L5_C_LSB)

#define IW_F3X6L5_IMM5_LSB 21
#define IW_F3X6L5_IMM5_SIZE 5
#define IW_F3X6L5_IMM5_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X6L5_IMM5_SIZE))
#define IW_F3X6L5_IMM5_SHIFTED_MASK (IW_F3X6L5_IMM5_UNSHIFTED_MASK << IW_F3X6L5_IMM5_LSB)
#define GET_IW_F3X6L5_IMM5(W) (((W) >> IW_F3X6L5_IMM5_LSB) & IW_F3X6L5_IMM5_UNSHIFTED_MASK)
#define SET_IW_F3X6L5_IMM5(V) (((V) & IW_F3X6L5_IMM5_UNSHIFTED_MASK) << IW_F3X6L5_IMM5_LSB)

#define IW_F2X6L10_A_LSB 6
#define IW_F2X6L10_A_SIZE 5
#define IW_F2X6L10_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X6L10_A_SIZE))
#define IW_F2X6L10_A_SHIFTED_MASK (IW_F2X6L10_A_UNSHIFTED_MASK << IW_F2X6L10_A_LSB)
#define GET_IW_F2X6L10_A(W) (((W) >> IW_F2X6L10_A_LSB) & IW_F2X6L10_A_UNSHIFTED_MASK)
#define SET_IW_F2X6L10_A(V) (((V) & IW_F2X6L10_A_UNSHIFTED_MASK) << IW_F2X6L10_A_LSB)

#define IW_F2X6L10_B_LSB 11
#define IW_F2X6L10_B_SIZE 5
#define IW_F2X6L10_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X6L10_B_SIZE))
#define IW_F2X6L10_B_SHIFTED_MASK (IW_F2X6L10_B_UNSHIFTED_MASK << IW_F2X6L10_B_LSB)
#define GET_IW_F2X6L10_B(W) (((W) >> IW_F2X6L10_B_LSB) & IW_F2X6L10_B_UNSHIFTED_MASK)
#define SET_IW_F2X6L10_B(V) (((V) & IW_F2X6L10_B_UNSHIFTED_MASK) << IW_F2X6L10_B_LSB)

#define IW_F2X6L10_LSB_LSB 16
#define IW_F2X6L10_LSB_SIZE 5
#define IW_F2X6L10_LSB_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X6L10_LSB_SIZE))
#define IW_F2X6L10_LSB_SHIFTED_MASK (IW_F2X6L10_LSB_UNSHIFTED_MASK << IW_F2X6L10_LSB_LSB)
#define GET_IW_F2X6L10_LSB(W) (((W) >> IW_F2X6L10_LSB_LSB) & IW_F2X6L10_LSB_UNSHIFTED_MASK)
#define SET_IW_F2X6L10_LSB(V) (((V) & IW_F2X6L10_LSB_UNSHIFTED_MASK) << IW_F2X6L10_LSB_LSB)

#define IW_F2X6L10_MSB_LSB 21
#define IW_F2X6L10_MSB_SIZE 5
#define IW_F2X6L10_MSB_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2X6L10_MSB_SIZE))
#define IW_F2X6L10_MSB_SHIFTED_MASK (IW_F2X6L10_MSB_UNSHIFTED_MASK << IW_F2X6L10_MSB_LSB)
#define GET_IW_F2X6L10_MSB(W) (((W) >> IW_F2X6L10_MSB_LSB) & IW_F2X6L10_MSB_UNSHIFTED_MASK)
#define SET_IW_F2X6L10_MSB(V) (((V) & IW_F2X6L10_MSB_UNSHIFTED_MASK) << IW_F2X6L10_MSB_LSB)

#define IW_F3X8_A_LSB 6
#define IW_F3X8_A_SIZE 5
#define IW_F3X8_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_A_SIZE))
#define IW_F3X8_A_SHIFTED_MASK (IW_F3X8_A_UNSHIFTED_MASK << IW_F3X8_A_LSB)
#define GET_IW_F3X8_A(W) (((W) >> IW_F3X8_A_LSB) & IW_F3X8_A_UNSHIFTED_MASK)
#define SET_IW_F3X8_A(V) (((V) & IW_F3X8_A_UNSHIFTED_MASK) << IW_F3X8_A_LSB)

#define IW_F3X8_B_LSB 11
#define IW_F3X8_B_SIZE 5
#define IW_F3X8_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_B_SIZE))
#define IW_F3X8_B_SHIFTED_MASK (IW_F3X8_B_UNSHIFTED_MASK << IW_F3X8_B_LSB)
#define GET_IW_F3X8_B(W) (((W) >> IW_F3X8_B_LSB) & IW_F3X8_B_UNSHIFTED_MASK)
#define SET_IW_F3X8_B(V) (((V) & IW_F3X8_B_UNSHIFTED_MASK) << IW_F3X8_B_LSB)

#define IW_F3X8_C_LSB 16
#define IW_F3X8_C_SIZE 5
#define IW_F3X8_C_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_C_SIZE))
#define IW_F3X8_C_SHIFTED_MASK (IW_F3X8_C_UNSHIFTED_MASK << IW_F3X8_C_LSB)
#define GET_IW_F3X8_C(W) (((W) >> IW_F3X8_C_LSB) & IW_F3X8_C_UNSHIFTED_MASK)
#define SET_IW_F3X8_C(V) (((V) & IW_F3X8_C_UNSHIFTED_MASK) << IW_F3X8_C_LSB)

#define IW_F3X8_READA_LSB 21
#define IW_F3X8_READA_SIZE 1
#define IW_F3X8_READA_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_READA_SIZE))
#define IW_F3X8_READA_SHIFTED_MASK (IW_F3X8_READA_UNSHIFTED_MASK << IW_F3X8_READA_LSB)
#define GET_IW_F3X8_READA(W) (((W) >> IW_F3X8_READA_LSB) & IW_F3X8_READA_UNSHIFTED_MASK)
#define SET_IW_F3X8_READA(V) (((V) & IW_F3X8_READA_UNSHIFTED_MASK) << IW_F3X8_READA_LSB)

#define IW_F3X8_READB_LSB 22
#define IW_F3X8_READB_SIZE 1
#define IW_F3X8_READB_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_READB_SIZE))
#define IW_F3X8_READB_SHIFTED_MASK (IW_F3X8_READB_UNSHIFTED_MASK << IW_F3X8_READB_LSB)
#define GET_IW_F3X8_READB(W) (((W) >> IW_F3X8_READB_LSB) & IW_F3X8_READB_UNSHIFTED_MASK)
#define SET_IW_F3X8_READB(V) (((V) & IW_F3X8_READB_UNSHIFTED_MASK) << IW_F3X8_READB_LSB)

#define IW_F3X8_READC_LSB 23
#define IW_F3X8_READC_SIZE 1
#define IW_F3X8_READC_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_READC_SIZE))
#define IW_F3X8_READC_SHIFTED_MASK (IW_F3X8_READC_UNSHIFTED_MASK << IW_F3X8_READC_LSB)
#define GET_IW_F3X8_READC(W) (((W) >> IW_F3X8_READC_LSB) & IW_F3X8_READC_UNSHIFTED_MASK)
#define SET_IW_F3X8_READC(V) (((V) & IW_F3X8_READC_UNSHIFTED_MASK) << IW_F3X8_READC_LSB)

#define IW_F3X8_N_LSB 24
#define IW_F3X8_N_SIZE 8
#define IW_F3X8_N_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F3X8_N_SIZE))
#define IW_F3X8_N_SHIFTED_MASK (IW_F3X8_N_UNSHIFTED_MASK << IW_F3X8_N_LSB)
#define GET_IW_F3X8_N(W) (((W) >> IW_F3X8_N_LSB) & IW_F3X8_N_UNSHIFTED_MASK)
#define SET_IW_F3X8_N(V) (((V) & IW_F3X8_N_UNSHIFTED_MASK) << IW_F3X8_N_LSB)

/* 16-bit R2 fields.  */

#define IW_I10_IMM10_LSB 6
#define IW_I10_IMM10_SIZE 10
#define IW_I10_IMM10_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_I10_IMM10_SIZE))
#define IW_I10_IMM10_SHIFTED_MASK (IW_I10_IMM10_UNSHIFTED_MASK << IW_I10_IMM10_LSB)
#define GET_IW_I10_IMM10(W) (((W) >> IW_I10_IMM10_LSB) & IW_I10_IMM10_UNSHIFTED_MASK)
#define SET_IW_I10_IMM10(V) (((V) & IW_I10_IMM10_UNSHIFTED_MASK) << IW_I10_IMM10_LSB)

#define IW_T1I7_A3_LSB 6
#define IW_T1I7_A3_SIZE 3
#define IW_T1I7_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T1I7_A3_SIZE))
#define IW_T1I7_A3_SHIFTED_MASK (IW_T1I7_A3_UNSHIFTED_MASK << IW_T1I7_A3_LSB)
#define GET_IW_T1I7_A3(W) (((W) >> IW_T1I7_A3_LSB) & IW_T1I7_A3_UNSHIFTED_MASK)
#define SET_IW_T1I7_A3(V) (((V) & IW_T1I7_A3_UNSHIFTED_MASK) << IW_T1I7_A3_LSB)

#define IW_T1I7_IMM7_LSB 9
#define IW_T1I7_IMM7_SIZE 7
#define IW_T1I7_IMM7_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T1I7_IMM7_SIZE))
#define IW_T1I7_IMM7_SHIFTED_MASK (IW_T1I7_IMM7_UNSHIFTED_MASK << IW_T1I7_IMM7_LSB)
#define GET_IW_T1I7_IMM7(W) (((W) >> IW_T1I7_IMM7_LSB) & IW_T1I7_IMM7_UNSHIFTED_MASK)
#define SET_IW_T1I7_IMM7(V) (((V) & IW_T1I7_IMM7_UNSHIFTED_MASK) << IW_T1I7_IMM7_LSB)

#define IW_T2I4_A3_LSB 6
#define IW_T2I4_A3_SIZE 3
#define IW_T2I4_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2I4_A3_SIZE))
#define IW_T2I4_A3_SHIFTED_MASK (IW_T2I4_A3_UNSHIFTED_MASK << IW_T2I4_A3_LSB)
#define GET_IW_T2I4_A3(W) (((W) >> IW_T2I4_A3_LSB) & IW_T2I4_A3_UNSHIFTED_MASK)
#define SET_IW_T2I4_A3(V) (((V) & IW_T2I4_A3_UNSHIFTED_MASK) << IW_T2I4_A3_LSB)

#define IW_T2I4_B3_LSB 9
#define IW_T2I4_B3_SIZE 3
#define IW_T2I4_B3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2I4_B3_SIZE))
#define IW_T2I4_B3_SHIFTED_MASK (IW_T2I4_B3_UNSHIFTED_MASK << IW_T2I4_B3_LSB)
#define GET_IW_T2I4_B3(W) (((W) >> IW_T2I4_B3_LSB) & IW_T2I4_B3_UNSHIFTED_MASK)
#define SET_IW_T2I4_B3(V) (((V) & IW_T2I4_B3_UNSHIFTED_MASK) << IW_T2I4_B3_LSB)

#define IW_T2I4_IMM4_LSB 12
#define IW_T2I4_IMM4_SIZE 4
#define IW_T2I4_IMM4_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2I4_IMM4_SIZE))
#define IW_T2I4_IMM4_SHIFTED_MASK (IW_T2I4_IMM4_UNSHIFTED_MASK << IW_T2I4_IMM4_LSB)
#define GET_IW_T2I4_IMM4(W) (((W) >> IW_T2I4_IMM4_LSB) & IW_T2I4_IMM4_UNSHIFTED_MASK)
#define SET_IW_T2I4_IMM4(V) (((V) & IW_T2I4_IMM4_UNSHIFTED_MASK) << IW_T2I4_IMM4_LSB)

#define IW_T1X1I6_A3_LSB 6
#define IW_T1X1I6_A3_SIZE 3
#define IW_T1X1I6_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T1X1I6_A3_SIZE))
#define IW_T1X1I6_A3_SHIFTED_MASK (IW_T1X1I6_A3_UNSHIFTED_MASK << IW_T1X1I6_A3_LSB)
#define GET_IW_T1X1I6_A3(W) (((W) >> IW_T1X1I6_A3_LSB) & IW_T1X1I6_A3_UNSHIFTED_MASK)
#define SET_IW_T1X1I6_A3(V) (((V) & IW_T1X1I6_A3_UNSHIFTED_MASK) << IW_T1X1I6_A3_LSB)

#define IW_T1X1I6_IMM6_LSB 9
#define IW_T1X1I6_IMM6_SIZE 6
#define IW_T1X1I6_IMM6_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T1X1I6_IMM6_SIZE))
#define IW_T1X1I6_IMM6_SHIFTED_MASK (IW_T1X1I6_IMM6_UNSHIFTED_MASK << IW_T1X1I6_IMM6_LSB)
#define GET_IW_T1X1I6_IMM6(W) (((W) >> IW_T1X1I6_IMM6_LSB) & IW_T1X1I6_IMM6_UNSHIFTED_MASK)
#define SET_IW_T1X1I6_IMM6(V) (((V) & IW_T1X1I6_IMM6_UNSHIFTED_MASK) << IW_T1X1I6_IMM6_LSB)

#define IW_T1X1I6_X_LSB 15
#define IW_T1X1I6_X_SIZE 1
#define IW_T1X1I6_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T1X1I6_X_SIZE))
#define IW_T1X1I6_X_SHIFTED_MASK (IW_T1X1I6_X_UNSHIFTED_MASK << IW_T1X1I6_X_LSB)
#define GET_IW_T1X1I6_X(W) (((W) >> IW_T1X1I6_X_LSB) & IW_T1X1I6_X_UNSHIFTED_MASK)
#define SET_IW_T1X1I6_X(V) (((V) & IW_T1X1I6_X_UNSHIFTED_MASK) << IW_T1X1I6_X_LSB)

#define IW_X1I7_IMM7_LSB 6
#define IW_X1I7_IMM7_SIZE 7
#define IW_X1I7_IMM7_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_X1I7_IMM7_SIZE))
#define IW_X1I7_IMM7_SHIFTED_MASK (IW_X1I7_IMM7_UNSHIFTED_MASK << IW_X1I7_IMM7_LSB)
#define GET_IW_X1I7_IMM7(W) (((W) >> IW_X1I7_IMM7_LSB) & IW_X1I7_IMM7_UNSHIFTED_MASK)
#define SET_IW_X1I7_IMM7(V) (((V) & IW_X1I7_IMM7_UNSHIFTED_MASK) << IW_X1I7_IMM7_LSB)

#define IW_X1I7_RSV_LSB 13
#define IW_X1I7_RSV_SIZE 2
#define IW_X1I7_RSV_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_X1I7_RSV_SIZE))
#define IW_X1I7_RSV_SHIFTED_MASK (IW_X1I7_RSV_UNSHIFTED_MASK << IW_X1I7_RSV_LSB)
#define GET_IW_X1I7_RSV(W) (((W) >> IW_X1I7_RSV_LSB) & IW_X1I7_RSV_UNSHIFTED_MASK)
#define SET_IW_X1I7_RSV(V) (((V) & IW_X1I7_RSV_UNSHIFTED_MASK) << IW_X1I7_RSV_LSB)

#define IW_X1I7_X_LSB 15
#define IW_X1I7_X_SIZE 1
#define IW_X1I7_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_X1I7_X_SIZE))
#define IW_X1I7_X_SHIFTED_MASK (IW_X1I7_X_UNSHIFTED_MASK << IW_X1I7_X_LSB)
#define GET_IW_X1I7_X(W) (((W) >> IW_X1I7_X_LSB) & IW_X1I7_X_UNSHIFTED_MASK)
#define SET_IW_X1I7_X(V) (((V) & IW_X1I7_X_UNSHIFTED_MASK) << IW_X1I7_X_LSB)

#define IW_L5I4X1_IMM4_LSB 6
#define IW_L5I4X1_IMM4_SIZE 4
#define IW_L5I4X1_IMM4_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_L5I4X1_IMM4_SIZE))
#define IW_L5I4X1_IMM4_SHIFTED_MASK (IW_L5I4X1_IMM4_UNSHIFTED_MASK << IW_L5I4X1_IMM4_LSB)
#define GET_IW_L5I4X1_IMM4(W) (((W) >> IW_L5I4X1_IMM4_LSB) & IW_L5I4X1_IMM4_UNSHIFTED_MASK)
#define SET_IW_L5I4X1_IMM4(V) (((V) & IW_L5I4X1_IMM4_UNSHIFTED_MASK) << IW_L5I4X1_IMM4_LSB)

#define IW_L5I4X1_REGRANGE_LSB 10
#define IW_L5I4X1_REGRANGE_SIZE 3
#define IW_L5I4X1_REGRANGE_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_L5I4X1_REGRANGE_SIZE))
#define IW_L5I4X1_REGRANGE_SHIFTED_MASK (IW_L5I4X1_REGRANGE_UNSHIFTED_MASK << IW_L5I4X1_REGRANGE_LSB)
#define GET_IW_L5I4X1_REGRANGE(W) (((W) >> IW_L5I4X1_REGRANGE_LSB) & IW_L5I4X1_REGRANGE_UNSHIFTED_MASK)
#define SET_IW_L5I4X1_REGRANGE(V) (((V) & IW_L5I4X1_REGRANGE_UNSHIFTED_MASK) << IW_L5I4X1_REGRANGE_LSB)

#define IW_L5I4X1_FP_LSB 13
#define IW_L5I4X1_FP_SIZE 1
#define IW_L5I4X1_FP_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_L5I4X1_FP_SIZE))
#define IW_L5I4X1_FP_SHIFTED_MASK (IW_L5I4X1_FP_UNSHIFTED_MASK << IW_L5I4X1_FP_LSB)
#define GET_IW_L5I4X1_FP(W) (((W) >> IW_L5I4X1_FP_LSB) & IW_L5I4X1_FP_UNSHIFTED_MASK)
#define SET_IW_L5I4X1_FP(V) (((V) & IW_L5I4X1_FP_UNSHIFTED_MASK) << IW_L5I4X1_FP_LSB)

#define IW_L5I4X1_CS_LSB 14
#define IW_L5I4X1_CS_SIZE 1
#define IW_L5I4X1_CS_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_L5I4X1_CS_SIZE))
#define IW_L5I4X1_CS_SHIFTED_MASK (IW_L5I4X1_CS_UNSHIFTED_MASK << IW_L5I4X1_CS_LSB)
#define GET_IW_L5I4X1_CS(W) (((W) >> IW_L5I4X1_CS_LSB) & IW_L5I4X1_CS_UNSHIFTED_MASK)
#define SET_IW_L5I4X1_CS(V) (((V) & IW_L5I4X1_CS_UNSHIFTED_MASK) << IW_L5I4X1_CS_LSB)

#define IW_L5I4X1_X_LSB 15
#define IW_L5I4X1_X_SIZE 1
#define IW_L5I4X1_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_L5I4X1_X_SIZE))
#define IW_L5I4X1_X_SHIFTED_MASK (IW_L5I4X1_X_UNSHIFTED_MASK << IW_L5I4X1_X_LSB)
#define GET_IW_L5I4X1_X(W) (((W) >> IW_L5I4X1_X_LSB) & IW_L5I4X1_X_UNSHIFTED_MASK)
#define SET_IW_L5I4X1_X(V) (((V) & IW_L5I4X1_X_UNSHIFTED_MASK) << IW_L5I4X1_X_LSB)

#define IW_T2X1L3_A3_LSB 6
#define IW_T2X1L3_A3_SIZE 3
#define IW_T2X1L3_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1L3_A3_SIZE))
#define IW_T2X1L3_A3_SHIFTED_MASK (IW_T2X1L3_A3_UNSHIFTED_MASK << IW_T2X1L3_A3_LSB)
#define GET_IW_T2X1L3_A3(W) (((W) >> IW_T2X1L3_A3_LSB) & IW_T2X1L3_A3_UNSHIFTED_MASK)
#define SET_IW_T2X1L3_A3(V) (((V) & IW_T2X1L3_A3_UNSHIFTED_MASK) << IW_T2X1L3_A3_LSB)

#define IW_T2X1L3_B3_LSB 9
#define IW_T2X1L3_B3_SIZE 3
#define IW_T2X1L3_B3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1L3_B3_SIZE))
#define IW_T2X1L3_B3_SHIFTED_MASK (IW_T2X1L3_B3_UNSHIFTED_MASK << IW_T2X1L3_B3_LSB)
#define GET_IW_T2X1L3_B3(W) (((W) >> IW_T2X1L3_B3_LSB) & IW_T2X1L3_B3_UNSHIFTED_MASK)
#define SET_IW_T2X1L3_B3(V) (((V) & IW_T2X1L3_B3_UNSHIFTED_MASK) << IW_T2X1L3_B3_LSB)

#define IW_T2X1L3_SHAMT_LSB 12
#define IW_T2X1L3_SHAMT_SIZE 3
#define IW_T2X1L3_SHAMT_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1L3_SHAMT_SIZE))
#define IW_T2X1L3_SHAMT_SHIFTED_MASK (IW_T2X1L3_SHAMT_UNSHIFTED_MASK << IW_T2X1L3_SHAMT_LSB)
#define GET_IW_T2X1L3_SHAMT(W) (((W) >> IW_T2X1L3_SHAMT_LSB) & IW_T2X1L3_SHAMT_UNSHIFTED_MASK)
#define SET_IW_T2X1L3_SHAMT(V) (((V) & IW_T2X1L3_SHAMT_UNSHIFTED_MASK) << IW_T2X1L3_SHAMT_LSB)

#define IW_T2X1L3_X_LSB 15
#define IW_T2X1L3_X_SIZE 1
#define IW_T2X1L3_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1L3_X_SIZE))
#define IW_T2X1L3_X_SHIFTED_MASK (IW_T2X1L3_X_UNSHIFTED_MASK << IW_T2X1L3_X_LSB)
#define GET_IW_T2X1L3_X(W) (((W) >> IW_T2X1L3_X_LSB) & IW_T2X1L3_X_UNSHIFTED_MASK)
#define SET_IW_T2X1L3_X(V) (((V) & IW_T2X1L3_X_UNSHIFTED_MASK) << IW_T2X1L3_X_LSB)

#define IW_T2X1I3_A3_LSB 6
#define IW_T2X1I3_A3_SIZE 3
#define IW_T2X1I3_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1I3_A3_SIZE))
#define IW_T2X1I3_A3_SHIFTED_MASK (IW_T2X1I3_A3_UNSHIFTED_MASK << IW_T2X1I3_A3_LSB)
#define GET_IW_T2X1I3_A3(W) (((W) >> IW_T2X1I3_A3_LSB) & IW_T2X1I3_A3_UNSHIFTED_MASK)
#define SET_IW_T2X1I3_A3(V) (((V) & IW_T2X1I3_A3_UNSHIFTED_MASK) << IW_T2X1I3_A3_LSB)

#define IW_T2X1I3_B3_LSB 9
#define IW_T2X1I3_B3_SIZE 3
#define IW_T2X1I3_B3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1I3_B3_SIZE))
#define IW_T2X1I3_B3_SHIFTED_MASK (IW_T2X1I3_B3_UNSHIFTED_MASK << IW_T2X1I3_B3_LSB)
#define GET_IW_T2X1I3_B3(W) (((W) >> IW_T2X1I3_B3_LSB) & IW_T2X1I3_B3_UNSHIFTED_MASK)
#define SET_IW_T2X1I3_B3(V) (((V) & IW_T2X1I3_B3_UNSHIFTED_MASK) << IW_T2X1I3_B3_LSB)

#define IW_T2X1I3_IMM3_LSB 12
#define IW_T2X1I3_IMM3_SIZE 3
#define IW_T2X1I3_IMM3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1I3_IMM3_SIZE))
#define IW_T2X1I3_IMM3_SHIFTED_MASK (IW_T2X1I3_IMM3_UNSHIFTED_MASK << IW_T2X1I3_IMM3_LSB)
#define GET_IW_T2X1I3_IMM3(W) (((W) >> IW_T2X1I3_IMM3_LSB) & IW_T2X1I3_IMM3_UNSHIFTED_MASK)
#define SET_IW_T2X1I3_IMM3(V) (((V) & IW_T2X1I3_IMM3_UNSHIFTED_MASK) << IW_T2X1I3_IMM3_LSB)

#define IW_T2X1I3_X_LSB 15
#define IW_T2X1I3_X_SIZE 1
#define IW_T2X1I3_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X1I3_X_SIZE))
#define IW_T2X1I3_X_SHIFTED_MASK (IW_T2X1I3_X_UNSHIFTED_MASK << IW_T2X1I3_X_LSB)
#define GET_IW_T2X1I3_X(W) (((W) >> IW_T2X1I3_X_LSB) & IW_T2X1I3_X_UNSHIFTED_MASK)
#define SET_IW_T2X1I3_X(V) (((V) & IW_T2X1I3_X_UNSHIFTED_MASK) << IW_T2X1I3_X_LSB)

#define IW_T3X1_A3_LSB 6
#define IW_T3X1_A3_SIZE 3
#define IW_T3X1_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T3X1_A3_SIZE))
#define IW_T3X1_A3_SHIFTED_MASK (IW_T3X1_A3_UNSHIFTED_MASK << IW_T3X1_A3_LSB)
#define GET_IW_T3X1_A3(W) (((W) >> IW_T3X1_A3_LSB) & IW_T3X1_A3_UNSHIFTED_MASK)
#define SET_IW_T3X1_A3(V) (((V) & IW_T3X1_A3_UNSHIFTED_MASK) << IW_T3X1_A3_LSB)

#define IW_T3X1_B3_LSB 9
#define IW_T3X1_B3_SIZE 3
#define IW_T3X1_B3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T3X1_B3_SIZE))
#define IW_T3X1_B3_SHIFTED_MASK (IW_T3X1_B3_UNSHIFTED_MASK << IW_T3X1_B3_LSB)
#define GET_IW_T3X1_B3(W) (((W) >> IW_T3X1_B3_LSB) & IW_T3X1_B3_UNSHIFTED_MASK)
#define SET_IW_T3X1_B3(V) (((V) & IW_T3X1_B3_UNSHIFTED_MASK) << IW_T3X1_B3_LSB)

#define IW_T3X1_C3_LSB 12
#define IW_T3X1_C3_SIZE 3
#define IW_T3X1_C3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T3X1_C3_SIZE))
#define IW_T3X1_C3_SHIFTED_MASK (IW_T3X1_C3_UNSHIFTED_MASK << IW_T3X1_C3_LSB)
#define GET_IW_T3X1_C3(W) (((W) >> IW_T3X1_C3_LSB) & IW_T3X1_C3_UNSHIFTED_MASK)
#define SET_IW_T3X1_C3(V) (((V) & IW_T3X1_C3_UNSHIFTED_MASK) << IW_T3X1_C3_LSB)

#define IW_T3X1_X_LSB 15
#define IW_T3X1_X_SIZE 1
#define IW_T3X1_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T3X1_X_SIZE))
#define IW_T3X1_X_SHIFTED_MASK (IW_T3X1_X_UNSHIFTED_MASK << IW_T3X1_X_LSB)
#define GET_IW_T3X1_X(W) (((W) >> IW_T3X1_X_LSB) & IW_T3X1_X_UNSHIFTED_MASK)
#define SET_IW_T3X1_X(V) (((V) & IW_T3X1_X_UNSHIFTED_MASK) << IW_T3X1_X_LSB)

/* The X field for all three R.N-class instruction formats is represented
   here as 4 bits, including the bits defined as constant 0 or 1 that
   determine which of the formats T2X3, F1X1, or X2L5 it is.  */
#define IW_R_N_X_LSB 12
#define IW_R_N_X_SIZE 4
#define IW_R_N_X_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_R_N_X_SIZE))
#define IW_R_N_X_SHIFTED_MASK (IW_R_N_X_UNSHIFTED_MASK << IW_R_N_X_LSB)
#define GET_IW_R_N_X(W) (((W) >> IW_R_N_X_LSB) & IW_R_N_X_UNSHIFTED_MASK)
#define SET_IW_R_N_X(V) (((V) & IW_R_N_X_UNSHIFTED_MASK) << IW_R_N_X_LSB)

#define IW_T2X3_A3_LSB 6
#define IW_T2X3_A3_SIZE 3
#define IW_T2X3_A3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X3_A3_SIZE))
#define IW_T2X3_A3_SHIFTED_MASK (IW_T2X3_A3_UNSHIFTED_MASK << IW_T2X3_A3_LSB)
#define GET_IW_T2X3_A3(W) (((W) >> IW_T2X3_A3_LSB) & IW_T2X3_A3_UNSHIFTED_MASK)
#define SET_IW_T2X3_A3(V) (((V) & IW_T2X3_A3_UNSHIFTED_MASK) << IW_T2X3_A3_LSB)

#define IW_T2X3_B3_LSB 9
#define IW_T2X3_B3_SIZE 3
#define IW_T2X3_B3_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_T2X3_B3_SIZE))
#define IW_T2X3_B3_SHIFTED_MASK (IW_T2X3_B3_UNSHIFTED_MASK << IW_T2X3_B3_LSB)
#define GET_IW_T2X3_B3(W) (((W) >> IW_T2X3_B3_LSB) & IW_T2X3_B3_UNSHIFTED_MASK)
#define SET_IW_T2X3_B3(V) (((V) & IW_T2X3_B3_UNSHIFTED_MASK) << IW_T2X3_B3_LSB)

#define IW_F1X1_A_LSB 6
#define IW_F1X1_A_SIZE 5
#define IW_F1X1_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X1_A_SIZE))
#define IW_F1X1_A_SHIFTED_MASK (IW_F1X1_A_UNSHIFTED_MASK << IW_F1X1_A_LSB)
#define GET_IW_F1X1_A(W) (((W) >> IW_F1X1_A_LSB) & IW_F1X1_A_UNSHIFTED_MASK)
#define SET_IW_F1X1_A(V) (((V) & IW_F1X1_A_UNSHIFTED_MASK) << IW_F1X1_A_LSB)

#define IW_F1X1_RSV_LSB 11
#define IW_F1X1_RSV_SIZE 1
#define IW_F1X1_RSV_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1X1_RSV_SIZE))
#define IW_F1X1_RSV_SHIFTED_MASK (IW_F1X1_RSV_UNSHIFTED_MASK << IW_F1X1_RSV_LSB)
#define GET_IW_F1X1_RSV(W) (((W) >> IW_F1X1_RSV_LSB) & IW_F1X1_RSV_UNSHIFTED_MASK)
#define SET_IW_F1X1_RSV(V) (((V) & IW_F1X1_RSV_UNSHIFTED_MASK) << IW_F1X1_RSV_LSB)

#define IW_X2L5_IMM5_LSB 6
#define IW_X2L5_IMM5_SIZE 5
#define IW_X2L5_IMM5_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_X2L5_IMM5_SIZE))
#define IW_X2L5_IMM5_SHIFTED_MASK (IW_X2L5_IMM5_UNSHIFTED_MASK << IW_X2L5_IMM5_LSB)
#define GET_IW_X2L5_IMM5(W) (((W) >> IW_X2L5_IMM5_LSB) & IW_X2L5_IMM5_UNSHIFTED_MASK)
#define SET_IW_X2L5_IMM5(V) (((V) & IW_X2L5_IMM5_UNSHIFTED_MASK) << IW_X2L5_IMM5_LSB)

#define IW_X2L5_RSV_LSB 11
#define IW_X2L5_RSV_SIZE 1
#define IW_X2L5_RSV_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_X2L5_RSV_SIZE))
#define IW_X2L5_RSV_SHIFTED_MASK (IW_X2L5_RSV_UNSHIFTED_MASK << IW_X2L5_RSV_LSB)
#define GET_IW_X2L5_RSV(W) (((W) >> IW_X2L5_RSV_LSB) & IW_X2L5_RSV_UNSHIFTED_MASK)
#define SET_IW_X2L5_RSV(V) (((V) & IW_X2L5_RSV_UNSHIFTED_MASK) << IW_X2L5_RSV_LSB)

#define IW_F1I5_IMM5_LSB 6
#define IW_F1I5_IMM5_SIZE 5
#define IW_F1I5_IMM5_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1I5_IMM5_SIZE))
#define IW_F1I5_IMM5_SHIFTED_MASK (IW_F1I5_IMM5_UNSHIFTED_MASK << IW_F1I5_IMM5_LSB)
#define GET_IW_F1I5_IMM5(W) (((W) >> IW_F1I5_IMM5_LSB) & IW_F1I5_IMM5_UNSHIFTED_MASK)
#define SET_IW_F1I5_IMM5(V) (((V) & IW_F1I5_IMM5_UNSHIFTED_MASK) << IW_F1I5_IMM5_LSB)

#define IW_F1I5_B_LSB 11
#define IW_F1I5_B_SIZE 5
#define IW_F1I5_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F1I5_B_SIZE))
#define IW_F1I5_B_SHIFTED_MASK (IW_F1I5_B_UNSHIFTED_MASK << IW_F1I5_B_LSB)
#define GET_IW_F1I5_B(W) (((W) >> IW_F1I5_B_LSB) & IW_F1I5_B_UNSHIFTED_MASK)
#define SET_IW_F1I5_B(V) (((V) & IW_F1I5_B_UNSHIFTED_MASK) << IW_F1I5_B_LSB)

#define IW_F2_A_LSB 6
#define IW_F2_A_SIZE 5
#define IW_F2_A_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2_A_SIZE))
#define IW_F2_A_SHIFTED_MASK (IW_F2_A_UNSHIFTED_MASK << IW_F2_A_LSB)
#define GET_IW_F2_A(W) (((W) >> IW_F2_A_LSB) & IW_F2_A_UNSHIFTED_MASK)
#define SET_IW_F2_A(V) (((V) & IW_F2_A_UNSHIFTED_MASK) << IW_F2_A_LSB)

#define IW_F2_B_LSB 11
#define IW_F2_B_SIZE 5
#define IW_F2_B_UNSHIFTED_MASK (0xffffffffu >> (32 - IW_F2_B_SIZE))
#define IW_F2_B_SHIFTED_MASK (IW_F2_B_UNSHIFTED_MASK << IW_F2_B_LSB)
#define GET_IW_F2_B(W) (((W) >> IW_F2_B_LSB) & IW_F2_B_UNSHIFTED_MASK)
#define SET_IW_F2_B(V) (((V) & IW_F2_B_UNSHIFTED_MASK) << IW_F2_B_LSB)

/* R2 opcodes.  */
#define R2_OP_CALL 0
#define R2_OP_AS_N 1
#define R2_OP_BR 2
#define R2_OP_BR_N 3
#define R2_OP_ADDI 4
#define R2_OP_LDBU_N 5
#define R2_OP_LDBU 6
#define R2_OP_LDB 7
#define R2_OP_JMPI 8
#define R2_OP_R_N 9
#define R2_OP_ANDI_N 11
#define R2_OP_ANDI 12
#define R2_OP_LDHU_N 13
#define R2_OP_LDHU 14
#define R2_OP_LDH 15
#define R2_OP_ASI_N 17
#define R2_OP_BGE 18
#define R2_OP_LDWSP_N 19
#define R2_OP_ORI 20
#define R2_OP_LDW_N 21
#define R2_OP_CMPGEI 22
#define R2_OP_LDW 23
#define R2_OP_SHI_N 25
#define R2_OP_BLT 26
#define R2_OP_MOVI_N 27
#define R2_OP_XORI 28
#define R2_OP_STZ_N 29
#define R2_OP_CMPLTI 30
#define R2_OP_ANDCI 31
#define R2_OP_OPX 32
#define R2_OP_PP_N 33
#define R2_OP_BNE 34
#define R2_OP_BNEZ_N 35
#define R2_OP_MULI 36
#define R2_OP_STB_N 37
#define R2_OP_CMPNEI 38
#define R2_OP_STB 39
#define R2_OP_I12 40
#define R2_OP_SPI_N 41
#define R2_OP_BEQ 42
#define R2_OP_BEQZ_N 43
#define R2_OP_ANDHI 44
#define R2_OP_STH_N 45
#define R2_OP_CMPEQI 46
#define R2_OP_STH 47
#define R2_OP_CUSTOM 48
#define R2_OP_BGEU 50
#define R2_OP_STWSP_N 51
#define R2_OP_ORHI 52
#define R2_OP_STW_N 53
#define R2_OP_CMPGEUI 54
#define R2_OP_STW 55
#define R2_OP_BLTU 58
#define R2_OP_MOV_N 59
#define R2_OP_XORHI 60
#define R2_OP_SPADDI_N 61
#define R2_OP_CMPLTUI 62
#define R2_OP_ANDCHI 63

#define R2_OPX_WRPIE 0
#define R2_OPX_ERET 1
#define R2_OPX_ROLI 2
#define R2_OPX_ROL 3
#define R2_OPX_FLUSHP 4
#define R2_OPX_RET 5
#define R2_OPX_NOR 6
#define R2_OPX_MULXUU 7
#define R2_OPX_ENI 8
#define R2_OPX_BRET 9
#define R2_OPX_ROR 11
#define R2_OPX_FLUSHI 12
#define R2_OPX_JMP 13
#define R2_OPX_AND 14
#define R2_OPX_CMPGE 16
#define R2_OPX_SLLI 18
#define R2_OPX_SLL 19
#define R2_OPX_WRPRS 20
#define R2_OPX_OR 22
#define R2_OPX_MULXSU 23
#define R2_OPX_CMPLT 24
#define R2_OPX_SRLI 26
#define R2_OPX_SRL 27
#define R2_OPX_NEXTPC 28
#define R2_OPX_CALLR 29
#define R2_OPX_XOR 30
#define R2_OPX_MULXSS 31
#define R2_OPX_CMPNE 32
#define R2_OPX_INSERT 35
#define R2_OPX_DIVU 36
#define R2_OPX_DIV 37
#define R2_OPX_RDCTL 38
#define R2_OPX_MUL 39
#define R2_OPX_CMPEQ 40
#define R2_OPX_INITI 41
#define R2_OPX_MERGE 43
#define R2_OPX_HBREAK 44
#define R2_OPX_TRAP 45
#define R2_OPX_WRCTL 46
#define R2_OPX_CMPGEU 48
#define R2_OPX_ADD 49
#define R2_OPX_EXTRACT 51
#define R2_OPX_BREAK 52
#define R2_OPX_LDEX 53
#define R2_OPX_SYNC 54
#define R2_OPX_LDSEX 55
#define R2_OPX_CMPLTU 56
#define R2_OPX_SUB 57
#define R2_OPX_SRAI 58
#define R2_OPX_SRA 59
#define R2_OPX_STEX 61
#define R2_OPX_STSEX 63

#define R2_I12_LDBIO 0
#define R2_I12_STBIO 1
#define R2_I12_LDBUIO 2
#define R2_I12_DCACHE 3
#define R2_I12_LDHIO 4
#define R2_I12_STHIO 5
#define R2_I12_LDHUIO 6
#define R2_I12_RDPRS 7
#define R2_I12_LDWIO 8
#define R2_I12_STWIO 9
#define R2_I12_LDWM 12
#define R2_I12_STWM 13

#define R2_DCACHE_INITD 0
#define R2_DCACHE_INITDA 1
#define R2_DCACHE_FLUSHD 2
#define R2_DCACHE_FLUSHDA 3

#define R2_AS_N_ADD_N 0
#define R2_AS_N_SUB_N 1

#define R2_R_N_AND_N 0
#define R2_R_N_OR_N 2
#define R2_R_N_XOR_N 3
#define R2_R_N_SLL_N 4
#define R2_R_N_SRL_N 5
#define R2_R_N_NOT_N 6
#define R2_R_N_NEG_N 7
#define R2_R_N_CALLR_N 8
#define R2_R_N_JMPR_N 10
#define R2_R_N_BREAK_N 12
#define R2_R_N_TRAP_N 13
#define R2_R_N_RET_N 14

#define R2_SPI_N_SPINCI_N 0
#define R2_SPI_N_SPDECI_N 1

#define R2_ASI_N_ADDI_N 0
#define R2_ASI_N_SUBI_N 1

#define R2_SHI_N_SLLI_N 0
#define R2_SHI_N_SRLI_N 1

#define R2_PP_N_POP_N 0
#define R2_PP_N_PUSH_N 1

#define R2_STZ_N_STWZ_N 0
#define R2_STZ_N_STBZ_N 1

/* Convenience macros for R2 encodings. */

#define MATCH_R2_OP(NAME) \
  (SET_IW_R2_OP (R2_OP_##NAME))
#define MASK_R2_OP \
  IW_R2_OP_SHIFTED_MASK

#define MATCH_R2_OPX0(NAME) \
  (SET_IW_R2_OP (R2_OP_OPX) | SET_IW_OPX_X (R2_OPX_##NAME))
#define MASK_R2_OPX0 \
  (IW_R2_OP_SHIFTED_MASK | IW_OPX_X_SHIFTED_MASK \
   | IW_F3X6L5_IMM5_SHIFTED_MASK)

#define MATCH_R2_OPX(NAME, A, B, C)				\
  (MATCH_R2_OPX0 (NAME) | SET_IW_F3X6L5_A (A) | SET_IW_F3X6L5_B (B) \
   | SET_IW_F3X6L5_C (C))
#define MASK_R2_OPX(A, B, C, N)				\
  (IW_R2_OP_SHIFTED_MASK | IW_OPX_X_SHIFTED_MASK	\
   | (A ? IW_F3X6L5_A_SHIFTED_MASK : 0)			\
   | (B ? IW_F3X6L5_B_SHIFTED_MASK : 0)			\
   | (C ? IW_F3X6L5_C_SHIFTED_MASK : 0)			\
   | (N ? IW_F3X6L5_IMM5_SHIFTED_MASK : 0))

#define MATCH_R2_I12(NAME) \
  (SET_IW_R2_OP (R2_OP_I12) | SET_IW_I12_X (R2_I12_##NAME))
#define MASK_R2_I12 \
  (IW_R2_OP_SHIFTED_MASK | IW_I12_X_SHIFTED_MASK )

#define MATCH_R2_DCACHE(NAME) \
  (MATCH_R2_I12(DCACHE) | SET_IW_F1X4I12_X (R2_DCACHE_##NAME))
#define MASK_R2_DCACHE \
  (MASK_R2_I12 | IW_F1X4I12_X_SHIFTED_MASK)

#define MATCH_R2_R_N(NAME) \
  (SET_IW_R2_OP (R2_OP_R_N) | SET_IW_R_N_X (R2_R_N_##NAME))
#define MASK_R2_R_N \
  (IW_R2_OP_SHIFTED_MASK | IW_R_N_X_SHIFTED_MASK )

/* Match/mask macros for R2 instructions.  */

#define MATCH_R2_ADD	MATCH_R2_OPX0 (ADD)
#define MASK_R2_ADD	MASK_R2_OPX0
#define MATCH_R2_ADDI	MATCH_R2_OP (ADDI)
#define MASK_R2_ADDI	MASK_R2_OP
#define MATCH_R2_ADD_N	(MATCH_R2_OP (AS_N) | SET_IW_T3X1_X (R2_AS_N_ADD_N))
#define MASK_R2_ADD_N	(MASK_R2_OP | IW_T3X1_X_SHIFTED_MASK)
#define MATCH_R2_ADDI_N	(MATCH_R2_OP (ASI_N) | SET_IW_T2X1I3_X (R2_ASI_N_ADDI_N))
#define MASK_R2_ADDI_N	(MASK_R2_OP | IW_T2X1I3_X_SHIFTED_MASK)
#define MATCH_R2_AND	MATCH_R2_OPX0 (AND)
#define MASK_R2_AND	MASK_R2_OPX0
#define MATCH_R2_ANDCHI	MATCH_R2_OP (ANDCHI)
#define MASK_R2_ANDCHI	MASK_R2_OP
#define MATCH_R2_ANDCI	MATCH_R2_OP (ANDCI)
#define MASK_R2_ANDCI	MASK_R2_OP
#define MATCH_R2_ANDHI	MATCH_R2_OP (ANDHI)
#define MASK_R2_ANDHI	MASK_R2_OP
#define MATCH_R2_ANDI	MATCH_R2_OP (ANDI)
#define MASK_R2_ANDI	MASK_R2_OP
#define MATCH_R2_ANDI_N	MATCH_R2_OP (ANDI_N)
#define MASK_R2_ANDI_N	MASK_R2_OP
#define MATCH_R2_AND_N	MATCH_R2_R_N (AND_N)
#define MASK_R2_AND_N	MASK_R2_R_N
#define MATCH_R2_BEQ	MATCH_R2_OP (BEQ)
#define MASK_R2_BEQ	MASK_R2_OP
#define MATCH_R2_BEQZ_N	MATCH_R2_OP (BEQZ_N)
#define MASK_R2_BEQZ_N	MASK_R2_OP
#define MATCH_R2_BGE	MATCH_R2_OP (BGE)
#define MASK_R2_BGE	MASK_R2_OP
#define MATCH_R2_BGEU	MATCH_R2_OP (BGEU)
#define MASK_R2_BGEU	MASK_R2_OP
#define MATCH_R2_BGT	MATCH_R2_OP (BLT)
#define MASK_R2_BGT	MASK_R2_OP
#define MATCH_R2_BGTU	MATCH_R2_OP (BLTU)
#define MASK_R2_BGTU	MASK_R2_OP
#define MATCH_R2_BLE	MATCH_R2_OP (BGE)
#define MASK_R2_BLE	MASK_R2_OP
#define MATCH_R2_BLEU	MATCH_R2_OP (BGEU)
#define MASK_R2_BLEU	MASK_R2_OP
#define MATCH_R2_BLT	MATCH_R2_OP (BLT)
#define MASK_R2_BLT	MASK_R2_OP
#define MATCH_R2_BLTU	MATCH_R2_OP (BLTU)
#define MASK_R2_BLTU	MASK_R2_OP
#define MATCH_R2_BNE	MATCH_R2_OP (BNE)
#define MASK_R2_BNE	MASK_R2_OP
#define MATCH_R2_BNEZ_N	MATCH_R2_OP (BNEZ_N)
#define MASK_R2_BNEZ_N	MASK_R2_OP
#define MATCH_R2_BR	MATCH_R2_OP (BR)
#define MASK_R2_BR	MASK_R2_OP | IW_F2I16_A_SHIFTED_MASK | IW_F2I16_B_SHIFTED_MASK
#define MATCH_R2_BREAK	MATCH_R2_OPX (BREAK, 0, 0, 0x1e)
#define MASK_R2_BREAK	MASK_R2_OPX (1, 1, 1, 0)
#define MATCH_R2_BREAK_N	MATCH_R2_R_N (BREAK_N)
#define MASK_R2_BREAK_N	MASK_R2_R_N
#define MATCH_R2_BRET	MATCH_R2_OPX (BRET, 0x1e, 0, 0)
#define MASK_R2_BRET	MASK_R2_OPX (1, 1, 1, 1)
#define MATCH_R2_BR_N	MATCH_R2_OP (BR_N)
#define MASK_R2_BR_N	MASK_R2_OP
#define MATCH_R2_CALL	MATCH_R2_OP (CALL)
#define MASK_R2_CALL	MASK_R2_OP
#define MATCH_R2_CALLR	MATCH_R2_OPX (CALLR, 0, 0, 0x1f)
#define MASK_R2_CALLR	MASK_R2_OPX (0, 1, 1, 1)
#define MATCH_R2_CALLR_N	MATCH_R2_R_N (CALLR_N)
#define MASK_R2_CALLR_N	MASK_R2_R_N
#define MATCH_R2_CMPEQ	MATCH_R2_OPX0 (CMPEQ)
#define MASK_R2_CMPEQ	MASK_R2_OPX0
#define MATCH_R2_CMPEQI	MATCH_R2_OP (CMPEQI)
#define MASK_R2_CMPEQI	MASK_R2_OP
#define MATCH_R2_CMPGE	MATCH_R2_OPX0 (CMPGE)
#define MASK_R2_CMPGE	MASK_R2_OPX0
#define MATCH_R2_CMPGEI	MATCH_R2_OP (CMPGEI)
#define MASK_R2_CMPGEI	MASK_R2_OP
#define MATCH_R2_CMPGEU	MATCH_R2_OPX0 (CMPGEU)
#define MASK_R2_CMPGEU	MASK_R2_OPX0
#define MATCH_R2_CMPGEUI	MATCH_R2_OP (CMPGEUI)
#define MASK_R2_CMPGEUI	MASK_R2_OP
#define MATCH_R2_CMPGT	MATCH_R2_OPX0 (CMPLT)
#define MASK_R2_CMPGT	MASK_R2_OPX0
#define MATCH_R2_CMPGTI	MATCH_R2_OP (CMPGEI)
#define MASK_R2_CMPGTI	MASK_R2_OP
#define MATCH_R2_CMPGTU	MATCH_R2_OPX0 (CMPLTU)
#define MASK_R2_CMPGTU	MASK_R2_OPX0
#define MATCH_R2_CMPGTUI	MATCH_R2_OP (CMPGEUI)
#define MASK_R2_CMPGTUI	MASK_R2_OP
#define MATCH_R2_CMPLE	MATCH_R2_OPX0 (CMPGE)
#define MASK_R2_CMPLE	MASK_R2_OPX0
#define MATCH_R2_CMPLEI	MATCH_R2_OP (CMPLTI)
#define MASK_R2_CMPLEI	MASK_R2_OP
#define MATCH_R2_CMPLEU	MATCH_R2_OPX0 (CMPGEU)
#define MASK_R2_CMPLEU	MASK_R2_OPX0
#define MATCH_R2_CMPLEUI	MATCH_R2_OP (CMPLTUI)
#define MASK_R2_CMPLEUI	MASK_R2_OP
#define MATCH_R2_CMPLT	MATCH_R2_OPX0 (CMPLT)
#define MASK_R2_CMPLT	MASK_R2_OPX0
#define MATCH_R2_CMPLTI	MATCH_R2_OP (CMPLTI)
#define MASK_R2_CMPLTI	MASK_R2_OP
#define MATCH_R2_CMPLTU	MATCH_R2_OPX0 (CMPLTU)
#define MASK_R2_CMPLTU	MASK_R2_OPX0
#define MATCH_R2_CMPLTUI	MATCH_R2_OP (CMPLTUI)
#define MASK_R2_CMPLTUI	MASK_R2_OP
#define MATCH_R2_CMPNE	MATCH_R2_OPX0 (CMPNE)
#define MASK_R2_CMPNE	MASK_R2_OPX0
#define MATCH_R2_CMPNEI	MATCH_R2_OP (CMPNEI)
#define MASK_R2_CMPNEI	MASK_R2_OP
#define MATCH_R2_CUSTOM	MATCH_R2_OP (CUSTOM)
#define MASK_R2_CUSTOM	MASK_R2_OP
#define MATCH_R2_DIV	MATCH_R2_OPX0 (DIV)
#define MASK_R2_DIV	MASK_R2_OPX0
#define MATCH_R2_DIVU	MATCH_R2_OPX0 (DIVU)
#define MASK_R2_DIVU	MASK_R2_OPX0
#define MATCH_R2_ENI	MATCH_R2_OPX (ENI, 0, 0, 0)
#define MASK_R2_ENI	MASK_R2_OPX (1, 1, 1, 0)
#define MATCH_R2_ERET	MATCH_R2_OPX (ERET, 0x1d, 0x1e, 0)
#define MASK_R2_ERET	MASK_R2_OPX (1, 1, 1, 1)
#define MATCH_R2_EXTRACT	MATCH_R2_OPX (EXTRACT, 0, 0, 0)
#define MASK_R2_EXTRACT	MASK_R2_OPX (0, 0, 0, 0)
#define MATCH_R2_FLUSHD	MATCH_R2_DCACHE (FLUSHD)
#define MASK_R2_FLUSHD	MASK_R2_DCACHE
#define MATCH_R2_FLUSHDA	MATCH_R2_DCACHE (FLUSHDA)
#define MASK_R2_FLUSHDA	MASK_R2_DCACHE
#define MATCH_R2_FLUSHI	MATCH_R2_OPX (FLUSHI, 0, 0, 0)
#define MASK_R2_FLUSHI	MASK_R2_OPX (0, 1, 1, 1)
#define MATCH_R2_FLUSHP	MATCH_R2_OPX (FLUSHP, 0, 0, 0)
#define MASK_R2_FLUSHP	MASK_R2_OPX (1, 1, 1, 1)
#define MATCH_R2_INITD	MATCH_R2_DCACHE (INITD)
#define MASK_R2_INITD	MASK_R2_DCACHE
#define MATCH_R2_INITDA	MATCH_R2_DCACHE (INITDA)
#define MASK_R2_INITDA	MASK_R2_DCACHE
#define MATCH_R2_INITI	MATCH_R2_OPX (INITI, 0, 0, 0)
#define MASK_R2_INITI	MASK_R2_OPX (0, 1, 1, 1)
#define MATCH_R2_INSERT	MATCH_R2_OPX (INSERT, 0, 0, 0)
#define MASK_R2_INSERT	MASK_R2_OPX (0, 0, 0, 0)
#define MATCH_R2_JMP	MATCH_R2_OPX (JMP, 0, 0, 0)
#define MASK_R2_JMP	MASK_R2_OPX (0, 1, 1, 1)
#define MATCH_R2_JMPI	MATCH_R2_OP (JMPI)
#define MASK_R2_JMPI	MASK_R2_OP
#define MATCH_R2_JMPR_N	MATCH_R2_R_N (JMPR_N)
#define MASK_R2_JMPR_N	MASK_R2_R_N
#define MATCH_R2_LDB	MATCH_R2_OP (LDB)
#define MASK_R2_LDB	MASK_R2_OP
#define MATCH_R2_LDBIO	MATCH_R2_I12 (LDBIO)
#define MASK_R2_LDBIO	MASK_R2_I12
#define MATCH_R2_LDBU	MATCH_R2_OP (LDBU)
#define MASK_R2_LDBU	MASK_R2_OP
#define MATCH_R2_LDBUIO	MATCH_R2_I12 (LDBUIO)
#define MASK_R2_LDBUIO	MASK_R2_I12
#define MATCH_R2_LDBU_N	MATCH_R2_OP (LDBU_N)
#define MASK_R2_LDBU_N	MASK_R2_OP
#define MATCH_R2_LDEX	MATCH_R2_OPX (LDEX, 0, 0, 0)
#define MASK_R2_LDEX	MASK_R2_OPX (0, 1, 0, 1)
#define MATCH_R2_LDH	MATCH_R2_OP (LDH)
#define MASK_R2_LDH	MASK_R2_OP
#define MATCH_R2_LDHIO	MATCH_R2_I12 (LDHIO)
#define MASK_R2_LDHIO	MASK_R2_I12
#define MATCH_R2_LDHU	MATCH_R2_OP (LDHU)
#define MASK_R2_LDHU	MASK_R2_OP
#define MATCH_R2_LDHUIO	MATCH_R2_I12 (LDHUIO)
#define MASK_R2_LDHUIO	MASK_R2_I12
#define MATCH_R2_LDHU_N	MATCH_R2_OP (LDHU_N)
#define MASK_R2_LDHU_N	MASK_R2_OP
#define MATCH_R2_LDSEX	MATCH_R2_OPX (LDSEX, 0, 0, 0)
#define MASK_R2_LDSEX	MASK_R2_OPX (0, 1, 0, 1)
#define MATCH_R2_LDW	MATCH_R2_OP (LDW)
#define MASK_R2_LDW	MASK_R2_OP
#define MATCH_R2_LDWIO	MATCH_R2_I12 (LDWIO)
#define MASK_R2_LDWIO	MASK_R2_I12
#define MATCH_R2_LDWM	MATCH_R2_I12 (LDWM)
#define MASK_R2_LDWM	MASK_R2_I12
#define MATCH_R2_LDWSP_N	MATCH_R2_OP (LDWSP_N)
#define MASK_R2_LDWSP_N	MASK_R2_OP
#define MATCH_R2_LDW_N	MATCH_R2_OP (LDW_N)
#define MASK_R2_LDW_N	MASK_R2_OP
#define MATCH_R2_MERGE	MATCH_R2_OPX (MERGE, 0, 0, 0)
#define MASK_R2_MERGE	MASK_R2_OPX (0, 0, 0, 0)
#define MATCH_R2_MOV	MATCH_R2_OPX (ADD, 0, 0, 0)
#define MASK_R2_MOV	MASK_R2_OPX (0, 1, 0, 1)
#define MATCH_R2_MOVHI	MATCH_R2_OP (ORHI) | SET_IW_F2I16_A (0)
#define MASK_R2_MOVHI	MASK_R2_OP | IW_F2I16_A_SHIFTED_MASK
#define MATCH_R2_MOVI	MATCH_R2_OP (ADDI) | SET_IW_F2I16_A (0)
#define MASK_R2_MOVI	MASK_R2_OP | IW_F2I16_A_SHIFTED_MASK
#define MATCH_R2_MOVUI	MATCH_R2_OP (ORI) | SET_IW_F2I16_A (0)
#define MASK_R2_MOVUI	MASK_R2_OP | IW_F2I16_A_SHIFTED_MASK
#define MATCH_R2_MOV_N	MATCH_R2_OP (MOV_N)
#define MASK_R2_MOV_N	MASK_R2_OP
#define MATCH_R2_MOVI_N	MATCH_R2_OP (MOVI_N)
#define MASK_R2_MOVI_N	MASK_R2_OP
#define MATCH_R2_MUL	MATCH_R2_OPX0 (MUL)
#define MASK_R2_MUL	MASK_R2_OPX0
#define MATCH_R2_MULI	MATCH_R2_OP (MULI)
#define MASK_R2_MULI	MASK_R2_OP
#define MATCH_R2_MULXSS	MATCH_R2_OPX0 (MULXSS)
#define MASK_R2_MULXSS	MASK_R2_OPX0
#define MATCH_R2_MULXSU	MATCH_R2_OPX0 (MULXSU)
#define MASK_R2_MULXSU	MASK_R2_OPX0
#define MATCH_R2_MULXUU	MATCH_R2_OPX0 (MULXUU)
#define MASK_R2_MULXUU	MASK_R2_OPX0
#define MATCH_R2_NEG_N	MATCH_R2_R_N (NEG_N)
#define MASK_R2_NEG_N	MASK_R2_R_N
#define MATCH_R2_NEXTPC	MATCH_R2_OPX (NEXTPC, 0, 0, 0)
#define MASK_R2_NEXTPC	MASK_R2_OPX (1, 1, 0, 1)
#define MATCH_R2_NOP	MATCH_R2_OPX (ADD, 0, 0, 0)
#define MASK_R2_NOP	MASK_R2_OPX (1, 1, 1, 1)
#define MATCH_R2_NOP_N	(MATCH_R2_OP (MOV_N) | SET_IW_F2_A (0) | SET_IW_F2_B (0))
#define MASK_R2_NOP_N	(MASK_R2_OP | IW_F2_A_SHIFTED_MASK | IW_F2_B_SHIFTED_MASK)
#define MATCH_R2_NOR	MATCH_R2_OPX0 (NOR)
#define MASK_R2_NOR	MASK_R2_OPX0
#define MATCH_R2_NOT_N	MATCH_R2_R_N (NOT_N)
#define MASK_R2_NOT_N	MASK_R2_R_N
#define MATCH_R2_OR	MATCH_R2_OPX0 (OR)
#define MASK_R2_OR	MASK_R2_OPX0
#define MATCH_R2_OR_N	MATCH_R2_R_N (OR_N)
#define MASK_R2_OR_N	MASK_R2_R_N
#define MATCH_R2_ORHI	MATCH_R2_OP (ORHI)
#define MASK_R2_ORHI	MASK_R2_OP
#define MATCH_R2_ORI	MATCH_R2_OP (ORI)
#define MASK_R2_ORI	MASK_R2_OP
#define MATCH_R2_POP_N	(MATCH_R2_OP (PP_N) | SET_IW_L5I4X1_X (R2_PP_N_POP_N))
#define MASK_R2_POP_N	(MASK_R2_OP | IW_L5I4X1_X_SHIFTED_MASK)
#define MATCH_R2_PUSH_N	(MATCH_R2_OP (PP_N) | SET_IW_L5I4X1_X (R2_PP_N_PUSH_N))
#define MASK_R2_PUSH_N	(MASK_R2_OP | IW_L5I4X1_X_SHIFTED_MASK)
#define MATCH_R2_RDCTL	MATCH_R2_OPX (RDCTL, 0, 0, 0)
#define MASK_R2_RDCTL	MASK_R2_OPX (1, 1, 0, 0)
#define MATCH_R2_RDPRS	MATCH_R2_I12 (RDPRS)
#define MASK_R2_RDPRS	MASK_R2_I12
#define MATCH_R2_RET	MATCH_R2_OPX (RET, 0x1f, 0, 0)
#define MASK_R2_RET	MASK_R2_OPX (1, 1, 1, 1)
#define MATCH_R2_RET_N	(MATCH_R2_R_N (RET_N) | SET_IW_X2L5_IMM5 (0))
#define MASK_R2_RET_N	(MASK_R2_R_N | IW_X2L5_IMM5_SHIFTED_MASK)
#define MATCH_R2_ROL	MATCH_R2_OPX0 (ROL)
#define MASK_R2_ROL	MASK_R2_OPX0
#define MATCH_R2_ROLI	MATCH_R2_OPX (ROLI, 0, 0, 0)
#define MASK_R2_ROLI	MASK_R2_OPX (0, 1, 0, 0)
#define MATCH_R2_ROR	MATCH_R2_OPX0 (ROR)
#define MASK_R2_ROR	MASK_R2_OPX0
#define MATCH_R2_SLL	MATCH_R2_OPX0 (SLL)
#define MASK_R2_SLL	MASK_R2_OPX0
#define MATCH_R2_SLLI	MATCH_R2_OPX (SLLI, 0, 0, 0)
#define MASK_R2_SLLI	MASK_R2_OPX (0, 1, 0, 0)
#define MATCH_R2_SLL_N	MATCH_R2_R_N (SLL_N)
#define MASK_R2_SLL_N	MASK_R2_R_N
#define MATCH_R2_SLLI_N	(MATCH_R2_OP (SHI_N) | SET_IW_T2X1L3_X (R2_SHI_N_SLLI_N))
#define MASK_R2_SLLI_N	(MASK_R2_OP | IW_T2X1L3_X_SHIFTED_MASK)
#define MATCH_R2_SPADDI_N	MATCH_R2_OP (SPADDI_N)
#define MASK_R2_SPADDI_N	MASK_R2_OP
#define MATCH_R2_SPDECI_N	(MATCH_R2_OP (SPI_N) | SET_IW_X1I7_X (R2_SPI_N_SPDECI_N))
#define MASK_R2_SPDECI_N	(MASK_R2_OP | IW_X1I7_X_SHIFTED_MASK)
#define MATCH_R2_SPINCI_N	(MATCH_R2_OP (SPI_N) | SET_IW_X1I7_X (R2_SPI_N_SPINCI_N))
#define MASK_R2_SPINCI_N	(MASK_R2_OP | IW_X1I7_X_SHIFTED_MASK)
#define MATCH_R2_SRA	MATCH_R2_OPX0 (SRA)
#define MASK_R2_SRA	MASK_R2_OPX0
#define MATCH_R2_SRAI	MATCH_R2_OPX (SRAI, 0, 0, 0)
#define MASK_R2_SRAI	MASK_R2_OPX (0, 1, 0, 0)
#define MATCH_R2_SRL	MATCH_R2_OPX0 (SRL)
#define MASK_R2_SRL	MASK_R2_OPX0
#define MATCH_R2_SRLI	MATCH_R2_OPX (SRLI, 0, 0, 0)
#define MASK_R2_SRLI	MASK_R2_OPX (0, 1, 0, 0)
#define MATCH_R2_SRL_N	MATCH_R2_R_N (SRL_N)
#define MASK_R2_SRL_N	MASK_R2_R_N
#define MATCH_R2_SRLI_N	(MATCH_R2_OP (SHI_N) | SET_IW_T2X1L3_X (R2_SHI_N_SRLI_N))
#define MASK_R2_SRLI_N	(MASK_R2_OP | IW_T2X1L3_X_SHIFTED_MASK)
#define MATCH_R2_STB	MATCH_R2_OP (STB)
#define MASK_R2_STB	MASK_R2_OP
#define MATCH_R2_STBIO	MATCH_R2_I12 (STBIO)
#define MASK_R2_STBIO	MASK_R2_I12
#define MATCH_R2_STB_N	MATCH_R2_OP (STB_N)
#define MASK_R2_STB_N	MASK_R2_OP
#define MATCH_R2_STBZ_N	(MATCH_R2_OP (STZ_N) | SET_IW_T1X1I6_X (R2_STZ_N_STBZ_N))
#define MASK_R2_STBZ_N	(MASK_R2_OP | IW_T1X1I6_X_SHIFTED_MASK)
#define MATCH_R2_STEX	MATCH_R2_OPX0 (STEX)
#define MASK_R2_STEX	MASK_R2_OPX0
#define MATCH_R2_STH	MATCH_R2_OP (STH)
#define MASK_R2_STH	MASK_R2_OP
#define MATCH_R2_STHIO	MATCH_R2_I12 (STHIO)
#define MASK_R2_STHIO	MASK_R2_I12
#define MATCH_R2_STH_N	MATCH_R2_OP (STH_N)
#define MASK_R2_STH_N	MASK_R2_OP
#define MATCH_R2_STSEX	MATCH_R2_OPX0 (STSEX)
#define MASK_R2_STSEX	MASK_R2_OPX0
#define MATCH_R2_STW	MATCH_R2_OP (STW)
#define MASK_R2_STW	MASK_R2_OP
#define MATCH_R2_STWIO	MATCH_R2_I12 (STWIO)
#define MASK_R2_STWIO	MASK_R2_I12
#define MATCH_R2_STWM	MATCH_R2_I12 (STWM)
#define MASK_R2_STWM	MASK_R2_I12
#define MATCH_R2_STWSP_N	MATCH_R2_OP (STWSP_N)
#define MASK_R2_STWSP_N	MASK_R2_OP
#define MATCH_R2_STW_N	MATCH_R2_OP (STW_N)
#define MASK_R2_STW_N	MASK_R2_OP
#define MATCH_R2_STWZ_N	MATCH_R2_OP (STZ_N)
#define MASK_R2_STWZ_N	MASK_R2_OP
#define MATCH_R2_SUB	MATCH_R2_OPX0 (SUB)
#define MASK_R2_SUB	MASK_R2_OPX0
#define MATCH_R2_SUBI	MATCH_R2_OP (ADDI)
#define MASK_R2_SUBI	MASK_R2_OP
#define MATCH_R2_SUB_N	(MATCH_R2_OP (AS_N) | SET_IW_T3X1_X (R2_AS_N_SUB_N))
#define MASK_R2_SUB_N	(MASK_R2_OP | IW_T3X1_X_SHIFTED_MASK)
#define MATCH_R2_SUBI_N	(MATCH_R2_OP (ASI_N) | SET_IW_T2X1I3_X (R2_ASI_N_SUBI_N))
#define MASK_R2_SUBI_N	(MASK_R2_OP | IW_T2X1I3_X_SHIFTED_MASK)
#define MATCH_R2_SYNC	MATCH_R2_OPX (SYNC, 0, 0, 0)
#define MASK_R2_SYNC	MASK_R2_OPX (1, 1, 1, 1)
#define MATCH_R2_TRAP	MATCH_R2_OPX (TRAP, 0, 0, 0x1d)
#define MASK_R2_TRAP	MASK_R2_OPX (1, 1, 1, 0)
#define MATCH_R2_TRAP_N	MATCH_R2_R_N (TRAP_N)
#define MASK_R2_TRAP_N	MASK_R2_R_N
#define MATCH_R2_WRCTL	MATCH_R2_OPX (WRCTL, 0, 0, 0)
#define MASK_R2_WRCTL	MASK_R2_OPX (0, 1, 1, 0)
#define MATCH_R2_WRPIE	MATCH_R2_OPX (WRPIE, 0, 0, 0)
#define MASK_R2_WRPIE	MASK_R2_OPX (0, 1, 0, 1)
#define MATCH_R2_WRPRS	MATCH_R2_OPX (WRPRS, 0, 0, 0)
#define MASK_R2_WRPRS	MASK_R2_OPX (0, 1, 0, 1)
#define MATCH_R2_XOR	MATCH_R2_OPX0 (XOR)
#define MASK_R2_XOR	MASK_R2_OPX0
#define MATCH_R2_XORHI	MATCH_R2_OP (XORHI)
#define MASK_R2_XORHI	MASK_R2_OP
#define MATCH_R2_XORI	MATCH_R2_OP (XORI)
#define MASK_R2_XORI	MASK_R2_OP
#define MATCH_R2_XOR_N	MATCH_R2_R_N (XOR_N)
#define MASK_R2_XOR_N	MASK_R2_R_N

#endif /* _NIOS2R2_H */


/* These are the data structures used to hold the instruction information.  */
extern const struct nios2_opcode nios2_r1_opcodes[];
extern const int nios2_num_r1_opcodes;
extern const struct nios2_opcode nios2_r2_opcodes[];
extern const int nios2_num_r2_opcodes;
extern struct nios2_opcode *nios2_opcodes;
extern int nios2_num_opcodes;

/* These are the data structures used to hold the register information.  */
extern const struct nios2_reg nios2_builtin_regs[];
extern struct nios2_reg *nios2_regs;
extern const int nios2_num_builtin_regs;
extern int nios2_num_regs;

/* Return the opcode descriptor for a single instruction.  */
extern const struct nios2_opcode *
nios2_find_opcode_hash (unsigned long, unsigned long);

/* Lookup tables for R2 immediate decodings.  */
extern unsigned int nios2_r2_asi_n_mappings[];
extern const int nios2_num_r2_asi_n_mappings;
extern unsigned int nios2_r2_shi_n_mappings[];
extern const int nios2_num_r2_shi_n_mappings;
extern unsigned int nios2_r2_andi_n_mappings[];
extern const int nios2_num_r2_andi_n_mappings;

/* Lookup table for 3-bit register decodings.  */
extern int nios2_r2_reg3_mappings[];
extern const int nios2_num_r2_reg3_mappings;

/* Lookup table for REG_RANGE value list decodings.  */
extern unsigned long nios2_r2_reg_range_mappings[];
extern const int nios2_num_r2_reg_range_mappings;

#endif /* _NIOS2_H */

/*#include "sysdep.h"
#include <stdio.h>
#include "opcode/nios2.h"
*/
/* Register string table */

const struct nios2_reg nios2_builtin_regs[] = {
  /* Standard register names.  */
  {"zero", 0, REG_NORMAL},
  {"at", 1, REG_NORMAL},			/* assembler temporary */
  {"r2", 2, REG_NORMAL | REG_3BIT | REG_LDWM},
  {"r3", 3, REG_NORMAL | REG_3BIT | REG_LDWM},
  {"r4", 4, REG_NORMAL | REG_3BIT | REG_LDWM},
  {"r5", 5, REG_NORMAL | REG_3BIT | REG_LDWM},
  {"r6", 6, REG_NORMAL | REG_3BIT | REG_LDWM},
  {"r7", 7, REG_NORMAL | REG_3BIT | REG_LDWM},
  {"r8", 8, REG_NORMAL | REG_LDWM},
  {"r9", 9, REG_NORMAL | REG_LDWM},
  {"r10", 10, REG_NORMAL | REG_LDWM},
  {"r11", 11, REG_NORMAL | REG_LDWM},
  {"r12", 12, REG_NORMAL | REG_LDWM},
  {"r13", 13, REG_NORMAL | REG_LDWM},
  {"r14", 14, REG_NORMAL | REG_LDWM},
  {"r15", 15, REG_NORMAL | REG_LDWM},
  {"r16", 16, REG_NORMAL | REG_3BIT | REG_LDWM | REG_POP},
  {"r17", 17, REG_NORMAL | REG_3BIT | REG_LDWM | REG_POP},
  {"r18", 18, REG_NORMAL | REG_LDWM | REG_POP},
  {"r19", 19, REG_NORMAL | REG_LDWM | REG_POP},
  {"r20", 20, REG_NORMAL | REG_LDWM | REG_POP},
  {"r21", 21, REG_NORMAL | REG_LDWM | REG_POP},
  {"r22", 22, REG_NORMAL | REG_LDWM | REG_POP},
  {"r23", 23, REG_NORMAL | REG_LDWM | REG_POP},
  {"et", 24, REG_NORMAL},
  {"bt", 25, REG_NORMAL},
  {"gp", 26, REG_NORMAL},			/* global pointer */
  {"sp", 27, REG_NORMAL},			/* stack pointer */
  {"fp", 28, REG_NORMAL | REG_LDWM | REG_POP},	/* frame pointer */
  {"ea", 29, REG_NORMAL},			/* exception return address */
  {"sstatus", 30, REG_NORMAL},			/* saved processor status */
  {"ra", 31, REG_NORMAL | REG_LDWM | REG_POP},	/* return address */

  /* Alternative names for special registers.  */
  {"r0", 0, REG_NORMAL},
  {"r1", 1, REG_NORMAL},
  {"r24", 24, REG_NORMAL},
  {"r25", 25, REG_NORMAL},
  {"r26", 26, REG_NORMAL},
  {"r27", 27, REG_NORMAL},
  {"r28", 28, REG_NORMAL | REG_LDWM | REG_POP},
  {"r29", 29, REG_NORMAL},
  {"r30", 30, REG_NORMAL},
  {"ba", 30, REG_NORMAL},			/* breakpoint return address */
  {"r31", 31, REG_NORMAL | REG_LDWM | REG_POP},

  /* Control register names.  */
  {"status", 0, REG_CONTROL},
  {"estatus", 1, REG_CONTROL},
  {"bstatus", 2, REG_CONTROL},
  {"ienable", 3, REG_CONTROL},
  {"ipending", 4, REG_CONTROL},
  {"cpuid", 5, REG_CONTROL},
  {"ctl6", 6, REG_CONTROL},
  {"exception", 7, REG_CONTROL},
  {"pteaddr", 8, REG_CONTROL},
  {"tlbacc", 9, REG_CONTROL},
  {"tlbmisc", 10, REG_CONTROL},
  {"eccinj", 11, REG_CONTROL},
  {"badaddr", 12, REG_CONTROL},
  {"config", 13, REG_CONTROL},
  {"mpubase", 14, REG_CONTROL},
  {"mpuacc", 15, REG_CONTROL},
  {"ctl16", 16, REG_CONTROL},
  {"ctl17", 17, REG_CONTROL},
  {"ctl18", 18, REG_CONTROL},
  {"ctl19", 19, REG_CONTROL},
  {"ctl20", 20, REG_CONTROL},
  {"ctl21", 21, REG_CONTROL},
  {"ctl22", 22, REG_CONTROL},
  {"ctl23", 23, REG_CONTROL},
  {"ctl24", 24, REG_CONTROL},
  {"ctl25", 25, REG_CONTROL},
  {"ctl26", 26, REG_CONTROL},
  {"ctl27", 27, REG_CONTROL},
  {"ctl28", 28, REG_CONTROL},
  {"ctl29", 29, REG_CONTROL},
  {"ctl30", 30, REG_CONTROL},
  {"ctl31", 31, REG_CONTROL},

  /* Alternative names for special control registers.  */
  {"ctl0", 0, REG_CONTROL},
  {"ctl1", 1, REG_CONTROL},
  {"ctl2", 2, REG_CONTROL},
  {"ctl3", 3, REG_CONTROL},
  {"ctl4", 4, REG_CONTROL},
  {"ctl5", 5, REG_CONTROL},
  {"ctl7", 7, REG_CONTROL},
  {"ctl8", 8, REG_CONTROL},
  {"ctl9", 9, REG_CONTROL},
  {"ctl10", 10, REG_CONTROL},
  {"ctl11", 11, REG_CONTROL},
  {"ctl12", 12, REG_CONTROL},
  {"ctl13", 13, REG_CONTROL},
  {"ctl14", 14, REG_CONTROL},
  {"ctl15", 15, REG_CONTROL},

  /* Coprocessor register names.  */
  {"c0", 0, REG_COPROCESSOR},
  {"c1", 1, REG_COPROCESSOR},
  {"c2", 2, REG_COPROCESSOR},
  {"c3", 3, REG_COPROCESSOR},
  {"c4", 4, REG_COPROCESSOR},
  {"c5", 5, REG_COPROCESSOR},
  {"c6", 6, REG_COPROCESSOR},
  {"c7", 7, REG_COPROCESSOR},
  {"c8", 8, REG_COPROCESSOR},
  {"c9", 9, REG_COPROCESSOR},
  {"c10", 10, REG_COPROCESSOR},
  {"c11", 11, REG_COPROCESSOR},
  {"c12", 12, REG_COPROCESSOR},
  {"c13", 13, REG_COPROCESSOR},
  {"c14", 14, REG_COPROCESSOR},
  {"c15", 15, REG_COPROCESSOR},
  {"c16", 16, REG_COPROCESSOR},
  {"c17", 17, REG_COPROCESSOR},
  {"c18", 18, REG_COPROCESSOR},
  {"c19", 19, REG_COPROCESSOR},
  {"c20", 20, REG_COPROCESSOR},
  {"c21", 21, REG_COPROCESSOR},
  {"c22", 22, REG_COPROCESSOR},
  {"c23", 23, REG_COPROCESSOR},
  {"c24", 24, REG_COPROCESSOR},
  {"c25", 25, REG_COPROCESSOR},
  {"c26", 26, REG_COPROCESSOR},
  {"c27", 27, REG_COPROCESSOR},
  {"c28", 28, REG_COPROCESSOR},
  {"c29", 29, REG_COPROCESSOR},
  {"c30", 30, REG_COPROCESSOR},
  {"c31", 31, REG_COPROCESSOR},
};

#define NIOS2_NUM_REGS \
       ((sizeof nios2_builtin_regs) / (sizeof (nios2_builtin_regs[0])))
const int nios2_num_builtin_regs = NIOS2_NUM_REGS;

/* This is not const in order to allow for dynamic extensions to the
   built-in instruction set.  */
struct nios2_reg *nios2_regs = (struct nios2_reg *) nios2_builtin_regs;
int nios2_num_regs = NIOS2_NUM_REGS;
#undef NIOS2_NUM_REGS

/* This is the opcode table used by the Nios II GNU as, disassembler
   and GDB.  */
const struct nios2_opcode nios2_r1_opcodes[] =
{
  /* { name, args, args_test, num_args, size, format,
       match, mask, pinfo, overflow } */
  {"add", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_ADD, MASK_R1_ADD, 0, no_overflow},
  {"addi", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_ADDI, MASK_R1_ADDI, 0, signed_immed16_overflow},
  {"and", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_AND, MASK_R1_AND, 0, no_overflow},
  {"andhi", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_ANDHI, MASK_R1_ANDHI, 0, unsigned_immed16_overflow},
  {"andi", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_ANDI, MASK_R1_ANDI, 0, unsigned_immed16_overflow},
  {"beq", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BEQ, MASK_R1_BEQ, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bge", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BGE, MASK_R1_BGE, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bgeu", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BGEU, MASK_R1_BGEU, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bgt", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BGT, MASK_R1_BGT,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bgtu", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BGTU, MASK_R1_BGTU,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"ble", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BLE, MASK_R1_BLE,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bleu", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BLEU, MASK_R1_BLEU,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"blt", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BLT, MASK_R1_BLT, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bltu", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BLTU, MASK_R1_BLTU, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bne", "s,t,o", "s,t,o,E", 3, 4, iw_i_type,
   MATCH_R1_BNE, MASK_R1_BNE, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"br", "o", "o,E", 1, 4, iw_i_type,
   MATCH_R1_BR, MASK_R1_BR, NIOS2_INSN_UBRANCH, branch_target_overflow},
  {"break", "j", "j,E", 1, 4, iw_r_type,
   MATCH_R1_BREAK, MASK_R1_BREAK, NIOS2_INSN_OPTARG, no_overflow},
  {"bret", "", "E", 0, 4, iw_r_type,
   MATCH_R1_BRET, MASK_R1_BRET, 0, no_overflow},
  {"call", "m", "m,E", 1, 4, iw_j_type,
   MATCH_R1_CALL, MASK_R1_CALL, NIOS2_INSN_CALL, call_target_overflow},
  {"callr", "s", "s,E", 1, 4, iw_r_type,
   MATCH_R1_CALLR, MASK_R1_CALLR, 0, no_overflow},
  {"cmpeq", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPEQ, MASK_R1_CMPEQ, 0, no_overflow},
  {"cmpeqi", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_CMPEQI, MASK_R1_CMPEQI, 0, signed_immed16_overflow},
  {"cmpge", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPGE, MASK_R1_CMPGE, 0, no_overflow},
  {"cmpgei", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_CMPGEI, MASK_R1_CMPGEI, 0, signed_immed16_overflow},
  {"cmpgeu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPGEU, MASK_R1_CMPGEU, 0, no_overflow},
  {"cmpgeui", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_CMPGEUI, MASK_R1_CMPGEUI, 0, unsigned_immed16_overflow},
  {"cmpgt", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPGT, MASK_R1_CMPGT, NIOS2_INSN_MACRO, no_overflow},
  {"cmpgti", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_CMPGTI, MASK_R1_CMPGTI, NIOS2_INSN_MACRO, signed_immed16_overflow},
  {"cmpgtu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPGTU, MASK_R1_CMPGTU, NIOS2_INSN_MACRO, no_overflow},
  {"cmpgtui", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_CMPGTUI, MASK_R1_CMPGTUI,
   NIOS2_INSN_MACRO, unsigned_immed16_overflow},
  {"cmple", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPLE, MASK_R1_CMPLE, NIOS2_INSN_MACRO, no_overflow},
  {"cmplei", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_CMPLEI, MASK_R1_CMPLEI, NIOS2_INSN_MACRO, signed_immed16_overflow},
  {"cmpleu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPLEU, MASK_R1_CMPLEU, NIOS2_INSN_MACRO, no_overflow},
  {"cmpleui", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_CMPLEUI, MASK_R1_CMPLEUI,
   NIOS2_INSN_MACRO, unsigned_immed16_overflow},
  {"cmplt", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPLT, MASK_R1_CMPLT, 0, no_overflow},
  {"cmplti", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_CMPLTI, MASK_R1_CMPLTI, 0, signed_immed16_overflow},
  {"cmpltu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPLTU, MASK_R1_CMPLTU, 0, no_overflow},
  {"cmpltui", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_CMPLTUI, MASK_R1_CMPLTUI, 0, unsigned_immed16_overflow},
  {"cmpne", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_CMPNE, MASK_R1_CMPNE, 0, no_overflow},
  {"cmpnei", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_CMPNEI, MASK_R1_CMPNEI, 0, signed_immed16_overflow},
  {"custom", "l,d,s,t", "l,d,s,t,E", 4, 4, iw_custom_type,
   MATCH_R1_CUSTOM, MASK_R1_CUSTOM, 0, custom_opcode_overflow},
  {"div", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_DIV, MASK_R1_DIV, 0, no_overflow},
  {"divu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_DIVU, MASK_R1_DIVU, 0, no_overflow},
  {"eret", "", "E", 0, 4, iw_r_type,
   MATCH_R1_ERET, MASK_R1_ERET, 0, no_overflow},
  {"flushd", "i(s)", "i(s),E", 2, 4, iw_i_type,
   MATCH_R1_FLUSHD, MASK_R1_FLUSHD, 0, address_offset_overflow},
  {"flushda", "i(s)", "i(s),E", 2, 4, iw_i_type,
   MATCH_R1_FLUSHDA, MASK_R1_FLUSHDA, 0, address_offset_overflow},
  {"flushi", "s", "s,E", 1, 4, iw_r_type,
   MATCH_R1_FLUSHI, MASK_R1_FLUSHI, 0, no_overflow},
  {"flushp", "", "E", 0, 4, iw_r_type,
   MATCH_R1_FLUSHP, MASK_R1_FLUSHP, 0, no_overflow},
  {"initd", "i(s)", "i(s),E", 2, 4, iw_i_type,
   MATCH_R1_INITD, MASK_R1_INITD, 0, address_offset_overflow},
  {"initda", "i(s)", "i(s),E", 2, 4, iw_i_type,
   MATCH_R1_INITDA, MASK_R1_INITDA, 0, address_offset_overflow},
  {"initi", "s", "s,E", 1, 4, iw_r_type,
   MATCH_R1_INITI, MASK_R1_INITI, 0, no_overflow},
  {"jmp", "s", "s,E", 1, 4, iw_r_type,
   MATCH_R1_JMP, MASK_R1_JMP, 0, no_overflow},
  {"jmpi", "m", "m,E", 1, 4, iw_j_type,
   MATCH_R1_JMPI, MASK_R1_JMPI, 0, call_target_overflow},
  {"ldb", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDB, MASK_R1_LDB, 0, address_offset_overflow},
  {"ldbio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDBIO, MASK_R1_LDBIO, 0, address_offset_overflow},
  {"ldbu", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDBU, MASK_R1_LDBU, 0, address_offset_overflow},
  {"ldbuio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDBUIO, MASK_R1_LDBUIO, 0, address_offset_overflow},
  {"ldh", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDH, MASK_R1_LDH, 0, address_offset_overflow},
  {"ldhio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDHIO, MASK_R1_LDHIO, 0, address_offset_overflow},
  {"ldhu", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDHU, MASK_R1_LDHU, 0, address_offset_overflow},
  {"ldhuio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDHUIO, MASK_R1_LDHUIO, 0, address_offset_overflow},
  {"ldw", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDW, MASK_R1_LDW, 0, address_offset_overflow},
  {"ldwio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_LDWIO, MASK_R1_LDWIO, 0, address_offset_overflow},
  {"mov", "d,s", "d,s,E", 2, 4, iw_r_type,
   MATCH_R1_MOV, MASK_R1_MOV, NIOS2_INSN_MACRO_MOV, no_overflow},
  {"movhi", "t,u", "t,u,E", 2, 4, iw_i_type,
   MATCH_R1_MOVHI, MASK_R1_MOVHI,
   NIOS2_INSN_MACRO_MOVI, unsigned_immed16_overflow},
  {"movi", "t,i", "t,i,E", 2, 4, iw_i_type,
   MATCH_R1_MOVI, MASK_R1_MOVI, NIOS2_INSN_MACRO_MOVI, signed_immed16_overflow},
  {"movia", "t,o", "t,o,E", 2, 4, iw_i_type,
   MATCH_R1_ORHI, MASK_R1_ORHI, NIOS2_INSN_MACRO_MOVIA, no_overflow},
  {"movui", "t,u", "t,u,E", 2, 4, iw_i_type,
   MATCH_R1_MOVUI, MASK_R1_MOVUI,
   NIOS2_INSN_MACRO_MOVI, unsigned_immed16_overflow},
  {"mul", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_MUL, MASK_R1_MUL, 0, no_overflow},
  {"muli", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_MULI, MASK_R1_MULI, 0, signed_immed16_overflow},
  {"mulxss", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_MULXSS, MASK_R1_MULXSS, 0, no_overflow},
  {"mulxsu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_MULXSU, MASK_R1_MULXSU, 0, no_overflow},
  {"mulxuu", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_MULXUU, MASK_R1_MULXUU, 0, no_overflow},
  {"nextpc", "d", "d,E", 1, 4, iw_r_type,
   MATCH_R1_NEXTPC, MASK_R1_NEXTPC, 0, no_overflow},
  {"nop", "", "E", 0, 4, iw_r_type,
   MATCH_R1_NOP, MASK_R1_NOP, NIOS2_INSN_MACRO_MOV, no_overflow},
  {"nor", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_NOR, MASK_R1_NOR, 0, no_overflow},
  {"or", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_OR, MASK_R1_OR, 0, no_overflow},
  {"orhi", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_ORHI, MASK_R1_ORHI, 0, unsigned_immed16_overflow},
  {"ori", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_ORI, MASK_R1_ORI, 0, unsigned_immed16_overflow},
  {"rdctl", "d,c", "d,c,E", 2, 4, iw_r_type,
   MATCH_R1_RDCTL, MASK_R1_RDCTL, 0, no_overflow},
  {"rdprs", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_RDPRS, MASK_R1_RDPRS, 0, signed_immed16_overflow},
  {"ret", "", "E", 0, 4, iw_r_type,
   MATCH_R1_RET, MASK_R1_RET, 0, no_overflow},
  {"rol", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_ROL, MASK_R1_ROL, 0, no_overflow},
  {"roli", "d,s,j", "d,s,j,E", 3, 4, iw_r_type,
   MATCH_R1_ROLI, MASK_R1_ROLI, 0, unsigned_immed5_overflow},
  {"ror", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_ROR, MASK_R1_ROR, 0, no_overflow},
  {"sll", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_SLL, MASK_R1_SLL, 0, no_overflow},
  {"slli", "d,s,j", "d,s,j,E", 3, 4, iw_r_type,
   MATCH_R1_SLLI, MASK_R1_SLLI, 0, unsigned_immed5_overflow},
  {"sra", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_SRA, MASK_R1_SRA, 0, no_overflow},
  {"srai", "d,s,j", "d,s,j,E", 3, 4, iw_r_type,
   MATCH_R1_SRAI, MASK_R1_SRAI, 0, unsigned_immed5_overflow},
  {"srl", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_SRL, MASK_R1_SRL, 0, no_overflow},
  {"srli", "d,s,j", "d,s,j,E", 3, 4, iw_r_type,
   MATCH_R1_SRLI, MASK_R1_SRLI, 0, unsigned_immed5_overflow},
  {"stb", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_STB, MASK_R1_STB, 0, address_offset_overflow},
  {"stbio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_STBIO, MASK_R1_STBIO, 0, address_offset_overflow},
  {"sth", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_STH, MASK_R1_STH, 0, address_offset_overflow},
  {"sthio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_STHIO, MASK_R1_STHIO, 0, address_offset_overflow},
  {"stw", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_STW, MASK_R1_STW, 0, address_offset_overflow},
  {"stwio", "t,i(s)", "t,i(s),E", 3, 4, iw_i_type,
   MATCH_R1_STWIO, MASK_R1_STWIO, 0, address_offset_overflow},
  {"sub", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_SUB, MASK_R1_SUB, 0, no_overflow},
  {"subi", "t,s,i", "t,s,i,E", 3, 4, iw_i_type,
   MATCH_R1_SUBI, MASK_R1_SUBI, NIOS2_INSN_MACRO, signed_immed16_overflow},
  {"sync", "", "E", 0, 4, iw_r_type,
   MATCH_R1_SYNC, MASK_R1_SYNC, 0, no_overflow},
  {"trap", "j", "j,E", 1, 4, iw_r_type,
   MATCH_R1_TRAP, MASK_R1_TRAP, NIOS2_INSN_OPTARG, no_overflow},
  {"wrctl", "c,s", "c,s,E", 2, 4, iw_r_type,
   MATCH_R1_WRCTL, MASK_R1_WRCTL, 0, no_overflow},
  {"wrprs", "d,s", "d,s,E", 2, 4, iw_r_type,
   MATCH_R1_WRPRS, MASK_R1_WRPRS, 0, no_overflow},
  {"xor", "d,s,t", "d,s,t,E", 3, 4, iw_r_type,
   MATCH_R1_XOR, MASK_R1_XOR, 0, no_overflow},
  {"xorhi", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_XORHI, MASK_R1_XORHI, 0, unsigned_immed16_overflow},
  {"xori", "t,s,u", "t,s,u,E", 3, 4, iw_i_type,
   MATCH_R1_XORI, MASK_R1_XORI, 0, unsigned_immed16_overflow}
};

#define NIOS2_NUM_R1_OPCODES \
       ((sizeof nios2_r1_opcodes) / (sizeof (nios2_r1_opcodes[0])))
const int nios2_num_r1_opcodes = NIOS2_NUM_R1_OPCODES;


const struct nios2_opcode nios2_r2_opcodes[] =
{
  /* { name, args, args_test, num_args, size, format,
       match, mask, pinfo, overflow } */
  {"add", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_ADD, MASK_R2_ADD, 0, no_overflow},
  {"addi", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ADDI, MASK_R2_ADDI, 0, signed_immed16_overflow},
  {"add.n", "D,S,T", "D,S,T,E", 3, 2, iw_T3X1_type,
   MATCH_R2_ADD_N, MASK_R2_ADD_N, 0, no_overflow},
  {"addi.n", "D,S,e", "D,S,e,E", 3, 2, iw_T2X1I3_type,
   MATCH_R2_ADDI_N, MASK_R2_ADDI_N, 0, enumeration_overflow},
  {"and", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_AND, MASK_R2_AND, 0, no_overflow},
  {"andchi", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ANDCHI, MASK_R2_ANDCHI, 0, unsigned_immed16_overflow},
  {"andci", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ANDCI, MASK_R2_ANDCI, 0, unsigned_immed16_overflow},
  {"andhi", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ANDHI, MASK_R2_ANDHI, 0, unsigned_immed16_overflow},
  {"andi", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ANDI, MASK_R2_ANDI, 0, unsigned_immed16_overflow},
  {"andi.n", "T,S,g", "T,S,g,E", 3, 2, iw_T2I4_type,
   MATCH_R2_ANDI_N, MASK_R2_ANDI_N, 0, enumeration_overflow},
  {"and.n", "D,S,T", "D,S,T,E", 3, 2, iw_T2X3_type,
   MATCH_R2_AND_N, MASK_R2_AND_N, 0, no_overflow},
  {"beq", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BEQ, MASK_R2_BEQ, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"beqz.n", "S,P", "S,P,E", 2, 2, iw_T1I7_type,
   MATCH_R2_BEQZ_N, MASK_R2_BEQZ_N, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bge", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BGE, MASK_R2_BGE, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bgeu", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BGEU, MASK_R2_BGEU, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bgt", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BGT, MASK_R2_BGT,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bgtu", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BGTU, MASK_R2_BGTU,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"ble", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BLE, MASK_R2_BLE,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bleu", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BLEU, MASK_R2_BLEU,
   NIOS2_INSN_MACRO|NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"blt", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BLT, MASK_R2_BLT, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bltu", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BLTU, MASK_R2_BLTU, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bne", "s,t,o", "s,t,o,E", 3, 4, iw_F2I16_type,
   MATCH_R2_BNE, MASK_R2_BNE, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"bnez.n", "S,P", "S,P,E", 2, 2, iw_T1I7_type,
   MATCH_R2_BNEZ_N, MASK_R2_BNEZ_N, NIOS2_INSN_CBRANCH, branch_target_overflow},
  {"br", "o", "o,E", 1, 4, iw_F2I16_type,
   MATCH_R2_BR, MASK_R2_BR, NIOS2_INSN_UBRANCH, branch_target_overflow},
  {"break", "j", "j,E", 1, 4, iw_F3X6L5_type,
   MATCH_R2_BREAK, MASK_R2_BREAK, NIOS2_INSN_OPTARG, no_overflow},
  {"break.n", "j", "j,E", 1, 2, iw_X2L5_type,
   MATCH_R2_BREAK_N, MASK_R2_BREAK_N, NIOS2_INSN_OPTARG, no_overflow},
  {"bret", "", "E", 0, 4, iw_F3X6_type,
   MATCH_R2_BRET, MASK_R2_BRET, 0, no_overflow},
  {"br.n", "O", "O,E", 1, 2, iw_I10_type,
   MATCH_R2_BR_N, MASK_R2_BR_N, NIOS2_INSN_UBRANCH, branch_target_overflow},
  {"call", "m", "m,E", 1, 4, iw_L26_type,
   MATCH_R2_CALL, MASK_R2_CALL, NIOS2_INSN_CALL, call_target_overflow},
  {"callr", "s", "s,E", 1, 4, iw_F3X6_type,
   MATCH_R2_CALLR, MASK_R2_CALLR, 0, no_overflow},
  {"callr.n", "s", "s,E", 1, 2, iw_F1X1_type,
   MATCH_R2_CALLR_N, MASK_R2_CALLR_N, 0, no_overflow},
  {"cmpeq", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPEQ, MASK_R2_CMPEQ, 0, no_overflow},
  {"cmpeqi", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPEQI, MASK_R2_CMPEQI, 0, signed_immed16_overflow},
  {"cmpge", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPGE, MASK_R2_CMPGE, 0, no_overflow},
  {"cmpgei", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPGEI, MASK_R2_CMPGEI, 0, signed_immed16_overflow},
  {"cmpgeu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPGEU, MASK_R2_CMPGEU, 0, no_overflow},
  {"cmpgeui", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPGEUI, MASK_R2_CMPGEUI, 0, unsigned_immed16_overflow},
  {"cmpgt", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPGT, MASK_R2_CMPGT, NIOS2_INSN_MACRO, no_overflow},
  {"cmpgti", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPGTI, MASK_R2_CMPGTI, NIOS2_INSN_MACRO, signed_immed16_overflow},
  {"cmpgtu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPGTU, MASK_R2_CMPGTU, NIOS2_INSN_MACRO, no_overflow},
  {"cmpgtui", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPGTUI, MASK_R2_CMPGTUI,
   NIOS2_INSN_MACRO, unsigned_immed16_overflow},
  {"cmple", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPLE, MASK_R2_CMPLE, NIOS2_INSN_MACRO, no_overflow},
  {"cmplei", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPLEI, MASK_R2_CMPLEI, NIOS2_INSN_MACRO, signed_immed16_overflow},
  {"cmpleu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPLEU, MASK_R2_CMPLEU, NIOS2_INSN_MACRO, no_overflow},
  {"cmpleui", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPLEUI, MASK_R2_CMPLEUI,
   NIOS2_INSN_MACRO, unsigned_immed16_overflow},
  {"cmplt", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPLT, MASK_R2_CMPLT, 0, no_overflow},
  {"cmplti", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPLTI, MASK_R2_CMPLTI, 0, signed_immed16_overflow},
  {"cmpltu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPLTU, MASK_R2_CMPLTU, 0, no_overflow},
  {"cmpltui", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPLTUI, MASK_R2_CMPLTUI, 0, unsigned_immed16_overflow},
  {"cmpne", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_CMPNE, MASK_R2_CMPNE, 0, no_overflow},
  {"cmpnei", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_CMPNEI, MASK_R2_CMPNEI, 0, signed_immed16_overflow},
  {"custom", "l,d,s,t", "l,d,s,t,E", 4, 4, iw_F3X8_type,
   MATCH_R2_CUSTOM, MASK_R2_CUSTOM, 0, custom_opcode_overflow},
  {"div", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_DIV, MASK_R2_DIV, 0, no_overflow},
  {"divu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_DIVU, MASK_R2_DIVU, 0, no_overflow},
  {"eni", "j", "j,E", 1, 4, iw_F3X6L5_type,
   MATCH_R2_ENI, MASK_R2_ENI, NIOS2_INSN_OPTARG, no_overflow},
  {"eret", "", "E", 0, 4, iw_F3X6_type,
   MATCH_R2_ERET, MASK_R2_ERET, 0, no_overflow},
  {"extract", "t,s,j,k", "t,s,j,k,E", 4, 4, iw_F2X6L10_type,
   MATCH_R2_EXTRACT, MASK_R2_EXTRACT, 0, no_overflow},
  {"flushd", "I(s)", "I(s),E", 2, 4, iw_F1X4I12_type,
   MATCH_R2_FLUSHD, MASK_R2_FLUSHD, 0, address_offset_overflow},
  {"flushda", "I(s)", "I(s),E", 2, 4, iw_F1X4I12_type,
   MATCH_R2_FLUSHDA, MASK_R2_FLUSHDA, 0, address_offset_overflow},
  {"flushi", "s", "s,E", 1, 4, iw_F3X6_type,
   MATCH_R2_FLUSHI, MASK_R2_FLUSHI, 0, no_overflow},
  {"flushp", "", "E", 0, 4, iw_F3X6_type,
   MATCH_R2_FLUSHP, MASK_R2_FLUSHP, 0, no_overflow},
  {"initd", "I(s)", "I(s),E", 2, 4, iw_F1X4I12_type,
   MATCH_R2_INITD, MASK_R2_INITD, 0, address_offset_overflow},
  {"initda", "I(s)", "I(s),E", 2, 4, iw_F1X4I12_type,
   MATCH_R2_INITDA, MASK_R2_INITDA, 0, address_offset_overflow},
  {"initi", "s", "s,E", 1, 4, iw_F3X6_type,
   MATCH_R2_INITI, MASK_R2_INITI, 0, no_overflow},
  {"insert", "t,s,j,k", "t,s,j,k,E", 4, 4, iw_F2X6L10_type,
   MATCH_R2_INSERT, MASK_R2_INSERT, 0, no_overflow},
  {"jmp", "s", "s,E", 1, 4, iw_F3X6_type,
   MATCH_R2_JMP, MASK_R2_JMP, 0, no_overflow},
  {"jmpi", "m", "m,E", 1, 4, iw_L26_type,
   MATCH_R2_JMPI, MASK_R2_JMPI, 0, call_target_overflow},
  {"jmpr.n", "s", "s,E", 1, 2, iw_F1X1_type,
   MATCH_R2_JMPR_N, MASK_R2_JMPR_N, 0, no_overflow},
  {"ldb", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_LDB, MASK_R2_LDB, 0, address_offset_overflow},
  {"ldbio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_LDBIO, MASK_R2_LDBIO, 0, signed_immed12_overflow},
  {"ldbu", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_LDBU, MASK_R2_LDBU, 0, address_offset_overflow},
  {"ldbuio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_LDBUIO, MASK_R2_LDBUIO, 0, signed_immed12_overflow},
  {"ldbu.n", "T,Y(S)", "T,Y(S),E", 3, 2, iw_T2I4_type,
   MATCH_R2_LDBU_N, MASK_R2_LDBU_N, 0, address_offset_overflow},
  {"ldex", "d,(s)", "d,(s),E", 2, 4, iw_F3X6_type,
   MATCH_R2_LDEX, MASK_R2_LDEX, 0, no_overflow},
  {"ldh", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_LDH, MASK_R2_LDH, 0, address_offset_overflow},
  {"ldhio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_LDHIO, MASK_R2_LDHIO, 0, signed_immed12_overflow},
  {"ldhu", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_LDHU, MASK_R2_LDHU, 0, address_offset_overflow},
  {"ldhuio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_LDHUIO, MASK_R2_LDHUIO, 0, signed_immed12_overflow},
  {"ldhu.n", "T,X(S)", "T,X(S),E", 3, 2, iw_T2I4_type,
   MATCH_R2_LDHU_N, MASK_R2_LDHU_N, 0, address_offset_overflow},
  {"ldsex", "d,(s)", "d,(s),E", 2, 4, iw_F3X6_type,
   MATCH_R2_LDSEX, MASK_R2_LDSEX, 0, no_overflow},
  {"ldw", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_LDW, MASK_R2_LDW, 0, address_offset_overflow},
  {"ldwio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_LDWIO, MASK_R2_LDWIO, 0, signed_immed12_overflow},
  {"ldwm", "R,B", "R,B,E", 2, 4, iw_F1X4L17_type,
   MATCH_R2_LDWM, MASK_R2_LDWM, 0, no_overflow},
  {"ldw.n", "T,W(S)", "T,W(S),E", 3, 2, iw_T2I4_type,
   MATCH_R2_LDW_N, MASK_R2_LDW_N, 0, address_offset_overflow},
  {"ldwsp.n", "t,V(s)", "t,V(s),E", 3, 2, iw_F1I5_type,
   MATCH_R2_LDWSP_N, MASK_R2_LDWSP_N, 0, address_offset_overflow},
  {"merge", "t,s,j,k", "t,s,j,k,E", 4, 4, iw_F2X6L10_type,
   MATCH_R2_MERGE, MASK_R2_MERGE, 0, no_overflow},
  {"mov", "d,s", "d,s,E", 2, 4, iw_F3X6_type,
   MATCH_R2_MOV, MASK_R2_MOV, NIOS2_INSN_MACRO_MOV, no_overflow},
  {"mov.n", "d,s", "d,s,E", 2, 2, iw_F2_type,
   MATCH_R2_MOV_N, MASK_R2_MOV_N, 0, no_overflow},
  {"movi.n", "D,h", "D,h,E", 2, 2, iw_T1I7_type,
   MATCH_R2_MOVI_N, MASK_R2_MOVI_N, 0, enumeration_overflow},
  {"movhi", "t,u", "t,u,E", 2, 4, iw_F2I16_type,
   MATCH_R2_MOVHI, MASK_R2_MOVHI,
   NIOS2_INSN_MACRO_MOVI, unsigned_immed16_overflow},
  {"movi", "t,i", "t,i,E", 2, 4, iw_F2I16_type,
   MATCH_R2_MOVI, MASK_R2_MOVI, NIOS2_INSN_MACRO_MOVI, signed_immed16_overflow},
  {"movia", "t,o", "t,o,E", 2, 4, iw_F2I16_type,
   MATCH_R2_ORHI, MASK_R2_ORHI, NIOS2_INSN_MACRO_MOVIA, no_overflow},
  {"movui", "t,u", "t,u,E", 2, 4, iw_F2I16_type,
   MATCH_R2_MOVUI, MASK_R2_MOVUI,
   NIOS2_INSN_MACRO_MOVI, unsigned_immed16_overflow},
  {"mul", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_MUL, MASK_R2_MUL, 0, no_overflow},
  {"muli", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_MULI, MASK_R2_MULI, 0, signed_immed16_overflow},
  {"mulxss", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_MULXSS, MASK_R2_MULXSS, 0, no_overflow},
  {"mulxsu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_MULXSU, MASK_R2_MULXSU, 0, no_overflow},
  {"mulxuu", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_MULXUU, MASK_R2_MULXUU, 0, no_overflow},
  /* The encoding of the neg.n operands is backwards, not
     the interpretation -- the first operand is still the
     destination and the second the source.  */
  {"neg.n", "S,D", "S,D,E", 2, 2, iw_T2X3_type,
   MATCH_R2_NEG_N, MASK_R2_NEG_N, 0, no_overflow},
  {"nextpc", "d", "d,E", 1, 4, iw_F3X6_type,
   MATCH_R2_NEXTPC, MASK_R2_NEXTPC, 0, no_overflow},
  {"nop", "", "E", 0, 4, iw_F3X6_type,
   MATCH_R2_NOP, MASK_R2_NOP, NIOS2_INSN_MACRO_MOV, no_overflow},
  {"nop.n", "", "E", 0, 2, iw_F2_type,
   MATCH_R2_NOP_N, MASK_R2_NOP_N, NIOS2_INSN_MACRO_MOV, no_overflow},
  {"nor", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_NOR, MASK_R2_NOR, 0, no_overflow},
  {"not.n", "D,S", "D,S,E", 2, 2, iw_T2X3_type,
   MATCH_R2_NOT_N, MASK_R2_NOT_N, 0, no_overflow},
  {"or", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_OR, MASK_R2_OR, 0, no_overflow},
  {"orhi", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ORHI, MASK_R2_ORHI, 0, unsigned_immed16_overflow},
  {"ori", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_ORI, MASK_R2_ORI, 0, unsigned_immed16_overflow},
  {"or.n", "D,S,T", "D,S,T,E", 3, 2, iw_T2X3_type,
   MATCH_R2_OR_N, MASK_R2_OR_N, 0, no_overflow},
  {"pop.n", "R,W", "R,W,E", 2, 2, iw_L5I4X1_type,
   MATCH_R2_POP_N, MASK_R2_POP_N, NIOS2_INSN_OPTARG, no_overflow},
  {"push.n", "R,W", "R,W,E", 2, 2, iw_L5I4X1_type,
   MATCH_R2_PUSH_N, MASK_R2_PUSH_N, NIOS2_INSN_OPTARG, no_overflow},
  {"rdctl", "d,c", "d,c,E", 2, 4, iw_F3X6L5_type,
   MATCH_R2_RDCTL, MASK_R2_RDCTL, 0, no_overflow},
  {"rdprs", "t,s,I", "t,s,I,E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_RDPRS, MASK_R2_RDPRS, 0, signed_immed12_overflow},
  {"ret", "", "E", 0, 4, iw_F3X6_type,
   MATCH_R2_RET, MASK_R2_RET, 0, no_overflow},
  {"ret.n", "", "E", 0, 2, iw_X2L5_type,
   MATCH_R2_RET_N, MASK_R2_RET_N, 0, no_overflow},
  {"rol", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_ROL, MASK_R2_ROL, 0, no_overflow},
  {"roli", "d,s,j", "d,s,j,E", 3, 4, iw_F3X6L5_type,
   MATCH_R2_ROLI, MASK_R2_ROLI, 0, unsigned_immed5_overflow},
  {"ror", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_ROR, MASK_R2_ROR, 0, no_overflow},
  {"sll", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_SLL, MASK_R2_SLL, 0, no_overflow},
  {"slli", "d,s,j", "d,s,j,E", 3, 4, iw_F3X6L5_type,
   MATCH_R2_SLLI, MASK_R2_SLLI, 0, unsigned_immed5_overflow},
  {"sll.n", "D,S,T", "D,S,T,E", 3, 2, iw_T2X3_type,
   MATCH_R2_SLL_N, MASK_R2_SLL_N, 0, no_overflow},
  {"slli.n", "D,S,f", "D,S,f,E", 3, 2, iw_T2X1L3_type,
   MATCH_R2_SLLI_N, MASK_R2_SLLI_N, 0, enumeration_overflow},
  {"spaddi.n", "D,U", "D,U,E", 2, 2, iw_T1I7_type,
   MATCH_R2_SPADDI_N, MASK_R2_SPADDI_N, 0, address_offset_overflow},
  {"spdeci.n", "U", "U,E", 1, 2, iw_X1I7_type,
   MATCH_R2_SPDECI_N, MASK_R2_SPDECI_N, 0, address_offset_overflow},
  {"spinci.n", "U", "U,E", 1, 2, iw_X1I7_type,
   MATCH_R2_SPINCI_N, MASK_R2_SPINCI_N, 0, address_offset_overflow},
  {"sra", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_SRA, MASK_R2_SRA, 0, no_overflow},
  {"srai", "d,s,j", "d,s,j,E", 3, 4, iw_F3X6L5_type,
   MATCH_R2_SRAI, MASK_R2_SRAI, 0, unsigned_immed5_overflow},
  {"srl", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_SRL, MASK_R2_SRL, 0, no_overflow},
  {"srli", "d,s,j", "d,s,j,E", 3, 4, iw_F3X6L5_type,
   MATCH_R2_SRLI, MASK_R2_SRLI, 0, unsigned_immed5_overflow},
  {"srl.n", "D,S,T", "D,S,T,E", 3, 2, iw_T2X3_type,
   MATCH_R2_SRL_N, MASK_R2_SRL_N, 0, no_overflow},
  {"srli.n", "D,S,f", "D,S,f,E", 3, 2, iw_T2X1L3_type,
   MATCH_R2_SRLI_N, MASK_R2_SRLI_N, 0, enumeration_overflow},
  {"stb", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_STB, MASK_R2_STB, 0, address_offset_overflow},
  {"stbio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_STBIO, MASK_R2_STBIO, 0, signed_immed12_overflow},
  {"stb.n", "T,Y(S)", "T,Y(S),E", 3, 2, iw_T2I4_type,
   MATCH_R2_STB_N, MASK_R2_STB_N, 0, address_offset_overflow},
  {"stbz.n", "t,M(S)", "t,M(S),E", 3, 2, iw_T1X1I6_type,
   MATCH_R2_STBZ_N, MASK_R2_STBZ_N, 0, address_offset_overflow},
  {"stex", "d,t,(s)", "d,t,(s),E", 3, 4, iw_F3X6_type,
   MATCH_R2_STEX, MASK_R2_STEX, 0, no_overflow},
  {"sth", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_STH, MASK_R2_STH, 0, address_offset_overflow},
  {"sthio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_STHIO, MASK_R2_STHIO, 0, signed_immed12_overflow},
  {"sth.n", "T,X(S)", "T,X(S),E", 3, 2, iw_T2I4_type,
   MATCH_R2_STH_N, MASK_R2_STH_N, 0, address_offset_overflow},
  {"stsex", "d,t,(s)", "d,t,(s),E", 3, 4, iw_F3X6_type,
   MATCH_R2_STSEX, MASK_R2_STSEX, 0, no_overflow},
  {"stw", "t,i(s)", "t,i(s),E", 3, 4, iw_F2I16_type,
   MATCH_R2_STW, MASK_R2_STW, 0, address_offset_overflow},
  {"stwio", "t,I(s)", "t,I(s),E", 3, 4, iw_F2X4I12_type,
   MATCH_R2_STWIO, MASK_R2_STWIO, 0, signed_immed12_overflow},
  {"stwm", "R,B", "R,B,E", 2, 4, iw_F1X4L17_type,
   MATCH_R2_STWM, MASK_R2_STWM, 0, no_overflow},
  {"stwsp.n", "t,V(s)", "t,V(s),E", 3, 2, iw_F1I5_type,
   MATCH_R2_STWSP_N, MASK_R2_STWSP_N, 0, address_offset_overflow},
  {"stw.n", "T,W(S)", "T,W(S),E", 3, 2, iw_T2I4_type,
   MATCH_R2_STW_N, MASK_R2_STW_N, 0, address_offset_overflow},
  {"stwz.n", "t,N(S)", "t,N(S),E", 3, 2, iw_T1X1I6_type,
   MATCH_R2_STWZ_N, MASK_R2_STWZ_N, 0, address_offset_overflow},
  {"sub", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_SUB, MASK_R2_SUB, 0, no_overflow},
  {"subi", "t,s,i", "t,s,i,E", 3, 4, iw_F2I16_type,
   MATCH_R2_SUBI, MASK_R2_SUBI, NIOS2_INSN_MACRO, signed_immed16_overflow},
  {"sub.n", "D,S,T", "D,S,T,E", 3, 2, iw_T3X1_type,
   MATCH_R2_SUB_N, MASK_R2_SUB_N, 0, no_overflow},
  {"subi.n", "D,S,e", "D,S,e,E", 3, 2, iw_T2X1I3_type,
   MATCH_R2_SUBI_N, MASK_R2_SUBI_N, 0, enumeration_overflow},
  {"sync", "", "E", 0, 4, iw_F3X6_type,
   MATCH_R2_SYNC, MASK_R2_SYNC, 0, no_overflow},
  {"trap", "j", "j,E", 1, 4, iw_F3X6L5_type,
   MATCH_R2_TRAP, MASK_R2_TRAP, NIOS2_INSN_OPTARG, no_overflow},
  {"trap.n", "j", "j,E", 1, 2, iw_X2L5_type,
   MATCH_R2_TRAP_N, MASK_R2_TRAP_N, NIOS2_INSN_OPTARG, no_overflow},
  {"wrctl", "c,s", "c,s,E", 2, 4, iw_F3X6L5_type,
   MATCH_R2_WRCTL, MASK_R2_WRCTL, 0, no_overflow},
  {"wrpie", "d,s", "d,s,E", 2, 4, iw_F3X6L5_type,
   MATCH_R2_WRPIE, MASK_R2_WRPIE, 0, no_overflow},
  {"wrprs", "d,s", "d,s,E", 2, 4, iw_F3X6_type,
   MATCH_R2_WRPRS, MASK_R2_WRPRS, 0, no_overflow},
  {"xor", "d,s,t", "d,s,t,E", 3, 4, iw_F3X6_type,
   MATCH_R2_XOR, MASK_R2_XOR, 0, no_overflow},
  {"xorhi", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_XORHI, MASK_R2_XORHI, 0, unsigned_immed16_overflow},
  {"xori", "t,s,u", "t,s,u,E", 3, 4, iw_F2I16_type,
   MATCH_R2_XORI, MASK_R2_XORI, 0, unsigned_immed16_overflow},
  {"xor.n", "D,S,T", "D,S,T,E", 3, 2, iw_T2X3_type,
   MATCH_R2_XOR_N, MASK_R2_XOR_N, 0, no_overflow},
};

#define NIOS2_NUM_R2_OPCODES \
       ((sizeof nios2_r2_opcodes) / (sizeof (nios2_r2_opcodes[0])))
const int nios2_num_r2_opcodes = NIOS2_NUM_R2_OPCODES;

/* Default to using the R1 instruction tables.  */
struct nios2_opcode *nios2_opcodes = (struct nios2_opcode *) nios2_r1_opcodes;
int nios2_num_opcodes = NIOS2_NUM_R1_OPCODES;
#undef NIOS2_NUM_R1_OPCODES
#undef NIOS2_NUM_R2_OPCODES

/* Decodings for R2 asi.n (addi.n/subi.n) immediate values.  */
unsigned int nios2_r2_asi_n_mappings[] =
  {1, 2, 4, 8, 16, 32, 64, 128};
const int nios2_num_r2_asi_n_mappings = 8;

/* Decodings for R2 shi.n (slli.n/srli.n) immediate values.  */
unsigned int nios2_r2_shi_n_mappings[] =
  {1, 2, 3, 8, 12, 16, 24, 31};
const int nios2_num_r2_shi_n_mappings = 8;

/* Decodings for R2 andi.n immediate values.  */
unsigned int nios2_r2_andi_n_mappings[] =
  {1, 2, 3, 4, 8, 0xf, 0x10, 0x1f,
   0x20, 0x3f, 0x7f, 0x80, 0xff, 0x7ff, 0xff00, 0xffff};
const int nios2_num_r2_andi_n_mappings = 16;

/* Decodings for R2 3-bit register fields.  */
int nios2_r2_reg3_mappings[] =
  {16, 17, 2, 3, 4, 5, 6, 7};
const int nios2_num_r2_reg3_mappings = 8;

/* Decodings for R2 push.n/pop.n REG_RANGE value list.  */
unsigned long nios2_r2_reg_range_mappings[] = {
  0x00010000,
  0x00030000,
  0x00070000,
  0x000f0000,
  0x001f0000,
  0x003f0000,
  0x007f0000,
  0x00ff0000
};
const int nios2_num_r2_reg_range_mappings = 8;

/*#include "sysdep.h"
#include "dis-asm.h"
#include "opcode/nios2.h"
#include "libiberty.h"
#include <string.h>
#include <assert.h>
*/
/* No symbol table is available when this code runs out in an embedded
   system as when it is used for disassembler support in a monitor.  */
#if !defined(EMBEDDED_ENV)
#define SYMTAB_AVAILABLE 1
/*
#include "elf-bfd.h"
#include "elf/nios2.h"
*/
#endif

/* Default length of Nios II instruction in bytes.  */
#define INSNLEN 4

/* Data structures used by the opcode hash table.  */
typedef struct _nios2_opcode_hash
{
  const struct nios2_opcode *opcode;
  struct _nios2_opcode_hash *next;
} nios2_opcode_hash;

/* Hash table size.  */
#define OPCODE_HASH_SIZE (IW_R1_OP_UNSHIFTED_MASK + 1)

/* Extract the opcode from an instruction word.  */
static unsigned int
nios2_r1_extract_opcode (unsigned int x)
{
  return GET_IW_R1_OP (x);
}

static unsigned int
nios2_r2_extract_opcode (unsigned int x)
{
  return GET_IW_R2_OP (x);
}

/* We maintain separate hash tables for R1 and R2 opcodes, and pseudo-ops
   are stored in a different table than regular instructions.  */

typedef struct _nios2_disassembler_state
{
  const struct nios2_opcode *opcodes;
  const int *num_opcodes;
  unsigned int (*extract_opcode) (unsigned int);
  nios2_opcode_hash *hash[OPCODE_HASH_SIZE];
  nios2_opcode_hash *ps_hash[OPCODE_HASH_SIZE];
  const struct nios2_opcode *nop;
  bfd_boolean init;
} nios2_disassembler_state;

static nios2_disassembler_state
nios2_r1_disassembler_state = {
  nios2_r1_opcodes,
  &nios2_num_r1_opcodes,
  nios2_r1_extract_opcode,
  {},
  {},
  NULL,
  0
};

static nios2_disassembler_state
nios2_r2_disassembler_state = {
  nios2_r2_opcodes,
  &nios2_num_r2_opcodes,
  nios2_r2_extract_opcode,
  {},
  {},
  NULL,
  0
};

/* Function to initialize the opcode hash table.  */
static void
nios2_init_opcode_hash (nios2_disassembler_state *state)
{
  unsigned int i;
  register const struct nios2_opcode *op;

  for (i = 0; i < OPCODE_HASH_SIZE; i++)
    for (op = state->opcodes; op < &state->opcodes[*(state->num_opcodes)]; op++)
      {
	nios2_opcode_hash *new_hash;
	nios2_opcode_hash **bucket = NULL;

	if ((op->pinfo & NIOS2_INSN_MACRO) == NIOS2_INSN_MACRO)
	  {
	    if (i == state->extract_opcode (op->match)
		&& (op->pinfo & (NIOS2_INSN_MACRO_MOV | NIOS2_INSN_MACRO_MOVI)
		    & 0x7fffffff))
	      {
		bucket = &(state->ps_hash[i]);
		if (strcmp (op->name, "nop") == 0)
		  state->nop = op;
	      }
	  }
	else if (i == state->extract_opcode (op->match))
	  bucket = &(state->hash[i]);

	if (bucket)
	  {
	    new_hash =
	      (nios2_opcode_hash *) malloc (sizeof (nios2_opcode_hash));
	    if (new_hash == NULL)
	      {
		fprintf (stderr,
			 "error allocating memory...broken disassembler\n");
		abort ();
	      }
	    new_hash->opcode = op;
	    new_hash->next = NULL;
	    while (*bucket)
	      bucket = &((*bucket)->next);
	    *bucket = new_hash;
	  }
      }
  state->init = 1;

#ifdef DEBUG_HASHTABLE
  for (i = 0; i < OPCODE_HASH_SIZE; ++i)
    {
      nios2_opcode_hash *tmp_hash = state->hash[i];
      printf ("index: 0x%02X	ops: ", i);
      while (tmp_hash != NULL)
	{
	  printf ("%s ", tmp_hash->opcode->name);
	  tmp_hash = tmp_hash->next;
	}
      printf ("\n");
    }

  for (i = 0; i < OPCODE_HASH_SIZE; ++i)
    {
      nios2_opcode_hash *tmp_hash = state->ps_hash[i];
      printf ("index: 0x%02X	ops: ", i);
      while (tmp_hash != NULL)
	{
	  printf ("%s ", tmp_hash->opcode->name);
	  tmp_hash = tmp_hash->next;
	}
      printf ("\n");
    }
#endif /* DEBUG_HASHTABLE */
}

/* Return a pointer to an nios2_opcode struct for a given instruction
   word OPCODE for bfd machine MACH, or NULL if there is an error.  */
const struct nios2_opcode *
nios2_find_opcode_hash (unsigned long opcode, unsigned long mach)
{
  nios2_opcode_hash *entry;
  nios2_disassembler_state *state;

  /* Select the right instruction set, hash tables, and opcode accessor
     for the mach variant.  */
  if (mach == bfd_mach_nios2r2)
    state = &nios2_r2_disassembler_state;
  else
    state = &nios2_r1_disassembler_state;

  /* Build a hash table to shorten the search time.  */
  if (!state->init)
    nios2_init_opcode_hash (state);

  /* Check for NOP first.  Both NOP and MOV are macros that expand into
     an ADD instruction, and we always want to give priority to NOP.  */
  if (state->nop->match == (opcode & state->nop->mask))
    return state->nop;

  /* First look in the pseudo-op hashtable.  */
  for (entry = state->ps_hash[state->extract_opcode (opcode)];
       entry; entry = entry->next)
    if (entry->opcode->match == (opcode & entry->opcode->mask))
      return entry->opcode;

  /* Otherwise look in the main hashtable.  */
  for (entry = state->hash[state->extract_opcode (opcode)];
       entry; entry = entry->next)
    if (entry->opcode->match == (opcode & entry->opcode->mask))
      return entry->opcode;

  return NULL;
}

/* There are 32 regular registers, 32 coprocessor registers,
   and 32 control registers.  */
#define NUMREGNAMES 32

/* Return a pointer to the base of the coprocessor register name array.  */
static struct nios2_reg *
nios2_coprocessor_regs (void)
{
  static struct nios2_reg *cached = NULL;

  if (!cached)
    {
      int i;
      for (i = NUMREGNAMES; i < nios2_num_regs; i++)
	if (!strcmp (nios2_regs[i].name, "c0"))
	  {
	    cached = nios2_regs + i;
	    break;
	  }
      assert (cached);
    }
  return cached;
}

/* Return a pointer to the base of the control register name array.  */
static struct nios2_reg *
nios2_control_regs (void)
{
  static struct nios2_reg *cached = NULL;

  if (!cached)
    {
      int i;
      for (i = NUMREGNAMES; i < nios2_num_regs; i++)
	if (!strcmp (nios2_regs[i].name, "status"))
	  {
	    cached = nios2_regs + i;
	    break;
	  }
      assert (cached);
    }
  return cached;
}

/* Helper routine to report internal errors.  */
static void
bad_opcode (const struct nios2_opcode *op)
{
  fprintf (stderr, "Internal error: broken opcode descriptor for `%s %s'\n",
	   op->name, op->args);
  abort ();
}

/* The function nios2_print_insn_arg uses the character pointed
   to by ARGPTR to determine how it print the next token or separator
   character in the arguments to an instruction.  */
static int
nios2_print_insn_arg (const char *argptr,
		      unsigned long opcode, bfd_vma address,
		      disassemble_info *info,
		      const struct nios2_opcode *op)
{
  unsigned long i = 0;
  struct nios2_reg *reg_base;

  switch (*argptr)
    {
    case ',':
    case '(':
    case ')':
      (*info->fprintf_func) (info->stream, "%c", *argptr);
      break;

    case 'c':
      /* Control register index.  */
      switch (op->format)
	{
	case iw_r_type:
	  i = GET_IW_R_IMM5 (opcode);
	  break;
	case iw_F3X6L5_type:
	  i = GET_IW_F3X6L5_IMM5 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      reg_base = nios2_control_regs ();
      (*info->fprintf_func) (info->stream, "%s", reg_base[i].name);
      break;

    case 'd':
      reg_base = nios2_regs;
      switch (op->format)
	{
	case iw_r_type:
	  i = GET_IW_R_C (opcode);
	  break;
	case iw_custom_type:
	  i = GET_IW_CUSTOM_C (opcode);
	  if (GET_IW_CUSTOM_READC (opcode) == 0)
	    reg_base = nios2_coprocessor_regs ();
	  break;
	case iw_F3X6L5_type:
	case iw_F3X6_type:
	  i = GET_IW_F3X6L5_C (opcode);
	  break;
	case iw_F3X8_type:
	  i = GET_IW_F3X8_C (opcode);
	  if (GET_IW_F3X8_READC (opcode) == 0)
	    reg_base = nios2_coprocessor_regs ();
	  break;
	case iw_F2_type:
	  i = GET_IW_F2_B (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      if (i < NUMREGNAMES)
	(*info->fprintf_func) (info->stream, "%s", reg_base[i].name);
      else
	(*info->fprintf_func) (info->stream, "unknown");
      break;

    case 's':
      reg_base = nios2_regs;
      switch (op->format)
	{
	case iw_r_type:
	  i = GET_IW_R_A (opcode);
	  break;
	case iw_i_type:
	  i = GET_IW_I_A (opcode);
	  break;
	case iw_custom_type:
	  i = GET_IW_CUSTOM_A (opcode);
	  if (GET_IW_CUSTOM_READA (opcode) == 0)
	    reg_base = nios2_coprocessor_regs ();
	  break;
	case iw_F2I16_type:
	  i = GET_IW_F2I16_A (opcode);
	  break;
	case iw_F2X4I12_type:
	  i = GET_IW_F2X4I12_A (opcode);
	  break;
	case iw_F1X4I12_type:
	  i = GET_IW_F1X4I12_A (opcode);
	  break;
	case iw_F1X4L17_type:
	  i = GET_IW_F1X4L17_A (opcode);
	  break;
	case iw_F3X6L5_type:
	case iw_F3X6_type:
	  i = GET_IW_F3X6L5_A (opcode);
	  break;
	case iw_F2X6L10_type:
	  i = GET_IW_F2X6L10_A (opcode);
	  break;
	case iw_F3X8_type:
	  i = GET_IW_F3X8_A (opcode);
	  if (GET_IW_F3X8_READA (opcode) == 0)
	    reg_base = nios2_coprocessor_regs ();
	  break;
	case iw_F1X1_type:
	  i = GET_IW_F1X1_A (opcode);
	  break;
	case iw_F1I5_type:
	  i = 27;   /* Implicit stack pointer reference.  */
	  break;
	case iw_F2_type:
	  i = GET_IW_F2_A (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      if (i < NUMREGNAMES)
	(*info->fprintf_func) (info->stream, "%s", reg_base[i].name);
      else
	(*info->fprintf_func) (info->stream, "unknown");
      break;

    case 't':
      reg_base = nios2_regs;
      switch (op->format)
	{
	case iw_r_type:
	  i = GET_IW_R_B (opcode);
	  break;
	case iw_i_type:
	  i = GET_IW_I_B (opcode);
	  break;
	case iw_custom_type:
	  i = GET_IW_CUSTOM_B (opcode);
	  if (GET_IW_CUSTOM_READB (opcode) == 0)
	    reg_base = nios2_coprocessor_regs ();
	  break;
	case iw_F2I16_type:
	  i = GET_IW_F2I16_B (opcode);
	  break;
	case iw_F2X4I12_type:
	  i = GET_IW_F2X4I12_B (opcode);
	  break;
	case iw_F3X6L5_type:
	case iw_F3X6_type:
	  i = GET_IW_F3X6L5_B (opcode);
	  break;
	case iw_F2X6L10_type:
	  i = GET_IW_F2X6L10_B (opcode);
	  break;
	case iw_F3X8_type:
	  i = GET_IW_F3X8_B (opcode);
	  if (GET_IW_F3X8_READB (opcode) == 0)
	    reg_base = nios2_coprocessor_regs ();
	  break;
	case iw_F1I5_type:
	  i = GET_IW_F1I5_B (opcode);
	  break;
	case iw_F2_type:
	  i = GET_IW_F2_B (opcode);
	  break;
	case iw_T1X1I6_type:
	  i = 0;
	  break;
	default:
	  bad_opcode (op);
	}
      if (i < NUMREGNAMES)
	(*info->fprintf_func) (info->stream, "%s", reg_base[i].name);
      else
	(*info->fprintf_func) (info->stream, "unknown");
      break;

    case 'D':
      switch (op->format)
	{
	case iw_T1I7_type:
	  i = GET_IW_T1I7_A3 (opcode);
	  break;
	case iw_T2X1L3_type:
	  i = GET_IW_T2X1L3_B3 (opcode);
	  break;
	case iw_T2X1I3_type:
	  i = GET_IW_T2X1I3_B3 (opcode);
	  break;
	case iw_T3X1_type:
	  i = GET_IW_T3X1_C3 (opcode);
	  break;
	case iw_T2X3_type:
	  if (op->num_args == 3)
	    i = GET_IW_T2X3_A3 (opcode);
	  else
	    i = GET_IW_T2X3_B3 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      i = nios2_r2_reg3_mappings[i];
      (*info->fprintf_func) (info->stream, "%s", nios2_regs[i].name);
      break;

    case 'M':
      /* 6-bit unsigned immediate with no shift.  */
      switch (op->format)
	{
	case iw_T1X1I6_type:
	  i = GET_IW_T1X1I6_IMM6 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'N':
      /* 6-bit unsigned immediate with 2-bit shift.  */
      switch (op->format)
	{
	case iw_T1X1I6_type:
	  i = GET_IW_T1X1I6_IMM6 (opcode) << 2;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'S':
      switch (op->format)
	{
	case iw_T1I7_type:
	  i = GET_IW_T1I7_A3 (opcode);
	  break;
	case iw_T2I4_type:
	  i = GET_IW_T2I4_A3 (opcode);
	  break;
	case iw_T2X1L3_type:
	  i = GET_IW_T2X1L3_A3 (opcode);
	  break;
	case iw_T2X1I3_type:
	  i = GET_IW_T2X1I3_A3 (opcode);
	  break;
	case iw_T3X1_type:
	  i = GET_IW_T3X1_A3 (opcode);
	  break;
	case iw_T2X3_type:
	  i = GET_IW_T2X3_A3 (opcode);
	  break;
	case iw_T1X1I6_type:
	  i = GET_IW_T1X1I6_A3 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      i = nios2_r2_reg3_mappings[i];
      (*info->fprintf_func) (info->stream, "%s", nios2_regs[i].name);
      break;

    case 'T':
      switch (op->format)
	{
	case iw_T2I4_type:
	  i = GET_IW_T2I4_B3 (opcode);
	  break;
	case iw_T3X1_type:
	  i = GET_IW_T3X1_B3 (opcode);
	  break;
	case iw_T2X3_type:
	  i = GET_IW_T2X3_B3 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      i = nios2_r2_reg3_mappings[i];
      (*info->fprintf_func) (info->stream, "%s", nios2_regs[i].name);
      break;

    case 'i':
      /* 16-bit signed immediate.  */
      switch (op->format)
	{
	case iw_i_type:
	  i = (signed) (GET_IW_I_IMM16 (opcode) << 16) >> 16;
	  break;
	case iw_F2I16_type:
	  i = (signed) (GET_IW_F2I16_IMM16 (opcode) << 16) >> 16;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'I':
      /* 12-bit signed immediate.  */
      switch (op->format)
	{
	case iw_F2X4I12_type:
	  i = (signed) (GET_IW_F2X4I12_IMM12 (opcode) << 20) >> 20;
	  break;
	case iw_F1X4I12_type:
	  i = (signed) (GET_IW_F1X4I12_IMM12 (opcode) << 20) >> 20;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'u':
      /* 16-bit unsigned immediate.  */
      switch (op->format)
	{
	case iw_i_type:
	  i = GET_IW_I_IMM16 (opcode);
	  break;
	case iw_F2I16_type:
	  i = GET_IW_F2I16_IMM16 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'U':
      /* 7-bit unsigned immediate with 2-bit shift.  */
      switch (op->format)
	{
	case iw_T1I7_type:
	  i = GET_IW_T1I7_IMM7 (opcode) << 2;
	  break;
	case iw_X1I7_type:
	  i = GET_IW_X1I7_IMM7 (opcode) << 2;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'V':
      /* 5-bit unsigned immediate with 2-bit shift.  */
      switch (op->format)
	{
	case iw_F1I5_type:
	  i = GET_IW_F1I5_IMM5 (opcode) << 2;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'W':
      /* 4-bit unsigned immediate with 2-bit shift.  */
      switch (op->format)
	{
	case iw_T2I4_type:
	  i = GET_IW_T2I4_IMM4 (opcode) << 2;
	  break;
	case iw_L5I4X1_type:
	  i = GET_IW_L5I4X1_IMM4 (opcode) << 2;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'X':
      /* 4-bit unsigned immediate with 1-bit shift.  */
      switch (op->format)
	{
	case iw_T2I4_type:
	  i = GET_IW_T2I4_IMM4 (opcode) << 1;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'Y':
      /* 4-bit unsigned immediate without shift.  */
      switch (op->format)
	{
	case iw_T2I4_type:
	  i = GET_IW_T2I4_IMM4 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'o':
      /* 16-bit signed immediate address offset.  */
      switch (op->format)
	{
	case iw_i_type:
	  i = (signed) (GET_IW_I_IMM16 (opcode) << 16) >> 16;
	  break;
	case iw_F2I16_type:
	  i = (signed) (GET_IW_F2I16_IMM16 (opcode) << 16) >> 16;
	  break;
	default:
	  bad_opcode (op);
	}
      address = address + 4 + i;
      (*info->print_address_func) (address, info);
      break;

    case 'O':
      /* 10-bit signed address offset with 1-bit shift.  */
      switch (op->format)
	{
	case iw_I10_type:
	  i = (signed) (GET_IW_I10_IMM10 (opcode) << 22) >> 21;
	  break;
	default:
	  bad_opcode (op);
	}
      address = address + 2 + i;
      (*info->print_address_func) (address, info);
      break;

    case 'P':
      /* 7-bit signed address offset with 1-bit shift.  */
      switch (op->format)
	{
	case iw_T1I7_type:
	  i = (signed) (GET_IW_T1I7_IMM7 (opcode) << 25) >> 24;
	  break;
	default:
	  bad_opcode (op);
	}
      address = address + 2 + i;
      (*info->print_address_func) (address, info);
      break;

    case 'j':
      /* 5-bit unsigned immediate.  */
      switch (op->format)
	{
	case iw_r_type:
	  i = GET_IW_R_IMM5 (opcode);
	  break;
	case iw_F3X6L5_type:
	  i = GET_IW_F3X6L5_IMM5 (opcode);
	  break;
	case iw_F2X6L10_type:
	  i = GET_IW_F2X6L10_MSB (opcode);
	  break;
	case iw_X2L5_type:
	  i = GET_IW_X2L5_IMM5 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'k':
      /* Second 5-bit unsigned immediate field.  */
      switch (op->format)
	{
	case iw_F2X6L10_type:
	  i = GET_IW_F2X6L10_LSB (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'l':
      /* 8-bit unsigned immediate.  */
      switch (op->format)
	{
	case iw_custom_type:
	  i = GET_IW_CUSTOM_N (opcode);
	  break;
	case iw_F3X8_type:
	  i = GET_IW_F3X8_N (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%lu", i);
      break;

    case 'm':
      /* 26-bit unsigned immediate.  */
      switch (op->format)
	{
	case iw_j_type:
	  i = GET_IW_J_IMM26 (opcode);
	  break;
	case iw_L26_type:
	  i = GET_IW_L26_IMM26 (opcode);
	  break;
	default:
	  bad_opcode (op);
	}
      /* This translates to an address because it's only used in call
	 instructions.  */
      address = (address & 0xf0000000) | (i << 2);
      (*info->print_address_func) (address, info);
      break;

    case 'e':
      /* Encoded enumeration for addi.n/subi.n.  */
      switch (op->format)
	{
	case iw_T2X1I3_type:
	  i = nios2_r2_asi_n_mappings[GET_IW_T2X1I3_IMM3 (opcode)];
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%lu", i);
      break;

    case 'f':
      /* Encoded enumeration for slli.n/srli.n.  */
      switch (op->format)
	{
	case iw_T2X1L3_type:
	  i = nios2_r2_shi_n_mappings[GET_IW_T2X1I3_IMM3 (opcode)];
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%lu", i);
      break;

    case 'g':
      /* Encoded enumeration for andi.n.  */
      switch (op->format)
	{
	case iw_T2I4_type:
	  i = nios2_r2_andi_n_mappings[GET_IW_T2I4_IMM4 (opcode)];
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%lu", i);
      break;

    case 'h':
      /* Encoded enumeration for movi.n.  */
      switch (op->format)
	{
	case iw_T1I7_type:
	  i = GET_IW_T1I7_IMM7 (opcode);
	  if (i == 125)
	    i = 0xff;
	  else if (i == 126)
	    i = -2;
	  else if (i == 127)
	    i = -1;
	  break;
	default:
	  bad_opcode (op);
	}
      (*info->fprintf_func) (info->stream, "%ld", i);
      break;

    case 'R':
      {
	unsigned long reglist = 0;
	int dir = 1;
	int k, t;

	switch (op->format)
	  {
	  case iw_F1X4L17_type:
	    /* Encoding for ldwm/stwm.  */
	    i = GET_IW_F1X4L17_REGMASK (opcode);
	    if (GET_IW_F1X4L17_RS (opcode))
	      {
		reglist = ((i << 14) & 0x00ffc000);
		if (i & (1 << 10))
		  reglist |= (1 << 28);
		if (i & (1 << 11))
		  reglist |= (1 << 31);
	      }
	    else
	      reglist = i << 2;
	    dir = GET_IW_F1X4L17_REGMASK (opcode) ? 1 : -1;
	    break;

	  case iw_L5I4X1_type:
	    /* Encoding for push.n/pop.n.  */
	    reglist |= (1 << 31);
	    if (GET_IW_L5I4X1_FP (opcode))
	      reglist |= (1 << 28);
	    if (GET_IW_L5I4X1_CS (opcode))
	      {
		int val = GET_IW_L5I4X1_REGRANGE (opcode);
		reglist |= nios2_r2_reg_range_mappings[val];
	      }
	    dir = (op->match == MATCH_R2_POP_N ? 1 : -1);
	    break;

	  default:
	    bad_opcode (op);
	  }

	t = 0;
	(*info->fprintf_func) (info->stream, "{");
	for (k = (dir == 1 ? 0 : 31);
	     (dir == 1 && k < 32) || (dir == -1 && k >= 0);
	     k += dir)
	  if (reglist & (1 << k))
	    {
	      if (t)
		(*info->fprintf_func) (info->stream, ",");
	      else
		t++;
	      (*info->fprintf_func) (info->stream, "%s", nios2_regs[k].name);
	    }
	(*info->fprintf_func) (info->stream, "}");
	break;
      }

    case 'B':
      /* Base register and options for ldwm/stwm.  */
      switch (op->format)
	{
	case iw_F1X4L17_type:
	  if (GET_IW_F1X4L17_ID (opcode) == 0)
	    (*info->fprintf_func) (info->stream, "--");

	  i = GET_IW_F1X4I12_A (opcode);
	  (*info->fprintf_func) (info->stream, "(%s)",
				 nios2_builtin_regs[i].name);

	  if (GET_IW_F1X4L17_ID (opcode))
	    (*info->fprintf_func) (info->stream, "++");
	  if (GET_IW_F1X4L17_WB (opcode))
	    (*info->fprintf_func) (info->stream, ",writeback");
	  if (GET_IW_F1X4L17_PC (opcode))
	    (*info->fprintf_func) (info->stream, ",ret");
	  break;
	default:
	  bad_opcode (op);
	}
      break;

    default:
      (*info->fprintf_func) (info->stream, "unknown");
      break;
    }
  return 0;
}

/* nios2_disassemble does all the work of disassembling a Nios II
   instruction opcode.  */
static int
nios2_disassemble (bfd_vma address, unsigned long opcode,
		   disassemble_info *info)
{
  const struct nios2_opcode *op;

  info->bytes_per_line = INSNLEN;
  info->bytes_per_chunk = INSNLEN;
  info->display_endian = info->endian;
  info->insn_info_valid = 1;
  info->branch_delay_insns = 0;
  info->data_size = 0;
  info->insn_type = dis_nonbranch;
  info->target = 0;
  info->target2 = 0;

  /* Find the major opcode and use this to disassemble
     the instruction and its arguments.  */
  op = nios2_find_opcode_hash (opcode, info->mach);

  if (op != NULL)
    {
      const char *argstr = op->args;
      (*info->fprintf_func) (info->stream, "%s", op->name);
      if (argstr != NULL && *argstr != '\0')
	{
	  (*info->fprintf_func) (info->stream, "\t");
	  while (*argstr != '\0')
	    {
	      nios2_print_insn_arg (argstr, opcode, address, info, op);
	      ++argstr;
	    }
	}
      /* Tell the caller how far to advance the program counter.  */
      info->bytes_per_chunk = op->size;
      return op->size;
    }
  else
    {
      /* Handle undefined instructions.  */
      info->insn_type = dis_noninsn;
      (*info->fprintf_func) (info->stream, "0x%lx", opcode);
      return INSNLEN;
    }
}


/* print_insn_nios2 is the main disassemble function for Nios II.
   The function diassembler(abfd) (source in disassemble.c) returns a
   pointer to this either print_insn_big_nios2 or
   print_insn_little_nios2, which in turn call this function when the
   bfd machine type is Nios II. print_insn_nios2 reads the
   instruction word at the address given, and prints the disassembled
   instruction on the stream info->stream using info->fprintf_func. */

static int
print_insn_nios2 (bfd_vma address, disassemble_info *info,
		  enum bfd_endian endianness)
{
  bfd_byte buffer[INSNLEN];
  int status;

  status = (*info->read_memory_func) (address, buffer, INSNLEN, info);
  if (status == 0)
    {
      unsigned long insn;
      if (endianness == BFD_ENDIAN_BIG)
	insn = (unsigned long) bfd_getb32 (buffer);
      else
	insn = (unsigned long) bfd_getl32 (buffer);
      return nios2_disassemble (address, insn, info);
    }

  /* We might have a 16-bit R2 instruction at the end of memory.  Try that.  */
  if (info->mach == bfd_mach_nios2r2)
    {
      status = (*info->read_memory_func) (address, buffer, 2, info);
      if (status == 0)
	{
	  unsigned long insn;
	  if (endianness == BFD_ENDIAN_BIG)
	    insn = (unsigned long) bfd_getb16 (buffer);
	  else
	    insn = (unsigned long) bfd_getl16 (buffer);
	  return nios2_disassemble (address, insn, info);
	}
    }

  /* If we got here, we couldn't read anything.  */
  (*info->memory_error_func) (status, address, info);
  return -1;
}

/* These two functions are the main entry points, accessed from
   disassemble.c.  */
int
print_insn_big_nios2 (bfd_vma address, disassemble_info *info)
{
  return print_insn_nios2 (address, info, BFD_ENDIAN_BIG);
}

int
print_insn_little_nios2 (bfd_vma address, disassemble_info *info)
{
  return print_insn_nios2 (address, info, BFD_ENDIAN_LITTLE);
}
