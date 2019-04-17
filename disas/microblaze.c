/* Disassemble Xilinx microblaze instructions.
   Copyright (C) 1993, 1999, 2000 Free Software Foundation, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, see <http://www.gnu.org/licenses/>. */

/*
 * Copyright (c) 2001 Xilinx, Inc.  All rights reserved. 
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Xilinx, Inc.  The name of the Company may not be used to endorse 
 * or promote products derived from this software without specific prior 
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Xilinx, Inc.
 */


#include "qemu/osdep.h"
#define STATIC_TABLE
#define DEFINE_TABLE

#ifndef MICROBLAZE_OPC
#define MICROBLAZE_OPC
/* Assembler instructions for Xilinx's microblaze processor
   Copyright (C) 1999, 2000 Free Software Foundation, Inc.

   
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

/*
 * Copyright (c) 2001 Xilinx, Inc.  All rights reserved. 
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Xilinx, Inc.  The name of the Company may not be used to endorse 
 * or promote products derived from this software without specific prior 
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Xilinx, Inc.
 */


#ifndef MICROBLAZE_OPCM
#define MICROBLAZE_OPCM

/*
 * Copyright (c) 2001 Xilinx, Inc.  All rights reserved. 
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by Xilinx, Inc.  The name of the Company may not be used to endorse 
 * or promote products derived from this software without specific prior 
 * written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 *	Xilinx, Inc.
 * $Header:
 */

enum microblaze_instr {
   add, rsub, addc, rsubc, addk, rsubk, addkc, rsubkc, cmp, cmpu,
   addi, rsubi, addic, rsubic, addik, rsubik, addikc, rsubikc, mul, mulh, mulhu, mulhsu,
   idiv, idivu, bsll, bsra, bsrl, get, put, nget, nput, cget, cput,
   ncget, ncput, muli, bslli, bsrai, bsrli, mului, or, and, xor,
   andn, pcmpbf, pcmpbc, pcmpeq, pcmpne, sra, src, srl, sext8, sext16, wic, wdc, wdcclear, wdcflush, mts, mfs, br, brd,
   brld, bra, brad, brald, microblaze_brk, beq, beqd, bne, bned, blt,
   bltd, ble, bled, bgt, bgtd, bge, bged, ori, andi, xori, andni,
   imm, rtsd, rtid, rtbd, rted, bri, brid, brlid, brai, braid, bralid,
   brki, beqi, beqid, bnei, bneid, blti, bltid, blei, bleid, bgti,
   bgtid, bgei, bgeid, lbu, lhu, lw, lwx, sb, sh, sw, swx, lbui, lhui, lwi,
   sbi, shi, swi, msrset, msrclr, tuqula, fadd, frsub, fmul, fdiv, 
   fcmp_lt, fcmp_eq, fcmp_le, fcmp_gt, fcmp_ne, fcmp_ge, fcmp_un, flt, fint, fsqrt, 
   tget, tcget, tnget, tncget, tput, tcput, tnput, tncput,
   eget, ecget, neget, necget, eput, ecput, neput, necput,
   teget, tecget, tneget, tnecget, teput, tecput, tneput, tnecput,
   aget, caget, naget, ncaget, aput, caput, naput, ncaput,
   taget, tcaget, tnaget, tncaget, taput, tcaput, tnaput, tncaput,
   eaget, ecaget, neaget, necaget, eaput, ecaput, neaput, necaput,
   teaget, tecaget, tneaget, tnecaget, teaput, tecaput, tneaput, tnecaput,
   getd, tgetd, cgetd, tcgetd, ngetd, tngetd, ncgetd, tncgetd,
   putd, tputd, cputd, tcputd, nputd, tnputd, ncputd, tncputd,
   egetd, tegetd, ecgetd, tecgetd, negetd, tnegetd, necgetd, tnecgetd,
   eputd, teputd, ecputd, tecputd, neputd, tneputd, necputd, tnecputd,
   agetd, tagetd, cagetd, tcagetd, nagetd, tnagetd, ncagetd, tncagetd,
   aputd, taputd, caputd, tcaputd, naputd, tnaputd, ncaputd, tncaputd,
   eagetd, teagetd, ecagetd, tecagetd, neagetd, tneagetd, necagetd, tnecagetd,
   eaputd, teaputd, ecaputd, tecaputd, neaputd, tneaputd, necaputd, tnecaputd,
   invalid_inst } ;

enum microblaze_instr_type {
   arithmetic_inst, logical_inst, mult_inst, div_inst, branch_inst,
   return_inst, immediate_inst, special_inst, memory_load_inst,
   memory_store_inst, barrel_shift_inst, anyware_inst };

#define INST_WORD_SIZE 4

/* gen purpose regs go from 0 to 31 */
/* mask is reg num - max_reg_num, ie reg_num - 32 in this case */

#define REG_PC_MASK 0x8000
#define REG_MSR_MASK 0x8001
#define REG_EAR_MASK 0x8003
#define REG_ESR_MASK 0x8005
#define REG_FSR_MASK 0x8007
#define REG_BTR_MASK 0x800b
#define REG_EDR_MASK 0x800d
#define REG_PVR_MASK 0xa000

#define REG_PID_MASK   0x9000
#define REG_ZPR_MASK   0x9001
#define REG_TLBX_MASK  0x9002
#define REG_TLBLO_MASK 0x9003
#define REG_TLBHI_MASK 0x9004
#define REG_TLBSX_MASK 0x9005

#define MIN_REGNUM 0
#define MAX_REGNUM 31

#define MIN_PVR_REGNUM 0
#define MAX_PVR_REGNUM 15

/* 32 is REG_PC */
#define REG_MSR 33 /* machine status reg */
#define REG_EAR 35 /* Exception reg */
#define REG_ESR 37 /* Exception reg */
#define REG_FSR 39 /* FPU Status reg */
#define REG_BTR 43 /* Branch Target reg */
#define REG_EDR 45 /* Exception reg */
#define REG_PVR 40960 /* Program Verification reg */

#define REG_PID   36864 /* MMU: Process ID reg       */
#define REG_ZPR   36865 /* MMU: Zone Protect reg     */
#define REG_TLBX  36866 /* MMU: TLB Index reg        */
#define REG_TLBLO 36867 /* MMU: TLB Low reg          */
#define REG_TLBHI 36868 /* MMU: TLB High reg         */
#define REG_TLBSX 36869 /* MMU: TLB Search Index reg */

/* alternate names for gen purpose regs */
#define REG_ROSDP 2 /* read-only small data pointer */
#define REG_RWSDP 13 /* read-write small data pointer */

/* Assembler Register - Used in Delay Slot Optimization */
#define REG_AS    18
#define REG_ZERO  0
 
#define RD_LOW  21 /* low bit for RD */
#define RA_LOW  16 /* low bit for RA */
#define RB_LOW  11 /* low bit for RB */
#define IMM_LOW  0 /* low bit for immediate */

#define RD_MASK 0x03E00000
#define RA_MASK 0x001F0000
#define RB_MASK 0x0000F800
#define IMM_MASK 0x0000FFFF

// imm mask for barrel shifts
#define IMM5_MASK 0x0000001F


// FSL imm mask for get, put instructions
#define  RFSL_MASK 0x000000F

// imm mask for msrset, msrclr instructions
#define  IMM15_MASK 0x00007FFF

#endif /* MICROBLAZE-OPCM */

#define INST_TYPE_RD_R1_R2 0
#define INST_TYPE_RD_R1_IMM 1
#define INST_TYPE_RD_R1_UNSIGNED_IMM 2
#define INST_TYPE_RD_R1 3
#define INST_TYPE_RD_R2 4
#define INST_TYPE_RD_IMM 5
#define INST_TYPE_R2 6
#define INST_TYPE_R1_R2 7
#define INST_TYPE_R1_IMM 8
#define INST_TYPE_IMM 9
#define INST_TYPE_SPECIAL_R1 10
#define INST_TYPE_RD_SPECIAL 11
#define INST_TYPE_R1 12
  // new instn type for barrel shift imms
#define INST_TYPE_RD_R1_IMM5  13
#define INST_TYPE_RD_RFSL    14
#define INST_TYPE_R1_RFSL    15

  // new insn type for insn cache
#define INST_TYPE_RD_R1_SPECIAL 16

// new insn type for msrclr, msrset insns.
#define INST_TYPE_RD_IMM15    17

// new insn type for tuqula rd - addik rd, r0, 42
#define INST_TYPE_RD    18

// new insn type for t*put
#define INST_TYPE_RFSL  19

#define INST_TYPE_NONE 25



#define INST_PC_OFFSET 1 /* instructions where the label address is resolved as a PC offset (for branch label)*/
#define INST_NO_OFFSET 0 /* instructions where the label address is resolved as an absolute value (for data mem or abs address)*/

#define IMMVAL_MASK_NON_SPECIAL 0x0000
#define IMMVAL_MASK_MTS 0x4000
#define IMMVAL_MASK_MFS 0x0000

#define OPCODE_MASK_H   0xFC000000 /* High 6 bits only */
#define OPCODE_MASK_H1  0xFFE00000 /* High 11 bits */
#define OPCODE_MASK_H2  0xFC1F0000 /* High 6 and bits 20-16 */
#define OPCODE_MASK_H12 0xFFFF0000 /* High 16 */
#define OPCODE_MASK_H4  0xFC0007FF /* High 6 and low 11 bits */
#define OPCODE_MASK_H13S 0xFFE0EFF0 /* High 11 and 15:1 bits and last nibble of last byte for spr */
#define OPCODE_MASK_H23S 0xFC1FC000 /* High 6, 20-16 and 15:1 bits and last nibble of last byte for spr */
#define OPCODE_MASK_H34 0xFC00FFFF /* High 6 and low 16 bits */
#define OPCODE_MASK_H14 0xFFE007FF /* High 11 and low 11 bits */
#define OPCODE_MASK_H24 0xFC1F07FF /* High 6, bits 20-16 and low 11 bits */
#define OPCODE_MASK_H124  0xFFFF07FF /* High 16, and low 11 bits */
#define OPCODE_MASK_H1234 0xFFFFFFFF /* All 32 bits */
#define OPCODE_MASK_H3  0xFC000600 /* High 6 bits and bits 21, 22 */  
#define OPCODE_MASK_H32 0xFC00FC00 /* High 6 bits and bit 16-21 */
#define OPCODE_MASK_H34B   0xFC0000FF /* High 6 bits and low 8 bits */
#define OPCODE_MASK_H34C   0xFC0007E0 /* High 6 bits and bits 21-26 */

// New Mask for msrset, msrclr insns.
#define OPCODE_MASK_H23N  0xFC1F8000 /* High 6 and bits 11 - 16 */

#define DELAY_SLOT 1
#define NO_DELAY_SLOT 0

#define MAX_OPCODES 280

static const struct op_code_struct {
  const char *name;
  short inst_type; /* registers and immediate values involved */
  short inst_offset_type; /* immediate vals offset from PC? (= 1 for branches) */
  short delay_slots; /* info about delay slots needed after this instr. */
  short immval_mask;
  unsigned long bit_sequence; /* all the fixed bits for the op are set and all the variable bits (reg names, imm vals) are set to 0 */ 
  unsigned long opcode_mask; /* which bits define the opcode */
  enum microblaze_instr instr;
  enum microblaze_instr_type instr_type;
  /* more info about output format here */
} opcodes[MAX_OPCODES] = 

{ 
  {"add",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x00000000, OPCODE_MASK_H4, add, arithmetic_inst },
  {"rsub",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x04000000, OPCODE_MASK_H4, rsub, arithmetic_inst },
  {"addc",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x08000000, OPCODE_MASK_H4, addc, arithmetic_inst },
  {"rsubc", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x0C000000, OPCODE_MASK_H4, rsubc, arithmetic_inst },
  {"addk",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x10000000, OPCODE_MASK_H4, addk, arithmetic_inst },
  {"rsubk", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x14000000, OPCODE_MASK_H4, rsubk, arithmetic_inst },
  {"cmp",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x14000001, OPCODE_MASK_H4, cmp, arithmetic_inst },
  {"cmpu",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x14000003, OPCODE_MASK_H4, cmpu, arithmetic_inst },
  {"addkc", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x18000000, OPCODE_MASK_H4, addkc, arithmetic_inst },
  {"rsubkc",INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x1C000000, OPCODE_MASK_H4, rsubkc, arithmetic_inst },
  {"addi",  INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x20000000, OPCODE_MASK_H, addi, arithmetic_inst },
  {"rsubi", INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x24000000, OPCODE_MASK_H, rsubi, arithmetic_inst },
  {"addic", INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x28000000, OPCODE_MASK_H, addic, arithmetic_inst },
  {"rsubic",INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x2C000000, OPCODE_MASK_H, rsubic, arithmetic_inst },
  {"addik", INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x30000000, OPCODE_MASK_H, addik, arithmetic_inst },
  {"rsubik",INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x34000000, OPCODE_MASK_H, rsubik, arithmetic_inst },
  {"addikc",INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x38000000, OPCODE_MASK_H, addikc, arithmetic_inst },
  {"rsubikc",INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x3C000000, OPCODE_MASK_H, rsubikc, arithmetic_inst },
  {"mul",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x40000000, OPCODE_MASK_H4, mul, mult_inst },
  {"mulh",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x40000001, OPCODE_MASK_H4, mulh, mult_inst },
  {"mulhu", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x40000003, OPCODE_MASK_H4, mulhu, mult_inst },
  {"mulhsu",INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x40000002, OPCODE_MASK_H4, mulhsu, mult_inst },
  {"idiv",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x48000000, OPCODE_MASK_H4, idiv, div_inst },
  {"idivu", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x48000002, OPCODE_MASK_H4, idivu, div_inst },
  {"bsll",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x44000400, OPCODE_MASK_H3, bsll, barrel_shift_inst },
  {"bsra",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x44000200, OPCODE_MASK_H3, bsra, barrel_shift_inst },
  {"bsrl",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x44000000, OPCODE_MASK_H3, bsrl, barrel_shift_inst },
  {"get",   INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C000000, OPCODE_MASK_H32, get, anyware_inst },
  {"put",   INST_TYPE_R1_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C008000, OPCODE_MASK_H32, put, anyware_inst },
  {"nget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C004000, OPCODE_MASK_H32, nget, anyware_inst },
  {"nput",  INST_TYPE_R1_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00C000, OPCODE_MASK_H32, nput, anyware_inst },
  {"cget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C002000, OPCODE_MASK_H32, cget, anyware_inst },
  {"cput",  INST_TYPE_R1_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00A000, OPCODE_MASK_H32, cput, anyware_inst },
  {"ncget", INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C006000, OPCODE_MASK_H32, ncget, anyware_inst },
  {"ncput", INST_TYPE_R1_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00E000, OPCODE_MASK_H32, ncput, anyware_inst },
  {"muli",  INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x60000000, OPCODE_MASK_H, muli, mult_inst },
  {"bslli", INST_TYPE_RD_R1_IMM5, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x64000400, OPCODE_MASK_H3, bslli, barrel_shift_inst },
  {"bsrai", INST_TYPE_RD_R1_IMM5, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x64000200, OPCODE_MASK_H3, bsrai, barrel_shift_inst },
  {"bsrli", INST_TYPE_RD_R1_IMM5, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x64000000, OPCODE_MASK_H3, bsrli, barrel_shift_inst },
  {"or",    INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x80000000, OPCODE_MASK_H4, or, logical_inst },
  {"and",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x84000000, OPCODE_MASK_H4, and, logical_inst },
  {"xor",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x88000000, OPCODE_MASK_H4, xor, logical_inst },
  {"andn",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x8C000000, OPCODE_MASK_H4, andn, logical_inst },
  {"pcmpbf",INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x80000400, OPCODE_MASK_H4, pcmpbf, logical_inst },
  {"pcmpbc",INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x84000400, OPCODE_MASK_H4, pcmpbc, logical_inst },
  {"pcmpeq",INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x88000400, OPCODE_MASK_H4, pcmpeq, logical_inst },
  {"pcmpne",INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x8C000400, OPCODE_MASK_H4, pcmpne, logical_inst },
  {"sra",   INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000001, OPCODE_MASK_H34, sra, logical_inst },
  {"src",   INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000021, OPCODE_MASK_H34, src, logical_inst },
  {"srl",   INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000041, OPCODE_MASK_H34, srl, logical_inst },
  {"sext8", INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000060, OPCODE_MASK_H34, sext8, logical_inst },
  {"sext16",INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000061, OPCODE_MASK_H34, sext16, logical_inst },
  {"wic",   INST_TYPE_RD_R1_SPECIAL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000068, OPCODE_MASK_H34B, wic, special_inst },
  {"wdc",   INST_TYPE_RD_R1_SPECIAL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000064, OPCODE_MASK_H34B, wdc, special_inst },
  {"wdc.clear", INST_TYPE_RD_R1_SPECIAL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000066, OPCODE_MASK_H34B, wdcclear, special_inst },
  {"wdc.flush", INST_TYPE_RD_R1_SPECIAL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x90000074, OPCODE_MASK_H34B, wdcflush, special_inst },
  {"mts",   INST_TYPE_SPECIAL_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_MTS, 0x9400C000, OPCODE_MASK_H13S, mts, special_inst },
  {"mfs",   INST_TYPE_RD_SPECIAL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_MFS, 0x94008000, OPCODE_MASK_H23S, mfs, special_inst },
  {"br",    INST_TYPE_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x98000000, OPCODE_MASK_H124, br, branch_inst },
  {"brd",   INST_TYPE_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x98100000, OPCODE_MASK_H124, brd, branch_inst },
  {"brld",  INST_TYPE_RD_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x98140000, OPCODE_MASK_H24, brld, branch_inst },
  {"bra",   INST_TYPE_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x98080000, OPCODE_MASK_H124, bra, branch_inst },
  {"brad",  INST_TYPE_R2, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x98180000, OPCODE_MASK_H124, brad, branch_inst },
  {"brald", INST_TYPE_RD_R2, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x981C0000, OPCODE_MASK_H24, brald, branch_inst },
  {"brk",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x980C0000, OPCODE_MASK_H24, microblaze_brk, branch_inst },
  {"beq",   INST_TYPE_R1_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9C000000, OPCODE_MASK_H14, beq, branch_inst },
  {"beqd",  INST_TYPE_R1_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9E000000, OPCODE_MASK_H14, beqd, branch_inst },
  {"bne",   INST_TYPE_R1_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9C200000, OPCODE_MASK_H14, bne, branch_inst },
  {"bned",  INST_TYPE_R1_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9E200000, OPCODE_MASK_H14, bned, branch_inst },
  {"blt",   INST_TYPE_R1_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9C400000, OPCODE_MASK_H14, blt, branch_inst },
  {"bltd",  INST_TYPE_R1_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9E400000, OPCODE_MASK_H14, bltd, branch_inst },
  {"ble",   INST_TYPE_R1_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9C600000, OPCODE_MASK_H14, ble, branch_inst },
  {"bled",  INST_TYPE_R1_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9E600000, OPCODE_MASK_H14, bled, branch_inst },
  {"bgt",   INST_TYPE_R1_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9C800000, OPCODE_MASK_H14, bgt, branch_inst },
  {"bgtd",  INST_TYPE_R1_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9E800000, OPCODE_MASK_H14, bgtd, branch_inst },
  {"bge",   INST_TYPE_R1_R2, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9CA00000, OPCODE_MASK_H14, bge, branch_inst },
  {"bged",  INST_TYPE_R1_R2, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x9EA00000, OPCODE_MASK_H14, bged, branch_inst },
  {"ori",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xA0000000, OPCODE_MASK_H, ori, logical_inst },
  {"andi",  INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xA4000000, OPCODE_MASK_H, andi, logical_inst },
  {"xori",  INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xA8000000, OPCODE_MASK_H, xori, logical_inst },
  {"andni", INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xAC000000, OPCODE_MASK_H, andni, logical_inst },
  {"imm",   INST_TYPE_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB0000000, OPCODE_MASK_H12, imm, immediate_inst },
  {"rtsd",  INST_TYPE_R1_IMM, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB6000000, OPCODE_MASK_H1, rtsd, return_inst },
  {"rtid",  INST_TYPE_R1_IMM, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB6200000, OPCODE_MASK_H1, rtid, return_inst },
  {"rtbd",  INST_TYPE_R1_IMM, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB6400000, OPCODE_MASK_H1, rtbd, return_inst },
  {"rted",  INST_TYPE_R1_IMM, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB6800000, OPCODE_MASK_H1, rted, return_inst },
  {"bri",   INST_TYPE_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB8000000, OPCODE_MASK_H12, bri, branch_inst },
  {"brid",  INST_TYPE_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB8100000, OPCODE_MASK_H12, brid, branch_inst },
  {"brlid", INST_TYPE_RD_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB8140000, OPCODE_MASK_H2, brlid, branch_inst },
  {"brai",  INST_TYPE_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB8080000, OPCODE_MASK_H12, brai, branch_inst },
  {"braid", INST_TYPE_IMM, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB8180000, OPCODE_MASK_H12, braid, branch_inst },
  {"bralid",INST_TYPE_RD_IMM, INST_NO_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB81C0000, OPCODE_MASK_H2, bralid, branch_inst },
  {"brki",  INST_TYPE_RD_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB80C0000, OPCODE_MASK_H2, brki, branch_inst },
  {"beqi",  INST_TYPE_R1_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBC000000, OPCODE_MASK_H1, beqi, branch_inst },
  {"beqid", INST_TYPE_R1_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBE000000, OPCODE_MASK_H1, beqid, branch_inst },
  {"bnei",  INST_TYPE_R1_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBC200000, OPCODE_MASK_H1, bnei, branch_inst },
  {"bneid", INST_TYPE_R1_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBE200000, OPCODE_MASK_H1, bneid, branch_inst },
  {"blti",  INST_TYPE_R1_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBC400000, OPCODE_MASK_H1, blti, branch_inst },
  {"bltid", INST_TYPE_R1_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBE400000, OPCODE_MASK_H1, bltid, branch_inst },
  {"blei",  INST_TYPE_R1_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBC600000, OPCODE_MASK_H1, blei, branch_inst },
  {"bleid", INST_TYPE_R1_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBE600000, OPCODE_MASK_H1, bleid, branch_inst },
  {"bgti",  INST_TYPE_R1_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBC800000, OPCODE_MASK_H1, bgti, branch_inst },
  {"bgtid", INST_TYPE_R1_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBE800000, OPCODE_MASK_H1, bgtid, branch_inst },
  {"bgei",  INST_TYPE_R1_IMM, INST_PC_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBCA00000, OPCODE_MASK_H1, bgei, branch_inst },
  {"bgeid", INST_TYPE_R1_IMM, INST_PC_OFFSET, DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xBEA00000, OPCODE_MASK_H1, bgeid, branch_inst },
  {"lbu",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xC0000000, OPCODE_MASK_H4, lbu, memory_load_inst },
  {"lhu",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xC4000000, OPCODE_MASK_H4, lhu, memory_load_inst },
  {"lw",    INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xC8000000, OPCODE_MASK_H4, lw, memory_load_inst },
  {"lwx",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xC8000400, OPCODE_MASK_H4, lwx, memory_load_inst },
  {"sb",    INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xD0000000, OPCODE_MASK_H4, sb, memory_store_inst },
  {"sh",    INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xD4000000, OPCODE_MASK_H4, sh, memory_store_inst },
  {"sw",    INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xD8000000, OPCODE_MASK_H4, sw, memory_store_inst },
  {"swx",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xD8000400, OPCODE_MASK_H4, swx, memory_store_inst },
  {"lbui",  INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xE0000000, OPCODE_MASK_H, lbui, memory_load_inst },
  {"lhui",  INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xE4000000, OPCODE_MASK_H, lhui, memory_load_inst },
  {"lwi",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xE8000000, OPCODE_MASK_H, lwi, memory_load_inst },
  {"sbi",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xF0000000, OPCODE_MASK_H, sbi, memory_store_inst },
  {"shi",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xF4000000, OPCODE_MASK_H, shi, memory_store_inst },
  {"swi",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xF8000000, OPCODE_MASK_H, swi, memory_store_inst },
  {"nop",   INST_TYPE_NONE, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x80000000, OPCODE_MASK_H1234, invalid_inst, logical_inst }, /* translates to or r0, r0, r0 */
  {"la",    INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x30000000, OPCODE_MASK_H, invalid_inst, arithmetic_inst }, /* la translates to addik */
  {"tuqula",INST_TYPE_RD, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x3000002A, OPCODE_MASK_H, invalid_inst, arithmetic_inst }, /* tuqula rd translates to addik rd, r0, 42 */
  {"not",   INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xA800FFFF, OPCODE_MASK_H34, invalid_inst, logical_inst }, /* not translates to xori rd,ra,-1 */
  {"neg",   INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x04000000, OPCODE_MASK_H, invalid_inst, arithmetic_inst }, /* neg translates to rsub rd, ra, r0 */
  {"rtb",   INST_TYPE_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xB6000004, OPCODE_MASK_H1, invalid_inst, return_inst }, /* rtb translates to rts rd, 4 */
  {"sub",   INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x04000000, OPCODE_MASK_H, invalid_inst, arithmetic_inst }, /* sub translates to rsub rd, rb, ra */
  {"lmi",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xE8000000, OPCODE_MASK_H, invalid_inst, memory_load_inst },
  {"smi",   INST_TYPE_RD_R1_IMM, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0xF8000000, OPCODE_MASK_H, invalid_inst, memory_store_inst },
  {"msrset",INST_TYPE_RD_IMM15, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x94100000, OPCODE_MASK_H23N, msrset, special_inst },
  {"msrclr",INST_TYPE_RD_IMM15, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x94110000, OPCODE_MASK_H23N, msrclr, special_inst },
  {"fadd",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000000, OPCODE_MASK_H4, fadd, arithmetic_inst },
  {"frsub",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000080, OPCODE_MASK_H4, frsub, arithmetic_inst },
  {"fmul",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000100, OPCODE_MASK_H4, fmul, arithmetic_inst },
  {"fdiv",  INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000180, OPCODE_MASK_H4, fdiv, arithmetic_inst },
  {"fcmp.lt", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000210, OPCODE_MASK_H4, fcmp_lt, arithmetic_inst },
  {"fcmp.eq", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000220, OPCODE_MASK_H4, fcmp_eq, arithmetic_inst },
  {"fcmp.le", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000230, OPCODE_MASK_H4, fcmp_le, arithmetic_inst },
  {"fcmp.gt", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000240, OPCODE_MASK_H4, fcmp_gt, arithmetic_inst },
  {"fcmp.ne", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000250, OPCODE_MASK_H4, fcmp_ne, arithmetic_inst },
  {"fcmp.ge", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000260, OPCODE_MASK_H4, fcmp_ge, arithmetic_inst },
  {"fcmp.un", INST_TYPE_RD_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000200, OPCODE_MASK_H4, fcmp_un, arithmetic_inst },
  {"flt",   INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000280, OPCODE_MASK_H4, flt,   arithmetic_inst },
  {"fint",  INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000300, OPCODE_MASK_H4, fint,  arithmetic_inst },
  {"fsqrt", INST_TYPE_RD_R1, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x58000380, OPCODE_MASK_H4, fsqrt, arithmetic_inst },
  {"tget",   INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C001000, OPCODE_MASK_H32, tget,   anyware_inst },
  {"tcget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C003000, OPCODE_MASK_H32, tcget,  anyware_inst },
  {"tnget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C005000, OPCODE_MASK_H32, tnget,  anyware_inst },
  {"tncget", INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C007000, OPCODE_MASK_H32, tncget, anyware_inst },
  {"tput",   INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C009000, OPCODE_MASK_H32, tput,   anyware_inst },
  {"tcput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00B000, OPCODE_MASK_H32, tcput,  anyware_inst },
  {"tnput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00D000, OPCODE_MASK_H32, tnput,  anyware_inst },
  {"tncput", INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00F000, OPCODE_MASK_H32, tncput, anyware_inst },
 
  {"eget",   INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C000400, OPCODE_MASK_H32, eget,   anyware_inst },
  {"ecget",  INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C002400, OPCODE_MASK_H32, ecget,  anyware_inst },
  {"neget",  INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C004400, OPCODE_MASK_H32, neget,  anyware_inst },
  {"necget", INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C006400, OPCODE_MASK_H32, necget, anyware_inst },
  {"eput",   INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C008400, OPCODE_MASK_H32, eput,   anyware_inst },
  {"ecput",  INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00A400, OPCODE_MASK_H32, ecput,  anyware_inst },
  {"neput",  INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00C400, OPCODE_MASK_H32, neput,  anyware_inst },
  {"necput", INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00E400, OPCODE_MASK_H32, necput, anyware_inst },
 
  {"teget",   INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C001400, OPCODE_MASK_H32, teget,   anyware_inst },
  {"tecget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C003400, OPCODE_MASK_H32, tecget,  anyware_inst },
  {"tneget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C005400, OPCODE_MASK_H32, tneget,  anyware_inst },
  {"tnecget", INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C007400, OPCODE_MASK_H32, tnecget, anyware_inst },
  {"teput",   INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C009400, OPCODE_MASK_H32, teput,   anyware_inst },
  {"tecput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00B400, OPCODE_MASK_H32, tecput,  anyware_inst },
  {"tneput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00D400, OPCODE_MASK_H32, tneput,  anyware_inst },
  {"tnecput", INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00F400, OPCODE_MASK_H32, tnecput, anyware_inst },
 
  {"aget",   INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C000800, OPCODE_MASK_H32, aget,   anyware_inst },
  {"caget",  INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C002800, OPCODE_MASK_H32, caget,  anyware_inst },
  {"naget",  INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C004800, OPCODE_MASK_H32, naget,  anyware_inst },
  {"ncaget", INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C006800, OPCODE_MASK_H32, ncaget, anyware_inst },
  {"aput",   INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C008800, OPCODE_MASK_H32, aput,   anyware_inst },
  {"caput",  INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00A800, OPCODE_MASK_H32, caput,  anyware_inst },
  {"naput",  INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00C800, OPCODE_MASK_H32, naput,  anyware_inst },
  {"ncaput", INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00E800, OPCODE_MASK_H32, ncaput, anyware_inst },
 
  {"taget",   INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C001800, OPCODE_MASK_H32, taget,   anyware_inst },
  {"tcaget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C003800, OPCODE_MASK_H32, tcaget,  anyware_inst },
  {"tnaget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C005800, OPCODE_MASK_H32, tnaget,  anyware_inst },
  {"tncaget", INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C007800, OPCODE_MASK_H32, tncaget, anyware_inst },
  {"taput",   INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C009800, OPCODE_MASK_H32, taput,   anyware_inst },
  {"tcaput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00B800, OPCODE_MASK_H32, tcaput,  anyware_inst },
  {"tnaput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00D800, OPCODE_MASK_H32, tnaput,  anyware_inst },
  {"tncaput", INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00F800, OPCODE_MASK_H32, tncaput, anyware_inst },
 
  {"eaget",   INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C000C00, OPCODE_MASK_H32, eget,   anyware_inst },
  {"ecaget",  INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C002C00, OPCODE_MASK_H32, ecget,  anyware_inst },
  {"neaget",  INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C004C00, OPCODE_MASK_H32, neget,  anyware_inst },
  {"necaget", INST_TYPE_RD_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C006C00, OPCODE_MASK_H32, necget, anyware_inst },
  {"eaput",   INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C008C00, OPCODE_MASK_H32, eput,   anyware_inst },
  {"ecaput",  INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00AC00, OPCODE_MASK_H32, ecput,  anyware_inst },
  {"neaput",  INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00CC00, OPCODE_MASK_H32, neput,  anyware_inst },
  {"necaput", INST_TYPE_R1_RFSL,  INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00EC00, OPCODE_MASK_H32, necput, anyware_inst },
 
  {"teaget",   INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C001C00, OPCODE_MASK_H32, teaget,   anyware_inst },
  {"tecaget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C003C00, OPCODE_MASK_H32, tecaget,  anyware_inst },
  {"tneaget",  INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C005C00, OPCODE_MASK_H32, tneaget,  anyware_inst },
  {"tnecaget", INST_TYPE_RD_RFSL, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C007C00, OPCODE_MASK_H32, tnecaget, anyware_inst },
  {"teaput",   INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C009C00, OPCODE_MASK_H32, teaput,   anyware_inst },
  {"tecaput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00BC00, OPCODE_MASK_H32, tecaput,  anyware_inst },
  {"tneaput",  INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00DC00, OPCODE_MASK_H32, tneaput,  anyware_inst },
  {"tnecaput", INST_TYPE_RFSL,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x6C00FC00, OPCODE_MASK_H32, tnecaput, anyware_inst },
 
  {"getd",    INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000000, OPCODE_MASK_H34C, getd,    anyware_inst },
  {"tgetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000080, OPCODE_MASK_H34C, tgetd,   anyware_inst },
  {"cgetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000100, OPCODE_MASK_H34C, cgetd,   anyware_inst },
  {"tcgetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000180, OPCODE_MASK_H34C, tcgetd,  anyware_inst },
  {"ngetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000200, OPCODE_MASK_H34C, ngetd,   anyware_inst },
  {"tngetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000280, OPCODE_MASK_H34C, tngetd,  anyware_inst },
  {"ncgetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000300, OPCODE_MASK_H34C, ncgetd,  anyware_inst },
  {"tncgetd", INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000380, OPCODE_MASK_H34C, tncgetd, anyware_inst },
  {"putd",    INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000400, OPCODE_MASK_H34C, putd,    anyware_inst },
  {"tputd",   INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000480, OPCODE_MASK_H34C, tputd,   anyware_inst },
  {"cputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000500, OPCODE_MASK_H34C, cputd,   anyware_inst },
  {"tcputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000580, OPCODE_MASK_H34C, tcputd,  anyware_inst },
  {"nputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000600, OPCODE_MASK_H34C, nputd,   anyware_inst },
  {"tnputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000680, OPCODE_MASK_H34C, tnputd,  anyware_inst },
  {"ncputd",  INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000700, OPCODE_MASK_H34C, ncputd,  anyware_inst },
  {"tncputd", INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000780, OPCODE_MASK_H34C, tncputd, anyware_inst },
 
  {"egetd",    INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000020, OPCODE_MASK_H34C, egetd,    anyware_inst },
  {"tegetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0000A0, OPCODE_MASK_H34C, tegetd,   anyware_inst },
  {"ecgetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000120, OPCODE_MASK_H34C, ecgetd,   anyware_inst },
  {"tecgetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0001A0, OPCODE_MASK_H34C, tecgetd,  anyware_inst },
  {"negetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000220, OPCODE_MASK_H34C, negetd,   anyware_inst },
  {"tnegetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0002A0, OPCODE_MASK_H34C, tnegetd,  anyware_inst },
  {"necgetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000320, OPCODE_MASK_H34C, necgetd,  anyware_inst },
  {"tnecgetd", INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0003A0, OPCODE_MASK_H34C, tnecgetd, anyware_inst },
  {"eputd",    INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000420, OPCODE_MASK_H34C, eputd,    anyware_inst },
  {"teputd",   INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0004A0, OPCODE_MASK_H34C, teputd,   anyware_inst },
  {"ecputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000520, OPCODE_MASK_H34C, ecputd,   anyware_inst },
  {"tecputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0005A0, OPCODE_MASK_H34C, tecputd,  anyware_inst },
  {"neputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000620, OPCODE_MASK_H34C, neputd,   anyware_inst },
  {"tneputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0006A0, OPCODE_MASK_H34C, tneputd,  anyware_inst },
  {"necputd",  INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000720, OPCODE_MASK_H34C, necputd,  anyware_inst },
  {"tnecputd", INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0007A0, OPCODE_MASK_H34C, tnecputd, anyware_inst },
 
  {"agetd",    INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000040, OPCODE_MASK_H34C, agetd,    anyware_inst },
  {"tagetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0000C0, OPCODE_MASK_H34C, tagetd,   anyware_inst },
  {"cagetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000140, OPCODE_MASK_H34C, cagetd,   anyware_inst },
  {"tcagetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0001C0, OPCODE_MASK_H34C, tcagetd,  anyware_inst },
  {"nagetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000240, OPCODE_MASK_H34C, nagetd,   anyware_inst },
  {"tnagetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0002C0, OPCODE_MASK_H34C, tnagetd,  anyware_inst },
  {"ncagetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000340, OPCODE_MASK_H34C, ncagetd,  anyware_inst },
  {"tncagetd", INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0003C0, OPCODE_MASK_H34C, tncagetd, anyware_inst },
  {"aputd",    INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000440, OPCODE_MASK_H34C, aputd,    anyware_inst },
  {"taputd",   INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0004C0, OPCODE_MASK_H34C, taputd,   anyware_inst },
  {"caputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000540, OPCODE_MASK_H34C, caputd,   anyware_inst },
  {"tcaputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0005C0, OPCODE_MASK_H34C, tcaputd,  anyware_inst },
  {"naputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000640, OPCODE_MASK_H34C, naputd,   anyware_inst },
  {"tnaputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0006C0, OPCODE_MASK_H34C, tnaputd,  anyware_inst },
  {"ncaputd",  INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000740, OPCODE_MASK_H34C, ncaputd,  anyware_inst },
  {"tncaputd", INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0007C0, OPCODE_MASK_H34C, tncaputd, anyware_inst },
 
  {"eagetd",    INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000060, OPCODE_MASK_H34C, eagetd,    anyware_inst },
  {"teagetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0000E0, OPCODE_MASK_H34C, teagetd,   anyware_inst },
  {"ecagetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000160, OPCODE_MASK_H34C, ecagetd,   anyware_inst },
  {"tecagetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0001E0, OPCODE_MASK_H34C, tecagetd,  anyware_inst },
  {"neagetd",   INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000260, OPCODE_MASK_H34C, neagetd,   anyware_inst },
  {"tneagetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0002E0, OPCODE_MASK_H34C, tneagetd,  anyware_inst },
  {"necagetd",  INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000360, OPCODE_MASK_H34C, necagetd,  anyware_inst },
  {"tnecagetd", INST_TYPE_RD_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0003E0, OPCODE_MASK_H34C, tnecagetd, anyware_inst },
  {"eaputd",    INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000460, OPCODE_MASK_H34C, eaputd,    anyware_inst },
  {"teaputd",   INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0004E0, OPCODE_MASK_H34C, teaputd,   anyware_inst },
  {"ecaputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000560, OPCODE_MASK_H34C, ecaputd,   anyware_inst },
  {"tecaputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0005E0, OPCODE_MASK_H34C, tecaputd,  anyware_inst },
  {"neaputd",   INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000660, OPCODE_MASK_H34C, neaputd,   anyware_inst },
  {"tneaputd",  INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0006E0, OPCODE_MASK_H34C, tneaputd,  anyware_inst },
  {"necaputd",  INST_TYPE_R1_R2, INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C000760, OPCODE_MASK_H34C, necaputd,  anyware_inst },
  {"tnecaputd", INST_TYPE_R2,    INST_NO_OFFSET, NO_DELAY_SLOT, IMMVAL_MASK_NON_SPECIAL, 0x4C0007E0, OPCODE_MASK_H34C, tnecaputd, anyware_inst },
  {"", 0, 0, 0, 0, 0, 0, 0, 0},
};

/* prefix for register names */
static const char register_prefix[] = "r";
static const char fsl_register_prefix[] = "rfsl";
static const char pvr_register_prefix[] = "rpvr";


/* #defines for valid immediate range */
#define MIN_IMM  ((int) 0x80000000)
#define MAX_IMM  ((int) 0x7fffffff)

#define MIN_IMM15 ((int) 0x0000)
#define MAX_IMM15 ((int) 0x7fff)

#endif /* MICROBLAZE_OPC */

#include "disas/dis-asm.h"

#define get_field_rd(instr) get_field(instr, RD_MASK, RD_LOW)
#define get_field_r1(instr) get_field(instr, RA_MASK, RA_LOW)
#define get_field_r2(instr) get_field(instr, RB_MASK, RB_LOW)
#define get_int_field_imm(instr) ((instr & IMM_MASK) >> IMM_LOW)
#define get_int_field_r1(instr) ((instr & RA_MASK) >> RA_LOW)

/* Local function prototypes. */

static char * get_field (long instr, long mask, unsigned short low);
static char * get_field_imm (long instr);
static char * get_field_imm5 (long instr);
static char * get_field_rfsl (long instr);
static char * get_field_imm15 (long instr);
#if 0
static char * get_field_unsigned_imm (long instr);
#endif

static char *
get_field (long instr, long mask, unsigned short low)
{
  char tmpstr[25];
  sprintf(tmpstr, "%s%d", register_prefix, (int)((instr & mask) >> low));
  return(strdup(tmpstr));
}

static char *
get_field_imm (long instr)
{
  char tmpstr[25];
  sprintf(tmpstr, "%d", (short)((instr & IMM_MASK) >> IMM_LOW));
  return(strdup(tmpstr));
}

static char *
get_field_imm5 (long instr)
{
  char tmpstr[25];
  sprintf(tmpstr, "%d", (short)((instr & IMM5_MASK) >> IMM_LOW));
  return(strdup(tmpstr));
}

static char *
get_field_rfsl (long instr)
{
  char tmpstr[25];
  sprintf(tmpstr, "%s%d", fsl_register_prefix, (short)((instr & RFSL_MASK) >> IMM_LOW));
  return(strdup(tmpstr));
}

static char *
get_field_imm15 (long instr)
{
  char tmpstr[25];
  sprintf(tmpstr, "%d", (short)((instr & IMM15_MASK) >> IMM_LOW));
  return(strdup(tmpstr));
}

#if 0
static char *
get_field_unsigned_imm (long instr)
{
  char tmpstr[25];
  sprintf(tmpstr, "%d", (int)((instr & IMM_MASK) >> IMM_LOW));
  return(strdup(tmpstr));
}
#endif

/*
  char *
  get_field_special (instr) 
  long instr;
  {
  char tmpstr[25];
  
  sprintf(tmpstr, "%s%s", register_prefix, (((instr & IMM_MASK) >> IMM_LOW) & REG_MSR_MASK) == 0 ? "pc" : "msr");
  
  return(strdup(tmpstr));
  }
*/

static char *
get_field_special(long instr, const struct op_code_struct *op)
{
   char tmpstr[25];
   char spr[6];

   switch ( (((instr & IMM_MASK) >> IMM_LOW) ^ op->immval_mask) ) {

   case REG_MSR_MASK :
      strcpy(spr, "msr");
      break;
   case REG_PC_MASK :
      strcpy(spr, "pc");
      break;
   case REG_EAR_MASK :
      strcpy(spr, "ear");
      break;
   case REG_ESR_MASK :
      strcpy(spr, "esr");
      break;
   case REG_FSR_MASK :
      strcpy(spr, "fsr");
      break;
   case REG_BTR_MASK :
      strcpy(spr, "btr");
      break;      
   case REG_EDR_MASK :
      strcpy(spr, "edr");
      break;
   case REG_PID_MASK :
      strcpy(spr, "pid");
      break;
   case REG_ZPR_MASK :
      strcpy(spr, "zpr");
      break;
   case REG_TLBX_MASK :
      strcpy(spr, "tlbx");
      break;
   case REG_TLBLO_MASK :
      strcpy(spr, "tlblo");
      break;
   case REG_TLBHI_MASK :
      strcpy(spr, "tlbhi");
      break;
   case REG_TLBSX_MASK :
      strcpy(spr, "tlbsx");
      break;
   default :
     {
       if ( ((((instr & IMM_MASK) >> IMM_LOW) ^ op->immval_mask) & 0xE000) == REG_PVR_MASK) {
	  sprintf(tmpstr, "%s%u", pvr_register_prefix,
                 (unsigned short)(((instr & IMM_MASK) >> IMM_LOW) ^
                                  op->immval_mask) ^ REG_PVR_MASK);
	 return(strdup(tmpstr));
       } else {
	 strcpy(spr, "pc");
       }
     }
     break;
   }
   
   sprintf(tmpstr, "%s%s", register_prefix, spr);
   return(strdup(tmpstr));
}

static unsigned long
read_insn_microblaze (bfd_vma memaddr, 
		      struct disassemble_info *info,
		      const struct op_code_struct **opr)
{
  unsigned char       ibytes[4];
  int                 status;
  const struct op_code_struct *op;
  unsigned long inst;

  status = info->read_memory_func (memaddr, ibytes, 4, info);

  if (status != 0) 
    {
      info->memory_error_func (status, memaddr, info);
      return 0;
    }

  if (info->endian == BFD_ENDIAN_BIG)
    inst = ((unsigned)ibytes[0] << 24) | (ibytes[1] << 16)
      | (ibytes[2] << 8) | ibytes[3];
  else if (info->endian == BFD_ENDIAN_LITTLE)
    inst = ((unsigned)ibytes[3] << 24) | (ibytes[2] << 16)
      | (ibytes[1] << 8) | ibytes[0];
  else
    abort ();

  /* Just a linear search of the table.  */
  for (op = opcodes; op->name != 0; op ++)
    if (op->bit_sequence == (inst & op->opcode_mask))
      break;

  *opr = op;
  return inst;
}


int 
print_insn_microblaze (bfd_vma memaddr, struct disassemble_info * info)
{
  fprintf_function    fprintf_func = info->fprintf_func;
  void *              stream = info->stream;
  unsigned long       inst, prev_inst;
  const struct op_code_struct *op, *pop;
  int                 immval = 0;
  bfd_boolean         immfound = FALSE;
  static bfd_vma prev_insn_addr = -1; /*init the prev insn addr */
  static int     prev_insn_vma = -1;  /*init the prev insn vma */
  int            curr_insn_vma = info->buffer_vma;

  info->bytes_per_chunk = 4;

  inst = read_insn_microblaze (memaddr, info, &op);
  if (inst == 0) {
    return -1;
  }
  
  if (prev_insn_vma == curr_insn_vma) {
  if (memaddr-(info->bytes_per_chunk) == prev_insn_addr) {
    prev_inst = read_insn_microblaze (prev_insn_addr, info, &pop);
    if (prev_inst == 0)
      return -1;
    if (pop->instr == imm) {
      immval = (get_int_field_imm(prev_inst) << 16) & 0xffff0000;
      immfound = TRUE;
    }
    else {
      immval = 0;
      immfound = FALSE;
    }
  }
  }
  /* make curr insn as prev insn */
  prev_insn_addr = memaddr;
  prev_insn_vma = curr_insn_vma;

  if (op->name == 0) {
    fprintf_func (stream, ".short 0x%04lx", inst);
  }
  else
    {
      fprintf_func (stream, "%s", op->name);
      
      switch (op->inst_type)
	{
  case INST_TYPE_RD_R1_R2:
     fprintf_func(stream, "\t%s, %s, %s", get_field_rd(inst), get_field_r1(inst), get_field_r2(inst));
     break;
        case INST_TYPE_RD_R1_IMM:
	  fprintf_func(stream, "\t%s, %s, %s", get_field_rd(inst), get_field_r1(inst), get_field_imm(inst));
	  if (info->print_address_func && get_int_field_r1(inst) == 0 && info->symbol_at_address_func) {
	    if (immfound)
	      immval |= (get_int_field_imm(inst) & 0x0000ffff);
	    else {
	      immval = get_int_field_imm(inst);
	      if (immval & 0x8000)
		immval |= 0xFFFF0000;
	    }
	    if (immval > 0 && info->symbol_at_address_func(immval, info)) {
	      fprintf_func (stream, "\t// ");
	      info->print_address_func (immval, info);
	    }
	  }
	  break;
	case INST_TYPE_RD_R1_IMM5:
	  fprintf_func(stream, "\t%s, %s, %s", get_field_rd(inst), get_field_r1(inst), get_field_imm5(inst));
	  break;
	case INST_TYPE_RD_RFSL:
	  fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_rfsl(inst));
	  break;
	case INST_TYPE_R1_RFSL:
	  fprintf_func(stream, "\t%s, %s", get_field_r1(inst), get_field_rfsl(inst));
	  break;
	case INST_TYPE_RD_SPECIAL:
	  fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_special(inst, op));
	  break;
	case INST_TYPE_SPECIAL_R1:
	  fprintf_func(stream, "\t%s, %s", get_field_special(inst, op), get_field_r1(inst));
	  break;
	case INST_TYPE_RD_R1:
	  fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_r1(inst));
	  break;
	case INST_TYPE_R1_R2:
	  fprintf_func(stream, "\t%s, %s", get_field_r1(inst), get_field_r2(inst));
	  break;
	case INST_TYPE_R1_IMM:
	  fprintf_func(stream, "\t%s, %s", get_field_r1(inst), get_field_imm(inst));
	  /* The non-pc relative instructions are returns, which shouldn't 
	     have a label printed */
	  if (info->print_address_func && op->inst_offset_type == INST_PC_OFFSET && info->symbol_at_address_func) {
	    if (immfound)
	      immval |= (get_int_field_imm(inst) & 0x0000ffff);
	    else {
	      immval = get_int_field_imm(inst);
	      if (immval & 0x8000)
		immval |= 0xFFFF0000;
	    }
	    immval += memaddr;
	    if (immval > 0 && info->symbol_at_address_func(immval, info)) {
	      fprintf_func (stream, "\t// ");
	      info->print_address_func (immval, info);
	    } else {
	      fprintf_func (stream, "\t\t// ");
	      fprintf_func (stream, "%x", immval);
	    }
	  }
	  break;
        case INST_TYPE_RD_IMM:
	  fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_imm(inst));
	  if (info->print_address_func && info->symbol_at_address_func) {
	    if (immfound)
	      immval |= (get_int_field_imm(inst) & 0x0000ffff);
	    else {
	      immval = get_int_field_imm(inst);
	      if (immval & 0x8000)
		immval |= 0xFFFF0000;
	    }
	    if (op->inst_offset_type == INST_PC_OFFSET)
	      immval += (int) memaddr;
	    if (info->symbol_at_address_func(immval, info)) {
	      fprintf_func (stream, "\t// ");
	      info->print_address_func (immval, info);
	    } 
	  }
	  break;
        case INST_TYPE_IMM:
	  fprintf_func(stream, "\t%s", get_field_imm(inst));
	  if (info->print_address_func && info->symbol_at_address_func && op->instr != imm) {
	    if (immfound)
	      immval |= (get_int_field_imm(inst) & 0x0000ffff);
	    else {
	      immval = get_int_field_imm(inst);
	      if (immval & 0x8000)
		immval |= 0xFFFF0000;
	    }
	    if (op->inst_offset_type == INST_PC_OFFSET)
	      immval += (int) memaddr;
	    if (immval > 0 && info->symbol_at_address_func(immval, info)) {
	      fprintf_func (stream, "\t// ");
	      info->print_address_func (immval, info);
	    } else if (op->inst_offset_type == INST_PC_OFFSET) {
	      fprintf_func (stream, "\t\t// ");
	      fprintf_func (stream, "%x", immval);
	    }
	  }
	  break;
        case INST_TYPE_RD_R2:
	  fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_r2(inst));
	  break;
  case INST_TYPE_R2:
     fprintf_func(stream, "\t%s", get_field_r2(inst));
     break;
  case INST_TYPE_R1:
     fprintf_func(stream, "\t%s", get_field_r1(inst));
     break;
  case INST_TYPE_RD_R1_SPECIAL:
     fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_r2(inst));
     break;
  case INST_TYPE_RD_IMM15:
     fprintf_func(stream, "\t%s, %s", get_field_rd(inst), get_field_imm15(inst));
     break;
     /* For tuqula instruction */
  case INST_TYPE_RD:
     fprintf_func(stream, "\t%s", get_field_rd(inst));
     break;
  case INST_TYPE_RFSL:
     fprintf_func(stream, "\t%s", get_field_rfsl(inst));
     break;
  default:
	  /* if the disassembler lags the instruction set */
	  fprintf_func (stream, "\tundecoded operands, inst is 0x%04lx", inst);
	  break;
	}
    }
  
  /* Say how many bytes we consumed? */
  return 4;
}
