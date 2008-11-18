/*
 *  MIPS32 emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"
#include "tcg-op.h"
#include "qemu-common.h"

#include "helper.h"
#define GEN_HELPER 1
#include "helper.h"

//#define MIPS_DEBUG_DISAS
//#define MIPS_DEBUG_SIGN_EXTENSIONS
//#define MIPS_SINGLE_STEP

/* MIPS major opcodes */
#define MASK_OP_MAJOR(op)  (op & (0x3F << 26))

enum {
    /* indirect opcode tables */
    OPC_SPECIAL  = (0x00 << 26),
    OPC_REGIMM   = (0x01 << 26),
    OPC_CP0      = (0x10 << 26),
    OPC_CP1      = (0x11 << 26),
    OPC_CP2      = (0x12 << 26),
    OPC_CP3      = (0x13 << 26),
    OPC_SPECIAL2 = (0x1C << 26),
    OPC_SPECIAL3 = (0x1F << 26),
    /* arithmetic with immediate */
    OPC_ADDI     = (0x08 << 26),
    OPC_ADDIU    = (0x09 << 26),
    OPC_SLTI     = (0x0A << 26),
    OPC_SLTIU    = (0x0B << 26),
    OPC_ANDI     = (0x0C << 26),
    OPC_ORI      = (0x0D << 26),
    OPC_XORI     = (0x0E << 26),
    OPC_LUI      = (0x0F << 26),
    OPC_DADDI    = (0x18 << 26),
    OPC_DADDIU   = (0x19 << 26),
    /* Jump and branches */
    OPC_J        = (0x02 << 26),
    OPC_JAL      = (0x03 << 26),
    OPC_BEQ      = (0x04 << 26),  /* Unconditional if rs = rt = 0 (B) */
    OPC_BEQL     = (0x14 << 26),
    OPC_BNE      = (0x05 << 26),
    OPC_BNEL     = (0x15 << 26),
    OPC_BLEZ     = (0x06 << 26),
    OPC_BLEZL    = (0x16 << 26),
    OPC_BGTZ     = (0x07 << 26),
    OPC_BGTZL    = (0x17 << 26),
    OPC_JALX     = (0x1D << 26),  /* MIPS 16 only */
    /* Load and stores */
    OPC_LDL      = (0x1A << 26),
    OPC_LDR      = (0x1B << 26),
    OPC_LB       = (0x20 << 26),
    OPC_LH       = (0x21 << 26),
    OPC_LWL      = (0x22 << 26),
    OPC_LW       = (0x23 << 26),
    OPC_LBU      = (0x24 << 26),
    OPC_LHU      = (0x25 << 26),
    OPC_LWR      = (0x26 << 26),
    OPC_LWU      = (0x27 << 26),
    OPC_SB       = (0x28 << 26),
    OPC_SH       = (0x29 << 26),
    OPC_SWL      = (0x2A << 26),
    OPC_SW       = (0x2B << 26),
    OPC_SDL      = (0x2C << 26),
    OPC_SDR      = (0x2D << 26),
    OPC_SWR      = (0x2E << 26),
    OPC_LL       = (0x30 << 26),
    OPC_LLD      = (0x34 << 26),
    OPC_LD       = (0x37 << 26),
    OPC_SC       = (0x38 << 26),
    OPC_SCD      = (0x3C << 26),
    OPC_SD       = (0x3F << 26),
    /* Floating point load/store */
    OPC_LWC1     = (0x31 << 26),
    OPC_LWC2     = (0x32 << 26),
    OPC_LDC1     = (0x35 << 26),
    OPC_LDC2     = (0x36 << 26),
    OPC_SWC1     = (0x39 << 26),
    OPC_SWC2     = (0x3A << 26),
    OPC_SDC1     = (0x3D << 26),
    OPC_SDC2     = (0x3E << 26),
    /* MDMX ASE specific */
    OPC_MDMX     = (0x1E << 26),
    /* Cache and prefetch */
    OPC_CACHE    = (0x2F << 26),
    OPC_PREF     = (0x33 << 26),
    /* Reserved major opcode */
    OPC_MAJOR3B_RESERVED = (0x3B << 26),
};

/* MIPS special opcodes */
#define MASK_SPECIAL(op)   MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    /* Shifts */
    OPC_SLL      = 0x00 | OPC_SPECIAL,
    /* NOP is SLL r0, r0, 0   */
    /* SSNOP is SLL r0, r0, 1 */
    /* EHB is SLL r0, r0, 3 */
    OPC_SRL      = 0x02 | OPC_SPECIAL, /* also ROTR */
    OPC_SRA      = 0x03 | OPC_SPECIAL,
    OPC_SLLV     = 0x04 | OPC_SPECIAL,
    OPC_SRLV     = 0x06 | OPC_SPECIAL, /* also ROTRV */
    OPC_SRAV     = 0x07 | OPC_SPECIAL,
    OPC_DSLLV    = 0x14 | OPC_SPECIAL,
    OPC_DSRLV    = 0x16 | OPC_SPECIAL, /* also DROTRV */
    OPC_DSRAV    = 0x17 | OPC_SPECIAL,
    OPC_DSLL     = 0x38 | OPC_SPECIAL,
    OPC_DSRL     = 0x3A | OPC_SPECIAL, /* also DROTR */
    OPC_DSRA     = 0x3B | OPC_SPECIAL,
    OPC_DSLL32   = 0x3C | OPC_SPECIAL,
    OPC_DSRL32   = 0x3E | OPC_SPECIAL, /* also DROTR32 */
    OPC_DSRA32   = 0x3F | OPC_SPECIAL,
    /* Multiplication / division */
    OPC_MULT     = 0x18 | OPC_SPECIAL,
    OPC_MULTU    = 0x19 | OPC_SPECIAL,
    OPC_DIV      = 0x1A | OPC_SPECIAL,
    OPC_DIVU     = 0x1B | OPC_SPECIAL,
    OPC_DMULT    = 0x1C | OPC_SPECIAL,
    OPC_DMULTU   = 0x1D | OPC_SPECIAL,
    OPC_DDIV     = 0x1E | OPC_SPECIAL,
    OPC_DDIVU    = 0x1F | OPC_SPECIAL,
    /* 2 registers arithmetic / logic */
    OPC_ADD      = 0x20 | OPC_SPECIAL,
    OPC_ADDU     = 0x21 | OPC_SPECIAL,
    OPC_SUB      = 0x22 | OPC_SPECIAL,
    OPC_SUBU     = 0x23 | OPC_SPECIAL,
    OPC_AND      = 0x24 | OPC_SPECIAL,
    OPC_OR       = 0x25 | OPC_SPECIAL,
    OPC_XOR      = 0x26 | OPC_SPECIAL,
    OPC_NOR      = 0x27 | OPC_SPECIAL,
    OPC_SLT      = 0x2A | OPC_SPECIAL,
    OPC_SLTU     = 0x2B | OPC_SPECIAL,
    OPC_DADD     = 0x2C | OPC_SPECIAL,
    OPC_DADDU    = 0x2D | OPC_SPECIAL,
    OPC_DSUB     = 0x2E | OPC_SPECIAL,
    OPC_DSUBU    = 0x2F | OPC_SPECIAL,
    /* Jumps */
    OPC_JR       = 0x08 | OPC_SPECIAL, /* Also JR.HB */
    OPC_JALR     = 0x09 | OPC_SPECIAL, /* Also JALR.HB */
    /* Traps */
    OPC_TGE      = 0x30 | OPC_SPECIAL,
    OPC_TGEU     = 0x31 | OPC_SPECIAL,
    OPC_TLT      = 0x32 | OPC_SPECIAL,
    OPC_TLTU     = 0x33 | OPC_SPECIAL,
    OPC_TEQ      = 0x34 | OPC_SPECIAL,
    OPC_TNE      = 0x36 | OPC_SPECIAL,
    /* HI / LO registers load & stores */
    OPC_MFHI     = 0x10 | OPC_SPECIAL,
    OPC_MTHI     = 0x11 | OPC_SPECIAL,
    OPC_MFLO     = 0x12 | OPC_SPECIAL,
    OPC_MTLO     = 0x13 | OPC_SPECIAL,
    /* Conditional moves */
    OPC_MOVZ     = 0x0A | OPC_SPECIAL,
    OPC_MOVN     = 0x0B | OPC_SPECIAL,

    OPC_MOVCI    = 0x01 | OPC_SPECIAL,

    /* Special */
    OPC_PMON     = 0x05 | OPC_SPECIAL, /* inofficial */
    OPC_SYSCALL  = 0x0C | OPC_SPECIAL,
    OPC_BREAK    = 0x0D | OPC_SPECIAL,
    OPC_SPIM     = 0x0E | OPC_SPECIAL, /* inofficial */
    OPC_SYNC     = 0x0F | OPC_SPECIAL,

    OPC_SPECIAL15_RESERVED = 0x15 | OPC_SPECIAL,
    OPC_SPECIAL28_RESERVED = 0x28 | OPC_SPECIAL,
    OPC_SPECIAL29_RESERVED = 0x29 | OPC_SPECIAL,
    OPC_SPECIAL35_RESERVED = 0x35 | OPC_SPECIAL,
    OPC_SPECIAL37_RESERVED = 0x37 | OPC_SPECIAL,
    OPC_SPECIAL39_RESERVED = 0x39 | OPC_SPECIAL,
    OPC_SPECIAL3D_RESERVED = 0x3D | OPC_SPECIAL,
};

/* Multiplication variants of the vr54xx. */
#define MASK_MUL_VR54XX(op)   MASK_SPECIAL(op) | (op & (0x1F << 6))

enum {
    OPC_VR54XX_MULS    = (0x03 << 6) | OPC_MULT,
    OPC_VR54XX_MULSU   = (0x03 << 6) | OPC_MULTU,
    OPC_VR54XX_MACC    = (0x05 << 6) | OPC_MULT,
    OPC_VR54XX_MACCU   = (0x05 << 6) | OPC_MULTU,
    OPC_VR54XX_MSAC    = (0x07 << 6) | OPC_MULT,
    OPC_VR54XX_MSACU   = (0x07 << 6) | OPC_MULTU,
    OPC_VR54XX_MULHI   = (0x09 << 6) | OPC_MULT,
    OPC_VR54XX_MULHIU  = (0x09 << 6) | OPC_MULTU,
    OPC_VR54XX_MULSHI  = (0x0B << 6) | OPC_MULT,
    OPC_VR54XX_MULSHIU = (0x0B << 6) | OPC_MULTU,
    OPC_VR54XX_MACCHI  = (0x0D << 6) | OPC_MULT,
    OPC_VR54XX_MACCHIU = (0x0D << 6) | OPC_MULTU,
    OPC_VR54XX_MSACHI  = (0x0F << 6) | OPC_MULT,
    OPC_VR54XX_MSACHIU = (0x0F << 6) | OPC_MULTU,
};

/* REGIMM (rt field) opcodes */
#define MASK_REGIMM(op)    MASK_OP_MAJOR(op) | (op & (0x1F << 16))

enum {
    OPC_BLTZ     = (0x00 << 16) | OPC_REGIMM,
    OPC_BLTZL    = (0x02 << 16) | OPC_REGIMM,
    OPC_BGEZ     = (0x01 << 16) | OPC_REGIMM,
    OPC_BGEZL    = (0x03 << 16) | OPC_REGIMM,
    OPC_BLTZAL   = (0x10 << 16) | OPC_REGIMM,
    OPC_BLTZALL  = (0x12 << 16) | OPC_REGIMM,
    OPC_BGEZAL   = (0x11 << 16) | OPC_REGIMM,
    OPC_BGEZALL  = (0x13 << 16) | OPC_REGIMM,
    OPC_TGEI     = (0x08 << 16) | OPC_REGIMM,
    OPC_TGEIU    = (0x09 << 16) | OPC_REGIMM,
    OPC_TLTI     = (0x0A << 16) | OPC_REGIMM,
    OPC_TLTIU    = (0x0B << 16) | OPC_REGIMM,
    OPC_TEQI     = (0x0C << 16) | OPC_REGIMM,
    OPC_TNEI     = (0x0E << 16) | OPC_REGIMM,
    OPC_SYNCI    = (0x1F << 16) | OPC_REGIMM,
};

/* Special2 opcodes */
#define MASK_SPECIAL2(op)  MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    /* Multiply & xxx operations */
    OPC_MADD     = 0x00 | OPC_SPECIAL2,
    OPC_MADDU    = 0x01 | OPC_SPECIAL2,
    OPC_MUL      = 0x02 | OPC_SPECIAL2,
    OPC_MSUB     = 0x04 | OPC_SPECIAL2,
    OPC_MSUBU    = 0x05 | OPC_SPECIAL2,
    /* Misc */
    OPC_CLZ      = 0x20 | OPC_SPECIAL2,
    OPC_CLO      = 0x21 | OPC_SPECIAL2,
    OPC_DCLZ     = 0x24 | OPC_SPECIAL2,
    OPC_DCLO     = 0x25 | OPC_SPECIAL2,
    /* Special */
    OPC_SDBBP    = 0x3F | OPC_SPECIAL2,
};

/* Special3 opcodes */
#define MASK_SPECIAL3(op)  MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    OPC_EXT      = 0x00 | OPC_SPECIAL3,
    OPC_DEXTM    = 0x01 | OPC_SPECIAL3,
    OPC_DEXTU    = 0x02 | OPC_SPECIAL3,
    OPC_DEXT     = 0x03 | OPC_SPECIAL3,
    OPC_INS      = 0x04 | OPC_SPECIAL3,
    OPC_DINSM    = 0x05 | OPC_SPECIAL3,
    OPC_DINSU    = 0x06 | OPC_SPECIAL3,
    OPC_DINS     = 0x07 | OPC_SPECIAL3,
    OPC_FORK     = 0x08 | OPC_SPECIAL3,
    OPC_YIELD    = 0x09 | OPC_SPECIAL3,
    OPC_BSHFL    = 0x20 | OPC_SPECIAL3,
    OPC_DBSHFL   = 0x24 | OPC_SPECIAL3,
    OPC_RDHWR    = 0x3B | OPC_SPECIAL3,
};

/* BSHFL opcodes */
#define MASK_BSHFL(op)     MASK_SPECIAL3(op) | (op & (0x1F << 6))

enum {
    OPC_WSBH     = (0x02 << 6) | OPC_BSHFL,
    OPC_SEB      = (0x10 << 6) | OPC_BSHFL,
    OPC_SEH      = (0x18 << 6) | OPC_BSHFL,
};

/* DBSHFL opcodes */
#define MASK_DBSHFL(op)    MASK_SPECIAL3(op) | (op & (0x1F << 6))

enum {
    OPC_DSBH     = (0x02 << 6) | OPC_DBSHFL,
    OPC_DSHD     = (0x05 << 6) | OPC_DBSHFL,
};

/* Coprocessor 0 (rs field) */
#define MASK_CP0(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC0     = (0x00 << 21) | OPC_CP0,
    OPC_DMFC0    = (0x01 << 21) | OPC_CP0,
    OPC_MTC0     = (0x04 << 21) | OPC_CP0,
    OPC_DMTC0    = (0x05 << 21) | OPC_CP0,
    OPC_MFTR     = (0x08 << 21) | OPC_CP0,
    OPC_RDPGPR   = (0x0A << 21) | OPC_CP0,
    OPC_MFMC0    = (0x0B << 21) | OPC_CP0,
    OPC_MTTR     = (0x0C << 21) | OPC_CP0,
    OPC_WRPGPR   = (0x0E << 21) | OPC_CP0,
    OPC_C0       = (0x10 << 21) | OPC_CP0,
    OPC_C0_FIRST = (0x10 << 21) | OPC_CP0,
    OPC_C0_LAST  = (0x1F << 21) | OPC_CP0,
};

/* MFMC0 opcodes */
#define MASK_MFMC0(op)     MASK_CP0(op) | (op & 0xFFFF)

enum {
    OPC_DMT      = 0x01 | (0 << 5) | (0x0F << 6) | (0x01 << 11) | OPC_MFMC0,
    OPC_EMT      = 0x01 | (1 << 5) | (0x0F << 6) | (0x01 << 11) | OPC_MFMC0,
    OPC_DVPE     = 0x01 | (0 << 5) | OPC_MFMC0,
    OPC_EVPE     = 0x01 | (1 << 5) | OPC_MFMC0,
    OPC_DI       = (0 << 5) | (0x0C << 11) | OPC_MFMC0,
    OPC_EI       = (1 << 5) | (0x0C << 11) | OPC_MFMC0,
};

/* Coprocessor 0 (with rs == C0) */
#define MASK_C0(op)        MASK_CP0(op) | (op & 0x3F)

enum {
    OPC_TLBR     = 0x01 | OPC_C0,
    OPC_TLBWI    = 0x02 | OPC_C0,
    OPC_TLBWR    = 0x06 | OPC_C0,
    OPC_TLBP     = 0x08 | OPC_C0,
    OPC_RFE      = 0x10 | OPC_C0,
    OPC_ERET     = 0x18 | OPC_C0,
    OPC_DERET    = 0x1F | OPC_C0,
    OPC_WAIT     = 0x20 | OPC_C0,
};

/* Coprocessor 1 (rs field) */
#define MASK_CP1(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC1     = (0x00 << 21) | OPC_CP1,
    OPC_DMFC1    = (0x01 << 21) | OPC_CP1,
    OPC_CFC1     = (0x02 << 21) | OPC_CP1,
    OPC_MFHC1    = (0x03 << 21) | OPC_CP1,
    OPC_MTC1     = (0x04 << 21) | OPC_CP1,
    OPC_DMTC1    = (0x05 << 21) | OPC_CP1,
    OPC_CTC1     = (0x06 << 21) | OPC_CP1,
    OPC_MTHC1    = (0x07 << 21) | OPC_CP1,
    OPC_BC1      = (0x08 << 21) | OPC_CP1, /* bc */
    OPC_BC1ANY2  = (0x09 << 21) | OPC_CP1,
    OPC_BC1ANY4  = (0x0A << 21) | OPC_CP1,
    OPC_S_FMT    = (0x10 << 21) | OPC_CP1, /* 16: fmt=single fp */
    OPC_D_FMT    = (0x11 << 21) | OPC_CP1, /* 17: fmt=double fp */
    OPC_E_FMT    = (0x12 << 21) | OPC_CP1, /* 18: fmt=extended fp */
    OPC_Q_FMT    = (0x13 << 21) | OPC_CP1, /* 19: fmt=quad fp */
    OPC_W_FMT    = (0x14 << 21) | OPC_CP1, /* 20: fmt=32bit fixed */
    OPC_L_FMT    = (0x15 << 21) | OPC_CP1, /* 21: fmt=64bit fixed */
    OPC_PS_FMT   = (0x16 << 21) | OPC_CP1, /* 22: fmt=paired single fp */
};

#define MASK_CP1_FUNC(op)       MASK_CP1(op) | (op & 0x3F)
#define MASK_BC1(op)            MASK_CP1(op) | (op & (0x3 << 16))

enum {
    OPC_BC1F     = (0x00 << 16) | OPC_BC1,
    OPC_BC1T     = (0x01 << 16) | OPC_BC1,
    OPC_BC1FL    = (0x02 << 16) | OPC_BC1,
    OPC_BC1TL    = (0x03 << 16) | OPC_BC1,
};

enum {
    OPC_BC1FANY2     = (0x00 << 16) | OPC_BC1ANY2,
    OPC_BC1TANY2     = (0x01 << 16) | OPC_BC1ANY2,
};

enum {
    OPC_BC1FANY4     = (0x00 << 16) | OPC_BC1ANY4,
    OPC_BC1TANY4     = (0x01 << 16) | OPC_BC1ANY4,
};

#define MASK_CP2(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC2    = (0x00 << 21) | OPC_CP2,
    OPC_DMFC2   = (0x01 << 21) | OPC_CP2,
    OPC_CFC2    = (0x02 << 21) | OPC_CP2,
    OPC_MFHC2   = (0x03 << 21) | OPC_CP2,
    OPC_MTC2    = (0x04 << 21) | OPC_CP2,
    OPC_DMTC2   = (0x05 << 21) | OPC_CP2,
    OPC_CTC2    = (0x06 << 21) | OPC_CP2,
    OPC_MTHC2   = (0x07 << 21) | OPC_CP2,
    OPC_BC2     = (0x08 << 21) | OPC_CP2,
};

#define MASK_CP3(op)       MASK_OP_MAJOR(op) | (op & 0x3F)

enum {
    OPC_LWXC1   = 0x00 | OPC_CP3,
    OPC_LDXC1   = 0x01 | OPC_CP3,
    OPC_LUXC1   = 0x05 | OPC_CP3,
    OPC_SWXC1   = 0x08 | OPC_CP3,
    OPC_SDXC1   = 0x09 | OPC_CP3,
    OPC_SUXC1   = 0x0D | OPC_CP3,
    OPC_PREFX   = 0x0F | OPC_CP3,
    OPC_ALNV_PS = 0x1E | OPC_CP3,
    OPC_MADD_S  = 0x20 | OPC_CP3,
    OPC_MADD_D  = 0x21 | OPC_CP3,
    OPC_MADD_PS = 0x26 | OPC_CP3,
    OPC_MSUB_S  = 0x28 | OPC_CP3,
    OPC_MSUB_D  = 0x29 | OPC_CP3,
    OPC_MSUB_PS = 0x2E | OPC_CP3,
    OPC_NMADD_S = 0x30 | OPC_CP3,
    OPC_NMADD_D = 0x31 | OPC_CP3,
    OPC_NMADD_PS= 0x36 | OPC_CP3,
    OPC_NMSUB_S = 0x38 | OPC_CP3,
    OPC_NMSUB_D = 0x39 | OPC_CP3,
    OPC_NMSUB_PS= 0x3E | OPC_CP3,
};

/* global register indices */
static TCGv_ptr cpu_env;
static TCGv cpu_gpr[32], cpu_PC;
static TCGv cpu_HI[MIPS_DSP_ACC], cpu_LO[MIPS_DSP_ACC], cpu_ACX[MIPS_DSP_ACC];
static TCGv cpu_dspctrl, btarget;
static TCGv_i32 bcond;
static TCGv_i32 fpu_fpr32[32], fpu_fpr32h[32];
static TCGv_i64 fpu_fpr64[32];
static TCGv_i32 fpu_fcr0, fpu_fcr31;

#include "gen-icount.h"

#define gen_helper_0i(name, arg) do {                             \
    TCGv_i32 helper_tmp = tcg_const_i32(arg);                     \
    gen_helper_##name(helper_tmp);                                \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_1i(name, arg1, arg2) do {                      \
    TCGv_i32 helper_tmp = tcg_const_i32(arg2);                    \
    gen_helper_##name(arg1, helper_tmp);                          \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_2i(name, arg1, arg2, arg3) do {                \
    TCGv_i32 helper_tmp = tcg_const_i32(arg3);                    \
    gen_helper_##name(arg1, arg2, helper_tmp);                    \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_3i(name, arg1, arg2, arg3, arg4) do {          \
    TCGv_i32 helper_tmp = tcg_const_i32(arg4);                    \
    gen_helper_##name(arg1, arg2, arg3, helper_tmp);              \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc, saved_pc;
    uint32_t opcode;
    /* Routine used to access memory */
    int mem_idx;
    uint32_t hflags, saved_hflags;
    int bstate;
    target_ulong btarget;
} DisasContext;

enum {
    BS_NONE     = 0, /* We go out of the TB without reaching a branch or an
                      * exception condition */
    BS_STOP     = 1, /* We want to stop translation for any reason */
    BS_BRANCH   = 2, /* We reached a branch condition     */
    BS_EXCP     = 3, /* We reached an exception condition */
};

static const char *regnames[] =
    { "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
      "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
      "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
      "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra", };

static const char *regnames_HI[] =
    { "HI0", "HI1", "HI2", "HI3", };

static const char *regnames_LO[] =
    { "LO0", "LO1", "LO2", "LO3", };

static const char *regnames_ACX[] =
    { "ACX0", "ACX1", "ACX2", "ACX3", };

static const char *fregnames[] =
    { "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
      "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
      "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
      "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31", };

static const char *fregnames_64[] =
    { "F0",  "F1",  "F2",  "F3",  "F4",  "F5",  "F6",  "F7",
      "F8",  "F9",  "F10", "F11", "F12", "F13", "F14", "F15",
      "F16", "F17", "F18", "F19", "F20", "F21", "F22", "F23",
      "F24", "F25", "F26", "F27", "F28", "F29", "F30", "F31", };

static const char *fregnames_h[] =
    { "h0",  "h1",  "h2",  "h3",  "h4",  "h5",  "h6",  "h7",
      "h8",  "h9",  "h10", "h11", "h12", "h13", "h14", "h15",
      "h16", "h17", "h18", "h19", "h20", "h21", "h22", "h23",
      "h24", "h25", "h26", "h27", "h28", "h29", "h30", "h31", };

#ifdef MIPS_DEBUG_DISAS
#define MIPS_DEBUG(fmt, args...)                                              \
do {                                                                          \
    if (loglevel & CPU_LOG_TB_IN_ASM) {                                       \
        fprintf(logfile, TARGET_FMT_lx ": %08x " fmt "\n",                    \
                ctx->pc, ctx->opcode , ##args);                               \
    }                                                                         \
} while (0)
#else
#define MIPS_DEBUG(fmt, args...) do { } while(0)
#endif

#define MIPS_INVAL(op)                                                        \
do {                                                                          \
    MIPS_DEBUG("Invalid %s %03x %03x %03x", op, ctx->opcode >> 26,            \
               ctx->opcode & 0x3F, ((ctx->opcode >> 16) & 0x1F));             \
} while (0)

/* General purpose registers moves. */
static inline void gen_load_gpr (TCGv t, int reg)
{
    if (reg == 0)
        tcg_gen_movi_tl(t, 0);
    else
        tcg_gen_mov_tl(t, cpu_gpr[reg]);
}

static inline void gen_store_gpr (TCGv t, int reg)
{
    if (reg != 0)
        tcg_gen_mov_tl(cpu_gpr[reg], t);
}

/* Moves to/from ACX register.  */
static inline void gen_load_ACX (TCGv t, int reg)
{
    tcg_gen_mov_tl(t, cpu_ACX[reg]);
}

static inline void gen_store_ACX (TCGv t, int reg)
{
    tcg_gen_mov_tl(cpu_ACX[reg], t);
}

/* Moves to/from shadow registers. */
static inline void gen_load_srsgpr (int from, int to)
{
    TCGv r_tmp1 = tcg_temp_new();

    if (from == 0)
        tcg_gen_movi_tl(r_tmp1, 0);
    else {
        TCGv_i32 r_tmp2 = tcg_temp_new_i32();
        TCGv_ptr addr = tcg_temp_new_ptr();

        tcg_gen_ld_i32(r_tmp2, cpu_env, offsetof(CPUState, CP0_SRSCtl));
        tcg_gen_shri_i32(r_tmp2, r_tmp2, CP0SRSCtl_PSS);
        tcg_gen_andi_i32(r_tmp2, r_tmp2, 0xf);
        tcg_gen_muli_i32(r_tmp2, r_tmp2, sizeof(target_ulong) * 32);
        tcg_gen_ext_i32_ptr(addr, r_tmp2);
        tcg_gen_add_ptr(addr, cpu_env, addr);

        tcg_gen_ld_tl(r_tmp1, addr, sizeof(target_ulong) * from);
        tcg_temp_free_ptr(addr);
        tcg_temp_free_i32(r_tmp2);
    }
    gen_store_gpr(r_tmp1, to);
    tcg_temp_free(r_tmp1);
}

static inline void gen_store_srsgpr (int from, int to)
{
    if (to != 0) {
        TCGv r_tmp1 = tcg_temp_new();
        TCGv_i32 r_tmp2 = tcg_temp_new_i32();
        TCGv_ptr addr = tcg_temp_new_ptr();

        gen_load_gpr(r_tmp1, from);
        tcg_gen_ld_i32(r_tmp2, cpu_env, offsetof(CPUState, CP0_SRSCtl));
        tcg_gen_shri_i32(r_tmp2, r_tmp2, CP0SRSCtl_PSS);
        tcg_gen_andi_i32(r_tmp2, r_tmp2, 0xf);
        tcg_gen_muli_i32(r_tmp2, r_tmp2, sizeof(target_ulong) * 32);
        tcg_gen_ext_i32_ptr(addr, r_tmp2);
        tcg_gen_add_ptr(addr, cpu_env, addr);

        tcg_gen_st_tl(r_tmp1, addr, sizeof(target_ulong) * to);
        tcg_temp_free_ptr(addr);
        tcg_temp_free_i32(r_tmp2);
        tcg_temp_free(r_tmp1);
    }
}

/* Floating point register moves. */
static inline void gen_load_fpr32 (TCGv_i32 t, int reg)
{
    tcg_gen_mov_i32(t, fpu_fpr32[reg]);
}

static inline void gen_store_fpr32 (TCGv_i32 t, int reg)
{
    tcg_gen_mov_i32(fpu_fpr32[reg], t);
}

static inline void gen_load_fpr64 (DisasContext *ctx, TCGv_i64 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_F64)
        tcg_gen_mov_i64(t, fpu_fpr64[reg]);
    else {
        tcg_gen_concat_i32_i64(t, fpu_fpr32[reg & ~1], fpu_fpr32[reg | 1]);
    }
}

static inline void gen_store_fpr64 (DisasContext *ctx, TCGv_i64 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_F64)
        tcg_gen_mov_i64(fpu_fpr64[reg], t);
    else {
        tcg_gen_trunc_i64_i32(fpu_fpr32[reg & ~1], t);
        tcg_gen_shri_i64(t, t, 32);
        tcg_gen_trunc_i64_i32(fpu_fpr32[reg | 1], t);
    }
}

static inline void gen_load_fpr32h (TCGv_i32 t, int reg)
{
    tcg_gen_mov_i32(t, fpu_fpr32h[reg]);
}

static inline void gen_store_fpr32h (TCGv_i32 t, int reg)
{
    tcg_gen_mov_i32(fpu_fpr32h[reg], t);
}

static inline void get_fp_cond (TCGv_i32 t)
{
    TCGv_i32 r_tmp1 = tcg_temp_new_i32();
    TCGv_i32 r_tmp2 = tcg_temp_new_i32();

    tcg_gen_shri_i32(r_tmp2, fpu_fcr31, 24);
    tcg_gen_andi_i32(r_tmp2, r_tmp2, 0xfe);
    tcg_gen_shri_i32(r_tmp1, fpu_fcr31, 23);
    tcg_gen_andi_i32(r_tmp1, r_tmp1, 0x1);
    tcg_gen_or_i32(t, r_tmp1, r_tmp2);
    tcg_temp_free_i32(r_tmp1);
    tcg_temp_free_i32(r_tmp2);
}

#define FOP_CONDS(type, fmt, bits)                                            \
static inline void gen_cmp ## type ## _ ## fmt(int n, TCGv_i##bits a,         \
                                               TCGv_i##bits b, int cc)        \
{                                                                             \
    switch (n) {                                                              \
    case  0: gen_helper_2i(cmp ## type ## _ ## fmt ## _f, a, b, cc);    break;\
    case  1: gen_helper_2i(cmp ## type ## _ ## fmt ## _un, a, b, cc);   break;\
    case  2: gen_helper_2i(cmp ## type ## _ ## fmt ## _eq, a, b, cc);   break;\
    case  3: gen_helper_2i(cmp ## type ## _ ## fmt ## _ueq, a, b, cc);  break;\
    case  4: gen_helper_2i(cmp ## type ## _ ## fmt ## _olt, a, b, cc);  break;\
    case  5: gen_helper_2i(cmp ## type ## _ ## fmt ## _ult, a, b, cc);  break;\
    case  6: gen_helper_2i(cmp ## type ## _ ## fmt ## _ole, a, b, cc);  break;\
    case  7: gen_helper_2i(cmp ## type ## _ ## fmt ## _ule, a, b, cc);  break;\
    case  8: gen_helper_2i(cmp ## type ## _ ## fmt ## _sf, a, b, cc);   break;\
    case  9: gen_helper_2i(cmp ## type ## _ ## fmt ## _ngle, a, b, cc); break;\
    case 10: gen_helper_2i(cmp ## type ## _ ## fmt ## _seq, a, b, cc);  break;\
    case 11: gen_helper_2i(cmp ## type ## _ ## fmt ## _ngl, a, b, cc);  break;\
    case 12: gen_helper_2i(cmp ## type ## _ ## fmt ## _lt, a, b, cc);   break;\
    case 13: gen_helper_2i(cmp ## type ## _ ## fmt ## _nge, a, b, cc);  break;\
    case 14: gen_helper_2i(cmp ## type ## _ ## fmt ## _le, a, b, cc);   break;\
    case 15: gen_helper_2i(cmp ## type ## _ ## fmt ## _ngt, a, b, cc);  break;\
    default: abort();                                                         \
    }                                                                         \
}

FOP_CONDS(, d, 64)
FOP_CONDS(abs, d, 64)
FOP_CONDS(, s, 32)
FOP_CONDS(abs, s, 32)
FOP_CONDS(, ps, 64)
FOP_CONDS(abs, ps, 64)
#undef FOP_CONDS

/* Tests */
#define OP_COND(name, cond)                                   \
static inline void glue(gen_op_, name) (TCGv t0, TCGv t1)     \
{                                                             \
    int l1 = gen_new_label();                                 \
    int l2 = gen_new_label();                                 \
                                                              \
    tcg_gen_brcond_tl(cond, t0, t1, l1);                      \
    tcg_gen_movi_tl(t0, 0);                                   \
    tcg_gen_br(l2);                                           \
    gen_set_label(l1);                                        \
    tcg_gen_movi_tl(t0, 1);                                   \
    gen_set_label(l2);                                        \
}
OP_COND(eq, TCG_COND_EQ);
OP_COND(ne, TCG_COND_NE);
OP_COND(ge, TCG_COND_GE);
OP_COND(geu, TCG_COND_GEU);
OP_COND(lt, TCG_COND_LT);
OP_COND(ltu, TCG_COND_LTU);
#undef OP_COND

#define OP_CONDI(name, cond)                                  \
static inline void glue(gen_op_, name) (TCGv t, target_ulong val) \
{                                                             \
    int l1 = gen_new_label();                                 \
    int l2 = gen_new_label();                                 \
                                                              \
    tcg_gen_brcondi_tl(cond, t, val, l1);                     \
    tcg_gen_movi_tl(t, 0);                                    \
    tcg_gen_br(l2);                                           \
    gen_set_label(l1);                                        \
    tcg_gen_movi_tl(t, 1);                                    \
    gen_set_label(l2);                                        \
}
OP_CONDI(lti, TCG_COND_LT);
OP_CONDI(ltiu, TCG_COND_LTU);
#undef OP_CONDI

#define OP_CONDZ(name, cond)                                  \
static inline void glue(gen_op_, name) (TCGv t)               \
{                                                             \
    int l1 = gen_new_label();                                 \
    int l2 = gen_new_label();                                 \
                                                              \
    tcg_gen_brcondi_tl(cond, t, 0, l1);                       \
    tcg_gen_movi_tl(t, 0);                                    \
    tcg_gen_br(l2);                                           \
    gen_set_label(l1);                                        \
    tcg_gen_movi_tl(t, 1);                                    \
    gen_set_label(l2);                                        \
}
OP_CONDZ(gez, TCG_COND_GE);
OP_CONDZ(gtz, TCG_COND_GT);
OP_CONDZ(lez, TCG_COND_LE);
OP_CONDZ(ltz, TCG_COND_LT);
#undef OP_CONDZ

static inline void gen_save_pc(target_ulong pc)
{
    tcg_gen_movi_tl(cpu_PC, pc);
}

static inline void save_cpu_state (DisasContext *ctx, int do_save_pc)
{
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile, "hflags %08x saved %08x\n",
                    ctx->hflags, ctx->saved_hflags);
    }
#endif
    if (do_save_pc && ctx->pc != ctx->saved_pc) {
        gen_save_pc(ctx->pc);
        ctx->saved_pc = ctx->pc;
    }
    if (ctx->hflags != ctx->saved_hflags) {
        TCGv_i32 r_tmp = tcg_temp_new_i32();

        tcg_gen_movi_i32(r_tmp, ctx->hflags);
        tcg_gen_st_i32(r_tmp, cpu_env, offsetof(CPUState, hflags));
        tcg_temp_free_i32(r_tmp);
        ctx->saved_hflags = ctx->hflags;
        switch (ctx->hflags & MIPS_HFLAG_BMASK) {
        case MIPS_HFLAG_BR:
            break;
        case MIPS_HFLAG_BC:
        case MIPS_HFLAG_BL:
        case MIPS_HFLAG_B:
            tcg_gen_movi_tl(btarget, ctx->btarget);
            break;
        }
    }
}

static inline void restore_cpu_state (CPUState *env, DisasContext *ctx)
{
    ctx->saved_hflags = ctx->hflags;
    switch (ctx->hflags & MIPS_HFLAG_BMASK) {
    case MIPS_HFLAG_BR:
        break;
    case MIPS_HFLAG_BC:
    case MIPS_HFLAG_BL:
    case MIPS_HFLAG_B:
        ctx->btarget = env->btarget;
        break;
    }
}

static inline void
generate_exception_err (DisasContext *ctx, int excp, int err)
{
    TCGv_i32 texcp = tcg_const_i32(excp);
    TCGv_i32 terr = tcg_const_i32(err);
    save_cpu_state(ctx, 1);
    gen_helper_raise_exception_err(texcp, terr);
    tcg_temp_free_i32(terr);
    tcg_temp_free_i32(texcp);
    gen_helper_interrupt_restart();
    tcg_gen_exit_tb(0);
}

static inline void
generate_exception (DisasContext *ctx, int excp)
{
    save_cpu_state(ctx, 1);
    gen_helper_0i(raise_exception, excp);
    gen_helper_interrupt_restart();
    tcg_gen_exit_tb(0);
}

/* Addresses computation */
static inline void gen_op_addr_add (DisasContext *ctx, TCGv t0, TCGv t1)
{
    tcg_gen_add_tl(t0, t0, t1);

#if defined(TARGET_MIPS64)
    /* For compatibility with 32-bit code, data reference in user mode
       with Status_UX = 0 should be casted to 32-bit and sign extended.
       See the MIPS64 PRA manual, section 4.10. */
    if (((ctx->hflags & MIPS_HFLAG_KSU) == MIPS_HFLAG_UM) &&
        !(ctx->hflags & MIPS_HFLAG_UX)) {
        tcg_gen_ext32s_i64(t0, t0);
    }
#endif
}

static inline void check_cp0_enabled(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_CP0)))
        generate_exception_err(ctx, EXCP_CpU, 1);
}

static inline void check_cp1_enabled(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_FPU)))
        generate_exception_err(ctx, EXCP_CpU, 1);
}

/* Verify that the processor is running with COP1X instructions enabled.
   This is associated with the nabla symbol in the MIPS32 and MIPS64
   opcode tables.  */

static inline void check_cop1x(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_COP1X)))
        generate_exception(ctx, EXCP_RI);
}

/* Verify that the processor is running with 64-bit floating-point
   operations enabled.  */

static inline void check_cp1_64bitmode(DisasContext *ctx)
{
    if (unlikely(~ctx->hflags & (MIPS_HFLAG_F64 | MIPS_HFLAG_COP1X)))
        generate_exception(ctx, EXCP_RI);
}

/*
 * Verify if floating point register is valid; an operation is not defined
 * if bit 0 of any register specification is set and the FR bit in the
 * Status register equals zero, since the register numbers specify an
 * even-odd pair of adjacent coprocessor general registers. When the FR bit
 * in the Status register equals one, both even and odd register numbers
 * are valid. This limitation exists only for 64 bit wide (d,l,ps) registers.
 *
 * Multiple 64 bit wide registers can be checked by calling
 * gen_op_cp1_registers(freg1 | freg2 | ... | fregN);
 */
static inline void check_cp1_registers(DisasContext *ctx, int regs)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_F64) && (regs & 1)))
        generate_exception(ctx, EXCP_RI);
}

/* This code generates a "reserved instruction" exception if the
   CPU does not support the instruction set corresponding to flags. */
static inline void check_insn(CPUState *env, DisasContext *ctx, int flags)
{
    if (unlikely(!(env->insn_flags & flags)))
        generate_exception(ctx, EXCP_RI);
}

/* This code generates a "reserved instruction" exception if 64-bit
   instructions are not enabled. */
static inline void check_mips_64(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_64)))
        generate_exception(ctx, EXCP_RI);
}

/* load/store instructions. */
#define OP_LD(insn,fname)                                        \
static inline void op_ldst_##insn(TCGv t0, DisasContext *ctx)    \
{                                                                \
    tcg_gen_qemu_##fname(t0, t0, ctx->mem_idx);                  \
}
OP_LD(lb,ld8s);
OP_LD(lbu,ld8u);
OP_LD(lh,ld16s);
OP_LD(lhu,ld16u);
OP_LD(lw,ld32s);
#if defined(TARGET_MIPS64)
OP_LD(lwu,ld32u);
OP_LD(ld,ld64);
#endif
#undef OP_LD

#define OP_ST(insn,fname)                                        \
static inline void op_ldst_##insn(TCGv t0, TCGv t1, DisasContext *ctx) \
{                                                                \
    tcg_gen_qemu_##fname(t1, t0, ctx->mem_idx);                  \
}
OP_ST(sb,st8);
OP_ST(sh,st16);
OP_ST(sw,st32);
#if defined(TARGET_MIPS64)
OP_ST(sd,st64);
#endif
#undef OP_ST

#define OP_LD_ATOMIC(insn,fname)                                        \
static inline void op_ldst_##insn(TCGv t0, TCGv t1, DisasContext *ctx)  \
{                                                                       \
    tcg_gen_mov_tl(t1, t0);                                             \
    tcg_gen_qemu_##fname(t0, t0, ctx->mem_idx);                         \
    tcg_gen_st_tl(t1, cpu_env, offsetof(CPUState, CP0_LLAddr));         \
}
OP_LD_ATOMIC(ll,ld32s);
#if defined(TARGET_MIPS64)
OP_LD_ATOMIC(lld,ld64);
#endif
#undef OP_LD_ATOMIC

#define OP_ST_ATOMIC(insn,fname,almask)                                 \
static inline void op_ldst_##insn(TCGv t0, TCGv t1, DisasContext *ctx)  \
{                                                                       \
    TCGv r_tmp = tcg_temp_local_new();                       \
    int l1 = gen_new_label();                                           \
    int l2 = gen_new_label();                                           \
    int l3 = gen_new_label();                                           \
                                                                        \
    tcg_gen_andi_tl(r_tmp, t0, almask);                                 \
    tcg_gen_brcondi_tl(TCG_COND_EQ, r_tmp, 0, l1);                      \
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, CP0_BadVAddr));       \
    generate_exception(ctx, EXCP_AdES);                                 \
    gen_set_label(l1);                                                  \
    tcg_gen_ld_tl(r_tmp, cpu_env, offsetof(CPUState, CP0_LLAddr));      \
    tcg_gen_brcond_tl(TCG_COND_NE, t0, r_tmp, l2);                      \
    tcg_gen_qemu_##fname(t1, t0, ctx->mem_idx);                         \
    tcg_gen_movi_tl(t0, 1);                                             \
    tcg_gen_br(l3);                                                     \
    gen_set_label(l2);                                                  \
    tcg_gen_movi_tl(t0, 0);                                             \
    gen_set_label(l3);                                                  \
    tcg_temp_free(r_tmp);                                               \
}
OP_ST_ATOMIC(sc,st32,0x3);
#if defined(TARGET_MIPS64)
OP_ST_ATOMIC(scd,st64,0x7);
#endif
#undef OP_ST_ATOMIC

/* Load and store */
static void gen_ldst (DisasContext *ctx, uint32_t opc, int rt,
                      int base, int16_t offset)
{
    const char *opn = "ldst";
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    if (base == 0) {
        tcg_gen_movi_tl(t0, offset);
    } else if (offset == 0) {
        gen_load_gpr(t0, base);
    } else {
        gen_load_gpr(t0, base);
        tcg_gen_movi_tl(t1, offset);
        gen_op_addr_add(ctx, t0, t1);
    }
    /* Don't do NOP if destination is zero: we must perform the actual
       memory access. */
    switch (opc) {
#if defined(TARGET_MIPS64)
    case OPC_LWU:
        op_ldst_lwu(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "lwu";
        break;
    case OPC_LD:
        op_ldst_ld(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "ld";
        break;
    case OPC_LLD:
        op_ldst_lld(t0, t1, ctx);
        gen_store_gpr(t0, rt);
        opn = "lld";
        break;
    case OPC_SD:
        gen_load_gpr(t1, rt);
        op_ldst_sd(t0, t1, ctx);
        opn = "sd";
        break;
    case OPC_SCD:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        op_ldst_scd(t0, t1, ctx);
        gen_store_gpr(t0, rt);
        opn = "scd";
        break;
    case OPC_LDL:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        gen_helper_3i(ldl, t1, t0, t1, ctx->mem_idx);
        gen_store_gpr(t1, rt);
        opn = "ldl";
        break;
    case OPC_SDL:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        gen_helper_2i(sdl, t0, t1, ctx->mem_idx);
        opn = "sdl";
        break;
    case OPC_LDR:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        gen_helper_3i(ldr, t1, t0, t1, ctx->mem_idx);
        gen_store_gpr(t1, rt);
        opn = "ldr";
        break;
    case OPC_SDR:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        gen_helper_2i(sdr, t0, t1, ctx->mem_idx);
        opn = "sdr";
        break;
#endif
    case OPC_LW:
        op_ldst_lw(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "lw";
        break;
    case OPC_SW:
        gen_load_gpr(t1, rt);
        op_ldst_sw(t0, t1, ctx);
        opn = "sw";
        break;
    case OPC_LH:
        op_ldst_lh(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "lh";
        break;
    case OPC_SH:
        gen_load_gpr(t1, rt);
        op_ldst_sh(t0, t1, ctx);
        opn = "sh";
        break;
    case OPC_LHU:
        op_ldst_lhu(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "lhu";
        break;
    case OPC_LB:
        op_ldst_lb(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "lb";
        break;
    case OPC_SB:
        gen_load_gpr(t1, rt);
        op_ldst_sb(t0, t1, ctx);
        opn = "sb";
        break;
    case OPC_LBU:
        op_ldst_lbu(t0, ctx);
        gen_store_gpr(t0, rt);
        opn = "lbu";
        break;
    case OPC_LWL:
        save_cpu_state(ctx, 1);
	gen_load_gpr(t1, rt);
        gen_helper_3i(lwl, t1, t0, t1, ctx->mem_idx);
        gen_store_gpr(t1, rt);
        opn = "lwl";
        break;
    case OPC_SWL:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        gen_helper_2i(swl, t0, t1, ctx->mem_idx);
        opn = "swr";
        break;
    case OPC_LWR:
        save_cpu_state(ctx, 1);
	gen_load_gpr(t1, rt);
        gen_helper_3i(lwr, t1, t0, t1, ctx->mem_idx);
        gen_store_gpr(t1, rt);
        opn = "lwr";
        break;
    case OPC_SWR:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        gen_helper_2i(swr, t0, t1, ctx->mem_idx);
        opn = "swr";
        break;
    case OPC_LL:
        op_ldst_ll(t0, t1, ctx);
        gen_store_gpr(t0, rt);
        opn = "ll";
        break;
    case OPC_SC:
        save_cpu_state(ctx, 1);
        gen_load_gpr(t1, rt);
        op_ldst_sc(t0, t1, ctx);
        gen_store_gpr(t0, rt);
        opn = "sc";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    MIPS_DEBUG("%s %s, %d(%s)", opn, regnames[rt], offset, regnames[base]);
 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* Load and store */
static void gen_flt_ldst (DisasContext *ctx, uint32_t opc, int ft,
                          int base, int16_t offset)
{
    const char *opn = "flt_ldst";
    TCGv t0 = tcg_temp_local_new();

    if (base == 0) {
        tcg_gen_movi_tl(t0, offset);
    } else if (offset == 0) {
        gen_load_gpr(t0, base);
    } else {
        TCGv t1 = tcg_temp_local_new();

        gen_load_gpr(t0, base);
        tcg_gen_movi_tl(t1, offset);
        gen_op_addr_add(ctx, t0, t1);
        tcg_temp_free(t1);
    }
    /* Don't do NOP if destination is zero: we must perform the actual
       memory access. */
    switch (opc) {
    case OPC_LWC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv t1 = tcg_temp_new();

            tcg_gen_qemu_ld32s(t1, t0, ctx->mem_idx);
            tcg_gen_trunc_tl_i32(fp0, t1);
            gen_store_fpr32(fp0, ft);
            tcg_temp_free(t1);
            tcg_temp_free_i32(fp0);
        }
        opn = "lwc1";
        break;
    case OPC_SWC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv t1 = tcg_temp_new();

            gen_load_fpr32(fp0, ft);
            tcg_gen_extu_i32_tl(t1, fp0);
            tcg_gen_qemu_st32(t1, t0, ctx->mem_idx);
            tcg_temp_free(t1);
            tcg_temp_free_i32(fp0);
        }
        opn = "swc1";
        break;
    case OPC_LDC1:
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            tcg_gen_qemu_ld64(fp0, t0, ctx->mem_idx);
            gen_store_fpr64(ctx, fp0, ft);
            tcg_temp_free_i64(fp0);
        }
        opn = "ldc1";
        break;
    case OPC_SDC1:
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, ft);
            tcg_gen_qemu_st64(fp0, t0, ctx->mem_idx);
            tcg_temp_free_i64(fp0);
        }
        opn = "sdc1";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    MIPS_DEBUG("%s %s, %d(%s)", opn, fregnames[ft], offset, regnames[base]);
 out:
    tcg_temp_free(t0);
}

/* Arithmetic with immediate operand */
static void gen_arith_imm (CPUState *env, DisasContext *ctx, uint32_t opc,
                           int rt, int rs, int16_t imm)
{
    target_ulong uimm;
    const char *opn = "imm arith";
    TCGv t0 = tcg_temp_local_new();

    if (rt == 0 && opc != OPC_ADDI && opc != OPC_DADDI) {
        /* If no destination, treat it as a NOP.
           For addi, we must generate the overflow exception when needed. */
        MIPS_DEBUG("NOP");
        goto out;
    }
    uimm = (uint16_t)imm;
    switch (opc) {
    case OPC_ADDI:
    case OPC_ADDIU:
#if defined(TARGET_MIPS64)
    case OPC_DADDI:
    case OPC_DADDIU:
#endif
    case OPC_SLTI:
    case OPC_SLTIU:
        uimm = (target_long)imm; /* Sign extend to 32/64 bits */
        /* Fall through. */
    case OPC_ANDI:
    case OPC_ORI:
    case OPC_XORI:
        gen_load_gpr(t0, rs);
        break;
    case OPC_LUI:
        tcg_gen_movi_tl(t0, imm << 16);
        break;
    case OPC_SLL:
    case OPC_SRA:
    case OPC_SRL:
#if defined(TARGET_MIPS64)
    case OPC_DSLL:
    case OPC_DSRA:
    case OPC_DSRL:
    case OPC_DSLL32:
    case OPC_DSRA32:
    case OPC_DSRL32:
#endif
        uimm &= 0x1f;
        gen_load_gpr(t0, rs);
        break;
    }
    switch (opc) {
    case OPC_ADDI:
        {
            TCGv r_tmp1 = tcg_temp_new();
            TCGv r_tmp2 = tcg_temp_new();
            int l1 = gen_new_label();

            save_cpu_state(ctx, 1);
            tcg_gen_ext32s_tl(r_tmp1, t0);
            tcg_gen_addi_tl(t0, r_tmp1, uimm);

            tcg_gen_xori_tl(r_tmp1, r_tmp1, ~uimm);
            tcg_gen_xori_tl(r_tmp2, t0, uimm);
            tcg_gen_and_tl(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free(r_tmp2);
            tcg_gen_brcondi_tl(TCG_COND_GE, r_tmp1, 0, l1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_temp_free(r_tmp1);

            tcg_gen_ext32s_tl(t0, t0);
        }
        opn = "addi";
        break;
    case OPC_ADDIU:
        tcg_gen_addi_tl(t0, t0, uimm);
        tcg_gen_ext32s_tl(t0, t0);
        opn = "addiu";
        break;
#if defined(TARGET_MIPS64)
    case OPC_DADDI:
        {
            TCGv r_tmp1 = tcg_temp_new();
            TCGv r_tmp2 = tcg_temp_new();
            int l1 = gen_new_label();

            save_cpu_state(ctx, 1);
            tcg_gen_mov_tl(r_tmp1, t0);
            tcg_gen_addi_tl(t0, t0, uimm);

            tcg_gen_xori_tl(r_tmp1, r_tmp1, ~uimm);
            tcg_gen_xori_tl(r_tmp2, t0, uimm);
            tcg_gen_and_tl(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free(r_tmp2);
            tcg_gen_brcondi_tl(TCG_COND_GE, r_tmp1, 0, l1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_temp_free(r_tmp1);
        }
        opn = "daddi";
        break;
    case OPC_DADDIU:
        tcg_gen_addi_tl(t0, t0, uimm);
        opn = "daddiu";
        break;
#endif
    case OPC_SLTI:
        gen_op_lti(t0, uimm);
        opn = "slti";
        break;
    case OPC_SLTIU:
        gen_op_ltiu(t0, uimm);
        opn = "sltiu";
        break;
    case OPC_ANDI:
        tcg_gen_andi_tl(t0, t0, uimm);
        opn = "andi";
        break;
    case OPC_ORI:
        tcg_gen_ori_tl(t0, t0, uimm);
        opn = "ori";
        break;
    case OPC_XORI:
        tcg_gen_xori_tl(t0, t0, uimm);
        opn = "xori";
        break;
    case OPC_LUI:
        opn = "lui";
        break;
    case OPC_SLL:
        tcg_gen_shli_tl(t0, t0, uimm);
        tcg_gen_ext32s_tl(t0, t0);
        opn = "sll";
        break;
    case OPC_SRA:
        tcg_gen_ext32s_tl(t0, t0);
        tcg_gen_sari_tl(t0, t0, uimm);
        opn = "sra";
        break;
    case OPC_SRL:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 0:
            if (uimm != 0) {
                tcg_gen_ext32u_tl(t0, t0);
                tcg_gen_shri_tl(t0, t0, uimm);
            } else {
                tcg_gen_ext32s_tl(t0, t0);
            }
            opn = "srl";
            break;
        case 1:
            /* rotr is decoded as srl on non-R2 CPUs */
            if (env->insn_flags & ISA_MIPS32R2) {
                if (uimm != 0) {
                    TCGv_i32 r_tmp1 = tcg_temp_new_i32();

                    tcg_gen_trunc_tl_i32(r_tmp1, t0);
                    tcg_gen_rotri_i32(r_tmp1, r_tmp1, uimm);
                    tcg_gen_ext_i32_tl(t0, r_tmp1);
                    tcg_temp_free_i32(r_tmp1);
                }
                opn = "rotr";
            } else {
                if (uimm != 0) {
                    tcg_gen_ext32u_tl(t0, t0);
                    tcg_gen_shri_tl(t0, t0, uimm);
                } else {
                    tcg_gen_ext32s_tl(t0, t0);
                }
                opn = "srl";
            }
            break;
        default:
            MIPS_INVAL("invalid srl flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DSLL:
        tcg_gen_shli_tl(t0, t0, uimm);
        opn = "dsll";
        break;
    case OPC_DSRA:
        tcg_gen_sari_tl(t0, t0, uimm);
        opn = "dsra";
        break;
    case OPC_DSRL:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 0:
            tcg_gen_shri_tl(t0, t0, uimm);
            opn = "dsrl";
            break;
        case 1:
            /* drotr is decoded as dsrl on non-R2 CPUs */
            if (env->insn_flags & ISA_MIPS32R2) {
                if (uimm != 0) {
                    tcg_gen_rotri_tl(t0, t0, uimm);
                }
                opn = "drotr";
            } else {
                tcg_gen_shri_tl(t0, t0, uimm);
                opn = "dsrl";
            }
            break;
        default:
            MIPS_INVAL("invalid dsrl flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DSLL32:
        tcg_gen_shli_tl(t0, t0, uimm + 32);
        opn = "dsll32";
        break;
    case OPC_DSRA32:
        tcg_gen_sari_tl(t0, t0, uimm + 32);
        opn = "dsra32";
        break;
    case OPC_DSRL32:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 0:
            tcg_gen_shri_tl(t0, t0, uimm + 32);
            opn = "dsrl32";
            break;
        case 1:
            /* drotr32 is decoded as dsrl32 on non-R2 CPUs */
            if (env->insn_flags & ISA_MIPS32R2) {
                tcg_gen_rotri_tl(t0, t0, uimm + 32);
                opn = "drotr32";
            } else {
                tcg_gen_shri_tl(t0, t0, uimm + 32);
                opn = "dsrl32";
            }
            break;
        default:
            MIPS_INVAL("invalid dsrl32 flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    gen_store_gpr(t0, rt);
    MIPS_DEBUG("%s %s, %s, " TARGET_FMT_lx, opn, regnames[rt], regnames[rs], uimm);
 out:
    tcg_temp_free(t0);
}

/* Arithmetic */
static void gen_arith (CPUState *env, DisasContext *ctx, uint32_t opc,
                       int rd, int rs, int rt)
{
    const char *opn = "arith";
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    if (rd == 0 && opc != OPC_ADD && opc != OPC_SUB
       && opc != OPC_DADD && opc != OPC_DSUB) {
        /* If no destination, treat it as a NOP.
           For add & sub, we must generate the overflow exception when needed. */
        MIPS_DEBUG("NOP");
        goto out;
    }
    gen_load_gpr(t0, rs);
    /* Specialcase the conventional move operation. */
    if (rt == 0 && (opc == OPC_ADDU || opc == OPC_DADDU
                    || opc == OPC_SUBU || opc == OPC_DSUBU)) {
        gen_store_gpr(t0, rd);
        goto out;
    }
    gen_load_gpr(t1, rt);
    switch (opc) {
    case OPC_ADD:
        {
            TCGv r_tmp1 = tcg_temp_new();
            TCGv r_tmp2 = tcg_temp_new();
            int l1 = gen_new_label();

            save_cpu_state(ctx, 1);
            tcg_gen_ext32s_tl(r_tmp1, t0);
            tcg_gen_ext32s_tl(r_tmp2, t1);
            tcg_gen_add_tl(t0, r_tmp1, r_tmp2);

            tcg_gen_xor_tl(r_tmp1, r_tmp1, t1);
            tcg_gen_xori_tl(r_tmp1, r_tmp1, -1);
            tcg_gen_xor_tl(r_tmp2, t0, t1);
            tcg_gen_and_tl(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free(r_tmp2);
            tcg_gen_brcondi_tl(TCG_COND_GE, r_tmp1, 0, l1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_temp_free(r_tmp1);

            tcg_gen_ext32s_tl(t0, t0);
        }
        opn = "add";
        break;
    case OPC_ADDU:
        tcg_gen_add_tl(t0, t0, t1);
        tcg_gen_ext32s_tl(t0, t0);
        opn = "addu";
        break;
    case OPC_SUB:
        {
            TCGv r_tmp1 = tcg_temp_new();
            TCGv r_tmp2 = tcg_temp_new();
            int l1 = gen_new_label();

            save_cpu_state(ctx, 1);
            tcg_gen_ext32s_tl(r_tmp1, t0);
            tcg_gen_ext32s_tl(r_tmp2, t1);
            tcg_gen_sub_tl(t0, r_tmp1, r_tmp2);

            tcg_gen_xor_tl(r_tmp2, r_tmp1, t1);
            tcg_gen_xor_tl(r_tmp1, r_tmp1, t0);
            tcg_gen_and_tl(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free(r_tmp2);
            tcg_gen_brcondi_tl(TCG_COND_GE, r_tmp1, 0, l1);
            /* operands of different sign, first operand and result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_temp_free(r_tmp1);

            tcg_gen_ext32s_tl(t0, t0);
        }
        opn = "sub";
        break;
    case OPC_SUBU:
        tcg_gen_sub_tl(t0, t0, t1);
        tcg_gen_ext32s_tl(t0, t0);
        opn = "subu";
        break;
#if defined(TARGET_MIPS64)
    case OPC_DADD:
        {
            TCGv r_tmp1 = tcg_temp_new();
            TCGv r_tmp2 = tcg_temp_new();
            int l1 = gen_new_label();

            save_cpu_state(ctx, 1);
            tcg_gen_mov_tl(r_tmp1, t0);
            tcg_gen_add_tl(t0, t0, t1);

            tcg_gen_xor_tl(r_tmp1, r_tmp1, t1);
            tcg_gen_xori_tl(r_tmp1, r_tmp1, -1);
            tcg_gen_xor_tl(r_tmp2, t0, t1);
            tcg_gen_and_tl(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free(r_tmp2);
            tcg_gen_brcondi_tl(TCG_COND_GE, r_tmp1, 0, l1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_temp_free(r_tmp1);
        }
        opn = "dadd";
        break;
    case OPC_DADDU:
        tcg_gen_add_tl(t0, t0, t1);
        opn = "daddu";
        break;
    case OPC_DSUB:
        {
            TCGv r_tmp1 = tcg_temp_new();
            TCGv r_tmp2 = tcg_temp_new();
            int l1 = gen_new_label();

            save_cpu_state(ctx, 1);
            tcg_gen_mov_tl(r_tmp1, t0);
            tcg_gen_sub_tl(t0, t0, t1);

            tcg_gen_xor_tl(r_tmp2, r_tmp1, t1);
            tcg_gen_xor_tl(r_tmp1, r_tmp1, t0);
            tcg_gen_and_tl(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free(r_tmp2);
            tcg_gen_brcondi_tl(TCG_COND_GE, r_tmp1, 0, l1);
            /* operands of different sign, first operand and result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_temp_free(r_tmp1);
        }
        opn = "dsub";
        break;
    case OPC_DSUBU:
        tcg_gen_sub_tl(t0, t0, t1);
        opn = "dsubu";
        break;
#endif
    case OPC_SLT:
        gen_op_lt(t0, t1);
        opn = "slt";
        break;
    case OPC_SLTU:
        gen_op_ltu(t0, t1);
        opn = "sltu";
        break;
    case OPC_AND:
        tcg_gen_and_tl(t0, t0, t1);
        opn = "and";
        break;
    case OPC_NOR:
        tcg_gen_or_tl(t0, t0, t1);
        tcg_gen_not_tl(t0, t0);
        opn = "nor";
        break;
    case OPC_OR:
        tcg_gen_or_tl(t0, t0, t1);
        opn = "or";
        break;
    case OPC_XOR:
        tcg_gen_xor_tl(t0, t0, t1);
        opn = "xor";
        break;
    case OPC_MUL:
        tcg_gen_mul_tl(t0, t0, t1);
        tcg_gen_ext32s_tl(t0, t0);
        opn = "mul";
        break;
    case OPC_MOVN:
        {
            int l1 = gen_new_label();

            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            gen_store_gpr(t0, rd);
            gen_set_label(l1);
        }
        opn = "movn";
        goto print;
    case OPC_MOVZ:
        {
            int l1 = gen_new_label();

            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            gen_store_gpr(t0, rd);
            gen_set_label(l1);
        }
        opn = "movz";
        goto print;
    case OPC_SLLV:
        tcg_gen_andi_tl(t0, t0, 0x1f);
        tcg_gen_shl_tl(t0, t1, t0);
        tcg_gen_ext32s_tl(t0, t0);
        opn = "sllv";
        break;
    case OPC_SRAV:
        tcg_gen_ext32s_tl(t1, t1);
        tcg_gen_andi_tl(t0, t0, 0x1f);
        tcg_gen_sar_tl(t0, t1, t0);
        opn = "srav";
        break;
    case OPC_SRLV:
        switch ((ctx->opcode >> 6) & 0x1f) {
        case 0:
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_andi_tl(t0, t0, 0x1f);
            tcg_gen_shr_tl(t0, t1, t0);
            tcg_gen_ext32s_tl(t0, t0);
            opn = "srlv";
            break;
        case 1:
            /* rotrv is decoded as srlv on non-R2 CPUs */
            if (env->insn_flags & ISA_MIPS32R2) {
                int l1 = gen_new_label();
                int l2 = gen_new_label();

                tcg_gen_andi_tl(t0, t0, 0x1f);
                tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
                {
                    TCGv_i32 r_tmp1 = tcg_temp_new_i32();
                    TCGv_i32 r_tmp2 = tcg_temp_new_i32();

                    tcg_gen_trunc_tl_i32(r_tmp1, t0);
                    tcg_gen_trunc_tl_i32(r_tmp2, t1);
                    tcg_gen_rotr_i32(r_tmp1, r_tmp1, r_tmp2);
                    tcg_temp_free_i32(r_tmp1);
                    tcg_temp_free_i32(r_tmp2);
                    tcg_gen_br(l2);
                }
                gen_set_label(l1);
                tcg_gen_mov_tl(t0, t1);
                gen_set_label(l2);
                opn = "rotrv";
            } else {
                tcg_gen_ext32u_tl(t1, t1);
                tcg_gen_andi_tl(t0, t0, 0x1f);
                tcg_gen_shr_tl(t0, t1, t0);
                tcg_gen_ext32s_tl(t0, t0);
                opn = "srlv";
            }
            break;
        default:
            MIPS_INVAL("invalid srlv flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DSLLV:
        tcg_gen_andi_tl(t0, t0, 0x3f);
        tcg_gen_shl_tl(t0, t1, t0);
        opn = "dsllv";
        break;
    case OPC_DSRAV:
        tcg_gen_andi_tl(t0, t0, 0x3f);
        tcg_gen_sar_tl(t0, t1, t0);
        opn = "dsrav";
        break;
    case OPC_DSRLV:
        switch ((ctx->opcode >> 6) & 0x1f) {
        case 0:
            tcg_gen_andi_tl(t0, t0, 0x3f);
            tcg_gen_shr_tl(t0, t1, t0);
            opn = "dsrlv";
            break;
        case 1:
            /* drotrv is decoded as dsrlv on non-R2 CPUs */
            if (env->insn_flags & ISA_MIPS32R2) {
                int l1 = gen_new_label();
                int l2 = gen_new_label();

                tcg_gen_andi_tl(t0, t0, 0x3f);
                tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
                {
                    tcg_gen_rotr_tl(t0, t1, t0);
                    tcg_gen_br(l2);
                }
                gen_set_label(l1);
                tcg_gen_mov_tl(t0, t1);
                gen_set_label(l2);
                opn = "drotrv";
            } else {
                tcg_gen_andi_tl(t0, t0, 0x3f);
                tcg_gen_shr_tl(t0, t1, t0);
                opn = "dsrlv";
            }
            break;
        default:
            MIPS_INVAL("invalid dsrlv flag");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    gen_store_gpr(t0, rd);
 print:
    MIPS_DEBUG("%s %s, %s, %s", opn, regnames[rd], regnames[rs], regnames[rt]);
 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* Arithmetic on HI/LO registers */
static void gen_HILO (DisasContext *ctx, uint32_t opc, int reg)
{
    const char *opn = "hilo";
    TCGv t0 = tcg_temp_local_new();

    if (reg == 0 && (opc == OPC_MFHI || opc == OPC_MFLO)) {
        /* Treat as NOP. */
        MIPS_DEBUG("NOP");
        goto out;
    }
    switch (opc) {
    case OPC_MFHI:
        tcg_gen_mov_tl(t0, cpu_HI[0]);
        gen_store_gpr(t0, reg);
        opn = "mfhi";
        break;
    case OPC_MFLO:
        tcg_gen_mov_tl(t0, cpu_LO[0]);
        gen_store_gpr(t0, reg);
        opn = "mflo";
        break;
    case OPC_MTHI:
        gen_load_gpr(t0, reg);
        tcg_gen_mov_tl(cpu_HI[0], t0);
        opn = "mthi";
        break;
    case OPC_MTLO:
        gen_load_gpr(t0, reg);
        tcg_gen_mov_tl(cpu_LO[0], t0);
        opn = "mtlo";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    MIPS_DEBUG("%s %s", opn, regnames[reg]);
 out:
    tcg_temp_free(t0);
}

static void gen_muldiv (DisasContext *ctx, uint32_t opc,
                        int rs, int rt)
{
    const char *opn = "mul/div";
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    switch (opc) {
    case OPC_DIV:
        {
            int l1 = gen_new_label();

            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            {
                int l2 = gen_new_label();
                TCGv_i32 r_tmp1 = tcg_temp_local_new_i32();
                TCGv_i32 r_tmp2 = tcg_temp_local_new_i32();
                TCGv_i32 r_tmp3 = tcg_temp_local_new_i32();

                tcg_gen_trunc_tl_i32(r_tmp1, t0);
                tcg_gen_trunc_tl_i32(r_tmp2, t1);
                tcg_gen_brcondi_i32(TCG_COND_NE, r_tmp1, -1 << 31, l2);
                tcg_gen_brcondi_i32(TCG_COND_NE, r_tmp2, -1, l2);
                tcg_gen_ext32s_tl(cpu_LO[0], t0);
                tcg_gen_movi_tl(cpu_HI[0], 0);
                tcg_gen_br(l1);
                gen_set_label(l2);
                tcg_gen_div_i32(r_tmp3, r_tmp1, r_tmp2);
                tcg_gen_rem_i32(r_tmp2, r_tmp1, r_tmp2);
                tcg_gen_ext_i32_tl(cpu_LO[0], r_tmp3);
                tcg_gen_ext_i32_tl(cpu_HI[0], r_tmp2);
                tcg_temp_free_i32(r_tmp1);
                tcg_temp_free_i32(r_tmp2);
                tcg_temp_free_i32(r_tmp3);
            }
            gen_set_label(l1);
        }
        opn = "div";
        break;
    case OPC_DIVU:
        {
            int l1 = gen_new_label();

            tcg_gen_ext32s_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            {
                TCGv_i32 r_tmp1 = tcg_temp_new_i32();
                TCGv_i32 r_tmp2 = tcg_temp_new_i32();
                TCGv_i32 r_tmp3 = tcg_temp_new_i32();

                tcg_gen_trunc_tl_i32(r_tmp1, t0);
                tcg_gen_trunc_tl_i32(r_tmp2, t1);
                tcg_gen_divu_i32(r_tmp3, r_tmp1, r_tmp2);
                tcg_gen_remu_i32(r_tmp1, r_tmp1, r_tmp2);
                tcg_gen_ext_i32_tl(cpu_LO[0], r_tmp3);
                tcg_gen_ext_i32_tl(cpu_HI[0], r_tmp1);
                tcg_temp_free_i32(r_tmp1);
                tcg_temp_free_i32(r_tmp2);
                tcg_temp_free_i32(r_tmp3);
            }
            gen_set_label(l1);
        }
        opn = "divu";
        break;
    case OPC_MULT:
        {
            TCGv_i64 r_tmp1 = tcg_temp_new_i64();
            TCGv_i64 r_tmp2 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(r_tmp1, t0);
            tcg_gen_ext_tl_i64(r_tmp2, t1);
            tcg_gen_mul_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free_i64(r_tmp2);
            tcg_gen_trunc_i64_tl(t0, r_tmp1);
            tcg_gen_shri_i64(r_tmp1, r_tmp1, 32);
            tcg_gen_trunc_i64_tl(t1, r_tmp1);
            tcg_temp_free_i64(r_tmp1);
            tcg_gen_ext32s_tl(cpu_LO[0], t0);
            tcg_gen_ext32s_tl(cpu_HI[0], t1);
        }
        opn = "mult";
        break;
    case OPC_MULTU:
        {
            TCGv_i64 r_tmp1 = tcg_temp_new_i64();
            TCGv_i64 r_tmp2 = tcg_temp_new_i64();

            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_extu_tl_i64(r_tmp1, t0);
            tcg_gen_extu_tl_i64(r_tmp2, t1);
            tcg_gen_mul_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free_i64(r_tmp2);
            tcg_gen_trunc_i64_tl(t0, r_tmp1);
            tcg_gen_shri_i64(r_tmp1, r_tmp1, 32);
            tcg_gen_trunc_i64_tl(t1, r_tmp1);
            tcg_temp_free_i64(r_tmp1);
            tcg_gen_ext32s_tl(cpu_LO[0], t0);
            tcg_gen_ext32s_tl(cpu_HI[0], t1);
        }
        opn = "multu";
        break;
#if defined(TARGET_MIPS64)
    case OPC_DDIV:
        {
            int l1 = gen_new_label();

            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            {
                int l2 = gen_new_label();

                tcg_gen_brcondi_tl(TCG_COND_NE, t0, -1LL << 63, l2);
                tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
                tcg_gen_mov_tl(cpu_LO[0], t0);
                tcg_gen_movi_tl(cpu_HI[0], 0);
                tcg_gen_br(l1);
                gen_set_label(l2);
                tcg_gen_div_i64(cpu_LO[0], t0, t1);
                tcg_gen_rem_i64(cpu_HI[0], t0, t1);
            }
            gen_set_label(l1);
        }
        opn = "ddiv";
        break;
    case OPC_DDIVU:
        {
            int l1 = gen_new_label();

            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            tcg_gen_divu_i64(cpu_LO[0], t0, t1);
            tcg_gen_remu_i64(cpu_HI[0], t0, t1);
            gen_set_label(l1);
        }
        opn = "ddivu";
        break;
    case OPC_DMULT:
        gen_helper_dmult(t0, t1);
        opn = "dmult";
        break;
    case OPC_DMULTU:
        gen_helper_dmultu(t0, t1);
        opn = "dmultu";
        break;
#endif
    case OPC_MADD:
        {
            TCGv_i64 r_tmp1 = tcg_temp_new_i64();
            TCGv_i64 r_tmp2 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(r_tmp1, t0);
            tcg_gen_ext_tl_i64(r_tmp2, t1);
            tcg_gen_mul_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_gen_concat_tl_i64(r_tmp2, cpu_LO[0], cpu_HI[0]);
            tcg_gen_add_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free_i64(r_tmp2);
            tcg_gen_trunc_i64_tl(t0, r_tmp1);
            tcg_gen_shri_i64(r_tmp1, r_tmp1, 32);
            tcg_gen_trunc_i64_tl(t1, r_tmp1);
            tcg_temp_free_i64(r_tmp1);
            tcg_gen_ext32s_tl(cpu_LO[0], t0);
            tcg_gen_ext32s_tl(cpu_LO[1], t1);
        }
        opn = "madd";
        break;
    case OPC_MADDU:
       {
            TCGv_i64 r_tmp1 = tcg_temp_new_i64();
            TCGv_i64 r_tmp2 = tcg_temp_new_i64();

            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_extu_tl_i64(r_tmp1, t0);
            tcg_gen_extu_tl_i64(r_tmp2, t1);
            tcg_gen_mul_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_gen_concat_tl_i64(r_tmp2, cpu_LO[0], cpu_HI[0]);
            tcg_gen_add_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free_i64(r_tmp2);
            tcg_gen_trunc_i64_tl(t0, r_tmp1);
            tcg_gen_shri_i64(r_tmp1, r_tmp1, 32);
            tcg_gen_trunc_i64_tl(t1, r_tmp1);
            tcg_temp_free_i64(r_tmp1);
            tcg_gen_ext32s_tl(cpu_LO[0], t0);
            tcg_gen_ext32s_tl(cpu_HI[0], t1);
        }
        opn = "maddu";
        break;
    case OPC_MSUB:
        {
            TCGv_i64 r_tmp1 = tcg_temp_new_i64();
            TCGv_i64 r_tmp2 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(r_tmp1, t0);
            tcg_gen_ext_tl_i64(r_tmp2, t1);
            tcg_gen_mul_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_gen_concat_tl_i64(r_tmp2, cpu_LO[0], cpu_HI[0]);
            tcg_gen_sub_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free_i64(r_tmp2);
            tcg_gen_trunc_i64_tl(t0, r_tmp1);
            tcg_gen_shri_i64(r_tmp1, r_tmp1, 32);
            tcg_gen_trunc_i64_tl(t1, r_tmp1);
            tcg_temp_free_i64(r_tmp1);
            tcg_gen_ext32s_tl(cpu_LO[0], t0);
            tcg_gen_ext32s_tl(cpu_HI[0], t1);
        }
        opn = "msub";
        break;
    case OPC_MSUBU:
        {
            TCGv_i64 r_tmp1 = tcg_temp_new_i64();
            TCGv_i64 r_tmp2 = tcg_temp_new_i64();

            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_extu_tl_i64(r_tmp1, t0);
            tcg_gen_extu_tl_i64(r_tmp2, t1);
            tcg_gen_mul_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_gen_concat_tl_i64(r_tmp2, cpu_LO[0], cpu_HI[0]);
            tcg_gen_sub_i64(r_tmp1, r_tmp1, r_tmp2);
            tcg_temp_free_i64(r_tmp2);
            tcg_gen_trunc_i64_tl(t0, r_tmp1);
            tcg_gen_shri_i64(r_tmp1, r_tmp1, 32);
            tcg_gen_trunc_i64_tl(t1, r_tmp1);
            tcg_temp_free_i64(r_tmp1);
            tcg_gen_ext32s_tl(cpu_LO[0], t0);
            tcg_gen_ext32s_tl(cpu_HI[0], t1);
        }
        opn = "msubu";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    MIPS_DEBUG("%s %s %s", opn, regnames[rs], regnames[rt]);
 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_mul_vr54xx (DisasContext *ctx, uint32_t opc,
                            int rd, int rs, int rt)
{
    const char *opn = "mul vr54xx";
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    switch (opc) {
    case OPC_VR54XX_MULS:
        gen_helper_muls(t0, t0, t1);
        opn = "muls";
	break;
    case OPC_VR54XX_MULSU:
        gen_helper_mulsu(t0, t0, t1);
        opn = "mulsu";
	break;
    case OPC_VR54XX_MACC:
        gen_helper_macc(t0, t0, t1);
        opn = "macc";
	break;
    case OPC_VR54XX_MACCU:
        gen_helper_maccu(t0, t0, t1);
        opn = "maccu";
	break;
    case OPC_VR54XX_MSAC:
        gen_helper_msac(t0, t0, t1);
        opn = "msac";
	break;
    case OPC_VR54XX_MSACU:
        gen_helper_msacu(t0, t0, t1);
        opn = "msacu";
	break;
    case OPC_VR54XX_MULHI:
        gen_helper_mulhi(t0, t0, t1);
        opn = "mulhi";
	break;
    case OPC_VR54XX_MULHIU:
        gen_helper_mulhiu(t0, t0, t1);
        opn = "mulhiu";
	break;
    case OPC_VR54XX_MULSHI:
        gen_helper_mulshi(t0, t0, t1);
        opn = "mulshi";
	break;
    case OPC_VR54XX_MULSHIU:
        gen_helper_mulshiu(t0, t0, t1);
        opn = "mulshiu";
	break;
    case OPC_VR54XX_MACCHI:
        gen_helper_macchi(t0, t0, t1);
        opn = "macchi";
	break;
    case OPC_VR54XX_MACCHIU:
        gen_helper_macchiu(t0, t0, t1);
        opn = "macchiu";
	break;
    case OPC_VR54XX_MSACHI:
        gen_helper_msachi(t0, t0, t1);
        opn = "msachi";
	break;
    case OPC_VR54XX_MSACHIU:
        gen_helper_msachiu(t0, t0, t1);
        opn = "msachiu";
	break;
    default:
        MIPS_INVAL("mul vr54xx");
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    gen_store_gpr(t0, rd);
    MIPS_DEBUG("%s %s, %s, %s", opn, regnames[rd], regnames[rs], regnames[rt]);

 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_cl (DisasContext *ctx, uint32_t opc,
                    int rd, int rs)
{
    const char *opn = "CLx";
    TCGv t0 = tcg_temp_local_new();

    if (rd == 0) {
        /* Treat as NOP. */
        MIPS_DEBUG("NOP");
        goto out;
    }
    gen_load_gpr(t0, rs);
    switch (opc) {
    case OPC_CLO:
        gen_helper_clo(t0, t0);
        opn = "clo";
        break;
    case OPC_CLZ:
        gen_helper_clz(t0, t0);
        opn = "clz";
        break;
#if defined(TARGET_MIPS64)
    case OPC_DCLO:
        gen_helper_dclo(t0, t0);
        opn = "dclo";
        break;
    case OPC_DCLZ:
        gen_helper_dclz(t0, t0);
        opn = "dclz";
        break;
#endif
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    gen_store_gpr(t0, rd);
    MIPS_DEBUG("%s %s, %s", opn, regnames[rd], regnames[rs]);

 out:
    tcg_temp_free(t0);
}

/* Traps */
static void gen_trap (DisasContext *ctx, uint32_t opc,
                      int rs, int rt, int16_t imm)
{
    int cond;
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    cond = 0;
    /* Load needed operands */
    switch (opc) {
    case OPC_TEQ:
    case OPC_TGE:
    case OPC_TGEU:
    case OPC_TLT:
    case OPC_TLTU:
    case OPC_TNE:
        /* Compare two registers */
        if (rs != rt) {
            gen_load_gpr(t0, rs);
            gen_load_gpr(t1, rt);
            cond = 1;
        }
        break;
    case OPC_TEQI:
    case OPC_TGEI:
    case OPC_TGEIU:
    case OPC_TLTI:
    case OPC_TLTIU:
    case OPC_TNEI:
        /* Compare register to immediate */
        if (rs != 0 || imm != 0) {
            gen_load_gpr(t0, rs);
            tcg_gen_movi_tl(t1, (int32_t)imm);
            cond = 1;
        }
        break;
    }
    if (cond == 0) {
        switch (opc) {
        case OPC_TEQ:   /* rs == rs */
        case OPC_TEQI:  /* r0 == 0  */
        case OPC_TGE:   /* rs >= rs */
        case OPC_TGEI:  /* r0 >= 0  */
        case OPC_TGEU:  /* rs >= rs unsigned */
        case OPC_TGEIU: /* r0 >= 0  unsigned */
            /* Always trap */
            tcg_gen_movi_tl(t0, 1);
            break;
        case OPC_TLT:   /* rs < rs           */
        case OPC_TLTI:  /* r0 < 0            */
        case OPC_TLTU:  /* rs < rs unsigned  */
        case OPC_TLTIU: /* r0 < 0  unsigned  */
        case OPC_TNE:   /* rs != rs          */
        case OPC_TNEI:  /* r0 != 0           */
            /* Never trap: treat as NOP. */
            goto out;
        default:
            MIPS_INVAL("trap");
            generate_exception(ctx, EXCP_RI);
            goto out;
        }
    } else {
        switch (opc) {
        case OPC_TEQ:
        case OPC_TEQI:
            gen_op_eq(t0, t1);
            break;
        case OPC_TGE:
        case OPC_TGEI:
            gen_op_ge(t0, t1);
            break;
        case OPC_TGEU:
        case OPC_TGEIU:
            gen_op_geu(t0, t1);
            break;
        case OPC_TLT:
        case OPC_TLTI:
            gen_op_lt(t0, t1);
            break;
        case OPC_TLTU:
        case OPC_TLTIU:
            gen_op_ltu(t0, t1);
            break;
        case OPC_TNE:
        case OPC_TNEI:
            gen_op_ne(t0, t1);
            break;
        default:
            MIPS_INVAL("trap");
            generate_exception(ctx, EXCP_RI);
            goto out;
        }
    }
    save_cpu_state(ctx, 1);
    {
        int l1 = gen_new_label();

        tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
        gen_helper_0i(raise_exception, EXCP_TRAP);
        gen_set_label(l1);
    }
    ctx->bstate = BS_STOP;
 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    TranslationBlock *tb;
    tb = ctx->tb;
    if ((tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK)) {
        tcg_gen_goto_tb(n);
        gen_save_pc(dest);
        tcg_gen_exit_tb((long)tb + n);
    } else {
        gen_save_pc(dest);
        tcg_gen_exit_tb(0);
    }
}

/* Branches (before delay slot) */
static void gen_compute_branch (DisasContext *ctx, uint32_t opc,
                                int rs, int rt, int32_t offset)
{
    target_ulong btgt = -1;
    int blink = 0;
    int bcond_compute = 0;
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
#ifdef MIPS_DEBUG_DISAS
        if (loglevel & CPU_LOG_TB_IN_ASM) {
            fprintf(logfile,
                    "Branch in delay slot at PC 0x" TARGET_FMT_lx "\n",
                    ctx->pc);
	}
#endif
        generate_exception(ctx, EXCP_RI);
        goto out;
    }

    /* Load needed operands */
    switch (opc) {
    case OPC_BEQ:
    case OPC_BEQL:
    case OPC_BNE:
    case OPC_BNEL:
        /* Compare two registers */
        if (rs != rt) {
            gen_load_gpr(t0, rs);
            gen_load_gpr(t1, rt);
            bcond_compute = 1;
        }
        btgt = ctx->pc + 4 + offset;
        break;
    case OPC_BGEZ:
    case OPC_BGEZAL:
    case OPC_BGEZALL:
    case OPC_BGEZL:
    case OPC_BGTZ:
    case OPC_BGTZL:
    case OPC_BLEZ:
    case OPC_BLEZL:
    case OPC_BLTZ:
    case OPC_BLTZAL:
    case OPC_BLTZALL:
    case OPC_BLTZL:
        /* Compare to zero */
        if (rs != 0) {
            gen_load_gpr(t0, rs);
            bcond_compute = 1;
        }
        btgt = ctx->pc + 4 + offset;
        break;
    case OPC_J:
    case OPC_JAL:
        /* Jump to immediate */
        btgt = ((ctx->pc + 4) & (int32_t)0xF0000000) | (uint32_t)offset;
        break;
    case OPC_JR:
    case OPC_JALR:
        /* Jump to register */
        if (offset != 0 && offset != 16) {
            /* Hint = 0 is JR/JALR, hint 16 is JR.HB/JALR.HB, the
               others are reserved. */
            MIPS_INVAL("jump hint");
            generate_exception(ctx, EXCP_RI);
            goto out;
        }
        gen_load_gpr(btarget, rs);
        break;
    default:
        MIPS_INVAL("branch/jump");
        generate_exception(ctx, EXCP_RI);
        goto out;
    }
    if (bcond_compute == 0) {
        /* No condition to be computed */
        switch (opc) {
        case OPC_BEQ:     /* rx == rx        */
        case OPC_BEQL:    /* rx == rx likely */
        case OPC_BGEZ:    /* 0 >= 0          */
        case OPC_BGEZL:   /* 0 >= 0 likely   */
        case OPC_BLEZ:    /* 0 <= 0          */
        case OPC_BLEZL:   /* 0 <= 0 likely   */
            /* Always take */
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("balways");
            break;
        case OPC_BGEZAL:  /* 0 >= 0          */
        case OPC_BGEZALL: /* 0 >= 0 likely   */
            /* Always take and link */
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("balways and link");
            break;
        case OPC_BNE:     /* rx != rx        */
        case OPC_BGTZ:    /* 0 > 0           */
        case OPC_BLTZ:    /* 0 < 0           */
            /* Treat as NOP. */
            MIPS_DEBUG("bnever (NOP)");
            goto out;
        case OPC_BLTZAL:  /* 0 < 0           */
            tcg_gen_movi_tl(t0, ctx->pc + 8);
            gen_store_gpr(t0, 31);
            MIPS_DEBUG("bnever and link");
            goto out;
        case OPC_BLTZALL: /* 0 < 0 likely */
            tcg_gen_movi_tl(t0, ctx->pc + 8);
            gen_store_gpr(t0, 31);
            /* Skip the instruction in the delay slot */
            MIPS_DEBUG("bnever, link and skip");
            ctx->pc += 4;
            goto out;
        case OPC_BNEL:    /* rx != rx likely */
        case OPC_BGTZL:   /* 0 > 0 likely */
        case OPC_BLTZL:   /* 0 < 0 likely */
            /* Skip the instruction in the delay slot */
            MIPS_DEBUG("bnever and skip");
            ctx->pc += 4;
            goto out;
        case OPC_J:
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("j " TARGET_FMT_lx, btgt);
            break;
        case OPC_JAL:
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            MIPS_DEBUG("jal " TARGET_FMT_lx, btgt);
            break;
        case OPC_JR:
            ctx->hflags |= MIPS_HFLAG_BR;
            MIPS_DEBUG("jr %s", regnames[rs]);
            break;
        case OPC_JALR:
            blink = rt;
            ctx->hflags |= MIPS_HFLAG_BR;
            MIPS_DEBUG("jalr %s, %s", regnames[rt], regnames[rs]);
            break;
        default:
            MIPS_INVAL("branch/jump");
            generate_exception(ctx, EXCP_RI);
            goto out;
        }
    } else {
        switch (opc) {
        case OPC_BEQ:
            gen_op_eq(t0, t1);
            MIPS_DEBUG("beq %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btgt);
            goto not_likely;
        case OPC_BEQL:
            gen_op_eq(t0, t1);
            MIPS_DEBUG("beql %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btgt);
            goto likely;
        case OPC_BNE:
            gen_op_ne(t0, t1);
            MIPS_DEBUG("bne %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btgt);
            goto not_likely;
        case OPC_BNEL:
            gen_op_ne(t0, t1);
            MIPS_DEBUG("bnel %s, %s, " TARGET_FMT_lx,
                       regnames[rs], regnames[rt], btgt);
            goto likely;
        case OPC_BGEZ:
            gen_op_gez(t0);
            MIPS_DEBUG("bgez %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto not_likely;
        case OPC_BGEZL:
            gen_op_gez(t0);
            MIPS_DEBUG("bgezl %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto likely;
        case OPC_BGEZAL:
            gen_op_gez(t0);
            MIPS_DEBUG("bgezal %s, " TARGET_FMT_lx, regnames[rs], btgt);
            blink = 31;
            goto not_likely;
        case OPC_BGEZALL:
            gen_op_gez(t0);
            blink = 31;
            MIPS_DEBUG("bgezall %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto likely;
        case OPC_BGTZ:
            gen_op_gtz(t0);
            MIPS_DEBUG("bgtz %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto not_likely;
        case OPC_BGTZL:
            gen_op_gtz(t0);
            MIPS_DEBUG("bgtzl %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto likely;
        case OPC_BLEZ:
            gen_op_lez(t0);
            MIPS_DEBUG("blez %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto not_likely;
        case OPC_BLEZL:
            gen_op_lez(t0);
            MIPS_DEBUG("blezl %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto likely;
        case OPC_BLTZ:
            gen_op_ltz(t0);
            MIPS_DEBUG("bltz %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto not_likely;
        case OPC_BLTZL:
            gen_op_ltz(t0);
            MIPS_DEBUG("bltzl %s, " TARGET_FMT_lx, regnames[rs], btgt);
            goto likely;
        case OPC_BLTZAL:
            gen_op_ltz(t0);
            blink = 31;
            MIPS_DEBUG("bltzal %s, " TARGET_FMT_lx, regnames[rs], btgt);
        not_likely:
            ctx->hflags |= MIPS_HFLAG_BC;
            tcg_gen_trunc_tl_i32(bcond, t0);
            break;
        case OPC_BLTZALL:
            gen_op_ltz(t0);
            blink = 31;
            MIPS_DEBUG("bltzall %s, " TARGET_FMT_lx, regnames[rs], btgt);
        likely:
            ctx->hflags |= MIPS_HFLAG_BL;
            tcg_gen_trunc_tl_i32(bcond, t0);
            break;
        default:
            MIPS_INVAL("conditional branch/jump");
            generate_exception(ctx, EXCP_RI);
            goto out;
        }
    }
    MIPS_DEBUG("enter ds: link %d cond %02x target " TARGET_FMT_lx,
               blink, ctx->hflags, btgt);

    ctx->btarget = btgt;
    if (blink > 0) {
        tcg_gen_movi_tl(t0, ctx->pc + 8);
        gen_store_gpr(t0, blink);
    }

 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* special3 bitfield operations */
static void gen_bitops (DisasContext *ctx, uint32_t opc, int rt,
                        int rs, int lsb, int msb)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    target_ulong mask;

    gen_load_gpr(t1, rs);
    switch (opc) {
    case OPC_EXT:
        if (lsb + msb > 31)
            goto fail;
        tcg_gen_shri_tl(t0, t1, lsb);
        if (msb != 31) {
            tcg_gen_andi_tl(t0, t0, (1 << (msb + 1)) - 1);
        } else {
            tcg_gen_ext32s_tl(t0, t0);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DEXTM:
        tcg_gen_shri_tl(t0, t1, lsb);
        if (msb != 31) {
            tcg_gen_andi_tl(t0, t0, (1ULL << (msb + 1 + 32)) - 1);
        }
        break;
    case OPC_DEXTU:
        tcg_gen_shri_tl(t0, t1, lsb + 32);
        tcg_gen_andi_tl(t0, t0, (1ULL << (msb + 1)) - 1);
        break;
    case OPC_DEXT:
        tcg_gen_shri_tl(t0, t1, lsb);
        tcg_gen_andi_tl(t0, t0, (1ULL << (msb + 1)) - 1);
        break;
#endif
    case OPC_INS:
        if (lsb > msb)
            goto fail;
        mask = ((msb - lsb + 1 < 32) ? ((1 << (msb - lsb + 1)) - 1) : ~0) << lsb;
        gen_load_gpr(t0, rt);
        tcg_gen_andi_tl(t0, t0, ~mask);
        tcg_gen_shli_tl(t1, t1, lsb);
        tcg_gen_andi_tl(t1, t1, mask);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_gen_ext32s_tl(t0, t0);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DINSM:
        if (lsb > msb)
            goto fail;
        mask = ((msb - lsb + 1 + 32 < 64) ? ((1ULL << (msb - lsb + 1 + 32)) - 1) : ~0ULL) << lsb;
        gen_load_gpr(t0, rt);
        tcg_gen_andi_tl(t0, t0, ~mask);
        tcg_gen_shli_tl(t1, t1, lsb);
        tcg_gen_andi_tl(t1, t1, mask);
        tcg_gen_or_tl(t0, t0, t1);
        break;
    case OPC_DINSU:
        if (lsb > msb)
            goto fail;
        mask = ((1ULL << (msb - lsb + 1)) - 1) << lsb;
        gen_load_gpr(t0, rt);
        tcg_gen_andi_tl(t0, t0, ~mask);
        tcg_gen_shli_tl(t1, t1, lsb + 32);
        tcg_gen_andi_tl(t1, t1, mask);
        tcg_gen_or_tl(t0, t0, t1);
        break;
    case OPC_DINS:
        if (lsb > msb)
            goto fail;
        gen_load_gpr(t0, rt);
        mask = ((1ULL << (msb - lsb + 1)) - 1) << lsb;
        gen_load_gpr(t0, rt);
        tcg_gen_andi_tl(t0, t0, ~mask);
        tcg_gen_shli_tl(t1, t1, lsb);
        tcg_gen_andi_tl(t1, t1, mask);
        tcg_gen_or_tl(t0, t0, t1);
        break;
#endif
    default:
fail:
        MIPS_INVAL("bitops");
        generate_exception(ctx, EXCP_RI);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
        return;
    }
    gen_store_gpr(t0, rt);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_bshfl (DisasContext *ctx, uint32_t op2, int rt, int rd)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t1, rt);
    switch (op2) {
    case OPC_WSBH:
        tcg_gen_shri_tl(t0, t1, 8);
        tcg_gen_andi_tl(t0, t0, 0x00FF00FF);
        tcg_gen_shli_tl(t1, t1, 8);
        tcg_gen_andi_tl(t1, t1, ~0x00FF00FF);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_gen_ext32s_tl(t0, t0);
        break;
    case OPC_SEB:
        tcg_gen_ext8s_tl(t0, t1);
        break;
    case OPC_SEH:
        tcg_gen_ext16s_tl(t0, t1);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DSBH:
        gen_load_gpr(t1, rt);
        tcg_gen_shri_tl(t0, t1, 8);
        tcg_gen_andi_tl(t0, t0, 0x00FF00FF00FF00FFULL);
        tcg_gen_shli_tl(t1, t1, 8);
        tcg_gen_andi_tl(t1, t1, ~0x00FF00FF00FF00FFULL);
        tcg_gen_or_tl(t0, t0, t1);
        break;
    case OPC_DSHD:
        gen_load_gpr(t1, rt);
        tcg_gen_shri_tl(t0, t1, 16);
        tcg_gen_andi_tl(t0, t0, 0x0000FFFF0000FFFFULL);
        tcg_gen_shli_tl(t1, t1, 16);
        tcg_gen_andi_tl(t1, t1, ~0x0000FFFF0000FFFFULL);
        tcg_gen_or_tl(t1, t0, t1);
        tcg_gen_shri_tl(t0, t1, 32);
        tcg_gen_shli_tl(t1, t1, 32);
        tcg_gen_or_tl(t0, t0, t1);
        break;
#endif
    default:
        MIPS_INVAL("bsfhl");
        generate_exception(ctx, EXCP_RI);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
        return;
    }
    gen_store_gpr(t0, rd);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

#ifndef CONFIG_USER_ONLY
/* CP0 (MMU and control) */
static inline void gen_mfc0_load32 (TCGv t, target_ulong off)
{
    TCGv_i32 r_tmp = tcg_temp_new_i32();

    tcg_gen_ld_i32(r_tmp, cpu_env, off);
    tcg_gen_ext_i32_tl(t, r_tmp);
    tcg_temp_free_i32(r_tmp);
}

static inline void gen_mfc0_load64 (TCGv t, target_ulong off)
{
    tcg_gen_ld_tl(t, cpu_env, off);
    tcg_gen_ext32s_tl(t, t);
}

static inline void gen_mtc0_store32 (TCGv t, target_ulong off)
{
    TCGv_i32 r_tmp = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(r_tmp, t);
    tcg_gen_st_i32(r_tmp, cpu_env, off);
    tcg_temp_free_i32(r_tmp);
}

static inline void gen_mtc0_store64 (TCGv t, target_ulong off)
{
    tcg_gen_ext32s_tl(t, t);
    tcg_gen_st_tl(t, cpu_env, off);
}

static void gen_mfc0 (CPUState *env, DisasContext *ctx, TCGv t0, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(env, ctx, ISA_MIPS32);

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Index));
            rn = "Index";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_mvpcontrol(t0);
            rn = "MVPControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_mvpconf0(t0);
            rn = "MVPConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_mvpconf1(t0);
            rn = "MVPConf1";
            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            gen_helper_mfc0_random(t0);
            rn = "Random";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEControl));
            rn = "VPEControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEConf0));
            rn = "VPEConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEConf1));
            rn = "VPEConf1";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load64(t0, offsetof(CPUState, CP0_YQMask));
            rn = "YQMask";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load64(t0, offsetof(CPUState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load64(t0, offsetof(CPUState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEOpt));
            rn = "VPEOpt";
            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EntryLo0));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "EntryLo0";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcstatus(t0);
            rn = "TCStatus";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcbind(t0);
            rn = "TCBind";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcrestart(t0);
            rn = "TCRestart";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tchalt(t0);
            rn = "TCHalt";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tccontext(t0);
            rn = "TCContext";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcschedule(t0);
            rn = "TCSchedule";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcschefback(t0);
            rn = "TCScheFBack";
            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EntryLo1));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_Context));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "Context";
            break;
        case 1:
//            gen_helper_mfc0_contextconfig(t0); /* SmartMIPS ASE */
            rn = "ContextConfig";
//            break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_PageMask));
            rn = "PageMask";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_PageGrain));
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Wired));
            rn = "Wired";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf0));
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf1));
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf2));
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf3));
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf4));
            rn = "SRSConf4";
            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_HWREna));
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_BadVAddr));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "BadVAddr";
            break;
        default:
            goto die;
       }
        break;
    case 9:
        switch (sel) {
        case 0:
            /* Mark as an IO operation because we read the time.  */
            if (use_icount)
                gen_io_start();
            gen_helper_mfc0_count(t0);
            if (use_icount) {
                gen_io_end();
                ctx->bstate = BS_STOP;
            }
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EntryHi));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Compare));
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Status));
            rn = "Status";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_IntCtl));
            rn = "IntCtl";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSCtl));
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSMap));
            rn = "SRSMap";
            break;
        default:
            goto die;
       }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Cause));
            rn = "Cause";
            break;
        default:
            goto die;
       }
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EPC));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_PRid));
            rn = "PRid";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_EBase));
            rn = "EBase";
            break;
        default:
            goto die;
       }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config0));
            rn = "Config";
            break;
        case 1:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config1));
            rn = "Config1";
            break;
        case 2:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config2));
            rn = "Config2";
            break;
        case 3:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config3));
            rn = "Config3";
            break;
        /* 4,5 are reserved */
        /* 6,7 are implementation dependent */
        case 6:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config6));
            rn = "Config6";
            break;
        case 7:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config7));
            rn = "Config7";
            break;
        default:
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_helper_mfc0_lladdr(t0);
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(mfc0_watchlo, t0, sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ...7:
            gen_helper_1i(mfc0_watchhi, t0, sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#if defined(TARGET_MIPS64)
            check_insn(env, ctx, ISA_MIPS3);
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_XContext));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "XContext";
            break;
#endif
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Framemask));
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        tcg_gen_movi_tl(t0, 0); /* unimplemented */
        rn = "'Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mfc0_debug(t0); /* EJTAG support */
            rn = "Debug";
            break;
        case 1:
//            gen_helper_mfc0_tracecontrol(t0); /* PDtrace support */
            rn = "TraceControl";
//            break;
        case 2:
//            gen_helper_mfc0_tracecontrol2(t0); /* PDtrace support */
            rn = "TraceControl2";
//            break;
        case 3:
//            gen_helper_mfc0_usertracedata(t0); /* PDtrace support */
            rn = "UserTraceData";
//            break;
        case 4:
//            gen_helper_mfc0_tracebpc(t0); /* PDtrace support */
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_DEPC));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Performance0));
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_mfc0_performance1(t0);
            rn = "Performance1";
//            break;
        case 2:
//            gen_helper_mfc0_performance2(t0);
            rn = "Performance2";
//            break;
        case 3:
//            gen_helper_mfc0_performance3(t0);
            rn = "Performance3";
//            break;
        case 4:
//            gen_helper_mfc0_performance4(t0);
            rn = "Performance4";
//            break;
        case 5:
//            gen_helper_mfc0_performance5(t0);
            rn = "Performance5";
//            break;
        case 6:
//            gen_helper_mfc0_performance6(t0);
            rn = "Performance6";
//            break;
        case 7:
//            gen_helper_mfc0_performance7(t0);
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
        break;
    case 26:
        tcg_gen_movi_tl(t0, 0); /* unimplemented */
        rn = "ECC";
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            tcg_gen_movi_tl(t0, 0); /* unimplemented */
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_TagLo));
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_DataLo));
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_TagHi));
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_DataHi));
            rn = "DataHi";
            break;
        default:
            goto die;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_ErrorEPC));
            tcg_gen_ext32s_tl(t0, t0);
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        break;
    default:
       goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

static void gen_mtc0 (CPUState *env, DisasContext *ctx, TCGv t0, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(env, ctx, ISA_MIPS32);

    if (use_icount)
        gen_io_start();

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_helper_mtc0_index(t0);
            rn = "Index";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_mvpcontrol(t0);
            rn = "MVPControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            /* ignored */
            rn = "MVPConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            /* ignored */
            rn = "MVPConf1";
            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "Random";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpecontrol(t0);
            rn = "VPEControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpeconf0(t0);
            rn = "VPEConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpeconf1(t0);
            rn = "VPEConf1";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_yqmask(t0);
            rn = "YQMask";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            gen_mtc0_store64(t0, offsetof(CPUState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            gen_mtc0_store64(t0, offsetof(CPUState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpeopt(t0);
            rn = "VPEOpt";
            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entrylo0(t0);
            rn = "EntryLo0";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcstatus(t0);
            rn = "TCStatus";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcbind(t0);
            rn = "TCBind";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcrestart(t0);
            rn = "TCRestart";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tchalt(t0);
            rn = "TCHalt";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tccontext(t0);
            rn = "TCContext";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcschedule(t0);
            rn = "TCSchedule";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcschefback(t0);
            rn = "TCScheFBack";
            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entrylo1(t0);
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_helper_mtc0_context(t0);
            rn = "Context";
            break;
        case 1:
//            gen_helper_mtc0_contextconfig(t0); /* SmartMIPS ASE */
            rn = "ContextConfig";
//            break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_helper_mtc0_pagemask(t0);
            rn = "PageMask";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_pagegrain(t0);
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_helper_mtc0_wired(t0);
            rn = "Wired";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf0(t0);
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf1(t0);
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf2(t0);
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf3(t0);
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf4(t0);
            rn = "SRSConf4";
            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_hwrena(t0);
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        /* ignored */
        rn = "BadVAddr";
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_helper_mtc0_count(t0);
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entryhi(t0);
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_helper_mtc0_compare(t0);
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_helper_mtc0_status(t0);
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Status";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_intctl(t0);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "IntCtl";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsctl(t0);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mtc0_store32(t0, offsetof(CPUState, CP0_SRSMap));
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSMap";
            break;
        default:
            goto die;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_helper_mtc0_cause(t0);
            rn = "Cause";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 14:
        switch (sel) {
        case 0:
            gen_mtc0_store64(t0, offsetof(CPUState, CP0_EPC));
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "PRid";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_ebase(t0);
            rn = "EBase";
            break;
        default:
            goto die;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_helper_mtc0_config0(t0);
            rn = "Config";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 1:
            /* ignored, read only */
            rn = "Config1";
            break;
        case 2:
            gen_helper_mtc0_config2(t0);
            rn = "Config2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 3:
            /* ignored, read only */
            rn = "Config3";
            break;
        /* 4,5 are reserved */
        /* 6,7 are implementation dependent */
        case 6:
            /* ignored */
            rn = "Config6";
            break;
        case 7:
            /* ignored */
            rn = "Config7";
            break;
        default:
            rn = "Invalid config selector";
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(mtc0_watchlo, t0, sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(mtc0_watchhi, t0, sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#if defined(TARGET_MIPS64)
            check_insn(env, ctx, ISA_MIPS3);
            gen_helper_mtc0_xcontext(t0);
            rn = "XContext";
            break;
#endif
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_helper_mtc0_framemask(t0);
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        /* ignored */
        rn = "Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mtc0_debug(t0); /* EJTAG support */
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Debug";
            break;
        case 1:
//            gen_helper_mtc0_tracecontrol(t0); /* PDtrace support */
            rn = "TraceControl";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            break;
        case 2:
//            gen_helper_mtc0_tracecontrol2(t0); /* PDtrace support */
            rn = "TraceControl2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            break;
        case 3:
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            gen_helper_mtc0_usertracedata(t0); /* PDtrace support */
            rn = "UserTraceData";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            break;
        case 4:
//            gen_helper_mtc0_tracebpc(t0); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mtc0_store64(t0, offsetof(CPUState, CP0_DEPC));
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_helper_mtc0_performance0(t0);
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_mtc0_performance1(t0);
            rn = "Performance1";
//            break;
        case 2:
//            gen_helper_mtc0_performance2(t0);
            rn = "Performance2";
//            break;
        case 3:
//            gen_helper_mtc0_performance3(t0);
            rn = "Performance3";
//            break;
        case 4:
//            gen_helper_mtc0_performance4(t0);
            rn = "Performance4";
//            break;
        case 5:
//            gen_helper_mtc0_performance5(t0);
            rn = "Performance5";
//            break;
        case 6:
//            gen_helper_mtc0_performance6(t0);
            rn = "Performance6";
//            break;
        case 7:
//            gen_helper_mtc0_performance7(t0);
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
       break;
    case 26:
        /* ignored */
        rn = "ECC";
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            /* ignored */
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
       break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taglo(t0);
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datalo(t0);
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taghi(t0);
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datahi(t0);
            rn = "DataHi";
            break;
        default:
            rn = "invalid sel";
            goto die;
        }
       break;
    case 30:
        switch (sel) {
        case 0:
            gen_mtc0_store64(t0, offsetof(CPUState, CP0_ErrorEPC));
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mtc0_store32(t0, offsetof(CPUState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    default:
       goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    /* For simplicity assume that all writes can cause interrupts.  */
    if (use_icount) {
        gen_io_end();
        ctx->bstate = BS_STOP;
    }
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

#if defined(TARGET_MIPS64)
static void gen_dmfc0 (CPUState *env, DisasContext *ctx, TCGv t0, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(env, ctx, ISA_MIPS64);

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Index));
            rn = "Index";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_mvpcontrol(t0);
            rn = "MVPControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_mvpconf0(t0);
            rn = "MVPConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_mvpconf1(t0);
            rn = "MVPConf1";
            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            gen_helper_mfc0_random(t0);
            rn = "Random";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEControl));
            rn = "VPEControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEConf0));
            rn = "VPEConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEConf1));
            rn = "VPEConf1";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_YQMask));
            rn = "YQMask";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_VPEOpt));
            rn = "VPEOpt";
            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EntryLo0));
            rn = "EntryLo0";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcstatus(t0);
            rn = "TCStatus";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mfc0_tcbind(t0);
            rn = "TCBind";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_dmfc0_tcrestart(t0);
            rn = "TCRestart";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_helper_dmfc0_tchalt(t0);
            rn = "TCHalt";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            gen_helper_dmfc0_tccontext(t0);
            rn = "TCContext";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            gen_helper_dmfc0_tcschedule(t0);
            rn = "TCSchedule";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_helper_dmfc0_tcschefback(t0);
            rn = "TCScheFBack";
            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EntryLo1));
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_Context));
            rn = "Context";
            break;
        case 1:
//            gen_helper_dmfc0_contextconfig(t0); /* SmartMIPS ASE */
            rn = "ContextConfig";
//            break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_PageMask));
            rn = "PageMask";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_PageGrain));
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Wired));
            rn = "Wired";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf0));
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf1));
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf2));
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf3));
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSConf4));
            rn = "SRSConf4";
            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_HWREna));
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_BadVAddr));
            rn = "BadVAddr";
            break;
        default:
            goto die;
        }
        break;
    case 9:
        switch (sel) {
        case 0:
            /* Mark as an IO operation because we read the time.  */
            if (use_icount)
                gen_io_start();
            gen_helper_mfc0_count(t0);
            if (use_icount) {
                gen_io_end();
                ctx->bstate = BS_STOP;
            }
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EntryHi));
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Compare));
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Status));
            rn = "Status";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_IntCtl));
            rn = "IntCtl";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSCtl));
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_SRSMap));
            rn = "SRSMap";
            break;
        default:
            goto die;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Cause));
            rn = "Cause";
            break;
        default:
            goto die;
        }
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_EPC));
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_PRid));
            rn = "PRid";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_EBase));
            rn = "EBase";
            break;
        default:
            goto die;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config0));
            rn = "Config";
            break;
        case 1:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config1));
            rn = "Config1";
            break;
        case 2:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config2));
            rn = "Config2";
            break;
        case 3:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config3));
            rn = "Config3";
            break;
       /* 6,7 are implementation dependent */
        case 6:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config6));
            rn = "Config6";
            break;
        case 7:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Config7));
            rn = "Config7";
            break;
        default:
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_helper_dmfc0_lladdr(t0);
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(dmfc0_watchlo, t0, sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(mfc0_watchhi, t0, sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
            check_insn(env, ctx, ISA_MIPS3);
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_XContext));
            rn = "XContext";
            break;
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Framemask));
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        tcg_gen_movi_tl(t0, 0); /* unimplemented */
        rn = "'Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mfc0_debug(t0); /* EJTAG support */
            rn = "Debug";
            break;
        case 1:
//            gen_helper_dmfc0_tracecontrol(t0); /* PDtrace support */
            rn = "TraceControl";
//            break;
        case 2:
//            gen_helper_dmfc0_tracecontrol2(t0); /* PDtrace support */
            rn = "TraceControl2";
//            break;
        case 3:
//            gen_helper_dmfc0_usertracedata(t0); /* PDtrace support */
            rn = "UserTraceData";
//            break;
        case 4:
//            gen_helper_dmfc0_tracebpc(t0); /* PDtrace support */
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_DEPC));
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_Performance0));
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_dmfc0_performance1(t0);
            rn = "Performance1";
//            break;
        case 2:
//            gen_helper_dmfc0_performance2(t0);
            rn = "Performance2";
//            break;
        case 3:
//            gen_helper_dmfc0_performance3(t0);
            rn = "Performance3";
//            break;
        case 4:
//            gen_helper_dmfc0_performance4(t0);
            rn = "Performance4";
//            break;
        case 5:
//            gen_helper_dmfc0_performance5(t0);
            rn = "Performance5";
//            break;
        case 6:
//            gen_helper_dmfc0_performance6(t0);
            rn = "Performance6";
//            break;
        case 7:
//            gen_helper_dmfc0_performance7(t0);
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
        break;
    case 26:
        tcg_gen_movi_tl(t0, 0); /* unimplemented */
        rn = "ECC";
        break;
    case 27:
        switch (sel) {
        /* ignored */
        case 0 ... 3:
            tcg_gen_movi_tl(t0, 0); /* unimplemented */
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_TagLo));
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_DataLo));
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_TagHi));
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_DataHi));
            rn = "DataHi";
            break;
        default:
            goto die;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, CP0_ErrorEPC));
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mfc0_load32(t0, offsetof(CPUState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        break;
    default:
        goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmfc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

static void gen_dmtc0 (CPUState *env, DisasContext *ctx, TCGv t0, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(env, ctx, ISA_MIPS64);

    if (use_icount)
        gen_io_start();

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_helper_mtc0_index(t0);
            rn = "Index";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_mvpcontrol(t0);
            rn = "MVPControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            /* ignored */
            rn = "MVPConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            /* ignored */
            rn = "MVPConf1";
            break;
        default:
            goto die;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "Random";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpecontrol(t0);
            rn = "VPEControl";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpeconf0(t0);
            rn = "VPEConf0";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpeconf1(t0);
            rn = "VPEConf1";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_yqmask(t0);
            rn = "YQMask";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_vpeopt(t0);
            rn = "VPEOpt";
            break;
        default:
            goto die;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entrylo0(t0);
            rn = "EntryLo0";
            break;
        case 1:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcstatus(t0);
            rn = "TCStatus";
            break;
        case 2:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcbind(t0);
            rn = "TCBind";
            break;
        case 3:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcrestart(t0);
            rn = "TCRestart";
            break;
        case 4:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tchalt(t0);
            rn = "TCHalt";
            break;
        case 5:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tccontext(t0);
            rn = "TCContext";
            break;
        case 6:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcschedule(t0);
            rn = "TCSchedule";
            break;
        case 7:
            check_insn(env, ctx, ASE_MT);
            gen_helper_mtc0_tcschefback(t0);
            rn = "TCScheFBack";
            break;
        default:
            goto die;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entrylo1(t0);
            rn = "EntryLo1";
            break;
        default:
            goto die;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_helper_mtc0_context(t0);
            rn = "Context";
            break;
        case 1:
//           gen_helper_mtc0_contextconfig(t0); /* SmartMIPS ASE */
            rn = "ContextConfig";
//           break;
        default:
            goto die;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_helper_mtc0_pagemask(t0);
            rn = "PageMask";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_pagegrain(t0);
            rn = "PageGrain";
            break;
        default:
            goto die;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_helper_mtc0_wired(t0);
            rn = "Wired";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf0(t0);
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf1(t0);
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf2(t0);
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf3(t0);
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf4(t0);
            rn = "SRSConf4";
            break;
        default:
            goto die;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_hwrena(t0);
            rn = "HWREna";
            break;
        default:
            goto die;
        }
        break;
    case 8:
        /* ignored */
        rn = "BadVAddr";
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_helper_mtc0_count(t0);
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entryhi(t0);
            rn = "EntryHi";
            break;
        default:
            goto die;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_helper_mtc0_compare(t0);
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_helper_mtc0_status(t0);
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Status";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_intctl(t0);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "IntCtl";
            break;
        case 2:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsctl(t0);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_mtc0_store32(t0, offsetof(CPUState, CP0_SRSMap));
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSMap";
            break;
        default:
            goto die;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_helper_mtc0_cause(t0);
            rn = "Cause";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, CP0_EPC));
            rn = "EPC";
            break;
        default:
            goto die;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "PRid";
            break;
        case 1:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_helper_mtc0_ebase(t0);
            rn = "EBase";
            break;
        default:
            goto die;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_helper_mtc0_config0(t0);
            rn = "Config";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 1:
            /* ignored */
            rn = "Config1";
            break;
        case 2:
            gen_helper_mtc0_config2(t0);
            rn = "Config2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 3:
            /* ignored */
            rn = "Config3";
            break;
        /* 6,7 are implementation dependent */
        default:
            rn = "Invalid config selector";
            goto die;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "LLAddr";
            break;
        default:
            goto die;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(mtc0_watchlo, t0, sel);
            rn = "WatchLo";
            break;
        default:
            goto die;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1i(mtc0_watchhi, t0, sel);
            rn = "WatchHi";
            break;
        default:
            goto die;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
            check_insn(env, ctx, ISA_MIPS3);
            gen_helper_mtc0_xcontext(t0);
            rn = "XContext";
            break;
        default:
            goto die;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        switch (sel) {
        case 0:
            gen_helper_mtc0_framemask(t0);
            rn = "Framemask";
            break;
        default:
            goto die;
        }
        break;
    case 22:
        /* ignored */
        rn = "Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mtc0_debug(t0); /* EJTAG support */
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Debug";
            break;
        case 1:
//            gen_helper_mtc0_tracecontrol(t0); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceControl";
//            break;
        case 2:
//            gen_helper_mtc0_tracecontrol2(t0); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceControl2";
//            break;
        case 3:
//            gen_helper_mtc0_usertracedata(t0); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "UserTraceData";
//            break;
        case 4:
//            gen_helper_mtc0_tracebpc(t0); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceBPC";
//            break;
        default:
            goto die;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, CP0_DEPC));
            rn = "DEPC";
            break;
        default:
            goto die;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_helper_mtc0_performance0(t0);
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_mtc0_performance1(t0);
            rn = "Performance1";
//            break;
        case 2:
//            gen_helper_mtc0_performance2(t0);
            rn = "Performance2";
//            break;
        case 3:
//            gen_helper_mtc0_performance3(t0);
            rn = "Performance3";
//            break;
        case 4:
//            gen_helper_mtc0_performance4(t0);
            rn = "Performance4";
//            break;
        case 5:
//            gen_helper_mtc0_performance5(t0);
            rn = "Performance5";
//            break;
        case 6:
//            gen_helper_mtc0_performance6(t0);
            rn = "Performance6";
//            break;
        case 7:
//            gen_helper_mtc0_performance7(t0);
            rn = "Performance7";
//            break;
        default:
            goto die;
        }
        break;
    case 26:
        /* ignored */
        rn = "ECC";
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            /* ignored */
            rn = "CacheErr";
            break;
        default:
            goto die;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taglo(t0);
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datalo(t0);
            rn = "DataLo";
            break;
        default:
            goto die;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taghi(t0);
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datahi(t0);
            rn = "DataHi";
            break;
        default:
            rn = "invalid sel";
            goto die;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_st_tl(t0, cpu_env, offsetof(CPUState, CP0_ErrorEPC));
            rn = "ErrorEPC";
            break;
        default:
            goto die;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mtc0_store32(t0, offsetof(CPUState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        default:
            goto die;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    default:
        goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    /* For simplicity assume that all writes can cause interrupts.  */
    if (use_icount) {
        gen_io_end();
        ctx->bstate = BS_STOP;
    }
    return;

die:
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "dmtc0 %s (reg %d sel %d)\n",
                rn, reg, sel);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}
#endif /* TARGET_MIPS64 */

static void gen_mftr(CPUState *env, DisasContext *ctx, int rt, int rd,
                     int u, int sel, int h)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    TCGv t0 = tcg_temp_local_new();

    if ((env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) == 0 &&
        ((env->tcs[other_tc].CP0_TCBind & (0xf << CP0TCBd_CurVPE)) !=
         (env->active_tc.CP0_TCBind & (0xf << CP0TCBd_CurVPE))))
        tcg_gen_movi_tl(t0, -1);
    else if ((env->CP0_VPEControl & (0xff << CP0VPECo_TargTC)) >
             (env->mvp->CP0_MVPConf0 & (0xff << CP0MVPC0_PTC)))
        tcg_gen_movi_tl(t0, -1);
    else if (u == 0) {
        switch (rt) {
        case 2:
            switch (sel) {
            case 1:
                gen_helper_mftc0_tcstatus(t0);
                break;
            case 2:
                gen_helper_mftc0_tcbind(t0);
                break;
            case 3:
                gen_helper_mftc0_tcrestart(t0);
                break;
            case 4:
                gen_helper_mftc0_tchalt(t0);
                break;
            case 5:
                gen_helper_mftc0_tccontext(t0);
                break;
            case 6:
                gen_helper_mftc0_tcschedule(t0);
                break;
            case 7:
                gen_helper_mftc0_tcschefback(t0);
                break;
            default:
                gen_mfc0(env, ctx, t0, rt, sel);
                break;
            }
            break;
        case 10:
            switch (sel) {
            case 0:
                gen_helper_mftc0_entryhi(t0);
                break;
            default:
                gen_mfc0(env, ctx, t0, rt, sel);
                break;
            }
        case 12:
            switch (sel) {
            case 0:
                gen_helper_mftc0_status(t0);
                break;
            default:
                gen_mfc0(env, ctx, t0, rt, sel);
                break;
            }
        case 23:
            switch (sel) {
            case 0:
                gen_helper_mftc0_debug(t0);
                break;
            default:
                gen_mfc0(env, ctx, t0, rt, sel);
                break;
            }
            break;
        default:
            gen_mfc0(env, ctx, t0, rt, sel);
        }
    } else switch (sel) {
    /* GPR registers. */
    case 0:
        gen_helper_1i(mftgpr, t0, rt);
        break;
    /* Auxiliary CPU registers */
    case 1:
        switch (rt) {
        case 0:
            gen_helper_1i(mftlo, t0, 0);
            break;
        case 1:
            gen_helper_1i(mfthi, t0, 0);
            break;
        case 2:
            gen_helper_1i(mftacx, t0, 0);
            break;
        case 4:
            gen_helper_1i(mftlo, t0, 1);
            break;
        case 5:
            gen_helper_1i(mfthi, t0, 1);
            break;
        case 6:
            gen_helper_1i(mftacx, t0, 1);
            break;
        case 8:
            gen_helper_1i(mftlo, t0, 2);
            break;
        case 9:
            gen_helper_1i(mfthi, t0, 2);
            break;
        case 10:
            gen_helper_1i(mftacx, t0, 2);
            break;
        case 12:
            gen_helper_1i(mftlo, t0, 3);
            break;
        case 13:
            gen_helper_1i(mfthi, t0, 3);
            break;
        case 14:
            gen_helper_1i(mftacx, t0, 3);
            break;
        case 16:
            gen_helper_mftdsp(t0);
            break;
        default:
            goto die;
        }
        break;
    /* Floating point (COP1). */
    case 2:
        /* XXX: For now we support only a single FPU context. */
        if (h == 0) {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, rt);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
        } else {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32h(fp0, rt);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
        }
        break;
    case 3:
        /* XXX: For now we support only a single FPU context. */
        gen_helper_1i(cfc1, t0, rt);
        break;
    /* COP2: Not implemented. */
    case 4:
    case 5:
        /* fall through */
    default:
        goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mftr (reg %d u %d sel %d h %d)\n",
                rt, u, sel, h);
    }
#endif
    gen_store_gpr(t0, rd);
    tcg_temp_free(t0);
    return;

die:
    tcg_temp_free(t0);
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mftr (reg %d u %d sel %d h %d)\n",
                rt, u, sel, h);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

static void gen_mttr(CPUState *env, DisasContext *ctx, int rd, int rt,
                     int u, int sel, int h)
{
    int other_tc = env->CP0_VPEControl & (0xff << CP0VPECo_TargTC);
    TCGv t0 = tcg_temp_local_new();

    gen_load_gpr(t0, rt);
    if ((env->CP0_VPEConf0 & (1 << CP0VPEC0_MVP)) == 0 &&
        ((env->tcs[other_tc].CP0_TCBind & (0xf << CP0TCBd_CurVPE)) !=
         (env->active_tc.CP0_TCBind & (0xf << CP0TCBd_CurVPE))))
        /* NOP */ ;
    else if ((env->CP0_VPEControl & (0xff << CP0VPECo_TargTC)) >
             (env->mvp->CP0_MVPConf0 & (0xff << CP0MVPC0_PTC)))
        /* NOP */ ;
    else if (u == 0) {
        switch (rd) {
        case 2:
            switch (sel) {
            case 1:
                gen_helper_mttc0_tcstatus(t0);
                break;
            case 2:
                gen_helper_mttc0_tcbind(t0);
                break;
            case 3:
                gen_helper_mttc0_tcrestart(t0);
                break;
            case 4:
                gen_helper_mttc0_tchalt(t0);
                break;
            case 5:
                gen_helper_mttc0_tccontext(t0);
                break;
            case 6:
                gen_helper_mttc0_tcschedule(t0);
                break;
            case 7:
                gen_helper_mttc0_tcschefback(t0);
                break;
            default:
                gen_mtc0(env, ctx, t0, rd, sel);
                break;
            }
            break;
        case 10:
            switch (sel) {
            case 0:
                gen_helper_mttc0_entryhi(t0);
                break;
            default:
                gen_mtc0(env, ctx, t0, rd, sel);
                break;
            }
        case 12:
            switch (sel) {
            case 0:
                gen_helper_mttc0_status(t0);
                break;
            default:
                gen_mtc0(env, ctx, t0, rd, sel);
                break;
            }
        case 23:
            switch (sel) {
            case 0:
                gen_helper_mttc0_debug(t0);
                break;
            default:
                gen_mtc0(env, ctx, t0, rd, sel);
                break;
            }
            break;
        default:
            gen_mtc0(env, ctx, t0, rd, sel);
        }
    } else switch (sel) {
    /* GPR registers. */
    case 0:
        gen_helper_1i(mttgpr, t0, rd);
        break;
    /* Auxiliary CPU registers */
    case 1:
        switch (rd) {
        case 0:
            gen_helper_1i(mttlo, t0, 0);
            break;
        case 1:
            gen_helper_1i(mtthi, t0, 0);
            break;
        case 2:
            gen_helper_1i(mttacx, t0, 0);
            break;
        case 4:
            gen_helper_1i(mttlo, t0, 1);
            break;
        case 5:
            gen_helper_1i(mtthi, t0, 1);
            break;
        case 6:
            gen_helper_1i(mttacx, t0, 1);
            break;
        case 8:
            gen_helper_1i(mttlo, t0, 2);
            break;
        case 9:
            gen_helper_1i(mtthi, t0, 2);
            break;
        case 10:
            gen_helper_1i(mttacx, t0, 2);
            break;
        case 12:
            gen_helper_1i(mttlo, t0, 3);
            break;
        case 13:
            gen_helper_1i(mtthi, t0, 3);
            break;
        case 14:
            gen_helper_1i(mttacx, t0, 3);
            break;
        case 16:
            gen_helper_mttdsp(t0);
            break;
        default:
            goto die;
        }
        break;
    /* Floating point (COP1). */
    case 2:
        /* XXX: For now we support only a single FPU context. */
        if (h == 0) {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32(fp0, rd);
            tcg_temp_free_i32(fp0);
        } else {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32h(fp0, rd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case 3:
        /* XXX: For now we support only a single FPU context. */
        gen_helper_1i(ctc1, t0, rd);
        break;
    /* COP2: Not implemented. */
    case 4:
    case 5:
        /* fall through */
    default:
        goto die;
    }
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mttr (reg %d u %d sel %d h %d)\n",
                rd, u, sel, h);
    }
#endif
    tcg_temp_free(t0);
    return;

die:
    tcg_temp_free(t0);
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "mttr (reg %d u %d sel %d h %d)\n",
                rd, u, sel, h);
    }
#endif
    generate_exception(ctx, EXCP_RI);
}

static void gen_cp0 (CPUState *env, DisasContext *ctx, uint32_t opc, int rt, int rd)
{
    const char *opn = "ldst";

    switch (opc) {
    case OPC_MFC0:
        if (rt == 0) {
            /* Treat as NOP. */
            return;
        }
        {
            TCGv t0 = tcg_temp_local_new();

            gen_mfc0(env, ctx, t0, rd, ctx->opcode & 0x7);
            gen_store_gpr(t0, rt);
            tcg_temp_free(t0);
        }
        opn = "mfc0";
        break;
    case OPC_MTC0:
        {
            TCGv t0 = tcg_temp_local_new();

            gen_load_gpr(t0, rt);
            save_cpu_state(ctx, 1);
            gen_mtc0(env, ctx, t0, rd, ctx->opcode & 0x7);
            tcg_temp_free(t0);
        }
        opn = "mtc0";
        break;
#if defined(TARGET_MIPS64)
    case OPC_DMFC0:
        check_insn(env, ctx, ISA_MIPS3);
        if (rt == 0) {
            /* Treat as NOP. */
            return;
        }
        {
            TCGv t0 = tcg_temp_local_new();

            gen_dmfc0(env, ctx, t0, rd, ctx->opcode & 0x7);
            gen_store_gpr(t0, rt);
            tcg_temp_free(t0);
        }
        opn = "dmfc0";
        break;
    case OPC_DMTC0:
        check_insn(env, ctx, ISA_MIPS3);
        {
            TCGv t0 = tcg_temp_local_new();

            gen_load_gpr(t0, rt);
            save_cpu_state(ctx, 1);
            gen_dmtc0(env, ctx, t0, rd, ctx->opcode & 0x7);
            tcg_temp_free(t0);
        }
        opn = "dmtc0";
        break;
#endif
    case OPC_MFTR:
        check_insn(env, ctx, ASE_MT);
        if (rd == 0) {
            /* Treat as NOP. */
            return;
        }
        gen_mftr(env, ctx, rt, rd, (ctx->opcode >> 5) & 1,
                 ctx->opcode & 0x7, (ctx->opcode >> 4) & 1);
        opn = "mftr";
        break;
    case OPC_MTTR:
        check_insn(env, ctx, ASE_MT);
        gen_mttr(env, ctx, rd, rt, (ctx->opcode >> 5) & 1,
                 ctx->opcode & 0x7, (ctx->opcode >> 4) & 1);
        opn = "mttr";
        break;
    case OPC_TLBWI:
        opn = "tlbwi";
        if (!env->tlb->do_tlbwi)
            goto die;
        gen_helper_tlbwi();
        break;
    case OPC_TLBWR:
        opn = "tlbwr";
        if (!env->tlb->do_tlbwr)
            goto die;
        gen_helper_tlbwr();
        break;
    case OPC_TLBP:
        opn = "tlbp";
        if (!env->tlb->do_tlbp)
            goto die;
        gen_helper_tlbp();
        break;
    case OPC_TLBR:
        opn = "tlbr";
        if (!env->tlb->do_tlbr)
            goto die;
        gen_helper_tlbr();
        break;
    case OPC_ERET:
        opn = "eret";
        check_insn(env, ctx, ISA_MIPS2);
        save_cpu_state(ctx, 1);
        gen_helper_eret();
        ctx->bstate = BS_EXCP;
        break;
    case OPC_DERET:
        opn = "deret";
        check_insn(env, ctx, ISA_MIPS32);
        if (!(ctx->hflags & MIPS_HFLAG_DM)) {
            MIPS_INVAL(opn);
            generate_exception(ctx, EXCP_RI);
        } else {
            save_cpu_state(ctx, 1);
            gen_helper_deret();
            ctx->bstate = BS_EXCP;
        }
        break;
    case OPC_WAIT:
        opn = "wait";
        check_insn(env, ctx, ISA_MIPS3 | ISA_MIPS32);
        /* If we get an exception, we want to restart at next instruction */
        ctx->pc += 4;
        save_cpu_state(ctx, 1);
        ctx->pc -= 4;
        gen_helper_wait();
        ctx->bstate = BS_EXCP;
        break;
    default:
 die:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s %d", opn, regnames[rt], rd);
}
#endif /* !CONFIG_USER_ONLY */

/* CP1 Branches (before delay slot) */
static void gen_compute_branch1 (CPUState *env, DisasContext *ctx, uint32_t op,
                                 int32_t cc, int32_t offset)
{
    target_ulong btarget;
    const char *opn = "cp1 cond branch";
    TCGv_i32 t0 = tcg_temp_new_i32();

    if (cc != 0)
        check_insn(env, ctx, ISA_MIPS4 | ISA_MIPS32);

    btarget = ctx->pc + 4 + offset;

    switch (op) {
    case OPC_BC1F:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0x1 << cc);
            tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1f";
        goto not_likely;
    case OPC_BC1FL:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0x1 << cc);
            tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1fl";
        goto likely;
    case OPC_BC1T:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0x1 << cc);
            tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1t";
        goto not_likely;
    case OPC_BC1TL:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0x1 << cc);
            tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1tl";
    likely:
        ctx->hflags |= MIPS_HFLAG_BL;
        break;
    case OPC_BC1FANY2:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0x3 << cc);
            tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1any2f";
        goto not_likely;
    case OPC_BC1TANY2:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0x3 << cc);
            tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1any2t";
        goto not_likely;
    case OPC_BC1FANY4:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0xf << cc);
            tcg_gen_brcondi_i32(TCG_COND_EQ, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1any4f";
        goto not_likely;
    case OPC_BC1TANY4:
        {
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            get_fp_cond(t0);
            tcg_gen_andi_i32(t0, t0, 0xf << cc);
            tcg_gen_brcondi_i32(TCG_COND_NE, t0, 0, l1);
            tcg_gen_movi_i32(bcond, 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_movi_i32(bcond, 1);
            gen_set_label(l2);
        }
        opn = "bc1any4t";
    not_likely:
        ctx->hflags |= MIPS_HFLAG_BC;
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        goto out;
    }
    MIPS_DEBUG("%s: cond %02x target " TARGET_FMT_lx, opn,
               ctx->hflags, btarget);
    ctx->btarget = btarget;

 out:
    tcg_temp_free_i32(t0);
}

/* Coprocessor 1 (FPU) */

#define FOP(func, fmt) (((fmt) << 21) | (func))

static void gen_cp1 (DisasContext *ctx, uint32_t opc, int rt, int fs)
{
    const char *opn = "cp1 move";
    TCGv t0 = tcg_temp_local_new();

    switch (opc) {
    case OPC_MFC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
	}
        gen_store_gpr(t0, rt);
        opn = "mfc1";
        break;
    case OPC_MTC1:
        gen_load_gpr(t0, rt);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32(fp0, fs);
            tcg_temp_free_i32(fp0);
	}
        opn = "mtc1";
        break;
    case OPC_CFC1:
        gen_helper_1i(cfc1, t0, fs);
        gen_store_gpr(t0, rt);
        opn = "cfc1";
        break;
    case OPC_CTC1:
        gen_load_gpr(t0, rt);
        gen_helper_1i(ctc1, t0, fs);
        opn = "ctc1";
        break;
    case OPC_DMFC1:
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            tcg_gen_trunc_i64_tl(t0, fp0);
            tcg_temp_free_i64(fp0);
	}
        gen_store_gpr(t0, rt);
        opn = "dmfc1";
        break;
    case OPC_DMTC1:
        gen_load_gpr(t0, rt);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            tcg_gen_extu_tl_i64(fp0, t0);
            gen_store_fpr64(ctx, fp0, fs);
            tcg_temp_free_i64(fp0);
	}
        opn = "dmtc1";
        break;
    case OPC_MFHC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32h(fp0, fs);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
	}
        gen_store_gpr(t0, rt);
        opn = "mfhc1";
        break;
    case OPC_MTHC1:
        gen_load_gpr(t0, rt);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32h(fp0, fs);
            tcg_temp_free_i32(fp0);
	}
        opn = "mthc1";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        goto out;
    }
    MIPS_DEBUG("%s %s %s", opn, regnames[rt], fregnames[fs]);

 out:
    tcg_temp_free(t0);
}

static void gen_movci (DisasContext *ctx, int rd, int rs, int cc, int tf)
{
    int l1 = gen_new_label();
    uint32_t ccbit;
    TCGCond cond;
    TCGv t0 = tcg_temp_local_new();
    TCGv_i32 r_tmp = tcg_temp_new_i32();

    if (cc)
        ccbit = 1 << (24 + cc);
    else
        ccbit = 1 << 23;
    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    gen_load_gpr(t0, rd);
    tcg_gen_andi_i32(r_tmp, fpu_fcr31, ccbit);
    tcg_gen_brcondi_i32(cond, r_tmp, 0, l1);
    tcg_temp_free_i32(r_tmp);
    gen_load_gpr(t0, rs);
    gen_set_label(l1);
    gen_store_gpr(t0, rd);
    tcg_temp_free(t0);
}

static inline void gen_movcf_s (int fs, int fd, int cc, int tf)
{
    uint32_t ccbit;
    int cond;
    TCGv_i32 r_tmp1 = tcg_temp_new_i32();
    TCGv_i32 fp0 = tcg_temp_local_new_i32();
    int l1 = gen_new_label();

    if (cc)
        ccbit = 1 << (24 + cc);
    else
        ccbit = 1 << 23;

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    gen_load_fpr32(fp0, fd);
    tcg_gen_andi_i32(r_tmp1, fpu_fcr31, ccbit);
    tcg_gen_brcondi_i32(cond, r_tmp1, 0, l1);
    tcg_temp_free_i32(r_tmp1);
    gen_load_fpr32(fp0, fs);
    gen_set_label(l1);
    gen_store_fpr32(fp0, fd);
    tcg_temp_free_i32(fp0);
}

static inline void gen_movcf_d (DisasContext *ctx, int fs, int fd, int cc, int tf)
{
    uint32_t ccbit;
    int cond;
    TCGv_i32 r_tmp1 = tcg_temp_new_i32();
    TCGv_i64 fp0 = tcg_temp_local_new_i64();
    int l1 = gen_new_label();

    if (cc)
        ccbit = 1 << (24 + cc);
    else
        ccbit = 1 << 23;

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    gen_load_fpr64(ctx, fp0, fd);
    tcg_gen_andi_i32(r_tmp1, fpu_fcr31, ccbit);
    tcg_gen_brcondi_i32(cond, r_tmp1, 0, l1);
    tcg_temp_free_i32(r_tmp1);
    gen_load_fpr64(ctx, fp0, fs);
    gen_set_label(l1);
    gen_store_fpr64(ctx, fp0, fd);
    tcg_temp_free_i64(fp0);
}

static inline void gen_movcf_ps (int fs, int fd, int cc, int tf)
{
    uint32_t ccbit1, ccbit2;
    int cond;
    TCGv_i32 r_tmp1 = tcg_temp_new_i32();
    TCGv_i32 fp0 = tcg_temp_local_new_i32();
    int l1 = gen_new_label();
    int l2 = gen_new_label();

    if (cc) {
        ccbit1 = 1 << (24 + cc);
        ccbit2 = 1 << (25 + cc);
    } else {
        ccbit1 = 1 << 23;
        ccbit2 = 1 << 25;
    }

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    gen_load_fpr32(fp0, fd);
    tcg_gen_andi_i32(r_tmp1, fpu_fcr31, ccbit1);
    tcg_gen_brcondi_i32(cond, r_tmp1, 0, l1);
    gen_load_fpr32(fp0, fs);
    gen_set_label(l1);
    gen_store_fpr32(fp0, fd);

    gen_load_fpr32h(fp0, fd);
    tcg_gen_andi_i32(r_tmp1, fpu_fcr31, ccbit2);
    tcg_gen_brcondi_i32(cond, r_tmp1, 0, l2);
    gen_load_fpr32h(fp0, fs);
    gen_set_label(l2);
    gen_store_fpr32h(fp0, fd);

    tcg_temp_free_i32(r_tmp1);
    tcg_temp_free_i32(fp0);
}


static void gen_farith (DisasContext *ctx, uint32_t op1,
                        int ft, int fs, int fd, int cc)
{
    const char *opn = "farith";
    const char *condnames[] = {
            "c.f",
            "c.un",
            "c.eq",
            "c.ueq",
            "c.olt",
            "c.ult",
            "c.ole",
            "c.ule",
            "c.sf",
            "c.ngle",
            "c.seq",
            "c.ngl",
            "c.lt",
            "c.nge",
            "c.le",
            "c.ngt",
    };
    const char *condnames_abs[] = {
            "cabs.f",
            "cabs.un",
            "cabs.eq",
            "cabs.ueq",
            "cabs.olt",
            "cabs.ult",
            "cabs.ole",
            "cabs.ule",
            "cabs.sf",
            "cabs.ngle",
            "cabs.seq",
            "cabs.ngl",
            "cabs.lt",
            "cabs.nge",
            "cabs.le",
            "cabs.ngt",
    };
    enum { BINOP, CMPOP, OTHEROP } optype = OTHEROP;
    uint32_t func = ctx->opcode & 0x3f;

    switch (ctx->opcode & FOP(0x3f, 0x1f)) {
    case FOP(0, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_helper_float_add_s(fp0, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "add.s";
        optype = BINOP;
        break;
    case FOP(1, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_helper_float_sub_s(fp0, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "sub.s";
        optype = BINOP;
        break;
    case FOP(2, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_helper_float_mul_s(fp0, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "mul.s";
        optype = BINOP;
        break;
    case FOP(3, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_helper_float_div_s(fp0, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "div.s";
        optype = BINOP;
        break;
    case FOP(4, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_sqrt_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "sqrt.s";
        break;
    case FOP(5, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_abs_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "abs.s";
        break;
    case FOP(6, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "mov.s";
        break;
    case FOP(7, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_chs_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "neg.s";
        break;
    case FOP(8, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_roundl_s(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "round.l.s";
        break;
    case FOP(9, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_truncl_s(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "trunc.l.s";
        break;
    case FOP(10, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_ceill_s(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "ceil.l.s";
        break;
    case FOP(11, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_floorl_s(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "floor.l.s";
        break;
    case FOP(12, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_roundw_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "round.w.s";
        break;
    case FOP(13, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_truncw_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "trunc.w.s";
        break;
    case FOP(14, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_ceilw_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "ceil.w.s";
        break;
    case FOP(15, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_floorw_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "floor.w.s";
        break;
    case FOP(17, 16):
        gen_movcf_s(fs, fd, (ft >> 2) & 0x7, ft & 0x1);
        opn = "movcf.s";
        break;
    case FOP(18, 16):
        {
            int l1 = gen_new_label();
            TCGv t0 = tcg_temp_new();
            TCGv_i32 fp0 = tcg_temp_local_new_i32();

            gen_load_gpr(t0, ft);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
            gen_load_fpr32(fp0, fs);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
            gen_set_label(l1);
            tcg_temp_free(t0);
        }
        opn = "movz.s";
        break;
    case FOP(19, 16):
        {
            int l1 = gen_new_label();
            TCGv t0 = tcg_temp_new();
            TCGv_i32 fp0 = tcg_temp_local_new_i32();

            gen_load_gpr(t0, ft);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
            gen_load_fpr32(fp0, fs);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
            gen_set_label(l1);
            tcg_temp_free(t0);
        }
        opn = "movn.s";
        break;
    case FOP(21, 16):
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_recip_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "recip.s";
        break;
    case FOP(22, 16):
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_rsqrt_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "rsqrt.s";
        break;
    case FOP(28, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, fd);
            gen_helper_float_recip2_s(fp0, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "recip2.s";
        break;
    case FOP(29, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_recip1_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "recip1.s";
        break;
    case FOP(30, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_rsqrt1_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "rsqrt1.s";
        break;
    case FOP(31, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_helper_float_rsqrt2_s(fp0, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "rsqrt2.s";
        break;
    case FOP(33, 16):
        check_cp1_registers(ctx, fd);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_cvtd_s(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "cvt.d.s";
        break;
    case FOP(36, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_cvtw_s(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "cvt.w.s";
        break;
    case FOP(37, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_cvtl_s(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "cvt.l.s";
        break;
    case FOP(38, 16):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp64 = tcg_temp_new_i64();
            TCGv_i32 fp32_0 = tcg_temp_new_i32();
            TCGv_i32 fp32_1 = tcg_temp_new_i32();

            gen_load_fpr32(fp32_0, fs);
            gen_load_fpr32(fp32_1, ft);
            tcg_gen_concat_i32_i64(fp64, fp32_0, fp32_1);
            tcg_temp_free_i32(fp32_1);
            tcg_temp_free_i32(fp32_0);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "cvt.ps.s";
        break;
    case FOP(48, 16):
    case FOP(49, 16):
    case FOP(50, 16):
    case FOP(51, 16):
    case FOP(52, 16):
    case FOP(53, 16):
    case FOP(54, 16):
    case FOP(55, 16):
    case FOP(56, 16):
    case FOP(57, 16):
    case FOP(58, 16):
    case FOP(59, 16):
    case FOP(60, 16):
    case FOP(61, 16):
    case FOP(62, 16):
    case FOP(63, 16):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            if (ctx->opcode & (1 << 6)) {
                check_cop1x(ctx);
                gen_cmpabs_s(func-48, fp0, fp1, cc);
                opn = condnames_abs[func-48];
            } else {
                gen_cmp_s(func-48, fp0, fp1, cc);
                opn = condnames[func-48];
            }
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        break;
    case FOP(0, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_add_d(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "add.d";
        optype = BINOP;
        break;
    case FOP(1, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_sub_d(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "sub.d";
        optype = BINOP;
        break;
    case FOP(2, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_mul_d(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "mul.d";
        optype = BINOP;
        break;
    case FOP(3, 17):
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_div_d(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "div.d";
        optype = BINOP;
        break;
    case FOP(4, 17):
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_sqrt_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "sqrt.d";
        break;
    case FOP(5, 17):
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_abs_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "abs.d";
        break;
    case FOP(6, 17):
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "mov.d";
        break;
    case FOP(7, 17):
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_chs_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "neg.d";
        break;
    case FOP(8, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_roundl_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "round.l.d";
        break;
    case FOP(9, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_truncl_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "trunc.l.d";
        break;
    case FOP(10, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_ceill_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "ceil.l.d";
        break;
    case FOP(11, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_floorl_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "floor.l.d";
        break;
    case FOP(12, 17):
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_roundw_d(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "round.w.d";
        break;
    case FOP(13, 17):
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_truncw_d(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "trunc.w.d";
        break;
    case FOP(14, 17):
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_ceilw_d(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "ceil.w.d";
        break;
    case FOP(15, 17):
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_floorw_d(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "floor.w.d";
        break;
    case FOP(17, 17):
        gen_movcf_d(ctx, fs, fd, (ft >> 2) & 0x7, ft & 0x1);
        opn = "movcf.d";
        break;
    case FOP(18, 17):
        {
            int l1 = gen_new_label();
            TCGv t0 = tcg_temp_new();
            TCGv_i64 fp0 = tcg_temp_local_new_i64();

            gen_load_gpr(t0, ft);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
            gen_set_label(l1);
            tcg_temp_free(t0);
        }
        opn = "movz.d";
        break;
    case FOP(19, 17):
        {
            int l1 = gen_new_label();
            TCGv t0 = tcg_temp_new();
            TCGv_i64 fp0 = tcg_temp_local_new_i64();

            gen_load_gpr(t0, ft);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
            gen_set_label(l1);
            tcg_temp_free(t0);
        }
        opn = "movn.d";
        break;
    case FOP(21, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_recip_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "recip.d";
        break;
    case FOP(22, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_rsqrt_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "rsqrt.d";
        break;
    case FOP(28, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_recip2_d(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "recip2.d";
        break;
    case FOP(29, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_recip1_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "recip1.d";
        break;
    case FOP(30, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_rsqrt1_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "rsqrt1.d";
        break;
    case FOP(31, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_rsqrt2_d(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "rsqrt2.d";
        break;
    case FOP(48, 17):
    case FOP(49, 17):
    case FOP(50, 17):
    case FOP(51, 17):
    case FOP(52, 17):
    case FOP(53, 17):
    case FOP(54, 17):
    case FOP(55, 17):
    case FOP(56, 17):
    case FOP(57, 17):
    case FOP(58, 17):
    case FOP(59, 17):
    case FOP(60, 17):
    case FOP(61, 17):
    case FOP(62, 17):
    case FOP(63, 17):
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            if (ctx->opcode & (1 << 6)) {
                check_cop1x(ctx);
                check_cp1_registers(ctx, fs | ft);
                gen_cmpabs_d(func-48, fp0, fp1, cc);
                opn = condnames_abs[func-48];
            } else {
                check_cp1_registers(ctx, fs | ft);
                gen_cmp_d(func-48, fp0, fp1, cc);
                opn = condnames[func-48];
            }
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
        }
        break;
    case FOP(32, 17):
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_cvts_d(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "cvt.s.d";
        break;
    case FOP(36, 17):
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_cvtw_d(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "cvt.w.d";
        break;
    case FOP(37, 17):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtl_d(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "cvt.l.d";
        break;
    case FOP(32, 20):
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_cvts_w(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "cvt.s.w";
        break;
    case FOP(33, 20):
        check_cp1_registers(ctx, fd);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(fp32, fs);
            gen_helper_float_cvtd_w(fp64, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        opn = "cvt.d.w";
        break;
    case FOP(32, 21):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_cvts_l(fp32, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        opn = "cvt.s.l";
        break;
    case FOP(33, 21):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtd_l(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "cvt.d.l";
        break;
    case FOP(38, 20):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtps_pw(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "cvt.ps.pw";
        break;
    case FOP(0, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_add_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "add.ps";
        break;
    case FOP(1, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_sub_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "sub.ps";
        break;
    case FOP(2, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_mul_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "mul.ps";
        break;
    case FOP(5, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_abs_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "abs.ps";
        break;
    case FOP(6, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "mov.ps";
        break;
    case FOP(7, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_chs_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "neg.ps";
        break;
    case FOP(17, 22):
        check_cp1_64bitmode(ctx);
        gen_movcf_ps(fs, fd, (ft >> 2) & 0x7, ft & 0x1);
        opn = "movcf.ps";
        break;
    case FOP(18, 22):
        check_cp1_64bitmode(ctx);
        {
            int l1 = gen_new_label();
            TCGv t0 = tcg_temp_new();
            TCGv_i32 fp0 = tcg_temp_local_new_i32();
            TCGv_i32 fph0 = tcg_temp_local_new_i32();

            gen_load_gpr(t0, ft);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
            gen_load_fpr32(fp0, fs);
            gen_load_fpr32h(fph0, fs);
            gen_store_fpr32(fp0, fd);
            gen_store_fpr32h(fph0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fph0);
            gen_set_label(l1);
            tcg_temp_free(t0);
        }
        opn = "movz.ps";
        break;
    case FOP(19, 22):
        check_cp1_64bitmode(ctx);
        {
            int l1 = gen_new_label();
            TCGv t0 = tcg_temp_new();
            TCGv_i32 fp0 = tcg_temp_local_new_i32();
            TCGv_i32 fph0 = tcg_temp_local_new_i32();

            gen_load_gpr(t0, ft);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);
            gen_load_fpr32(fp0, fs);
            gen_load_fpr32h(fph0, fs);
            gen_store_fpr32(fp0, fd);
            gen_store_fpr32h(fph0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fph0);
            gen_set_label(l1);
            tcg_temp_free(t0);
        }
        opn = "movn.ps";
        break;
    case FOP(24, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, ft);
            gen_load_fpr64(ctx, fp1, fs);
            gen_helper_float_addr_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "addr.ps";
        break;
    case FOP(26, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, ft);
            gen_load_fpr64(ctx, fp1, fs);
            gen_helper_float_mulr_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "mulr.ps";
        break;
    case FOP(28, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, fd);
            gen_helper_float_recip2_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "recip2.ps";
        break;
    case FOP(29, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_recip1_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "recip1.ps";
        break;
    case FOP(30, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_rsqrt1_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "rsqrt1.ps";
        break;
    case FOP(31, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_rsqrt2_ps(fp0, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "rsqrt2.ps";
        break;
    case FOP(32, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32h(fp0, fs);
            gen_helper_float_cvts_pu(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "cvt.s.pu";
        break;
    case FOP(36, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtpw_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "cvt.pw.ps";
        break;
    case FOP(40, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_helper_float_cvts_pl(fp0, fp0);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "cvt.s.pl";
        break;
    case FOP(44, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_store_fpr32h(fp0, fd);
            gen_store_fpr32(fp1, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        opn = "pll.ps";
        break;
    case FOP(45, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32h(fp1, ft);
            gen_store_fpr32(fp1, fd);
            gen_store_fpr32h(fp0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        opn = "plu.ps";
        break;
    case FOP(46, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32h(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_store_fpr32(fp1, fd);
            gen_store_fpr32h(fp0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        opn = "pul.ps";
        break;
    case FOP(47, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32h(fp0, fs);
            gen_load_fpr32h(fp1, ft);
            gen_store_fpr32(fp1, fd);
            gen_store_fpr32h(fp0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        opn = "puu.ps";
        break;
    case FOP(48, 22):
    case FOP(49, 22):
    case FOP(50, 22):
    case FOP(51, 22):
    case FOP(52, 22):
    case FOP(53, 22):
    case FOP(54, 22):
    case FOP(55, 22):
    case FOP(56, 22):
    case FOP(57, 22):
    case FOP(58, 22):
    case FOP(59, 22):
    case FOP(60, 22):
    case FOP(61, 22):
    case FOP(62, 22):
    case FOP(63, 22):
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            if (ctx->opcode & (1 << 6)) {
                gen_cmpabs_ps(func-48, fp0, fp1, cc);
                opn = condnames_abs[func-48];
            } else {
                gen_cmp_ps(func-48, fp0, fp1, cc);
                opn = condnames[func-48];
            }
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
        }
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        return;
    }
    switch (optype) {
    case BINOP:
        MIPS_DEBUG("%s %s, %s, %s", opn, fregnames[fd], fregnames[fs], fregnames[ft]);
        break;
    case CMPOP:
        MIPS_DEBUG("%s %s,%s", opn, fregnames[fs], fregnames[ft]);
        break;
    default:
        MIPS_DEBUG("%s %s,%s", opn, fregnames[fd], fregnames[fs]);
        break;
    }
}

/* Coprocessor 3 (FPU) */
static void gen_flt3_ldst (DisasContext *ctx, uint32_t opc,
                           int fd, int fs, int base, int index)
{
    const char *opn = "extended float load/store";
    int store = 0;
    TCGv t0 = tcg_temp_local_new();
    TCGv t1 = tcg_temp_local_new();

    if (base == 0) {
        gen_load_gpr(t0, index);
    } else if (index == 0) {
        gen_load_gpr(t0, base);
    } else {
        gen_load_gpr(t0, base);
        gen_load_gpr(t1, index);
        gen_op_addr_add(ctx, t0, t1);
    }
    /* Don't do NOP if destination is zero: we must perform the actual
       memory access. */
    switch (opc) {
    case OPC_LWXC1:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_qemu_ld32s(t1, t0, ctx->mem_idx);
            tcg_gen_trunc_tl_i32(fp0, t1);
            gen_store_fpr32(fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        opn = "lwxc1";
        break;
    case OPC_LDXC1:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            tcg_gen_qemu_ld64(fp0, t0, ctx->mem_idx);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "ldxc1";
        break;
    case OPC_LUXC1:
        check_cp1_64bitmode(ctx);
        tcg_gen_andi_tl(t0, t0, ~0x7);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            tcg_gen_qemu_ld64(fp0, t0, ctx->mem_idx);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        opn = "luxc1";
        break;
    case OPC_SWXC1:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            tcg_gen_extu_i32_tl(t1, fp0);
            tcg_gen_qemu_st32(t1, t0, ctx->mem_idx);
            tcg_temp_free_i32(fp0);
        }
        opn = "swxc1";
        store = 1;
        break;
    case OPC_SDXC1:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fs);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            tcg_gen_qemu_st64(fp0, t0, ctx->mem_idx);
            tcg_temp_free_i64(fp0);
        }
        opn = "sdxc1";
        store = 1;
        break;
    case OPC_SUXC1:
        check_cp1_64bitmode(ctx);
        tcg_gen_andi_tl(t0, t0, ~0x7);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            tcg_gen_qemu_st64(fp0, t0, ctx->mem_idx);
            tcg_temp_free_i64(fp0);
        }
        opn = "suxc1";
        store = 1;
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception(ctx, EXCP_RI);
        tcg_temp_free(t0);
        tcg_temp_free(t1);
        return;
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    MIPS_DEBUG("%s %s, %s(%s)", opn, fregnames[store ? fs : fd],
               regnames[index], regnames[base]);
}

static void gen_flt3_arith (DisasContext *ctx, uint32_t opc,
                            int fd, int fr, int fs, int ft)
{
    const char *opn = "flt3_arith";

    switch (opc) {
    case OPC_ALNV_PS:
        check_cp1_64bitmode(ctx);
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv_i32 fp0 = tcg_temp_local_new_i32();
            TCGv_i32 fph0 = tcg_temp_local_new_i32();
            TCGv_i32 fp1 = tcg_temp_local_new_i32();
            TCGv_i32 fph1 = tcg_temp_local_new_i32();
            int l1 = gen_new_label();
            int l2 = gen_new_label();

            gen_load_gpr(t0, fr);
            tcg_gen_andi_tl(t0, t0, 0x7);
            gen_load_fpr32(fp0, fs);
            gen_load_fpr32h(fph0, fs);
            gen_load_fpr32(fp1, ft);
            gen_load_fpr32h(fph1, ft);

            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
            gen_store_fpr32(fp0, fd);
            gen_store_fpr32h(fph0, fd);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 4, l2);
            tcg_temp_free(t0);
#ifdef TARGET_WORDS_BIGENDIAN
            gen_store_fpr32(fph1, fd);
            gen_store_fpr32h(fp0, fd);
#else
            gen_store_fpr32(fph0, fd);
            gen_store_fpr32h(fp1, fd);
#endif
            gen_set_label(l2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fph0);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fph1);
        }
        opn = "alnv.ps";
        break;
    case OPC_MADD_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_load_fpr32(fp2, fr);
            gen_helper_float_muladd_s(fp2, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp2, fd);
            tcg_temp_free_i32(fp2);
        }
        opn = "madd.s";
        break;
    case OPC_MADD_D:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fd | fs | ft | fr);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_muladd_d(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "madd.d";
        break;
    case OPC_MADD_PS:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_muladd_ps(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "madd.ps";
        break;
    case OPC_MSUB_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_load_fpr32(fp2, fr);
            gen_helper_float_mulsub_s(fp2, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp2, fd);
            tcg_temp_free_i32(fp2);
        }
        opn = "msub.s";
        break;
    case OPC_MSUB_D:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fd | fs | ft | fr);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_mulsub_d(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "msub.d";
        break;
    case OPC_MSUB_PS:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_mulsub_ps(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "msub.ps";
        break;
    case OPC_NMADD_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_load_fpr32(fp2, fr);
            gen_helper_float_nmuladd_s(fp2, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp2, fd);
            tcg_temp_free_i32(fp2);
        }
        opn = "nmadd.s";
        break;
    case OPC_NMADD_D:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fd | fs | ft | fr);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_nmuladd_d(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "nmadd.d";
        break;
    case OPC_NMADD_PS:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_nmuladd_ps(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "nmadd.ps";
        break;
    case OPC_NMSUB_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(fp0, fs);
            gen_load_fpr32(fp1, ft);
            gen_load_fpr32(fp2, fr);
            gen_helper_float_nmulsub_s(fp2, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(fp2, fd);
            tcg_temp_free_i32(fp2);
        }
        opn = "nmsub.s";
        break;
    case OPC_NMSUB_D:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fd | fs | ft | fr);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_nmulsub_d(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "nmsub.d";
        break;
    case OPC_NMSUB_PS:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_nmulsub_ps(fp2, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        opn = "nmsub.ps";
        break;
    default:
        MIPS_INVAL(opn);
        generate_exception (ctx, EXCP_RI);
        return;
    }
    MIPS_DEBUG("%s %s, %s, %s, %s", opn, fregnames[fd], fregnames[fr],
               fregnames[fs], fregnames[ft]);
}

/* ISA extensions (ASEs) */
/* MIPS16 extension to MIPS32 */
/* SmartMIPS extension to MIPS32 */

#if defined(TARGET_MIPS64)

/* MDMX extension to MIPS64 */

#endif

static void decode_opc (CPUState *env, DisasContext *ctx)
{
    int32_t offset;
    int rs, rt, rd, sa;
    uint32_t op, op1, op2;
    int16_t imm;

    /* make sure instructions are on a word boundary */
    if (ctx->pc & 0x3) {
        env->CP0_BadVAddr = ctx->pc;
        generate_exception(ctx, EXCP_AdEL);
        return;
    }

    /* Handle blikely not taken case */
    if ((ctx->hflags & MIPS_HFLAG_BMASK) == MIPS_HFLAG_BL) {
        int l1 = gen_new_label();

        MIPS_DEBUG("blikely condition (" TARGET_FMT_lx ")", ctx->pc + 4);
        tcg_gen_brcondi_i32(TCG_COND_NE, bcond, 0, l1);
        {
            TCGv_i32 r_tmp = tcg_temp_new_i32();

            tcg_gen_movi_i32(r_tmp, ctx->hflags & ~MIPS_HFLAG_BMASK);
            tcg_gen_st_i32(r_tmp, cpu_env, offsetof(CPUState, hflags));
            tcg_temp_free_i32(r_tmp);
        }
        gen_goto_tb(ctx, 1, ctx->pc + 4);
        gen_set_label(l1);
    }
    op = MASK_OP_MAJOR(ctx->opcode);
    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;
    imm = (int16_t)ctx->opcode;
    switch (op) {
    case OPC_SPECIAL:
        op1 = MASK_SPECIAL(ctx->opcode);
        switch (op1) {
        case OPC_SLL:          /* Arithmetic with immediate */
        case OPC_SRL ... OPC_SRA:
            gen_arith_imm(env, ctx, op1, rd, rt, sa);
            break;
        case OPC_MOVZ ... OPC_MOVN:
            check_insn(env, ctx, ISA_MIPS4 | ISA_MIPS32);
        case OPC_SLLV:         /* Arithmetic */
        case OPC_SRLV ... OPC_SRAV:
        case OPC_ADD ... OPC_NOR:
        case OPC_SLT ... OPC_SLTU:
            gen_arith(env, ctx, op1, rd, rs, rt);
            break;
        case OPC_MULT ... OPC_DIVU:
            if (sa) {
                check_insn(env, ctx, INSN_VR54XX);
                op1 = MASK_MUL_VR54XX(ctx->opcode);
                gen_mul_vr54xx(ctx, op1, rd, rs, rt);
            } else
                gen_muldiv(ctx, op1, rs, rt);
            break;
        case OPC_JR ... OPC_JALR:
            gen_compute_branch(ctx, op1, rs, rd, sa);
            return;
        case OPC_TGE ... OPC_TEQ: /* Traps */
        case OPC_TNE:
            gen_trap(ctx, op1, rs, rt, -1);
            break;
        case OPC_MFHI:          /* Move from HI/LO */
        case OPC_MFLO:
            gen_HILO(ctx, op1, rd);
            break;
        case OPC_MTHI:
        case OPC_MTLO:          /* Move to HI/LO */
            gen_HILO(ctx, op1, rs);
            break;
        case OPC_PMON:          /* Pmon entry point, also R4010 selsl */
#ifdef MIPS_STRICT_STANDARD
            MIPS_INVAL("PMON / selsl");
            generate_exception(ctx, EXCP_RI);
#else
            gen_helper_0i(pmon, sa);
#endif
            break;
        case OPC_SYSCALL:
            generate_exception(ctx, EXCP_SYSCALL);
            break;
        case OPC_BREAK:
            generate_exception(ctx, EXCP_BREAK);
            break;
        case OPC_SPIM:
#ifdef MIPS_STRICT_STANDARD
            MIPS_INVAL("SPIM");
            generate_exception(ctx, EXCP_RI);
#else
           /* Implemented as RI exception for now. */
            MIPS_INVAL("spim (unofficial)");
            generate_exception(ctx, EXCP_RI);
#endif
            break;
        case OPC_SYNC:
            /* Treat as NOP. */
            break;

        case OPC_MOVCI:
            check_insn(env, ctx, ISA_MIPS4 | ISA_MIPS32);
            if (env->CP0_Config1 & (1 << CP0C1_FP)) {
                save_cpu_state(ctx, 1);
                check_cp1_enabled(ctx);
                gen_movci(ctx, rd, rs, (ctx->opcode >> 18) & 0x7,
                          (ctx->opcode >> 16) & 1);
            } else {
                generate_exception_err(ctx, EXCP_CpU, 1);
            }
            break;

#if defined(TARGET_MIPS64)
       /* MIPS64 specific opcodes */
        case OPC_DSLL:
        case OPC_DSRL ... OPC_DSRA:
        case OPC_DSLL32:
        case OPC_DSRL32 ... OPC_DSRA32:
            check_insn(env, ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_arith_imm(env, ctx, op1, rd, rt, sa);
            break;
        case OPC_DSLLV:
        case OPC_DSRLV ... OPC_DSRAV:
        case OPC_DADD ... OPC_DSUBU:
            check_insn(env, ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_arith(env, ctx, op1, rd, rs, rt);
            break;
        case OPC_DMULT ... OPC_DDIVU:
            check_insn(env, ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, op1, rs, rt);
            break;
#endif
        default:            /* Invalid */
            MIPS_INVAL("special");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SPECIAL2:
        op1 = MASK_SPECIAL2(ctx->opcode);
        switch (op1) {
        case OPC_MADD ... OPC_MADDU: /* Multiply and add/sub */
        case OPC_MSUB ... OPC_MSUBU:
            check_insn(env, ctx, ISA_MIPS32);
            gen_muldiv(ctx, op1, rs, rt);
            break;
        case OPC_MUL:
            gen_arith(env, ctx, op1, rd, rs, rt);
            break;
        case OPC_CLZ ... OPC_CLO:
            check_insn(env, ctx, ISA_MIPS32);
            gen_cl(ctx, op1, rd, rs);
            break;
        case OPC_SDBBP:
            /* XXX: not clear which exception should be raised
             *      when in debug mode...
             */
            check_insn(env, ctx, ISA_MIPS32);
            if (!(ctx->hflags & MIPS_HFLAG_DM)) {
                generate_exception(ctx, EXCP_DBp);
            } else {
                generate_exception(ctx, EXCP_DBp);
            }
            /* Treat as NOP. */
            break;
#if defined(TARGET_MIPS64)
        case OPC_DCLZ ... OPC_DCLO:
            check_insn(env, ctx, ISA_MIPS64);
            check_mips_64(ctx);
            gen_cl(ctx, op1, rd, rs);
            break;
#endif
        default:            /* Invalid */
            MIPS_INVAL("special2");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SPECIAL3:
        op1 = MASK_SPECIAL3(ctx->opcode);
        switch (op1) {
        case OPC_EXT:
        case OPC_INS:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_bitops(ctx, op1, rt, rs, sa, rd);
            break;
        case OPC_BSHFL:
            check_insn(env, ctx, ISA_MIPS32R2);
            op2 = MASK_BSHFL(ctx->opcode);
            gen_bshfl(ctx, op2, rt, rd);
            break;
        case OPC_RDHWR:
            check_insn(env, ctx, ISA_MIPS32R2);
            {
                TCGv t0 = tcg_temp_local_new();

                switch (rd) {
                case 0:
                    save_cpu_state(ctx, 1);
                    gen_helper_rdhwr_cpunum(t0);
                    break;
                case 1:
                    save_cpu_state(ctx, 1);
                    gen_helper_rdhwr_synci_step(t0);
                    break;
                case 2:
                    save_cpu_state(ctx, 1);
                    gen_helper_rdhwr_cc(t0);
                    break;
                case 3:
                    save_cpu_state(ctx, 1);
                    gen_helper_rdhwr_ccres(t0);
                    break;
                case 29:
                    if (env->user_mode_only) {
                        tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUState, tls_value));
                        break;
                    } else {
                        /* XXX: Some CPUs implement this in hardware.
                           Not supported yet. */
                    }
                default:            /* Invalid */
                    MIPS_INVAL("rdhwr");
                    generate_exception(ctx, EXCP_RI);
                    break;
                }
                gen_store_gpr(t0, rt);
                tcg_temp_free(t0);
            }
            break;
        case OPC_FORK:
            check_insn(env, ctx, ASE_MT);
            {
                TCGv t0 = tcg_temp_local_new();
                TCGv t1 = tcg_temp_local_new();

                gen_load_gpr(t0, rt);
                gen_load_gpr(t1, rs);
                gen_helper_fork(t0, t1);
                tcg_temp_free(t0);
                tcg_temp_free(t1);
            }
            break;
        case OPC_YIELD:
            check_insn(env, ctx, ASE_MT);
            {
                TCGv t0 = tcg_temp_local_new();

                gen_load_gpr(t0, rs);
                gen_helper_yield(t0, t0);
                gen_store_gpr(t0, rd);
                tcg_temp_free(t0);
            }
            break;
#if defined(TARGET_MIPS64)
        case OPC_DEXTM ... OPC_DEXT:
        case OPC_DINSM ... OPC_DINS:
            check_insn(env, ctx, ISA_MIPS64R2);
            check_mips_64(ctx);
            gen_bitops(ctx, op1, rt, rs, sa, rd);
            break;
        case OPC_DBSHFL:
            check_insn(env, ctx, ISA_MIPS64R2);
            check_mips_64(ctx);
            op2 = MASK_DBSHFL(ctx->opcode);
            gen_bshfl(ctx, op2, rt, rd);
            break;
#endif
        default:            /* Invalid */
            MIPS_INVAL("special3");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_REGIMM:
        op1 = MASK_REGIMM(ctx->opcode);
        switch (op1) {
        case OPC_BLTZ ... OPC_BGEZL: /* REGIMM branches */
        case OPC_BLTZAL ... OPC_BGEZALL:
            gen_compute_branch(ctx, op1, rs, -1, imm << 2);
            return;
        case OPC_TGEI ... OPC_TEQI: /* REGIMM traps */
        case OPC_TNEI:
            gen_trap(ctx, op1, rs, -1, imm);
            break;
        case OPC_SYNCI:
            check_insn(env, ctx, ISA_MIPS32R2);
            /* Treat as NOP. */
            break;
        default:            /* Invalid */
            MIPS_INVAL("regimm");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_CP0:
        check_cp0_enabled(ctx);
        op1 = MASK_CP0(ctx->opcode);
        switch (op1) {
        case OPC_MFC0:
        case OPC_MTC0:
        case OPC_MFTR:
        case OPC_MTTR:
#if defined(TARGET_MIPS64)
        case OPC_DMFC0:
        case OPC_DMTC0:
#endif
#ifndef CONFIG_USER_ONLY
            if (!env->user_mode_only)
                gen_cp0(env, ctx, op1, rt, rd);
#endif /* !CONFIG_USER_ONLY */
            break;
        case OPC_C0_FIRST ... OPC_C0_LAST:
#ifndef CONFIG_USER_ONLY
            if (!env->user_mode_only)
                gen_cp0(env, ctx, MASK_C0(ctx->opcode), rt, rd);
#endif /* !CONFIG_USER_ONLY */
            break;
        case OPC_MFMC0:
#ifndef CONFIG_USER_ONLY
            if (!env->user_mode_only) {
                TCGv t0 = tcg_temp_local_new();

                op2 = MASK_MFMC0(ctx->opcode);
                switch (op2) {
                case OPC_DMT:
                    check_insn(env, ctx, ASE_MT);
                    gen_helper_dmt(t0, t0);
                    break;
                case OPC_EMT:
                    check_insn(env, ctx, ASE_MT);
                    gen_helper_emt(t0, t0);
                    break;
                case OPC_DVPE:
                    check_insn(env, ctx, ASE_MT);
                    gen_helper_dvpe(t0, t0);
                    break;
                case OPC_EVPE:
                    check_insn(env, ctx, ASE_MT);
                    gen_helper_evpe(t0, t0);
                    break;
                case OPC_DI:
                    check_insn(env, ctx, ISA_MIPS32R2);
                    save_cpu_state(ctx, 1);
                    gen_helper_di(t0);
                    /* Stop translation as we may have switched the execution mode */
                    ctx->bstate = BS_STOP;
                    break;
                case OPC_EI:
                    check_insn(env, ctx, ISA_MIPS32R2);
                    save_cpu_state(ctx, 1);
                    gen_helper_ei(t0);
                    /* Stop translation as we may have switched the execution mode */
                    ctx->bstate = BS_STOP;
                    break;
                default:            /* Invalid */
                    MIPS_INVAL("mfmc0");
                    generate_exception(ctx, EXCP_RI);
                    break;
                }
                gen_store_gpr(t0, rt);
                tcg_temp_free(t0);
            }
#endif /* !CONFIG_USER_ONLY */
            break;
        case OPC_RDPGPR:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_load_srsgpr(rt, rd);
            break;
        case OPC_WRPGPR:
            check_insn(env, ctx, ISA_MIPS32R2);
            gen_store_srsgpr(rt, rd);
            break;
        default:
            MIPS_INVAL("cp0");
            generate_exception(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_ADDI ... OPC_LUI: /* Arithmetic with immediate opcode */
         gen_arith_imm(env, ctx, op, rt, rs, imm);
         break;
    case OPC_J ... OPC_JAL: /* Jump */
         offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 2;
         gen_compute_branch(ctx, op, rs, rt, offset);
         return;
    case OPC_BEQ ... OPC_BGTZ: /* Branch */
    case OPC_BEQL ... OPC_BGTZL:
         gen_compute_branch(ctx, op, rs, rt, imm << 2);
         return;
    case OPC_LB ... OPC_LWR: /* Load and stores */
    case OPC_SB ... OPC_SW:
    case OPC_SWR:
    case OPC_LL:
    case OPC_SC:
         gen_ldst(ctx, op, rt, rs, imm);
         break;
    case OPC_CACHE:
        check_insn(env, ctx, ISA_MIPS3 | ISA_MIPS32);
        /* Treat as NOP. */
        break;
    case OPC_PREF:
        check_insn(env, ctx, ISA_MIPS4 | ISA_MIPS32);
        /* Treat as NOP. */
        break;

    /* Floating point (COP1). */
    case OPC_LWC1:
    case OPC_LDC1:
    case OPC_SWC1:
    case OPC_SDC1:
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            save_cpu_state(ctx, 1);
            check_cp1_enabled(ctx);
            gen_flt_ldst(ctx, op, rt, rs, imm);
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

    case OPC_CP1:
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            save_cpu_state(ctx, 1);
            check_cp1_enabled(ctx);
            op1 = MASK_CP1(ctx->opcode);
            switch (op1) {
            case OPC_MFHC1:
            case OPC_MTHC1:
                check_insn(env, ctx, ISA_MIPS32R2);
            case OPC_MFC1:
            case OPC_CFC1:
            case OPC_MTC1:
            case OPC_CTC1:
                gen_cp1(ctx, op1, rt, rd);
                break;
#if defined(TARGET_MIPS64)
            case OPC_DMFC1:
            case OPC_DMTC1:
                check_insn(env, ctx, ISA_MIPS3);
                gen_cp1(ctx, op1, rt, rd);
                break;
#endif
            case OPC_BC1ANY2:
            case OPC_BC1ANY4:
                check_cop1x(ctx);
                check_insn(env, ctx, ASE_MIPS3D);
                /* fall through */
            case OPC_BC1:
                gen_compute_branch1(env, ctx, MASK_BC1(ctx->opcode),
                                    (rt >> 2) & 0x7, imm << 2);
                return;
            case OPC_S_FMT:
            case OPC_D_FMT:
            case OPC_W_FMT:
            case OPC_L_FMT:
            case OPC_PS_FMT:
                gen_farith(ctx, MASK_CP1_FUNC(ctx->opcode), rt, rd, sa,
                           (imm >> 8) & 0x7);
                break;
            default:
                MIPS_INVAL("cp1");
                generate_exception (ctx, EXCP_RI);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

    /* COP2.  */
    case OPC_LWC2:
    case OPC_LDC2:
    case OPC_SWC2:
    case OPC_SDC2:
    case OPC_CP2:
        /* COP2: Not implemented. */
        generate_exception_err(ctx, EXCP_CpU, 2);
        break;

    case OPC_CP3:
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            save_cpu_state(ctx, 1);
            check_cp1_enabled(ctx);
            op1 = MASK_CP3(ctx->opcode);
            switch (op1) {
            case OPC_LWXC1:
            case OPC_LDXC1:
            case OPC_LUXC1:
            case OPC_SWXC1:
            case OPC_SDXC1:
            case OPC_SUXC1:
                gen_flt3_ldst(ctx, op1, sa, rd, rs, rt);
                break;
            case OPC_PREFX:
                /* Treat as NOP. */
                break;
            case OPC_ALNV_PS:
            case OPC_MADD_S:
            case OPC_MADD_D:
            case OPC_MADD_PS:
            case OPC_MSUB_S:
            case OPC_MSUB_D:
            case OPC_MSUB_PS:
            case OPC_NMADD_S:
            case OPC_NMADD_D:
            case OPC_NMADD_PS:
            case OPC_NMSUB_S:
            case OPC_NMSUB_D:
            case OPC_NMSUB_PS:
                gen_flt3_arith(ctx, op1, sa, rs, rd, rt);
                break;
            default:
                MIPS_INVAL("cp3");
                generate_exception (ctx, EXCP_RI);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

#if defined(TARGET_MIPS64)
    /* MIPS64 opcodes */
    case OPC_LWU:
    case OPC_LDL ... OPC_LDR:
    case OPC_SDL ... OPC_SDR:
    case OPC_LLD:
    case OPC_LD:
    case OPC_SCD:
    case OPC_SD:
        check_insn(env, ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ldst(ctx, op, rt, rs, imm);
        break;
    case OPC_DADDI ... OPC_DADDIU:
        check_insn(env, ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_arith_imm(env, ctx, op, rt, rs, imm);
        break;
#endif
    case OPC_JALX:
        check_insn(env, ctx, ASE_MIPS16);
        /* MIPS16: Not implemented. */
    case OPC_MDMX:
        check_insn(env, ctx, ASE_MDMX);
        /* MDMX: Not implemented. */
    default:            /* Invalid */
        MIPS_INVAL("major opcode");
        generate_exception(ctx, EXCP_RI);
        break;
    }
    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        int hflags = ctx->hflags & MIPS_HFLAG_BMASK;
        /* Branches completion */
        ctx->hflags &= ~MIPS_HFLAG_BMASK;
        ctx->bstate = BS_BRANCH;
        save_cpu_state(ctx, 0);
        /* FIXME: Need to clear can_do_io.  */
        switch (hflags) {
        case MIPS_HFLAG_B:
            /* unconditional branch */
            MIPS_DEBUG("unconditional branch");
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BL:
            /* blikely taken case */
            MIPS_DEBUG("blikely branch taken");
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BC:
            /* Conditional branch */
            MIPS_DEBUG("conditional branch");
            {
                int l1 = gen_new_label();

                tcg_gen_brcondi_i32(TCG_COND_NE, bcond, 0, l1);
                gen_goto_tb(ctx, 1, ctx->pc + 4);
                gen_set_label(l1);
                gen_goto_tb(ctx, 0, ctx->btarget);
            }
            break;
        case MIPS_HFLAG_BR:
            /* unconditional branch to register */
            MIPS_DEBUG("branch to register");
            tcg_gen_mov_tl(cpu_PC, btarget);
            tcg_gen_exit_tb(0);
            break;
        default:
            MIPS_DEBUG("unknown branch");
            break;
        }
    }
}

static inline void
gen_intermediate_code_internal (CPUState *env, TranslationBlock *tb,
                                int search_pc)
{
    DisasContext ctx;
    target_ulong pc_start;
    uint16_t *gen_opc_end;
    CPUBreakpoint *bp;
    int j, lj = -1;
    int num_insns;
    int max_insns;

    if (search_pc && loglevel)
        fprintf (logfile, "search pc %d\n", search_pc);

    pc_start = tb->pc;
    /* Leave some spare opc slots for branch handling. */
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE - 16;
    ctx.pc = pc_start;
    ctx.saved_pc = -1;
    ctx.tb = tb;
    ctx.bstate = BS_NONE;
    /* Restore delay slot state from the tb context.  */
    ctx.hflags = (uint32_t)tb->flags; /* FIXME: maybe use 64 bits here? */
    restore_cpu_state(env, &ctx);
    if (env->user_mode_only)
        ctx.mem_idx = MIPS_HFLAG_UM;
    else
        ctx.mem_idx = ctx.hflags & MIPS_HFLAG_KSU;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0)
        max_insns = CF_COUNT_MASK;
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "------------------------------------------------\n");
        /* FIXME: This may print out stale hflags from env... */
        cpu_dump_state(env, logfile, fprintf, 0);
    }
#endif
#ifdef MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
        fprintf(logfile, "\ntb %p idx %d hflags %04x\n",
                tb, ctx.mem_idx, ctx.hflags);
#endif
    gen_icount_start();
    while (ctx.bstate == BS_NONE) {
        if (unlikely(env->breakpoints)) {
            for (bp = env->breakpoints; bp != NULL; bp = bp->next) {
                if (bp->pc == ctx.pc) {
                    save_cpu_state(&ctx, 1);
                    ctx.bstate = BS_BRANCH;
                    gen_helper_0i(raise_exception, EXCP_DEBUG);
                    /* Include the breakpoint location or the tb won't
                     * be flushed when it must be.  */
                    ctx.pc += 4;
                    goto done_generating;
                }
            }
        }

        if (search_pc) {
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
            }
            gen_opc_pc[lj] = ctx.pc;
            gen_opc_hflags[lj] = ctx.hflags & MIPS_HFLAG_BMASK;
            gen_opc_instr_start[lj] = 1;
            gen_opc_icount[lj] = num_insns;
        }
        if (num_insns + 1 == max_insns && (tb->cflags & CF_LAST_IO))
            gen_io_start();
        ctx.opcode = ldl_code(ctx.pc);
        decode_opc(env, &ctx);
        ctx.pc += 4;
        num_insns++;

        if (env->singlestep_enabled)
            break;

        if ((ctx.pc & (TARGET_PAGE_SIZE - 1)) == 0)
            break;

        if (gen_opc_ptr >= gen_opc_end)
            break;

        if (num_insns >= max_insns)
            break;
#if defined (MIPS_SINGLE_STEP)
        break;
#endif
    }
    if (tb->cflags & CF_LAST_IO)
        gen_io_end();
    if (env->singlestep_enabled) {
        save_cpu_state(&ctx, ctx.bstate == BS_NONE);
        gen_helper_0i(raise_exception, EXCP_DEBUG);
    } else {
	switch (ctx.bstate) {
        case BS_STOP:
            gen_helper_interrupt_restart();
            gen_goto_tb(&ctx, 0, ctx.pc);
            break;
        case BS_NONE:
            save_cpu_state(&ctx, 0);
            gen_goto_tb(&ctx, 0, ctx.pc);
            break;
        case BS_EXCP:
            gen_helper_interrupt_restart();
            tcg_gen_exit_tb(0);
            break;
        case BS_BRANCH:
        default:
            break;
	}
    }
done_generating:
    gen_icount_end(tb, num_insns);
    *gen_opc_ptr = INDEX_op_end;
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    } else {
        tb->size = ctx.pc - pc_start;
        tb->icount = num_insns;
    }
#ifdef DEBUG_DISAS
#if defined MIPS_DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM)
        fprintf(logfile, "\n");
#endif
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, ctx.pc - pc_start, 0);
        fprintf(logfile, "\n");
    }
    if (loglevel & CPU_LOG_TB_CPU) {
        fprintf(logfile, "---------------- %d %08x\n", ctx.bstate, ctx.hflags);
    }
#endif
}

void gen_intermediate_code (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 0);
}

void gen_intermediate_code_pc (CPUState *env, struct TranslationBlock *tb)
{
    gen_intermediate_code_internal(env, tb, 1);
}

static void fpu_dump_state(CPUState *env, FILE *f,
                           int (*fpu_fprintf)(FILE *f, const char *fmt, ...),
                           int flags)
{
    int i;
    int is_fpu64 = !!(env->hflags & MIPS_HFLAG_F64);

#define printfpr(fp)                                                        \
    do {                                                                    \
        if (is_fpu64)                                                       \
            fpu_fprintf(f, "w:%08x d:%016lx fd:%13g fs:%13g psu: %13g\n",   \
                        (fp)->w[FP_ENDIAN_IDX], (fp)->d, (fp)->fd,          \
                        (fp)->fs[FP_ENDIAN_IDX], (fp)->fs[!FP_ENDIAN_IDX]); \
        else {                                                              \
            fpr_t tmp;                                                      \
            tmp.w[FP_ENDIAN_IDX] = (fp)->w[FP_ENDIAN_IDX];                  \
            tmp.w[!FP_ENDIAN_IDX] = ((fp) + 1)->w[FP_ENDIAN_IDX];           \
            fpu_fprintf(f, "w:%08x d:%016lx fd:%13g fs:%13g psu:%13g\n",    \
                        tmp.w[FP_ENDIAN_IDX], tmp.d, tmp.fd,                \
                        tmp.fs[FP_ENDIAN_IDX], tmp.fs[!FP_ENDIAN_IDX]);     \
        }                                                                   \
    } while(0)


    fpu_fprintf(f, "CP1 FCR0 0x%08x  FCR31 0x%08x  SR.FR %d  fp_status 0x%08x(0x%02x)\n",
                env->active_fpu.fcr0, env->active_fpu.fcr31, is_fpu64, env->active_fpu.fp_status,
                get_float_exception_flags(&env->active_fpu.fp_status));
    for (i = 0; i < 32; (is_fpu64) ? i++ : (i += 2)) {
        fpu_fprintf(f, "%3s: ", fregnames[i]);
        printfpr(&env->active_fpu.fpr[i]);
    }

#undef printfpr
}

#if defined(TARGET_MIPS64) && defined(MIPS_DEBUG_SIGN_EXTENSIONS)
/* Debug help: The architecture requires 32bit code to maintain proper
   sign-extended values on 64bit machines.  */

#define SIGN_EXT_P(val) ((((val) & ~0x7fffffff) == 0) || (((val) & ~0x7fffffff) == ~0x7fffffff))

static void
cpu_mips_check_sign_extensions (CPUState *env, FILE *f,
                                int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                                int flags)
{
    int i;

    if (!SIGN_EXT_P(env->active_tc.PC))
        cpu_fprintf(f, "BROKEN: pc=0x" TARGET_FMT_lx "\n", env->active_tc.PC);
    if (!SIGN_EXT_P(env->active_tc.HI[0]))
        cpu_fprintf(f, "BROKEN: HI=0x" TARGET_FMT_lx "\n", env->active_tc.HI[0]);
    if (!SIGN_EXT_P(env->active_tc.LO[0]))
        cpu_fprintf(f, "BROKEN: LO=0x" TARGET_FMT_lx "\n", env->active_tc.LO[0]);
    if (!SIGN_EXT_P(env->btarget))
        cpu_fprintf(f, "BROKEN: btarget=0x" TARGET_FMT_lx "\n", env->btarget);

    for (i = 0; i < 32; i++) {
        if (!SIGN_EXT_P(env->active_tc.gpr[i]))
            cpu_fprintf(f, "BROKEN: %s=0x" TARGET_FMT_lx "\n", regnames[i], env->active_tc.gpr[i]);
    }

    if (!SIGN_EXT_P(env->CP0_EPC))
        cpu_fprintf(f, "BROKEN: EPC=0x" TARGET_FMT_lx "\n", env->CP0_EPC);
    if (!SIGN_EXT_P(env->CP0_LLAddr))
        cpu_fprintf(f, "BROKEN: LLAddr=0x" TARGET_FMT_lx "\n", env->CP0_LLAddr);
}
#endif

void cpu_dump_state (CPUState *env, FILE *f,
                     int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                     int flags)
{
    int i;

    cpu_fprintf(f, "pc=0x" TARGET_FMT_lx " HI=0x" TARGET_FMT_lx " LO=0x" TARGET_FMT_lx " ds %04x " TARGET_FMT_lx " %d\n",
                env->active_tc.PC, env->active_tc.HI[0], env->active_tc.LO[0],
                env->hflags, env->btarget, env->bcond);
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0)
            cpu_fprintf(f, "GPR%02d:", i);
        cpu_fprintf(f, " %s " TARGET_FMT_lx, regnames[i], env->active_tc.gpr[i]);
        if ((i & 3) == 3)
            cpu_fprintf(f, "\n");
    }

    cpu_fprintf(f, "CP0 Status  0x%08x Cause   0x%08x EPC    0x" TARGET_FMT_lx "\n",
                env->CP0_Status, env->CP0_Cause, env->CP0_EPC);
    cpu_fprintf(f, "    Config0 0x%08x Config1 0x%08x LLAddr 0x" TARGET_FMT_lx "\n",
                env->CP0_Config0, env->CP0_Config1, env->CP0_LLAddr);
    if (env->hflags & MIPS_HFLAG_FPU)
        fpu_dump_state(env, f, cpu_fprintf, flags);
#if defined(TARGET_MIPS64) && defined(MIPS_DEBUG_SIGN_EXTENSIONS)
    cpu_mips_check_sign_extensions(env, f, cpu_fprintf, flags);
#endif
}

static void mips_tcg_init(void)
{
    int i;
    static int inited;

    /* Initialize various static tables. */
    if (inited)
	return;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    for (i = 0; i < 32; i++)
        cpu_gpr[i] = tcg_global_mem_new(TCG_AREG0,
                                        offsetof(CPUState, active_tc.gpr[i]),
                                        regnames[i]);
    cpu_PC = tcg_global_mem_new(TCG_AREG0,
                                offsetof(CPUState, active_tc.PC), "PC");
    for (i = 0; i < MIPS_DSP_ACC; i++) {
        cpu_HI[i] = tcg_global_mem_new(TCG_AREG0,
                                       offsetof(CPUState, active_tc.HI[i]),
                                       regnames_HI[i]);
        cpu_LO[i] = tcg_global_mem_new(TCG_AREG0,
                                       offsetof(CPUState, active_tc.LO[i]),
                                       regnames_LO[i]);
        cpu_ACX[i] = tcg_global_mem_new(TCG_AREG0,
                                        offsetof(CPUState, active_tc.ACX[i]),
                                        regnames_ACX[i]);
    }
    cpu_dspctrl = tcg_global_mem_new(TCG_AREG0,
                                     offsetof(CPUState, active_tc.DSPControl),
                                     "DSPControl");
    bcond = tcg_global_mem_new_i32(TCG_AREG0,
                                   offsetof(CPUState, bcond), "bcond");
    btarget = tcg_global_mem_new(TCG_AREG0,
                                 offsetof(CPUState, btarget), "btarget");
    for (i = 0; i < 32; i++)
        fpu_fpr32[i] = tcg_global_mem_new_i32(TCG_AREG0,
            offsetof(CPUState, active_fpu.fpr[i].w[FP_ENDIAN_IDX]),
            fregnames[i]);
    for (i = 0; i < 32; i++)
        fpu_fpr64[i] = tcg_global_mem_new_i64(TCG_AREG0,
            offsetof(CPUState, active_fpu.fpr[i]),
            fregnames_64[i]);
    for (i = 0; i < 32; i++)
        fpu_fpr32h[i] = tcg_global_mem_new_i32(TCG_AREG0,
            offsetof(CPUState, active_fpu.fpr[i].w[!FP_ENDIAN_IDX]),
            fregnames_h[i]);
    fpu_fcr0 = tcg_global_mem_new_i32(TCG_AREG0,
                                      offsetof(CPUState, active_fpu.fcr0),
                                      "fcr0");
    fpu_fcr31 = tcg_global_mem_new_i32(TCG_AREG0,
                                       offsetof(CPUState, active_fpu.fcr31),
                                       "fcr31");

    /* register helpers */
#define GEN_HELPER 2
#include "helper.h"

    inited = 1;
}

#include "translate_init.c"

CPUMIPSState *cpu_mips_init (const char *cpu_model)
{
    CPUMIPSState *env;
    const mips_def_t *def;

    def = cpu_mips_find_by_name(cpu_model);
    if (!def)
        return NULL;
    env = qemu_mallocz(sizeof(CPUMIPSState));
    if (!env)
        return NULL;
    env->cpu_model = def;

    cpu_exec_init(env);
    env->cpu_model_str = cpu_model;
    mips_tcg_init();
    cpu_reset(env);
    return env;
}

void cpu_reset (CPUMIPSState *env)
{
    memset(env, 0, offsetof(CPUMIPSState, breakpoints));

    tlb_flush(env, 1);

    /* Minimal init */
#if defined(CONFIG_USER_ONLY)
    env->user_mode_only = 1;
#endif
    if (env->user_mode_only) {
        env->hflags = MIPS_HFLAG_UM;
    } else {
        if (env->hflags & MIPS_HFLAG_BMASK) {
            /* If the exception was raised from a delay slot,
               come back to the jump.  */
            env->CP0_ErrorEPC = env->active_tc.PC - 4;
        } else {
            env->CP0_ErrorEPC = env->active_tc.PC;
        }
        env->active_tc.PC = (int32_t)0xBFC00000;
        env->CP0_Wired = 0;
        /* SMP not implemented */
        env->CP0_EBase = 0x80000000;
        env->CP0_Status = (1 << CP0St_BEV) | (1 << CP0St_ERL);
        /* vectored interrupts not implemented, timer on int 7,
           no performance counters. */
        env->CP0_IntCtl = 0xe0000000;
        {
            int i;

            for (i = 0; i < 7; i++) {
                env->CP0_WatchLo[i] = 0;
                env->CP0_WatchHi[i] = 0x80000000;
            }
            env->CP0_WatchLo[7] = 0;
            env->CP0_WatchHi[7] = 0;
        }
        /* Count register increments in debug mode, EJTAG version 1 */
        env->CP0_Debug = (1 << CP0DB_CNT) | (0x1 << CP0DB_VER);
        env->hflags = MIPS_HFLAG_CP0;
    }
    env->exception_index = EXCP_NONE;
    cpu_mips_register(env, env->cpu_model);
}

void gen_pc_load(CPUState *env, TranslationBlock *tb,
                unsigned long searched_pc, int pc_pos, void *puc)
{
    env->active_tc.PC = gen_opc_pc[pc_pos];
    env->hflags &= ~MIPS_HFLAG_BMASK;
    env->hflags |= gen_opc_hflags[pc_pos];
}
