/* s390-dis.c -- Disassemble S390 instructions
   Copyright 2000, 2001, 2002, 2003, 2005 Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

   This file is part of GDB, GAS and the GNU binutils.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include <stdio.h>
#include "dis-asm.h"

/* s390.h -- Header file for S390 opcode table
   Copyright 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

   This file is part of BFD, the Binary File Descriptor library.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#ifndef S390_H
#define S390_H

/* List of instruction sets variations. */

enum s390_opcode_mode_val
  {
    S390_OPCODE_ESA = 0,
    S390_OPCODE_ZARCH
  };

enum s390_opcode_cpu_val
  {
    S390_OPCODE_G5 = 0,
    S390_OPCODE_G6,
    S390_OPCODE_Z900,
    S390_OPCODE_Z990,
    S390_OPCODE_Z9_109,
    S390_OPCODE_Z9_EC
  };

/* The opcode table is an array of struct s390_opcode.  */

struct s390_opcode
  {
    /* The opcode name.  */
    const char * name;

    /* The opcode itself.  Those bits which will be filled in with
       operands are zeroes.  */
    unsigned char opcode[6];

    /* The opcode mask.  This is used by the disassembler.  This is a
       mask containing ones indicating those bits which must match the
       opcode field, and zeroes indicating those bits which need not
       match (and are presumably filled in by operands).  */
    unsigned char mask[6];

    /* The opcode length in bytes. */
    int oplen;

    /* An array of operand codes.  Each code is an index into the
       operand table.  They appear in the order which the operands must
       appear in assembly code, and are terminated by a zero.  */
    unsigned char operands[6];

    /* Bitmask of execution modes this opcode is available for.  */
    unsigned int modes;

    /* First cpu this opcode is available for.  */
    enum s390_opcode_cpu_val min_cpu;
  };

/* The table itself is sorted by major opcode number, and is otherwise
   in the order in which the disassembler should consider
   instructions.  */
extern const struct s390_opcode s390_opcodes[];
extern const int                s390_num_opcodes;

/* A opcode format table for the .insn pseudo mnemonic.  */
extern const struct s390_opcode s390_opformats[];
extern const int                s390_num_opformats;

/* Values defined for the flags field of a struct powerpc_opcode.  */

/* The operands table is an array of struct s390_operand.  */

struct s390_operand
  {
    /* The number of bits in the operand.  */
    int bits;

    /* How far the operand is left shifted in the instruction.  */
    int shift;

    /* One bit syntax flags.  */
    unsigned long flags;
  };

/* Elements in the table are retrieved by indexing with values from
   the operands field of the powerpc_opcodes table.  */

extern const struct s390_operand s390_operands[];

/* Values defined for the flags field of a struct s390_operand.  */

/* This operand names a register.  The disassembler uses this to print
   register names with a leading 'r'.  */
#define S390_OPERAND_GPR 0x1

/* This operand names a floating point register.  The disassembler
   prints these with a leading 'f'. */
#define S390_OPERAND_FPR 0x2

/* This operand names an access register.  The disassembler
   prints these with a leading 'a'.  */
#define S390_OPERAND_AR 0x4

/* This operand names a control register.  The disassembler
   prints these with a leading 'c'.  */
#define S390_OPERAND_CR 0x8

/* This operand is a displacement.  */
#define S390_OPERAND_DISP 0x10

/* This operand names a base register.  */
#define S390_OPERAND_BASE 0x20

/* This operand names an index register, it can be skipped.  */
#define S390_OPERAND_INDEX 0x40

/* This operand is a relative branch displacement.  The disassembler
   prints these symbolically if possible.  */
#define S390_OPERAND_PCREL 0x80

/* This operand takes signed values.  */
#define S390_OPERAND_SIGNED 0x100

/* This operand is a length.  */
#define S390_OPERAND_LENGTH 0x200

/* This operand is optional. Only a single operand at the end of
   the instruction may be optional.  */
#define S390_OPERAND_OPTIONAL 0x400

	#endif /* S390_H */


static int init_flag = 0;
static int opc_index[256];
static int current_arch_mask = 0;

/* Set up index table for first opcode byte.  */

static void
init_disasm (struct disassemble_info *info)
{
  const struct s390_opcode *opcode;
  const struct s390_opcode *opcode_end;

  memset (opc_index, 0, sizeof (opc_index));
  opcode_end = s390_opcodes + s390_num_opcodes;
  for (opcode = s390_opcodes; opcode < opcode_end; opcode++)
    {
      opc_index[(int) opcode->opcode[0]] = opcode - s390_opcodes;
      while ((opcode < opcode_end) &&
	     (opcode[1].opcode[0] == opcode->opcode[0]))
	opcode++;
    }
//  switch (info->mach)
//    {
//    case bfd_mach_s390_31:
      current_arch_mask = 1 << S390_OPCODE_ESA;
//      break;
//    case bfd_mach_s390_64:
//      current_arch_mask = 1 << S390_OPCODE_ZARCH;
//      break;
//    default:
//      abort ();
//    }
  init_flag = 1;
}

/* Extracts an operand value from an instruction.  */

static inline unsigned int
s390_extract_operand (unsigned char *insn, const struct s390_operand *operand)
{
  unsigned int val;
  int bits;

  /* Extract fragments of the operand byte for byte.  */
  insn += operand->shift / 8;
  bits = (operand->shift & 7) + operand->bits;
  val = 0;
  do
    {
      val <<= 8;
      val |= (unsigned int) *insn++;
      bits -= 8;
    }
  while (bits > 0);
  val >>= -bits;
  val &= ((1U << (operand->bits - 1)) << 1) - 1;

  /* Check for special long displacement case.  */
  if (operand->bits == 20 && operand->shift == 20)
    val = (val & 0xff) << 12 | (val & 0xfff00) >> 8;

  /* Sign extend value if the operand is signed or pc relative.  */
  if ((operand->flags & (S390_OPERAND_SIGNED | S390_OPERAND_PCREL))
      && (val & (1U << (operand->bits - 1))))
    val |= (-1U << (operand->bits - 1)) << 1;

  /* Double value if the operand is pc relative.  */
  if (operand->flags & S390_OPERAND_PCREL)
    val <<= 1;

  /* Length x in an instructions has real length x + 1.  */
  if (operand->flags & S390_OPERAND_LENGTH)
    val++;
  return val;
}

/* Print a S390 instruction.  */

int
print_insn_s390 (bfd_vma memaddr, struct disassemble_info *info)
{
  bfd_byte buffer[6];
  const struct s390_opcode *opcode;
  const struct s390_opcode *opcode_end;
  unsigned int value;
  int status, opsize, bufsize;
  char separator;

  if (init_flag == 0)
    init_disasm (info);

  /* The output looks better if we put 6 bytes on a line.  */
  info->bytes_per_line = 6;

  /* Every S390 instruction is max 6 bytes long.  */
  memset (buffer, 0, 6);
  status = (*info->read_memory_func) (memaddr, buffer, 6, info);
  if (status != 0)
    {
      for (bufsize = 0; bufsize < 6; bufsize++)
	if ((*info->read_memory_func) (memaddr, buffer, bufsize + 1, info) != 0)
	  break;
      if (bufsize <= 0)
	{
	  (*info->memory_error_func) (status, memaddr, info);
	  return -1;
	}
      /* Opsize calculation looks strange but it works
	 00xxxxxx -> 2 bytes, 01xxxxxx/10xxxxxx -> 4 bytes,
	 11xxxxxx -> 6 bytes.  */
      opsize = ((((buffer[0] >> 6) + 1) >> 1) + 1) << 1;
      status = opsize > bufsize;
    }
  else
    {
      bufsize = 6;
      opsize = ((((buffer[0] >> 6) + 1) >> 1) + 1) << 1;
    }

  if (status == 0)
    {
      /* Find the first match in the opcode table.  */
      opcode_end = s390_opcodes + s390_num_opcodes;
      for (opcode = s390_opcodes + opc_index[(int) buffer[0]];
	   (opcode < opcode_end) && (buffer[0] == opcode->opcode[0]);
	   opcode++)
	{
	  const struct s390_operand *operand;
	  const unsigned char *opindex;

	  /* Check architecture.  */
	  if (!(opcode->modes & current_arch_mask))
	    continue;
	  /* Check signature of the opcode.  */
	  if ((buffer[1] & opcode->mask[1]) != opcode->opcode[1]
	      || (buffer[2] & opcode->mask[2]) != opcode->opcode[2]
	      || (buffer[3] & opcode->mask[3]) != opcode->opcode[3]
	      || (buffer[4] & opcode->mask[4]) != opcode->opcode[4]
	      || (buffer[5] & opcode->mask[5]) != opcode->opcode[5])
	    continue;

	  /* The instruction is valid.  */
	  if (opcode->operands[0] != 0)
	    (*info->fprintf_func) (info->stream, "%s\t", opcode->name);
	  else
	    (*info->fprintf_func) (info->stream, "%s", opcode->name);

	  /* Extract the operands.  */
	  separator = 0;
	  for (opindex = opcode->operands; *opindex != 0; opindex++)
	    {
	      unsigned int value;

	      operand = s390_operands + *opindex;
	      value = s390_extract_operand (buffer, operand);

	      if ((operand->flags & S390_OPERAND_INDEX) && value == 0)
		continue;
	      if ((operand->flags & S390_OPERAND_BASE) &&
		  value == 0 && separator == '(')
		{
		  separator = ',';
		  continue;
		}

	      if (separator)
		(*info->fprintf_func) (info->stream, "%c", separator);

	      if (operand->flags & S390_OPERAND_GPR)
		(*info->fprintf_func) (info->stream, "%%r%i", value);
	      else if (operand->flags & S390_OPERAND_FPR)
		(*info->fprintf_func) (info->stream, "%%f%i", value);
	      else if (operand->flags & S390_OPERAND_AR)
		(*info->fprintf_func) (info->stream, "%%a%i", value);
	      else if (operand->flags & S390_OPERAND_CR)
		(*info->fprintf_func) (info->stream, "%%c%i", value);
	      else if (operand->flags & S390_OPERAND_PCREL)
		(*info->print_address_func) (memaddr + (int) value, info);
	      else if (operand->flags & S390_OPERAND_SIGNED)
		(*info->fprintf_func) (info->stream, "%i", (int) value);
	      else
		(*info->fprintf_func) (info->stream, "%u", value);

	      if (operand->flags & S390_OPERAND_DISP)
		{
		  separator = '(';
		}
	      else if (operand->flags & S390_OPERAND_BASE)
		{
		  (*info->fprintf_func) (info->stream, ")");
		  separator = ',';
		}
	      else
		separator = ',';
	    }

	  /* Found instruction, printed it, return its size.  */
	  return opsize;
	}
      /* No matching instruction found, fall through to hex print.  */
    }

  if (bufsize >= 4)
    {
      value = (unsigned int) buffer[0];
      value = (value << 8) + (unsigned int) buffer[1];
      value = (value << 8) + (unsigned int) buffer[2];
      value = (value << 8) + (unsigned int) buffer[3];
      (*info->fprintf_func) (info->stream, ".long\t0x%08x", value);
      return 4;
    }
  else if (bufsize >= 2)
    {
      value = (unsigned int) buffer[0];
      value = (value << 8) + (unsigned int) buffer[1];
      (*info->fprintf_func) (info->stream, ".short\t0x%04x", value);
      return 2;
    }
  else
    {
      value = (unsigned int) buffer[0];
      (*info->fprintf_func) (info->stream, ".byte\t0x%02x", value);
      return 1;
    }
}
/* s390-opc.c -- S390 opcode list
   Copyright 2000, 2001, 2003 Free Software Foundation, Inc.
   Contributed by Martin Schwidefsky (schwidefsky@de.ibm.com).

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
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin Street - Fifth Floor, Boston, MA
   02110-1301, USA.  */

#include <stdio.h>

/* This file holds the S390 opcode table.  The opcode table
   includes almost all of the extended instruction mnemonics.  This
   permits the disassembler to use them, and simplifies the assembler
   logic, at the cost of increasing the table size.  The table is
   strictly constant data, so the compiler should be able to put it in
   the .text section.

   This file also holds the operand table.  All knowledge about
   inserting operands into instructions and vice-versa is kept in this
   file.  */

/* The operands table.
   The fields are bits, shift, insert, extract, flags.  */

const struct s390_operand s390_operands[] =
{
#define UNUSED 0
  { 0, 0, 0 },                    /* Indicates the end of the operand list */

#define R_8    1                  /* GPR starting at position 8 */
  { 4, 8, S390_OPERAND_GPR },
#define R_12   2                  /* GPR starting at position 12 */
  { 4, 12, S390_OPERAND_GPR },
#define R_16   3                  /* GPR starting at position 16 */
  { 4, 16, S390_OPERAND_GPR },
#define R_20   4                  /* GPR starting at position 20 */
  { 4, 20, S390_OPERAND_GPR },
#define R_24   5                  /* GPR starting at position 24 */
  { 4, 24, S390_OPERAND_GPR },
#define R_28   6                  /* GPR starting at position 28 */
  { 4, 28, S390_OPERAND_GPR },
#define R_32   7                  /* GPR starting at position 32 */
  { 4, 32, S390_OPERAND_GPR },

#define F_8    8                  /* FPR starting at position 8 */
  { 4, 8, S390_OPERAND_FPR },
#define F_12   9                  /* FPR starting at position 12 */
  { 4, 12, S390_OPERAND_FPR },
#define F_16   10                 /* FPR starting at position 16 */
  { 4, 16, S390_OPERAND_FPR },
#define F_20   11                 /* FPR starting at position 16 */
  { 4, 16, S390_OPERAND_FPR },
#define F_24   12                 /* FPR starting at position 24 */
  { 4, 24, S390_OPERAND_FPR },
#define F_28   13                 /* FPR starting at position 28 */
  { 4, 28, S390_OPERAND_FPR },
#define F_32   14                 /* FPR starting at position 32 */
  { 4, 32, S390_OPERAND_FPR },

#define A_8    15                 /* Access reg. starting at position 8 */
  { 4, 8, S390_OPERAND_AR },
#define A_12   16                 /* Access reg. starting at position 12 */
  { 4, 12, S390_OPERAND_AR },
#define A_24   17                 /* Access reg. starting at position 24 */
  { 4, 24, S390_OPERAND_AR },
#define A_28   18                 /* Access reg. starting at position 28 */
  { 4, 28, S390_OPERAND_AR },

#define C_8    19                 /* Control reg. starting at position 8 */
  { 4, 8, S390_OPERAND_CR },
#define C_12   20                 /* Control reg. starting at position 12 */
  { 4, 12, S390_OPERAND_CR },

#define B_16   21                 /* Base register starting at position 16 */
  { 4, 16, S390_OPERAND_BASE|S390_OPERAND_GPR },
#define B_32   22                 /* Base register starting at position 32 */
  { 4, 32, S390_OPERAND_BASE|S390_OPERAND_GPR },

#define X_12   23                 /* Index register starting at position 12 */
  { 4, 12, S390_OPERAND_INDEX|S390_OPERAND_GPR },

#define D_20   24                 /* Displacement starting at position 20 */
  { 12, 20, S390_OPERAND_DISP },
#define D_36   25                 /* Displacement starting at position 36 */
  { 12, 36, S390_OPERAND_DISP },
#define D20_20 26		  /* 20 bit displacement starting at 20 */
  { 20, 20, S390_OPERAND_DISP|S390_OPERAND_SIGNED },

#define L4_8   27                 /* 4 bit length starting at position 8 */
  { 4, 8, S390_OPERAND_LENGTH },
#define L4_12  28                 /* 4 bit length starting at position 12 */
  { 4, 12, S390_OPERAND_LENGTH },
#define L8_8   29                 /* 8 bit length starting at position 8 */
  { 8, 8, S390_OPERAND_LENGTH },

#define U4_8   30                 /* 4 bit unsigned value starting at 8 */
  { 4, 8, 0 },
#define U4_12  31                 /* 4 bit unsigned value starting at 12 */
  { 4, 12, 0 },
#define U4_16  32                 /* 4 bit unsigned value starting at 16 */
  { 4, 16, 0 },
#define U4_20  33                 /* 4 bit unsigned value starting at 20 */
  { 4, 20, 0 },
#define U8_8   34                 /* 8 bit unsigned value starting at 8 */
  { 8, 8, 0 },
#define U8_16  35                 /* 8 bit unsigned value starting at 16 */
  { 8, 16, 0 },
#define I16_16 36                 /* 16 bit signed value starting at 16 */
  { 16, 16, S390_OPERAND_SIGNED },
#define U16_16 37                 /* 16 bit unsigned value starting at 16 */
  { 16, 16, 0 },
#define J16_16 38                 /* PC relative jump offset at 16 */
  { 16, 16, S390_OPERAND_PCREL },
#define J32_16 39                 /* PC relative long offset at 16 */
  { 32, 16, S390_OPERAND_PCREL },
#define I32_16 40		  /* 32 bit signed value starting at 16 */
  { 32, 16, S390_OPERAND_SIGNED },
#define U32_16 41		  /* 32 bit unsigned value starting at 16 */
  { 32, 16, 0 },
#define M_16   42                 /* 4 bit optional mask starting at 16 */
  { 4, 16, S390_OPERAND_OPTIONAL },
#define RO_28  43                 /* optional GPR starting at position 28 */
  { 4, 28, (S390_OPERAND_GPR | S390_OPERAND_OPTIONAL) }

};


/* Macros used to form opcodes.  */

/* 8/16/48 bit opcodes.  */
#define OP8(x) { x, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define OP16(x) { x >> 8, x & 255, 0x00, 0x00, 0x00, 0x00 }
#define OP48(x) { x >> 40, (x >> 32) & 255, (x >> 24) & 255, \
                  (x >> 16) & 255, (x >> 8) & 255, x & 255}

/* The new format of the INSTR_x_y and MASK_x_y defines is based
   on the following rules:
   1) the middle part of the definition (x in INSTR_x_y) is the official
      names of the instruction format that you can find in the principals
      of operation.
   2) the last part of the definition (y in INSTR_x_y) gives you an idea
      which operands the binary represenation of the instruction has.
      The meanings of the letters in y are:
      a - access register
      c - control register
      d - displacement, 12 bit
      f - floating pointer register
      i - signed integer, 4, 8, 16 or 32 bit
      l - length, 4 or 8 bit
      p - pc relative
      r - general purpose register
      u - unsigned integer, 4, 8, 16 or 32 bit
      m - mode field, 4 bit
      0 - operand skipped.
      The order of the letters reflects the layout of the format in
      storage and not the order of the paramaters of the instructions.
      The use of the letters is not a 100% match with the PoP but it is
      quite close.

      For example the instruction "mvo" is defined in the PoP as follows:

      MVO  D1(L1,B1),D2(L2,B2)   [SS]

      --------------------------------------
      | 'F1' | L1 | L2 | B1 | D1 | B2 | D2 |
      --------------------------------------
       0      8    12   16   20   32   36

      The instruction format is: INSTR_SS_LLRDRD / MASK_SS_LLRDRD.  */

#define INSTR_E          2, { 0,0,0,0,0,0 }                    /* e.g. pr    */
#define INSTR_RIE_RRP    6, { R_8,R_12,J16_16,0,0,0 }          /* e.g. brxhg */
#define INSTR_RIL_0P     6, { J32_16,0,0,0,0 }                 /* e.g. jg    */
#define INSTR_RIL_RP     6, { R_8,J32_16,0,0,0,0 }             /* e.g. brasl */
#define INSTR_RIL_UP     6, { U4_8,J32_16,0,0,0,0 }            /* e.g. brcl  */
#define INSTR_RIL_RI     6, { R_8,I32_16,0,0,0,0 }             /* e.g. afi   */
#define INSTR_RIL_RU     6, { R_8,U32_16,0,0,0,0 }             /* e.g. alfi  */
#define INSTR_RI_0P      4, { J16_16,0,0,0,0,0 }               /* e.g. j     */
#define INSTR_RI_RI      4, { R_8,I16_16,0,0,0,0 }             /* e.g. ahi   */
#define INSTR_RI_RP      4, { R_8,J16_16,0,0,0,0 }             /* e.g. brct  */
#define INSTR_RI_RU      4, { R_8,U16_16,0,0,0,0 }             /* e.g. tml   */
#define INSTR_RI_UP      4, { U4_8,J16_16,0,0,0,0 }            /* e.g. brc   */
#define INSTR_RRE_00     4, { 0,0,0,0,0,0 }                    /* e.g. palb  */
#define INSTR_RRE_0R     4, { R_28,0,0,0,0,0 }                 /* e.g. tb    */
#define INSTR_RRE_AA     4, { A_24,A_28,0,0,0,0 }              /* e.g. cpya  */
#define INSTR_RRE_AR     4, { A_24,R_28,0,0,0,0 }              /* e.g. sar   */
#define INSTR_RRE_F0     4, { F_24,0,0,0,0,0 }                 /* e.g. sqer  */
#define INSTR_RRE_FF     4, { F_24,F_28,0,0,0,0 }              /* e.g. debr  */
#define INSTR_RRE_R0     4, { R_24,0,0,0,0,0 }                 /* e.g. ipm   */
#define INSTR_RRE_RA     4, { R_24,A_28,0,0,0,0 }              /* e.g. ear   */
#define INSTR_RRE_RF     4, { R_24,F_28,0,0,0,0 }              /* e.g. cefbr */
#define INSTR_RRE_RR     4, { R_24,R_28,0,0,0,0 }              /* e.g. lura  */
#define INSTR_RRE_FR     4, { F_24,R_28,0,0,0,0 }              /* e.g. ldgr  */
/* Actually efpc and sfpc do not take an optional operand.
   This is just a workaround for existing code e.g. glibc.  */
#define INSTR_RRE_RR_OPT 4, { R_24,RO_28,0,0,0,0 }             /* efpc, sfpc */
#define INSTR_RRF_F0FF   4, { F_16,F_24,F_28,0,0,0 }           /* e.g. madbr */
#define INSTR_RRF_F0FF2  4, { F_24,F_16,F_28,0,0,0 }           /* e.g. cpsdr */
#define INSTR_RRF_F0FR   4, { F_24,F_16,R_28,0,0,0 }           /* e.g. iedtr */
#define INSTR_RRF_FUFF   4, { F_24,F_16,F_28,U4_20,0,0 }       /* e.g. didbr */
#define INSTR_RRF_RURR   4, { R_24,R_28,R_16,U4_20,0,0 }       /* e.g. .insn */
#define INSTR_RRF_R0RR   4, { R_24,R_28,R_16,0,0,0 }           /* e.g. idte  */
#define INSTR_RRF_U0FF   4, { F_24,U4_16,F_28,0,0,0 }          /* e.g. fixr  */
#define INSTR_RRF_U0RF   4, { R_24,U4_16,F_28,0,0,0 }          /* e.g. cfebr */
#define INSTR_RRF_UUFF   4, { F_24,U4_16,F_28,U4_20,0,0 }      /* e.g. fidtr */
#define INSTR_RRF_0UFF   4, { F_24,F_28,U4_20,0,0,0 }          /* e.g. ldetr */
#define INSTR_RRF_FFFU   4, { F_24,F_16,F_28,U4_20,0,0 }       /* e.g. qadtr */
#define INSTR_RRF_M0RR   4, { R_24,R_28,M_16,0,0,0 }           /* e.g. sske  */
#define INSTR_RR_0R      2, { R_12, 0,0,0,0,0 }                /* e.g. br    */
#define INSTR_RR_FF      2, { F_8,F_12,0,0,0,0 }               /* e.g. adr   */
#define INSTR_RR_R0      2, { R_8, 0,0,0,0,0 }                 /* e.g. spm   */
#define INSTR_RR_RR      2, { R_8,R_12,0,0,0,0 }               /* e.g. lr    */
#define INSTR_RR_U0      2, { U8_8, 0,0,0,0,0 }                /* e.g. svc   */
#define INSTR_RR_UR      2, { U4_8,R_12,0,0,0,0 }              /* e.g. bcr   */
#define INSTR_RRR_F0FF   4, { F_24,F_28,F_16,0,0,0 }           /* e.g. ddtr  */
#define INSTR_RSE_RRRD   6, { R_8,R_12,D_20,B_16,0,0 }         /* e.g. lmh   */
#define INSTR_RSE_CCRD   6, { C_8,C_12,D_20,B_16,0,0 }         /* e.g. lmh   */
#define INSTR_RSE_RURD   6, { R_8,U4_12,D_20,B_16,0,0 }        /* e.g. icmh  */
#define INSTR_RSL_R0RD   6, { R_8,D_20,B_16,0,0,0 }            /* e.g. tp    */
#define INSTR_RSI_RRP    4, { R_8,R_12,J16_16,0,0,0 }          /* e.g. brxh  */
#define INSTR_RSY_RRRD   6, { R_8,R_12,D20_20,B_16,0,0 }       /* e.g. stmy  */
#define INSTR_RSY_RURD   6, { R_8,U4_12,D20_20,B_16,0,0 }      /* e.g. icmh  */
#define INSTR_RSY_AARD   6, { A_8,A_12,D20_20,B_16,0,0 }       /* e.g. lamy  */
#define INSTR_RSY_CCRD   6, { C_8,C_12,D20_20,B_16,0,0 }       /* e.g. lamy  */
#define INSTR_RS_AARD    4, { A_8,A_12,D_20,B_16,0,0 }         /* e.g. lam   */
#define INSTR_RS_CCRD    4, { C_8,C_12,D_20,B_16,0,0 }         /* e.g. lctl  */
#define INSTR_RS_R0RD    4, { R_8,D_20,B_16,0,0,0 }            /* e.g. sll   */
#define INSTR_RS_RRRD    4, { R_8,R_12,D_20,B_16,0,0 }         /* e.g. cs    */
#define INSTR_RS_RURD    4, { R_8,U4_12,D_20,B_16,0,0 }        /* e.g. icm   */
#define INSTR_RXE_FRRD   6, { F_8,D_20,X_12,B_16,0,0 }         /* e.g. axbr  */
#define INSTR_RXE_RRRD   6, { R_8,D_20,X_12,B_16,0,0 }         /* e.g. lg    */
#define INSTR_RXF_FRRDF  6, { F_32,F_8,D_20,X_12,B_16,0 }      /* e.g. madb  */
#define INSTR_RXF_RRRDR  6, { R_32,R_8,D_20,X_12,B_16,0 }      /* e.g. .insn */
#define INSTR_RXY_RRRD   6, { R_8,D20_20,X_12,B_16,0,0 }       /* e.g. ly    */
#define INSTR_RXY_FRRD   6, { F_8,D20_20,X_12,B_16,0,0 }       /* e.g. ley   */
#define INSTR_RX_0RRD    4, { D_20,X_12,B_16,0,0,0 }           /* e.g. be    */
#define INSTR_RX_FRRD    4, { F_8,D_20,X_12,B_16,0,0 }         /* e.g. ae    */
#define INSTR_RX_RRRD    4, { R_8,D_20,X_12,B_16,0,0 }         /* e.g. l     */
#define INSTR_RX_URRD    4, { U4_8,D_20,X_12,B_16,0,0 }        /* e.g. bc    */
#define INSTR_SI_URD     4, { D_20,B_16,U8_8,0,0,0 }           /* e.g. cli   */
#define INSTR_SIY_URD    6, { D20_20,B_16,U8_8,0,0,0 }         /* e.g. tmy   */
#define INSTR_SSE_RDRD   6, { D_20,B_16,D_36,B_32,0,0 }        /* e.g. mvsdk */
#define INSTR_SS_L0RDRD  6, { D_20,L8_8,B_16,D_36,B_32,0     } /* e.g. mvc   */
#define INSTR_SS_L2RDRD  6, { D_20,B_16,D_36,L8_8,B_32,0     } /* e.g. pka   */
#define INSTR_SS_LIRDRD  6, { D_20,L4_8,B_16,D_36,B_32,U4_12 } /* e.g. srp   */
#define INSTR_SS_LLRDRD  6, { D_20,L4_8,B_16,D_36,L4_12,B_32 } /* e.g. pack  */
#define INSTR_SS_RRRDRD  6, { D_20,R_8,B_16,D_36,B_32,R_12 }   /* e.g. mvck  */
#define INSTR_SS_RRRDRD2 6, { R_8,D_20,B_16,R_12,D_36,B_32 }   /* e.g. plo   */
#define INSTR_SS_RRRDRD3 6, { R_8,R_12,D_20,B_16,D_36,B_32 }   /* e.g. lmd   */
#define INSTR_S_00       4, { 0,0,0,0,0,0 }                    /* e.g. hsch  */
#define INSTR_S_RD       4, { D_20,B_16,0,0,0,0 }              /* e.g. lpsw  */
#define INSTR_SSF_RRDRD  6, { D_20,B_16,D_36,B_32,R_8,0 }      /* e.g. mvcos */

#define MASK_E           { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIE_RRP     { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RIL_0P      { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_RP      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_UP      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_RI      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RIL_RU      { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_0P       { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_RI       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_RP       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_RU       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RI_UP       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRE_00      { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 }
#define MASK_RRE_0R      { 0xff, 0xff, 0xff, 0xf0, 0x00, 0x00 }
#define MASK_RRE_AA      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_AR      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_F0      { 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00 }
#define MASK_RRE_FF      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_R0      { 0xff, 0xff, 0xff, 0x0f, 0x00, 0x00 }
#define MASK_RRE_RA      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_RF      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_RR      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_FR      { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRE_RR_OPT  { 0xff, 0xff, 0xff, 0x00, 0x00, 0x00 }
#define MASK_RRF_F0FF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_F0FF2   { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_F0FR    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_FUFF    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_RURR    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_R0RR    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_U0FF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_U0RF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RRF_UUFF    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_0UFF    { 0xff, 0xff, 0xf0, 0x00, 0x00, 0x00 }
#define MASK_RRF_FFFU    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRF_M0RR    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RR_0R       { 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_FF       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_R0       { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_RR       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_U0       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RR_UR       { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RRR_F0FF    { 0xff, 0xff, 0x0f, 0x00, 0x00, 0x00 }
#define MASK_RSE_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSE_CCRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSE_RURD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSL_R0RD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSI_RRP     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_AARD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_CCRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_R0RD     { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_RRRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RS_RURD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RSY_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSY_RURD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSY_AARD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RSY_CCRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXE_FRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXE_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXF_FRRDF   { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXF_RRRDR   { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXY_RRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RXY_FRRD    { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_RX_0RRD     { 0xff, 0xf0, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RX_FRRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RX_RRRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_RX_URRD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SI_URD      { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SIY_URD     { 0xff, 0x00, 0x00, 0x00, 0x00, 0xff }
#define MASK_SSE_RDRD    { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_L0RDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_L2RDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_LIRDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_LLRDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_RRRDRD   { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_RRRDRD2  { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SS_RRRDRD3  { 0xff, 0x00, 0x00, 0x00, 0x00, 0x00 }
#define MASK_S_00        { 0xff, 0xff, 0xff, 0xff, 0x00, 0x00 }
#define MASK_S_RD        { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 }
#define MASK_SSF_RRDRD   { 0xff, 0x0f, 0x00, 0x00, 0x00, 0x00 }

/* The opcode formats table (blueprints for .insn pseudo mnemonic).  */

const struct s390_opcode s390_opformats[] =
  {
  { "e",	OP8(0x00LL),	MASK_E,		INSTR_E,	3, 0 },
  { "ri",	OP8(0x00LL),	MASK_RI_RI,	INSTR_RI_RI,	3, 0 },
  { "rie",	OP8(0x00LL),	MASK_RIE_RRP,	INSTR_RIE_RRP,	3, 0 },
  { "ril",	OP8(0x00LL),	MASK_RIL_RP,	INSTR_RIL_RP,	3, 0 },
  { "rilu",	OP8(0x00LL),	MASK_RIL_RU,	INSTR_RIL_RU,	3, 0 },
  { "rr",	OP8(0x00LL),	MASK_RR_RR,	INSTR_RR_RR,	3, 0 },
  { "rre",	OP8(0x00LL),	MASK_RRE_RR,	INSTR_RRE_RR,	3, 0 },
  { "rrf",	OP8(0x00LL),	MASK_RRF_RURR,	INSTR_RRF_RURR,	3, 0 },
  { "rs",	OP8(0x00LL),	MASK_RS_RRRD,	INSTR_RS_RRRD,	3, 0 },
  { "rse",	OP8(0x00LL),	MASK_RSE_RRRD,	INSTR_RSE_RRRD,	3, 0 },
  { "rsi",	OP8(0x00LL),	MASK_RSI_RRP,	INSTR_RSI_RRP,	3, 0 },
  { "rsy",	OP8(0x00LL),	MASK_RSY_RRRD,	INSTR_RSY_RRRD,	3, 3 },
  { "rx",	OP8(0x00LL),	MASK_RX_RRRD,	INSTR_RX_RRRD,	3, 0 },
  { "rxe",	OP8(0x00LL),	MASK_RXE_RRRD,	INSTR_RXE_RRRD,	3, 0 },
  { "rxf",	OP8(0x00LL),	MASK_RXF_RRRDR,	INSTR_RXF_RRRDR,3, 0 },
  { "rxy",	OP8(0x00LL),	MASK_RXY_RRRD,	INSTR_RXY_RRRD,	3, 3 },
  { "s",	OP8(0x00LL),	MASK_S_RD,	INSTR_S_RD,	3, 0 },
  { "si",	OP8(0x00LL),	MASK_SI_URD,	INSTR_SI_URD,	3, 0 },
  { "siy",	OP8(0x00LL),	MASK_SIY_URD,	INSTR_SIY_URD,	3, 3 },
  { "ss",	OP8(0x00LL),	MASK_SS_RRRDRD,	INSTR_SS_RRRDRD,3, 0 },
  { "sse",	OP8(0x00LL),	MASK_SSE_RDRD,	INSTR_SSE_RDRD,	3, 0 },
  { "ssf",	OP8(0x00LL),	MASK_SSF_RRDRD,	INSTR_SSF_RRDRD,3, 0 },
};

const int s390_num_opformats =
  sizeof (s390_opformats) / sizeof (s390_opformats[0]);

/* The opcode table. This file was generated by s390-mkopc.

   The format of the opcode table is:

   NAME	     OPCODE	MASK	OPERANDS

   Name is the name of the instruction.
   OPCODE is the instruction opcode.
   MASK is the opcode mask; this is used to tell the disassembler
     which bits in the actual opcode must match OPCODE.
   OPERANDS is the list of operands.

   The disassembler reads the table in order and prints the first
   instruction which matches.  */

const struct s390_opcode s390_opcodes[] =
  {
  { "dp", OP8(0xfdLL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "mp", OP8(0xfcLL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "sp", OP8(0xfbLL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "ap", OP8(0xfaLL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "cp", OP8(0xf9LL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "zap", OP8(0xf8LL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "unpk", OP8(0xf3LL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "pack", OP8(0xf2LL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "mvo", OP8(0xf1LL), MASK_SS_LLRDRD, INSTR_SS_LLRDRD, 3, 0},
  { "srp", OP8(0xf0LL), MASK_SS_LIRDRD, INSTR_SS_LIRDRD, 3, 0},
  { "lmd", OP8(0xefLL), MASK_SS_RRRDRD3, INSTR_SS_RRRDRD3, 2, 2},
  { "plo", OP8(0xeeLL), MASK_SS_RRRDRD2, INSTR_SS_RRRDRD2, 3, 0},
  { "stdy", OP48(0xed0000000067LL), MASK_RXY_FRRD, INSTR_RXY_FRRD, 2, 3},
  { "stey", OP48(0xed0000000066LL), MASK_RXY_FRRD, INSTR_RXY_FRRD, 2, 3},
  { "ldy", OP48(0xed0000000065LL), MASK_RXY_FRRD, INSTR_RXY_FRRD, 2, 3},
  { "ley", OP48(0xed0000000064LL), MASK_RXY_FRRD, INSTR_RXY_FRRD, 2, 3},
  { "tgxt", OP48(0xed0000000059LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 2, 5},
  { "tcxt", OP48(0xed0000000058LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 2, 5},
  { "tgdt", OP48(0xed0000000055LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 2, 5},
  { "tcdt", OP48(0xed0000000054LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 2, 5},
  { "tget", OP48(0xed0000000051LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 2, 5},
  { "tcet", OP48(0xed0000000050LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 2, 5},
  { "srxt", OP48(0xed0000000049LL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 5},
  { "slxt", OP48(0xed0000000048LL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 5},
  { "srdt", OP48(0xed0000000041LL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 5},
  { "sldt", OP48(0xed0000000040LL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 5},
  { "msd", OP48(0xed000000003fLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 3},
  { "mad", OP48(0xed000000003eLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 3},
  { "myh", OP48(0xed000000003dLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 4},
  { "mayh", OP48(0xed000000003cLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 4},
  { "my", OP48(0xed000000003bLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 4},
  { "may", OP48(0xed000000003aLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 4},
  { "myl", OP48(0xed0000000039LL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 4},
  { "mayl", OP48(0xed0000000038LL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 2, 4},
  { "mee", OP48(0xed0000000037LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "sqe", OP48(0xed0000000034LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "mse", OP48(0xed000000002fLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 3},
  { "mae", OP48(0xed000000002eLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 3},
  { "lxe", OP48(0xed0000000026LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "lxd", OP48(0xed0000000025LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "lde", OP48(0xed0000000024LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "msdb", OP48(0xed000000001fLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 0},
  { "madb", OP48(0xed000000001eLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 0},
  { "ddb", OP48(0xed000000001dLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "mdb", OP48(0xed000000001cLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "sdb", OP48(0xed000000001bLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "adb", OP48(0xed000000001aLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "cdb", OP48(0xed0000000019LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "kdb", OP48(0xed0000000018LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "meeb", OP48(0xed0000000017LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "sqdb", OP48(0xed0000000015LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "sqeb", OP48(0xed0000000014LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "tcxb", OP48(0xed0000000012LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "tcdb", OP48(0xed0000000011LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "tceb", OP48(0xed0000000010LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "mseb", OP48(0xed000000000fLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 0},
  { "maeb", OP48(0xed000000000eLL), MASK_RXF_FRRDF, INSTR_RXF_FRRDF, 3, 0},
  { "deb", OP48(0xed000000000dLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "mdeb", OP48(0xed000000000cLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "seb", OP48(0xed000000000bLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "aeb", OP48(0xed000000000aLL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "ceb", OP48(0xed0000000009LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "keb", OP48(0xed0000000008LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "mxdb", OP48(0xed0000000007LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "lxeb", OP48(0xed0000000006LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "lxdb", OP48(0xed0000000005LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "ldeb", OP48(0xed0000000004LL), MASK_RXE_FRRD, INSTR_RXE_FRRD, 3, 0},
  { "brxlg", OP48(0xec0000000045LL), MASK_RIE_RRP, INSTR_RIE_RRP, 2, 2},
  { "brxhg", OP48(0xec0000000044LL), MASK_RIE_RRP, INSTR_RIE_RRP, 2, 2},
  { "tp", OP48(0xeb00000000c0LL), MASK_RSL_R0RD, INSTR_RSL_R0RD, 3, 0},
  { "stamy", OP48(0xeb000000009bLL), MASK_RSY_AARD, INSTR_RSY_AARD, 2, 3},
  { "lamy", OP48(0xeb000000009aLL), MASK_RSY_AARD, INSTR_RSY_AARD, 2, 3},
  { "lmy", OP48(0xeb0000000098LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "lmh", OP48(0xeb0000000096LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "lmh", OP48(0xeb0000000096LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "stmy", OP48(0xeb0000000090LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "clclu", OP48(0xeb000000008fLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "mvclu", OP48(0xeb000000008eLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 3, 3},
  { "mvclu", OP48(0xeb000000008eLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 3, 0},
  { "icmy", OP48(0xeb0000000081LL), MASK_RSY_RURD, INSTR_RSY_RURD, 2, 3},
  { "icmh", OP48(0xeb0000000080LL), MASK_RSY_RURD, INSTR_RSY_RURD, 2, 3},
  { "icmh", OP48(0xeb0000000080LL), MASK_RSE_RURD, INSTR_RSE_RURD, 2, 2},
  { "xiy", OP48(0xeb0000000057LL), MASK_SIY_URD, INSTR_SIY_URD, 2, 3},
  { "oiy", OP48(0xeb0000000056LL), MASK_SIY_URD, INSTR_SIY_URD, 2, 3},
  { "cliy", OP48(0xeb0000000055LL), MASK_SIY_URD, INSTR_SIY_URD, 2, 3},
  { "niy", OP48(0xeb0000000054LL), MASK_SIY_URD, INSTR_SIY_URD, 2, 3},
  { "mviy", OP48(0xeb0000000052LL), MASK_SIY_URD, INSTR_SIY_URD, 2, 3},
  { "tmy", OP48(0xeb0000000051LL), MASK_SIY_URD, INSTR_SIY_URD, 2, 3},
  { "bxleg", OP48(0xeb0000000045LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "bxleg", OP48(0xeb0000000045LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "bxhg", OP48(0xeb0000000044LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "bxhg", OP48(0xeb0000000044LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "cdsg", OP48(0xeb000000003eLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "cdsg", OP48(0xeb000000003eLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "cdsy", OP48(0xeb0000000031LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "csg", OP48(0xeb0000000030LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "csg", OP48(0xeb0000000030LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "lctlg", OP48(0xeb000000002fLL), MASK_RSY_CCRD, INSTR_RSY_CCRD, 2, 3},
  { "lctlg", OP48(0xeb000000002fLL), MASK_RSE_CCRD, INSTR_RSE_CCRD, 2, 2},
  { "stcmy", OP48(0xeb000000002dLL), MASK_RSY_RURD, INSTR_RSY_RURD, 2, 3},
  { "stcmh", OP48(0xeb000000002cLL), MASK_RSY_RURD, INSTR_RSY_RURD, 2, 3},
  { "stcmh", OP48(0xeb000000002cLL), MASK_RSE_RURD, INSTR_RSE_RURD, 2, 2},
  { "stmh", OP48(0xeb0000000026LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "stmh", OP48(0xeb0000000026LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "stctg", OP48(0xeb0000000025LL), MASK_RSY_CCRD, INSTR_RSY_CCRD, 2, 3},
  { "stctg", OP48(0xeb0000000025LL), MASK_RSE_CCRD, INSTR_RSE_CCRD, 2, 2},
  { "stmg", OP48(0xeb0000000024LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "stmg", OP48(0xeb0000000024LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "clmy", OP48(0xeb0000000021LL), MASK_RSY_RURD, INSTR_RSY_RURD, 2, 3},
  { "clmh", OP48(0xeb0000000020LL), MASK_RSY_RURD, INSTR_RSY_RURD, 2, 3},
  { "clmh", OP48(0xeb0000000020LL), MASK_RSE_RURD, INSTR_RSE_RURD, 2, 2},
  { "rll", OP48(0xeb000000001dLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 3, 3},
  { "rll", OP48(0xeb000000001dLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 3, 2},
  { "rllg", OP48(0xeb000000001cLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "rllg", OP48(0xeb000000001cLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "csy", OP48(0xeb0000000014LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "tracg", OP48(0xeb000000000fLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "tracg", OP48(0xeb000000000fLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "sllg", OP48(0xeb000000000dLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "sllg", OP48(0xeb000000000dLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "srlg", OP48(0xeb000000000cLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "srlg", OP48(0xeb000000000cLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "slag", OP48(0xeb000000000bLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "slag", OP48(0xeb000000000bLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "srag", OP48(0xeb000000000aLL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "srag", OP48(0xeb000000000aLL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "lmg", OP48(0xeb0000000004LL), MASK_RSY_RRRD, INSTR_RSY_RRRD, 2, 3},
  { "lmg", OP48(0xeb0000000004LL), MASK_RSE_RRRD, INSTR_RSE_RRRD, 2, 2},
  { "unpka", OP8(0xeaLL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "pka", OP8(0xe9LL), MASK_SS_L2RDRD, INSTR_SS_L2RDRD, 3, 0},
  { "mvcin", OP8(0xe8LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "mvcdk", OP16(0xe50fLL), MASK_SSE_RDRD, INSTR_SSE_RDRD, 3, 0},
  { "mvcsk", OP16(0xe50eLL), MASK_SSE_RDRD, INSTR_SSE_RDRD, 3, 0},
  { "tprot", OP16(0xe501LL), MASK_SSE_RDRD, INSTR_SSE_RDRD, 3, 0},
  { "strag", OP48(0xe50000000002LL), MASK_SSE_RDRD, INSTR_SSE_RDRD, 2, 2},
  { "lasp", OP16(0xe500LL), MASK_SSE_RDRD, INSTR_SSE_RDRD, 3, 0},
  { "slb", OP48(0xe30000000099LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "slb", OP48(0xe30000000099LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "alc", OP48(0xe30000000098LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "alc", OP48(0xe30000000098LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "dl", OP48(0xe30000000097LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "dl", OP48(0xe30000000097LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "ml", OP48(0xe30000000096LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "ml", OP48(0xe30000000096LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "llh", OP48(0xe30000000095LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 4},
  { "llc", OP48(0xe30000000094LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 4},
  { "llgh", OP48(0xe30000000091LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "llgh", OP48(0xe30000000091LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "llgc", OP48(0xe30000000090LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "llgc", OP48(0xe30000000090LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "lpq", OP48(0xe3000000008fLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lpq", OP48(0xe3000000008fLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "stpq", OP48(0xe3000000008eLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "stpq", OP48(0xe3000000008eLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "slbg", OP48(0xe30000000089LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "slbg", OP48(0xe30000000089LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "alcg", OP48(0xe30000000088LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "alcg", OP48(0xe30000000088LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "dlg", OP48(0xe30000000087LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "dlg", OP48(0xe30000000087LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "mlg", OP48(0xe30000000086LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "mlg", OP48(0xe30000000086LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "xg", OP48(0xe30000000082LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "xg", OP48(0xe30000000082LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "og", OP48(0xe30000000081LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "og", OP48(0xe30000000081LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "ng", OP48(0xe30000000080LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "ng", OP48(0xe30000000080LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "shy", OP48(0xe3000000007bLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "ahy", OP48(0xe3000000007aLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "chy", OP48(0xe30000000079LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lhy", OP48(0xe30000000078LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lgb", OP48(0xe30000000077LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lb", OP48(0xe30000000076LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "icy", OP48(0xe30000000073LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "stcy", OP48(0xe30000000072LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lay", OP48(0xe30000000071LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "sthy", OP48(0xe30000000070LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "sly", OP48(0xe3000000005fLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "aly", OP48(0xe3000000005eLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "sy", OP48(0xe3000000005bLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "ay", OP48(0xe3000000005aLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "cy", OP48(0xe30000000059LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "ly", OP48(0xe30000000058LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "xy", OP48(0xe30000000057LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "oy", OP48(0xe30000000056LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "cly", OP48(0xe30000000055LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "ny", OP48(0xe30000000054LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "msy", OP48(0xe30000000051LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "sty", OP48(0xe30000000050LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "bctg", OP48(0xe30000000046LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "bctg", OP48(0xe30000000046LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "strvh", OP48(0xe3000000003fLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "strvh", OP48(0xe3000000003fLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "strv", OP48(0xe3000000003eLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "strv", OP48(0xe3000000003eLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "clgf", OP48(0xe30000000031LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "clgf", OP48(0xe30000000031LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "cgf", OP48(0xe30000000030LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "cgf", OP48(0xe30000000030LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "strvg", OP48(0xe3000000002fLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "strvg", OP48(0xe3000000002fLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "cvdg", OP48(0xe3000000002eLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "cvdg", OP48(0xe3000000002eLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "cvdy", OP48(0xe30000000026LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "stg", OP48(0xe30000000024LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "stg", OP48(0xe30000000024LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "clg", OP48(0xe30000000021LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "clg", OP48(0xe30000000021LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "cg", OP48(0xe30000000020LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "cg", OP48(0xe30000000020LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "lrvh", OP48(0xe3000000001fLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "lrvh", OP48(0xe3000000001fLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "lrv", OP48(0xe3000000001eLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 3, 3},
  { "lrv", OP48(0xe3000000001eLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 3, 2},
  { "dsgf", OP48(0xe3000000001dLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "dsgf", OP48(0xe3000000001dLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "msgf", OP48(0xe3000000001cLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "msgf", OP48(0xe3000000001cLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "slgf", OP48(0xe3000000001bLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "slgf", OP48(0xe3000000001bLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "algf", OP48(0xe3000000001aLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "algf", OP48(0xe3000000001aLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "sgf", OP48(0xe30000000019LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "sgf", OP48(0xe30000000019LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "agf", OP48(0xe30000000018LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "agf", OP48(0xe30000000018LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "llgt", OP48(0xe30000000017LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "llgt", OP48(0xe30000000017LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "llgf", OP48(0xe30000000016LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "llgf", OP48(0xe30000000016LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "lgh", OP48(0xe30000000015LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lgh", OP48(0xe30000000015LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "lgf", OP48(0xe30000000014LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lgf", OP48(0xe30000000014LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "lray", OP48(0xe30000000013LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lt", OP48(0xe30000000012LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 4},
  { "lrvg", OP48(0xe3000000000fLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lrvg", OP48(0xe3000000000fLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "cvbg", OP48(0xe3000000000eLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "cvbg", OP48(0xe3000000000eLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "dsg", OP48(0xe3000000000dLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "dsg", OP48(0xe3000000000dLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "msg", OP48(0xe3000000000cLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "msg", OP48(0xe3000000000cLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "slg", OP48(0xe3000000000bLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "slg", OP48(0xe3000000000bLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "alg", OP48(0xe3000000000aLL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "alg", OP48(0xe3000000000aLL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "sg", OP48(0xe30000000009LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "sg", OP48(0xe30000000009LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "ag", OP48(0xe30000000008LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "ag", OP48(0xe30000000008LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "cvby", OP48(0xe30000000006LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lg", OP48(0xe30000000004LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lg", OP48(0xe30000000004LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "lrag", OP48(0xe30000000003LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 3},
  { "lrag", OP48(0xe30000000003LL), MASK_RXE_RRRD, INSTR_RXE_RRRD, 2, 2},
  { "ltg", OP48(0xe30000000002LL), MASK_RXY_RRRD, INSTR_RXY_RRRD, 2, 4},
  { "unpku", OP8(0xe2LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "pku", OP8(0xe1LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "edmk", OP8(0xdfLL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "ed", OP8(0xdeLL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "trt", OP8(0xddLL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "tr", OP8(0xdcLL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "mvcs", OP8(0xdbLL), MASK_SS_RRRDRD, INSTR_SS_RRRDRD, 3, 0},
  { "mvcp", OP8(0xdaLL), MASK_SS_RRRDRD, INSTR_SS_RRRDRD, 3, 0},
  { "mvck", OP8(0xd9LL), MASK_SS_RRRDRD, INSTR_SS_RRRDRD, 3, 0},
  { "xc", OP8(0xd7LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "oc", OP8(0xd6LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "clc", OP8(0xd5LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "nc", OP8(0xd4LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "mvz", OP8(0xd3LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "mvc", OP8(0xd2LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "mvn", OP8(0xd1LL), MASK_SS_L0RDRD, INSTR_SS_L0RDRD, 3, 0},
  { "csst", OP16(0xc802LL), MASK_SSF_RRDRD, INSTR_SSF_RRDRD, 2, 5},
  { "ectg", OP16(0xc801LL), MASK_SSF_RRDRD, INSTR_SSF_RRDRD, 2, 5},
  { "mvcos", OP16(0xc800LL), MASK_SSF_RRDRD, INSTR_SSF_RRDRD, 2, 4},
  { "clfi", OP16(0xc20fLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "clgfi", OP16(0xc20eLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "cfi", OP16(0xc20dLL), MASK_RIL_RI, INSTR_RIL_RI, 2, 4},
  { "cgfi", OP16(0xc20cLL), MASK_RIL_RI, INSTR_RIL_RI, 2, 4},
  { "alfi", OP16(0xc20bLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "algfi", OP16(0xc20aLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "afi", OP16(0xc209LL), MASK_RIL_RI, INSTR_RIL_RI, 2, 4},
  { "agfi", OP16(0xc208LL), MASK_RIL_RI, INSTR_RIL_RI, 2, 4},
  { "slfi", OP16(0xc205LL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "slgfi", OP16(0xc204LL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "jg", OP16(0xc0f4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgno", OP16(0xc0e4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnh", OP16(0xc0d4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnp", OP16(0xc0d4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgle", OP16(0xc0c4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnl", OP16(0xc0b4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnm", OP16(0xc0b4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jghe", OP16(0xc0a4LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnlh", OP16(0xc094LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jge", OP16(0xc084LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgz", OP16(0xc084LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgne", OP16(0xc074LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnz", OP16(0xc074LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jglh", OP16(0xc064LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnhe", OP16(0xc054LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgl", OP16(0xc044LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgm", OP16(0xc044LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgnle", OP16(0xc034LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgh", OP16(0xc024LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgp", OP16(0xc024LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "jgo", OP16(0xc014LL), MASK_RIL_0P, INSTR_RIL_0P, 3, 2},
  { "llilf", OP16(0xc00fLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "llihf", OP16(0xc00eLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "oilf", OP16(0xc00dLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "oihf", OP16(0xc00cLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "nilf", OP16(0xc00bLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "nihf", OP16(0xc00aLL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "iilf", OP16(0xc009LL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "iihf", OP16(0xc008LL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "xilf", OP16(0xc007LL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "xihf", OP16(0xc006LL), MASK_RIL_RU, INSTR_RIL_RU, 2, 4},
  { "brasl", OP16(0xc005LL), MASK_RIL_RP, INSTR_RIL_RP, 3, 2},
  { "brcl", OP16(0xc004LL), MASK_RIL_UP, INSTR_RIL_UP, 3, 2},
  { "lgfi", OP16(0xc001LL), MASK_RIL_RI, INSTR_RIL_RI, 2, 4},
  { "larl", OP16(0xc000LL), MASK_RIL_RP, INSTR_RIL_RP, 3, 2},
  { "icm", OP8(0xbfLL), MASK_RS_RURD, INSTR_RS_RURD, 3, 0},
  { "stcm", OP8(0xbeLL), MASK_RS_RURD, INSTR_RS_RURD, 3, 0},
  { "clm", OP8(0xbdLL), MASK_RS_RURD, INSTR_RS_RURD, 3, 0},
  { "cds", OP8(0xbbLL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "cs", OP8(0xbaLL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "cu42", OP16(0xb9b3LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cu41", OP16(0xb9b2LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cu24", OP16(0xb9b1LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cu14", OP16(0xb9b0LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "lptea", OP16(0xb9aaLL), MASK_RRF_RURR, INSTR_RRF_RURR, 2, 4},
  { "esea", OP16(0xb99dLL), MASK_RRE_R0, INSTR_RRE_R0, 2, 2},
  { "slbr", OP16(0xb999LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 2},
  { "alcr", OP16(0xb998LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 2},
  { "dlr", OP16(0xb997LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 2},
  { "mlr", OP16(0xb996LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 2},
  { "llhr", OP16(0xb995LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "llcr", OP16(0xb994LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "troo", OP16(0xb993LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 3, 4},
  { "troo", OP16(0xb993LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "trot", OP16(0xb992LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 3, 4},
  { "trot", OP16(0xb992LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "trto", OP16(0xb991LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 3, 4},
  { "trto", OP16(0xb991LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "trtt", OP16(0xb990LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 3, 4},
  { "trtt", OP16(0xb990LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "idte", OP16(0xb98eLL), MASK_RRF_R0RR, INSTR_RRF_R0RR, 2, 3},
  { "epsw", OP16(0xb98dLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 2},
  { "cspg", OP16(0xb98aLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 3},
  { "slbgr", OP16(0xb989LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "alcgr", OP16(0xb988LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "dlgr", OP16(0xb987LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "mlgr", OP16(0xb986LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "llghr", OP16(0xb985LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "llgcr", OP16(0xb984LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "flogr", OP16(0xb983LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "xgr", OP16(0xb982LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "ogr", OP16(0xb981LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "ngr", OP16(0xb980LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "bctgr", OP16(0xb946LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "klmd", OP16(0xb93fLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 3},
  { "kimd", OP16(0xb93eLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 3},
  { "clgfr", OP16(0xb931LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cgfr", OP16(0xb930LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "kmc", OP16(0xb92fLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 3},
  { "km", OP16(0xb92eLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 3},
  { "lhr", OP16(0xb927LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "lbr", OP16(0xb926LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "sturg", OP16(0xb925LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "clgr", OP16(0xb921LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cgr", OP16(0xb920LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lrvr", OP16(0xb91fLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 2},
  { "kmac", OP16(0xb91eLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 3},
  { "dsgfr", OP16(0xb91dLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "msgfr", OP16(0xb91cLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "slgfr", OP16(0xb91bLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "algfr", OP16(0xb91aLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "sgfr", OP16(0xb919LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "agfr", OP16(0xb918LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "llgtr", OP16(0xb917LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "llgfr", OP16(0xb916LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lgfr", OP16(0xb914LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lcgfr", OP16(0xb913LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "ltgfr", OP16(0xb912LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lngfr", OP16(0xb911LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lpgfr", OP16(0xb910LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lrvgr", OP16(0xb90fLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "eregg", OP16(0xb90eLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "dsgr", OP16(0xb90dLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "msgr", OP16(0xb90cLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "slgr", OP16(0xb90bLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "algr", OP16(0xb90aLL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "sgr", OP16(0xb909LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "agr", OP16(0xb908LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lghr", OP16(0xb907LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "lgbr", OP16(0xb906LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 4},
  { "lurag", OP16(0xb905LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lgr", OP16(0xb904LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lcgr", OP16(0xb903LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "ltgr", OP16(0xb902LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lngr", OP16(0xb901LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lpgr", OP16(0xb900LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "lctl", OP8(0xb7LL), MASK_RS_CCRD, INSTR_RS_CCRD, 3, 0},
  { "stctl", OP8(0xb6LL), MASK_RS_CCRD, INSTR_RS_CCRD, 3, 0},
  { "rrxtr", OP16(0xb3ffLL), MASK_RRF_FFFU, INSTR_RRF_FFFU, 2, 5},
  { "iextr", OP16(0xb3feLL), MASK_RRF_F0FR, INSTR_RRF_F0FR, 2, 5},
  { "qaxtr", OP16(0xb3fdLL), MASK_RRF_FFFU, INSTR_RRF_FFFU, 2, 5},
  { "cextr", OP16(0xb3fcLL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "cxstr", OP16(0xb3fbLL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "cxutr", OP16(0xb3faLL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "cxgtr", OP16(0xb3f9LL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "rrdtr", OP16(0xb3f7LL), MASK_RRF_FFFU, INSTR_RRF_FFFU, 2, 5},
  { "iedtr", OP16(0xb3f6LL), MASK_RRF_F0FR, INSTR_RRF_F0FR, 2, 5},
  { "qadtr", OP16(0xb3f5LL), MASK_RRF_FFFU, INSTR_RRF_FFFU, 2, 5},
  { "cedtr", OP16(0xb3f4LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "cdstr", OP16(0xb3f3LL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "cdutr", OP16(0xb3f2LL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "cdgtr", OP16(0xb3f1LL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "esxtr", OP16(0xb3efLL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "eextr", OP16(0xb3edLL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cxtr", OP16(0xb3ecLL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "csxtr", OP16(0xb3ebLL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cuxtr", OP16(0xb3eaLL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cgxtr", OP16(0xb3e9LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 5},
  { "kxtr", OP16(0xb3e8LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "esdtr", OP16(0xb3e7LL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "eedtr", OP16(0xb3e5LL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cdtr", OP16(0xb3e4LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "csdtr", OP16(0xb3e3LL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cudtr", OP16(0xb3e2LL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cgdtr", OP16(0xb3e1LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 5},
  { "kdtr", OP16(0xb3e0LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "fixtr", OP16(0xb3dfLL), MASK_RRF_UUFF, INSTR_RRF_UUFF, 2, 5},
  { "ltxtr", OP16(0xb3deLL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "ldxtr", OP16(0xb3ddLL), MASK_RRF_UUFF, INSTR_RRF_UUFF, 2, 5},
  { "lxdtr", OP16(0xb3dcLL), MASK_RRF_0UFF, INSTR_RRF_0UFF, 2, 5},
  { "sxtr", OP16(0xb3dbLL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "axtr", OP16(0xb3daLL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "dxtr", OP16(0xb3d9LL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "mxtr", OP16(0xb3d8LL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "fidtr", OP16(0xb3d7LL), MASK_RRF_UUFF, INSTR_RRF_UUFF, 2, 5},
  { "ltdtr", OP16(0xb3d6LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "ledtr", OP16(0xb3d5LL), MASK_RRF_UUFF, INSTR_RRF_UUFF, 2, 5},
  { "ldetr", OP16(0xb3d4LL), MASK_RRF_0UFF, INSTR_RRF_0UFF, 2, 5},
  { "sdtr", OP16(0xb3d3LL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "adtr", OP16(0xb3d2LL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "ddtr", OP16(0xb3d1LL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "mdtr", OP16(0xb3d0LL), MASK_RRR_F0FF, INSTR_RRR_F0FF, 2, 5},
  { "lgdr", OP16(0xb3cdLL), MASK_RRE_RF, INSTR_RRE_RF, 2, 5},
  { "cgxr", OP16(0xb3caLL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cgdr", OP16(0xb3c9LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cger", OP16(0xb3c8LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cxgr", OP16(0xb3c6LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cdgr", OP16(0xb3c5LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cegr", OP16(0xb3c4LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "ldgr", OP16(0xb3c1LL), MASK_RRE_FR, INSTR_RRE_FR, 2, 5},
  { "cfxr", OP16(0xb3baLL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cfdr", OP16(0xb3b9LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cfer", OP16(0xb3b8LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cxfr", OP16(0xb3b6LL), MASK_RRE_RF, INSTR_RRE_RF, 3, 0},
  { "cdfr", OP16(0xb3b5LL), MASK_RRE_RF, INSTR_RRE_RF, 3, 0},
  { "cefr", OP16(0xb3b4LL), MASK_RRE_RF, INSTR_RRE_RF, 3, 0},
  { "cgxbr", OP16(0xb3aaLL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cgdbr", OP16(0xb3a9LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cgebr", OP16(0xb3a8LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 2, 2},
  { "cxgbr", OP16(0xb3a6LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cdgbr", OP16(0xb3a5LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cegbr", OP16(0xb3a4LL), MASK_RRE_RR, INSTR_RRE_RR, 2, 2},
  { "cfxbr", OP16(0xb39aLL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 3, 0},
  { "cfdbr", OP16(0xb399LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 3, 0},
  { "cfebr", OP16(0xb398LL), MASK_RRF_U0RF, INSTR_RRF_U0RF, 3, 0},
  { "cxfbr", OP16(0xb396LL), MASK_RRE_RF, INSTR_RRE_RF, 3, 0},
  { "cdfbr", OP16(0xb395LL), MASK_RRE_RF, INSTR_RRE_RF, 3, 0},
  { "cefbr", OP16(0xb394LL), MASK_RRE_RF, INSTR_RRE_RF, 3, 0},
  { "efpc", OP16(0xb38cLL), MASK_RRE_RR_OPT, INSTR_RRE_RR_OPT, 3, 0},
  { "sfasr", OP16(0xb385LL), MASK_RRE_R0, INSTR_RRE_R0, 2, 5},
  { "sfpc", OP16(0xb384LL), MASK_RRE_RR_OPT, INSTR_RRE_RR_OPT, 3, 0},
  { "fidr", OP16(0xb37fLL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "fier", OP16(0xb377LL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "lzxr", OP16(0xb376LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "lzdr", OP16(0xb375LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "lzer", OP16(0xb374LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "lcdfr", OP16(0xb373LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "cpsdr", OP16(0xb372LL), MASK_RRF_F0FF2, INSTR_RRF_F0FF2, 2, 5},
  { "lndfr", OP16(0xb371LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "lpdfr", OP16(0xb370LL), MASK_RRE_FF, INSTR_RRE_FF, 2, 5},
  { "cxr", OP16(0xb369LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "fixr", OP16(0xb367LL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "lexr", OP16(0xb366LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lxr", OP16(0xb365LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "lcxr", OP16(0xb363LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ltxr", OP16(0xb362LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lnxr", OP16(0xb361LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lpxr", OP16(0xb360LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "fidbr", OP16(0xb35fLL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "didbr", OP16(0xb35bLL), MASK_RRF_FUFF, INSTR_RRF_FUFF, 3, 0},
  { "thdr", OP16(0xb359LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "thder", OP16(0xb358LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "fiebr", OP16(0xb357LL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "diebr", OP16(0xb353LL), MASK_RRF_FUFF, INSTR_RRF_FUFF, 3, 0},
  { "tbdr", OP16(0xb351LL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "tbedr", OP16(0xb350LL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "dxbr", OP16(0xb34dLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "mxbr", OP16(0xb34cLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sxbr", OP16(0xb34bLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "axbr", OP16(0xb34aLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "cxbr", OP16(0xb349LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "kxbr", OP16(0xb348LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "fixbr", OP16(0xb347LL), MASK_RRF_U0FF, INSTR_RRF_U0FF, 3, 0},
  { "lexbr", OP16(0xb346LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ldxbr", OP16(0xb345LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ledbr", OP16(0xb344LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lcxbr", OP16(0xb343LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ltxbr", OP16(0xb342LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lnxbr", OP16(0xb341LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lpxbr", OP16(0xb340LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "msdr", OP16(0xb33fLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 3},
  { "madr", OP16(0xb33eLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 3},
  { "myhr", OP16(0xb33dLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 2, 4},
  { "mayhr", OP16(0xb33cLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 2, 4},
  { "myr", OP16(0xb33bLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 2, 4},
  { "mayr", OP16(0xb33aLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 2, 4},
  { "mylr", OP16(0xb339LL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 2, 4},
  { "maylr", OP16(0xb338LL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 2, 4},
  { "meer", OP16(0xb337LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sqxr", OP16(0xb336LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "mser", OP16(0xb32fLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 3},
  { "maer", OP16(0xb32eLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 3},
  { "lxer", OP16(0xb326LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lxdr", OP16(0xb325LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lder", OP16(0xb324LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "msdbr", OP16(0xb31fLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 0},
  { "madbr", OP16(0xb31eLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 0},
  { "ddbr", OP16(0xb31dLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "mdbr", OP16(0xb31cLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sdbr", OP16(0xb31bLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "adbr", OP16(0xb31aLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "cdbr", OP16(0xb319LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "kdbr", OP16(0xb318LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "meebr", OP16(0xb317LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sqxbr", OP16(0xb316LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sqdbr", OP16(0xb315LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sqebr", OP16(0xb314LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lcdbr", OP16(0xb313LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ltdbr", OP16(0xb312LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lndbr", OP16(0xb311LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lpdbr", OP16(0xb310LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "msebr", OP16(0xb30fLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 0},
  { "maebr", OP16(0xb30eLL), MASK_RRF_F0FF, INSTR_RRF_F0FF, 3, 0},
  { "debr", OP16(0xb30dLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "mdebr", OP16(0xb30cLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "sebr", OP16(0xb30bLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "aebr", OP16(0xb30aLL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "cebr", OP16(0xb309LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "kebr", OP16(0xb308LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "mxdbr", OP16(0xb307LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lxebr", OP16(0xb306LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lxdbr", OP16(0xb305LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ldebr", OP16(0xb304LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lcebr", OP16(0xb303LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "ltebr", OP16(0xb302LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lnebr", OP16(0xb301LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "lpebr", OP16(0xb300LL), MASK_RRE_FF, INSTR_RRE_FF, 3, 0},
  { "trap4", OP16(0xb2ffLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "lfas", OP16(0xb2bdLL), MASK_S_RD, INSTR_S_RD, 2, 5},
  { "srnmt", OP16(0xb2b9LL), MASK_S_RD, INSTR_S_RD, 2, 5},
  { "lpswe", OP16(0xb2b2LL), MASK_S_RD, INSTR_S_RD, 2, 2},
  { "stfl", OP16(0xb2b1LL), MASK_S_RD, INSTR_S_RD, 3, 2},
  { "stfle", OP16(0xb2b0LL), MASK_S_RD, INSTR_S_RD, 2, 4},
  { "cu12", OP16(0xb2a7LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cutfu", OP16(0xb2a7LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cutfu", OP16(0xb2a7LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "cu21", OP16(0xb2a6LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cuutf", OP16(0xb2a6LL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "cuutf", OP16(0xb2a6LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "tre", OP16(0xb2a5LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "lfpc", OP16(0xb29dLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stfpc", OP16(0xb29cLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "srnm", OP16(0xb299LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stsi", OP16(0xb27dLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stckf", OP16(0xb27cLL), MASK_S_RD, INSTR_S_RD, 2, 4},
  { "sacf", OP16(0xb279LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stcke", OP16(0xb278LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "rp", OP16(0xb277LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "xsch", OP16(0xb276LL), MASK_S_00, INSTR_S_00, 3, 0},
  { "siga", OP16(0xb274LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "cmpsc", OP16(0xb263LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "cmpsc", OP16(0xb263LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "srst", OP16(0xb25eLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "clst", OP16(0xb25dLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "bsa", OP16(0xb25aLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "bsg", OP16(0xb258LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "cuse", OP16(0xb257LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "mvst", OP16(0xb255LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "mvpg", OP16(0xb254LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "msr", OP16(0xb252LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "csp", OP16(0xb250LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "ear", OP16(0xb24fLL), MASK_RRE_RA, INSTR_RRE_RA, 3, 0},
  { "sar", OP16(0xb24eLL), MASK_RRE_AR, INSTR_RRE_AR, 3, 0},
  { "cpya", OP16(0xb24dLL), MASK_RRE_AA, INSTR_RRE_AA, 3, 0},
  { "tar", OP16(0xb24cLL), MASK_RRE_AR, INSTR_RRE_AR, 3, 0},
  { "lura", OP16(0xb24bLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "esta", OP16(0xb24aLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "ereg", OP16(0xb249LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "palb", OP16(0xb248LL), MASK_RRE_00, INSTR_RRE_00, 3, 0},
  { "msta", OP16(0xb247LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "stura", OP16(0xb246LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "sqer", OP16(0xb245LL), MASK_RRE_F0, INSTR_RRE_F0, 3, 0},
  { "sqdr", OP16(0xb244LL), MASK_RRE_F0, INSTR_RRE_F0, 3, 0},
  { "cksm", OP16(0xb241LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "bakr", OP16(0xb240LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "schm", OP16(0xb23cLL), MASK_S_00, INSTR_S_00, 3, 0},
  { "rchp", OP16(0xb23bLL), MASK_S_00, INSTR_S_00, 3, 0},
  { "stcps", OP16(0xb23aLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stcrw", OP16(0xb239LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "rsch", OP16(0xb238LL), MASK_S_00, INSTR_S_00, 3, 0},
  { "sal", OP16(0xb237LL), MASK_S_00, INSTR_S_00, 3, 0},
  { "tpi", OP16(0xb236LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "tsch", OP16(0xb235LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stsch", OP16(0xb234LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "ssch", OP16(0xb233LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "msch", OP16(0xb232LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "hsch", OP16(0xb231LL), MASK_S_00, INSTR_S_00, 3, 0},
  { "csch", OP16(0xb230LL), MASK_S_00, INSTR_S_00, 3, 0},
  { "pgout", OP16(0xb22fLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "pgin", OP16(0xb22eLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "dxr", OP16(0xb22dLL), MASK_RRE_F0, INSTR_RRE_F0, 3, 0},
  { "tb", OP16(0xb22cLL), MASK_RRE_0R, INSTR_RRE_0R, 3, 0},
  { "sske", OP16(0xb22bLL), MASK_RRF_M0RR, INSTR_RRF_M0RR, 2, 4},
  { "sske", OP16(0xb22bLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "rrbe", OP16(0xb22aLL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "iske", OP16(0xb229LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "pt", OP16(0xb228LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "esar", OP16(0xb227LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "epar", OP16(0xb226LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "ssar", OP16(0xb225LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "iac", OP16(0xb224LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "ivsk", OP16(0xb223LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "ipm", OP16(0xb222LL), MASK_RRE_R0, INSTR_RRE_R0, 3, 0},
  { "ipte", OP16(0xb221LL), MASK_RRE_RR, INSTR_RRE_RR, 3, 0},
  { "cfc", OP16(0xb21aLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "sac", OP16(0xb219LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "pc", OP16(0xb218LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "sie", OP16(0xb214LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stap", OP16(0xb212LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stpx", OP16(0xb211LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "spx", OP16(0xb210LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "ptlb", OP16(0xb20dLL), MASK_S_00, INSTR_S_00, 3, 0},
  { "ipk", OP16(0xb20bLL), MASK_S_00, INSTR_S_00, 3, 0},
  { "spka", OP16(0xb20aLL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stpt", OP16(0xb209LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "spt", OP16(0xb208LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stckc", OP16(0xb207LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "sckc", OP16(0xb206LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stck", OP16(0xb205LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "sck", OP16(0xb204LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "stidp", OP16(0xb202LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "lra", OP8(0xb1LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "mc", OP8(0xafLL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "sigp", OP8(0xaeLL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "stosm", OP8(0xadLL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "stnsm", OP8(0xacLL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "clcle", OP8(0xa9LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "mvcle", OP8(0xa8LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "j", OP16(0xa7f4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jno", OP16(0xa7e4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnh", OP16(0xa7d4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnp", OP16(0xa7d4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jle", OP16(0xa7c4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnl", OP16(0xa7b4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnm", OP16(0xa7b4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jhe", OP16(0xa7a4LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnlh", OP16(0xa794LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "je", OP16(0xa784LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jz", OP16(0xa784LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jne", OP16(0xa774LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnz", OP16(0xa774LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jlh", OP16(0xa764LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnhe", OP16(0xa754LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jl", OP16(0xa744LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jm", OP16(0xa744LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jnle", OP16(0xa734LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jh", OP16(0xa724LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jp", OP16(0xa724LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "jo", OP16(0xa714LL), MASK_RI_0P, INSTR_RI_0P, 3, 0},
  { "cghi", OP16(0xa70fLL), MASK_RI_RI, INSTR_RI_RI, 2, 2},
  { "chi", OP16(0xa70eLL), MASK_RI_RI, INSTR_RI_RI, 3, 0},
  { "mghi", OP16(0xa70dLL), MASK_RI_RI, INSTR_RI_RI, 2, 2},
  { "mhi", OP16(0xa70cLL), MASK_RI_RI, INSTR_RI_RI, 3, 0},
  { "aghi", OP16(0xa70bLL), MASK_RI_RI, INSTR_RI_RI, 2, 2},
  { "ahi", OP16(0xa70aLL), MASK_RI_RI, INSTR_RI_RI, 3, 0},
  { "lghi", OP16(0xa709LL), MASK_RI_RI, INSTR_RI_RI, 2, 2},
  { "lhi", OP16(0xa708LL), MASK_RI_RI, INSTR_RI_RI, 3, 0},
  { "brctg", OP16(0xa707LL), MASK_RI_RP, INSTR_RI_RP, 2, 2},
  { "brct", OP16(0xa706LL), MASK_RI_RP, INSTR_RI_RP, 3, 0},
  { "bras", OP16(0xa705LL), MASK_RI_RP, INSTR_RI_RP, 3, 0},
  { "brc", OP16(0xa704LL), MASK_RI_UP, INSTR_RI_UP, 3, 0},
  { "tmhl", OP16(0xa703LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "tmhh", OP16(0xa702LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "tml", OP16(0xa701LL), MASK_RI_RU, INSTR_RI_RU, 3, 0},
  { "tmll", OP16(0xa701LL), MASK_RI_RU, INSTR_RI_RU, 3, 0},
  { "tmh", OP16(0xa700LL), MASK_RI_RU, INSTR_RI_RU, 3, 0},
  { "tmlh", OP16(0xa700LL), MASK_RI_RU, INSTR_RI_RU, 3, 0},
  { "llill", OP16(0xa50fLL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "llilh", OP16(0xa50eLL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "llihl", OP16(0xa50dLL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "llihh", OP16(0xa50cLL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "oill", OP16(0xa50bLL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "oilh", OP16(0xa50aLL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "oihl", OP16(0xa509LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "oihh", OP16(0xa508LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "nill", OP16(0xa507LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "nilh", OP16(0xa506LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "nihl", OP16(0xa505LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "nihh", OP16(0xa504LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "iill", OP16(0xa503LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "iilh", OP16(0xa502LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "iihl", OP16(0xa501LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "iihh", OP16(0xa500LL), MASK_RI_RU, INSTR_RI_RU, 2, 2},
  { "stam", OP8(0x9bLL), MASK_RS_AARD, INSTR_RS_AARD, 3, 0},
  { "lam", OP8(0x9aLL), MASK_RS_AARD, INSTR_RS_AARD, 3, 0},
  { "trace", OP8(0x99LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "lm", OP8(0x98LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "xi", OP8(0x97LL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "oi", OP8(0x96LL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "cli", OP8(0x95LL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "ni", OP8(0x94LL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "ts", OP8(0x93LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "mvi", OP8(0x92LL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "tm", OP8(0x91LL), MASK_SI_URD, INSTR_SI_URD, 3, 0},
  { "stm", OP8(0x90LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "slda", OP8(0x8fLL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "srda", OP8(0x8eLL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "sldl", OP8(0x8dLL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "srdl", OP8(0x8cLL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "sla", OP8(0x8bLL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "sra", OP8(0x8aLL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "sll", OP8(0x89LL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "srl", OP8(0x88LL), MASK_RS_R0RD, INSTR_RS_R0RD, 3, 0},
  { "bxle", OP8(0x87LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "bxh", OP8(0x86LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "brxle", OP8(0x85LL), MASK_RSI_RRP, INSTR_RSI_RRP, 3, 0},
  { "brxh", OP8(0x84LL), MASK_RSI_RRP, INSTR_RSI_RRP, 3, 0},
  { "diag", OP8(0x83LL), MASK_RS_RRRD, INSTR_RS_RRRD, 3, 0},
  { "lpsw", OP8(0x82LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "ssm", OP8(0x80LL), MASK_S_RD, INSTR_S_RD, 3, 0},
  { "su", OP8(0x7fLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "au", OP8(0x7eLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "de", OP8(0x7dLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "me", OP8(0x7cLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "mde", OP8(0x7cLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "se", OP8(0x7bLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "ae", OP8(0x7aLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "ce", OP8(0x79LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "le", OP8(0x78LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "ms", OP8(0x71LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "ste", OP8(0x70LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "sw", OP8(0x6fLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "aw", OP8(0x6eLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "dd", OP8(0x6dLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "md", OP8(0x6cLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "sd", OP8(0x6bLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "ad", OP8(0x6aLL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "cd", OP8(0x69LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "ld", OP8(0x68LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "mxd", OP8(0x67LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "std", OP8(0x60LL), MASK_RX_FRRD, INSTR_RX_FRRD, 3, 0},
  { "sl", OP8(0x5fLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "al", OP8(0x5eLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "d", OP8(0x5dLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "m", OP8(0x5cLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "s", OP8(0x5bLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "a", OP8(0x5aLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "c", OP8(0x59LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "l", OP8(0x58LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "x", OP8(0x57LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "o", OP8(0x56LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "cl", OP8(0x55LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "n", OP8(0x54LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "lae", OP8(0x51LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "st", OP8(0x50LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "cvb", OP8(0x4fLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "cvd", OP8(0x4eLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "bas", OP8(0x4dLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "mh", OP8(0x4cLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "sh", OP8(0x4bLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "ah", OP8(0x4aLL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "ch", OP8(0x49LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "lh", OP8(0x48LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "b", OP16(0x47f0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bno", OP16(0x47e0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnh", OP16(0x47d0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnp", OP16(0x47d0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "ble", OP16(0x47c0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnl", OP16(0x47b0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnm", OP16(0x47b0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bhe", OP16(0x47a0LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnlh", OP16(0x4790LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "be", OP16(0x4780LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bz", OP16(0x4780LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bne", OP16(0x4770LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnz", OP16(0x4770LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "blh", OP16(0x4760LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnhe", OP16(0x4750LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bl", OP16(0x4740LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bm", OP16(0x4740LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bnle", OP16(0x4730LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bh", OP16(0x4720LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bp", OP16(0x4720LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bo", OP16(0x4710LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bc", OP8(0x47LL), MASK_RX_URRD, INSTR_RX_URRD, 3, 0},
  { "nop", OP16(0x4700LL), MASK_RX_0RRD, INSTR_RX_0RRD, 3, 0},
  { "bct", OP8(0x46LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "bal", OP8(0x45LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "ex", OP8(0x44LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "ic", OP8(0x43LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "stc", OP8(0x42LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "la", OP8(0x41LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "sth", OP8(0x40LL), MASK_RX_RRRD, INSTR_RX_RRRD, 3, 0},
  { "sur", OP8(0x3fLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "aur", OP8(0x3eLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "der", OP8(0x3dLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "mer", OP8(0x3cLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "mder", OP8(0x3cLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ser", OP8(0x3bLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "aer", OP8(0x3aLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "cer", OP8(0x39LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ler", OP8(0x38LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "sxr", OP8(0x37LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "axr", OP8(0x36LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lrer", OP8(0x35LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ledr", OP8(0x35LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "her", OP8(0x34LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lcer", OP8(0x33LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lter", OP8(0x32LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lner", OP8(0x31LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lper", OP8(0x30LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "swr", OP8(0x2fLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "awr", OP8(0x2eLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ddr", OP8(0x2dLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "mdr", OP8(0x2cLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "sdr", OP8(0x2bLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "adr", OP8(0x2aLL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "cdr", OP8(0x29LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ldr", OP8(0x28LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "mxdr", OP8(0x27LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "mxr", OP8(0x26LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lrdr", OP8(0x25LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ldxr", OP8(0x25LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "hdr", OP8(0x24LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lcdr", OP8(0x23LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "ltdr", OP8(0x22LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lndr", OP8(0x21LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "lpdr", OP8(0x20LL), MASK_RR_FF, INSTR_RR_FF, 3, 0},
  { "slr", OP8(0x1fLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "alr", OP8(0x1eLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "dr", OP8(0x1dLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "mr", OP8(0x1cLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "sr", OP8(0x1bLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "ar", OP8(0x1aLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "cr", OP8(0x19LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "lr", OP8(0x18LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "xr", OP8(0x17LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "or", OP8(0x16LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "clr", OP8(0x15LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "nr", OP8(0x14LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "lcr", OP8(0x13LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "ltr", OP8(0x12LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "lnr", OP8(0x11LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "lpr", OP8(0x10LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "clcl", OP8(0x0fLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "mvcl", OP8(0x0eLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "basr", OP8(0x0dLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "bassm", OP8(0x0cLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "bsm", OP8(0x0bLL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "svc", OP8(0x0aLL), MASK_RR_U0, INSTR_RR_U0, 3, 0},
  { "br", OP16(0x07f0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnor", OP16(0x07e0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnhr", OP16(0x07d0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnpr", OP16(0x07d0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bler", OP16(0x07c0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnlr", OP16(0x07b0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnmr", OP16(0x07b0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bher", OP16(0x07a0LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnlhr", OP16(0x0790LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "ber", OP16(0x0780LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bzr", OP16(0x0780LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bner", OP16(0x0770LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnzr", OP16(0x0770LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "blhr", OP16(0x0760LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnher", OP16(0x0750LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "blr", OP16(0x0740LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bmr", OP16(0x0740LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bnler", OP16(0x0730LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bhr", OP16(0x0720LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bpr", OP16(0x0720LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bor", OP16(0x0710LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bcr", OP8(0x07LL), MASK_RR_UR, INSTR_RR_UR, 3, 0},
  { "nopr", OP16(0x0700LL), MASK_RR_0R, INSTR_RR_0R, 3, 0},
  { "bctr", OP8(0x06LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "balr", OP8(0x05LL), MASK_RR_RR, INSTR_RR_RR, 3, 0},
  { "spm", OP8(0x04LL), MASK_RR_R0, INSTR_RR_R0, 3, 0},
  { "trap2", OP16(0x01ffLL), MASK_E, INSTR_E, 3, 0},
  { "sam64", OP16(0x010eLL), MASK_E, INSTR_E, 2, 2},
  { "sam31", OP16(0x010dLL), MASK_E, INSTR_E, 3, 2},
  { "sam24", OP16(0x010cLL), MASK_E, INSTR_E, 3, 2},
  { "tam", OP16(0x010bLL), MASK_E, INSTR_E, 3, 2},
  { "pfpo", OP16(0x010aLL), MASK_E, INSTR_E, 2, 5},
  { "sckpf", OP16(0x0107LL), MASK_E, INSTR_E, 3, 0},
  { "upt", OP16(0x0102LL), MASK_E, INSTR_E, 3, 0},
  { "pr", OP16(0x0101LL), MASK_E, INSTR_E, 3, 0}
};

const int s390_num_opcodes =
  sizeof (s390_opcodes) / sizeof (s390_opcodes[0]);
