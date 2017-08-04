/*
 *  MIPS32 emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2004-2005 Jocelyn Mayer
 *  Copyright (c) 2006 Marius Groeger (FPU operations)
 *  Copyright (c) 2006 Thiemo Seufer (MIPS32R2 support)
 *  Copyright (c) 2009 CodeSourcery (MIPS16 and microMIPS support)
 *  Copyright (c) 2012 Jia Liu & Dongxue Zhang (MIPS ASE DSP support)
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
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "disas/disas.h"
#include "exec/exec-all.h"
#include "tcg-op.h"
#include "exec/cpu_ldst.h"
#include "hw/mips/cpudevs.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "exec/semihost.h"

#include "target/mips/trace.h"
#include "trace-tcg.h"
#include "exec/log.h"

#define MIPS_DEBUG_DISAS 0

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
    /* logic with immediate */
    OPC_ANDI     = (0x0C << 26),
    OPC_ORI      = (0x0D << 26),
    OPC_XORI     = (0x0E << 26),
    OPC_LUI      = (0x0F << 26),
    /* arithmetic with immediate */
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
    OPC_JALX     = (0x1D << 26),
    OPC_DAUI     = (0x1D << 26),
    /* Load and stores */
    OPC_LDL      = (0x1A << 26),
    OPC_LDR      = (0x1B << 26),
    OPC_LB       = (0x20 << 26),
    OPC_LH       = (0x21 << 26),
    OPC_LWL      = (0x22 << 26),
    OPC_LW       = (0x23 << 26),
    OPC_LWPC     = OPC_LW | 0x5,
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
    OPC_LDPC     = OPC_LD | 0x5,
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
    /* Compact Branches */
    OPC_BLEZALC  = (0x06 << 26),
    OPC_BGEZALC  = (0x06 << 26),
    OPC_BGEUC    = (0x06 << 26),
    OPC_BGTZALC  = (0x07 << 26),
    OPC_BLTZALC  = (0x07 << 26),
    OPC_BLTUC    = (0x07 << 26),
    OPC_BOVC     = (0x08 << 26),
    OPC_BEQZALC  = (0x08 << 26),
    OPC_BEQC     = (0x08 << 26),
    OPC_BLEZC    = (0x16 << 26),
    OPC_BGEZC    = (0x16 << 26),
    OPC_BGEC     = (0x16 << 26),
    OPC_BGTZC    = (0x17 << 26),
    OPC_BLTZC    = (0x17 << 26),
    OPC_BLTC     = (0x17 << 26),
    OPC_BNVC     = (0x18 << 26),
    OPC_BNEZALC  = (0x18 << 26),
    OPC_BNEC     = (0x18 << 26),
    OPC_BC       = (0x32 << 26),
    OPC_BEQZC    = (0x36 << 26),
    OPC_JIC      = (0x36 << 26),
    OPC_BALC     = (0x3A << 26),
    OPC_BNEZC    = (0x3E << 26),
    OPC_JIALC    = (0x3E << 26),
    /* MDMX ASE specific */
    OPC_MDMX     = (0x1E << 26),
    /* MSA ASE, same as MDMX */
    OPC_MSA      = OPC_MDMX,
    /* Cache and prefetch */
    OPC_CACHE    = (0x2F << 26),
    OPC_PREF     = (0x33 << 26),
    /* PC-relative address computation / loads */
    OPC_PCREL    = (0x3B << 26),
};

/* PC-relative address computation / loads  */
#define MASK_OPC_PCREL_TOP2BITS(op)  (MASK_OP_MAJOR(op) | (op & (3 << 19)))
#define MASK_OPC_PCREL_TOP5BITS(op)  (MASK_OP_MAJOR(op) | (op & (0x1f << 16)))
enum {
    /* Instructions determined by bits 19 and 20 */
    OPC_ADDIUPC = OPC_PCREL | (0 << 19),
    R6_OPC_LWPC = OPC_PCREL | (1 << 19),
    OPC_LWUPC   = OPC_PCREL | (2 << 19),

    /* Instructions determined by bits 16 ... 20 */
    OPC_AUIPC   = OPC_PCREL | (0x1e << 16),
    OPC_ALUIPC  = OPC_PCREL | (0x1f << 16),

    /* Other */
    R6_OPC_LDPC = OPC_PCREL | (6 << 18),
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
    OPC_ROTR     = OPC_SRL | (1 << 21),
    OPC_SRA      = 0x03 | OPC_SPECIAL,
    OPC_SLLV     = 0x04 | OPC_SPECIAL,
    OPC_SRLV     = 0x06 | OPC_SPECIAL, /* also ROTRV */
    OPC_ROTRV    = OPC_SRLV | (1 << 6),
    OPC_SRAV     = 0x07 | OPC_SPECIAL,
    OPC_DSLLV    = 0x14 | OPC_SPECIAL,
    OPC_DSRLV    = 0x16 | OPC_SPECIAL, /* also DROTRV */
    OPC_DROTRV   = OPC_DSRLV | (1 << 6),
    OPC_DSRAV    = 0x17 | OPC_SPECIAL,
    OPC_DSLL     = 0x38 | OPC_SPECIAL,
    OPC_DSRL     = 0x3A | OPC_SPECIAL, /* also DROTR */
    OPC_DROTR    = OPC_DSRL | (1 << 21),
    OPC_DSRA     = 0x3B | OPC_SPECIAL,
    OPC_DSLL32   = 0x3C | OPC_SPECIAL,
    OPC_DSRL32   = 0x3E | OPC_SPECIAL, /* also DROTR32 */
    OPC_DROTR32  = OPC_DSRL32 | (1 << 21),
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

    OPC_SELEQZ   = 0x35 | OPC_SPECIAL,
    OPC_SELNEZ   = 0x37 | OPC_SPECIAL,

    OPC_MOVCI    = 0x01 | OPC_SPECIAL,

    /* Special */
    OPC_PMON     = 0x05 | OPC_SPECIAL, /* unofficial */
    OPC_SYSCALL  = 0x0C | OPC_SPECIAL,
    OPC_BREAK    = 0x0D | OPC_SPECIAL,
    OPC_SPIM     = 0x0E | OPC_SPECIAL, /* unofficial */
    OPC_SYNC     = 0x0F | OPC_SPECIAL,

    OPC_SPECIAL28_RESERVED = 0x28 | OPC_SPECIAL,
    OPC_SPECIAL29_RESERVED = 0x29 | OPC_SPECIAL,
    OPC_SPECIAL39_RESERVED = 0x39 | OPC_SPECIAL,
    OPC_SPECIAL3D_RESERVED = 0x3D | OPC_SPECIAL,
};

/* R6 Multiply and Divide instructions have the same Opcode
   and function field as legacy OPC_MULT[U]/OPC_DIV[U] */
#define MASK_R6_MULDIV(op)   (MASK_SPECIAL(op) | (op & (0x7ff)))

enum {
    R6_OPC_MUL   = OPC_MULT  | (2 << 6),
    R6_OPC_MUH   = OPC_MULT  | (3 << 6),
    R6_OPC_MULU  = OPC_MULTU | (2 << 6),
    R6_OPC_MUHU  = OPC_MULTU | (3 << 6),
    R6_OPC_DIV   = OPC_DIV   | (2 << 6),
    R6_OPC_MOD   = OPC_DIV   | (3 << 6),
    R6_OPC_DIVU  = OPC_DIVU  | (2 << 6),
    R6_OPC_MODU  = OPC_DIVU  | (3 << 6),

    R6_OPC_DMUL   = OPC_DMULT  | (2 << 6),
    R6_OPC_DMUH   = OPC_DMULT  | (3 << 6),
    R6_OPC_DMULU  = OPC_DMULTU | (2 << 6),
    R6_OPC_DMUHU  = OPC_DMULTU | (3 << 6),
    R6_OPC_DDIV   = OPC_DDIV   | (2 << 6),
    R6_OPC_DMOD   = OPC_DDIV   | (3 << 6),
    R6_OPC_DDIVU  = OPC_DDIVU  | (2 << 6),
    R6_OPC_DMODU  = OPC_DDIVU  | (3 << 6),

    R6_OPC_CLZ      = 0x10 | OPC_SPECIAL,
    R6_OPC_CLO      = 0x11 | OPC_SPECIAL,
    R6_OPC_DCLZ     = 0x12 | OPC_SPECIAL,
    R6_OPC_DCLO     = 0x13 | OPC_SPECIAL,
    R6_OPC_SDBBP    = 0x0e | OPC_SPECIAL,

    OPC_LSA  = 0x05 | OPC_SPECIAL,
    OPC_DLSA = 0x15 | OPC_SPECIAL,
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
    OPC_SIGRIE   = (0x17 << 16) | OPC_REGIMM,
    OPC_SYNCI    = (0x1F << 16) | OPC_REGIMM,

    OPC_DAHI     = (0x06 << 16) | OPC_REGIMM,
    OPC_DATI     = (0x1e << 16) | OPC_REGIMM,
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
    /* Loongson 2F */
    OPC_MULT_G_2F   = 0x10 | OPC_SPECIAL2,
    OPC_DMULT_G_2F  = 0x11 | OPC_SPECIAL2,
    OPC_MULTU_G_2F  = 0x12 | OPC_SPECIAL2,
    OPC_DMULTU_G_2F = 0x13 | OPC_SPECIAL2,
    OPC_DIV_G_2F    = 0x14 | OPC_SPECIAL2,
    OPC_DDIV_G_2F   = 0x15 | OPC_SPECIAL2,
    OPC_DIVU_G_2F   = 0x16 | OPC_SPECIAL2,
    OPC_DDIVU_G_2F  = 0x17 | OPC_SPECIAL2,
    OPC_MOD_G_2F    = 0x1c | OPC_SPECIAL2,
    OPC_DMOD_G_2F   = 0x1d | OPC_SPECIAL2,
    OPC_MODU_G_2F   = 0x1e | OPC_SPECIAL2,
    OPC_DMODU_G_2F  = 0x1f | OPC_SPECIAL2,
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

    /* Loongson 2E */
    OPC_MULT_G_2E   = 0x18 | OPC_SPECIAL3,
    OPC_MULTU_G_2E  = 0x19 | OPC_SPECIAL3,
    OPC_DIV_G_2E    = 0x1A | OPC_SPECIAL3,
    OPC_DIVU_G_2E   = 0x1B | OPC_SPECIAL3,
    OPC_DMULT_G_2E  = 0x1C | OPC_SPECIAL3,
    OPC_DMULTU_G_2E = 0x1D | OPC_SPECIAL3,
    OPC_DDIV_G_2E   = 0x1E | OPC_SPECIAL3,
    OPC_DDIVU_G_2E  = 0x1F | OPC_SPECIAL3,
    OPC_MOD_G_2E    = 0x22 | OPC_SPECIAL3,
    OPC_MODU_G_2E   = 0x23 | OPC_SPECIAL3,
    OPC_DMOD_G_2E   = 0x26 | OPC_SPECIAL3,
    OPC_DMODU_G_2E  = 0x27 | OPC_SPECIAL3,

    /* MIPS DSP Load */
    OPC_LX_DSP         = 0x0A | OPC_SPECIAL3,
    /* MIPS DSP Arithmetic */
    OPC_ADDU_QB_DSP    = 0x10 | OPC_SPECIAL3,
    OPC_ADDU_OB_DSP    = 0x14 | OPC_SPECIAL3,
    OPC_ABSQ_S_PH_DSP  = 0x12 | OPC_SPECIAL3,
    OPC_ABSQ_S_QH_DSP  = 0x16 | OPC_SPECIAL3,
    /* OPC_ADDUH_QB_DSP is same as OPC_MULT_G_2E.  */
    /* OPC_ADDUH_QB_DSP   = 0x18 | OPC_SPECIAL3,  */
    OPC_CMPU_EQ_QB_DSP = 0x11 | OPC_SPECIAL3,
    OPC_CMPU_EQ_OB_DSP = 0x15 | OPC_SPECIAL3,
    /* MIPS DSP GPR-Based Shift Sub-class */
    OPC_SHLL_QB_DSP    = 0x13 | OPC_SPECIAL3,
    OPC_SHLL_OB_DSP    = 0x17 | OPC_SPECIAL3,
    /* MIPS DSP Multiply Sub-class insns */
    /* OPC_MUL_PH_DSP is same as OPC_ADDUH_QB_DSP.  */
    /* OPC_MUL_PH_DSP     = 0x18 | OPC_SPECIAL3,  */
    OPC_DPA_W_PH_DSP   = 0x30 | OPC_SPECIAL3,
    OPC_DPAQ_W_QH_DSP  = 0x34 | OPC_SPECIAL3,
    /* DSP Bit/Manipulation Sub-class */
    OPC_INSV_DSP       = 0x0C | OPC_SPECIAL3,
    OPC_DINSV_DSP      = 0x0D | OPC_SPECIAL3,
    /* MIPS DSP Append Sub-class */
    OPC_APPEND_DSP     = 0x31 | OPC_SPECIAL3,
    OPC_DAPPEND_DSP    = 0x35 | OPC_SPECIAL3,
    /* MIPS DSP Accumulator and DSPControl Access Sub-class */
    OPC_EXTR_W_DSP     = 0x38 | OPC_SPECIAL3,
    OPC_DEXTR_W_DSP    = 0x3C | OPC_SPECIAL3,

    /* EVA */
    OPC_LWLE           = 0x19 | OPC_SPECIAL3,
    OPC_LWRE           = 0x1A | OPC_SPECIAL3,
    OPC_CACHEE         = 0x1B | OPC_SPECIAL3,
    OPC_SBE            = 0x1C | OPC_SPECIAL3,
    OPC_SHE            = 0x1D | OPC_SPECIAL3,
    OPC_SCE            = 0x1E | OPC_SPECIAL3,
    OPC_SWE            = 0x1F | OPC_SPECIAL3,
    OPC_SWLE           = 0x21 | OPC_SPECIAL3,
    OPC_SWRE           = 0x22 | OPC_SPECIAL3,
    OPC_PREFE          = 0x23 | OPC_SPECIAL3,
    OPC_LBUE           = 0x28 | OPC_SPECIAL3,
    OPC_LHUE           = 0x29 | OPC_SPECIAL3,
    OPC_LBE            = 0x2C | OPC_SPECIAL3,
    OPC_LHE            = 0x2D | OPC_SPECIAL3,
    OPC_LLE            = 0x2E | OPC_SPECIAL3,
    OPC_LWE            = 0x2F | OPC_SPECIAL3,

    /* R6 */
    R6_OPC_PREF        = 0x35 | OPC_SPECIAL3,
    R6_OPC_CACHE       = 0x25 | OPC_SPECIAL3,
    R6_OPC_LL          = 0x36 | OPC_SPECIAL3,
    R6_OPC_SC          = 0x26 | OPC_SPECIAL3,
    R6_OPC_LLD         = 0x37 | OPC_SPECIAL3,
    R6_OPC_SCD         = 0x27 | OPC_SPECIAL3,
};

/* BSHFL opcodes */
#define MASK_BSHFL(op)     MASK_SPECIAL3(op) | (op & (0x1F << 6))

enum {
    OPC_WSBH      = (0x02 << 6) | OPC_BSHFL,
    OPC_SEB       = (0x10 << 6) | OPC_BSHFL,
    OPC_SEH       = (0x18 << 6) | OPC_BSHFL,
    OPC_ALIGN     = (0x08 << 6) | OPC_BSHFL, /* 010.bp */
    OPC_ALIGN_END = (0x0B << 6) | OPC_BSHFL, /* 010.00 to 010.11 */
    OPC_BITSWAP   = (0x00 << 6) | OPC_BSHFL  /* 00000 */
};

/* DBSHFL opcodes */
#define MASK_DBSHFL(op)    MASK_SPECIAL3(op) | (op & (0x1F << 6))

enum {
    OPC_DSBH       = (0x02 << 6) | OPC_DBSHFL,
    OPC_DSHD       = (0x05 << 6) | OPC_DBSHFL,
    OPC_DALIGN     = (0x08 << 6) | OPC_DBSHFL, /* 01.bp */
    OPC_DALIGN_END = (0x0F << 6) | OPC_DBSHFL, /* 01.000 to 01.111 */
    OPC_DBITSWAP   = (0x00 << 6) | OPC_DBSHFL, /* 00000 */
};

/* MIPS DSP REGIMM opcodes */
enum {
    OPC_BPOSGE32 = (0x1C << 16) | OPC_REGIMM,
    OPC_BPOSGE64 = (0x1D << 16) | OPC_REGIMM,
};

#define MASK_LX(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
/* MIPS DSP Load */
enum {
    OPC_LBUX = (0x06 << 6) | OPC_LX_DSP,
    OPC_LHX  = (0x04 << 6) | OPC_LX_DSP,
    OPC_LWX  = (0x00 << 6) | OPC_LX_DSP,
    OPC_LDX = (0x08 << 6) | OPC_LX_DSP,
};

#define MASK_ADDU_QB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Arithmetic Sub-class */
    OPC_ADDQ_PH        = (0x0A << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDQ_S_PH      = (0x0E << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDQ_S_W       = (0x16 << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDU_QB        = (0x00 << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDU_S_QB      = (0x04 << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDU_PH        = (0x08 << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDU_S_PH      = (0x0C << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBQ_PH        = (0x0B << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBQ_S_PH      = (0x0F << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBQ_S_W       = (0x17 << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBU_QB        = (0x01 << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBU_S_QB      = (0x05 << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBU_PH        = (0x09 << 6) | OPC_ADDU_QB_DSP,
    OPC_SUBU_S_PH      = (0x0D << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDSC          = (0x10 << 6) | OPC_ADDU_QB_DSP,
    OPC_ADDWC          = (0x11 << 6) | OPC_ADDU_QB_DSP,
    OPC_MODSUB         = (0x12 << 6) | OPC_ADDU_QB_DSP,
    OPC_RADDU_W_QB     = (0x14 << 6) | OPC_ADDU_QB_DSP,
    /* MIPS DSP Multiply Sub-class insns */
    OPC_MULEU_S_PH_QBL = (0x06 << 6) | OPC_ADDU_QB_DSP,
    OPC_MULEU_S_PH_QBR = (0x07 << 6) | OPC_ADDU_QB_DSP,
    OPC_MULQ_RS_PH     = (0x1F << 6) | OPC_ADDU_QB_DSP,
    OPC_MULEQ_S_W_PHL  = (0x1C << 6) | OPC_ADDU_QB_DSP,
    OPC_MULEQ_S_W_PHR  = (0x1D << 6) | OPC_ADDU_QB_DSP,
    OPC_MULQ_S_PH      = (0x1E << 6) | OPC_ADDU_QB_DSP,
};

#define OPC_ADDUH_QB_DSP OPC_MULT_G_2E
#define MASK_ADDUH_QB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Arithmetic Sub-class */
    OPC_ADDUH_QB   = (0x00 << 6) | OPC_ADDUH_QB_DSP,
    OPC_ADDUH_R_QB = (0x02 << 6) | OPC_ADDUH_QB_DSP,
    OPC_ADDQH_PH   = (0x08 << 6) | OPC_ADDUH_QB_DSP,
    OPC_ADDQH_R_PH = (0x0A << 6) | OPC_ADDUH_QB_DSP,
    OPC_ADDQH_W    = (0x10 << 6) | OPC_ADDUH_QB_DSP,
    OPC_ADDQH_R_W  = (0x12 << 6) | OPC_ADDUH_QB_DSP,
    OPC_SUBUH_QB   = (0x01 << 6) | OPC_ADDUH_QB_DSP,
    OPC_SUBUH_R_QB = (0x03 << 6) | OPC_ADDUH_QB_DSP,
    OPC_SUBQH_PH   = (0x09 << 6) | OPC_ADDUH_QB_DSP,
    OPC_SUBQH_R_PH = (0x0B << 6) | OPC_ADDUH_QB_DSP,
    OPC_SUBQH_W    = (0x11 << 6) | OPC_ADDUH_QB_DSP,
    OPC_SUBQH_R_W  = (0x13 << 6) | OPC_ADDUH_QB_DSP,
    /* MIPS DSP Multiply Sub-class insns */
    OPC_MUL_PH     = (0x0C << 6) | OPC_ADDUH_QB_DSP,
    OPC_MUL_S_PH   = (0x0E << 6) | OPC_ADDUH_QB_DSP,
    OPC_MULQ_S_W   = (0x16 << 6) | OPC_ADDUH_QB_DSP,
    OPC_MULQ_RS_W  = (0x17 << 6) | OPC_ADDUH_QB_DSP,
};

#define MASK_ABSQ_S_PH(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Arithmetic Sub-class */
    OPC_ABSQ_S_QB       = (0x01 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_ABSQ_S_PH       = (0x09 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_ABSQ_S_W        = (0x11 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEQ_W_PHL    = (0x0C << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEQ_W_PHR    = (0x0D << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEQU_PH_QBL  = (0x04 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEQU_PH_QBR  = (0x05 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEQU_PH_QBLA = (0x06 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEQU_PH_QBRA = (0x07 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEU_PH_QBL   = (0x1C << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEU_PH_QBR   = (0x1D << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEU_PH_QBLA  = (0x1E << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_PRECEU_PH_QBRA  = (0x1F << 6) | OPC_ABSQ_S_PH_DSP,
    /* DSP Bit/Manipulation Sub-class */
    OPC_BITREV          = (0x1B << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_REPL_QB         = (0x02 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_REPLV_QB        = (0x03 << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_REPL_PH         = (0x0A << 6) | OPC_ABSQ_S_PH_DSP,
    OPC_REPLV_PH        = (0x0B << 6) | OPC_ABSQ_S_PH_DSP,
};

#define MASK_CMPU_EQ_QB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Arithmetic Sub-class */
    OPC_PRECR_QB_PH      = (0x0D << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PRECRQ_QB_PH     = (0x0C << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PRECR_SRA_PH_W   = (0x1E << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PRECR_SRA_R_PH_W = (0x1F << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PRECRQ_PH_W      = (0x14 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PRECRQ_RS_PH_W   = (0x15 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PRECRQU_S_QB_PH  = (0x0F << 6) | OPC_CMPU_EQ_QB_DSP,
    /* DSP Compare-Pick Sub-class */
    OPC_CMPU_EQ_QB       = (0x00 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPU_LT_QB       = (0x01 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPU_LE_QB       = (0x02 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPGU_EQ_QB      = (0x04 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPGU_LT_QB      = (0x05 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPGU_LE_QB      = (0x06 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPGDU_EQ_QB     = (0x18 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPGDU_LT_QB     = (0x19 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMPGDU_LE_QB     = (0x1A << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMP_EQ_PH        = (0x08 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMP_LT_PH        = (0x09 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_CMP_LE_PH        = (0x0A << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PICK_QB          = (0x03 << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PICK_PH          = (0x0B << 6) | OPC_CMPU_EQ_QB_DSP,
    OPC_PACKRL_PH        = (0x0E << 6) | OPC_CMPU_EQ_QB_DSP,
};

#define MASK_SHLL_QB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP GPR-Based Shift Sub-class */
    OPC_SHLL_QB    = (0x00 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLLV_QB   = (0x02 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLL_PH    = (0x08 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLLV_PH   = (0x0A << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLL_S_PH  = (0x0C << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLLV_S_PH = (0x0E << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLL_S_W   = (0x14 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHLLV_S_W  = (0x16 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRL_QB    = (0x01 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRLV_QB   = (0x03 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRL_PH    = (0x19 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRLV_PH   = (0x1B << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRA_QB    = (0x04 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRA_R_QB  = (0x05 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRAV_QB   = (0x06 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRAV_R_QB = (0x07 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRA_PH    = (0x09 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRAV_PH   = (0x0B << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRA_R_PH  = (0x0D << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRAV_R_PH = (0x0F << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRA_R_W   = (0x15 << 6) | OPC_SHLL_QB_DSP,
    OPC_SHRAV_R_W  = (0x17 << 6) | OPC_SHLL_QB_DSP,
};

#define MASK_DPA_W_PH(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Multiply Sub-class insns */
    OPC_DPAU_H_QBL    = (0x03 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPAU_H_QBR    = (0x07 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSU_H_QBL    = (0x0B << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSU_H_QBR    = (0x0F << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPA_W_PH      = (0x00 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPAX_W_PH     = (0x08 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPAQ_S_W_PH   = (0x04 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPAQX_S_W_PH  = (0x18 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPAQX_SA_W_PH = (0x1A << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPS_W_PH      = (0x01 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSX_W_PH     = (0x09 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSQ_S_W_PH   = (0x05 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSQX_S_W_PH  = (0x19 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSQX_SA_W_PH = (0x1B << 6) | OPC_DPA_W_PH_DSP,
    OPC_MULSAQ_S_W_PH = (0x06 << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPAQ_SA_L_W   = (0x0C << 6) | OPC_DPA_W_PH_DSP,
    OPC_DPSQ_SA_L_W   = (0x0D << 6) | OPC_DPA_W_PH_DSP,
    OPC_MAQ_S_W_PHL   = (0x14 << 6) | OPC_DPA_W_PH_DSP,
    OPC_MAQ_S_W_PHR   = (0x16 << 6) | OPC_DPA_W_PH_DSP,
    OPC_MAQ_SA_W_PHL  = (0x10 << 6) | OPC_DPA_W_PH_DSP,
    OPC_MAQ_SA_W_PHR  = (0x12 << 6) | OPC_DPA_W_PH_DSP,
    OPC_MULSA_W_PH    = (0x02 << 6) | OPC_DPA_W_PH_DSP,
};

#define MASK_INSV(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* DSP Bit/Manipulation Sub-class */
    OPC_INSV = (0x00 << 6) | OPC_INSV_DSP,
};

#define MASK_APPEND(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Append Sub-class */
    OPC_APPEND  = (0x00 << 6) | OPC_APPEND_DSP,
    OPC_PREPEND = (0x01 << 6) | OPC_APPEND_DSP,
    OPC_BALIGN  = (0x10 << 6) | OPC_APPEND_DSP,
};

#define MASK_EXTR_W(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Accumulator and DSPControl Access Sub-class */
    OPC_EXTR_W     = (0x00 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTR_R_W   = (0x04 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTR_RS_W  = (0x06 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTR_S_H   = (0x0E << 6) | OPC_EXTR_W_DSP,
    OPC_EXTRV_S_H  = (0x0F << 6) | OPC_EXTR_W_DSP,
    OPC_EXTRV_W    = (0x01 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTRV_R_W  = (0x05 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTRV_RS_W = (0x07 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTP       = (0x02 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTPV      = (0x03 << 6) | OPC_EXTR_W_DSP,
    OPC_EXTPDP     = (0x0A << 6) | OPC_EXTR_W_DSP,
    OPC_EXTPDPV    = (0x0B << 6) | OPC_EXTR_W_DSP,
    OPC_SHILO      = (0x1A << 6) | OPC_EXTR_W_DSP,
    OPC_SHILOV     = (0x1B << 6) | OPC_EXTR_W_DSP,
    OPC_MTHLIP     = (0x1F << 6) | OPC_EXTR_W_DSP,
    OPC_WRDSP      = (0x13 << 6) | OPC_EXTR_W_DSP,
    OPC_RDDSP      = (0x12 << 6) | OPC_EXTR_W_DSP,
};

#define MASK_ABSQ_S_QH(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Arithmetic Sub-class */
    OPC_PRECEQ_L_PWL    = (0x14 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQ_L_PWR    = (0x15 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQ_PW_QHL   = (0x0C << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQ_PW_QHR   = (0x0D << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQ_PW_QHLA  = (0x0E << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQ_PW_QHRA  = (0x0F << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQU_QH_OBL  = (0x04 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQU_QH_OBR  = (0x05 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQU_QH_OBLA = (0x06 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEQU_QH_OBRA = (0x07 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEU_QH_OBL   = (0x1C << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEU_QH_OBR   = (0x1D << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEU_QH_OBLA  = (0x1E << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_PRECEU_QH_OBRA  = (0x1F << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_ABSQ_S_OB       = (0x01 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_ABSQ_S_PW       = (0x11 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_ABSQ_S_QH       = (0x09 << 6) | OPC_ABSQ_S_QH_DSP,
    /* DSP Bit/Manipulation Sub-class */
    OPC_REPL_OB         = (0x02 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_REPL_PW         = (0x12 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_REPL_QH         = (0x0A << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_REPLV_OB        = (0x03 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_REPLV_PW        = (0x13 << 6) | OPC_ABSQ_S_QH_DSP,
    OPC_REPLV_QH        = (0x0B << 6) | OPC_ABSQ_S_QH_DSP,
};

#define MASK_ADDU_OB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Multiply Sub-class insns */
    OPC_MULEQ_S_PW_QHL = (0x1C << 6) | OPC_ADDU_OB_DSP,
    OPC_MULEQ_S_PW_QHR = (0x1D << 6) | OPC_ADDU_OB_DSP,
    OPC_MULEU_S_QH_OBL = (0x06 << 6) | OPC_ADDU_OB_DSP,
    OPC_MULEU_S_QH_OBR = (0x07 << 6) | OPC_ADDU_OB_DSP,
    OPC_MULQ_RS_QH     = (0x1F << 6) | OPC_ADDU_OB_DSP,
    /* MIPS DSP Arithmetic Sub-class */
    OPC_RADDU_L_OB     = (0x14 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBQ_PW        = (0x13 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBQ_S_PW      = (0x17 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBQ_QH        = (0x0B << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBQ_S_QH      = (0x0F << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBU_OB        = (0x01 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBU_S_OB      = (0x05 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBU_QH        = (0x09 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBU_S_QH      = (0x0D << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBUH_OB       = (0x19 << 6) | OPC_ADDU_OB_DSP,
    OPC_SUBUH_R_OB     = (0x1B << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDQ_PW        = (0x12 << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDQ_S_PW      = (0x16 << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDQ_QH        = (0x0A << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDQ_S_QH      = (0x0E << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDU_OB        = (0x00 << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDU_S_OB      = (0x04 << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDU_QH        = (0x08 << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDU_S_QH      = (0x0C << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDUH_OB       = (0x18 << 6) | OPC_ADDU_OB_DSP,
    OPC_ADDUH_R_OB     = (0x1A << 6) | OPC_ADDU_OB_DSP,
};

#define MASK_CMPU_EQ_OB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* DSP Compare-Pick Sub-class */
    OPC_CMP_EQ_PW         = (0x10 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMP_LT_PW         = (0x11 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMP_LE_PW         = (0x12 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMP_EQ_QH         = (0x08 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMP_LT_QH         = (0x09 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMP_LE_QH         = (0x0A << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPGDU_EQ_OB      = (0x18 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPGDU_LT_OB      = (0x19 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPGDU_LE_OB      = (0x1A << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPGU_EQ_OB       = (0x04 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPGU_LT_OB       = (0x05 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPGU_LE_OB       = (0x06 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPU_EQ_OB        = (0x00 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPU_LT_OB        = (0x01 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_CMPU_LE_OB        = (0x02 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PACKRL_PW         = (0x0E << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PICK_OB           = (0x03 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PICK_PW           = (0x13 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PICK_QH           = (0x0B << 6) | OPC_CMPU_EQ_OB_DSP,
    /* MIPS DSP Arithmetic Sub-class */
    OPC_PRECR_OB_QH       = (0x0D << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECR_SRA_QH_PW   = (0x1E << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECR_SRA_R_QH_PW = (0x1F << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECRQ_OB_QH      = (0x0C << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECRQ_PW_L       = (0x1C << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECRQ_QH_PW      = (0x14 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECRQ_RS_QH_PW   = (0x15 << 6) | OPC_CMPU_EQ_OB_DSP,
    OPC_PRECRQU_S_OB_QH   = (0x0F << 6) | OPC_CMPU_EQ_OB_DSP,
};

#define MASK_DAPPEND(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* DSP Append Sub-class */
    OPC_DAPPEND  = (0x00 << 6) | OPC_DAPPEND_DSP,
    OPC_PREPENDD = (0x03 << 6) | OPC_DAPPEND_DSP,
    OPC_PREPENDW = (0x01 << 6) | OPC_DAPPEND_DSP,
    OPC_DBALIGN  = (0x10 << 6) | OPC_DAPPEND_DSP,
};

#define MASK_DEXTR_W(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Accumulator and DSPControl Access Sub-class */
    OPC_DMTHLIP     = (0x1F << 6) | OPC_DEXTR_W_DSP,
    OPC_DSHILO      = (0x1A << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTP       = (0x02 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTPDP     = (0x0A << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTPDPV    = (0x0B << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTPV      = (0x03 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_L     = (0x10 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_R_L   = (0x14 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_RS_L  = (0x16 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_W     = (0x00 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_R_W   = (0x04 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_RS_W  = (0x06 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTR_S_H   = (0x0E << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_L    = (0x11 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_R_L  = (0x15 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_RS_L = (0x17 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_S_H  = (0x0F << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_W    = (0x01 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_R_W  = (0x05 << 6) | OPC_DEXTR_W_DSP,
    OPC_DEXTRV_RS_W = (0x07 << 6) | OPC_DEXTR_W_DSP,
    OPC_DSHILOV     = (0x1B << 6) | OPC_DEXTR_W_DSP,
};

#define MASK_DINSV(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* DSP Bit/Manipulation Sub-class */
    OPC_DINSV = (0x00 << 6) | OPC_DINSV_DSP,
};

#define MASK_DPAQ_W_QH(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP Multiply Sub-class insns */
    OPC_DMADD         = (0x19 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DMADDU        = (0x1D << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DMSUB         = (0x1B << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DMSUBU        = (0x1F << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPA_W_QH      = (0x00 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPAQ_S_W_QH   = (0x04 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPAQ_SA_L_PW  = (0x0C << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPAU_H_OBL    = (0x03 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPAU_H_OBR    = (0x07 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPS_W_QH      = (0x01 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPSQ_S_W_QH   = (0x05 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPSQ_SA_L_PW  = (0x0D << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPSU_H_OBL    = (0x0B << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_DPSU_H_OBR    = (0x0F << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_S_L_PWL   = (0x1C << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_S_L_PWR   = (0x1E << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_S_W_QHLL  = (0x14 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_SA_W_QHLL = (0x10 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_S_W_QHLR  = (0x15 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_SA_W_QHLR = (0x11 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_S_W_QHRL  = (0x16 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_SA_W_QHRL = (0x12 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_S_W_QHRR  = (0x17 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MAQ_SA_W_QHRR = (0x13 << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MULSAQ_S_L_PW = (0x0E << 6) | OPC_DPAQ_W_QH_DSP,
    OPC_MULSAQ_S_W_QH = (0x06 << 6) | OPC_DPAQ_W_QH_DSP,
};

#define MASK_SHLL_OB(op) (MASK_SPECIAL3(op) | (op & (0x1F << 6)))
enum {
    /* MIPS DSP GPR-Based Shift Sub-class */
    OPC_SHLL_PW    = (0x10 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLL_S_PW  = (0x14 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLLV_OB   = (0x02 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLLV_PW   = (0x12 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLLV_S_PW = (0x16 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLLV_QH   = (0x0A << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLLV_S_QH = (0x0E << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRA_PW    = (0x11 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRA_R_PW  = (0x15 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRAV_OB   = (0x06 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRAV_R_OB = (0x07 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRAV_PW   = (0x13 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRAV_R_PW = (0x17 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRAV_QH   = (0x0B << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRAV_R_QH = (0x0F << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRLV_OB   = (0x03 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRLV_QH   = (0x1B << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLL_OB    = (0x00 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLL_QH    = (0x08 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHLL_S_QH  = (0x0C << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRA_OB    = (0x04 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRA_R_OB  = (0x05 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRA_QH    = (0x09 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRA_R_QH  = (0x0D << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRL_OB    = (0x01 << 6) | OPC_SHLL_OB_DSP,
    OPC_SHRL_QH    = (0x19 << 6) | OPC_SHLL_OB_DSP,
};

/* Coprocessor 0 (rs field) */
#define MASK_CP0(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

enum {
    OPC_MFC0     = (0x00 << 21) | OPC_CP0,
    OPC_DMFC0    = (0x01 << 21) | OPC_CP0,
    OPC_MFHC0    = (0x02 << 21) | OPC_CP0,
    OPC_MTC0     = (0x04 << 21) | OPC_CP0,
    OPC_DMTC0    = (0x05 << 21) | OPC_CP0,
    OPC_MTHC0    = (0x06 << 21) | OPC_CP0,
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
    OPC_DVP      = 0x04 | (0 << 3) | (1 << 5) | (0 << 11) | OPC_MFMC0,
    OPC_EVP      = 0x04 | (0 << 3) | (0 << 5) | (0 << 11) | OPC_MFMC0,
};

/* Coprocessor 0 (with rs == C0) */
#define MASK_C0(op)        MASK_CP0(op) | (op & 0x3F)

enum {
    OPC_TLBR     = 0x01 | OPC_C0,
    OPC_TLBWI    = 0x02 | OPC_C0,
    OPC_TLBINV   = 0x03 | OPC_C0,
    OPC_TLBINVF  = 0x04 | OPC_C0,
    OPC_TLBWR    = 0x06 | OPC_C0,
    OPC_TLBP     = 0x08 | OPC_C0,
    OPC_RFE      = 0x10 | OPC_C0,
    OPC_ERET     = 0x18 | OPC_C0,
    OPC_DERET    = 0x1F | OPC_C0,
    OPC_WAIT     = 0x20 | OPC_C0,
};

/* Coprocessor 1 (rs field) */
#define MASK_CP1(op)       MASK_OP_MAJOR(op) | (op & (0x1F << 21))

/* Values for the fmt field in FP instructions */
enum {
    /* 0 - 15 are reserved */
    FMT_S = 16,          /* single fp */
    FMT_D = 17,          /* double fp */
    FMT_E = 18,          /* extended fp */
    FMT_Q = 19,          /* quad fp */
    FMT_W = 20,          /* 32-bit fixed */
    FMT_L = 21,          /* 64-bit fixed */
    FMT_PS = 22,         /* paired single fp */
    /* 23 - 31 are reserved */
};

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
    OPC_BZ_V     = (0x0B << 21) | OPC_CP1,
    OPC_BNZ_V    = (0x0F << 21) | OPC_CP1,
    OPC_S_FMT    = (FMT_S << 21) | OPC_CP1,
    OPC_D_FMT    = (FMT_D << 21) | OPC_CP1,
    OPC_E_FMT    = (FMT_E << 21) | OPC_CP1,
    OPC_Q_FMT    = (FMT_Q << 21) | OPC_CP1,
    OPC_W_FMT    = (FMT_W << 21) | OPC_CP1,
    OPC_L_FMT    = (FMT_L << 21) | OPC_CP1,
    OPC_PS_FMT   = (FMT_PS << 21) | OPC_CP1,
    OPC_BC1EQZ   = (0x09 << 21) | OPC_CP1,
    OPC_BC1NEZ   = (0x0D << 21) | OPC_CP1,
    OPC_BZ_B     = (0x18 << 21) | OPC_CP1,
    OPC_BZ_H     = (0x19 << 21) | OPC_CP1,
    OPC_BZ_W     = (0x1A << 21) | OPC_CP1,
    OPC_BZ_D     = (0x1B << 21) | OPC_CP1,
    OPC_BNZ_B    = (0x1C << 21) | OPC_CP1,
    OPC_BNZ_H    = (0x1D << 21) | OPC_CP1,
    OPC_BNZ_W    = (0x1E << 21) | OPC_CP1,
    OPC_BNZ_D    = (0x1F << 21) | OPC_CP1,
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
    OPC_BC2EQZ  = (0x09 << 21) | OPC_CP2,
    OPC_BC2NEZ  = (0x0D << 21) | OPC_CP2,
};

#define MASK_LMI(op)  (MASK_OP_MAJOR(op) | (op & (0x1F << 21)) | (op & 0x1F))

enum {
    OPC_PADDSH  = (24 << 21) | (0x00) | OPC_CP2,
    OPC_PADDUSH = (25 << 21) | (0x00) | OPC_CP2,
    OPC_PADDH   = (26 << 21) | (0x00) | OPC_CP2,
    OPC_PADDW   = (27 << 21) | (0x00) | OPC_CP2,
    OPC_PADDSB  = (28 << 21) | (0x00) | OPC_CP2,
    OPC_PADDUSB = (29 << 21) | (0x00) | OPC_CP2,
    OPC_PADDB   = (30 << 21) | (0x00) | OPC_CP2,
    OPC_PADDD   = (31 << 21) | (0x00) | OPC_CP2,

    OPC_PSUBSH  = (24 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBUSH = (25 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBH   = (26 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBW   = (27 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBSB  = (28 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBUSB = (29 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBB   = (30 << 21) | (0x01) | OPC_CP2,
    OPC_PSUBD   = (31 << 21) | (0x01) | OPC_CP2,

    OPC_PSHUFH   = (24 << 21) | (0x02) | OPC_CP2,
    OPC_PACKSSWH = (25 << 21) | (0x02) | OPC_CP2,
    OPC_PACKSSHB = (26 << 21) | (0x02) | OPC_CP2,
    OPC_PACKUSHB = (27 << 21) | (0x02) | OPC_CP2,
    OPC_XOR_CP2  = (28 << 21) | (0x02) | OPC_CP2,
    OPC_NOR_CP2  = (29 << 21) | (0x02) | OPC_CP2,
    OPC_AND_CP2  = (30 << 21) | (0x02) | OPC_CP2,
    OPC_PANDN    = (31 << 21) | (0x02) | OPC_CP2,

    OPC_PUNPCKLHW = (24 << 21) | (0x03) | OPC_CP2,
    OPC_PUNPCKHHW = (25 << 21) | (0x03) | OPC_CP2,
    OPC_PUNPCKLBH = (26 << 21) | (0x03) | OPC_CP2,
    OPC_PUNPCKHBH = (27 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_0  = (28 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_1  = (29 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_2  = (30 << 21) | (0x03) | OPC_CP2,
    OPC_PINSRH_3  = (31 << 21) | (0x03) | OPC_CP2,

    OPC_PAVGH   = (24 << 21) | (0x08) | OPC_CP2,
    OPC_PAVGB   = (25 << 21) | (0x08) | OPC_CP2,
    OPC_PMAXSH  = (26 << 21) | (0x08) | OPC_CP2,
    OPC_PMINSH  = (27 << 21) | (0x08) | OPC_CP2,
    OPC_PMAXUB  = (28 << 21) | (0x08) | OPC_CP2,
    OPC_PMINUB  = (29 << 21) | (0x08) | OPC_CP2,

    OPC_PCMPEQW = (24 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPGTW = (25 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPEQH = (26 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPGTH = (27 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPEQB = (28 << 21) | (0x09) | OPC_CP2,
    OPC_PCMPGTB = (29 << 21) | (0x09) | OPC_CP2,

    OPC_PSLLW   = (24 << 21) | (0x0A) | OPC_CP2,
    OPC_PSLLH   = (25 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULLH  = (26 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULHH  = (27 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULUW  = (28 << 21) | (0x0A) | OPC_CP2,
    OPC_PMULHUH = (29 << 21) | (0x0A) | OPC_CP2,

    OPC_PSRLW     = (24 << 21) | (0x0B) | OPC_CP2,
    OPC_PSRLH     = (25 << 21) | (0x0B) | OPC_CP2,
    OPC_PSRAW     = (26 << 21) | (0x0B) | OPC_CP2,
    OPC_PSRAH     = (27 << 21) | (0x0B) | OPC_CP2,
    OPC_PUNPCKLWD = (28 << 21) | (0x0B) | OPC_CP2,
    OPC_PUNPCKHWD = (29 << 21) | (0x0B) | OPC_CP2,

    OPC_ADDU_CP2 = (24 << 21) | (0x0C) | OPC_CP2,
    OPC_OR_CP2   = (25 << 21) | (0x0C) | OPC_CP2,
    OPC_ADD_CP2  = (26 << 21) | (0x0C) | OPC_CP2,
    OPC_DADD_CP2 = (27 << 21) | (0x0C) | OPC_CP2,
    OPC_SEQU_CP2 = (28 << 21) | (0x0C) | OPC_CP2,
    OPC_SEQ_CP2  = (29 << 21) | (0x0C) | OPC_CP2,

    OPC_SUBU_CP2 = (24 << 21) | (0x0D) | OPC_CP2,
    OPC_PASUBUB  = (25 << 21) | (0x0D) | OPC_CP2,
    OPC_SUB_CP2  = (26 << 21) | (0x0D) | OPC_CP2,
    OPC_DSUB_CP2 = (27 << 21) | (0x0D) | OPC_CP2,
    OPC_SLTU_CP2 = (28 << 21) | (0x0D) | OPC_CP2,
    OPC_SLT_CP2  = (29 << 21) | (0x0D) | OPC_CP2,

    OPC_SLL_CP2  = (24 << 21) | (0x0E) | OPC_CP2,
    OPC_DSLL_CP2 = (25 << 21) | (0x0E) | OPC_CP2,
    OPC_PEXTRH   = (26 << 21) | (0x0E) | OPC_CP2,
    OPC_PMADDHW  = (27 << 21) | (0x0E) | OPC_CP2,
    OPC_SLEU_CP2 = (28 << 21) | (0x0E) | OPC_CP2,
    OPC_SLE_CP2  = (29 << 21) | (0x0E) | OPC_CP2,

    OPC_SRL_CP2  = (24 << 21) | (0x0F) | OPC_CP2,
    OPC_DSRL_CP2 = (25 << 21) | (0x0F) | OPC_CP2,
    OPC_SRA_CP2  = (26 << 21) | (0x0F) | OPC_CP2,
    OPC_DSRA_CP2 = (27 << 21) | (0x0F) | OPC_CP2,
    OPC_BIADD    = (28 << 21) | (0x0F) | OPC_CP2,
    OPC_PMOVMSKB = (29 << 21) | (0x0F) | OPC_CP2,
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

/* MSA Opcodes */
#define MASK_MSA_MINOR(op)    (MASK_OP_MAJOR(op) | (op & 0x3F))
enum {
    OPC_MSA_I8_00   = 0x00 | OPC_MSA,
    OPC_MSA_I8_01   = 0x01 | OPC_MSA,
    OPC_MSA_I8_02   = 0x02 | OPC_MSA,
    OPC_MSA_I5_06   = 0x06 | OPC_MSA,
    OPC_MSA_I5_07   = 0x07 | OPC_MSA,
    OPC_MSA_BIT_09  = 0x09 | OPC_MSA,
    OPC_MSA_BIT_0A  = 0x0A | OPC_MSA,
    OPC_MSA_3R_0D   = 0x0D | OPC_MSA,
    OPC_MSA_3R_0E   = 0x0E | OPC_MSA,
    OPC_MSA_3R_0F   = 0x0F | OPC_MSA,
    OPC_MSA_3R_10   = 0x10 | OPC_MSA,
    OPC_MSA_3R_11   = 0x11 | OPC_MSA,
    OPC_MSA_3R_12   = 0x12 | OPC_MSA,
    OPC_MSA_3R_13   = 0x13 | OPC_MSA,
    OPC_MSA_3R_14   = 0x14 | OPC_MSA,
    OPC_MSA_3R_15   = 0x15 | OPC_MSA,
    OPC_MSA_ELM     = 0x19 | OPC_MSA,
    OPC_MSA_3RF_1A  = 0x1A | OPC_MSA,
    OPC_MSA_3RF_1B  = 0x1B | OPC_MSA,
    OPC_MSA_3RF_1C  = 0x1C | OPC_MSA,
    OPC_MSA_VEC     = 0x1E | OPC_MSA,

    /* MI10 instruction */
    OPC_LD_B    = (0x20) | OPC_MSA,
    OPC_LD_H    = (0x21) | OPC_MSA,
    OPC_LD_W    = (0x22) | OPC_MSA,
    OPC_LD_D    = (0x23) | OPC_MSA,
    OPC_ST_B    = (0x24) | OPC_MSA,
    OPC_ST_H    = (0x25) | OPC_MSA,
    OPC_ST_W    = (0x26) | OPC_MSA,
    OPC_ST_D    = (0x27) | OPC_MSA,
};

enum {
    /* I5 instruction df(bits 22..21) = _b, _h, _w, _d */
    OPC_ADDVI_df    = (0x0 << 23) | OPC_MSA_I5_06,
    OPC_CEQI_df     = (0x0 << 23) | OPC_MSA_I5_07,
    OPC_SUBVI_df    = (0x1 << 23) | OPC_MSA_I5_06,
    OPC_MAXI_S_df   = (0x2 << 23) | OPC_MSA_I5_06,
    OPC_CLTI_S_df   = (0x2 << 23) | OPC_MSA_I5_07,
    OPC_MAXI_U_df   = (0x3 << 23) | OPC_MSA_I5_06,
    OPC_CLTI_U_df   = (0x3 << 23) | OPC_MSA_I5_07,
    OPC_MINI_S_df   = (0x4 << 23) | OPC_MSA_I5_06,
    OPC_CLEI_S_df   = (0x4 << 23) | OPC_MSA_I5_07,
    OPC_MINI_U_df   = (0x5 << 23) | OPC_MSA_I5_06,
    OPC_CLEI_U_df   = (0x5 << 23) | OPC_MSA_I5_07,
    OPC_LDI_df      = (0x6 << 23) | OPC_MSA_I5_07,

    /* I8 instruction */
    OPC_ANDI_B  = (0x0 << 24) | OPC_MSA_I8_00,
    OPC_BMNZI_B = (0x0 << 24) | OPC_MSA_I8_01,
    OPC_SHF_B   = (0x0 << 24) | OPC_MSA_I8_02,
    OPC_ORI_B   = (0x1 << 24) | OPC_MSA_I8_00,
    OPC_BMZI_B  = (0x1 << 24) | OPC_MSA_I8_01,
    OPC_SHF_H   = (0x1 << 24) | OPC_MSA_I8_02,
    OPC_NORI_B  = (0x2 << 24) | OPC_MSA_I8_00,
    OPC_BSELI_B = (0x2 << 24) | OPC_MSA_I8_01,
    OPC_SHF_W   = (0x2 << 24) | OPC_MSA_I8_02,
    OPC_XORI_B  = (0x3 << 24) | OPC_MSA_I8_00,

    /* VEC/2R/2RF instruction */
    OPC_AND_V   = (0x00 << 21) | OPC_MSA_VEC,
    OPC_OR_V    = (0x01 << 21) | OPC_MSA_VEC,
    OPC_NOR_V   = (0x02 << 21) | OPC_MSA_VEC,
    OPC_XOR_V   = (0x03 << 21) | OPC_MSA_VEC,
    OPC_BMNZ_V  = (0x04 << 21) | OPC_MSA_VEC,
    OPC_BMZ_V   = (0x05 << 21) | OPC_MSA_VEC,
    OPC_BSEL_V  = (0x06 << 21) | OPC_MSA_VEC,

    OPC_MSA_2R      = (0x18 << 21) | OPC_MSA_VEC,
    OPC_MSA_2RF     = (0x19 << 21) | OPC_MSA_VEC,

    /* 2R instruction df(bits 17..16) = _b, _h, _w, _d */
    OPC_FILL_df = (0x00 << 18) | OPC_MSA_2R,
    OPC_PCNT_df = (0x01 << 18) | OPC_MSA_2R,
    OPC_NLOC_df = (0x02 << 18) | OPC_MSA_2R,
    OPC_NLZC_df = (0x03 << 18) | OPC_MSA_2R,

    /* 2RF instruction df(bit 16) = _w, _d */
    OPC_FCLASS_df   = (0x00 << 17) | OPC_MSA_2RF,
    OPC_FTRUNC_S_df = (0x01 << 17) | OPC_MSA_2RF,
    OPC_FTRUNC_U_df = (0x02 << 17) | OPC_MSA_2RF,
    OPC_FSQRT_df    = (0x03 << 17) | OPC_MSA_2RF,
    OPC_FRSQRT_df   = (0x04 << 17) | OPC_MSA_2RF,
    OPC_FRCP_df     = (0x05 << 17) | OPC_MSA_2RF,
    OPC_FRINT_df    = (0x06 << 17) | OPC_MSA_2RF,
    OPC_FLOG2_df    = (0x07 << 17) | OPC_MSA_2RF,
    OPC_FEXUPL_df   = (0x08 << 17) | OPC_MSA_2RF,
    OPC_FEXUPR_df   = (0x09 << 17) | OPC_MSA_2RF,
    OPC_FFQL_df     = (0x0A << 17) | OPC_MSA_2RF,
    OPC_FFQR_df     = (0x0B << 17) | OPC_MSA_2RF,
    OPC_FTINT_S_df  = (0x0C << 17) | OPC_MSA_2RF,
    OPC_FTINT_U_df  = (0x0D << 17) | OPC_MSA_2RF,
    OPC_FFINT_S_df  = (0x0E << 17) | OPC_MSA_2RF,
    OPC_FFINT_U_df  = (0x0F << 17) | OPC_MSA_2RF,

    /* 3R instruction df(bits 22..21) = _b, _h, _w, d */
    OPC_SLL_df      = (0x0 << 23) | OPC_MSA_3R_0D,
    OPC_ADDV_df     = (0x0 << 23) | OPC_MSA_3R_0E,
    OPC_CEQ_df      = (0x0 << 23) | OPC_MSA_3R_0F,
    OPC_ADD_A_df    = (0x0 << 23) | OPC_MSA_3R_10,
    OPC_SUBS_S_df   = (0x0 << 23) | OPC_MSA_3R_11,
    OPC_MULV_df     = (0x0 << 23) | OPC_MSA_3R_12,
    OPC_DOTP_S_df   = (0x0 << 23) | OPC_MSA_3R_13,
    OPC_SLD_df      = (0x0 << 23) | OPC_MSA_3R_14,
    OPC_VSHF_df     = (0x0 << 23) | OPC_MSA_3R_15,
    OPC_SRA_df      = (0x1 << 23) | OPC_MSA_3R_0D,
    OPC_SUBV_df     = (0x1 << 23) | OPC_MSA_3R_0E,
    OPC_ADDS_A_df   = (0x1 << 23) | OPC_MSA_3R_10,
    OPC_SUBS_U_df   = (0x1 << 23) | OPC_MSA_3R_11,
    OPC_MADDV_df    = (0x1 << 23) | OPC_MSA_3R_12,
    OPC_DOTP_U_df   = (0x1 << 23) | OPC_MSA_3R_13,
    OPC_SPLAT_df    = (0x1 << 23) | OPC_MSA_3R_14,
    OPC_SRAR_df     = (0x1 << 23) | OPC_MSA_3R_15,
    OPC_SRL_df      = (0x2 << 23) | OPC_MSA_3R_0D,
    OPC_MAX_S_df    = (0x2 << 23) | OPC_MSA_3R_0E,
    OPC_CLT_S_df    = (0x2 << 23) | OPC_MSA_3R_0F,
    OPC_ADDS_S_df   = (0x2 << 23) | OPC_MSA_3R_10,
    OPC_SUBSUS_U_df = (0x2 << 23) | OPC_MSA_3R_11,
    OPC_MSUBV_df    = (0x2 << 23) | OPC_MSA_3R_12,
    OPC_DPADD_S_df  = (0x2 << 23) | OPC_MSA_3R_13,
    OPC_PCKEV_df    = (0x2 << 23) | OPC_MSA_3R_14,
    OPC_SRLR_df     = (0x2 << 23) | OPC_MSA_3R_15,
    OPC_BCLR_df     = (0x3 << 23) | OPC_MSA_3R_0D,
    OPC_MAX_U_df    = (0x3 << 23) | OPC_MSA_3R_0E,
    OPC_CLT_U_df    = (0x3 << 23) | OPC_MSA_3R_0F,
    OPC_ADDS_U_df   = (0x3 << 23) | OPC_MSA_3R_10,
    OPC_SUBSUU_S_df = (0x3 << 23) | OPC_MSA_3R_11,
    OPC_DPADD_U_df  = (0x3 << 23) | OPC_MSA_3R_13,
    OPC_PCKOD_df    = (0x3 << 23) | OPC_MSA_3R_14,
    OPC_BSET_df     = (0x4 << 23) | OPC_MSA_3R_0D,
    OPC_MIN_S_df    = (0x4 << 23) | OPC_MSA_3R_0E,
    OPC_CLE_S_df    = (0x4 << 23) | OPC_MSA_3R_0F,
    OPC_AVE_S_df    = (0x4 << 23) | OPC_MSA_3R_10,
    OPC_ASUB_S_df   = (0x4 << 23) | OPC_MSA_3R_11,
    OPC_DIV_S_df    = (0x4 << 23) | OPC_MSA_3R_12,
    OPC_DPSUB_S_df  = (0x4 << 23) | OPC_MSA_3R_13,
    OPC_ILVL_df     = (0x4 << 23) | OPC_MSA_3R_14,
    OPC_HADD_S_df   = (0x4 << 23) | OPC_MSA_3R_15,
    OPC_BNEG_df     = (0x5 << 23) | OPC_MSA_3R_0D,
    OPC_MIN_U_df    = (0x5 << 23) | OPC_MSA_3R_0E,
    OPC_CLE_U_df    = (0x5 << 23) | OPC_MSA_3R_0F,
    OPC_AVE_U_df    = (0x5 << 23) | OPC_MSA_3R_10,
    OPC_ASUB_U_df   = (0x5 << 23) | OPC_MSA_3R_11,
    OPC_DIV_U_df    = (0x5 << 23) | OPC_MSA_3R_12,
    OPC_DPSUB_U_df  = (0x5 << 23) | OPC_MSA_3R_13,
    OPC_ILVR_df     = (0x5 << 23) | OPC_MSA_3R_14,
    OPC_HADD_U_df   = (0x5 << 23) | OPC_MSA_3R_15,
    OPC_BINSL_df    = (0x6 << 23) | OPC_MSA_3R_0D,
    OPC_MAX_A_df    = (0x6 << 23) | OPC_MSA_3R_0E,
    OPC_AVER_S_df   = (0x6 << 23) | OPC_MSA_3R_10,
    OPC_MOD_S_df    = (0x6 << 23) | OPC_MSA_3R_12,
    OPC_ILVEV_df    = (0x6 << 23) | OPC_MSA_3R_14,
    OPC_HSUB_S_df   = (0x6 << 23) | OPC_MSA_3R_15,
    OPC_BINSR_df    = (0x7 << 23) | OPC_MSA_3R_0D,
    OPC_MIN_A_df    = (0x7 << 23) | OPC_MSA_3R_0E,
    OPC_AVER_U_df   = (0x7 << 23) | OPC_MSA_3R_10,
    OPC_MOD_U_df    = (0x7 << 23) | OPC_MSA_3R_12,
    OPC_ILVOD_df    = (0x7 << 23) | OPC_MSA_3R_14,
    OPC_HSUB_U_df   = (0x7 << 23) | OPC_MSA_3R_15,

    /* ELM instructions df(bits 21..16) = _b, _h, _w, _d */
    OPC_SLDI_df     = (0x0 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_CTCMSA      = (0x0 << 22) | (0x3E << 16) | OPC_MSA_ELM,
    OPC_SPLATI_df   = (0x1 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_CFCMSA      = (0x1 << 22) | (0x3E << 16) | OPC_MSA_ELM,
    OPC_COPY_S_df   = (0x2 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_MOVE_V      = (0x2 << 22) | (0x3E << 16) | OPC_MSA_ELM,
    OPC_COPY_U_df   = (0x3 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_INSERT_df   = (0x4 << 22) | (0x00 << 16) | OPC_MSA_ELM,
    OPC_INSVE_df    = (0x5 << 22) | (0x00 << 16) | OPC_MSA_ELM,

    /* 3RF instruction _df(bit 21) = _w, _d */
    OPC_FCAF_df     = (0x0 << 22) | OPC_MSA_3RF_1A,
    OPC_FADD_df     = (0x0 << 22) | OPC_MSA_3RF_1B,
    OPC_FCUN_df     = (0x1 << 22) | OPC_MSA_3RF_1A,
    OPC_FSUB_df     = (0x1 << 22) | OPC_MSA_3RF_1B,
    OPC_FCOR_df     = (0x1 << 22) | OPC_MSA_3RF_1C,
    OPC_FCEQ_df     = (0x2 << 22) | OPC_MSA_3RF_1A,
    OPC_FMUL_df     = (0x2 << 22) | OPC_MSA_3RF_1B,
    OPC_FCUNE_df    = (0x2 << 22) | OPC_MSA_3RF_1C,
    OPC_FCUEQ_df    = (0x3 << 22) | OPC_MSA_3RF_1A,
    OPC_FDIV_df     = (0x3 << 22) | OPC_MSA_3RF_1B,
    OPC_FCNE_df     = (0x3 << 22) | OPC_MSA_3RF_1C,
    OPC_FCLT_df     = (0x4 << 22) | OPC_MSA_3RF_1A,
    OPC_FMADD_df    = (0x4 << 22) | OPC_MSA_3RF_1B,
    OPC_MUL_Q_df    = (0x4 << 22) | OPC_MSA_3RF_1C,
    OPC_FCULT_df    = (0x5 << 22) | OPC_MSA_3RF_1A,
    OPC_FMSUB_df    = (0x5 << 22) | OPC_MSA_3RF_1B,
    OPC_MADD_Q_df   = (0x5 << 22) | OPC_MSA_3RF_1C,
    OPC_FCLE_df     = (0x6 << 22) | OPC_MSA_3RF_1A,
    OPC_MSUB_Q_df   = (0x6 << 22) | OPC_MSA_3RF_1C,
    OPC_FCULE_df    = (0x7 << 22) | OPC_MSA_3RF_1A,
    OPC_FEXP2_df    = (0x7 << 22) | OPC_MSA_3RF_1B,
    OPC_FSAF_df     = (0x8 << 22) | OPC_MSA_3RF_1A,
    OPC_FEXDO_df    = (0x8 << 22) | OPC_MSA_3RF_1B,
    OPC_FSUN_df     = (0x9 << 22) | OPC_MSA_3RF_1A,
    OPC_FSOR_df     = (0x9 << 22) | OPC_MSA_3RF_1C,
    OPC_FSEQ_df     = (0xA << 22) | OPC_MSA_3RF_1A,
    OPC_FTQ_df      = (0xA << 22) | OPC_MSA_3RF_1B,
    OPC_FSUNE_df    = (0xA << 22) | OPC_MSA_3RF_1C,
    OPC_FSUEQ_df    = (0xB << 22) | OPC_MSA_3RF_1A,
    OPC_FSNE_df     = (0xB << 22) | OPC_MSA_3RF_1C,
    OPC_FSLT_df     = (0xC << 22) | OPC_MSA_3RF_1A,
    OPC_FMIN_df     = (0xC << 22) | OPC_MSA_3RF_1B,
    OPC_MULR_Q_df   = (0xC << 22) | OPC_MSA_3RF_1C,
    OPC_FSULT_df    = (0xD << 22) | OPC_MSA_3RF_1A,
    OPC_FMIN_A_df   = (0xD << 22) | OPC_MSA_3RF_1B,
    OPC_MADDR_Q_df  = (0xD << 22) | OPC_MSA_3RF_1C,
    OPC_FSLE_df     = (0xE << 22) | OPC_MSA_3RF_1A,
    OPC_FMAX_df     = (0xE << 22) | OPC_MSA_3RF_1B,
    OPC_MSUBR_Q_df  = (0xE << 22) | OPC_MSA_3RF_1C,
    OPC_FSULE_df    = (0xF << 22) | OPC_MSA_3RF_1A,
    OPC_FMAX_A_df   = (0xF << 22) | OPC_MSA_3RF_1B,

    /* BIT instruction df(bits 22..16) = _B _H _W _D */
    OPC_SLLI_df     = (0x0 << 23) | OPC_MSA_BIT_09,
    OPC_SAT_S_df    = (0x0 << 23) | OPC_MSA_BIT_0A,
    OPC_SRAI_df     = (0x1 << 23) | OPC_MSA_BIT_09,
    OPC_SAT_U_df    = (0x1 << 23) | OPC_MSA_BIT_0A,
    OPC_SRLI_df     = (0x2 << 23) | OPC_MSA_BIT_09,
    OPC_SRARI_df    = (0x2 << 23) | OPC_MSA_BIT_0A,
    OPC_BCLRI_df    = (0x3 << 23) | OPC_MSA_BIT_09,
    OPC_SRLRI_df    = (0x3 << 23) | OPC_MSA_BIT_0A,
    OPC_BSETI_df    = (0x4 << 23) | OPC_MSA_BIT_09,
    OPC_BNEGI_df    = (0x5 << 23) | OPC_MSA_BIT_09,
    OPC_BINSLI_df   = (0x6 << 23) | OPC_MSA_BIT_09,
    OPC_BINSRI_df   = (0x7 << 23) | OPC_MSA_BIT_09,
};

/* global register indices */
static TCGv_env cpu_env;
static TCGv cpu_gpr[32], cpu_PC;
static TCGv cpu_HI[MIPS_DSP_ACC], cpu_LO[MIPS_DSP_ACC];
static TCGv cpu_dspctrl, btarget, bcond;
static TCGv_i32 hflags;
static TCGv_i32 fpu_fcr0, fpu_fcr31;
static TCGv_i64 fpu_f64[32];
static TCGv_i64 msa_wr_d[64];

#include "exec/gen-icount.h"

#define gen_helper_0e0i(name, arg) do {                           \
    TCGv_i32 helper_tmp = tcg_const_i32(arg);                     \
    gen_helper_##name(cpu_env, helper_tmp);                       \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_0e1i(name, arg1, arg2) do {                    \
    TCGv_i32 helper_tmp = tcg_const_i32(arg2);                    \
    gen_helper_##name(cpu_env, arg1, helper_tmp);                 \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_1e0i(name, ret, arg1) do {                     \
    TCGv_i32 helper_tmp = tcg_const_i32(arg1);                    \
    gen_helper_##name(ret, cpu_env, helper_tmp);                  \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_1e1i(name, ret, arg1, arg2) do {               \
    TCGv_i32 helper_tmp = tcg_const_i32(arg2);                    \
    gen_helper_##name(ret, cpu_env, arg1, helper_tmp);            \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_0e2i(name, arg1, arg2, arg3) do {              \
    TCGv_i32 helper_tmp = tcg_const_i32(arg3);                    \
    gen_helper_##name(cpu_env, arg1, arg2, helper_tmp);           \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_1e2i(name, ret, arg1, arg2, arg3) do {         \
    TCGv_i32 helper_tmp = tcg_const_i32(arg3);                    \
    gen_helper_##name(ret, cpu_env, arg1, arg2, helper_tmp);      \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

#define gen_helper_0e3i(name, arg1, arg2, arg3, arg4) do {        \
    TCGv_i32 helper_tmp = tcg_const_i32(arg4);                    \
    gen_helper_##name(cpu_env, arg1, arg2, arg3, helper_tmp);     \
    tcg_temp_free_i32(helper_tmp);                                \
    } while(0)

typedef struct DisasContext {
    struct TranslationBlock *tb;
    target_ulong pc, saved_pc;
    uint32_t opcode;
    int singlestep_enabled;
    int insn_flags;
    int32_t CP0_Config1;
    /* Routine used to access memory */
    int mem_idx;
    TCGMemOp default_tcg_memop_mask;
    uint32_t hflags, saved_hflags;
    int bstate;
    target_ulong btarget;
    bool ulri;
    int kscrexist;
    bool rxi;
    int ie;
    bool bi;
    bool bp;
    uint64_t PAMask;
    bool mvh;
    bool eva;
    bool sc;
    int CP0_LLAddr_shift;
    bool ps;
    bool vp;
    bool cmgcr;
    bool mrp;
    bool nan2008;
    bool abs2008;
} DisasContext;

enum {
    BS_NONE     = 0, /* We go out of the TB without reaching a branch or an
                      * exception condition */
    BS_STOP     = 1, /* We want to stop translation for any reason */
    BS_BRANCH   = 2, /* We reached a branch condition     */
    BS_EXCP     = 3, /* We reached an exception condition */
};

static const char * const regnames[] = {
    "r0", "at", "v0", "v1", "a0", "a1", "a2", "a3",
    "t0", "t1", "t2", "t3", "t4", "t5", "t6", "t7",
    "s0", "s1", "s2", "s3", "s4", "s5", "s6", "s7",
    "t8", "t9", "k0", "k1", "gp", "sp", "s8", "ra",
};

static const char * const regnames_HI[] = {
    "HI0", "HI1", "HI2", "HI3",
};

static const char * const regnames_LO[] = {
    "LO0", "LO1", "LO2", "LO3",
};

static const char * const fregnames[] = {
    "f0",  "f1",  "f2",  "f3",  "f4",  "f5",  "f6",  "f7",
    "f8",  "f9",  "f10", "f11", "f12", "f13", "f14", "f15",
    "f16", "f17", "f18", "f19", "f20", "f21", "f22", "f23",
    "f24", "f25", "f26", "f27", "f28", "f29", "f30", "f31",
};

static const char * const msaregnames[] = {
    "w0.d0",  "w0.d1",  "w1.d0",  "w1.d1",
    "w2.d0",  "w2.d1",  "w3.d0",  "w3.d1",
    "w4.d0",  "w4.d1",  "w5.d0",  "w5.d1",
    "w6.d0",  "w6.d1",  "w7.d0",  "w7.d1",
    "w8.d0",  "w8.d1",  "w9.d0",  "w9.d1",
    "w10.d0", "w10.d1", "w11.d0", "w11.d1",
    "w12.d0", "w12.d1", "w13.d0", "w13.d1",
    "w14.d0", "w14.d1", "w15.d0", "w15.d1",
    "w16.d0", "w16.d1", "w17.d0", "w17.d1",
    "w18.d0", "w18.d1", "w19.d0", "w19.d1",
    "w20.d0", "w20.d1", "w21.d0", "w21.d1",
    "w22.d0", "w22.d1", "w23.d0", "w23.d1",
    "w24.d0", "w24.d1", "w25.d0", "w25.d1",
    "w26.d0", "w26.d1", "w27.d0", "w27.d1",
    "w28.d0", "w28.d1", "w29.d0", "w29.d1",
    "w30.d0", "w30.d1", "w31.d0", "w31.d1",
};

#define LOG_DISAS(...)                                                        \
    do {                                                                      \
        if (MIPS_DEBUG_DISAS) {                                               \
            qemu_log_mask(CPU_LOG_TB_IN_ASM, ## __VA_ARGS__);                 \
        }                                                                     \
    } while (0)

#define MIPS_INVAL(op)                                                        \
    do {                                                                      \
        if (MIPS_DEBUG_DISAS) {                                               \
            qemu_log_mask(CPU_LOG_TB_IN_ASM,                                  \
                          TARGET_FMT_lx ": %08x Invalid %s %03x %03x %03x\n", \
                          ctx->pc, ctx->opcode, op, ctx->opcode >> 26,        \
                          ctx->opcode & 0x3F, ((ctx->opcode >> 16) & 0x1F));  \
        }                                                                     \
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

/* Moves to/from shadow registers. */
static inline void gen_load_srsgpr (int from, int to)
{
    TCGv t0 = tcg_temp_new();

    if (from == 0)
        tcg_gen_movi_tl(t0, 0);
    else {
        TCGv_i32 t2 = tcg_temp_new_i32();
        TCGv_ptr addr = tcg_temp_new_ptr();

        tcg_gen_ld_i32(t2, cpu_env, offsetof(CPUMIPSState, CP0_SRSCtl));
        tcg_gen_shri_i32(t2, t2, CP0SRSCtl_PSS);
        tcg_gen_andi_i32(t2, t2, 0xf);
        tcg_gen_muli_i32(t2, t2, sizeof(target_ulong) * 32);
        tcg_gen_ext_i32_ptr(addr, t2);
        tcg_gen_add_ptr(addr, cpu_env, addr);

        tcg_gen_ld_tl(t0, addr, sizeof(target_ulong) * from);
        tcg_temp_free_ptr(addr);
        tcg_temp_free_i32(t2);
    }
    gen_store_gpr(t0, to);
    tcg_temp_free(t0);
}

static inline void gen_store_srsgpr (int from, int to)
{
    if (to != 0) {
        TCGv t0 = tcg_temp_new();
        TCGv_i32 t2 = tcg_temp_new_i32();
        TCGv_ptr addr = tcg_temp_new_ptr();

        gen_load_gpr(t0, from);
        tcg_gen_ld_i32(t2, cpu_env, offsetof(CPUMIPSState, CP0_SRSCtl));
        tcg_gen_shri_i32(t2, t2, CP0SRSCtl_PSS);
        tcg_gen_andi_i32(t2, t2, 0xf);
        tcg_gen_muli_i32(t2, t2, sizeof(target_ulong) * 32);
        tcg_gen_ext_i32_ptr(addr, t2);
        tcg_gen_add_ptr(addr, cpu_env, addr);

        tcg_gen_st_tl(t0, addr, sizeof(target_ulong) * to);
        tcg_temp_free_ptr(addr);
        tcg_temp_free_i32(t2);
        tcg_temp_free(t0);
    }
}

/* Tests */
static inline void gen_save_pc(target_ulong pc)
{
    tcg_gen_movi_tl(cpu_PC, pc);
}

static inline void save_cpu_state(DisasContext *ctx, int do_save_pc)
{
    LOG_DISAS("hflags %08x saved %08x\n", ctx->hflags, ctx->saved_hflags);
    if (do_save_pc && ctx->pc != ctx->saved_pc) {
        gen_save_pc(ctx->pc);
        ctx->saved_pc = ctx->pc;
    }
    if (ctx->hflags != ctx->saved_hflags) {
        tcg_gen_movi_i32(hflags, ctx->hflags);
        ctx->saved_hflags = ctx->hflags;
        switch (ctx->hflags & MIPS_HFLAG_BMASK_BASE) {
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

static inline void restore_cpu_state(CPUMIPSState *env, DisasContext *ctx)
{
    ctx->saved_hflags = ctx->hflags;
    switch (ctx->hflags & MIPS_HFLAG_BMASK_BASE) {
    case MIPS_HFLAG_BR:
        break;
    case MIPS_HFLAG_BC:
    case MIPS_HFLAG_BL:
    case MIPS_HFLAG_B:
        ctx->btarget = env->btarget;
        break;
    }
}

static inline void generate_exception_err(DisasContext *ctx, int excp, int err)
{
    TCGv_i32 texcp = tcg_const_i32(excp);
    TCGv_i32 terr = tcg_const_i32(err);
    save_cpu_state(ctx, 1);
    gen_helper_raise_exception_err(cpu_env, texcp, terr);
    tcg_temp_free_i32(terr);
    tcg_temp_free_i32(texcp);
    ctx->bstate = BS_EXCP;
}

static inline void generate_exception(DisasContext *ctx, int excp)
{
    gen_helper_0e0i(raise_exception, excp);
}

static inline void generate_exception_end(DisasContext *ctx, int excp)
{
    generate_exception_err(ctx, excp, 0);
}

/* Floating point register moves. */
static void gen_load_fpr32(DisasContext *ctx, TCGv_i32 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_FRE) {
        generate_exception(ctx, EXCP_RI);
    }
    tcg_gen_extrl_i64_i32(t, fpu_f64[reg]);
}

static void gen_store_fpr32(DisasContext *ctx, TCGv_i32 t, int reg)
{
    TCGv_i64 t64;
    if (ctx->hflags & MIPS_HFLAG_FRE) {
        generate_exception(ctx, EXCP_RI);
    }
    t64 = tcg_temp_new_i64();
    tcg_gen_extu_i32_i64(t64, t);
    tcg_gen_deposit_i64(fpu_f64[reg], fpu_f64[reg], t64, 0, 32);
    tcg_temp_free_i64(t64);
}

static void gen_load_fpr32h(DisasContext *ctx, TCGv_i32 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_F64) {
        tcg_gen_extrh_i64_i32(t, fpu_f64[reg]);
    } else {
        gen_load_fpr32(ctx, t, reg | 1);
    }
}

static void gen_store_fpr32h(DisasContext *ctx, TCGv_i32 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_F64) {
        TCGv_i64 t64 = tcg_temp_new_i64();
        tcg_gen_extu_i32_i64(t64, t);
        tcg_gen_deposit_i64(fpu_f64[reg], fpu_f64[reg], t64, 32, 32);
        tcg_temp_free_i64(t64);
    } else {
        gen_store_fpr32(ctx, t, reg | 1);
    }
}

static void gen_load_fpr64(DisasContext *ctx, TCGv_i64 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_F64) {
        tcg_gen_mov_i64(t, fpu_f64[reg]);
    } else {
        tcg_gen_concat32_i64(t, fpu_f64[reg & ~1], fpu_f64[reg | 1]);
    }
}

static void gen_store_fpr64(DisasContext *ctx, TCGv_i64 t, int reg)
{
    if (ctx->hflags & MIPS_HFLAG_F64) {
        tcg_gen_mov_i64(fpu_f64[reg], t);
    } else {
        TCGv_i64 t0;
        tcg_gen_deposit_i64(fpu_f64[reg & ~1], fpu_f64[reg & ~1], t, 0, 32);
        t0 = tcg_temp_new_i64();
        tcg_gen_shri_i64(t0, t, 32);
        tcg_gen_deposit_i64(fpu_f64[reg | 1], fpu_f64[reg | 1], t0, 0, 32);
        tcg_temp_free_i64(t0);
    }
}

static inline int get_fp_bit (int cc)
{
    if (cc)
        return 24 + cc;
    else
        return 23;
}

/* Addresses computation */
static inline void gen_op_addr_add (DisasContext *ctx, TCGv ret, TCGv arg0, TCGv arg1)
{
    tcg_gen_add_tl(ret, arg0, arg1);

#if defined(TARGET_MIPS64)
    if (ctx->hflags & MIPS_HFLAG_AWRAP) {
        tcg_gen_ext32s_i64(ret, ret);
    }
#endif
}

/* Addresses computation (translation time) */
static target_long addr_add(DisasContext *ctx, target_long base,
                            target_long offset)
{
    target_long sum = base + offset;

#if defined(TARGET_MIPS64)
    if (ctx->hflags & MIPS_HFLAG_AWRAP) {
        sum = (int32_t)sum;
    }
#endif
    return sum;
}

/* Sign-extract the low 32-bits to a target_long.  */
static inline void gen_move_low32(TCGv ret, TCGv_i64 arg)
{
#if defined(TARGET_MIPS64)
    tcg_gen_ext32s_i64(ret, arg);
#else
    tcg_gen_extrl_i64_i32(ret, arg);
#endif
}

/* Sign-extract the high 32-bits to a target_long.  */
static inline void gen_move_high32(TCGv ret, TCGv_i64 arg)
{
#if defined(TARGET_MIPS64)
    tcg_gen_sari_i64(ret, arg, 32);
#else
    tcg_gen_extrh_i64_i32(ret, arg);
#endif
}

static inline void check_cp0_enabled(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_CP0)))
        generate_exception_err(ctx, EXCP_CpU, 0);
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
        generate_exception_end(ctx, EXCP_RI);
}

/* Verify that the processor is running with 64-bit floating-point
   operations enabled.  */

static inline void check_cp1_64bitmode(DisasContext *ctx)
{
    if (unlikely(~ctx->hflags & (MIPS_HFLAG_F64 | MIPS_HFLAG_COP1X)))
        generate_exception_end(ctx, EXCP_RI);
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
        generate_exception_end(ctx, EXCP_RI);
}

/* Verify that the processor is running with DSP instructions enabled.
   This is enabled by CP0 Status register MX(24) bit.
 */

static inline void check_dsp(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_DSP))) {
        if (ctx->insn_flags & ASE_DSP) {
            generate_exception_end(ctx, EXCP_DSPDIS);
        } else {
            generate_exception_end(ctx, EXCP_RI);
        }
    }
}

static inline void check_dspr2(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_DSPR2))) {
        if (ctx->insn_flags & ASE_DSP) {
            generate_exception_end(ctx, EXCP_DSPDIS);
        } else {
            generate_exception_end(ctx, EXCP_RI);
        }
    }
}

/* This code generates a "reserved instruction" exception if the
   CPU does not support the instruction set corresponding to flags. */
static inline void check_insn(DisasContext *ctx, int flags)
{
    if (unlikely(!(ctx->insn_flags & flags))) {
        generate_exception_end(ctx, EXCP_RI);
    }
}

/* This code generates a "reserved instruction" exception if the
   CPU has corresponding flag set which indicates that the instruction
   has been removed. */
static inline void check_insn_opc_removed(DisasContext *ctx, int flags)
{
    if (unlikely(ctx->insn_flags & flags)) {
        generate_exception_end(ctx, EXCP_RI);
    }
}

/* This code generates a "reserved instruction" exception if the
   CPU does not support 64-bit paired-single (PS) floating point data type */
static inline void check_ps(DisasContext *ctx)
{
    if (unlikely(!ctx->ps)) {
        generate_exception(ctx, EXCP_RI);
    }
    check_cp1_64bitmode(ctx);
}

#ifdef TARGET_MIPS64
/* This code generates a "reserved instruction" exception if 64-bit
   instructions are not enabled. */
static inline void check_mips_64(DisasContext *ctx)
{
    if (unlikely(!(ctx->hflags & MIPS_HFLAG_64)))
        generate_exception_end(ctx, EXCP_RI);
}
#endif

#ifndef CONFIG_USER_ONLY
static inline void check_mvh(DisasContext *ctx)
{
    if (unlikely(!ctx->mvh)) {
        generate_exception(ctx, EXCP_RI);
    }
}
#endif

/* Define small wrappers for gen_load_fpr* so that we have a uniform
   calling interface for 32 and 64-bit FPRs.  No sense in changing
   all callers for gen_load_fpr32 when we need the CTX parameter for
   this one use.  */
#define gen_ldcmp_fpr32(ctx, x, y) gen_load_fpr32(ctx, x, y)
#define gen_ldcmp_fpr64(ctx, x, y) gen_load_fpr64(ctx, x, y)
#define FOP_CONDS(type, abs, fmt, ifmt, bits)                                 \
static inline void gen_cmp ## type ## _ ## fmt(DisasContext *ctx, int n,      \
                                               int ft, int fs, int cc)        \
{                                                                             \
    TCGv_i##bits fp0 = tcg_temp_new_i##bits ();                               \
    TCGv_i##bits fp1 = tcg_temp_new_i##bits ();                               \
    switch (ifmt) {                                                           \
    case FMT_PS:                                                              \
        check_ps(ctx);                                                        \
        break;                                                                \
    case FMT_D:                                                               \
        if (abs) {                                                            \
            check_cop1x(ctx);                                                 \
        }                                                                     \
        check_cp1_registers(ctx, fs | ft);                                    \
        break;                                                                \
    case FMT_S:                                                               \
        if (abs) {                                                            \
            check_cop1x(ctx);                                                 \
        }                                                                     \
        break;                                                                \
    }                                                                         \
    gen_ldcmp_fpr##bits (ctx, fp0, fs);                                       \
    gen_ldcmp_fpr##bits (ctx, fp1, ft);                                       \
    switch (n) {                                                              \
    case  0: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _f, fp0, fp1, cc);    break;\
    case  1: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _un, fp0, fp1, cc);   break;\
    case  2: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _eq, fp0, fp1, cc);   break;\
    case  3: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ueq, fp0, fp1, cc);  break;\
    case  4: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _olt, fp0, fp1, cc);  break;\
    case  5: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ult, fp0, fp1, cc);  break;\
    case  6: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ole, fp0, fp1, cc);  break;\
    case  7: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ule, fp0, fp1, cc);  break;\
    case  8: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _sf, fp0, fp1, cc);   break;\
    case  9: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ngle, fp0, fp1, cc); break;\
    case 10: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _seq, fp0, fp1, cc);  break;\
    case 11: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ngl, fp0, fp1, cc);  break;\
    case 12: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _lt, fp0, fp1, cc);   break;\
    case 13: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _nge, fp0, fp1, cc);  break;\
    case 14: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _le, fp0, fp1, cc);   break;\
    case 15: gen_helper_0e2i(cmp ## type ## _ ## fmt ## _ngt, fp0, fp1, cc);  break;\
    default: abort();                                                         \
    }                                                                         \
    tcg_temp_free_i##bits (fp0);                                              \
    tcg_temp_free_i##bits (fp1);                                              \
}

FOP_CONDS(, 0, d, FMT_D, 64)
FOP_CONDS(abs, 1, d, FMT_D, 64)
FOP_CONDS(, 0, s, FMT_S, 32)
FOP_CONDS(abs, 1, s, FMT_S, 32)
FOP_CONDS(, 0, ps, FMT_PS, 64)
FOP_CONDS(abs, 1, ps, FMT_PS, 64)
#undef FOP_CONDS

#define FOP_CONDNS(fmt, ifmt, bits, STORE)                              \
static inline void gen_r6_cmp_ ## fmt(DisasContext * ctx, int n,        \
                                      int ft, int fs, int fd)           \
{                                                                       \
    TCGv_i ## bits fp0 = tcg_temp_new_i ## bits();                      \
    TCGv_i ## bits fp1 = tcg_temp_new_i ## bits();                      \
    if (ifmt == FMT_D) {                                                \
        check_cp1_registers(ctx, fs | ft | fd);                         \
    }                                                                   \
    gen_ldcmp_fpr ## bits(ctx, fp0, fs);                                \
    gen_ldcmp_fpr ## bits(ctx, fp1, ft);                                \
    switch (n) {                                                        \
    case  0:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _af(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case  1:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _un(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case  2:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _eq(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case  3:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _ueq(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case  4:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _lt(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case  5:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _ult(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case  6:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _le(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case  7:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _ule(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case  8:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _saf(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case  9:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sun(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case 10:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _seq(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case 11:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sueq(fp0, cpu_env, fp0, fp1);     \
        break;                                                          \
    case 12:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _slt(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case 13:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sult(fp0, cpu_env, fp0, fp1);     \
        break;                                                          \
    case 14:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sle(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case 15:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sule(fp0, cpu_env, fp0, fp1);     \
        break;                                                          \
    case 17:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _or(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case 18:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _une(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case 19:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _ne(fp0, cpu_env, fp0, fp1);       \
        break;                                                          \
    case 25:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sor(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    case 26:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sune(fp0, cpu_env, fp0, fp1);     \
        break;                                                          \
    case 27:                                                            \
        gen_helper_r6_cmp_ ## fmt ## _sne(fp0, cpu_env, fp0, fp1);      \
        break;                                                          \
    default:                                                            \
        abort();                                                        \
    }                                                                   \
    STORE;                                                              \
    tcg_temp_free_i ## bits (fp0);                                      \
    tcg_temp_free_i ## bits (fp1);                                      \
}

FOP_CONDNS(d, FMT_D, 64, gen_store_fpr64(ctx, fp0, fd))
FOP_CONDNS(s, FMT_S, 32, gen_store_fpr32(ctx, fp0, fd))
#undef FOP_CONDNS
#undef gen_ldcmp_fpr32
#undef gen_ldcmp_fpr64

/* load/store instructions. */
#ifdef CONFIG_USER_ONLY
#define OP_LD_ATOMIC(insn,fname)                                           \
static inline void op_ld_##insn(TCGv ret, TCGv arg1, int mem_idx,          \
                                DisasContext *ctx)                         \
{                                                                          \
    TCGv t0 = tcg_temp_new();                                              \
    tcg_gen_mov_tl(t0, arg1);                                              \
    tcg_gen_qemu_##fname(ret, arg1, ctx->mem_idx);                         \
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUMIPSState, lladdr));                \
    tcg_gen_st_tl(ret, cpu_env, offsetof(CPUMIPSState, llval));                \
    tcg_temp_free(t0);                                                     \
}
#else
#define OP_LD_ATOMIC(insn,fname)                                           \
static inline void op_ld_##insn(TCGv ret, TCGv arg1, int mem_idx,          \
                                DisasContext *ctx)                         \
{                                                                          \
    gen_helper_1e1i(insn, ret, arg1, mem_idx);                             \
}
#endif
OP_LD_ATOMIC(ll,ld32s);
#if defined(TARGET_MIPS64)
OP_LD_ATOMIC(lld,ld64);
#endif
#undef OP_LD_ATOMIC

#ifdef CONFIG_USER_ONLY
#define OP_ST_ATOMIC(insn,fname,ldname,almask)                               \
static inline void op_st_##insn(TCGv arg1, TCGv arg2, int rt, int mem_idx,   \
                                DisasContext *ctx)                           \
{                                                                            \
    TCGv t0 = tcg_temp_new();                                                \
    TCGLabel *l1 = gen_new_label();                                          \
    TCGLabel *l2 = gen_new_label();                                          \
                                                                             \
    tcg_gen_andi_tl(t0, arg2, almask);                                       \
    tcg_gen_brcondi_tl(TCG_COND_EQ, t0, 0, l1);                              \
    tcg_gen_st_tl(arg2, cpu_env, offsetof(CPUMIPSState, CP0_BadVAddr));          \
    generate_exception(ctx, EXCP_AdES);                                      \
    gen_set_label(l1);                                                       \
    tcg_gen_ld_tl(t0, cpu_env, offsetof(CPUMIPSState, lladdr));                  \
    tcg_gen_brcond_tl(TCG_COND_NE, arg2, t0, l2);                            \
    tcg_gen_movi_tl(t0, rt | ((almask << 3) & 0x20));                        \
    tcg_gen_st_tl(t0, cpu_env, offsetof(CPUMIPSState, llreg));                   \
    tcg_gen_st_tl(arg1, cpu_env, offsetof(CPUMIPSState, llnewval));              \
    generate_exception_end(ctx, EXCP_SC);                                    \
    gen_set_label(l2);                                                       \
    tcg_gen_movi_tl(t0, 0);                                                  \
    gen_store_gpr(t0, rt);                                                   \
    tcg_temp_free(t0);                                                       \
}
#else
#define OP_ST_ATOMIC(insn,fname,ldname,almask)                               \
static inline void op_st_##insn(TCGv arg1, TCGv arg2, int rt, int mem_idx,   \
                                DisasContext *ctx)                           \
{                                                                            \
    TCGv t0 = tcg_temp_new();                                                \
    gen_helper_1e2i(insn, t0, arg1, arg2, mem_idx);                          \
    gen_store_gpr(t0, rt);                                                   \
    tcg_temp_free(t0);                                                       \
}
#endif
OP_ST_ATOMIC(sc,st32,ld32s,0x3);
#if defined(TARGET_MIPS64)
OP_ST_ATOMIC(scd,st64,ld64,0x7);
#endif
#undef OP_ST_ATOMIC

static void gen_base_offset_addr (DisasContext *ctx, TCGv addr,
                                  int base, int16_t offset)
{
    if (base == 0) {
        tcg_gen_movi_tl(addr, offset);
    } else if (offset == 0) {
        gen_load_gpr(addr, base);
    } else {
        tcg_gen_movi_tl(addr, offset);
        gen_op_addr_add(ctx, addr, cpu_gpr[base], addr);
    }
}

static target_ulong pc_relative_pc (DisasContext *ctx)
{
    target_ulong pc = ctx->pc;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        int branch_bytes = ctx->hflags & MIPS_HFLAG_BDS16 ? 2 : 4;

        pc -= branch_bytes;
    }

    pc &= ~(target_ulong)3;
    return pc;
}

/* Load */
static void gen_ld(DisasContext *ctx, uint32_t opc,
                   int rt, int base, int16_t offset)
{
    TCGv t0, t1, t2;
    int mem_idx = ctx->mem_idx;

    if (rt == 0 && ctx->insn_flags & (INSN_LOONGSON2E | INSN_LOONGSON2F)) {
        /* Loongson CPU uses a load to zero register for prefetch.
           We emulate it as a NOP. On other CPU we must perform the
           actual memory access. */
        return;
    }

    t0 = tcg_temp_new();
    gen_base_offset_addr(ctx, t0, base, offset);

    switch (opc) {
#if defined(TARGET_MIPS64)
    case OPC_LWU:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LD:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEQ |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LLD:
    case R6_OPC_LLD:
        op_ld_lld(t0, t0, mem_idx, ctx);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LDL:
        t1 = tcg_temp_new();
        /* Do a byte access to possibly trigger a page
           fault with the unaligned address.  */
        tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_UB);
        tcg_gen_andi_tl(t1, t0, 7);
#ifndef TARGET_WORDS_BIGENDIAN
        tcg_gen_xori_tl(t1, t1, 7);
#endif
        tcg_gen_shli_tl(t1, t1, 3);
        tcg_gen_andi_tl(t0, t0, ~7);
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEQ);
        tcg_gen_shl_tl(t0, t0, t1);
        t2 = tcg_const_tl(-1);
        tcg_gen_shl_tl(t2, t2, t1);
        gen_load_gpr(t1, rt);
        tcg_gen_andc_tl(t1, t1, t2);
        tcg_temp_free(t2);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_temp_free(t1);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LDR:
        t1 = tcg_temp_new();
        /* Do a byte access to possibly trigger a page
           fault with the unaligned address.  */
        tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_UB);
        tcg_gen_andi_tl(t1, t0, 7);
#ifdef TARGET_WORDS_BIGENDIAN
        tcg_gen_xori_tl(t1, t1, 7);
#endif
        tcg_gen_shli_tl(t1, t1, 3);
        tcg_gen_andi_tl(t0, t0, ~7);
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEQ);
        tcg_gen_shr_tl(t0, t0, t1);
        tcg_gen_xori_tl(t1, t1, 63);
        t2 = tcg_const_tl(0xfffffffffffffffeull);
        tcg_gen_shl_tl(t2, t2, t1);
        gen_load_gpr(t1, rt);
        tcg_gen_and_tl(t1, t1, t2);
        tcg_temp_free(t2);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_temp_free(t1);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LDPC:
        t1 = tcg_const_tl(pc_relative_pc(ctx));
        gen_op_addr_add(ctx, t0, t0, t1);
        tcg_temp_free(t1);
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEQ);
        gen_store_gpr(t0, rt);
        break;
#endif
    case OPC_LWPC:
        t1 = tcg_const_tl(pc_relative_pc(ctx));
        gen_op_addr_add(ctx, t0, t0, t1);
        tcg_temp_free(t1);
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TESL);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LWE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LW:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TESL |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LHE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LH:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TESW |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LHUE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LHU:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEUW |
                           ctx->default_tcg_memop_mask);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LBE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LB:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_SB);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LBUE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LBU:
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_UB);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LWLE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LWL:
        t1 = tcg_temp_new();
        /* Do a byte access to possibly trigger a page
           fault with the unaligned address.  */
        tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_UB);
        tcg_gen_andi_tl(t1, t0, 3);
#ifndef TARGET_WORDS_BIGENDIAN
        tcg_gen_xori_tl(t1, t1, 3);
#endif
        tcg_gen_shli_tl(t1, t1, 3);
        tcg_gen_andi_tl(t0, t0, ~3);
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEUL);
        tcg_gen_shl_tl(t0, t0, t1);
        t2 = tcg_const_tl(-1);
        tcg_gen_shl_tl(t2, t2, t1);
        gen_load_gpr(t1, rt);
        tcg_gen_andc_tl(t1, t1, t2);
        tcg_temp_free(t2);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_temp_free(t1);
        tcg_gen_ext32s_tl(t0, t0);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LWRE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LWR:
        t1 = tcg_temp_new();
        /* Do a byte access to possibly trigger a page
           fault with the unaligned address.  */
        tcg_gen_qemu_ld_tl(t1, t0, mem_idx, MO_UB);
        tcg_gen_andi_tl(t1, t0, 3);
#ifdef TARGET_WORDS_BIGENDIAN
        tcg_gen_xori_tl(t1, t1, 3);
#endif
        tcg_gen_shli_tl(t1, t1, 3);
        tcg_gen_andi_tl(t0, t0, ~3);
        tcg_gen_qemu_ld_tl(t0, t0, mem_idx, MO_TEUL);
        tcg_gen_shr_tl(t0, t0, t1);
        tcg_gen_xori_tl(t1, t1, 31);
        t2 = tcg_const_tl(0xfffffffeull);
        tcg_gen_shl_tl(t2, t2, t1);
        gen_load_gpr(t1, rt);
        tcg_gen_and_tl(t1, t1, t2);
        tcg_temp_free(t2);
        tcg_gen_or_tl(t0, t0, t1);
        tcg_temp_free(t1);
        tcg_gen_ext32s_tl(t0, t0);
        gen_store_gpr(t0, rt);
        break;
    case OPC_LLE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_LL:
    case R6_OPC_LL:
        op_ld_ll(t0, t0, mem_idx, ctx);
        gen_store_gpr(t0, rt);
        break;
    }
    tcg_temp_free(t0);
}

/* Store */
static void gen_st (DisasContext *ctx, uint32_t opc, int rt,
                    int base, int16_t offset)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int mem_idx = ctx->mem_idx;

    gen_base_offset_addr(ctx, t0, base, offset);
    gen_load_gpr(t1, rt);
    switch (opc) {
#if defined(TARGET_MIPS64)
    case OPC_SD:
        tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_TEQ |
                           ctx->default_tcg_memop_mask);
        break;
    case OPC_SDL:
        gen_helper_0e2i(sdl, t1, t0, mem_idx);
        break;
    case OPC_SDR:
        gen_helper_0e2i(sdr, t1, t0, mem_idx);
        break;
#endif
    case OPC_SWE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_SW:
        tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_TEUL |
                           ctx->default_tcg_memop_mask);
        break;
    case OPC_SHE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_SH:
        tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_TEUW |
                           ctx->default_tcg_memop_mask);
        break;
    case OPC_SBE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_SB:
        tcg_gen_qemu_st_tl(t1, t0, mem_idx, MO_8);
        break;
    case OPC_SWLE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_SWL:
        gen_helper_0e2i(swl, t1, t0, mem_idx);
        break;
    case OPC_SWRE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_SWR:
        gen_helper_0e2i(swr, t1, t0, mem_idx);
        break;
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}


/* Store conditional */
static void gen_st_cond (DisasContext *ctx, uint32_t opc, int rt,
                         int base, int16_t offset)
{
    TCGv t0, t1;
    int mem_idx = ctx->mem_idx;

#ifdef CONFIG_USER_ONLY
    t0 = tcg_temp_local_new();
    t1 = tcg_temp_local_new();
#else
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
#endif
    gen_base_offset_addr(ctx, t0, base, offset);
    gen_load_gpr(t1, rt);
    switch (opc) {
#if defined(TARGET_MIPS64)
    case OPC_SCD:
    case R6_OPC_SCD:
        op_st_scd(t1, t0, rt, mem_idx, ctx);
        break;
#endif
    case OPC_SCE:
        mem_idx = MIPS_HFLAG_UM;
        /* fall through */
    case OPC_SC:
    case R6_OPC_SC:
        op_st_sc(t1, t0, rt, mem_idx, ctx);
        break;
    }
    tcg_temp_free(t1);
    tcg_temp_free(t0);
}

/* Load and store */
static void gen_flt_ldst (DisasContext *ctx, uint32_t opc, int ft,
                          int base, int16_t offset)
{
    TCGv t0 = tcg_temp_new();

    gen_base_offset_addr(ctx, t0, base, offset);
    /* Don't do NOP if destination is zero: we must perform the actual
       memory access. */
    switch (opc) {
    case OPC_LWC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            tcg_gen_qemu_ld_i32(fp0, t0, ctx->mem_idx, MO_TESL |
                                ctx->default_tcg_memop_mask);
            gen_store_fpr32(ctx, fp0, ft);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_SWC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, ft);
            tcg_gen_qemu_st_i32(fp0, t0, ctx->mem_idx, MO_TEUL |
                                ctx->default_tcg_memop_mask);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_LDC1:
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(fp0, t0, ctx->mem_idx, MO_TEQ |
                                ctx->default_tcg_memop_mask);
            gen_store_fpr64(ctx, fp0, ft);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_SDC1:
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, ft);
            tcg_gen_qemu_st_i64(fp0, t0, ctx->mem_idx, MO_TEQ |
                                ctx->default_tcg_memop_mask);
            tcg_temp_free_i64(fp0);
        }
        break;
    default:
        MIPS_INVAL("flt_ldst");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }
 out:
    tcg_temp_free(t0);
}

static void gen_cop1_ldst(DisasContext *ctx, uint32_t op, int rt,
                          int rs, int16_t imm)
{
    if (ctx->CP0_Config1 & (1 << CP0C1_FP)) {
        check_cp1_enabled(ctx);
        switch (op) {
        case OPC_LDC1:
        case OPC_SDC1:
            check_insn(ctx, ISA_MIPS2);
            /* Fallthrough */
        default:
            gen_flt_ldst(ctx, op, rt, rs, imm);
        }
    } else {
        generate_exception_err(ctx, EXCP_CpU, 1);
    }
}

/* Arithmetic with immediate operand */
static void gen_arith_imm(DisasContext *ctx, uint32_t opc,
                          int rt, int rs, int16_t imm)
{
    target_ulong uimm = (target_long)imm; /* Sign extend to 32/64 bits */

    if (rt == 0 && opc != OPC_ADDI && opc != OPC_DADDI) {
        /* If no destination, treat it as a NOP.
           For addi, we must generate the overflow exception when needed. */
        return;
    }
    switch (opc) {
    case OPC_ADDI:
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            gen_load_gpr(t1, rs);
            tcg_gen_addi_tl(t0, t1, uimm);
            tcg_gen_ext32s_tl(t0, t0);

            tcg_gen_xori_tl(t1, t1, ~uimm);
            tcg_gen_xori_tl(t2, t0, uimm);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
            tcg_temp_free(t1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            tcg_gen_ext32s_tl(t0, t0);
            gen_store_gpr(t0, rt);
            tcg_temp_free(t0);
        }
        break;
    case OPC_ADDIU:
        if (rs != 0) {
            tcg_gen_addi_tl(cpu_gpr[rt], cpu_gpr[rs], uimm);
            tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rt], uimm);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DADDI:
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            gen_load_gpr(t1, rs);
            tcg_gen_addi_tl(t0, t1, uimm);

            tcg_gen_xori_tl(t1, t1, ~uimm);
            tcg_gen_xori_tl(t2, t0, uimm);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
            tcg_temp_free(t1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            gen_store_gpr(t0, rt);
            tcg_temp_free(t0);
        }
        break;
    case OPC_DADDIU:
        if (rs != 0) {
            tcg_gen_addi_tl(cpu_gpr[rt], cpu_gpr[rs], uimm);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rt], uimm);
        }
        break;
#endif
    }
}

/* Logic with immediate operand */
static void gen_logic_imm(DisasContext *ctx, uint32_t opc,
                          int rt, int rs, int16_t imm)
{
    target_ulong uimm;

    if (rt == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }
    uimm = (uint16_t)imm;
    switch (opc) {
    case OPC_ANDI:
        if (likely(rs != 0))
            tcg_gen_andi_tl(cpu_gpr[rt], cpu_gpr[rs], uimm);
        else
            tcg_gen_movi_tl(cpu_gpr[rt], 0);
        break;
    case OPC_ORI:
        if (rs != 0)
            tcg_gen_ori_tl(cpu_gpr[rt], cpu_gpr[rs], uimm);
        else
            tcg_gen_movi_tl(cpu_gpr[rt], uimm);
        break;
    case OPC_XORI:
        if (likely(rs != 0))
            tcg_gen_xori_tl(cpu_gpr[rt], cpu_gpr[rs], uimm);
        else
            tcg_gen_movi_tl(cpu_gpr[rt], uimm);
        break;
    case OPC_LUI:
        if (rs != 0 && (ctx->insn_flags & ISA_MIPS32R6)) {
            /* OPC_AUI */
            tcg_gen_addi_tl(cpu_gpr[rt], cpu_gpr[rs], imm << 16);
            tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rt], imm << 16);
        }
        break;

    default:
        break;
    }
}

/* Set on less than with immediate operand */
static void gen_slt_imm(DisasContext *ctx, uint32_t opc,
                        int rt, int rs, int16_t imm)
{
    target_ulong uimm = (target_long)imm; /* Sign extend to 32/64 bits */
    TCGv t0;

    if (rt == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }
    t0 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    switch (opc) {
    case OPC_SLTI:
        tcg_gen_setcondi_tl(TCG_COND_LT, cpu_gpr[rt], t0, uimm);
        break;
    case OPC_SLTIU:
        tcg_gen_setcondi_tl(TCG_COND_LTU, cpu_gpr[rt], t0, uimm);
        break;
    }
    tcg_temp_free(t0);
}

/* Shifts with immediate operand */
static void gen_shift_imm(DisasContext *ctx, uint32_t opc,
                          int rt, int rs, int16_t imm)
{
    target_ulong uimm = ((uint16_t)imm) & 0x1f;
    TCGv t0;

    if (rt == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    switch (opc) {
    case OPC_SLL:
        tcg_gen_shli_tl(t0, t0, uimm);
        tcg_gen_ext32s_tl(cpu_gpr[rt], t0);
        break;
    case OPC_SRA:
        tcg_gen_sari_tl(cpu_gpr[rt], t0, uimm);
        break;
    case OPC_SRL:
        if (uimm != 0) {
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_shri_tl(cpu_gpr[rt], t0, uimm);
        } else {
            tcg_gen_ext32s_tl(cpu_gpr[rt], t0);
        }
        break;
    case OPC_ROTR:
        if (uimm != 0) {
            TCGv_i32 t1 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(t1, t0);
            tcg_gen_rotri_i32(t1, t1, uimm);
            tcg_gen_ext_i32_tl(cpu_gpr[rt], t1);
            tcg_temp_free_i32(t1);
        } else {
            tcg_gen_ext32s_tl(cpu_gpr[rt], t0);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DSLL:
        tcg_gen_shli_tl(cpu_gpr[rt], t0, uimm);
        break;
    case OPC_DSRA:
        tcg_gen_sari_tl(cpu_gpr[rt], t0, uimm);
        break;
    case OPC_DSRL:
        tcg_gen_shri_tl(cpu_gpr[rt], t0, uimm);
        break;
    case OPC_DROTR:
        if (uimm != 0) {
            tcg_gen_rotri_tl(cpu_gpr[rt], t0, uimm);
        } else {
            tcg_gen_mov_tl(cpu_gpr[rt], t0);
        }
        break;
    case OPC_DSLL32:
        tcg_gen_shli_tl(cpu_gpr[rt], t0, uimm + 32);
        break;
    case OPC_DSRA32:
        tcg_gen_sari_tl(cpu_gpr[rt], t0, uimm + 32);
        break;
    case OPC_DSRL32:
        tcg_gen_shri_tl(cpu_gpr[rt], t0, uimm + 32);
        break;
    case OPC_DROTR32:
        tcg_gen_rotri_tl(cpu_gpr[rt], t0, uimm + 32);
        break;
#endif
    }
    tcg_temp_free(t0);
}

/* Arithmetic */
static void gen_arith(DisasContext *ctx, uint32_t opc,
                      int rd, int rs, int rt)
{
    if (rd == 0 && opc != OPC_ADD && opc != OPC_SUB
       && opc != OPC_DADD && opc != OPC_DSUB) {
        /* If no destination, treat it as a NOP.
           For add & sub, we must generate the overflow exception when needed. */
        return;
    }

    switch (opc) {
    case OPC_ADD:
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            gen_load_gpr(t1, rs);
            gen_load_gpr(t2, rt);
            tcg_gen_add_tl(t0, t1, t2);
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_xor_tl(t1, t1, t2);
            tcg_gen_xor_tl(t2, t0, t2);
            tcg_gen_andc_tl(t1, t2, t1);
            tcg_temp_free(t2);
            tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
            tcg_temp_free(t1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            gen_store_gpr(t0, rd);
            tcg_temp_free(t0);
        }
        break;
    case OPC_ADDU:
        if (rs != 0 && rt != 0) {
            tcg_gen_add_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rt]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
    case OPC_SUB:
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            gen_load_gpr(t1, rs);
            gen_load_gpr(t2, rt);
            tcg_gen_sub_tl(t0, t1, t2);
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_xor_tl(t2, t1, t2);
            tcg_gen_xor_tl(t1, t0, t1);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
            tcg_temp_free(t1);
            /* operands of different sign, first operand and result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            gen_store_gpr(t0, rd);
            tcg_temp_free(t0);
        }
        break;
    case OPC_SUBU:
        if (rs != 0 && rt != 0) {
            tcg_gen_sub_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_neg_tl(cpu_gpr[rd], cpu_gpr[rt]);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DADD:
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            gen_load_gpr(t1, rs);
            gen_load_gpr(t2, rt);
            tcg_gen_add_tl(t0, t1, t2);
            tcg_gen_xor_tl(t1, t1, t2);
            tcg_gen_xor_tl(t2, t0, t2);
            tcg_gen_andc_tl(t1, t2, t1);
            tcg_temp_free(t2);
            tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
            tcg_temp_free(t1);
            /* operands of same sign, result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            gen_store_gpr(t0, rd);
            tcg_temp_free(t0);
        }
        break;
    case OPC_DADDU:
        if (rs != 0 && rt != 0) {
            tcg_gen_add_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rt]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
    case OPC_DSUB:
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_temp_new();
            TCGLabel *l1 = gen_new_label();

            gen_load_gpr(t1, rs);
            gen_load_gpr(t2, rt);
            tcg_gen_sub_tl(t0, t1, t2);
            tcg_gen_xor_tl(t2, t1, t2);
            tcg_gen_xor_tl(t1, t0, t1);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_temp_free(t2);
            tcg_gen_brcondi_tl(TCG_COND_GE, t1, 0, l1);
            tcg_temp_free(t1);
            /* operands of different sign, first operand and result different sign */
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(l1);
            gen_store_gpr(t0, rd);
            tcg_temp_free(t0);
        }
        break;
    case OPC_DSUBU:
        if (rs != 0 && rt != 0) {
            tcg_gen_sub_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_neg_tl(cpu_gpr[rd], cpu_gpr[rt]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
#endif
    case OPC_MUL:
        if (likely(rs != 0 && rt != 0)) {
            tcg_gen_mul_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
    }
}

/* Conditional move */
static void gen_cond_move(DisasContext *ctx, uint32_t opc,
                          int rd, int rs, int rt)
{
    TCGv t0, t1, t2;

    if (rd == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, rt);
    t1 = tcg_const_tl(0);
    t2 = tcg_temp_new();
    gen_load_gpr(t2, rs);
    switch (opc) {
    case OPC_MOVN:
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[rd], t0, t1, t2, cpu_gpr[rd]);
        break;
    case OPC_MOVZ:
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_gpr[rd], t0, t1, t2, cpu_gpr[rd]);
        break;
    case OPC_SELNEZ:
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_gpr[rd], t0, t1, t2, t1);
        break;
    case OPC_SELEQZ:
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_gpr[rd], t0, t1, t2, t1);
        break;
    }
    tcg_temp_free(t2);
    tcg_temp_free(t1);
    tcg_temp_free(t0);
}

/* Logic */
static void gen_logic(DisasContext *ctx, uint32_t opc,
                      int rd, int rs, int rt)
{
    if (rd == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }

    switch (opc) {
    case OPC_AND:
        if (likely(rs != 0 && rt != 0)) {
            tcg_gen_and_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
    case OPC_NOR:
        if (rs != 0 && rt != 0) {
            tcg_gen_nor_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_not_tl(cpu_gpr[rd], cpu_gpr[rt]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_not_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], ~((target_ulong)0));
        }
        break;
    case OPC_OR:
        if (likely(rs != 0 && rt != 0)) {
            tcg_gen_or_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rt]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
    case OPC_XOR:
        if (likely(rs != 0 && rt != 0)) {
            tcg_gen_xor_tl(cpu_gpr[rd], cpu_gpr[rs], cpu_gpr[rt]);
        } else if (rs == 0 && rt != 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rt]);
        } else if (rs != 0 && rt == 0) {
            tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
        } else {
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
        }
        break;
    }
}

/* Set on lower than */
static void gen_slt(DisasContext *ctx, uint32_t opc,
                    int rd, int rs, int rt)
{
    TCGv t0, t1;

    if (rd == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    switch (opc) {
    case OPC_SLT:
        tcg_gen_setcond_tl(TCG_COND_LT, cpu_gpr[rd], t0, t1);
        break;
    case OPC_SLTU:
        tcg_gen_setcond_tl(TCG_COND_LTU, cpu_gpr[rd], t0, t1);
        break;
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* Shifts */
static void gen_shift(DisasContext *ctx, uint32_t opc,
                      int rd, int rs, int rt)
{
    TCGv t0, t1;

    if (rd == 0) {
        /* If no destination, treat it as a NOP.
           For add & sub, we must generate the overflow exception when needed. */
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    switch (opc) {
    case OPC_SLLV:
        tcg_gen_andi_tl(t0, t0, 0x1f);
        tcg_gen_shl_tl(t0, t1, t0);
        tcg_gen_ext32s_tl(cpu_gpr[rd], t0);
        break;
    case OPC_SRAV:
        tcg_gen_andi_tl(t0, t0, 0x1f);
        tcg_gen_sar_tl(cpu_gpr[rd], t1, t0);
        break;
    case OPC_SRLV:
        tcg_gen_ext32u_tl(t1, t1);
        tcg_gen_andi_tl(t0, t0, 0x1f);
        tcg_gen_shr_tl(t0, t1, t0);
        tcg_gen_ext32s_tl(cpu_gpr[rd], t0);
        break;
    case OPC_ROTRV:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_andi_i32(t2, t2, 0x1f);
            tcg_gen_rotr_i32(t2, t3, t2);
            tcg_gen_ext_i32_tl(cpu_gpr[rd], t2);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DSLLV:
        tcg_gen_andi_tl(t0, t0, 0x3f);
        tcg_gen_shl_tl(cpu_gpr[rd], t1, t0);
        break;
    case OPC_DSRAV:
        tcg_gen_andi_tl(t0, t0, 0x3f);
        tcg_gen_sar_tl(cpu_gpr[rd], t1, t0);
        break;
    case OPC_DSRLV:
        tcg_gen_andi_tl(t0, t0, 0x3f);
        tcg_gen_shr_tl(cpu_gpr[rd], t1, t0);
        break;
    case OPC_DROTRV:
        tcg_gen_andi_tl(t0, t0, 0x3f);
        tcg_gen_rotr_tl(cpu_gpr[rd], t1, t0);
        break;
#endif
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* Arithmetic on HI/LO registers */
static void gen_HILO(DisasContext *ctx, uint32_t opc, int acc, int reg)
{
    if (reg == 0 && (opc == OPC_MFHI || opc == OPC_MFLO)) {
        /* Treat as NOP. */
        return;
    }

    if (acc != 0) {
        check_dsp(ctx);
    }

    switch (opc) {
    case OPC_MFHI:
#if defined(TARGET_MIPS64)
        if (acc != 0) {
            tcg_gen_ext32s_tl(cpu_gpr[reg], cpu_HI[acc]);
        } else
#endif
        {
            tcg_gen_mov_tl(cpu_gpr[reg], cpu_HI[acc]);
        }
        break;
    case OPC_MFLO:
#if defined(TARGET_MIPS64)
        if (acc != 0) {
            tcg_gen_ext32s_tl(cpu_gpr[reg], cpu_LO[acc]);
        } else
#endif
        {
            tcg_gen_mov_tl(cpu_gpr[reg], cpu_LO[acc]);
        }
        break;
    case OPC_MTHI:
        if (reg != 0) {
#if defined(TARGET_MIPS64)
            if (acc != 0) {
                tcg_gen_ext32s_tl(cpu_HI[acc], cpu_gpr[reg]);
            } else
#endif
            {
                tcg_gen_mov_tl(cpu_HI[acc], cpu_gpr[reg]);
            }
        } else {
            tcg_gen_movi_tl(cpu_HI[acc], 0);
        }
        break;
    case OPC_MTLO:
        if (reg != 0) {
#if defined(TARGET_MIPS64)
            if (acc != 0) {
                tcg_gen_ext32s_tl(cpu_LO[acc], cpu_gpr[reg]);
            } else
#endif
            {
                tcg_gen_mov_tl(cpu_LO[acc], cpu_gpr[reg]);
            }
        } else {
            tcg_gen_movi_tl(cpu_LO[acc], 0);
        }
        break;
    }
}

static inline void gen_r6_ld(target_long addr, int reg, int memidx,
                             TCGMemOp memop)
{
    TCGv t0 = tcg_const_tl(addr);
    tcg_gen_qemu_ld_tl(t0, t0, memidx, memop);
    gen_store_gpr(t0, reg);
    tcg_temp_free(t0);
}

static inline void gen_pcrel(DisasContext *ctx, int opc, target_ulong pc,
                             int rs)
{
    target_long offset;
    target_long addr;

    switch (MASK_OPC_PCREL_TOP2BITS(opc)) {
    case OPC_ADDIUPC:
        if (rs != 0) {
            offset = sextract32(ctx->opcode << 2, 0, 21);
            addr = addr_add(ctx, pc, offset);
            tcg_gen_movi_tl(cpu_gpr[rs], addr);
        }
        break;
    case R6_OPC_LWPC:
        offset = sextract32(ctx->opcode << 2, 0, 21);
        addr = addr_add(ctx, pc, offset);
        gen_r6_ld(addr, rs, ctx->mem_idx, MO_TESL);
        break;
#if defined(TARGET_MIPS64)
    case OPC_LWUPC:
        check_mips_64(ctx);
        offset = sextract32(ctx->opcode << 2, 0, 21);
        addr = addr_add(ctx, pc, offset);
        gen_r6_ld(addr, rs, ctx->mem_idx, MO_TEUL);
        break;
#endif
    default:
        switch (MASK_OPC_PCREL_TOP5BITS(opc)) {
        case OPC_AUIPC:
            if (rs != 0) {
                offset = sextract32(ctx->opcode, 0, 16) << 16;
                addr = addr_add(ctx, pc, offset);
                tcg_gen_movi_tl(cpu_gpr[rs], addr);
            }
            break;
        case OPC_ALUIPC:
            if (rs != 0) {
                offset = sextract32(ctx->opcode, 0, 16) << 16;
                addr = ~0xFFFF & addr_add(ctx, pc, offset);
                tcg_gen_movi_tl(cpu_gpr[rs], addr);
            }
            break;
#if defined(TARGET_MIPS64)
        case R6_OPC_LDPC: /* bits 16 and 17 are part of immediate */
        case R6_OPC_LDPC + (1 << 16):
        case R6_OPC_LDPC + (2 << 16):
        case R6_OPC_LDPC + (3 << 16):
            check_mips_64(ctx);
            offset = sextract32(ctx->opcode << 3, 0, 21);
            addr = addr_add(ctx, (pc & ~0x7), offset);
            gen_r6_ld(addr, rs, ctx->mem_idx, MO_TEQ);
            break;
#endif
        default:
            MIPS_INVAL("OPC_PCREL");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    }
}

static void gen_r6_muldiv(DisasContext *ctx, int opc, int rd, int rs, int rt)
{
    TCGv t0, t1;

    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    switch (opc) {
    case R6_OPC_DIV:
        {
            TCGv t2 = tcg_temp_new();
            TCGv t3 = tcg_temp_new();
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_ext32s_tl(t1, t1);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, INT_MIN);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1);
            tcg_gen_and_tl(t2, t2, t3);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
            tcg_gen_or_tl(t2, t2, t3);
            tcg_gen_movi_tl(t3, 0);
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
            tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_MOD:
        {
            TCGv t2 = tcg_temp_new();
            TCGv t3 = tcg_temp_new();
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_ext32s_tl(t1, t1);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, INT_MIN);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1);
            tcg_gen_and_tl(t2, t2, t3);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
            tcg_gen_or_tl(t2, t2, t3);
            tcg_gen_movi_tl(t3, 0);
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
            tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_DIVU:
        {
            TCGv t2 = tcg_const_tl(0);
            TCGv t3 = tcg_const_tl(1);
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
            tcg_gen_divu_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_MODU:
        {
            TCGv t2 = tcg_const_tl(0);
            TCGv t3 = tcg_const_tl(1);
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
            tcg_gen_remu_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_MUL:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_mul_i32(t2, t2, t3);
            tcg_gen_ext_i32_tl(cpu_gpr[rd], t2);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
    case R6_OPC_MUH:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_muls2_i32(t2, t3, t2, t3);
            tcg_gen_ext_i32_tl(cpu_gpr[rd], t3);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
    case R6_OPC_MULU:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_mul_i32(t2, t2, t3);
            tcg_gen_ext_i32_tl(cpu_gpr[rd], t2);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
    case R6_OPC_MUHU:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_mulu2_i32(t2, t3, t2, t3);
            tcg_gen_ext_i32_tl(cpu_gpr[rd], t3);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
#if defined(TARGET_MIPS64)
    case R6_OPC_DDIV:
        {
            TCGv t2 = tcg_temp_new();
            TCGv t3 = tcg_temp_new();
            tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, -1LL << 63);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1LL);
            tcg_gen_and_tl(t2, t2, t3);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
            tcg_gen_or_tl(t2, t2, t3);
            tcg_gen_movi_tl(t3, 0);
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
            tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_DMOD:
        {
            TCGv t2 = tcg_temp_new();
            TCGv t3 = tcg_temp_new();
            tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, -1LL << 63);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1LL);
            tcg_gen_and_tl(t2, t2, t3);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
            tcg_gen_or_tl(t2, t2, t3);
            tcg_gen_movi_tl(t3, 0);
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
            tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_DDIVU:
        {
            TCGv t2 = tcg_const_tl(0);
            TCGv t3 = tcg_const_tl(1);
            tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
            tcg_gen_divu_i64(cpu_gpr[rd], t0, t1);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_DMODU:
        {
            TCGv t2 = tcg_const_tl(0);
            TCGv t3 = tcg_const_tl(1);
            tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
            tcg_gen_remu_i64(cpu_gpr[rd], t0, t1);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_DMUL:
        tcg_gen_mul_i64(cpu_gpr[rd], t0, t1);
        break;
    case R6_OPC_DMUH:
        {
            TCGv t2 = tcg_temp_new();
            tcg_gen_muls2_i64(t2, cpu_gpr[rd], t0, t1);
            tcg_temp_free(t2);
        }
        break;
    case R6_OPC_DMULU:
        tcg_gen_mul_i64(cpu_gpr[rd], t0, t1);
        break;
    case R6_OPC_DMUHU:
        {
            TCGv t2 = tcg_temp_new();
            tcg_gen_mulu2_i64(t2, cpu_gpr[rd], t0, t1);
            tcg_temp_free(t2);
        }
        break;
#endif
    default:
        MIPS_INVAL("r6 mul/div");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }
 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_muldiv(DisasContext *ctx, uint32_t opc,
                       int acc, int rs, int rt)
{
    TCGv t0, t1;

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    if (acc != 0) {
        check_dsp(ctx);
    }

    switch (opc) {
    case OPC_DIV:
        {
            TCGv t2 = tcg_temp_new();
            TCGv t3 = tcg_temp_new();
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_ext32s_tl(t1, t1);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, INT_MIN);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1);
            tcg_gen_and_tl(t2, t2, t3);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
            tcg_gen_or_tl(t2, t2, t3);
            tcg_gen_movi_tl(t3, 0);
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
            tcg_gen_div_tl(cpu_LO[acc], t0, t1);
            tcg_gen_rem_tl(cpu_HI[acc], t0, t1);
            tcg_gen_ext32s_tl(cpu_LO[acc], cpu_LO[acc]);
            tcg_gen_ext32s_tl(cpu_HI[acc], cpu_HI[acc]);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case OPC_DIVU:
        {
            TCGv t2 = tcg_const_tl(0);
            TCGv t3 = tcg_const_tl(1);
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
            tcg_gen_divu_tl(cpu_LO[acc], t0, t1);
            tcg_gen_remu_tl(cpu_HI[acc], t0, t1);
            tcg_gen_ext32s_tl(cpu_LO[acc], cpu_LO[acc]);
            tcg_gen_ext32s_tl(cpu_HI[acc], cpu_HI[acc]);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case OPC_MULT:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_muls2_i32(t2, t3, t2, t3);
            tcg_gen_ext_i32_tl(cpu_LO[acc], t2);
            tcg_gen_ext_i32_tl(cpu_HI[acc], t3);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
    case OPC_MULTU:
        {
            TCGv_i32 t2 = tcg_temp_new_i32();
            TCGv_i32 t3 = tcg_temp_new_i32();
            tcg_gen_trunc_tl_i32(t2, t0);
            tcg_gen_trunc_tl_i32(t3, t1);
            tcg_gen_mulu2_i32(t2, t3, t2, t3);
            tcg_gen_ext_i32_tl(cpu_LO[acc], t2);
            tcg_gen_ext_i32_tl(cpu_HI[acc], t3);
            tcg_temp_free_i32(t2);
            tcg_temp_free_i32(t3);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DDIV:
        {
            TCGv t2 = tcg_temp_new();
            TCGv t3 = tcg_temp_new();
            tcg_gen_setcondi_tl(TCG_COND_EQ, t2, t0, -1LL << 63);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, -1LL);
            tcg_gen_and_tl(t2, t2, t3);
            tcg_gen_setcondi_tl(TCG_COND_EQ, t3, t1, 0);
            tcg_gen_or_tl(t2, t2, t3);
            tcg_gen_movi_tl(t3, 0);
            tcg_gen_movcond_tl(TCG_COND_NE, t1, t2, t3, t2, t1);
            tcg_gen_div_tl(cpu_LO[acc], t0, t1);
            tcg_gen_rem_tl(cpu_HI[acc], t0, t1);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case OPC_DDIVU:
        {
            TCGv t2 = tcg_const_tl(0);
            TCGv t3 = tcg_const_tl(1);
            tcg_gen_movcond_tl(TCG_COND_EQ, t1, t1, t2, t3, t1);
            tcg_gen_divu_i64(cpu_LO[acc], t0, t1);
            tcg_gen_remu_i64(cpu_HI[acc], t0, t1);
            tcg_temp_free(t3);
            tcg_temp_free(t2);
        }
        break;
    case OPC_DMULT:
        tcg_gen_muls2_i64(cpu_LO[acc], cpu_HI[acc], t0, t1);
        break;
    case OPC_DMULTU:
        tcg_gen_mulu2_i64(cpu_LO[acc], cpu_HI[acc], t0, t1);
        break;
#endif
    case OPC_MADD:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, t0);
            tcg_gen_ext_tl_i64(t3, t1);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
            tcg_gen_add_i64(t2, t2, t3);
            tcg_temp_free_i64(t3);
            gen_move_low32(cpu_LO[acc], t2);
            gen_move_high32(cpu_HI[acc], t2);
            tcg_temp_free_i64(t2);
        }
        break;
    case OPC_MADDU:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_extu_tl_i64(t2, t0);
            tcg_gen_extu_tl_i64(t3, t1);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
            tcg_gen_add_i64(t2, t2, t3);
            tcg_temp_free_i64(t3);
            gen_move_low32(cpu_LO[acc], t2);
            gen_move_high32(cpu_HI[acc], t2);
            tcg_temp_free_i64(t2);
        }
        break;
    case OPC_MSUB:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext_tl_i64(t2, t0);
            tcg_gen_ext_tl_i64(t3, t1);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
            tcg_gen_sub_i64(t2, t3, t2);
            tcg_temp_free_i64(t3);
            gen_move_low32(cpu_LO[acc], t2);
            gen_move_high32(cpu_HI[acc], t2);
            tcg_temp_free_i64(t2);
        }
        break;
    case OPC_MSUBU:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGv_i64 t3 = tcg_temp_new_i64();

            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_extu_tl_i64(t2, t0);
            tcg_gen_extu_tl_i64(t3, t1);
            tcg_gen_mul_i64(t2, t2, t3);
            tcg_gen_concat_tl_i64(t3, cpu_LO[acc], cpu_HI[acc]);
            tcg_gen_sub_i64(t2, t3, t2);
            tcg_temp_free_i64(t3);
            gen_move_low32(cpu_LO[acc], t2);
            gen_move_high32(cpu_HI[acc], t2);
            tcg_temp_free_i64(t2);
        }
        break;
    default:
        MIPS_INVAL("mul/div");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }
 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_mul_vr54xx (DisasContext *ctx, uint32_t opc,
                            int rd, int rs, int rt)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    switch (opc) {
    case OPC_VR54XX_MULS:
        gen_helper_muls(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MULSU:
        gen_helper_mulsu(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MACC:
        gen_helper_macc(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MACCU:
        gen_helper_maccu(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MSAC:
        gen_helper_msac(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MSACU:
        gen_helper_msacu(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MULHI:
        gen_helper_mulhi(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MULHIU:
        gen_helper_mulhiu(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MULSHI:
        gen_helper_mulshi(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MULSHIU:
        gen_helper_mulshiu(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MACCHI:
        gen_helper_macchi(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MACCHIU:
        gen_helper_macchiu(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MSACHI:
        gen_helper_msachi(t0, cpu_env, t0, t1);
        break;
    case OPC_VR54XX_MSACHIU:
        gen_helper_msachiu(t0, cpu_env, t0, t1);
        break;
    default:
        MIPS_INVAL("mul vr54xx");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }
    gen_store_gpr(t0, rd);

 out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_cl (DisasContext *ctx, uint32_t opc,
                    int rd, int rs)
{
    TCGv t0;

    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }
    t0 = cpu_gpr[rd];
    gen_load_gpr(t0, rs);

    switch (opc) {
    case OPC_CLO:
    case R6_OPC_CLO:
#if defined(TARGET_MIPS64)
    case OPC_DCLO:
    case R6_OPC_DCLO:
#endif
        tcg_gen_not_tl(t0, t0);
        break;
    }

    switch (opc) {
    case OPC_CLO:
    case R6_OPC_CLO:
    case OPC_CLZ:
    case R6_OPC_CLZ:
        tcg_gen_ext32u_tl(t0, t0);
        tcg_gen_clzi_tl(t0, t0, TARGET_LONG_BITS);
        tcg_gen_subi_tl(t0, t0, TARGET_LONG_BITS - 32);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DCLO:
    case R6_OPC_DCLO:
    case OPC_DCLZ:
    case R6_OPC_DCLZ:
        tcg_gen_clzi_i64(t0, t0, 64);
        break;
#endif
    }
}

/* Godson integer instructions */
static void gen_loongson_integer(DisasContext *ctx, uint32_t opc,
                                 int rd, int rs, int rt)
{
    TCGv t0, t1;

    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }

    switch (opc) {
    case OPC_MULT_G_2E:
    case OPC_MULT_G_2F:
    case OPC_MULTU_G_2E:
    case OPC_MULTU_G_2F:
#if defined(TARGET_MIPS64)
    case OPC_DMULT_G_2E:
    case OPC_DMULT_G_2F:
    case OPC_DMULTU_G_2E:
    case OPC_DMULTU_G_2F:
#endif
        t0 = tcg_temp_new();
        t1 = tcg_temp_new();
        break;
    default:
        t0 = tcg_temp_local_new();
        t1 = tcg_temp_local_new();
        break;
    }

    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);

    switch (opc) {
    case OPC_MULT_G_2E:
    case OPC_MULT_G_2F:
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        break;
    case OPC_MULTU_G_2E:
    case OPC_MULTU_G_2F:
        tcg_gen_ext32u_tl(t0, t0);
        tcg_gen_ext32u_tl(t1, t1);
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
        break;
    case OPC_DIV_G_2E:
    case OPC_DIV_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_ext32s_tl(t0, t0);
            tcg_gen_ext32s_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, INT_MIN, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1, l2);
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l3);
        }
        break;
    case OPC_DIVU_G_2E:
    case OPC_DIVU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_divu_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l2);
        }
        break;
    case OPC_MOD_G_2E:
    case OPC_MOD_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, INT_MIN, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1, l2);
            gen_set_label(l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l3);
        }
        break;
    case OPC_MODU_G_2E:
    case OPC_MODU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_ext32u_tl(t0, t0);
            tcg_gen_ext32u_tl(t1, t1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_remu_tl(cpu_gpr[rd], t0, t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
            gen_set_label(l2);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DMULT_G_2E:
    case OPC_DMULT_G_2F:
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        break;
    case OPC_DMULTU_G_2E:
    case OPC_DMULTU_G_2F:
        tcg_gen_mul_tl(cpu_gpr[rd], t0, t1);
        break;
    case OPC_DDIV_G_2E:
    case OPC_DDIV_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, -1LL << 63, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_div_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l3);
        }
        break;
    case OPC_DDIVU_G_2E:
    case OPC_DDIVU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_divu_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l2);
        }
        break;
    case OPC_DMOD_G_2E:
    case OPC_DMOD_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            TCGLabel *l3 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, t1, 0, l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, -1LL << 63, l2);
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, -1LL, l2);
            gen_set_label(l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l3);
            gen_set_label(l2);
            tcg_gen_rem_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l3);
        }
        break;
    case OPC_DMODU_G_2E:
    case OPC_DMODU_G_2F:
        {
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
            tcg_gen_movi_tl(cpu_gpr[rd], 0);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_remu_tl(cpu_gpr[rd], t0, t1);
            gen_set_label(l2);
        }
        break;
#endif
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* Loongson multimedia instructions */
static void gen_loongson_multimedia(DisasContext *ctx, int rd, int rs, int rt)
{
    uint32_t opc, shift_max;
    TCGv_i64 t0, t1;

    opc = MASK_LMI(ctx->opcode);
    switch (opc) {
    case OPC_ADD_CP2:
    case OPC_SUB_CP2:
    case OPC_DADD_CP2:
    case OPC_DSUB_CP2:
        t0 = tcg_temp_local_new_i64();
        t1 = tcg_temp_local_new_i64();
        break;
    default:
        t0 = tcg_temp_new_i64();
        t1 = tcg_temp_new_i64();
        break;
    }

    check_cp1_enabled(ctx);
    gen_load_fpr64(ctx, t0, rs);
    gen_load_fpr64(ctx, t1, rt);

#define LMI_HELPER(UP, LO) \
    case OPC_##UP: gen_helper_##LO(t0, t0, t1); break
#define LMI_HELPER_1(UP, LO) \
    case OPC_##UP: gen_helper_##LO(t0, t0); break
#define LMI_DIRECT(UP, LO, OP) \
    case OPC_##UP: tcg_gen_##OP##_i64(t0, t0, t1); break

    switch (opc) {
    LMI_HELPER(PADDSH, paddsh);
    LMI_HELPER(PADDUSH, paddush);
    LMI_HELPER(PADDH, paddh);
    LMI_HELPER(PADDW, paddw);
    LMI_HELPER(PADDSB, paddsb);
    LMI_HELPER(PADDUSB, paddusb);
    LMI_HELPER(PADDB, paddb);

    LMI_HELPER(PSUBSH, psubsh);
    LMI_HELPER(PSUBUSH, psubush);
    LMI_HELPER(PSUBH, psubh);
    LMI_HELPER(PSUBW, psubw);
    LMI_HELPER(PSUBSB, psubsb);
    LMI_HELPER(PSUBUSB, psubusb);
    LMI_HELPER(PSUBB, psubb);

    LMI_HELPER(PSHUFH, pshufh);
    LMI_HELPER(PACKSSWH, packsswh);
    LMI_HELPER(PACKSSHB, packsshb);
    LMI_HELPER(PACKUSHB, packushb);

    LMI_HELPER(PUNPCKLHW, punpcklhw);
    LMI_HELPER(PUNPCKHHW, punpckhhw);
    LMI_HELPER(PUNPCKLBH, punpcklbh);
    LMI_HELPER(PUNPCKHBH, punpckhbh);
    LMI_HELPER(PUNPCKLWD, punpcklwd);
    LMI_HELPER(PUNPCKHWD, punpckhwd);

    LMI_HELPER(PAVGH, pavgh);
    LMI_HELPER(PAVGB, pavgb);
    LMI_HELPER(PMAXSH, pmaxsh);
    LMI_HELPER(PMINSH, pminsh);
    LMI_HELPER(PMAXUB, pmaxub);
    LMI_HELPER(PMINUB, pminub);

    LMI_HELPER(PCMPEQW, pcmpeqw);
    LMI_HELPER(PCMPGTW, pcmpgtw);
    LMI_HELPER(PCMPEQH, pcmpeqh);
    LMI_HELPER(PCMPGTH, pcmpgth);
    LMI_HELPER(PCMPEQB, pcmpeqb);
    LMI_HELPER(PCMPGTB, pcmpgtb);

    LMI_HELPER(PSLLW, psllw);
    LMI_HELPER(PSLLH, psllh);
    LMI_HELPER(PSRLW, psrlw);
    LMI_HELPER(PSRLH, psrlh);
    LMI_HELPER(PSRAW, psraw);
    LMI_HELPER(PSRAH, psrah);

    LMI_HELPER(PMULLH, pmullh);
    LMI_HELPER(PMULHH, pmulhh);
    LMI_HELPER(PMULHUH, pmulhuh);
    LMI_HELPER(PMADDHW, pmaddhw);

    LMI_HELPER(PASUBUB, pasubub);
    LMI_HELPER_1(BIADD, biadd);
    LMI_HELPER_1(PMOVMSKB, pmovmskb);

    LMI_DIRECT(PADDD, paddd, add);
    LMI_DIRECT(PSUBD, psubd, sub);
    LMI_DIRECT(XOR_CP2, xor, xor);
    LMI_DIRECT(NOR_CP2, nor, nor);
    LMI_DIRECT(AND_CP2, and, and);
    LMI_DIRECT(OR_CP2, or, or);

    case OPC_PANDN:
        tcg_gen_andc_i64(t0, t1, t0);
        break;

    case OPC_PINSRH_0:
        tcg_gen_deposit_i64(t0, t0, t1, 0, 16);
        break;
    case OPC_PINSRH_1:
        tcg_gen_deposit_i64(t0, t0, t1, 16, 16);
        break;
    case OPC_PINSRH_2:
        tcg_gen_deposit_i64(t0, t0, t1, 32, 16);
        break;
    case OPC_PINSRH_3:
        tcg_gen_deposit_i64(t0, t0, t1, 48, 16);
        break;

    case OPC_PEXTRH:
        tcg_gen_andi_i64(t1, t1, 3);
        tcg_gen_shli_i64(t1, t1, 4);
        tcg_gen_shr_i64(t0, t0, t1);
        tcg_gen_ext16u_i64(t0, t0);
        break;

    case OPC_ADDU_CP2:
        tcg_gen_add_i64(t0, t0, t1);
        tcg_gen_ext32s_i64(t0, t0);
        break;
    case OPC_SUBU_CP2:
        tcg_gen_sub_i64(t0, t0, t1);
        tcg_gen_ext32s_i64(t0, t0);
        break;

    case OPC_SLL_CP2:
        shift_max = 32;
        goto do_shift;
    case OPC_SRL_CP2:
        shift_max = 32;
        goto do_shift;
    case OPC_SRA_CP2:
        shift_max = 32;
        goto do_shift;
    case OPC_DSLL_CP2:
        shift_max = 64;
        goto do_shift;
    case OPC_DSRL_CP2:
        shift_max = 64;
        goto do_shift;
    case OPC_DSRA_CP2:
        shift_max = 64;
        goto do_shift;
    do_shift:
        /* Make sure shift count isn't TCG undefined behaviour.  */
        tcg_gen_andi_i64(t1, t1, shift_max - 1);

        switch (opc) {
        case OPC_SLL_CP2:
        case OPC_DSLL_CP2:
            tcg_gen_shl_i64(t0, t0, t1);
            break;
        case OPC_SRA_CP2:
        case OPC_DSRA_CP2:
            /* Since SRA is UndefinedResult without sign-extended inputs,
               we can treat SRA and DSRA the same.  */
            tcg_gen_sar_i64(t0, t0, t1);
            break;
        case OPC_SRL_CP2:
            /* We want to shift in zeros for SRL; zero-extend first.  */
            tcg_gen_ext32u_i64(t0, t0);
            /* FALLTHRU */
        case OPC_DSRL_CP2:
            tcg_gen_shr_i64(t0, t0, t1);
            break;
        }

        if (shift_max == 32) {
            tcg_gen_ext32s_i64(t0, t0);
        }

        /* Shifts larger than MAX produce zero.  */
        tcg_gen_setcondi_i64(TCG_COND_LTU, t1, t1, shift_max);
        tcg_gen_neg_i64(t1, t1);
        tcg_gen_and_i64(t0, t0, t1);
        break;

    case OPC_ADD_CP2:
    case OPC_DADD_CP2:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGLabel *lab = gen_new_label();

            tcg_gen_mov_i64(t2, t0);
            tcg_gen_add_i64(t0, t1, t2);
            if (opc == OPC_ADD_CP2) {
                tcg_gen_ext32s_i64(t0, t0);
            }
            tcg_gen_xor_i64(t1, t1, t2);
            tcg_gen_xor_i64(t2, t2, t0);
            tcg_gen_andc_i64(t1, t2, t1);
            tcg_temp_free_i64(t2);
            tcg_gen_brcondi_i64(TCG_COND_GE, t1, 0, lab);
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(lab);
            break;
        }

    case OPC_SUB_CP2:
    case OPC_DSUB_CP2:
        {
            TCGv_i64 t2 = tcg_temp_new_i64();
            TCGLabel *lab = gen_new_label();

            tcg_gen_mov_i64(t2, t0);
            tcg_gen_sub_i64(t0, t1, t2);
            if (opc == OPC_SUB_CP2) {
                tcg_gen_ext32s_i64(t0, t0);
            }
            tcg_gen_xor_i64(t1, t1, t2);
            tcg_gen_xor_i64(t2, t2, t0);
            tcg_gen_and_i64(t1, t1, t2);
            tcg_temp_free_i64(t2);
            tcg_gen_brcondi_i64(TCG_COND_GE, t1, 0, lab);
            generate_exception(ctx, EXCP_OVERFLOW);
            gen_set_label(lab);
            break;
        }

    case OPC_PMULUW:
        tcg_gen_ext32u_i64(t0, t0);
        tcg_gen_ext32u_i64(t1, t1);
        tcg_gen_mul_i64(t0, t0, t1);
        break;

    case OPC_SEQU_CP2:
    case OPC_SEQ_CP2:
    case OPC_SLTU_CP2:
    case OPC_SLT_CP2:
    case OPC_SLEU_CP2:
    case OPC_SLE_CP2:
        /* ??? Document is unclear: Set FCC[CC].  Does that mean the
           FD field is the CC field?  */
    default:
        MIPS_INVAL("loongson_cp2");
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

#undef LMI_HELPER
#undef LMI_DIRECT

    gen_store_fpr64(ctx, t0, rd);

    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

/* Traps */
static void gen_trap (DisasContext *ctx, uint32_t opc,
                      int rs, int rt, int16_t imm)
{
    int cond;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

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
            generate_exception_end(ctx, EXCP_TRAP);
            break;
        case OPC_TLT:   /* rs < rs           */
        case OPC_TLTI:  /* r0 < 0            */
        case OPC_TLTU:  /* rs < rs unsigned  */
        case OPC_TLTIU: /* r0 < 0  unsigned  */
        case OPC_TNE:   /* rs != rs          */
        case OPC_TNEI:  /* r0 != 0           */
            /* Never trap: treat as NOP. */
            break;
        }
    } else {
        TCGLabel *l1 = gen_new_label();

        switch (opc) {
        case OPC_TEQ:
        case OPC_TEQI:
            tcg_gen_brcond_tl(TCG_COND_NE, t0, t1, l1);
            break;
        case OPC_TGE:
        case OPC_TGEI:
            tcg_gen_brcond_tl(TCG_COND_LT, t0, t1, l1);
            break;
        case OPC_TGEU:
        case OPC_TGEIU:
            tcg_gen_brcond_tl(TCG_COND_LTU, t0, t1, l1);
            break;
        case OPC_TLT:
        case OPC_TLTI:
            tcg_gen_brcond_tl(TCG_COND_GE, t0, t1, l1);
            break;
        case OPC_TLTU:
        case OPC_TLTIU:
            tcg_gen_brcond_tl(TCG_COND_GEU, t0, t1, l1);
            break;
        case OPC_TNE:
        case OPC_TNEI:
            tcg_gen_brcond_tl(TCG_COND_EQ, t0, t1, l1);
            break;
        }
        generate_exception(ctx, EXCP_TRAP);
        gen_set_label(l1);
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static inline bool use_goto_tb(DisasContext *ctx, target_ulong dest)
{
    if (unlikely(ctx->singlestep_enabled)) {
        return false;
    }

#ifndef CONFIG_USER_ONLY
    return (ctx->tb->pc & TARGET_PAGE_MASK) == (dest & TARGET_PAGE_MASK);
#else
    return true;
#endif
}

static inline void gen_goto_tb(DisasContext *ctx, int n, target_ulong dest)
{
    if (use_goto_tb(ctx, dest)) {
        tcg_gen_goto_tb(n);
        gen_save_pc(dest);
        tcg_gen_exit_tb((uintptr_t)ctx->tb + n);
    } else {
        gen_save_pc(dest);
        if (ctx->singlestep_enabled) {
            save_cpu_state(ctx, 0);
            gen_helper_raise_exception_debug(cpu_env);
        }
        tcg_gen_lookup_and_goto_ptr(cpu_PC);
    }
}

/* Branches (before delay slot) */
static void gen_compute_branch (DisasContext *ctx, uint32_t opc,
                                int insn_bytes,
                                int rs, int rt, int32_t offset,
                                int delayslot_size)
{
    target_ulong btgt = -1;
    int blink = 0;
    int bcond_compute = 0;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
#ifdef MIPS_DEBUG_DISAS
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x"
                  TARGET_FMT_lx "\n", ctx->pc);
#endif
        generate_exception_end(ctx, EXCP_RI);
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
        btgt = ctx->pc + insn_bytes + offset;
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
        btgt = ctx->pc + insn_bytes + offset;
        break;
    case OPC_BPOSGE32:
#if defined(TARGET_MIPS64)
    case OPC_BPOSGE64:
        tcg_gen_andi_tl(t0, cpu_dspctrl, 0x7F);
#else
        tcg_gen_andi_tl(t0, cpu_dspctrl, 0x3F);
#endif
        bcond_compute = 1;
        btgt = ctx->pc + insn_bytes + offset;
        break;
    case OPC_J:
    case OPC_JAL:
    case OPC_JALX:
        /* Jump to immediate */
        btgt = ((ctx->pc + insn_bytes) & (int32_t)0xF0000000) | (uint32_t)offset;
        break;
    case OPC_JR:
    case OPC_JALR:
        /* Jump to register */
        if (offset != 0 && offset != 16) {
            /* Hint = 0 is JR/JALR, hint 16 is JR.HB/JALR.HB, the
               others are reserved. */
            MIPS_INVAL("jump hint");
            generate_exception_end(ctx, EXCP_RI);
            goto out;
        }
        gen_load_gpr(btarget, rs);
        break;
    default:
        MIPS_INVAL("branch/jump");
        generate_exception_end(ctx, EXCP_RI);
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
            break;
        case OPC_BGEZAL:  /* 0 >= 0          */
        case OPC_BGEZALL: /* 0 >= 0 likely   */
            /* Always take and link */
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        case OPC_BNE:     /* rx != rx        */
        case OPC_BGTZ:    /* 0 > 0           */
        case OPC_BLTZ:    /* 0 < 0           */
            /* Treat as NOP. */
            goto out;
        case OPC_BLTZAL:  /* 0 < 0           */
            /* Handle as an unconditional branch to get correct delay
               slot checking.  */
            blink = 31;
            btgt = ctx->pc + insn_bytes + delayslot_size;
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        case OPC_BLTZALL: /* 0 < 0 likely */
            tcg_gen_movi_tl(cpu_gpr[31], ctx->pc + 8);
            /* Skip the instruction in the delay slot */
            ctx->pc += 4;
            goto out;
        case OPC_BNEL:    /* rx != rx likely */
        case OPC_BGTZL:   /* 0 > 0 likely */
        case OPC_BLTZL:   /* 0 < 0 likely */
            /* Skip the instruction in the delay slot */
            ctx->pc += 4;
            goto out;
        case OPC_J:
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        case OPC_JALX:
            ctx->hflags |= MIPS_HFLAG_BX;
            /* Fallthrough */
        case OPC_JAL:
            blink = 31;
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        case OPC_JR:
            ctx->hflags |= MIPS_HFLAG_BR;
            break;
        case OPC_JALR:
            blink = rt;
            ctx->hflags |= MIPS_HFLAG_BR;
            break;
        default:
            MIPS_INVAL("branch/jump");
            generate_exception_end(ctx, EXCP_RI);
            goto out;
        }
    } else {
        switch (opc) {
        case OPC_BEQ:
            tcg_gen_setcond_tl(TCG_COND_EQ, bcond, t0, t1);
            goto not_likely;
        case OPC_BEQL:
            tcg_gen_setcond_tl(TCG_COND_EQ, bcond, t0, t1);
            goto likely;
        case OPC_BNE:
            tcg_gen_setcond_tl(TCG_COND_NE, bcond, t0, t1);
            goto not_likely;
        case OPC_BNEL:
            tcg_gen_setcond_tl(TCG_COND_NE, bcond, t0, t1);
            goto likely;
        case OPC_BGEZ:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 0);
            goto not_likely;
        case OPC_BGEZL:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 0);
            goto likely;
        case OPC_BGEZAL:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 0);
            blink = 31;
            goto not_likely;
        case OPC_BGEZALL:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 0);
            blink = 31;
            goto likely;
        case OPC_BGTZ:
            tcg_gen_setcondi_tl(TCG_COND_GT, bcond, t0, 0);
            goto not_likely;
        case OPC_BGTZL:
            tcg_gen_setcondi_tl(TCG_COND_GT, bcond, t0, 0);
            goto likely;
        case OPC_BLEZ:
            tcg_gen_setcondi_tl(TCG_COND_LE, bcond, t0, 0);
            goto not_likely;
        case OPC_BLEZL:
            tcg_gen_setcondi_tl(TCG_COND_LE, bcond, t0, 0);
            goto likely;
        case OPC_BLTZ:
            tcg_gen_setcondi_tl(TCG_COND_LT, bcond, t0, 0);
            goto not_likely;
        case OPC_BLTZL:
            tcg_gen_setcondi_tl(TCG_COND_LT, bcond, t0, 0);
            goto likely;
        case OPC_BPOSGE32:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 32);
            goto not_likely;
#if defined(TARGET_MIPS64)
        case OPC_BPOSGE64:
            tcg_gen_setcondi_tl(TCG_COND_GE, bcond, t0, 64);
            goto not_likely;
#endif
        case OPC_BLTZAL:
            tcg_gen_setcondi_tl(TCG_COND_LT, bcond, t0, 0);
            blink = 31;
        not_likely:
            ctx->hflags |= MIPS_HFLAG_BC;
            break;
        case OPC_BLTZALL:
            tcg_gen_setcondi_tl(TCG_COND_LT, bcond, t0, 0);
            blink = 31;
        likely:
            ctx->hflags |= MIPS_HFLAG_BL;
            break;
        default:
            MIPS_INVAL("conditional branch/jump");
            generate_exception_end(ctx, EXCP_RI);
            goto out;
        }
    }

    ctx->btarget = btgt;

    switch (delayslot_size) {
    case 2:
        ctx->hflags |= MIPS_HFLAG_BDS16;
        break;
    case 4:
        ctx->hflags |= MIPS_HFLAG_BDS32;
        break;
    }

    if (blink > 0) {
        int post_delay = insn_bytes + delayslot_size;
        int lowbit = !!(ctx->hflags & MIPS_HFLAG_M16);

        tcg_gen_movi_tl(cpu_gpr[blink], ctx->pc + post_delay + lowbit);
    }

 out:
    if (insn_bytes == 2)
        ctx->hflags |= MIPS_HFLAG_B16;
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* special3 bitfield operations */
static void gen_bitops (DisasContext *ctx, uint32_t opc, int rt,
                        int rs, int lsb, int msb)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t1, rs);
    switch (opc) {
    case OPC_EXT:
        if (lsb + msb > 31) {
            goto fail;
        }
        if (msb != 31) {
            tcg_gen_extract_tl(t0, t1, lsb, msb + 1);
        } else {
            /* The two checks together imply that lsb == 0,
               so this is a simple sign-extension.  */
            tcg_gen_ext32s_tl(t0, t1);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DEXTU:
        lsb += 32;
        goto do_dext;
    case OPC_DEXTM:
        msb += 32;
        goto do_dext;
    case OPC_DEXT:
    do_dext:
        if (lsb + msb > 63) {
            goto fail;
        }
        tcg_gen_extract_tl(t0, t1, lsb, msb + 1);
        break;
#endif
    case OPC_INS:
        if (lsb > msb) {
            goto fail;
        }
        gen_load_gpr(t0, rt);
        tcg_gen_deposit_tl(t0, t0, t1, lsb, msb - lsb + 1);
        tcg_gen_ext32s_tl(t0, t0);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DINSU:
        lsb += 32;
        /* FALLTHRU */
    case OPC_DINSM:
        msb += 32;
        /* FALLTHRU */
    case OPC_DINS:
        if (lsb > msb) {
            goto fail;
        }
        gen_load_gpr(t0, rt);
        tcg_gen_deposit_tl(t0, t0, t1, lsb, msb - lsb + 1);
        break;
#endif
    default:
fail:
        MIPS_INVAL("bitops");
        generate_exception_end(ctx, EXCP_RI);
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
    TCGv t0;

    if (rd == 0) {
        /* If no destination, treat it as a NOP. */
        return;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, rt);
    switch (op2) {
    case OPC_WSBH:
        {
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_const_tl(0x00FF00FF);

            tcg_gen_shri_tl(t1, t0, 8);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_gen_and_tl(t0, t0, t2);
            tcg_gen_shli_tl(t0, t0, 8);
            tcg_gen_or_tl(t0, t0, t1);
            tcg_temp_free(t2);
            tcg_temp_free(t1);
            tcg_gen_ext32s_tl(cpu_gpr[rd], t0);
        }
        break;
    case OPC_SEB:
        tcg_gen_ext8s_tl(cpu_gpr[rd], t0);
        break;
    case OPC_SEH:
        tcg_gen_ext16s_tl(cpu_gpr[rd], t0);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DSBH:
        {
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_const_tl(0x00FF00FF00FF00FFULL);

            tcg_gen_shri_tl(t1, t0, 8);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_gen_and_tl(t0, t0, t2);
            tcg_gen_shli_tl(t0, t0, 8);
            tcg_gen_or_tl(cpu_gpr[rd], t0, t1);
            tcg_temp_free(t2);
            tcg_temp_free(t1);
        }
        break;
    case OPC_DSHD:
        {
            TCGv t1 = tcg_temp_new();
            TCGv t2 = tcg_const_tl(0x0000FFFF0000FFFFULL);

            tcg_gen_shri_tl(t1, t0, 16);
            tcg_gen_and_tl(t1, t1, t2);
            tcg_gen_and_tl(t0, t0, t2);
            tcg_gen_shli_tl(t0, t0, 16);
            tcg_gen_or_tl(t0, t0, t1);
            tcg_gen_shri_tl(t1, t0, 32);
            tcg_gen_shli_tl(t0, t0, 32);
            tcg_gen_or_tl(cpu_gpr[rd], t0, t1);
            tcg_temp_free(t2);
            tcg_temp_free(t1);
        }
        break;
#endif
    default:
        MIPS_INVAL("bsfhl");
        generate_exception_end(ctx, EXCP_RI);
        tcg_temp_free(t0);
        return;
    }
    tcg_temp_free(t0);
}

static void gen_lsa(DisasContext *ctx, int opc, int rd, int rs, int rt,
                    int imm2)
{
    TCGv t0;
    TCGv t1;
    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }
    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    gen_load_gpr(t0, rs);
    gen_load_gpr(t1, rt);
    tcg_gen_shli_tl(t0, t0, imm2 + 1);
    tcg_gen_add_tl(cpu_gpr[rd], t0, t1);
    if (opc == OPC_LSA) {
        tcg_gen_ext32s_tl(cpu_gpr[rd], cpu_gpr[rd]);
    }

    tcg_temp_free(t1);
    tcg_temp_free(t0);

    return;
}

static void gen_align(DisasContext *ctx, int opc, int rd, int rs, int rt,
                      int bp)
{
    TCGv t0;
    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }
    t0 = tcg_temp_new();
    gen_load_gpr(t0, rt);
    if (bp == 0) {
        switch (opc) {
        case OPC_ALIGN:
            tcg_gen_ext32s_tl(cpu_gpr[rd], t0);
            break;
#if defined(TARGET_MIPS64)
        case OPC_DALIGN:
            tcg_gen_mov_tl(cpu_gpr[rd], t0);
            break;
#endif
        }
    } else {
        TCGv t1 = tcg_temp_new();
        gen_load_gpr(t1, rs);
        switch (opc) {
        case OPC_ALIGN:
            {
                TCGv_i64 t2 = tcg_temp_new_i64();
                tcg_gen_concat_tl_i64(t2, t1, t0);
                tcg_gen_shri_i64(t2, t2, 8 * (4 - bp));
                gen_move_low32(cpu_gpr[rd], t2);
                tcg_temp_free_i64(t2);
            }
            break;
#if defined(TARGET_MIPS64)
        case OPC_DALIGN:
            tcg_gen_shli_tl(t0, t0, 8 * bp);
            tcg_gen_shri_tl(t1, t1, 8 * (8 - bp));
            tcg_gen_or_tl(cpu_gpr[rd], t1, t0);
            break;
#endif
        }
        tcg_temp_free(t1);
    }

    tcg_temp_free(t0);
}

static void gen_bitswap(DisasContext *ctx, int opc, int rd, int rt)
{
    TCGv t0;
    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }
    t0 = tcg_temp_new();
    gen_load_gpr(t0, rt);
    switch (opc) {
    case OPC_BITSWAP:
        gen_helper_bitswap(cpu_gpr[rd], t0);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DBITSWAP:
        gen_helper_dbitswap(cpu_gpr[rd], t0);
        break;
#endif
    }
    tcg_temp_free(t0);
}

#ifndef CONFIG_USER_ONLY
/* CP0 (MMU and control) */
static inline void gen_mthc0_entrylo(TCGv arg, target_ulong off)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_ext_tl_i64(t0, arg);
    tcg_gen_ld_i64(t1, cpu_env, off);
#if defined(TARGET_MIPS64)
    tcg_gen_deposit_i64(t1, t1, t0, 30, 32);
#else
    tcg_gen_concat32_i64(t1, t1, t0);
#endif
    tcg_gen_st_i64(t1, cpu_env, off);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t0);
}

static inline void gen_mthc0_store64(TCGv arg, target_ulong off)
{
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();

    tcg_gen_ext_tl_i64(t0, arg);
    tcg_gen_ld_i64(t1, cpu_env, off);
    tcg_gen_concat32_i64(t1, t1, t0);
    tcg_gen_st_i64(t1, cpu_env, off);
    tcg_temp_free_i64(t1);
    tcg_temp_free_i64(t0);
}

static inline void gen_mfhc0_entrylo(TCGv arg, target_ulong off)
{
    TCGv_i64 t0 = tcg_temp_new_i64();

    tcg_gen_ld_i64(t0, cpu_env, off);
#if defined(TARGET_MIPS64)
    tcg_gen_shri_i64(t0, t0, 30);
#else
    tcg_gen_shri_i64(t0, t0, 32);
#endif
    gen_move_low32(arg, t0);
    tcg_temp_free_i64(t0);
}

static inline void gen_mfhc0_load64(TCGv arg, target_ulong off, int shift)
{
    TCGv_i64 t0 = tcg_temp_new_i64();

    tcg_gen_ld_i64(t0, cpu_env, off);
    tcg_gen_shri_i64(t0, t0, 32 + shift);
    gen_move_low32(arg, t0);
    tcg_temp_free_i64(t0);
}

static inline void gen_mfc0_load32 (TCGv arg, target_ulong off)
{
    TCGv_i32 t0 = tcg_temp_new_i32();

    tcg_gen_ld_i32(t0, cpu_env, off);
    tcg_gen_ext_i32_tl(arg, t0);
    tcg_temp_free_i32(t0);
}

static inline void gen_mfc0_load64 (TCGv arg, target_ulong off)
{
    tcg_gen_ld_tl(arg, cpu_env, off);
    tcg_gen_ext32s_tl(arg, arg);
}

static inline void gen_mtc0_store32 (TCGv arg, target_ulong off)
{
    TCGv_i32 t0 = tcg_temp_new_i32();

    tcg_gen_trunc_tl_i32(t0, arg);
    tcg_gen_st_i32(t0, cpu_env, off);
    tcg_temp_free_i32(t0);
}

#define CP0_CHECK(c)                            \
    do {                                        \
        if (!(c)) {                             \
            goto cp0_unimplemented;             \
        }                                       \
    } while (0)

static void gen_mfhc0(DisasContext *ctx, TCGv arg, int reg, int sel)
{
    const char *rn = "invalid";

    CP0_CHECK(ctx->hflags & MIPS_HFLAG_ELPA);

    switch (reg) {
    case 2:
        switch (sel) {
        case 0:
            gen_mfhc0_entrylo(arg, offsetof(CPUMIPSState, CP0_EntryLo0));
            rn = "EntryLo0";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_mfhc0_entrylo(arg, offsetof(CPUMIPSState, CP0_EntryLo1));
            rn = "EntryLo1";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_mfhc0_load64(arg, offsetof(CPUMIPSState, lladdr),
                             ctx->CP0_LLAddr_shift);
            rn = "LLAddr";
            break;
        case 1:
            CP0_CHECK(ctx->mrp);
            gen_helper_mfhc0_maar(arg, cpu_env);
            rn = "MAAR";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfhc0_load64(arg, offsetof(CPUMIPSState, CP0_TagLo), 0);
            rn = "TagLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    default:
        goto cp0_unimplemented;
    }
    trace_mips_translate_c0("mfhc0", rn, reg, sel);
    return;

cp0_unimplemented:
    qemu_log_mask(LOG_UNIMP, "mfhc0 %s (reg %d sel %d)\n", rn, reg, sel);
    tcg_gen_movi_tl(arg, 0);
}

static void gen_mthc0(DisasContext *ctx, TCGv arg, int reg, int sel)
{
    const char *rn = "invalid";
    uint64_t mask = ctx->PAMask >> 36;

    CP0_CHECK(ctx->hflags & MIPS_HFLAG_ELPA);

    switch (reg) {
    case 2:
        switch (sel) {
        case 0:
            tcg_gen_andi_tl(arg, arg, mask);
            gen_mthc0_entrylo(arg, offsetof(CPUMIPSState, CP0_EntryLo0));
            rn = "EntryLo0";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            tcg_gen_andi_tl(arg, arg, mask);
            gen_mthc0_entrylo(arg, offsetof(CPUMIPSState, CP0_EntryLo1));
            rn = "EntryLo1";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            /* LLAddr is read-only (the only exception is bit 0 if LLB is
               supported); the CP0_LLAddr_rw_bitmask does not seem to be
               relevant for modern MIPS cores supporting MTHC0, therefore
               treating MTHC0 to LLAddr as NOP. */
            rn = "LLAddr";
            break;
        case 1:
            CP0_CHECK(ctx->mrp);
            gen_helper_mthc0_maar(cpu_env, arg);
            rn = "MAAR";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            tcg_gen_andi_tl(arg, arg, mask);
            gen_mthc0_store64(arg, offsetof(CPUMIPSState, CP0_TagLo));
            rn = "TagLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    default:
        goto cp0_unimplemented;
    }
    trace_mips_translate_c0("mthc0", rn, reg, sel);

cp0_unimplemented:
    qemu_log_mask(LOG_UNIMP, "mthc0 %s (reg %d sel %d)\n", rn, reg, sel);
}

static inline void gen_mfc0_unimplemented(DisasContext *ctx, TCGv arg)
{
    if (ctx->insn_flags & ISA_MIPS32R6) {
        tcg_gen_movi_tl(arg, 0);
    } else {
        tcg_gen_movi_tl(arg, ~0);
    }
}

static void gen_mfc0(DisasContext *ctx, TCGv arg, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(ctx, ISA_MIPS32);

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Index));
            rn = "Index";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_mvpcontrol(arg, cpu_env);
            rn = "MVPControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_mvpconf0(arg, cpu_env);
            rn = "MVPConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_mvpconf1(arg, cpu_env);
            rn = "MVPConf1";
            break;
        case 4:
            CP0_CHECK(ctx->vp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPControl));
            rn = "VPControl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            CP0_CHECK(!(ctx->insn_flags & ISA_MIPS32R6));
            gen_helper_mfc0_random(arg, cpu_env);
            rn = "Random";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEControl));
            rn = "VPEControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEConf0));
            rn = "VPEConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEConf1));
            rn = "VPEConf1";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load64(arg, offsetof(CPUMIPSState, CP0_YQMask));
            rn = "YQMask";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load64(arg, offsetof(CPUMIPSState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load64(arg, offsetof(CPUMIPSState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEOpt));
            rn = "VPEOpt";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            {
                TCGv_i64 tmp = tcg_temp_new_i64();
                tcg_gen_ld_i64(tmp, cpu_env,
                               offsetof(CPUMIPSState, CP0_EntryLo0));
#if defined(TARGET_MIPS64)
                if (ctx->rxi) {
                    /* Move RI/XI fields to bits 31:30 */
                    tcg_gen_shri_tl(arg, tmp, CP0EnLo_XI);
                    tcg_gen_deposit_tl(tmp, tmp, arg, 30, 2);
                }
#endif
                gen_move_low32(arg, tmp);
                tcg_temp_free_i64(tmp);
            }
            rn = "EntryLo0";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcstatus(arg, cpu_env);
            rn = "TCStatus";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcbind(arg, cpu_env);
            rn = "TCBind";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcrestart(arg, cpu_env);
            rn = "TCRestart";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tchalt(arg, cpu_env);
            rn = "TCHalt";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tccontext(arg, cpu_env);
            rn = "TCContext";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcschedule(arg, cpu_env);
            rn = "TCSchedule";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcschefback(arg, cpu_env);
            rn = "TCScheFBack";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            {
                TCGv_i64 tmp = tcg_temp_new_i64();
                tcg_gen_ld_i64(tmp, cpu_env,
                               offsetof(CPUMIPSState, CP0_EntryLo1));
#if defined(TARGET_MIPS64)
                if (ctx->rxi) {
                    /* Move RI/XI fields to bits 31:30 */
                    tcg_gen_shri_tl(arg, tmp, CP0EnLo_XI);
                    tcg_gen_deposit_tl(tmp, tmp, arg, 30, 2);
                }
#endif
                gen_move_low32(arg, tmp);
                tcg_temp_free_i64(tmp);
            }
            rn = "EntryLo1";
            break;
        case 1:
            CP0_CHECK(ctx->vp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_GlobalNumber));
            rn = "GlobalNumber";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_Context));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "Context";
            break;
        case 1:
//            gen_helper_mfc0_contextconfig(arg); /* SmartMIPS ASE */
            rn = "ContextConfig";
            goto cp0_unimplemented;
        case 2:
            CP0_CHECK(ctx->ulri);
            tcg_gen_ld_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, active_tc.CP0_UserLocal));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "UserLocal";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_PageMask));
            rn = "PageMask";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_PageGrain));
            rn = "PageGrain";
            break;
        case 2:
            CP0_CHECK(ctx->sc);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_SegCtl0));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "SegCtl0";
            break;
        case 3:
            CP0_CHECK(ctx->sc);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_SegCtl1));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "SegCtl1";
            break;
        case 4:
            CP0_CHECK(ctx->sc);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_SegCtl2));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "SegCtl2";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Wired));
            rn = "Wired";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf0));
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf1));
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf2));
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf3));
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf4));
            rn = "SRSConf4";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_HWREna));
            rn = "HWREna";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_BadVAddr));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "BadVAddr";
            break;
        case 1:
            CP0_CHECK(ctx->bi);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_BadInstr));
            rn = "BadInstr";
            break;
        case 2:
            CP0_CHECK(ctx->bp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_BadInstrP));
            rn = "BadInstrP";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 9:
        switch (sel) {
        case 0:
            /* Mark as an IO operation because we read the time.  */
            if (ctx->tb->cflags & CF_USE_ICOUNT) {
                gen_io_start();
	    }
            gen_helper_mfc0_count(arg, cpu_env);
            if (ctx->tb->cflags & CF_USE_ICOUNT) {
                gen_io_end();
            }
            /* Break the TB to be able to take timer interrupts immediately
               after reading count. BS_STOP isn't sufficient, we need to ensure
               we break completely out of translated code.  */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EntryHi));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "EntryHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Compare));
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Status));
            rn = "Status";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_IntCtl));
            rn = "IntCtl";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSCtl));
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSMap));
            rn = "SRSMap";
            break;
        default:
            goto cp0_unimplemented;
       }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Cause));
            rn = "Cause";
            break;
        default:
            goto cp0_unimplemented;
       }
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EPC));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "EPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_PRid));
            rn = "PRid";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EBase));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "EBase";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            CP0_CHECK(ctx->cmgcr);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_CMGCRBase));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "CMGCRBase";
            break;
        default:
            goto cp0_unimplemented;
       }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config0));
            rn = "Config";
            break;
        case 1:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config1));
            rn = "Config1";
            break;
        case 2:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config2));
            rn = "Config2";
            break;
        case 3:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config3));
            rn = "Config3";
            break;
        case 4:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config4));
            rn = "Config4";
            break;
        case 5:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config5));
            rn = "Config5";
            break;
        /* 6,7 are implementation dependent */
        case 6:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config6));
            rn = "Config6";
            break;
        case 7:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config7));
            rn = "Config7";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_helper_mfc0_lladdr(arg, cpu_env);
            rn = "LLAddr";
            break;
        case 1:
            CP0_CHECK(ctx->mrp);
            gen_helper_mfc0_maar(arg, cpu_env);
            rn = "MAAR";
            break;
        case 2:
            CP0_CHECK(ctx->mrp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_MAARI));
            rn = "MAARI";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1e0i(mfc0_watchlo, arg, sel);
            rn = "WatchLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ...7:
            gen_helper_1e0i(mfc0_watchhi, arg, sel);
            rn = "WatchHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#if defined(TARGET_MIPS64)
            check_insn(ctx, ISA_MIPS3);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_XContext));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "XContext";
            break;
#endif
        default:
            goto cp0_unimplemented;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        CP0_CHECK(!(ctx->insn_flags & ISA_MIPS32R6));
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Framemask));
            rn = "Framemask";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 22:
        tcg_gen_movi_tl(arg, 0); /* unimplemented */
        rn = "'Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mfc0_debug(arg, cpu_env); /* EJTAG support */
            rn = "Debug";
            break;
        case 1:
//            gen_helper_mfc0_tracecontrol(arg); /* PDtrace support */
            rn = "TraceControl";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_mfc0_tracecontrol2(arg); /* PDtrace support */
            rn = "TraceControl2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_mfc0_usertracedata(arg); /* PDtrace support */
            rn = "UserTraceData";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_mfc0_tracebpc(arg); /* PDtrace support */
            rn = "TraceBPC";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_DEPC));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "DEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Performance0));
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_mfc0_performance1(arg);
            rn = "Performance1";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_mfc0_performance2(arg);
            rn = "Performance2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_mfc0_performance3(arg);
            rn = "Performance3";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_mfc0_performance4(arg);
            rn = "Performance4";
            goto cp0_unimplemented;
        case 5:
//            gen_helper_mfc0_performance5(arg);
            rn = "Performance5";
            goto cp0_unimplemented;
        case 6:
//            gen_helper_mfc0_performance6(arg);
            rn = "Performance6";
            goto cp0_unimplemented;
        case 7:
//            gen_helper_mfc0_performance7(arg);
            rn = "Performance7";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 26:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_ErrCtl));
            rn = "ErrCtl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            tcg_gen_movi_tl(arg, 0); /* unimplemented */
            rn = "CacheErr";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            {
                TCGv_i64 tmp = tcg_temp_new_i64();
                tcg_gen_ld_i64(tmp, cpu_env, offsetof(CPUMIPSState, CP0_TagLo));
                gen_move_low32(arg, tmp);
                tcg_temp_free_i64(tmp);
            }
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_DataLo));
            rn = "DataLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_TagHi));
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_DataHi));
            rn = "DataHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_ErrorEPC));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "ErrorEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        case 2 ... 7:
            CP0_CHECK(ctx->kscrexist & (1 << sel));
            tcg_gen_ld_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, CP0_KScratch[sel-2]));
            tcg_gen_ext32s_tl(arg, arg);
            rn = "KScratch";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    default:
       goto cp0_unimplemented;
    }
    trace_mips_translate_c0("mfc0", rn, reg, sel);
    return;

cp0_unimplemented:
    qemu_log_mask(LOG_UNIMP, "mfc0 %s (reg %d sel %d)\n", rn, reg, sel);
    gen_mfc0_unimplemented(ctx, arg);
}

static void gen_mtc0(DisasContext *ctx, TCGv arg, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(ctx, ISA_MIPS32);

    if (ctx->tb->cflags & CF_USE_ICOUNT) {
        gen_io_start();
    }

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_helper_mtc0_index(cpu_env, arg);
            rn = "Index";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_mvpcontrol(cpu_env, arg);
            rn = "MVPControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            /* ignored */
            rn = "MVPConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            /* ignored */
            rn = "MVPConf1";
            break;
        case 4:
            CP0_CHECK(ctx->vp);
            /* ignored */
            rn = "VPControl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "Random";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpecontrol(cpu_env, arg);
            rn = "VPEControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpeconf0(cpu_env, arg);
            rn = "VPEConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpeconf1(cpu_env, arg);
            rn = "VPEConf1";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_yqmask(cpu_env, arg);
            rn = "YQMask";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_st_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_st_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpeopt(cpu_env, arg);
            rn = "VPEOpt";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entrylo0(cpu_env, arg);
            rn = "EntryLo0";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcstatus(cpu_env, arg);
            rn = "TCStatus";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcbind(cpu_env, arg);
            rn = "TCBind";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcrestart(cpu_env, arg);
            rn = "TCRestart";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tchalt(cpu_env, arg);
            rn = "TCHalt";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tccontext(cpu_env, arg);
            rn = "TCContext";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcschedule(cpu_env, arg);
            rn = "TCSchedule";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcschefback(cpu_env, arg);
            rn = "TCScheFBack";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entrylo1(cpu_env, arg);
            rn = "EntryLo1";
            break;
        case 1:
            CP0_CHECK(ctx->vp);
            /* ignored */
            rn = "GlobalNumber";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_helper_mtc0_context(cpu_env, arg);
            rn = "Context";
            break;
        case 1:
//            gen_helper_mtc0_contextconfig(cpu_env, arg); /* SmartMIPS ASE */
            rn = "ContextConfig";
            goto cp0_unimplemented;
        case 2:
            CP0_CHECK(ctx->ulri);
            tcg_gen_st_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, active_tc.CP0_UserLocal));
            rn = "UserLocal";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_helper_mtc0_pagemask(cpu_env, arg);
            rn = "PageMask";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_pagegrain(cpu_env, arg);
            rn = "PageGrain";
            ctx->bstate = BS_STOP;
            break;
        case 2:
            CP0_CHECK(ctx->sc);
            gen_helper_mtc0_segctl0(cpu_env, arg);
            rn = "SegCtl0";
            break;
        case 3:
            CP0_CHECK(ctx->sc);
            gen_helper_mtc0_segctl1(cpu_env, arg);
            rn = "SegCtl1";
            break;
        case 4:
            CP0_CHECK(ctx->sc);
            gen_helper_mtc0_segctl2(cpu_env, arg);
            rn = "SegCtl2";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_helper_mtc0_wired(cpu_env, arg);
            rn = "Wired";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf0(cpu_env, arg);
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf1(cpu_env, arg);
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf2(cpu_env, arg);
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf3(cpu_env, arg);
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf4(cpu_env, arg);
            rn = "SRSConf4";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_hwrena(cpu_env, arg);
            ctx->bstate = BS_STOP;
            rn = "HWREna";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "BadVAddr";
            break;
        case 1:
            /* ignored */
            rn = "BadInstr";
            break;
        case 2:
            /* ignored */
            rn = "BadInstrP";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_helper_mtc0_count(cpu_env, arg);
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entryhi(cpu_env, arg);
            rn = "EntryHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_helper_mtc0_compare(cpu_env, arg);
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            save_cpu_state(ctx, 1);
            gen_helper_mtc0_status(cpu_env, arg);
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Status";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_intctl(cpu_env, arg);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "IntCtl";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsctl(cpu_env, arg);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mtc0_store32(arg, offsetof(CPUMIPSState, CP0_SRSMap));
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSMap";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            save_cpu_state(ctx, 1);
            gen_helper_mtc0_cause(cpu_env, arg);
            /* Stop translation as we may have triggered an interrupt. BS_STOP
             * isn't sufficient, we need to ensure we break out of translated
             * code to check for pending interrupts.  */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Cause";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EPC));
            rn = "EPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "PRid";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_ebase(cpu_env, arg);
            rn = "EBase";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_helper_mtc0_config0(cpu_env, arg);
            rn = "Config";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 1:
            /* ignored, read only */
            rn = "Config1";
            break;
        case 2:
            gen_helper_mtc0_config2(cpu_env, arg);
            rn = "Config2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 3:
            gen_helper_mtc0_config3(cpu_env, arg);
            rn = "Config3";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 4:
            gen_helper_mtc0_config4(cpu_env, arg);
            rn = "Config4";
            ctx->bstate = BS_STOP;
            break;
        case 5:
            gen_helper_mtc0_config5(cpu_env, arg);
            rn = "Config5";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
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
            goto cp0_unimplemented;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_helper_mtc0_lladdr(cpu_env, arg);
            rn = "LLAddr";
            break;
        case 1:
            CP0_CHECK(ctx->mrp);
            gen_helper_mtc0_maar(cpu_env, arg);
            rn = "MAAR";
            break;
        case 2:
            CP0_CHECK(ctx->mrp);
            gen_helper_mtc0_maari(cpu_env, arg);
            rn = "MAARI";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_0e1i(mtc0_watchlo, arg, sel);
            rn = "WatchLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_helper_0e1i(mtc0_watchhi, arg, sel);
            rn = "WatchHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
#if defined(TARGET_MIPS64)
            check_insn(ctx, ISA_MIPS3);
            gen_helper_mtc0_xcontext(cpu_env, arg);
            rn = "XContext";
            break;
#endif
        default:
            goto cp0_unimplemented;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        CP0_CHECK(!(ctx->insn_flags & ISA_MIPS32R6));
        switch (sel) {
        case 0:
            gen_helper_mtc0_framemask(cpu_env, arg);
            rn = "Framemask";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 22:
        /* ignored */
        rn = "Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mtc0_debug(cpu_env, arg); /* EJTAG support */
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Debug";
            break;
        case 1:
//            gen_helper_mtc0_tracecontrol(cpu_env, arg); /* PDtrace support */
            rn = "TraceControl";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            goto cp0_unimplemented;
        case 2:
//            gen_helper_mtc0_tracecontrol2(cpu_env, arg); /* PDtrace support */
            rn = "TraceControl2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            goto cp0_unimplemented;
        case 3:
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
//            gen_helper_mtc0_usertracedata(cpu_env, arg); /* PDtrace support */
            rn = "UserTraceData";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            goto cp0_unimplemented;
        case 4:
//            gen_helper_mtc0_tracebpc(cpu_env, arg); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceBPC";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_DEPC));
            rn = "DEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_helper_mtc0_performance0(cpu_env, arg);
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_mtc0_performance1(arg);
            rn = "Performance1";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_mtc0_performance2(arg);
            rn = "Performance2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_mtc0_performance3(arg);
            rn = "Performance3";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_mtc0_performance4(arg);
            rn = "Performance4";
            goto cp0_unimplemented;
        case 5:
//            gen_helper_mtc0_performance5(arg);
            rn = "Performance5";
            goto cp0_unimplemented;
        case 6:
//            gen_helper_mtc0_performance6(arg);
            rn = "Performance6";
            goto cp0_unimplemented;
        case 7:
//            gen_helper_mtc0_performance7(arg);
            rn = "Performance7";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
       break;
    case 26:
        switch (sel) {
        case 0:
            gen_helper_mtc0_errctl(cpu_env, arg);
            ctx->bstate = BS_STOP;
            rn = "ErrCtl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            /* ignored */
            rn = "CacheErr";
            break;
        default:
            goto cp0_unimplemented;
        }
       break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taglo(cpu_env, arg);
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datalo(cpu_env, arg);
            rn = "DataLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taghi(cpu_env, arg);
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datahi(cpu_env, arg);
            rn = "DataHi";
            break;
        default:
            rn = "invalid sel";
            goto cp0_unimplemented;
        }
       break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_ErrorEPC));
            rn = "ErrorEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mtc0_store32(arg, offsetof(CPUMIPSState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        case 2 ... 7:
            CP0_CHECK(ctx->kscrexist & (1 << sel));
            tcg_gen_st_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, CP0_KScratch[sel-2]));
            rn = "KScratch";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    default:
       goto cp0_unimplemented;
    }
    trace_mips_translate_c0("mtc0", rn, reg, sel);

    /* For simplicity assume that all writes can cause interrupts.  */
    if (ctx->tb->cflags & CF_USE_ICOUNT) {
        gen_io_end();
        /* BS_STOP isn't sufficient, we need to ensure we break out of
         * translated code to check for pending interrupts.  */
        gen_save_pc(ctx->pc + 4);
        ctx->bstate = BS_EXCP;
    }
    return;

cp0_unimplemented:
    qemu_log_mask(LOG_UNIMP, "mtc0 %s (reg %d sel %d)\n", rn, reg, sel);
}

#if defined(TARGET_MIPS64)
static void gen_dmfc0(DisasContext *ctx, TCGv arg, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(ctx, ISA_MIPS64);

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Index));
            rn = "Index";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_mvpcontrol(arg, cpu_env);
            rn = "MVPControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_mvpconf0(arg, cpu_env);
            rn = "MVPConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_mvpconf1(arg, cpu_env);
            rn = "MVPConf1";
            break;
        case 4:
            CP0_CHECK(ctx->vp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPControl));
            rn = "VPControl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            CP0_CHECK(!(ctx->insn_flags & ISA_MIPS32R6));
            gen_helper_mfc0_random(arg, cpu_env);
            rn = "Random";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEControl));
            rn = "VPEControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEConf0));
            rn = "VPEConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEConf1));
            rn = "VPEConf1";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_YQMask));
            rn = "YQMask";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_VPEOpt));
            rn = "VPEOpt";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EntryLo0));
            rn = "EntryLo0";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcstatus(arg, cpu_env);
            rn = "TCStatus";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mfc0_tcbind(arg, cpu_env);
            rn = "TCBind";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_dmfc0_tcrestart(arg, cpu_env);
            rn = "TCRestart";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_dmfc0_tchalt(arg, cpu_env);
            rn = "TCHalt";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_dmfc0_tccontext(arg, cpu_env);
            rn = "TCContext";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_dmfc0_tcschedule(arg, cpu_env);
            rn = "TCSchedule";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_dmfc0_tcschefback(arg, cpu_env);
            rn = "TCScheFBack";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EntryLo1));
            rn = "EntryLo1";
            break;
        case 1:
            CP0_CHECK(ctx->vp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_GlobalNumber));
            rn = "GlobalNumber";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_Context));
            rn = "Context";
            break;
        case 1:
//            gen_helper_dmfc0_contextconfig(arg); /* SmartMIPS ASE */
            rn = "ContextConfig";
            goto cp0_unimplemented;
        case 2:
            CP0_CHECK(ctx->ulri);
            tcg_gen_ld_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, active_tc.CP0_UserLocal));
            rn = "UserLocal";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_PageMask));
            rn = "PageMask";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_PageGrain));
            rn = "PageGrain";
            break;
        case 2:
            CP0_CHECK(ctx->sc);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_SegCtl0));
            rn = "SegCtl0";
            break;
        case 3:
            CP0_CHECK(ctx->sc);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_SegCtl1));
            rn = "SegCtl1";
            break;
        case 4:
            CP0_CHECK(ctx->sc);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_SegCtl2));
            rn = "SegCtl2";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Wired));
            rn = "Wired";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf0));
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf1));
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf2));
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf3));
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSConf4));
            rn = "SRSConf4";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_HWREna));
            rn = "HWREna";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_BadVAddr));
            rn = "BadVAddr";
            break;
        case 1:
            CP0_CHECK(ctx->bi);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_BadInstr));
            rn = "BadInstr";
            break;
        case 2:
            CP0_CHECK(ctx->bp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_BadInstrP));
            rn = "BadInstrP";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 9:
        switch (sel) {
        case 0:
            /* Mark as an IO operation because we read the time.  */
            if (ctx->tb->cflags & CF_USE_ICOUNT) {
                gen_io_start();
            }
            gen_helper_mfc0_count(arg, cpu_env);
            if (ctx->tb->cflags & CF_USE_ICOUNT) {
                gen_io_end();
            }
            /* Break the TB to be able to take timer interrupts immediately
               after reading count. BS_STOP isn't sufficient, we need to ensure
               we break completely out of translated code.  */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        break;
    case 10:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EntryHi));
            rn = "EntryHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Compare));
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        break;
    case 12:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Status));
            rn = "Status";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_IntCtl));
            rn = "IntCtl";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSCtl));
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_SRSMap));
            rn = "SRSMap";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Cause));
            rn = "Cause";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EPC));
            rn = "EPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_PRid));
            rn = "PRid";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EBase));
            rn = "EBase";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            CP0_CHECK(ctx->cmgcr);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_CMGCRBase));
            rn = "CMGCRBase";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config0));
            rn = "Config";
            break;
        case 1:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config1));
            rn = "Config1";
            break;
        case 2:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config2));
            rn = "Config2";
            break;
        case 3:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config3));
            rn = "Config3";
            break;
        case 4:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config4));
            rn = "Config4";
            break;
        case 5:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config5));
            rn = "Config5";
            break;
       /* 6,7 are implementation dependent */
        case 6:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config6));
            rn = "Config6";
            break;
        case 7:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Config7));
            rn = "Config7";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_helper_dmfc0_lladdr(arg, cpu_env);
            rn = "LLAddr";
            break;
        case 1:
            CP0_CHECK(ctx->mrp);
            gen_helper_dmfc0_maar(arg, cpu_env);
            rn = "MAAR";
            break;
        case 2:
            CP0_CHECK(ctx->mrp);
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_MAARI));
            rn = "MAARI";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1e0i(dmfc0_watchlo, arg, sel);
            rn = "WatchLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_helper_1e0i(mfc0_watchhi, arg, sel);
            rn = "WatchHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
            check_insn(ctx, ISA_MIPS3);
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_XContext));
            rn = "XContext";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        CP0_CHECK(!(ctx->insn_flags & ISA_MIPS32R6));
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Framemask));
            rn = "Framemask";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 22:
        tcg_gen_movi_tl(arg, 0); /* unimplemented */
        rn = "'Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mfc0_debug(arg, cpu_env); /* EJTAG support */
            rn = "Debug";
            break;
        case 1:
//            gen_helper_dmfc0_tracecontrol(arg, cpu_env); /* PDtrace support */
            rn = "TraceControl";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_dmfc0_tracecontrol2(arg, cpu_env); /* PDtrace support */
            rn = "TraceControl2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_dmfc0_usertracedata(arg, cpu_env); /* PDtrace support */
            rn = "UserTraceData";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_dmfc0_tracebpc(arg, cpu_env); /* PDtrace support */
            rn = "TraceBPC";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_DEPC));
            rn = "DEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_Performance0));
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_dmfc0_performance1(arg);
            rn = "Performance1";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_dmfc0_performance2(arg);
            rn = "Performance2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_dmfc0_performance3(arg);
            rn = "Performance3";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_dmfc0_performance4(arg);
            rn = "Performance4";
            goto cp0_unimplemented;
        case 5:
//            gen_helper_dmfc0_performance5(arg);
            rn = "Performance5";
            goto cp0_unimplemented;
        case 6:
//            gen_helper_dmfc0_performance6(arg);
            rn = "Performance6";
            goto cp0_unimplemented;
        case 7:
//            gen_helper_dmfc0_performance7(arg);
            rn = "Performance7";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 26:
        switch (sel) {
        case 0:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_ErrCtl));
            rn = "ErrCtl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 27:
        switch (sel) {
        /* ignored */
        case 0 ... 3:
            tcg_gen_movi_tl(arg, 0); /* unimplemented */
            rn = "CacheErr";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_TagLo));
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_DataLo));
            rn = "DataLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_TagHi));
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_DataHi));
            rn = "DataHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_ld_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_ErrorEPC));
            rn = "ErrorEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mfc0_load32(arg, offsetof(CPUMIPSState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        case 2 ... 7:
            CP0_CHECK(ctx->kscrexist & (1 << sel));
            tcg_gen_ld_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, CP0_KScratch[sel-2]));
            rn = "KScratch";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    default:
        goto cp0_unimplemented;
    }
    trace_mips_translate_c0("dmfc0", rn, reg, sel);
    return;

cp0_unimplemented:
    qemu_log_mask(LOG_UNIMP, "dmfc0 %s (reg %d sel %d)\n", rn, reg, sel);
    gen_mfc0_unimplemented(ctx, arg);
}

static void gen_dmtc0(DisasContext *ctx, TCGv arg, int reg, int sel)
{
    const char *rn = "invalid";

    if (sel != 0)
        check_insn(ctx, ISA_MIPS64);

    if (ctx->tb->cflags & CF_USE_ICOUNT) {
        gen_io_start();
    }

    switch (reg) {
    case 0:
        switch (sel) {
        case 0:
            gen_helper_mtc0_index(cpu_env, arg);
            rn = "Index";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_mvpcontrol(cpu_env, arg);
            rn = "MVPControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            /* ignored */
            rn = "MVPConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            /* ignored */
            rn = "MVPConf1";
            break;
        case 4:
            CP0_CHECK(ctx->vp);
            /* ignored */
            rn = "VPControl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 1:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "Random";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpecontrol(cpu_env, arg);
            rn = "VPEControl";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpeconf0(cpu_env, arg);
            rn = "VPEConf0";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpeconf1(cpu_env, arg);
            rn = "VPEConf1";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_yqmask(cpu_env, arg);
            rn = "YQMask";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_VPESchedule));
            rn = "VPESchedule";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_VPEScheFBack));
            rn = "VPEScheFBack";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_vpeopt(cpu_env, arg);
            rn = "VPEOpt";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 2:
        switch (sel) {
        case 0:
            gen_helper_dmtc0_entrylo0(cpu_env, arg);
            rn = "EntryLo0";
            break;
        case 1:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcstatus(cpu_env, arg);
            rn = "TCStatus";
            break;
        case 2:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcbind(cpu_env, arg);
            rn = "TCBind";
            break;
        case 3:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcrestart(cpu_env, arg);
            rn = "TCRestart";
            break;
        case 4:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tchalt(cpu_env, arg);
            rn = "TCHalt";
            break;
        case 5:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tccontext(cpu_env, arg);
            rn = "TCContext";
            break;
        case 6:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcschedule(cpu_env, arg);
            rn = "TCSchedule";
            break;
        case 7:
            CP0_CHECK(ctx->insn_flags & ASE_MT);
            gen_helper_mtc0_tcschefback(cpu_env, arg);
            rn = "TCScheFBack";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 3:
        switch (sel) {
        case 0:
            gen_helper_dmtc0_entrylo1(cpu_env, arg);
            rn = "EntryLo1";
            break;
        case 1:
            CP0_CHECK(ctx->vp);
            /* ignored */
            rn = "GlobalNumber";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 4:
        switch (sel) {
        case 0:
            gen_helper_mtc0_context(cpu_env, arg);
            rn = "Context";
            break;
        case 1:
//           gen_helper_mtc0_contextconfig(cpu_env, arg); /* SmartMIPS ASE */
            rn = "ContextConfig";
            goto cp0_unimplemented;
        case 2:
            CP0_CHECK(ctx->ulri);
            tcg_gen_st_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, active_tc.CP0_UserLocal));
            rn = "UserLocal";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 5:
        switch (sel) {
        case 0:
            gen_helper_mtc0_pagemask(cpu_env, arg);
            rn = "PageMask";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_pagegrain(cpu_env, arg);
            rn = "PageGrain";
            break;
        case 2:
            CP0_CHECK(ctx->sc);
            gen_helper_mtc0_segctl0(cpu_env, arg);
            rn = "SegCtl0";
            break;
        case 3:
            CP0_CHECK(ctx->sc);
            gen_helper_mtc0_segctl1(cpu_env, arg);
            rn = "SegCtl1";
            break;
        case 4:
            CP0_CHECK(ctx->sc);
            gen_helper_mtc0_segctl2(cpu_env, arg);
            rn = "SegCtl2";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 6:
        switch (sel) {
        case 0:
            gen_helper_mtc0_wired(cpu_env, arg);
            rn = "Wired";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf0(cpu_env, arg);
            rn = "SRSConf0";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf1(cpu_env, arg);
            rn = "SRSConf1";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf2(cpu_env, arg);
            rn = "SRSConf2";
            break;
        case 4:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf3(cpu_env, arg);
            rn = "SRSConf3";
            break;
        case 5:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsconf4(cpu_env, arg);
            rn = "SRSConf4";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 7:
        switch (sel) {
        case 0:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_hwrena(cpu_env, arg);
            ctx->bstate = BS_STOP;
            rn = "HWREna";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 8:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "BadVAddr";
            break;
        case 1:
            /* ignored */
            rn = "BadInstr";
            break;
        case 2:
            /* ignored */
            rn = "BadInstrP";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 9:
        switch (sel) {
        case 0:
            gen_helper_mtc0_count(cpu_env, arg);
            rn = "Count";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 10:
        switch (sel) {
        case 0:
            gen_helper_mtc0_entryhi(cpu_env, arg);
            rn = "EntryHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 11:
        switch (sel) {
        case 0:
            gen_helper_mtc0_compare(cpu_env, arg);
            rn = "Compare";
            break;
        /* 6,7 are implementation dependent */
        default:
            goto cp0_unimplemented;
        }
        /* Stop translation as we may have switched the execution mode */
        ctx->bstate = BS_STOP;
        break;
    case 12:
        switch (sel) {
        case 0:
            save_cpu_state(ctx, 1);
            gen_helper_mtc0_status(cpu_env, arg);
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Status";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_intctl(cpu_env, arg);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "IntCtl";
            break;
        case 2:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_srsctl(cpu_env, arg);
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSCtl";
            break;
        case 3:
            check_insn(ctx, ISA_MIPS32R2);
            gen_mtc0_store32(arg, offsetof(CPUMIPSState, CP0_SRSMap));
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "SRSMap";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 13:
        switch (sel) {
        case 0:
            save_cpu_state(ctx, 1);
            gen_helper_mtc0_cause(cpu_env, arg);
            /* Stop translation as we may have triggered an intetrupt. BS_STOP
             * isn't sufficient, we need to ensure we break out of translated
             * code to check for pending interrupts.  */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Cause";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 14:
        switch (sel) {
        case 0:
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_EPC));
            rn = "EPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 15:
        switch (sel) {
        case 0:
            /* ignored */
            rn = "PRid";
            break;
        case 1:
            check_insn(ctx, ISA_MIPS32R2);
            gen_helper_mtc0_ebase(cpu_env, arg);
            rn = "EBase";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 16:
        switch (sel) {
        case 0:
            gen_helper_mtc0_config0(cpu_env, arg);
            rn = "Config";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 1:
            /* ignored, read only */
            rn = "Config1";
            break;
        case 2:
            gen_helper_mtc0_config2(cpu_env, arg);
            rn = "Config2";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 3:
            gen_helper_mtc0_config3(cpu_env, arg);
            rn = "Config3";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        case 4:
            /* currently ignored */
            rn = "Config4";
            break;
        case 5:
            gen_helper_mtc0_config5(cpu_env, arg);
            rn = "Config5";
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            break;
        /* 6,7 are implementation dependent */
        default:
            rn = "Invalid config selector";
            goto cp0_unimplemented;
        }
        break;
    case 17:
        switch (sel) {
        case 0:
            gen_helper_mtc0_lladdr(cpu_env, arg);
            rn = "LLAddr";
            break;
        case 1:
            CP0_CHECK(ctx->mrp);
            gen_helper_mtc0_maar(cpu_env, arg);
            rn = "MAAR";
            break;
        case 2:
            CP0_CHECK(ctx->mrp);
            gen_helper_mtc0_maari(cpu_env, arg);
            rn = "MAARI";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 18:
        switch (sel) {
        case 0 ... 7:
            gen_helper_0e1i(mtc0_watchlo, arg, sel);
            rn = "WatchLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 19:
        switch (sel) {
        case 0 ... 7:
            gen_helper_0e1i(mtc0_watchhi, arg, sel);
            rn = "WatchHi";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 20:
        switch (sel) {
        case 0:
            check_insn(ctx, ISA_MIPS3);
            gen_helper_mtc0_xcontext(cpu_env, arg);
            rn = "XContext";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 21:
       /* Officially reserved, but sel 0 is used for R1x000 framemask */
        CP0_CHECK(!(ctx->insn_flags & ISA_MIPS32R6));
        switch (sel) {
        case 0:
            gen_helper_mtc0_framemask(cpu_env, arg);
            rn = "Framemask";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 22:
        /* ignored */
        rn = "Diagnostic"; /* implementation dependent */
        break;
    case 23:
        switch (sel) {
        case 0:
            gen_helper_mtc0_debug(cpu_env, arg); /* EJTAG support */
            /* BS_STOP isn't good enough here, hflags may have changed. */
            gen_save_pc(ctx->pc + 4);
            ctx->bstate = BS_EXCP;
            rn = "Debug";
            break;
        case 1:
//            gen_helper_mtc0_tracecontrol(cpu_env, arg); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceControl";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_mtc0_tracecontrol2(cpu_env, arg); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceControl2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_mtc0_usertracedata(cpu_env, arg); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "UserTraceData";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_mtc0_tracebpc(cpu_env, arg); /* PDtrace support */
            /* Stop translation as we may have switched the execution mode */
            ctx->bstate = BS_STOP;
            rn = "TraceBPC";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 24:
        switch (sel) {
        case 0:
            /* EJTAG support */
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_DEPC));
            rn = "DEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 25:
        switch (sel) {
        case 0:
            gen_helper_mtc0_performance0(cpu_env, arg);
            rn = "Performance0";
            break;
        case 1:
//            gen_helper_mtc0_performance1(cpu_env, arg);
            rn = "Performance1";
            goto cp0_unimplemented;
        case 2:
//            gen_helper_mtc0_performance2(cpu_env, arg);
            rn = "Performance2";
            goto cp0_unimplemented;
        case 3:
//            gen_helper_mtc0_performance3(cpu_env, arg);
            rn = "Performance3";
            goto cp0_unimplemented;
        case 4:
//            gen_helper_mtc0_performance4(cpu_env, arg);
            rn = "Performance4";
            goto cp0_unimplemented;
        case 5:
//            gen_helper_mtc0_performance5(cpu_env, arg);
            rn = "Performance5";
            goto cp0_unimplemented;
        case 6:
//            gen_helper_mtc0_performance6(cpu_env, arg);
            rn = "Performance6";
            goto cp0_unimplemented;
        case 7:
//            gen_helper_mtc0_performance7(cpu_env, arg);
            rn = "Performance7";
            goto cp0_unimplemented;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 26:
        switch (sel) {
        case 0:
            gen_helper_mtc0_errctl(cpu_env, arg);
            ctx->bstate = BS_STOP;
            rn = "ErrCtl";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 27:
        switch (sel) {
        case 0 ... 3:
            /* ignored */
            rn = "CacheErr";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 28:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taglo(cpu_env, arg);
            rn = "TagLo";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datalo(cpu_env, arg);
            rn = "DataLo";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 29:
        switch (sel) {
        case 0:
        case 2:
        case 4:
        case 6:
            gen_helper_mtc0_taghi(cpu_env, arg);
            rn = "TagHi";
            break;
        case 1:
        case 3:
        case 5:
        case 7:
            gen_helper_mtc0_datahi(cpu_env, arg);
            rn = "DataHi";
            break;
        default:
            rn = "invalid sel";
            goto cp0_unimplemented;
        }
        break;
    case 30:
        switch (sel) {
        case 0:
            tcg_gen_st_tl(arg, cpu_env, offsetof(CPUMIPSState, CP0_ErrorEPC));
            rn = "ErrorEPC";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    case 31:
        switch (sel) {
        case 0:
            /* EJTAG support */
            gen_mtc0_store32(arg, offsetof(CPUMIPSState, CP0_DESAVE));
            rn = "DESAVE";
            break;
        case 2 ... 7:
            CP0_CHECK(ctx->kscrexist & (1 << sel));
            tcg_gen_st_tl(arg, cpu_env,
                          offsetof(CPUMIPSState, CP0_KScratch[sel-2]));
            rn = "KScratch";
            break;
        default:
            goto cp0_unimplemented;
        }
        break;
    default:
        goto cp0_unimplemented;
    }
    trace_mips_translate_c0("dmtc0", rn, reg, sel);

    /* For simplicity assume that all writes can cause interrupts.  */
    if (ctx->tb->cflags & CF_USE_ICOUNT) {
        gen_io_end();
        /* BS_STOP isn't sufficient, we need to ensure we break out of
         * translated code to check for pending interrupts.  */
        gen_save_pc(ctx->pc + 4);
        ctx->bstate = BS_EXCP;
    }
    return;

cp0_unimplemented:
    qemu_log_mask(LOG_UNIMP, "dmtc0 %s (reg %d sel %d)\n", rn, reg, sel);
}
#endif /* TARGET_MIPS64 */

static void gen_mftr(CPUMIPSState *env, DisasContext *ctx, int rt, int rd,
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
        case 1:
            switch (sel) {
            case 1:
                gen_helper_mftc0_vpecontrol(t0, cpu_env);
                break;
            case 2:
                gen_helper_mftc0_vpeconf0(t0, cpu_env);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 2:
            switch (sel) {
            case 1:
                gen_helper_mftc0_tcstatus(t0, cpu_env);
                break;
            case 2:
                gen_helper_mftc0_tcbind(t0, cpu_env);
                break;
            case 3:
                gen_helper_mftc0_tcrestart(t0, cpu_env);
                break;
            case 4:
                gen_helper_mftc0_tchalt(t0, cpu_env);
                break;
            case 5:
                gen_helper_mftc0_tccontext(t0, cpu_env);
                break;
            case 6:
                gen_helper_mftc0_tcschedule(t0, cpu_env);
                break;
            case 7:
                gen_helper_mftc0_tcschefback(t0, cpu_env);
                break;
            default:
                gen_mfc0(ctx, t0, rt, sel);
                break;
            }
            break;
        case 10:
            switch (sel) {
            case 0:
                gen_helper_mftc0_entryhi(t0, cpu_env);
                break;
            default:
                gen_mfc0(ctx, t0, rt, sel);
                break;
            }
        case 12:
            switch (sel) {
            case 0:
                gen_helper_mftc0_status(t0, cpu_env);
                break;
            default:
                gen_mfc0(ctx, t0, rt, sel);
                break;
            }
        case 13:
            switch (sel) {
            case 0:
                gen_helper_mftc0_cause(t0, cpu_env);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 14:
            switch (sel) {
            case 0:
                gen_helper_mftc0_epc(t0, cpu_env);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 15:
            switch (sel) {
            case 1:
                gen_helper_mftc0_ebase(t0, cpu_env);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 16:
            switch (sel) {
            case 0 ... 7:
                gen_helper_mftc0_configx(t0, cpu_env, tcg_const_tl(sel));
                break;
            default:
                goto die;
                break;
            }
            break;
        case 23:
            switch (sel) {
            case 0:
                gen_helper_mftc0_debug(t0, cpu_env);
                break;
            default:
                gen_mfc0(ctx, t0, rt, sel);
                break;
            }
            break;
        default:
            gen_mfc0(ctx, t0, rt, sel);
        }
    } else switch (sel) {
    /* GPR registers. */
    case 0:
        gen_helper_1e0i(mftgpr, t0, rt);
        break;
    /* Auxiliary CPU registers */
    case 1:
        switch (rt) {
        case 0:
            gen_helper_1e0i(mftlo, t0, 0);
            break;
        case 1:
            gen_helper_1e0i(mfthi, t0, 0);
            break;
        case 2:
            gen_helper_1e0i(mftacx, t0, 0);
            break;
        case 4:
            gen_helper_1e0i(mftlo, t0, 1);
            break;
        case 5:
            gen_helper_1e0i(mfthi, t0, 1);
            break;
        case 6:
            gen_helper_1e0i(mftacx, t0, 1);
            break;
        case 8:
            gen_helper_1e0i(mftlo, t0, 2);
            break;
        case 9:
            gen_helper_1e0i(mfthi, t0, 2);
            break;
        case 10:
            gen_helper_1e0i(mftacx, t0, 2);
            break;
        case 12:
            gen_helper_1e0i(mftlo, t0, 3);
            break;
        case 13:
            gen_helper_1e0i(mfthi, t0, 3);
            break;
        case 14:
            gen_helper_1e0i(mftacx, t0, 3);
            break;
        case 16:
            gen_helper_mftdsp(t0, cpu_env);
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

            gen_load_fpr32(ctx, fp0, rt);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
        } else {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32h(ctx, fp0, rt);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
        }
        break;
    case 3:
        /* XXX: For now we support only a single FPU context. */
        gen_helper_1e0i(cfc1, t0, rt);
        break;
    /* COP2: Not implemented. */
    case 4:
    case 5:
        /* fall through */
    default:
        goto die;
    }
    trace_mips_translate_tr("mftr", rt, u, sel, h);
    gen_store_gpr(t0, rd);
    tcg_temp_free(t0);
    return;

die:
    tcg_temp_free(t0);
    LOG_DISAS("mftr (reg %d u %d sel %d h %d)\n", rt, u, sel, h);
    generate_exception_end(ctx, EXCP_RI);
}

static void gen_mttr(CPUMIPSState *env, DisasContext *ctx, int rd, int rt,
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
        case 1:
            switch (sel) {
            case 1:
                gen_helper_mttc0_vpecontrol(cpu_env, t0);
                break;
            case 2:
                gen_helper_mttc0_vpeconf0(cpu_env, t0);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 2:
            switch (sel) {
            case 1:
                gen_helper_mttc0_tcstatus(cpu_env, t0);
                break;
            case 2:
                gen_helper_mttc0_tcbind(cpu_env, t0);
                break;
            case 3:
                gen_helper_mttc0_tcrestart(cpu_env, t0);
                break;
            case 4:
                gen_helper_mttc0_tchalt(cpu_env, t0);
                break;
            case 5:
                gen_helper_mttc0_tccontext(cpu_env, t0);
                break;
            case 6:
                gen_helper_mttc0_tcschedule(cpu_env, t0);
                break;
            case 7:
                gen_helper_mttc0_tcschefback(cpu_env, t0);
                break;
            default:
                gen_mtc0(ctx, t0, rd, sel);
                break;
            }
            break;
        case 10:
            switch (sel) {
            case 0:
                gen_helper_mttc0_entryhi(cpu_env, t0);
                break;
            default:
                gen_mtc0(ctx, t0, rd, sel);
                break;
            }
        case 12:
            switch (sel) {
            case 0:
                gen_helper_mttc0_status(cpu_env, t0);
                break;
            default:
                gen_mtc0(ctx, t0, rd, sel);
                break;
            }
        case 13:
            switch (sel) {
            case 0:
                gen_helper_mttc0_cause(cpu_env, t0);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 15:
            switch (sel) {
            case 1:
                gen_helper_mttc0_ebase(cpu_env, t0);
                break;
            default:
                goto die;
                break;
            }
            break;
        case 23:
            switch (sel) {
            case 0:
                gen_helper_mttc0_debug(cpu_env, t0);
                break;
            default:
                gen_mtc0(ctx, t0, rd, sel);
                break;
            }
            break;
        default:
            gen_mtc0(ctx, t0, rd, sel);
        }
    } else switch (sel) {
    /* GPR registers. */
    case 0:
        gen_helper_0e1i(mttgpr, t0, rd);
        break;
    /* Auxiliary CPU registers */
    case 1:
        switch (rd) {
        case 0:
            gen_helper_0e1i(mttlo, t0, 0);
            break;
        case 1:
            gen_helper_0e1i(mtthi, t0, 0);
            break;
        case 2:
            gen_helper_0e1i(mttacx, t0, 0);
            break;
        case 4:
            gen_helper_0e1i(mttlo, t0, 1);
            break;
        case 5:
            gen_helper_0e1i(mtthi, t0, 1);
            break;
        case 6:
            gen_helper_0e1i(mttacx, t0, 1);
            break;
        case 8:
            gen_helper_0e1i(mttlo, t0, 2);
            break;
        case 9:
            gen_helper_0e1i(mtthi, t0, 2);
            break;
        case 10:
            gen_helper_0e1i(mttacx, t0, 2);
            break;
        case 12:
            gen_helper_0e1i(mttlo, t0, 3);
            break;
        case 13:
            gen_helper_0e1i(mtthi, t0, 3);
            break;
        case 14:
            gen_helper_0e1i(mttacx, t0, 3);
            break;
        case 16:
            gen_helper_mttdsp(cpu_env, t0);
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
            gen_store_fpr32(ctx, fp0, rd);
            tcg_temp_free_i32(fp0);
        } else {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32h(ctx, fp0, rd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case 3:
        /* XXX: For now we support only a single FPU context. */
        {
            TCGv_i32 fs_tmp = tcg_const_i32(rd);

            gen_helper_0e2i(ctc1, t0, fs_tmp, rt);
            tcg_temp_free_i32(fs_tmp);
        }
        /* Stop translation as we may have changed hflags */
        ctx->bstate = BS_STOP;
        break;
    /* COP2: Not implemented. */
    case 4:
    case 5:
        /* fall through */
    default:
        goto die;
    }
    trace_mips_translate_tr("mttr", rd, u, sel, h);
    tcg_temp_free(t0);
    return;

die:
    tcg_temp_free(t0);
    LOG_DISAS("mttr (reg %d u %d sel %d h %d)\n", rd, u, sel, h);
    generate_exception_end(ctx, EXCP_RI);
}

static void gen_cp0 (CPUMIPSState *env, DisasContext *ctx, uint32_t opc, int rt, int rd)
{
    const char *opn = "ldst";

    check_cp0_enabled(ctx);
    switch (opc) {
    case OPC_MFC0:
        if (rt == 0) {
            /* Treat as NOP. */
            return;
        }
        gen_mfc0(ctx, cpu_gpr[rt], rd, ctx->opcode & 0x7);
        opn = "mfc0";
        break;
    case OPC_MTC0:
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_mtc0(ctx, t0, rd, ctx->opcode & 0x7);
            tcg_temp_free(t0);
        }
        opn = "mtc0";
        break;
#if defined(TARGET_MIPS64)
    case OPC_DMFC0:
        check_insn(ctx, ISA_MIPS3);
        if (rt == 0) {
            /* Treat as NOP. */
            return;
        }
        gen_dmfc0(ctx, cpu_gpr[rt], rd, ctx->opcode & 0x7);
        opn = "dmfc0";
        break;
    case OPC_DMTC0:
        check_insn(ctx, ISA_MIPS3);
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_dmtc0(ctx, t0, rd, ctx->opcode & 0x7);
            tcg_temp_free(t0);
        }
        opn = "dmtc0";
        break;
#endif
    case OPC_MFHC0:
        check_mvh(ctx);
        if (rt == 0) {
            /* Treat as NOP. */
            return;
        }
        gen_mfhc0(ctx, cpu_gpr[rt], rd, ctx->opcode & 0x7);
        opn = "mfhc0";
        break;
    case OPC_MTHC0:
        check_mvh(ctx);
        {
            TCGv t0 = tcg_temp_new();
            gen_load_gpr(t0, rt);
            gen_mthc0(ctx, t0, rd, ctx->opcode & 0x7);
            tcg_temp_free(t0);
        }
        opn = "mthc0";
        break;
    case OPC_MFTR:
        check_insn(ctx, ASE_MT);
        if (rd == 0) {
            /* Treat as NOP. */
            return;
        }
        gen_mftr(env, ctx, rt, rd, (ctx->opcode >> 5) & 1,
                 ctx->opcode & 0x7, (ctx->opcode >> 4) & 1);
        opn = "mftr";
        break;
    case OPC_MTTR:
        check_insn(ctx, ASE_MT);
        gen_mttr(env, ctx, rd, rt, (ctx->opcode >> 5) & 1,
                 ctx->opcode & 0x7, (ctx->opcode >> 4) & 1);
        opn = "mttr";
        break;
    case OPC_TLBWI:
        opn = "tlbwi";
        if (!env->tlb->helper_tlbwi)
            goto die;
        gen_helper_tlbwi(cpu_env);
        break;
    case OPC_TLBINV:
        opn = "tlbinv";
        if (ctx->ie >= 2) {
            if (!env->tlb->helper_tlbinv) {
                goto die;
            }
            gen_helper_tlbinv(cpu_env);
        } /* treat as nop if TLBINV not supported */
        break;
    case OPC_TLBINVF:
        opn = "tlbinvf";
        if (ctx->ie >= 2) {
            if (!env->tlb->helper_tlbinvf) {
                goto die;
            }
            gen_helper_tlbinvf(cpu_env);
        } /* treat as nop if TLBINV not supported */
        break;
    case OPC_TLBWR:
        opn = "tlbwr";
        if (!env->tlb->helper_tlbwr)
            goto die;
        gen_helper_tlbwr(cpu_env);
        break;
    case OPC_TLBP:
        opn = "tlbp";
        if (!env->tlb->helper_tlbp)
            goto die;
        gen_helper_tlbp(cpu_env);
        break;
    case OPC_TLBR:
        opn = "tlbr";
        if (!env->tlb->helper_tlbr)
            goto die;
        gen_helper_tlbr(cpu_env);
        break;
    case OPC_ERET: /* OPC_ERETNC */
        if ((ctx->insn_flags & ISA_MIPS32R6) &&
            (ctx->hflags & MIPS_HFLAG_BMASK)) {
            goto die;
        } else {
            int bit_shift = (ctx->hflags & MIPS_HFLAG_M16) ? 16 : 6;
            if (ctx->opcode & (1 << bit_shift)) {
                /* OPC_ERETNC */
                opn = "eretnc";
                check_insn(ctx, ISA_MIPS32R5);
                gen_helper_eretnc(cpu_env);
            } else {
                /* OPC_ERET */
                opn = "eret";
                check_insn(ctx, ISA_MIPS2);
                gen_helper_eret(cpu_env);
            }
            ctx->bstate = BS_EXCP;
        }
        break;
    case OPC_DERET:
        opn = "deret";
        check_insn(ctx, ISA_MIPS32);
        if ((ctx->insn_flags & ISA_MIPS32R6) &&
            (ctx->hflags & MIPS_HFLAG_BMASK)) {
            goto die;
        }
        if (!(ctx->hflags & MIPS_HFLAG_DM)) {
            MIPS_INVAL(opn);
            generate_exception_end(ctx, EXCP_RI);
        } else {
            gen_helper_deret(cpu_env);
            ctx->bstate = BS_EXCP;
        }
        break;
    case OPC_WAIT:
        opn = "wait";
        check_insn(ctx, ISA_MIPS3 | ISA_MIPS32);
        if ((ctx->insn_flags & ISA_MIPS32R6) &&
            (ctx->hflags & MIPS_HFLAG_BMASK)) {
            goto die;
        }
        /* If we get an exception, we want to restart at next instruction */
        ctx->pc += 4;
        save_cpu_state(ctx, 1);
        ctx->pc -= 4;
        gen_helper_wait(cpu_env);
        ctx->bstate = BS_EXCP;
        break;
    default:
 die:
        MIPS_INVAL(opn);
        generate_exception_end(ctx, EXCP_RI);
        return;
    }
    (void)opn; /* avoid a compiler warning */
}
#endif /* !CONFIG_USER_ONLY */

/* CP1 Branches (before delay slot) */
static void gen_compute_branch1(DisasContext *ctx, uint32_t op,
                                int32_t cc, int32_t offset)
{
    target_ulong btarget;
    TCGv_i32 t0 = tcg_temp_new_i32();

    if ((ctx->insn_flags & ISA_MIPS32R6) && (ctx->hflags & MIPS_HFLAG_BMASK)) {
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }

    if (cc != 0)
        check_insn(ctx, ISA_MIPS4 | ISA_MIPS32);

    btarget = ctx->pc + 4 + offset;

    switch (op) {
    case OPC_BC1F:
        tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
        tcg_gen_not_i32(t0, t0);
        tcg_gen_andi_i32(t0, t0, 1);
        tcg_gen_extu_i32_tl(bcond, t0);
        goto not_likely;
    case OPC_BC1FL:
        tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
        tcg_gen_not_i32(t0, t0);
        tcg_gen_andi_i32(t0, t0, 1);
        tcg_gen_extu_i32_tl(bcond, t0);
        goto likely;
    case OPC_BC1T:
        tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
        tcg_gen_andi_i32(t0, t0, 1);
        tcg_gen_extu_i32_tl(bcond, t0);
        goto not_likely;
    case OPC_BC1TL:
        tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
        tcg_gen_andi_i32(t0, t0, 1);
        tcg_gen_extu_i32_tl(bcond, t0);
    likely:
        ctx->hflags |= MIPS_HFLAG_BL;
        break;
    case OPC_BC1FANY2:
        {
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+1));
            tcg_gen_nand_i32(t0, t0, t1);
            tcg_temp_free_i32(t1);
            tcg_gen_andi_i32(t0, t0, 1);
            tcg_gen_extu_i32_tl(bcond, t0);
        }
        goto not_likely;
    case OPC_BC1TANY2:
        {
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+1));
            tcg_gen_or_i32(t0, t0, t1);
            tcg_temp_free_i32(t1);
            tcg_gen_andi_i32(t0, t0, 1);
            tcg_gen_extu_i32_tl(bcond, t0);
        }
        goto not_likely;
    case OPC_BC1FANY4:
        {
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+1));
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+2));
            tcg_gen_and_i32(t0, t0, t1);
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+3));
            tcg_gen_nand_i32(t0, t0, t1);
            tcg_temp_free_i32(t1);
            tcg_gen_andi_i32(t0, t0, 1);
            tcg_gen_extu_i32_tl(bcond, t0);
        }
        goto not_likely;
    case OPC_BC1TANY4:
        {
            TCGv_i32 t1 = tcg_temp_new_i32();
            tcg_gen_shri_i32(t0, fpu_fcr31, get_fp_bit(cc));
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+1));
            tcg_gen_or_i32(t0, t0, t1);
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+2));
            tcg_gen_or_i32(t0, t0, t1);
            tcg_gen_shri_i32(t1, fpu_fcr31, get_fp_bit(cc+3));
            tcg_gen_or_i32(t0, t0, t1);
            tcg_temp_free_i32(t1);
            tcg_gen_andi_i32(t0, t0, 1);
            tcg_gen_extu_i32_tl(bcond, t0);
        }
    not_likely:
        ctx->hflags |= MIPS_HFLAG_BC;
        break;
    default:
        MIPS_INVAL("cp1 cond branch");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }
    ctx->btarget = btarget;
    ctx->hflags |= MIPS_HFLAG_BDS32;
 out:
    tcg_temp_free_i32(t0);
}

/* R6 CP1 Branches */
static void gen_compute_branch1_r6(DisasContext *ctx, uint32_t op,
                                   int32_t ft, int32_t offset,
                                   int delayslot_size)
{
    target_ulong btarget;
    TCGv_i64 t0 = tcg_temp_new_i64();

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
#ifdef MIPS_DEBUG_DISAS
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x" TARGET_FMT_lx
                  "\n", ctx->pc);
#endif
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }

    gen_load_fpr64(ctx, t0, ft);
    tcg_gen_andi_i64(t0, t0, 1);

    btarget = addr_add(ctx, ctx->pc + 4, offset);

    switch (op) {
    case OPC_BC1EQZ:
        tcg_gen_xori_i64(t0, t0, 1);
        ctx->hflags |= MIPS_HFLAG_BC;
        break;
    case OPC_BC1NEZ:
        /* t0 already set */
        ctx->hflags |= MIPS_HFLAG_BC;
        break;
    default:
        MIPS_INVAL("cp1 cond branch");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }

    tcg_gen_trunc_i64_tl(bcond, t0);

    ctx->btarget = btarget;

    switch (delayslot_size) {
    case 2:
        ctx->hflags |= MIPS_HFLAG_BDS16;
        break;
    case 4:
        ctx->hflags |= MIPS_HFLAG_BDS32;
        break;
    }

out:
    tcg_temp_free_i64(t0);
}

/* Coprocessor 1 (FPU) */

#define FOP(func, fmt) (((fmt) << 21) | (func))

enum fopcode {
    OPC_ADD_S = FOP(0, FMT_S),
    OPC_SUB_S = FOP(1, FMT_S),
    OPC_MUL_S = FOP(2, FMT_S),
    OPC_DIV_S = FOP(3, FMT_S),
    OPC_SQRT_S = FOP(4, FMT_S),
    OPC_ABS_S = FOP(5, FMT_S),
    OPC_MOV_S = FOP(6, FMT_S),
    OPC_NEG_S = FOP(7, FMT_S),
    OPC_ROUND_L_S = FOP(8, FMT_S),
    OPC_TRUNC_L_S = FOP(9, FMT_S),
    OPC_CEIL_L_S = FOP(10, FMT_S),
    OPC_FLOOR_L_S = FOP(11, FMT_S),
    OPC_ROUND_W_S = FOP(12, FMT_S),
    OPC_TRUNC_W_S = FOP(13, FMT_S),
    OPC_CEIL_W_S = FOP(14, FMT_S),
    OPC_FLOOR_W_S = FOP(15, FMT_S),
    OPC_SEL_S = FOP(16, FMT_S),
    OPC_MOVCF_S = FOP(17, FMT_S),
    OPC_MOVZ_S = FOP(18, FMT_S),
    OPC_MOVN_S = FOP(19, FMT_S),
    OPC_SELEQZ_S = FOP(20, FMT_S),
    OPC_RECIP_S = FOP(21, FMT_S),
    OPC_RSQRT_S = FOP(22, FMT_S),
    OPC_SELNEZ_S = FOP(23, FMT_S),
    OPC_MADDF_S = FOP(24, FMT_S),
    OPC_MSUBF_S = FOP(25, FMT_S),
    OPC_RINT_S = FOP(26, FMT_S),
    OPC_CLASS_S = FOP(27, FMT_S),
    OPC_MIN_S = FOP(28, FMT_S),
    OPC_RECIP2_S = FOP(28, FMT_S),
    OPC_MINA_S = FOP(29, FMT_S),
    OPC_RECIP1_S = FOP(29, FMT_S),
    OPC_MAX_S = FOP(30, FMT_S),
    OPC_RSQRT1_S = FOP(30, FMT_S),
    OPC_MAXA_S = FOP(31, FMT_S),
    OPC_RSQRT2_S = FOP(31, FMT_S),
    OPC_CVT_D_S = FOP(33, FMT_S),
    OPC_CVT_W_S = FOP(36, FMT_S),
    OPC_CVT_L_S = FOP(37, FMT_S),
    OPC_CVT_PS_S = FOP(38, FMT_S),
    OPC_CMP_F_S = FOP (48, FMT_S),
    OPC_CMP_UN_S = FOP (49, FMT_S),
    OPC_CMP_EQ_S = FOP (50, FMT_S),
    OPC_CMP_UEQ_S = FOP (51, FMT_S),
    OPC_CMP_OLT_S = FOP (52, FMT_S),
    OPC_CMP_ULT_S = FOP (53, FMT_S),
    OPC_CMP_OLE_S = FOP (54, FMT_S),
    OPC_CMP_ULE_S = FOP (55, FMT_S),
    OPC_CMP_SF_S = FOP (56, FMT_S),
    OPC_CMP_NGLE_S = FOP (57, FMT_S),
    OPC_CMP_SEQ_S = FOP (58, FMT_S),
    OPC_CMP_NGL_S = FOP (59, FMT_S),
    OPC_CMP_LT_S = FOP (60, FMT_S),
    OPC_CMP_NGE_S = FOP (61, FMT_S),
    OPC_CMP_LE_S = FOP (62, FMT_S),
    OPC_CMP_NGT_S = FOP (63, FMT_S),

    OPC_ADD_D = FOP(0, FMT_D),
    OPC_SUB_D = FOP(1, FMT_D),
    OPC_MUL_D = FOP(2, FMT_D),
    OPC_DIV_D = FOP(3, FMT_D),
    OPC_SQRT_D = FOP(4, FMT_D),
    OPC_ABS_D = FOP(5, FMT_D),
    OPC_MOV_D = FOP(6, FMT_D),
    OPC_NEG_D = FOP(7, FMT_D),
    OPC_ROUND_L_D = FOP(8, FMT_D),
    OPC_TRUNC_L_D = FOP(9, FMT_D),
    OPC_CEIL_L_D = FOP(10, FMT_D),
    OPC_FLOOR_L_D = FOP(11, FMT_D),
    OPC_ROUND_W_D = FOP(12, FMT_D),
    OPC_TRUNC_W_D = FOP(13, FMT_D),
    OPC_CEIL_W_D = FOP(14, FMT_D),
    OPC_FLOOR_W_D = FOP(15, FMT_D),
    OPC_SEL_D = FOP(16, FMT_D),
    OPC_MOVCF_D = FOP(17, FMT_D),
    OPC_MOVZ_D = FOP(18, FMT_D),
    OPC_MOVN_D = FOP(19, FMT_D),
    OPC_SELEQZ_D = FOP(20, FMT_D),
    OPC_RECIP_D = FOP(21, FMT_D),
    OPC_RSQRT_D = FOP(22, FMT_D),
    OPC_SELNEZ_D = FOP(23, FMT_D),
    OPC_MADDF_D = FOP(24, FMT_D),
    OPC_MSUBF_D = FOP(25, FMT_D),
    OPC_RINT_D = FOP(26, FMT_D),
    OPC_CLASS_D = FOP(27, FMT_D),
    OPC_MIN_D = FOP(28, FMT_D),
    OPC_RECIP2_D = FOP(28, FMT_D),
    OPC_MINA_D = FOP(29, FMT_D),
    OPC_RECIP1_D = FOP(29, FMT_D),
    OPC_MAX_D = FOP(30, FMT_D),
    OPC_RSQRT1_D = FOP(30, FMT_D),
    OPC_MAXA_D = FOP(31, FMT_D),
    OPC_RSQRT2_D = FOP(31, FMT_D),
    OPC_CVT_S_D = FOP(32, FMT_D),
    OPC_CVT_W_D = FOP(36, FMT_D),
    OPC_CVT_L_D = FOP(37, FMT_D),
    OPC_CMP_F_D = FOP (48, FMT_D),
    OPC_CMP_UN_D = FOP (49, FMT_D),
    OPC_CMP_EQ_D = FOP (50, FMT_D),
    OPC_CMP_UEQ_D = FOP (51, FMT_D),
    OPC_CMP_OLT_D = FOP (52, FMT_D),
    OPC_CMP_ULT_D = FOP (53, FMT_D),
    OPC_CMP_OLE_D = FOP (54, FMT_D),
    OPC_CMP_ULE_D = FOP (55, FMT_D),
    OPC_CMP_SF_D = FOP (56, FMT_D),
    OPC_CMP_NGLE_D = FOP (57, FMT_D),
    OPC_CMP_SEQ_D = FOP (58, FMT_D),
    OPC_CMP_NGL_D = FOP (59, FMT_D),
    OPC_CMP_LT_D = FOP (60, FMT_D),
    OPC_CMP_NGE_D = FOP (61, FMT_D),
    OPC_CMP_LE_D = FOP (62, FMT_D),
    OPC_CMP_NGT_D = FOP (63, FMT_D),

    OPC_CVT_S_W = FOP(32, FMT_W),
    OPC_CVT_D_W = FOP(33, FMT_W),
    OPC_CVT_S_L = FOP(32, FMT_L),
    OPC_CVT_D_L = FOP(33, FMT_L),
    OPC_CVT_PS_PW = FOP(38, FMT_W),

    OPC_ADD_PS = FOP(0, FMT_PS),
    OPC_SUB_PS = FOP(1, FMT_PS),
    OPC_MUL_PS = FOP(2, FMT_PS),
    OPC_DIV_PS = FOP(3, FMT_PS),
    OPC_ABS_PS = FOP(5, FMT_PS),
    OPC_MOV_PS = FOP(6, FMT_PS),
    OPC_NEG_PS = FOP(7, FMT_PS),
    OPC_MOVCF_PS = FOP(17, FMT_PS),
    OPC_MOVZ_PS = FOP(18, FMT_PS),
    OPC_MOVN_PS = FOP(19, FMT_PS),
    OPC_ADDR_PS = FOP(24, FMT_PS),
    OPC_MULR_PS = FOP(26, FMT_PS),
    OPC_RECIP2_PS = FOP(28, FMT_PS),
    OPC_RECIP1_PS = FOP(29, FMT_PS),
    OPC_RSQRT1_PS = FOP(30, FMT_PS),
    OPC_RSQRT2_PS = FOP(31, FMT_PS),

    OPC_CVT_S_PU = FOP(32, FMT_PS),
    OPC_CVT_PW_PS = FOP(36, FMT_PS),
    OPC_CVT_S_PL = FOP(40, FMT_PS),
    OPC_PLL_PS = FOP(44, FMT_PS),
    OPC_PLU_PS = FOP(45, FMT_PS),
    OPC_PUL_PS = FOP(46, FMT_PS),
    OPC_PUU_PS = FOP(47, FMT_PS),
    OPC_CMP_F_PS = FOP (48, FMT_PS),
    OPC_CMP_UN_PS = FOP (49, FMT_PS),
    OPC_CMP_EQ_PS = FOP (50, FMT_PS),
    OPC_CMP_UEQ_PS = FOP (51, FMT_PS),
    OPC_CMP_OLT_PS = FOP (52, FMT_PS),
    OPC_CMP_ULT_PS = FOP (53, FMT_PS),
    OPC_CMP_OLE_PS = FOP (54, FMT_PS),
    OPC_CMP_ULE_PS = FOP (55, FMT_PS),
    OPC_CMP_SF_PS = FOP (56, FMT_PS),
    OPC_CMP_NGLE_PS = FOP (57, FMT_PS),
    OPC_CMP_SEQ_PS = FOP (58, FMT_PS),
    OPC_CMP_NGL_PS = FOP (59, FMT_PS),
    OPC_CMP_LT_PS = FOP (60, FMT_PS),
    OPC_CMP_NGE_PS = FOP (61, FMT_PS),
    OPC_CMP_LE_PS = FOP (62, FMT_PS),
    OPC_CMP_NGT_PS = FOP (63, FMT_PS),
};

enum r6_f_cmp_op {
    R6_OPC_CMP_AF_S   = FOP(0, FMT_W),
    R6_OPC_CMP_UN_S   = FOP(1, FMT_W),
    R6_OPC_CMP_EQ_S   = FOP(2, FMT_W),
    R6_OPC_CMP_UEQ_S  = FOP(3, FMT_W),
    R6_OPC_CMP_LT_S   = FOP(4, FMT_W),
    R6_OPC_CMP_ULT_S  = FOP(5, FMT_W),
    R6_OPC_CMP_LE_S   = FOP(6, FMT_W),
    R6_OPC_CMP_ULE_S  = FOP(7, FMT_W),
    R6_OPC_CMP_SAF_S  = FOP(8, FMT_W),
    R6_OPC_CMP_SUN_S  = FOP(9, FMT_W),
    R6_OPC_CMP_SEQ_S  = FOP(10, FMT_W),
    R6_OPC_CMP_SEUQ_S = FOP(11, FMT_W),
    R6_OPC_CMP_SLT_S  = FOP(12, FMT_W),
    R6_OPC_CMP_SULT_S = FOP(13, FMT_W),
    R6_OPC_CMP_SLE_S  = FOP(14, FMT_W),
    R6_OPC_CMP_SULE_S = FOP(15, FMT_W),
    R6_OPC_CMP_OR_S   = FOP(17, FMT_W),
    R6_OPC_CMP_UNE_S  = FOP(18, FMT_W),
    R6_OPC_CMP_NE_S   = FOP(19, FMT_W),
    R6_OPC_CMP_SOR_S  = FOP(25, FMT_W),
    R6_OPC_CMP_SUNE_S = FOP(26, FMT_W),
    R6_OPC_CMP_SNE_S  = FOP(27, FMT_W),

    R6_OPC_CMP_AF_D   = FOP(0, FMT_L),
    R6_OPC_CMP_UN_D   = FOP(1, FMT_L),
    R6_OPC_CMP_EQ_D   = FOP(2, FMT_L),
    R6_OPC_CMP_UEQ_D  = FOP(3, FMT_L),
    R6_OPC_CMP_LT_D   = FOP(4, FMT_L),
    R6_OPC_CMP_ULT_D  = FOP(5, FMT_L),
    R6_OPC_CMP_LE_D   = FOP(6, FMT_L),
    R6_OPC_CMP_ULE_D  = FOP(7, FMT_L),
    R6_OPC_CMP_SAF_D  = FOP(8, FMT_L),
    R6_OPC_CMP_SUN_D  = FOP(9, FMT_L),
    R6_OPC_CMP_SEQ_D  = FOP(10, FMT_L),
    R6_OPC_CMP_SEUQ_D = FOP(11, FMT_L),
    R6_OPC_CMP_SLT_D  = FOP(12, FMT_L),
    R6_OPC_CMP_SULT_D = FOP(13, FMT_L),
    R6_OPC_CMP_SLE_D  = FOP(14, FMT_L),
    R6_OPC_CMP_SULE_D = FOP(15, FMT_L),
    R6_OPC_CMP_OR_D   = FOP(17, FMT_L),
    R6_OPC_CMP_UNE_D  = FOP(18, FMT_L),
    R6_OPC_CMP_NE_D   = FOP(19, FMT_L),
    R6_OPC_CMP_SOR_D  = FOP(25, FMT_L),
    R6_OPC_CMP_SUNE_D = FOP(26, FMT_L),
    R6_OPC_CMP_SNE_D  = FOP(27, FMT_L),
};
static void gen_cp1 (DisasContext *ctx, uint32_t opc, int rt, int fs)
{
    TCGv t0 = tcg_temp_new();

    switch (opc) {
    case OPC_MFC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
        }
        gen_store_gpr(t0, rt);
        break;
    case OPC_MTC1:
        gen_load_gpr(t0, rt);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32(ctx, fp0, fs);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_CFC1:
        gen_helper_1e0i(cfc1, t0, fs);
        gen_store_gpr(t0, rt);
        break;
    case OPC_CTC1:
        gen_load_gpr(t0, rt);
        save_cpu_state(ctx, 0);
        {
            TCGv_i32 fs_tmp = tcg_const_i32(fs);

            gen_helper_0e2i(ctc1, t0, fs_tmp, rt);
            tcg_temp_free_i32(fs_tmp);
        }
        /* Stop translation as we may have changed hflags */
        ctx->bstate = BS_STOP;
        break;
#if defined(TARGET_MIPS64)
    case OPC_DMFC1:
        gen_load_fpr64(ctx, t0, fs);
        gen_store_gpr(t0, rt);
        break;
    case OPC_DMTC1:
        gen_load_gpr(t0, rt);
        gen_store_fpr64(ctx, t0, fs);
        break;
#endif
    case OPC_MFHC1:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32h(ctx, fp0, fs);
            tcg_gen_ext_i32_tl(t0, fp0);
            tcg_temp_free_i32(fp0);
        }
        gen_store_gpr(t0, rt);
        break;
    case OPC_MTHC1:
        gen_load_gpr(t0, rt);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32h(ctx, fp0, fs);
            tcg_temp_free_i32(fp0);
        }
        break;
    default:
        MIPS_INVAL("cp1 move");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }

 out:
    tcg_temp_free(t0);
}

static void gen_movci (DisasContext *ctx, int rd, int rs, int cc, int tf)
{
    TCGLabel *l1;
    TCGCond cond;
    TCGv_i32 t0;

    if (rd == 0) {
        /* Treat as NOP. */
        return;
    }

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    l1 = gen_new_label();
    t0 = tcg_temp_new_i32();
    tcg_gen_andi_i32(t0, fpu_fcr31, 1 << get_fp_bit(cc));
    tcg_gen_brcondi_i32(cond, t0, 0, l1);
    tcg_temp_free_i32(t0);
    if (rs == 0) {
        tcg_gen_movi_tl(cpu_gpr[rd], 0);
    } else {
        tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
    }
    gen_set_label(l1);
}

static inline void gen_movcf_s(DisasContext *ctx, int fs, int fd, int cc,
                               int tf)
{
    int cond;
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGLabel *l1 = gen_new_label();

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    tcg_gen_andi_i32(t0, fpu_fcr31, 1 << get_fp_bit(cc));
    tcg_gen_brcondi_i32(cond, t0, 0, l1);
    gen_load_fpr32(ctx, t0, fs);
    gen_store_fpr32(ctx, t0, fd);
    gen_set_label(l1);
    tcg_temp_free_i32(t0);
}

static inline void gen_movcf_d (DisasContext *ctx, int fs, int fd, int cc, int tf)
{
    int cond;
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGv_i64 fp0;
    TCGLabel *l1 = gen_new_label();

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    tcg_gen_andi_i32(t0, fpu_fcr31, 1 << get_fp_bit(cc));
    tcg_gen_brcondi_i32(cond, t0, 0, l1);
    tcg_temp_free_i32(t0);
    fp0 = tcg_temp_new_i64();
    gen_load_fpr64(ctx, fp0, fs);
    gen_store_fpr64(ctx, fp0, fd);
    tcg_temp_free_i64(fp0);
    gen_set_label(l1);
}

static inline void gen_movcf_ps(DisasContext *ctx, int fs, int fd,
                                int cc, int tf)
{
    int cond;
    TCGv_i32 t0 = tcg_temp_new_i32();
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    if (tf)
        cond = TCG_COND_EQ;
    else
        cond = TCG_COND_NE;

    tcg_gen_andi_i32(t0, fpu_fcr31, 1 << get_fp_bit(cc));
    tcg_gen_brcondi_i32(cond, t0, 0, l1);
    gen_load_fpr32(ctx, t0, fs);
    gen_store_fpr32(ctx, t0, fd);
    gen_set_label(l1);

    tcg_gen_andi_i32(t0, fpu_fcr31, 1 << get_fp_bit(cc+1));
    tcg_gen_brcondi_i32(cond, t0, 0, l2);
    gen_load_fpr32h(ctx, t0, fs);
    gen_store_fpr32h(ctx, t0, fd);
    tcg_temp_free_i32(t0);
    gen_set_label(l2);
}

static void gen_sel_s(DisasContext *ctx, enum fopcode op1, int fd, int ft,
                      int fs)
{
    TCGv_i32 t1 = tcg_const_i32(0);
    TCGv_i32 fp0 = tcg_temp_new_i32();
    TCGv_i32 fp1 = tcg_temp_new_i32();
    TCGv_i32 fp2 = tcg_temp_new_i32();
    gen_load_fpr32(ctx, fp0, fd);
    gen_load_fpr32(ctx, fp1, ft);
    gen_load_fpr32(ctx, fp2, fs);

    switch (op1) {
    case OPC_SEL_S:
        tcg_gen_andi_i32(fp0, fp0, 1);
        tcg_gen_movcond_i32(TCG_COND_NE, fp0, fp0, t1, fp1, fp2);
        break;
    case OPC_SELEQZ_S:
        tcg_gen_andi_i32(fp1, fp1, 1);
        tcg_gen_movcond_i32(TCG_COND_EQ, fp0, fp1, t1, fp2, t1);
        break;
    case OPC_SELNEZ_S:
        tcg_gen_andi_i32(fp1, fp1, 1);
        tcg_gen_movcond_i32(TCG_COND_NE, fp0, fp1, t1, fp2, t1);
        break;
    default:
        MIPS_INVAL("gen_sel_s");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    gen_store_fpr32(ctx, fp0, fd);
    tcg_temp_free_i32(fp2);
    tcg_temp_free_i32(fp1);
    tcg_temp_free_i32(fp0);
    tcg_temp_free_i32(t1);
}

static void gen_sel_d(DisasContext *ctx, enum fopcode op1, int fd, int ft,
                      int fs)
{
    TCGv_i64 t1 = tcg_const_i64(0);
    TCGv_i64 fp0 = tcg_temp_new_i64();
    TCGv_i64 fp1 = tcg_temp_new_i64();
    TCGv_i64 fp2 = tcg_temp_new_i64();
    gen_load_fpr64(ctx, fp0, fd);
    gen_load_fpr64(ctx, fp1, ft);
    gen_load_fpr64(ctx, fp2, fs);

    switch (op1) {
    case OPC_SEL_D:
        tcg_gen_andi_i64(fp0, fp0, 1);
        tcg_gen_movcond_i64(TCG_COND_NE, fp0, fp0, t1, fp1, fp2);
        break;
    case OPC_SELEQZ_D:
        tcg_gen_andi_i64(fp1, fp1, 1);
        tcg_gen_movcond_i64(TCG_COND_EQ, fp0, fp1, t1, fp2, t1);
        break;
    case OPC_SELNEZ_D:
        tcg_gen_andi_i64(fp1, fp1, 1);
        tcg_gen_movcond_i64(TCG_COND_NE, fp0, fp1, t1, fp2, t1);
        break;
    default:
        MIPS_INVAL("gen_sel_d");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    gen_store_fpr64(ctx, fp0, fd);
    tcg_temp_free_i64(fp2);
    tcg_temp_free_i64(fp1);
    tcg_temp_free_i64(fp0);
    tcg_temp_free_i64(t1);
}

static void gen_farith (DisasContext *ctx, enum fopcode op1,
                        int ft, int fs, int fd, int cc)
{
    uint32_t func = ctx->opcode & 0x3f;
    switch (op1) {
    case OPC_ADD_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_add_s(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_SUB_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_sub_s(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_MUL_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_mul_s(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_DIV_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_div_s(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_SQRT_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_sqrt_s(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_ABS_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->abs2008) {
                tcg_gen_andi_i32(fp0, fp0, 0x7fffffffUL);
            } else {
                gen_helper_float_abs_s(fp0, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_MOV_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_NEG_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->abs2008) {
                tcg_gen_xori_i32(fp0, fp0, 1UL << 31);
            } else {
                gen_helper_float_chs_s(fp0, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_ROUND_L_S:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            if (ctx->nan2008) {
                gen_helper_float_round_2008_l_s(fp64, cpu_env, fp32);
            } else {
                gen_helper_float_round_l_s(fp64, cpu_env, fp32);
            }
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_TRUNC_L_S:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            if (ctx->nan2008) {
                gen_helper_float_trunc_2008_l_s(fp64, cpu_env, fp32);
            } else {
                gen_helper_float_trunc_l_s(fp64, cpu_env, fp32);
            }
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_CEIL_L_S:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            if (ctx->nan2008) {
                gen_helper_float_ceil_2008_l_s(fp64, cpu_env, fp32);
            } else {
                gen_helper_float_ceil_l_s(fp64, cpu_env, fp32);
            }
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_FLOOR_L_S:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            if (ctx->nan2008) {
                gen_helper_float_floor_2008_l_s(fp64, cpu_env, fp32);
            } else {
                gen_helper_float_floor_l_s(fp64, cpu_env, fp32);
            }
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_ROUND_W_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_round_2008_w_s(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_round_w_s(fp0, cpu_env, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_TRUNC_W_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_trunc_2008_w_s(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_trunc_w_s(fp0, cpu_env, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_CEIL_W_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_ceil_2008_w_s(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_ceil_w_s(fp0, cpu_env, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_FLOOR_W_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_floor_2008_w_s(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_floor_w_s(fp0, cpu_env, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_SEL_S:
        check_insn(ctx, ISA_MIPS32R6);
        gen_sel_s(ctx, op1, fd, ft, fs);
        break;
    case OPC_SELEQZ_S:
        check_insn(ctx, ISA_MIPS32R6);
        gen_sel_s(ctx, op1, fd, ft, fs);
        break;
    case OPC_SELNEZ_S:
        check_insn(ctx, ISA_MIPS32R6);
        gen_sel_s(ctx, op1, fd, ft, fs);
        break;
    case OPC_MOVCF_S:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        gen_movcf_s(ctx, fs, fd, (ft >> 2) & 0x7, ft & 0x1);
        break;
    case OPC_MOVZ_S:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        {
            TCGLabel *l1 = gen_new_label();
            TCGv_i32 fp0;

            if (ft != 0) {
                tcg_gen_brcondi_tl(TCG_COND_NE, cpu_gpr[ft], 0, l1);
            }
            fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
            gen_set_label(l1);
        }
        break;
    case OPC_MOVN_S:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        {
            TCGLabel *l1 = gen_new_label();
            TCGv_i32 fp0;

            if (ft != 0) {
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[ft], 0, l1);
                fp0 = tcg_temp_new_i32();
                gen_load_fpr32(ctx, fp0, fs);
                gen_store_fpr32(ctx, fp0, fd);
                tcg_temp_free_i32(fp0);
                gen_set_label(l1);
            }
        }
        break;
    case OPC_RECIP_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_recip_s(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_RSQRT_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_rsqrt_s(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_MADDF_S:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_load_fpr32(ctx, fp2, fd);
            gen_helper_float_maddf_s(fp2, cpu_env, fp0, fp1, fp2);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_MSUBF_S:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_load_fpr32(ctx, fp2, fd);
            gen_helper_float_msubf_s(fp2, cpu_env, fp0, fp1, fp2);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_RINT_S:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_rint_s(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_CLASS_S:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_class_s(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_MIN_S: /* OPC_RECIP2_S */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MIN_S */
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_min_s(fp2, cpu_env, fp0, fp1);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fp0);
        } else {
            /* OPC_RECIP2_S */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i32 fp0 = tcg_temp_new_i32();
                TCGv_i32 fp1 = tcg_temp_new_i32();

                gen_load_fpr32(ctx, fp0, fs);
                gen_load_fpr32(ctx, fp1, ft);
                gen_helper_float_recip2_s(fp0, cpu_env, fp0, fp1);
                tcg_temp_free_i32(fp1);
                gen_store_fpr32(ctx, fp0, fd);
                tcg_temp_free_i32(fp0);
            }
        }
        break;
    case OPC_MINA_S: /* OPC_RECIP1_S */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MINA_S */
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_mina_s(fp2, cpu_env, fp0, fp1);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fp0);
        } else {
            /* OPC_RECIP1_S */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i32 fp0 = tcg_temp_new_i32();

                gen_load_fpr32(ctx, fp0, fs);
                gen_helper_float_recip1_s(fp0, cpu_env, fp0);
                gen_store_fpr32(ctx, fp0, fd);
                tcg_temp_free_i32(fp0);
            }
        }
        break;
    case OPC_MAX_S: /* OPC_RSQRT1_S */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MAX_S */
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_max_s(fp1, cpu_env, fp0, fp1);
            gen_store_fpr32(ctx, fp1, fd);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fp0);
        } else {
            /* OPC_RSQRT1_S */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i32 fp0 = tcg_temp_new_i32();

                gen_load_fpr32(ctx, fp0, fs);
                gen_helper_float_rsqrt1_s(fp0, cpu_env, fp0);
                gen_store_fpr32(ctx, fp0, fd);
                tcg_temp_free_i32(fp0);
            }
        }
        break;
    case OPC_MAXA_S: /* OPC_RSQRT2_S */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MAXA_S */
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_helper_float_maxa_s(fp1, cpu_env, fp0, fp1);
            gen_store_fpr32(ctx, fp1, fd);
            tcg_temp_free_i32(fp1);
            tcg_temp_free_i32(fp0);
        } else {
            /* OPC_RSQRT2_S */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i32 fp0 = tcg_temp_new_i32();
                TCGv_i32 fp1 = tcg_temp_new_i32();

                gen_load_fpr32(ctx, fp0, fs);
                gen_load_fpr32(ctx, fp1, ft);
                gen_helper_float_rsqrt2_s(fp0, cpu_env, fp0, fp1);
                tcg_temp_free_i32(fp1);
                gen_store_fpr32(ctx, fp0, fd);
                tcg_temp_free_i32(fp0);
            }
        }
        break;
    case OPC_CVT_D_S:
        check_cp1_registers(ctx, fd);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            gen_helper_float_cvtd_s(fp64, cpu_env, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_CVT_W_S:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_cvt_2008_w_s(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_cvt_w_s(fp0, cpu_env, fp0);
            }
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_CVT_L_S:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            if (ctx->nan2008) {
                gen_helper_float_cvt_2008_l_s(fp64, cpu_env, fp32);
            } else {
                gen_helper_float_cvt_l_s(fp64, cpu_env, fp32);
            }
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_CVT_PS_S:
        check_ps(ctx);
        {
            TCGv_i64 fp64 = tcg_temp_new_i64();
            TCGv_i32 fp32_0 = tcg_temp_new_i32();
            TCGv_i32 fp32_1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp32_0, fs);
            gen_load_fpr32(ctx, fp32_1, ft);
            tcg_gen_concat_i32_i64(fp64, fp32_1, fp32_0);
            tcg_temp_free_i32(fp32_1);
            tcg_temp_free_i32(fp32_0);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_CMP_F_S:
    case OPC_CMP_UN_S:
    case OPC_CMP_EQ_S:
    case OPC_CMP_UEQ_S:
    case OPC_CMP_OLT_S:
    case OPC_CMP_ULT_S:
    case OPC_CMP_OLE_S:
    case OPC_CMP_ULE_S:
    case OPC_CMP_SF_S:
    case OPC_CMP_NGLE_S:
    case OPC_CMP_SEQ_S:
    case OPC_CMP_NGL_S:
    case OPC_CMP_LT_S:
    case OPC_CMP_NGE_S:
    case OPC_CMP_LE_S:
    case OPC_CMP_NGT_S:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        if (ctx->opcode & (1 << 6)) {
            gen_cmpabs_s(ctx, func-48, ft, fs, cc);
        } else {
            gen_cmp_s(ctx, func-48, ft, fs, cc);
        }
        break;
    case OPC_ADD_D:
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_add_d(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_SUB_D:
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_sub_d(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MUL_D:
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_mul_d(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_DIV_D:
        check_cp1_registers(ctx, fs | ft | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_div_d(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_SQRT_D:
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_sqrt_d(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_ABS_D:
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->abs2008) {
                tcg_gen_andi_i64(fp0, fp0, 0x7fffffffffffffffULL);
            } else {
                gen_helper_float_abs_d(fp0, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MOV_D:
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_NEG_D:
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->abs2008) {
                tcg_gen_xori_i64(fp0, fp0, 1ULL << 63);
            } else {
                gen_helper_float_chs_d(fp0, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_ROUND_L_D:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_round_2008_l_d(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_round_l_d(fp0, cpu_env, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_TRUNC_L_D:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_trunc_2008_l_d(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_trunc_l_d(fp0, cpu_env, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_CEIL_L_D:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_ceil_2008_l_d(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_ceil_l_d(fp0, cpu_env, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_FLOOR_L_D:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_floor_2008_l_d(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_floor_l_d(fp0, cpu_env, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_ROUND_W_D:
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            if (ctx->nan2008) {
                gen_helper_float_round_2008_w_d(fp32, cpu_env, fp64);
            } else {
                gen_helper_float_round_w_d(fp32, cpu_env, fp64);
            }
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_TRUNC_W_D:
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            if (ctx->nan2008) {
                gen_helper_float_trunc_2008_w_d(fp32, cpu_env, fp64);
            } else {
                gen_helper_float_trunc_w_d(fp32, cpu_env, fp64);
            }
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_CEIL_W_D:
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            if (ctx->nan2008) {
                gen_helper_float_ceil_2008_w_d(fp32, cpu_env, fp64);
            } else {
                gen_helper_float_ceil_w_d(fp32, cpu_env, fp64);
            }
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_FLOOR_W_D:
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            if (ctx->nan2008) {
                gen_helper_float_floor_2008_w_d(fp32, cpu_env, fp64);
            } else {
                gen_helper_float_floor_w_d(fp32, cpu_env, fp64);
            }
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_SEL_D:
        check_insn(ctx, ISA_MIPS32R6);
        gen_sel_d(ctx, op1, fd, ft, fs);
        break;
    case OPC_SELEQZ_D:
        check_insn(ctx, ISA_MIPS32R6);
        gen_sel_d(ctx, op1, fd, ft, fs);
        break;
    case OPC_SELNEZ_D:
        check_insn(ctx, ISA_MIPS32R6);
        gen_sel_d(ctx, op1, fd, ft, fs);
        break;
    case OPC_MOVCF_D:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        gen_movcf_d(ctx, fs, fd, (ft >> 2) & 0x7, ft & 0x1);
        break;
    case OPC_MOVZ_D:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        {
            TCGLabel *l1 = gen_new_label();
            TCGv_i64 fp0;

            if (ft != 0) {
                tcg_gen_brcondi_tl(TCG_COND_NE, cpu_gpr[ft], 0, l1);
            }
            fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
            gen_set_label(l1);
        }
        break;
    case OPC_MOVN_D:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        {
            TCGLabel *l1 = gen_new_label();
            TCGv_i64 fp0;

            if (ft != 0) {
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[ft], 0, l1);
                fp0 = tcg_temp_new_i64();
                gen_load_fpr64(ctx, fp0, fs);
                gen_store_fpr64(ctx, fp0, fd);
                tcg_temp_free_i64(fp0);
                gen_set_label(l1);
            }
        }
        break;
    case OPC_RECIP_D:
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_recip_d(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_RSQRT_D:
        check_cp1_registers(ctx, fs | fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_rsqrt_d(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MADDF_D:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fd);
            gen_helper_float_maddf_d(fp2, cpu_env, fp0, fp1, fp2);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
            tcg_temp_free_i64(fp1);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MSUBF_D:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fd);
            gen_helper_float_msubf_d(fp2, cpu_env, fp0, fp1, fp2);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
            tcg_temp_free_i64(fp1);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_RINT_D:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_rint_d(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_CLASS_D:
        check_insn(ctx, ISA_MIPS32R6);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_class_d(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MIN_D: /* OPC_RECIP2_D */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MIN_D */
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_min_d(fp1, cpu_env, fp0, fp1);
            gen_store_fpr64(ctx, fp1, fd);
            tcg_temp_free_i64(fp1);
            tcg_temp_free_i64(fp0);
        } else {
            /* OPC_RECIP2_D */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i64 fp0 = tcg_temp_new_i64();
                TCGv_i64 fp1 = tcg_temp_new_i64();

                gen_load_fpr64(ctx, fp0, fs);
                gen_load_fpr64(ctx, fp1, ft);
                gen_helper_float_recip2_d(fp0, cpu_env, fp0, fp1);
                tcg_temp_free_i64(fp1);
                gen_store_fpr64(ctx, fp0, fd);
                tcg_temp_free_i64(fp0);
            }
        }
        break;
    case OPC_MINA_D: /* OPC_RECIP1_D */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MINA_D */
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_mina_d(fp1, cpu_env, fp0, fp1);
            gen_store_fpr64(ctx, fp1, fd);
            tcg_temp_free_i64(fp1);
            tcg_temp_free_i64(fp0);
        } else {
            /* OPC_RECIP1_D */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i64 fp0 = tcg_temp_new_i64();

                gen_load_fpr64(ctx, fp0, fs);
                gen_helper_float_recip1_d(fp0, cpu_env, fp0);
                gen_store_fpr64(ctx, fp0, fd);
                tcg_temp_free_i64(fp0);
            }
        }
        break;
    case OPC_MAX_D: /*  OPC_RSQRT1_D */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MAX_D */
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_max_d(fp1, cpu_env, fp0, fp1);
            gen_store_fpr64(ctx, fp1, fd);
            tcg_temp_free_i64(fp1);
            tcg_temp_free_i64(fp0);
        } else {
            /* OPC_RSQRT1_D */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i64 fp0 = tcg_temp_new_i64();

                gen_load_fpr64(ctx, fp0, fs);
                gen_helper_float_rsqrt1_d(fp0, cpu_env, fp0);
                gen_store_fpr64(ctx, fp0, fd);
                tcg_temp_free_i64(fp0);
            }
        }
        break;
    case OPC_MAXA_D: /* OPC_RSQRT2_D */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_MAXA_D */
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_maxa_d(fp1, cpu_env, fp0, fp1);
            gen_store_fpr64(ctx, fp1, fd);
            tcg_temp_free_i64(fp1);
            tcg_temp_free_i64(fp0);
        } else {
            /* OPC_RSQRT2_D */
            check_cp1_64bitmode(ctx);
            {
                TCGv_i64 fp0 = tcg_temp_new_i64();
                TCGv_i64 fp1 = tcg_temp_new_i64();

                gen_load_fpr64(ctx, fp0, fs);
                gen_load_fpr64(ctx, fp1, ft);
                gen_helper_float_rsqrt2_d(fp0, cpu_env, fp0, fp1);
                tcg_temp_free_i64(fp1);
                gen_store_fpr64(ctx, fp0, fd);
                tcg_temp_free_i64(fp0);
            }
        }
        break;
    case OPC_CMP_F_D:
    case OPC_CMP_UN_D:
    case OPC_CMP_EQ_D:
    case OPC_CMP_UEQ_D:
    case OPC_CMP_OLT_D:
    case OPC_CMP_ULT_D:
    case OPC_CMP_OLE_D:
    case OPC_CMP_ULE_D:
    case OPC_CMP_SF_D:
    case OPC_CMP_NGLE_D:
    case OPC_CMP_SEQ_D:
    case OPC_CMP_NGL_D:
    case OPC_CMP_LT_D:
    case OPC_CMP_NGE_D:
    case OPC_CMP_LE_D:
    case OPC_CMP_NGT_D:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        if (ctx->opcode & (1 << 6)) {
            gen_cmpabs_d(ctx, func-48, ft, fs, cc);
        } else {
            gen_cmp_d(ctx, func-48, ft, fs, cc);
        }
        break;
    case OPC_CVT_S_D:
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_cvts_d(fp32, cpu_env, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_CVT_W_D:
        check_cp1_registers(ctx, fs);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            if (ctx->nan2008) {
                gen_helper_float_cvt_2008_w_d(fp32, cpu_env, fp64);
            } else {
                gen_helper_float_cvt_w_d(fp32, cpu_env, fp64);
            }
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_CVT_L_D:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            if (ctx->nan2008) {
                gen_helper_float_cvt_2008_l_d(fp0, cpu_env, fp0);
            } else {
                gen_helper_float_cvt_l_d(fp0, cpu_env, fp0);
            }
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_CVT_S_W:
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_cvts_w(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_CVT_D_W:
        check_cp1_registers(ctx, fd);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr32(ctx, fp32, fs);
            gen_helper_float_cvtd_w(fp64, cpu_env, fp32);
            tcg_temp_free_i32(fp32);
            gen_store_fpr64(ctx, fp64, fd);
            tcg_temp_free_i64(fp64);
        }
        break;
    case OPC_CVT_S_L:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp32 = tcg_temp_new_i32();
            TCGv_i64 fp64 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp64, fs);
            gen_helper_float_cvts_l(fp32, cpu_env, fp64);
            tcg_temp_free_i64(fp64);
            gen_store_fpr32(ctx, fp32, fd);
            tcg_temp_free_i32(fp32);
        }
        break;
    case OPC_CVT_D_L:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtd_l(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_CVT_PS_PW:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtps_pw(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_ADD_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_add_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_SUB_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_sub_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MUL_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_mul_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_ABS_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_abs_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MOV_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_NEG_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_chs_ps(fp0, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MOVCF_PS:
        check_ps(ctx);
        gen_movcf_ps(ctx, fs, fd, (ft >> 2) & 0x7, ft & 0x1);
        break;
    case OPC_MOVZ_PS:
        check_ps(ctx);
        {
            TCGLabel *l1 = gen_new_label();
            TCGv_i64 fp0;

            if (ft != 0)
                tcg_gen_brcondi_tl(TCG_COND_NE, cpu_gpr[ft], 0, l1);
            fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
            gen_set_label(l1);
        }
        break;
    case OPC_MOVN_PS:
        check_ps(ctx);
        {
            TCGLabel *l1 = gen_new_label();
            TCGv_i64 fp0;

            if (ft != 0) {
                tcg_gen_brcondi_tl(TCG_COND_EQ, cpu_gpr[ft], 0, l1);
                fp0 = tcg_temp_new_i64();
                gen_load_fpr64(ctx, fp0, fs);
                gen_store_fpr64(ctx, fp0, fd);
                tcg_temp_free_i64(fp0);
                gen_set_label(l1);
            }
        }
        break;
    case OPC_ADDR_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, ft);
            gen_load_fpr64(ctx, fp1, fs);
            gen_helper_float_addr_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_MULR_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, ft);
            gen_load_fpr64(ctx, fp1, fs);
            gen_helper_float_mulr_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_RECIP2_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_recip2_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_RECIP1_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_recip1_ps(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_RSQRT1_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_rsqrt1_ps(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_RSQRT2_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_helper_float_rsqrt2_ps(fp0, cpu_env, fp0, fp1);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_CVT_S_PU:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32h(ctx, fp0, fs);
            gen_helper_float_cvts_pu(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_CVT_PW_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_helper_float_cvtpw_ps(fp0, cpu_env, fp0);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_CVT_S_PL:
        check_cp1_64bitmode(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_helper_float_cvts_pl(fp0, cpu_env, fp0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_PLL_PS:
        check_ps(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_store_fpr32h(ctx, fp0, fd);
            gen_store_fpr32(ctx, fp1, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        break;
    case OPC_PLU_PS:
        check_ps(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32h(ctx, fp1, ft);
            gen_store_fpr32(ctx, fp1, fd);
            gen_store_fpr32h(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        break;
    case OPC_PUL_PS:
        check_ps(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32h(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_store_fpr32(ctx, fp1, fd);
            gen_store_fpr32h(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        break;
    case OPC_PUU_PS:
        check_ps(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();

            gen_load_fpr32h(ctx, fp0, fs);
            gen_load_fpr32h(ctx, fp1, ft);
            gen_store_fpr32(ctx, fp1, fd);
            gen_store_fpr32h(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
        }
        break;
    case OPC_CMP_F_PS:
    case OPC_CMP_UN_PS:
    case OPC_CMP_EQ_PS:
    case OPC_CMP_UEQ_PS:
    case OPC_CMP_OLT_PS:
    case OPC_CMP_ULT_PS:
    case OPC_CMP_OLE_PS:
    case OPC_CMP_ULE_PS:
    case OPC_CMP_SF_PS:
    case OPC_CMP_NGLE_PS:
    case OPC_CMP_SEQ_PS:
    case OPC_CMP_NGL_PS:
    case OPC_CMP_LT_PS:
    case OPC_CMP_NGE_PS:
    case OPC_CMP_LE_PS:
    case OPC_CMP_NGT_PS:
        if (ctx->opcode & (1 << 6)) {
            gen_cmpabs_ps(ctx, func-48, ft, fs, cc);
        } else {
            gen_cmp_ps(ctx, func-48, ft, fs, cc);
        }
        break;
    default:
        MIPS_INVAL("farith");
        generate_exception_end(ctx, EXCP_RI);
        return;
    }
}

/* Coprocessor 3 (FPU) */
static void gen_flt3_ldst (DisasContext *ctx, uint32_t opc,
                           int fd, int fs, int base, int index)
{
    TCGv t0 = tcg_temp_new();

    if (base == 0) {
        gen_load_gpr(t0, index);
    } else if (index == 0) {
        gen_load_gpr(t0, base);
    } else {
        gen_op_addr_add(ctx, t0, cpu_gpr[base], cpu_gpr[index]);
    }
    /* Don't do NOP if destination is zero: we must perform the actual
       memory access. */
    switch (opc) {
    case OPC_LWXC1:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();

            tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESL);
            tcg_gen_trunc_tl_i32(fp0, t0);
            gen_store_fpr32(ctx, fp0, fd);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_LDXC1:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fd);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            tcg_gen_qemu_ld_i64(fp0, t0, ctx->mem_idx, MO_TEQ);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_LUXC1:
        check_cp1_64bitmode(ctx);
        tcg_gen_andi_tl(t0, t0, ~0x7);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();

            tcg_gen_qemu_ld_i64(fp0, t0, ctx->mem_idx, MO_TEQ);
            gen_store_fpr64(ctx, fp0, fd);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_SWXC1:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            gen_load_fpr32(ctx, fp0, fs);
            tcg_gen_qemu_st_i32(fp0, t0, ctx->mem_idx, MO_TEUL);
            tcg_temp_free_i32(fp0);
        }
        break;
    case OPC_SDXC1:
        check_cop1x(ctx);
        check_cp1_registers(ctx, fs);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            tcg_gen_qemu_st_i64(fp0, t0, ctx->mem_idx, MO_TEQ);
            tcg_temp_free_i64(fp0);
        }
        break;
    case OPC_SUXC1:
        check_cp1_64bitmode(ctx);
        tcg_gen_andi_tl(t0, t0, ~0x7);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            gen_load_fpr64(ctx, fp0, fs);
            tcg_gen_qemu_st_i64(fp0, t0, ctx->mem_idx, MO_TEQ);
            tcg_temp_free_i64(fp0);
        }
        break;
    }
    tcg_temp_free(t0);
}

static void gen_flt3_arith (DisasContext *ctx, uint32_t opc,
                            int fd, int fr, int fs, int ft)
{
    switch (opc) {
    case OPC_ALNV_PS:
        check_ps(ctx);
        {
            TCGv t0 = tcg_temp_local_new();
            TCGv_i32 fp = tcg_temp_new_i32();
            TCGv_i32 fph = tcg_temp_new_i32();
            TCGLabel *l1 = gen_new_label();
            TCGLabel *l2 = gen_new_label();

            gen_load_gpr(t0, fr);
            tcg_gen_andi_tl(t0, t0, 0x7);

            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 0, l1);
            gen_load_fpr32(ctx, fp, fs);
            gen_load_fpr32h(ctx, fph, fs);
            gen_store_fpr32(ctx, fp, fd);
            gen_store_fpr32h(ctx, fph, fd);
            tcg_gen_br(l2);
            gen_set_label(l1);
            tcg_gen_brcondi_tl(TCG_COND_NE, t0, 4, l2);
            tcg_temp_free(t0);
#ifdef TARGET_WORDS_BIGENDIAN
            gen_load_fpr32(ctx, fp, fs);
            gen_load_fpr32h(ctx, fph, ft);
            gen_store_fpr32h(ctx, fp, fd);
            gen_store_fpr32(ctx, fph, fd);
#else
            gen_load_fpr32h(ctx, fph, fs);
            gen_load_fpr32(ctx, fp, ft);
            gen_store_fpr32(ctx, fph, fd);
            gen_store_fpr32h(ctx, fp, fd);
#endif
            gen_set_label(l2);
            tcg_temp_free_i32(fp);
            tcg_temp_free_i32(fph);
        }
        break;
    case OPC_MADD_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_load_fpr32(ctx, fp2, fr);
            gen_helper_float_madd_s(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
        }
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
            gen_helper_float_madd_d(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_MADD_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_madd_ps(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_MSUB_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_load_fpr32(ctx, fp2, fr);
            gen_helper_float_msub_s(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
        }
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
            gen_helper_float_msub_d(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_MSUB_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_msub_ps(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_NMADD_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_load_fpr32(ctx, fp2, fr);
            gen_helper_float_nmadd_s(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
        }
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
            gen_helper_float_nmadd_d(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_NMADD_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_nmadd_ps(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_NMSUB_S:
        check_cop1x(ctx);
        {
            TCGv_i32 fp0 = tcg_temp_new_i32();
            TCGv_i32 fp1 = tcg_temp_new_i32();
            TCGv_i32 fp2 = tcg_temp_new_i32();

            gen_load_fpr32(ctx, fp0, fs);
            gen_load_fpr32(ctx, fp1, ft);
            gen_load_fpr32(ctx, fp2, fr);
            gen_helper_float_nmsub_s(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i32(fp0);
            tcg_temp_free_i32(fp1);
            gen_store_fpr32(ctx, fp2, fd);
            tcg_temp_free_i32(fp2);
        }
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
            gen_helper_float_nmsub_d(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    case OPC_NMSUB_PS:
        check_ps(ctx);
        {
            TCGv_i64 fp0 = tcg_temp_new_i64();
            TCGv_i64 fp1 = tcg_temp_new_i64();
            TCGv_i64 fp2 = tcg_temp_new_i64();

            gen_load_fpr64(ctx, fp0, fs);
            gen_load_fpr64(ctx, fp1, ft);
            gen_load_fpr64(ctx, fp2, fr);
            gen_helper_float_nmsub_ps(fp2, cpu_env, fp0, fp1, fp2);
            tcg_temp_free_i64(fp0);
            tcg_temp_free_i64(fp1);
            gen_store_fpr64(ctx, fp2, fd);
            tcg_temp_free_i64(fp2);
        }
        break;
    default:
        MIPS_INVAL("flt3_arith");
        generate_exception_end(ctx, EXCP_RI);
        return;
    }
}

static void gen_rdhwr(DisasContext *ctx, int rt, int rd, int sel)
{
    TCGv t0;

#if !defined(CONFIG_USER_ONLY)
    /* The Linux kernel will emulate rdhwr if it's not supported natively.
       Therefore only check the ISA in system mode.  */
    check_insn(ctx, ISA_MIPS32R2);
#endif
    t0 = tcg_temp_new();

    switch (rd) {
    case 0:
        gen_helper_rdhwr_cpunum(t0, cpu_env);
        gen_store_gpr(t0, rt);
        break;
    case 1:
        gen_helper_rdhwr_synci_step(t0, cpu_env);
        gen_store_gpr(t0, rt);
        break;
    case 2:
        if (ctx->tb->cflags & CF_USE_ICOUNT) {
            gen_io_start();
        }
        gen_helper_rdhwr_cc(t0, cpu_env);
        if (ctx->tb->cflags & CF_USE_ICOUNT) {
            gen_io_end();
        }
        gen_store_gpr(t0, rt);
        /* Break the TB to be able to take timer interrupts immediately
           after reading count. BS_STOP isn't sufficient, we need to ensure
           we break completely out of translated code.  */
        gen_save_pc(ctx->pc + 4);
        ctx->bstate = BS_EXCP;
        break;
    case 3:
        gen_helper_rdhwr_ccres(t0, cpu_env);
        gen_store_gpr(t0, rt);
        break;
    case 4:
        check_insn(ctx, ISA_MIPS32R6);
        if (sel != 0) {
            /* Performance counter registers are not implemented other than
             * control register 0.
             */
            generate_exception(ctx, EXCP_RI);
        }
        gen_helper_rdhwr_performance(t0, cpu_env);
        gen_store_gpr(t0, rt);
        break;
    case 5:
        check_insn(ctx, ISA_MIPS32R6);
        gen_helper_rdhwr_xnp(t0, cpu_env);
        gen_store_gpr(t0, rt);
        break;
    case 29:
#if defined(CONFIG_USER_ONLY)
        tcg_gen_ld_tl(t0, cpu_env,
                      offsetof(CPUMIPSState, active_tc.CP0_UserLocal));
        gen_store_gpr(t0, rt);
        break;
#else
        if ((ctx->hflags & MIPS_HFLAG_CP0) ||
            (ctx->hflags & MIPS_HFLAG_HWRENA_ULR)) {
            tcg_gen_ld_tl(t0, cpu_env,
                          offsetof(CPUMIPSState, active_tc.CP0_UserLocal));
            gen_store_gpr(t0, rt);
        } else {
            generate_exception_end(ctx, EXCP_RI);
        }
        break;
#endif
    default:            /* Invalid */
        MIPS_INVAL("rdhwr");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
    tcg_temp_free(t0);
}

static inline void clear_branch_hflags(DisasContext *ctx)
{
    ctx->hflags &= ~MIPS_HFLAG_BMASK;
    if (ctx->bstate == BS_NONE) {
        save_cpu_state(ctx, 0);
    } else {
        /* it is not safe to save ctx->hflags as hflags may be changed
           in execution time by the instruction in delay / forbidden slot. */
        tcg_gen_andi_i32(hflags, hflags, ~MIPS_HFLAG_BMASK);
    }
}

static void gen_branch(DisasContext *ctx, int insn_bytes)
{
    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        int proc_hflags = ctx->hflags & MIPS_HFLAG_BMASK;
        /* Branches completion */
        clear_branch_hflags(ctx);
        ctx->bstate = BS_BRANCH;
        /* FIXME: Need to clear can_do_io.  */
        switch (proc_hflags & MIPS_HFLAG_BMASK_BASE) {
        case MIPS_HFLAG_FBNSLOT:
            gen_goto_tb(ctx, 0, ctx->pc + insn_bytes);
            break;
        case MIPS_HFLAG_B:
            /* unconditional branch */
            if (proc_hflags & MIPS_HFLAG_BX) {
                tcg_gen_xori_i32(hflags, hflags, MIPS_HFLAG_M16);
            }
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BL:
            /* blikely taken case */
            gen_goto_tb(ctx, 0, ctx->btarget);
            break;
        case MIPS_HFLAG_BC:
            /* Conditional branch */
            {
                TCGLabel *l1 = gen_new_label();

                tcg_gen_brcondi_tl(TCG_COND_NE, bcond, 0, l1);
                gen_goto_tb(ctx, 1, ctx->pc + insn_bytes);
                gen_set_label(l1);
                gen_goto_tb(ctx, 0, ctx->btarget);
            }
            break;
        case MIPS_HFLAG_BR:
            /* unconditional branch to register */
            if (ctx->insn_flags & (ASE_MIPS16 | ASE_MICROMIPS)) {
                TCGv t0 = tcg_temp_new();
                TCGv_i32 t1 = tcg_temp_new_i32();

                tcg_gen_andi_tl(t0, btarget, 0x1);
                tcg_gen_trunc_tl_i32(t1, t0);
                tcg_temp_free(t0);
                tcg_gen_andi_i32(hflags, hflags, ~(uint32_t)MIPS_HFLAG_M16);
                tcg_gen_shli_i32(t1, t1, MIPS_HFLAG_M16_SHIFT);
                tcg_gen_or_i32(hflags, hflags, t1);
                tcg_temp_free_i32(t1);

                tcg_gen_andi_tl(cpu_PC, btarget, ~(target_ulong)0x1);
            } else {
                tcg_gen_mov_tl(cpu_PC, btarget);
            }
            if (ctx->singlestep_enabled) {
                save_cpu_state(ctx, 0);
                gen_helper_raise_exception_debug(cpu_env);
            }
            tcg_gen_lookup_and_goto_ptr(cpu_PC);
            break;
        default:
            fprintf(stderr, "unknown branch 0x%x\n", proc_hflags);
            abort();
        }
    }
}

/* Compact Branches */
static void gen_compute_compact_branch(DisasContext *ctx, uint32_t opc,
                                       int rs, int rt, int32_t offset)
{
    int bcond_compute = 0;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    int m16_lowbit = (ctx->hflags & MIPS_HFLAG_M16) != 0;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
#ifdef MIPS_DEBUG_DISAS
        LOG_DISAS("Branch in delay / forbidden slot at PC 0x" TARGET_FMT_lx
                  "\n", ctx->pc);
#endif
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }

    /* Load needed operands and calculate btarget */
    switch (opc) {
    /* compact branch */
    case OPC_BOVC: /* OPC_BEQZALC, OPC_BEQC */
    case OPC_BNVC: /* OPC_BNEZALC, OPC_BNEC */
        gen_load_gpr(t0, rs);
        gen_load_gpr(t1, rt);
        bcond_compute = 1;
        ctx->btarget = addr_add(ctx, ctx->pc + 4, offset);
        if (rs <= rt && rs == 0) {
            /* OPC_BEQZALC, OPC_BNEZALC */
            tcg_gen_movi_tl(cpu_gpr[31], ctx->pc + 4 + m16_lowbit);
        }
        break;
    case OPC_BLEZC: /* OPC_BGEZC, OPC_BGEC */
    case OPC_BGTZC: /* OPC_BLTZC, OPC_BLTC */
        gen_load_gpr(t0, rs);
        gen_load_gpr(t1, rt);
        bcond_compute = 1;
        ctx->btarget = addr_add(ctx, ctx->pc + 4, offset);
        break;
    case OPC_BLEZALC: /* OPC_BGEZALC, OPC_BGEUC */
    case OPC_BGTZALC: /* OPC_BLTZALC, OPC_BLTUC */
        if (rs == 0 || rs == rt) {
            /* OPC_BLEZALC, OPC_BGEZALC */
            /* OPC_BGTZALC, OPC_BLTZALC */
            tcg_gen_movi_tl(cpu_gpr[31], ctx->pc + 4 + m16_lowbit);
        }
        gen_load_gpr(t0, rs);
        gen_load_gpr(t1, rt);
        bcond_compute = 1;
        ctx->btarget = addr_add(ctx, ctx->pc + 4, offset);
        break;
    case OPC_BC:
    case OPC_BALC:
        ctx->btarget = addr_add(ctx, ctx->pc + 4, offset);
        break;
    case OPC_BEQZC:
    case OPC_BNEZC:
        if (rs != 0) {
            /* OPC_BEQZC, OPC_BNEZC */
            gen_load_gpr(t0, rs);
            bcond_compute = 1;
            ctx->btarget = addr_add(ctx, ctx->pc + 4, offset);
        } else {
            /* OPC_JIC, OPC_JIALC */
            TCGv tbase = tcg_temp_new();
            TCGv toffset = tcg_temp_new();

            gen_load_gpr(tbase, rt);
            tcg_gen_movi_tl(toffset, offset);
            gen_op_addr_add(ctx, btarget, tbase, toffset);
            tcg_temp_free(tbase);
            tcg_temp_free(toffset);
        }
        break;
    default:
        MIPS_INVAL("Compact branch/jump");
        generate_exception_end(ctx, EXCP_RI);
        goto out;
    }

    if (bcond_compute == 0) {
        /* Uncoditional compact branch */
        switch (opc) {
        case OPC_JIALC:
            tcg_gen_movi_tl(cpu_gpr[31], ctx->pc + 4 + m16_lowbit);
            /* Fallthrough */
        case OPC_JIC:
            ctx->hflags |= MIPS_HFLAG_BR;
            break;
        case OPC_BALC:
            tcg_gen_movi_tl(cpu_gpr[31], ctx->pc + 4 + m16_lowbit);
            /* Fallthrough */
        case OPC_BC:
            ctx->hflags |= MIPS_HFLAG_B;
            break;
        default:
            MIPS_INVAL("Compact branch/jump");
            generate_exception_end(ctx, EXCP_RI);
            goto out;
        }

        /* Generating branch here as compact branches don't have delay slot */
        gen_branch(ctx, 4);
    } else {
        /* Conditional compact branch */
        TCGLabel *fs = gen_new_label();
        save_cpu_state(ctx, 0);

        switch (opc) {
        case OPC_BLEZALC: /* OPC_BGEZALC, OPC_BGEUC */
            if (rs == 0 && rt != 0) {
                /* OPC_BLEZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LE), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BGEZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GE), t1, 0, fs);
            } else {
                /* OPC_BGEUC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_GEU), t0, t1, fs);
            }
            break;
        case OPC_BGTZALC: /* OPC_BLTZALC, OPC_BLTUC */
            if (rs == 0 && rt != 0) {
                /* OPC_BGTZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GT), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BLTZALC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LT), t1, 0, fs);
            } else {
                /* OPC_BLTUC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_LTU), t0, t1, fs);
            }
            break;
        case OPC_BLEZC: /* OPC_BGEZC, OPC_BGEC */
            if (rs == 0 && rt != 0) {
                /* OPC_BLEZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LE), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BGEZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GE), t1, 0, fs);
            } else {
                /* OPC_BGEC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_GE), t0, t1, fs);
            }
            break;
        case OPC_BGTZC: /* OPC_BLTZC, OPC_BLTC */
            if (rs == 0 && rt != 0) {
                /* OPC_BGTZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_GT), t1, 0, fs);
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* OPC_BLTZC */
                tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_LT), t1, 0, fs);
            } else {
                /* OPC_BLTC */
                tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_LT), t0, t1, fs);
            }
            break;
        case OPC_BOVC: /* OPC_BEQZALC, OPC_BEQC */
        case OPC_BNVC: /* OPC_BNEZALC, OPC_BNEC */
            if (rs >= rt) {
                /* OPC_BOVC, OPC_BNVC */
                TCGv t2 = tcg_temp_new();
                TCGv t3 = tcg_temp_new();
                TCGv t4 = tcg_temp_new();
                TCGv input_overflow = tcg_temp_new();

                gen_load_gpr(t0, rs);
                gen_load_gpr(t1, rt);
                tcg_gen_ext32s_tl(t2, t0);
                tcg_gen_setcond_tl(TCG_COND_NE, input_overflow, t2, t0);
                tcg_gen_ext32s_tl(t3, t1);
                tcg_gen_setcond_tl(TCG_COND_NE, t4, t3, t1);
                tcg_gen_or_tl(input_overflow, input_overflow, t4);

                tcg_gen_add_tl(t4, t2, t3);
                tcg_gen_ext32s_tl(t4, t4);
                tcg_gen_xor_tl(t2, t2, t3);
                tcg_gen_xor_tl(t3, t4, t3);
                tcg_gen_andc_tl(t2, t3, t2);
                tcg_gen_setcondi_tl(TCG_COND_LT, t4, t2, 0);
                tcg_gen_or_tl(t4, t4, input_overflow);
                if (opc == OPC_BOVC) {
                    /* OPC_BOVC */
                    tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_NE), t4, 0, fs);
                } else {
                    /* OPC_BNVC */
                    tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_EQ), t4, 0, fs);
                }
                tcg_temp_free(input_overflow);
                tcg_temp_free(t4);
                tcg_temp_free(t3);
                tcg_temp_free(t2);
            } else if (rs < rt && rs == 0) {
                /* OPC_BEQZALC, OPC_BNEZALC */
                if (opc == OPC_BEQZALC) {
                    /* OPC_BEQZALC */
                    tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_EQ), t1, 0, fs);
                } else {
                    /* OPC_BNEZALC */
                    tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_NE), t1, 0, fs);
                }
            } else {
                /* OPC_BEQC, OPC_BNEC */
                if (opc == OPC_BEQC) {
                    /* OPC_BEQC */
                    tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_EQ), t0, t1, fs);
                } else {
                    /* OPC_BNEC */
                    tcg_gen_brcond_tl(tcg_invert_cond(TCG_COND_NE), t0, t1, fs);
                }
            }
            break;
        case OPC_BEQZC:
            tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_EQ), t0, 0, fs);
            break;
        case OPC_BNEZC:
            tcg_gen_brcondi_tl(tcg_invert_cond(TCG_COND_NE), t0, 0, fs);
            break;
        default:
            MIPS_INVAL("Compact conditional branch/jump");
            generate_exception_end(ctx, EXCP_RI);
            goto out;
        }

        /* Generating branch here as compact branches don't have delay slot */
        gen_goto_tb(ctx, 1, ctx->btarget);
        gen_set_label(fs);

        ctx->hflags |= MIPS_HFLAG_FBNSLOT;
    }

out:
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

/* ISA extensions (ASEs) */
/* MIPS16 extension to MIPS32 */

/* MIPS16 major opcodes */
enum {
  M16_OPC_ADDIUSP = 0x00,
  M16_OPC_ADDIUPC = 0x01,
  M16_OPC_B = 0x02,
  M16_OPC_JAL = 0x03,
  M16_OPC_BEQZ = 0x04,
  M16_OPC_BNEQZ = 0x05,
  M16_OPC_SHIFT = 0x06,
  M16_OPC_LD = 0x07,
  M16_OPC_RRIA = 0x08,
  M16_OPC_ADDIU8 = 0x09,
  M16_OPC_SLTI = 0x0a,
  M16_OPC_SLTIU = 0x0b,
  M16_OPC_I8 = 0x0c,
  M16_OPC_LI = 0x0d,
  M16_OPC_CMPI = 0x0e,
  M16_OPC_SD = 0x0f,
  M16_OPC_LB = 0x10,
  M16_OPC_LH = 0x11,
  M16_OPC_LWSP = 0x12,
  M16_OPC_LW = 0x13,
  M16_OPC_LBU = 0x14,
  M16_OPC_LHU = 0x15,
  M16_OPC_LWPC = 0x16,
  M16_OPC_LWU = 0x17,
  M16_OPC_SB = 0x18,
  M16_OPC_SH = 0x19,
  M16_OPC_SWSP = 0x1a,
  M16_OPC_SW = 0x1b,
  M16_OPC_RRR = 0x1c,
  M16_OPC_RR = 0x1d,
  M16_OPC_EXTEND = 0x1e,
  M16_OPC_I64 = 0x1f
};

/* I8 funct field */
enum {
  I8_BTEQZ = 0x0,
  I8_BTNEZ = 0x1,
  I8_SWRASP = 0x2,
  I8_ADJSP = 0x3,
  I8_SVRS = 0x4,
  I8_MOV32R = 0x5,
  I8_MOVR32 = 0x7
};

/* RRR f field */
enum {
  RRR_DADDU = 0x0,
  RRR_ADDU = 0x1,
  RRR_DSUBU = 0x2,
  RRR_SUBU = 0x3
};

/* RR funct field */
enum {
  RR_JR = 0x00,
  RR_SDBBP = 0x01,
  RR_SLT = 0x02,
  RR_SLTU = 0x03,
  RR_SLLV = 0x04,
  RR_BREAK = 0x05,
  RR_SRLV = 0x06,
  RR_SRAV = 0x07,
  RR_DSRL = 0x08,
  RR_CMP = 0x0a,
  RR_NEG = 0x0b,
  RR_AND = 0x0c,
  RR_OR = 0x0d,
  RR_XOR = 0x0e,
  RR_NOT = 0x0f,
  RR_MFHI = 0x10,
  RR_CNVT = 0x11,
  RR_MFLO = 0x12,
  RR_DSRA = 0x13,
  RR_DSLLV = 0x14,
  RR_DSRLV = 0x16,
  RR_DSRAV = 0x17,
  RR_MULT = 0x18,
  RR_MULTU = 0x19,
  RR_DIV = 0x1a,
  RR_DIVU = 0x1b,
  RR_DMULT = 0x1c,
  RR_DMULTU = 0x1d,
  RR_DDIV = 0x1e,
  RR_DDIVU = 0x1f
};

/* I64 funct field */
enum {
  I64_LDSP = 0x0,
  I64_SDSP = 0x1,
  I64_SDRASP = 0x2,
  I64_DADJSP = 0x3,
  I64_LDPC = 0x4,
  I64_DADDIU5 = 0x5,
  I64_DADDIUPC = 0x6,
  I64_DADDIUSP = 0x7
};

/* RR ry field for CNVT */
enum {
  RR_RY_CNVT_ZEB = 0x0,
  RR_RY_CNVT_ZEH = 0x1,
  RR_RY_CNVT_ZEW = 0x2,
  RR_RY_CNVT_SEB = 0x4,
  RR_RY_CNVT_SEH = 0x5,
  RR_RY_CNVT_SEW = 0x6,
};

static int xlat (int r)
{
  static int map[] = { 16, 17, 2, 3, 4, 5, 6, 7 };

  return map[r];
}

static void gen_mips16_save (DisasContext *ctx,
                             int xsregs, int aregs,
                             int do_ra, int do_s0, int do_s1,
                             int framesize)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();
    int args, astatic;

    switch (aregs) {
    case 0:
    case 1:
    case 2:
    case 3:
    case 11:
        args = 0;
        break;
    case 4:
    case 5:
    case 6:
    case 7:
        args = 1;
        break;
    case 8:
    case 9:
    case 10:
        args = 2;
        break;
    case 12:
    case 13:
        args = 3;
        break;
    case 14:
        args = 4;
        break;
    default:
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    switch (args) {
    case 4:
        gen_base_offset_addr(ctx, t0, 29, 12);
        gen_load_gpr(t1, 7);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);
        /* Fall through */
    case 3:
        gen_base_offset_addr(ctx, t0, 29, 8);
        gen_load_gpr(t1, 6);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);
        /* Fall through */
    case 2:
        gen_base_offset_addr(ctx, t0, 29, 4);
        gen_load_gpr(t1, 5);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);
        /* Fall through */
    case 1:
        gen_base_offset_addr(ctx, t0, 29, 0);
        gen_load_gpr(t1, 4);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);
    }

    gen_load_gpr(t0, 29);

#define DECR_AND_STORE(reg) do {                                 \
        tcg_gen_movi_tl(t2, -4);                                 \
        gen_op_addr_add(ctx, t0, t0, t2);                        \
        gen_load_gpr(t1, reg);                                   \
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL); \
    } while (0)

    if (do_ra) {
        DECR_AND_STORE(31);
    }

    switch (xsregs) {
    case 7:
        DECR_AND_STORE(30);
        /* Fall through */
    case 6:
        DECR_AND_STORE(23);
        /* Fall through */
    case 5:
        DECR_AND_STORE(22);
        /* Fall through */
    case 4:
        DECR_AND_STORE(21);
        /* Fall through */
    case 3:
        DECR_AND_STORE(20);
        /* Fall through */
    case 2:
        DECR_AND_STORE(19);
        /* Fall through */
    case 1:
        DECR_AND_STORE(18);
    }

    if (do_s1) {
        DECR_AND_STORE(17);
    }
    if (do_s0) {
        DECR_AND_STORE(16);
    }

    switch (aregs) {
    case 0:
    case 4:
    case 8:
    case 12:
    case 14:
        astatic = 0;
        break;
    case 1:
    case 5:
    case 9:
    case 13:
        astatic = 1;
        break;
    case 2:
    case 6:
    case 10:
        astatic = 2;
        break;
    case 3:
    case 7:
        astatic = 3;
        break;
    case 11:
        astatic = 4;
        break;
    default:
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    if (astatic > 0) {
        DECR_AND_STORE(7);
        if (astatic > 1) {
            DECR_AND_STORE(6);
            if (astatic > 2) {
                DECR_AND_STORE(5);
                if (astatic > 3) {
                    DECR_AND_STORE(4);
                }
            }
        }
    }
#undef DECR_AND_STORE

    tcg_gen_movi_tl(t2, -framesize);
    gen_op_addr_add(ctx, cpu_gpr[29], cpu_gpr[29], t2);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static void gen_mips16_restore (DisasContext *ctx,
                                int xsregs, int aregs,
                                int do_ra, int do_s0, int do_s1,
                                int framesize)
{
    int astatic;
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    TCGv t2 = tcg_temp_new();

    tcg_gen_movi_tl(t2, framesize);
    gen_op_addr_add(ctx, t0, cpu_gpr[29], t2);

#define DECR_AND_LOAD(reg) do {                            \
        tcg_gen_movi_tl(t2, -4);                           \
        gen_op_addr_add(ctx, t0, t0, t2);                  \
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TESL); \
        gen_store_gpr(t1, reg);                            \
    } while (0)

    if (do_ra) {
        DECR_AND_LOAD(31);
    }

    switch (xsregs) {
    case 7:
        DECR_AND_LOAD(30);
        /* Fall through */
    case 6:
        DECR_AND_LOAD(23);
        /* Fall through */
    case 5:
        DECR_AND_LOAD(22);
        /* Fall through */
    case 4:
        DECR_AND_LOAD(21);
        /* Fall through */
    case 3:
        DECR_AND_LOAD(20);
        /* Fall through */
    case 2:
        DECR_AND_LOAD(19);
        /* Fall through */
    case 1:
        DECR_AND_LOAD(18);
    }

    if (do_s1) {
        DECR_AND_LOAD(17);
    }
    if (do_s0) {
        DECR_AND_LOAD(16);
    }

    switch (aregs) {
    case 0:
    case 4:
    case 8:
    case 12:
    case 14:
        astatic = 0;
        break;
    case 1:
    case 5:
    case 9:
    case 13:
        astatic = 1;
        break;
    case 2:
    case 6:
    case 10:
        astatic = 2;
        break;
    case 3:
    case 7:
        astatic = 3;
        break;
    case 11:
        astatic = 4;
        break;
    default:
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    if (astatic > 0) {
        DECR_AND_LOAD(7);
        if (astatic > 1) {
            DECR_AND_LOAD(6);
            if (astatic > 2) {
                DECR_AND_LOAD(5);
                if (astatic > 3) {
                    DECR_AND_LOAD(4);
                }
            }
        }
    }
#undef DECR_AND_LOAD

    tcg_gen_movi_tl(t2, framesize);
    gen_op_addr_add(ctx, cpu_gpr[29], cpu_gpr[29], t2);
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(t2);
}

static void gen_addiupc (DisasContext *ctx, int rx, int imm,
                         int is_64_bit, int extended)
{
    TCGv t0;

    if (extended && (ctx->hflags & MIPS_HFLAG_BMASK)) {
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    t0 = tcg_temp_new();

    tcg_gen_movi_tl(t0, pc_relative_pc(ctx));
    tcg_gen_addi_tl(cpu_gpr[rx], t0, imm);
    if (!is_64_bit) {
        tcg_gen_ext32s_tl(cpu_gpr[rx], cpu_gpr[rx]);
    }

    tcg_temp_free(t0);
}

static void gen_cache_operation(DisasContext *ctx, uint32_t op, int base,
                                int16_t offset)
{
    TCGv_i32 t0 = tcg_const_i32(op);
    TCGv t1 = tcg_temp_new();
    gen_base_offset_addr(ctx, t1, base, offset);
    gen_helper_cache(cpu_env, t1, t0);
}

#if defined(TARGET_MIPS64)
static void decode_i64_mips16 (DisasContext *ctx,
                               int ry, int funct, int16_t offset,
                               int extended)
{
    switch (funct) {
    case I64_LDSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 3;
        gen_ld(ctx, OPC_LD, ry, 29, offset);
        break;
    case I64_SDSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 3;
        gen_st(ctx, OPC_SD, ry, 29, offset);
        break;
    case I64_SDRASP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : (ctx->opcode & 0xff) << 3;
        gen_st(ctx, OPC_SD, 31, 29, offset);
        break;
    case I64_DADJSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : ((int8_t)ctx->opcode) << 3;
        gen_arith_imm(ctx, OPC_DADDIU, 29, 29, offset);
        break;
    case I64_LDPC:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        if (extended && (ctx->hflags & MIPS_HFLAG_BMASK)) {
            generate_exception_end(ctx, EXCP_RI);
        } else {
            offset = extended ? offset : offset << 3;
            gen_ld(ctx, OPC_LDPC, ry, 0, offset);
        }
        break;
    case I64_DADDIU5:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : ((int8_t)(offset << 3)) >> 3;
        gen_arith_imm(ctx, OPC_DADDIU, ry, ry, offset);
        break;
    case I64_DADDIUPC:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 2;
        gen_addiupc(ctx, ry, offset, 1, extended);
        break;
    case I64_DADDIUSP:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        offset = extended ? offset : offset << 2;
        gen_arith_imm(ctx, OPC_DADDIU, ry, 29, offset);
        break;
    }
}
#endif

static int decode_extended_mips16_opc (CPUMIPSState *env, DisasContext *ctx)
{
    int extend = cpu_lduw_code(env, ctx->pc + 2);
    int op, rx, ry, funct, sa;
    int16_t imm, offset;

    ctx->opcode = (ctx->opcode << 16) | extend;
    op = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 22) & 0x1f;
    funct = (ctx->opcode >> 8) & 0x7;
    rx = xlat((ctx->opcode >> 8) & 0x7);
    ry = xlat((ctx->opcode >> 5) & 0x7);
    offset = imm = (int16_t) (((ctx->opcode >> 16) & 0x1f) << 11
                              | ((ctx->opcode >> 21) & 0x3f) << 5
                              | (ctx->opcode & 0x1f));

    /* The extended opcodes cleverly reuse the opcodes from their 16-bit
       counterparts.  */
    switch (op) {
    case M16_OPC_ADDIUSP:
        gen_arith_imm(ctx, OPC_ADDIU, rx, 29, imm);
        break;
    case M16_OPC_ADDIUPC:
        gen_addiupc(ctx, rx, imm, 0, 1);
        break;
    case M16_OPC_B:
        gen_compute_branch(ctx, OPC_BEQ, 4, 0, 0, offset << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_BEQZ:
        gen_compute_branch(ctx, OPC_BEQ, 4, rx, 0, offset << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_BNEQZ:
        gen_compute_branch(ctx, OPC_BNE, 4, rx, 0, offset << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_SHIFT:
        switch (ctx->opcode & 0x3) {
        case 0x0:
            gen_shift_imm(ctx, OPC_SLL, rx, ry, sa);
            break;
        case 0x1:
#if defined(TARGET_MIPS64)
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSLL, rx, ry, sa);
#else
            generate_exception_end(ctx, EXCP_RI);
#endif
            break;
        case 0x2:
            gen_shift_imm(ctx, OPC_SRL, rx, ry, sa);
            break;
        case 0x3:
            gen_shift_imm(ctx, OPC_SRA, rx, ry, sa);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LD, ry, rx, offset);
        break;
#endif
    case M16_OPC_RRIA:
        imm = ctx->opcode & 0xf;
        imm = imm | ((ctx->opcode >> 20) & 0x7f) << 4;
        imm = imm | ((ctx->opcode >> 16) & 0xf) << 11;
        imm = (int16_t) (imm << 1) >> 1;
        if ((ctx->opcode >> 4) & 0x1) {
#if defined(TARGET_MIPS64)
            check_mips_64(ctx);
            gen_arith_imm(ctx, OPC_DADDIU, ry, rx, imm);
#else
            generate_exception_end(ctx, EXCP_RI);
#endif
        } else {
            gen_arith_imm(ctx, OPC_ADDIU, ry, rx, imm);
        }
        break;
    case M16_OPC_ADDIU8:
        gen_arith_imm(ctx, OPC_ADDIU, rx, rx, imm);
        break;
    case M16_OPC_SLTI:
        gen_slt_imm(ctx, OPC_SLTI, 24, rx, imm);
        break;
    case M16_OPC_SLTIU:
        gen_slt_imm(ctx, OPC_SLTIU, 24, rx, imm);
        break;
    case M16_OPC_I8:
        switch (funct) {
        case I8_BTEQZ:
            gen_compute_branch(ctx, OPC_BEQ, 4, 24, 0, offset << 1, 0);
            break;
        case I8_BTNEZ:
            gen_compute_branch(ctx, OPC_BNE, 4, 24, 0, offset << 1, 0);
            break;
        case I8_SWRASP:
            gen_st(ctx, OPC_SW, 31, 29, imm);
            break;
        case I8_ADJSP:
            gen_arith_imm(ctx, OPC_ADDIU, 29, 29, imm);
            break;
        case I8_SVRS:
            check_insn(ctx, ISA_MIPS32);
            {
                int xsregs = (ctx->opcode >> 24) & 0x7;
                int aregs = (ctx->opcode >> 16) & 0xf;
                int do_ra = (ctx->opcode >> 6) & 0x1;
                int do_s0 = (ctx->opcode >> 5) & 0x1;
                int do_s1 = (ctx->opcode >> 4) & 0x1;
                int framesize = (((ctx->opcode >> 20) & 0xf) << 4
                                 | (ctx->opcode & 0xf)) << 3;

                if (ctx->opcode & (1 << 7)) {
                    gen_mips16_save(ctx, xsregs, aregs,
                                    do_ra, do_s0, do_s1,
                                    framesize);
                } else {
                    gen_mips16_restore(ctx, xsregs, aregs,
                                       do_ra, do_s0, do_s1,
                                       framesize);
                }
            }
            break;
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case M16_OPC_LI:
        tcg_gen_movi_tl(cpu_gpr[rx], (uint16_t) imm);
        break;
    case M16_OPC_CMPI:
        tcg_gen_xori_tl(cpu_gpr[24], cpu_gpr[rx], (uint16_t) imm);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_SD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_st(ctx, OPC_SD, ry, rx, offset);
        break;
#endif
    case M16_OPC_LB:
        gen_ld(ctx, OPC_LB, ry, rx, offset);
        break;
    case M16_OPC_LH:
        gen_ld(ctx, OPC_LH, ry, rx, offset);
        break;
    case M16_OPC_LWSP:
        gen_ld(ctx, OPC_LW, rx, 29, offset);
        break;
    case M16_OPC_LW:
        gen_ld(ctx, OPC_LW, ry, rx, offset);
        break;
    case M16_OPC_LBU:
        gen_ld(ctx, OPC_LBU, ry, rx, offset);
        break;
    case M16_OPC_LHU:
        gen_ld(ctx, OPC_LHU, ry, rx, offset);
        break;
    case M16_OPC_LWPC:
        gen_ld(ctx, OPC_LWPC, rx, 0, offset);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LWU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LWU, ry, rx, offset);
        break;
#endif
    case M16_OPC_SB:
        gen_st(ctx, OPC_SB, ry, rx, offset);
        break;
    case M16_OPC_SH:
        gen_st(ctx, OPC_SH, ry, rx, offset);
        break;
    case M16_OPC_SWSP:
        gen_st(ctx, OPC_SW, rx, 29, offset);
        break;
    case M16_OPC_SW:
        gen_st(ctx, OPC_SW, ry, rx, offset);
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_I64:
        decode_i64_mips16(ctx, ry, funct, offset, 1);
        break;
#endif
    default:
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    return 4;
}

static inline bool is_uhi(int sdbbp_code)
{
#ifdef CONFIG_USER_ONLY
    return false;
#else
    return semihosting_enabled() && sdbbp_code == 1;
#endif
}

static int decode_mips16_opc (CPUMIPSState *env, DisasContext *ctx)
{
    int rx, ry;
    int sa;
    int op, cnvt_op, op1, offset;
    int funct;
    int n_bytes;

    op = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 2) & 0x7;
    sa = sa == 0 ? 8 : sa;
    rx = xlat((ctx->opcode >> 8) & 0x7);
    cnvt_op = (ctx->opcode >> 5) & 0x7;
    ry = xlat((ctx->opcode >> 5) & 0x7);
    op1 = offset = ctx->opcode & 0x1f;

    n_bytes = 2;

    switch (op) {
    case M16_OPC_ADDIUSP:
        {
            int16_t imm = ((uint8_t) ctx->opcode) << 2;

            gen_arith_imm(ctx, OPC_ADDIU, rx, 29, imm);
        }
        break;
    case M16_OPC_ADDIUPC:
        gen_addiupc(ctx, rx, ((uint8_t) ctx->opcode) << 2, 0, 0);
        break;
    case M16_OPC_B:
        offset = (ctx->opcode & 0x7ff) << 1;
        offset = (int16_t)(offset << 4) >> 4;
        gen_compute_branch(ctx, OPC_BEQ, 2, 0, 0, offset, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_JAL:
        offset = cpu_lduw_code(env, ctx->pc + 2);
        offset = (((ctx->opcode & 0x1f) << 21)
                  | ((ctx->opcode >> 5) & 0x1f) << 16
                  | offset) << 2;
        op = ((ctx->opcode >> 10) & 0x1) ? OPC_JALX : OPC_JAL;
        gen_compute_branch(ctx, op, 4, rx, ry, offset, 2);
        n_bytes = 4;
        break;
    case M16_OPC_BEQZ:
        gen_compute_branch(ctx, OPC_BEQ, 2, rx, 0,
                           ((int8_t)ctx->opcode) << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_BNEQZ:
        gen_compute_branch(ctx, OPC_BNE, 2, rx, 0,
                           ((int8_t)ctx->opcode) << 1, 0);
        /* No delay slot, so just process as a normal instruction */
        break;
    case M16_OPC_SHIFT:
        switch (ctx->opcode & 0x3) {
        case 0x0:
            gen_shift_imm(ctx, OPC_SLL, rx, ry, sa);
            break;
        case 0x1:
#if defined(TARGET_MIPS64)
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSLL, rx, ry, sa);
#else
            generate_exception_end(ctx, EXCP_RI);
#endif
            break;
        case 0x2:
            gen_shift_imm(ctx, OPC_SRL, rx, ry, sa);
            break;
        case 0x3:
            gen_shift_imm(ctx, OPC_SRA, rx, ry, sa);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_LD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LD, ry, rx, offset << 3);
        break;
#endif
    case M16_OPC_RRIA:
        {
            int16_t imm = (int8_t)((ctx->opcode & 0xf) << 4) >> 4;

            if ((ctx->opcode >> 4) & 1) {
#if defined(TARGET_MIPS64)
                check_insn(ctx, ISA_MIPS3);
                check_mips_64(ctx);
                gen_arith_imm(ctx, OPC_DADDIU, ry, rx, imm);
#else
                generate_exception_end(ctx, EXCP_RI);
#endif
            } else {
                gen_arith_imm(ctx, OPC_ADDIU, ry, rx, imm);
            }
        }
        break;
    case M16_OPC_ADDIU8:
        {
            int16_t imm = (int8_t) ctx->opcode;

            gen_arith_imm(ctx, OPC_ADDIU, rx, rx, imm);
        }
        break;
    case M16_OPC_SLTI:
        {
            int16_t imm = (uint8_t) ctx->opcode;
            gen_slt_imm(ctx, OPC_SLTI, 24, rx, imm);
        }
        break;
    case M16_OPC_SLTIU:
        {
            int16_t imm = (uint8_t) ctx->opcode;
            gen_slt_imm(ctx, OPC_SLTIU, 24, rx, imm);
        }
        break;
    case M16_OPC_I8:
        {
            int reg32;

            funct = (ctx->opcode >> 8) & 0x7;
            switch (funct) {
            case I8_BTEQZ:
                gen_compute_branch(ctx, OPC_BEQ, 2, 24, 0,
                                   ((int8_t)ctx->opcode) << 1, 0);
                break;
            case I8_BTNEZ:
                gen_compute_branch(ctx, OPC_BNE, 2, 24, 0,
                                   ((int8_t)ctx->opcode) << 1, 0);
                break;
            case I8_SWRASP:
                gen_st(ctx, OPC_SW, 31, 29, (ctx->opcode & 0xff) << 2);
                break;
            case I8_ADJSP:
                gen_arith_imm(ctx, OPC_ADDIU, 29, 29,
                              ((int8_t)ctx->opcode) << 3);
                break;
            case I8_SVRS:
                check_insn(ctx, ISA_MIPS32);
                {
                    int do_ra = ctx->opcode & (1 << 6);
                    int do_s0 = ctx->opcode & (1 << 5);
                    int do_s1 = ctx->opcode & (1 << 4);
                    int framesize = ctx->opcode & 0xf;

                    if (framesize == 0) {
                        framesize = 128;
                    } else {
                        framesize = framesize << 3;
                    }

                    if (ctx->opcode & (1 << 7)) {
                        gen_mips16_save(ctx, 0, 0,
                                        do_ra, do_s0, do_s1, framesize);
                    } else {
                        gen_mips16_restore(ctx, 0, 0,
                                           do_ra, do_s0, do_s1, framesize);
                    }
                }
                break;
            case I8_MOV32R:
                {
                    int rz = xlat(ctx->opcode & 0x7);

                    reg32 = (((ctx->opcode >> 3) & 0x3) << 3) |
                        ((ctx->opcode >> 5) & 0x7);
                    gen_arith(ctx, OPC_ADDU, reg32, rz, 0);
                }
                break;
            case I8_MOVR32:
                reg32 = ctx->opcode & 0x1f;
                gen_arith(ctx, OPC_ADDU, ry, reg32, 0);
                break;
            default:
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
        }
        break;
    case M16_OPC_LI:
        {
            int16_t imm = (uint8_t) ctx->opcode;

            gen_arith_imm(ctx, OPC_ADDIU, rx, 0, imm);
        }
        break;
    case M16_OPC_CMPI:
        {
            int16_t imm = (uint8_t) ctx->opcode;
            gen_logic_imm(ctx, OPC_XORI, 24, rx, imm);
        }
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_SD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_st(ctx, OPC_SD, ry, rx, offset << 3);
        break;
#endif
    case M16_OPC_LB:
        gen_ld(ctx, OPC_LB, ry, rx, offset);
        break;
    case M16_OPC_LH:
        gen_ld(ctx, OPC_LH, ry, rx, offset << 1);
        break;
    case M16_OPC_LWSP:
        gen_ld(ctx, OPC_LW, rx, 29, ((uint8_t)ctx->opcode) << 2);
        break;
    case M16_OPC_LW:
        gen_ld(ctx, OPC_LW, ry, rx, offset << 2);
        break;
    case M16_OPC_LBU:
        gen_ld(ctx, OPC_LBU, ry, rx, offset);
        break;
    case M16_OPC_LHU:
        gen_ld(ctx, OPC_LHU, ry, rx, offset << 1);
        break;
    case M16_OPC_LWPC:
        gen_ld(ctx, OPC_LWPC, rx, 0, ((uint8_t)ctx->opcode) << 2);
        break;
#if defined (TARGET_MIPS64)
    case M16_OPC_LWU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, OPC_LWU, ry, rx, offset << 2);
        break;
#endif
    case M16_OPC_SB:
        gen_st(ctx, OPC_SB, ry, rx, offset);
        break;
    case M16_OPC_SH:
        gen_st(ctx, OPC_SH, ry, rx, offset << 1);
        break;
    case M16_OPC_SWSP:
        gen_st(ctx, OPC_SW, rx, 29, ((uint8_t)ctx->opcode) << 2);
        break;
    case M16_OPC_SW:
        gen_st(ctx, OPC_SW, ry, rx, offset << 2);
        break;
    case M16_OPC_RRR:
        {
            int rz = xlat((ctx->opcode >> 2) & 0x7);
            int mips32_op;

            switch (ctx->opcode & 0x3) {
            case RRR_ADDU:
                mips32_op = OPC_ADDU;
                break;
            case RRR_SUBU:
                mips32_op = OPC_SUBU;
                break;
#if defined(TARGET_MIPS64)
            case RRR_DADDU:
                mips32_op = OPC_DADDU;
                check_insn(ctx, ISA_MIPS3);
                check_mips_64(ctx);
                break;
            case RRR_DSUBU:
                mips32_op = OPC_DSUBU;
                check_insn(ctx, ISA_MIPS3);
                check_mips_64(ctx);
                break;
#endif
            default:
                generate_exception_end(ctx, EXCP_RI);
                goto done;
            }

            gen_arith(ctx, mips32_op, rz, rx, ry);
        done:
            ;
        }
        break;
    case M16_OPC_RR:
        switch (op1) {
        case RR_JR:
            {
                int nd = (ctx->opcode >> 7) & 0x1;
                int link = (ctx->opcode >> 6) & 0x1;
                int ra = (ctx->opcode >> 5) & 0x1;

                if (nd) {
                    check_insn(ctx, ISA_MIPS32);
                }

                if (link) {
                    op = OPC_JALR;
                } else {
                    op = OPC_JR;
                }

                gen_compute_branch(ctx, op, 2, ra ? 31 : rx, 31, 0,
                                   (nd ? 0 : 2));
            }
            break;
        case RR_SDBBP:
            if (is_uhi(extract32(ctx->opcode, 5, 6))) {
                gen_helper_do_semihosting(cpu_env);
            } else {
                /* XXX: not clear which exception should be raised
                 *      when in debug mode...
                 */
                check_insn(ctx, ISA_MIPS32);
                generate_exception_end(ctx, EXCP_DBp);
            }
            break;
        case RR_SLT:
            gen_slt(ctx, OPC_SLT, 24, rx, ry);
            break;
        case RR_SLTU:
            gen_slt(ctx, OPC_SLTU, 24, rx, ry);
            break;
        case RR_BREAK:
            generate_exception_end(ctx, EXCP_BREAK);
            break;
        case RR_SLLV:
            gen_shift(ctx, OPC_SLLV, ry, rx, ry);
            break;
        case RR_SRLV:
            gen_shift(ctx, OPC_SRLV, ry, rx, ry);
            break;
        case RR_SRAV:
            gen_shift(ctx, OPC_SRAV, ry, rx, ry);
            break;
#if defined (TARGET_MIPS64)
        case RR_DSRL:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSRL, ry, ry, sa);
            break;
#endif
        case RR_CMP:
            gen_logic(ctx, OPC_XOR, 24, rx, ry);
            break;
        case RR_NEG:
            gen_arith(ctx, OPC_SUBU, rx, 0, ry);
            break;
        case RR_AND:
            gen_logic(ctx, OPC_AND, rx, rx, ry);
            break;
        case RR_OR:
            gen_logic(ctx, OPC_OR, rx, rx, ry);
            break;
        case RR_XOR:
            gen_logic(ctx, OPC_XOR, rx, rx, ry);
            break;
        case RR_NOT:
            gen_logic(ctx, OPC_NOR, rx, ry, 0);
            break;
        case RR_MFHI:
            gen_HILO(ctx, OPC_MFHI, 0, rx);
            break;
        case RR_CNVT:
            check_insn(ctx, ISA_MIPS32);
            switch (cnvt_op) {
            case RR_RY_CNVT_ZEB:
                tcg_gen_ext8u_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_ZEH:
                tcg_gen_ext16u_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_SEB:
                tcg_gen_ext8s_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_SEH:
                tcg_gen_ext16s_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
#if defined (TARGET_MIPS64)
            case RR_RY_CNVT_ZEW:
                check_insn(ctx, ISA_MIPS64);
                check_mips_64(ctx);
                tcg_gen_ext32u_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
            case RR_RY_CNVT_SEW:
                check_insn(ctx, ISA_MIPS64);
                check_mips_64(ctx);
                tcg_gen_ext32s_tl(cpu_gpr[rx], cpu_gpr[rx]);
                break;
#endif
            default:
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
            break;
        case RR_MFLO:
            gen_HILO(ctx, OPC_MFLO, 0, rx);
            break;
#if defined (TARGET_MIPS64)
        case RR_DSRA:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, OPC_DSRA, ry, ry, sa);
            break;
        case RR_DSLLV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, OPC_DSLLV, ry, rx, ry);
            break;
        case RR_DSRLV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, OPC_DSRLV, ry, rx, ry);
            break;
        case RR_DSRAV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, OPC_DSRAV, ry, rx, ry);
            break;
#endif
        case RR_MULT:
            gen_muldiv(ctx, OPC_MULT, 0, rx, ry);
            break;
        case RR_MULTU:
            gen_muldiv(ctx, OPC_MULTU, 0, rx, ry);
            break;
        case RR_DIV:
            gen_muldiv(ctx, OPC_DIV, 0, rx, ry);
            break;
        case RR_DIVU:
            gen_muldiv(ctx, OPC_DIVU, 0, rx, ry);
            break;
#if defined (TARGET_MIPS64)
        case RR_DMULT:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DMULT, 0, rx, ry);
            break;
        case RR_DMULTU:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DMULTU, 0, rx, ry);
            break;
        case RR_DDIV:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DDIV, 0, rx, ry);
            break;
        case RR_DDIVU:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_muldiv(ctx, OPC_DDIVU, 0, rx, ry);
            break;
#endif
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case M16_OPC_EXTEND:
        decode_extended_mips16_opc(env, ctx);
        n_bytes = 4;
        break;
#if defined(TARGET_MIPS64)
    case M16_OPC_I64:
        funct = (ctx->opcode >> 8) & 0x7;
        decode_i64_mips16(ctx, ry, funct, offset, 0);
        break;
#endif
    default:
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    return n_bytes;
}

/* microMIPS extension to MIPS32/MIPS64 */

/*
 * microMIPS32/microMIPS64 major opcodes
 *
 * 1. MIPS Architecture for Programmers Volume II-B:
 *      The microMIPS32 Instruction Set (Revision 3.05)
 *
 *    Table 6.2 microMIPS32 Encoding of Major Opcode Field
 *
 * 2. MIPS Architecture For Programmers Volume II-A:
 *      The MIPS64 Instruction Set (Revision 3.51)
 */

enum {
    POOL32A = 0x00,
    POOL16A = 0x01,
    LBU16 = 0x02,
    MOVE16 = 0x03,
    ADDI32 = 0x04,
    R6_LUI = 0x04,
    AUI = 0x04,
    LBU32 = 0x05,
    SB32 = 0x06,
    LB32 = 0x07,

    POOL32B = 0x08,
    POOL16B = 0x09,
    LHU16 = 0x0a,
    ANDI16 = 0x0b,
    ADDIU32 = 0x0c,
    LHU32 = 0x0d,
    SH32 = 0x0e,
    LH32 = 0x0f,

    POOL32I = 0x10,
    POOL16C = 0x11,
    LWSP16 = 0x12,
    POOL16D = 0x13,
    ORI32 = 0x14,
    POOL32F = 0x15,
    POOL32S = 0x16,  /* MIPS64 */
    DADDIU32 = 0x17, /* MIPS64 */

    POOL32C = 0x18,
    LWGP16 = 0x19,
    LW16 = 0x1a,
    POOL16E = 0x1b,
    XORI32 = 0x1c,
    JALS32 = 0x1d,
    BOVC = 0x1d,
    BEQC = 0x1d,
    BEQZALC = 0x1d,
    ADDIUPC = 0x1e,
    PCREL = 0x1e,
    BNVC = 0x1f,
    BNEC = 0x1f,
    BNEZALC = 0x1f,

    R6_BEQZC = 0x20,
    JIC = 0x20,
    POOL16F = 0x21,
    SB16 = 0x22,
    BEQZ16 = 0x23,
    BEQZC16 = 0x23,
    SLTI32 = 0x24,
    BEQ32 = 0x25,
    BC = 0x25,
    SWC132 = 0x26,
    LWC132 = 0x27,

    /* 0x29 is reserved */
    RES_29 = 0x29,
    R6_BNEZC = 0x28,
    JIALC = 0x28,
    SH16 = 0x2a,
    BNEZ16 = 0x2b,
    BNEZC16 = 0x2b,
    SLTIU32 = 0x2c,
    BNE32 = 0x2d,
    BALC = 0x2d,
    SDC132 = 0x2e,
    LDC132 = 0x2f,

    /* 0x31 is reserved */
    RES_31 = 0x31,
    BLEZALC = 0x30,
    BGEZALC = 0x30,
    BGEUC = 0x30,
    SWSP16 = 0x32,
    B16 = 0x33,
    BC16 = 0x33,
    ANDI32 = 0x34,
    J32 = 0x35,
    BGTZC = 0x35,
    BLTZC = 0x35,
    BLTC = 0x35,
    SD32 = 0x36, /* MIPS64 */
    LD32 = 0x37, /* MIPS64 */

    /* 0x39 is reserved */
    RES_39 = 0x39,
    BGTZALC = 0x38,
    BLTZALC = 0x38,
    BLTUC = 0x38,
    SW16 = 0x3a,
    LI16 = 0x3b,
    JALX32 = 0x3c,
    JAL32 = 0x3d,
    BLEZC = 0x3d,
    BGEZC = 0x3d,
    BGEC = 0x3d,
    SW32 = 0x3e,
    LW32 = 0x3f
};

/* PCREL Instructions perform PC-Relative address calculation. bits 20..16 */
enum {
    ADDIUPC_00 = 0x00,
    ADDIUPC_07 = 0x07,
    AUIPC = 0x1e,
    ALUIPC = 0x1f,
    LWPC_08 = 0x08,
    LWPC_0F = 0x0F,
};

/* POOL32A encoding of minor opcode field */

enum {
    /* These opcodes are distinguished only by bits 9..6; those bits are
     * what are recorded below. */
    SLL32 = 0x0,
    SRL32 = 0x1,
    SRA = 0x2,
    ROTR = 0x3,
    SELEQZ = 0x5,
    SELNEZ = 0x6,
    R6_RDHWR = 0x7,

    SLLV = 0x0,
    SRLV = 0x1,
    SRAV = 0x2,
    ROTRV = 0x3,
    ADD = 0x4,
    ADDU32 = 0x5,
    SUB = 0x6,
    SUBU32 = 0x7,
    MUL = 0x8,
    AND = 0x9,
    OR32 = 0xa,
    NOR = 0xb,
    XOR32 = 0xc,
    SLT = 0xd,
    SLTU = 0xe,

    MOVN = 0x0,
    R6_MUL  = 0x0,
    MOVZ = 0x1,
    MUH  = 0x1,
    MULU = 0x2,
    MUHU = 0x3,
    LWXS = 0x4,
    R6_DIV  = 0x4,
    MOD  = 0x5,
    R6_DIVU = 0x6,
    MODU = 0x7,

    /* The following can be distinguished by their lower 6 bits. */
    BREAK32 = 0x07,
    INS = 0x0c,
    LSA = 0x0f,
    ALIGN = 0x1f,
    EXT = 0x2c,
    POOL32AXF = 0x3c,
    SIGRIE = 0x3f
};

/* POOL32AXF encoding of minor opcode field extension */

/*
 * 1. MIPS Architecture for Programmers Volume II-B:
 *      The microMIPS32 Instruction Set (Revision 3.05)
 *
 *    Table 6.5 POOL32Axf Encoding of Minor Opcode Extension Field
 *
 * 2. MIPS Architecture for Programmers VolumeIV-e:
 *      The MIPS DSP Application-Specific Extension
 *        to the microMIPS32 Architecture (Revision 2.34)
 *
 *    Table 5.5 POOL32Axf Encoding of Minor Opcode Extension Field
 */

enum {
    /* bits 11..6 */
    TEQ = 0x00,
    TGE = 0x08,
    TGEU = 0x10,
    TLT = 0x20,
    TLTU = 0x28,
    TNE = 0x30,

    MFC0 = 0x03,
    MTC0 = 0x0b,

    /* begin of microMIPS32 DSP */

    /* bits 13..12 for 0x01 */
    MFHI_ACC = 0x0,
    MFLO_ACC = 0x1,
    MTHI_ACC = 0x2,
    MTLO_ACC = 0x3,

    /* bits 13..12 for 0x2a */
    MADD_ACC = 0x0,
    MADDU_ACC = 0x1,
    MSUB_ACC = 0x2,
    MSUBU_ACC = 0x3,

    /* bits 13..12 for 0x32 */
    MULT_ACC = 0x0,
    MULTU_ACC = 0x1,

    /* end of microMIPS32 DSP */

    /* bits 15..12 for 0x2c */
    BITSWAP = 0x0,
    SEB = 0x2,
    SEH = 0x3,
    CLO = 0x4,
    CLZ = 0x5,
    RDHWR = 0x6,
    WSBH = 0x7,
    MULT = 0x8,
    MULTU = 0x9,
    DIV = 0xa,
    DIVU = 0xb,
    MADD = 0xc,
    MADDU = 0xd,
    MSUB = 0xe,
    MSUBU = 0xf,

    /* bits 15..12 for 0x34 */
    MFC2 = 0x4,
    MTC2 = 0x5,
    MFHC2 = 0x8,
    MTHC2 = 0x9,
    CFC2 = 0xc,
    CTC2 = 0xd,

    /* bits 15..12 for 0x3c */
    JALR = 0x0,
    JR = 0x0,                   /* alias */
    JALRC = 0x0,
    JRC = 0x0,
    JALR_HB = 0x1,
    JALRC_HB = 0x1,
    JALRS = 0x4,
    JALRS_HB = 0x5,

    /* bits 15..12 for 0x05 */
    RDPGPR = 0xe,
    WRPGPR = 0xf,

    /* bits 15..12 for 0x0d */
    TLBP = 0x0,
    TLBR = 0x1,
    TLBWI = 0x2,
    TLBWR = 0x3,
    TLBINV = 0x4,
    TLBINVF = 0x5,
    WAIT = 0x9,
    IRET = 0xd,
    DERET = 0xe,
    ERET = 0xf,

    /* bits 15..12 for 0x15 */
    DMT = 0x0,
    DVPE = 0x1,
    EMT = 0x2,
    EVPE = 0x3,

    /* bits 15..12 for 0x1d */
    DI = 0x4,
    EI = 0x5,

    /* bits 15..12 for 0x2d */
    SYNC = 0x6,
    SYSCALL = 0x8,
    SDBBP = 0xd,

    /* bits 15..12 for 0x35 */
    MFHI32 = 0x0,
    MFLO32 = 0x1,
    MTHI32 = 0x2,
    MTLO32 = 0x3,
};

/* POOL32B encoding of minor opcode field (bits 15..12) */

enum {
    LWC2 = 0x0,
    LWP = 0x1,
    LDP = 0x4,
    LWM32 = 0x5,
    CACHE = 0x6,
    LDM = 0x7,
    SWC2 = 0x8,
    SWP = 0x9,
    SDP = 0xc,
    SWM32 = 0xd,
    SDM = 0xf
};

/* POOL32C encoding of minor opcode field (bits 15..12) */

enum {
    LWL = 0x0,
    SWL = 0x8,
    LWR = 0x1,
    SWR = 0x9,
    PREF = 0x2,
    ST_EVA = 0xa,
    LL = 0x3,
    SC = 0xb,
    LDL = 0x4,
    SDL = 0xc,
    LDR = 0x5,
    SDR = 0xd,
    LD_EVA = 0x6,
    LWU = 0xe,
    LLD = 0x7,
    SCD = 0xf
};

/* POOL32C LD-EVA encoding of minor opcode field (bits 11..9) */

enum {
    LBUE = 0x0,
    LHUE = 0x1,
    LWLE = 0x2,
    LWRE = 0x3,
    LBE = 0x4,
    LHE = 0x5,
    LLE = 0x6,
    LWE = 0x7,
};

/* POOL32C ST-EVA encoding of minor opcode field (bits 11..9) */

enum {
    SWLE = 0x0,
    SWRE = 0x1,
    PREFE = 0x2,
    CACHEE = 0x3,
    SBE = 0x4,
    SHE = 0x5,
    SCE = 0x6,
    SWE = 0x7,
};

/* POOL32F encoding of minor opcode field (bits 5..0) */

enum {
    /* These are the bit 7..6 values */
    ADD_FMT = 0x0,

    SUB_FMT = 0x1,

    MUL_FMT = 0x2,

    DIV_FMT = 0x3,

    /* These are the bit 8..6 values */
    MOVN_FMT = 0x0,
    RSQRT2_FMT = 0x0,
    MOVF_FMT = 0x0,
    RINT_FMT = 0x0,
    SELNEZ_FMT = 0x0,

    MOVZ_FMT = 0x1,
    LWXC1 = 0x1,
    MOVT_FMT = 0x1,
    CLASS_FMT = 0x1,
    SELEQZ_FMT = 0x1,

    PLL_PS = 0x2,
    SWXC1 = 0x2,
    SEL_FMT = 0x2,

    PLU_PS = 0x3,
    LDXC1 = 0x3,

    MOVN_FMT_04 = 0x4,
    PUL_PS = 0x4,
    SDXC1 = 0x4,
    RECIP2_FMT = 0x4,

    MOVZ_FMT_05 = 0x05,
    PUU_PS = 0x5,
    LUXC1 = 0x5,

    CVT_PS_S = 0x6,
    SUXC1 = 0x6,
    ADDR_PS = 0x6,
    PREFX = 0x6,
    MADDF_FMT = 0x6,

    MULR_PS = 0x7,
    MSUBF_FMT = 0x7,

    MADD_S = 0x01,
    MADD_D = 0x09,
    MADD_PS = 0x11,
    ALNV_PS = 0x19,
    MSUB_S = 0x21,
    MSUB_D = 0x29,
    MSUB_PS = 0x31,

    NMADD_S = 0x02,
    NMADD_D = 0x0a,
    NMADD_PS = 0x12,
    NMSUB_S = 0x22,
    NMSUB_D = 0x2a,
    NMSUB_PS = 0x32,

    MIN_FMT = 0x3,
    MAX_FMT = 0xb,
    MINA_FMT = 0x23,
    MAXA_FMT = 0x2b,
    POOL32FXF = 0x3b,

    CABS_COND_FMT = 0x1c,              /* MIPS3D */
    C_COND_FMT = 0x3c,

    CMP_CONDN_S = 0x5,
    CMP_CONDN_D = 0x15
};

/* POOL32Fxf encoding of minor opcode extension field */

enum {
    CVT_L = 0x04,
    RSQRT_FMT = 0x08,
    FLOOR_L = 0x0c,
    CVT_PW_PS = 0x1c,
    CVT_W = 0x24,
    SQRT_FMT = 0x28,
    FLOOR_W = 0x2c,
    CVT_PS_PW = 0x3c,
    CFC1 = 0x40,
    RECIP_FMT = 0x48,
    CEIL_L = 0x4c,
    CTC1 = 0x60,
    CEIL_W = 0x6c,
    MFC1 = 0x80,
    CVT_S_PL = 0x84,
    TRUNC_L = 0x8c,
    MTC1 = 0xa0,
    CVT_S_PU = 0xa4,
    TRUNC_W = 0xac,
    MFHC1 = 0xc0,
    ROUND_L = 0xcc,
    MTHC1 = 0xe0,
    ROUND_W = 0xec,

    MOV_FMT = 0x01,
    MOVF = 0x05,
    ABS_FMT = 0x0d,
    RSQRT1_FMT = 0x1d,
    MOVT = 0x25,
    NEG_FMT = 0x2d,
    CVT_D = 0x4d,
    RECIP1_FMT = 0x5d,
    CVT_S = 0x6d
};

/* POOL32I encoding of minor opcode field (bits 25..21) */

enum {
    BLTZ = 0x00,
    BLTZAL = 0x01,
    BGEZ = 0x02,
    BGEZAL = 0x03,
    BLEZ = 0x04,
    BNEZC = 0x05,
    BGTZ = 0x06,
    BEQZC = 0x07,
    TLTI = 0x08,
    BC1EQZC = 0x08,
    TGEI = 0x09,
    BC1NEZC = 0x09,
    TLTIU = 0x0a,
    BC2EQZC = 0x0a,
    TGEIU = 0x0b,
    BC2NEZC = 0x0a,
    TNEI = 0x0c,
    R6_SYNCI = 0x0c,
    LUI = 0x0d,
    TEQI = 0x0e,
    SYNCI = 0x10,
    BLTZALS = 0x11,
    BGEZALS = 0x13,
    BC2F = 0x14,
    BC2T = 0x15,
    BPOSGE64 = 0x1a,
    BPOSGE32 = 0x1b,
    /* These overlap and are distinguished by bit16 of the instruction */
    BC1F = 0x1c,
    BC1T = 0x1d,
    BC1ANY2F = 0x1c,
    BC1ANY2T = 0x1d,
    BC1ANY4F = 0x1e,
    BC1ANY4T = 0x1f
};

/* POOL16A encoding of minor opcode field */

enum {
    ADDU16 = 0x0,
    SUBU16 = 0x1
};

/* POOL16B encoding of minor opcode field */

enum {
    SLL16 = 0x0,
    SRL16 = 0x1
};

/* POOL16C encoding of minor opcode field */

enum {
    NOT16 = 0x00,
    XOR16 = 0x04,
    AND16 = 0x08,
    OR16 = 0x0c,
    LWM16 = 0x10,
    SWM16 = 0x14,
    JR16 = 0x18,
    JRC16 = 0x1a,
    JALR16 = 0x1c,
    JALR16S = 0x1e,
    MFHI16 = 0x20,
    MFLO16 = 0x24,
    BREAK16 = 0x28,
    SDBBP16 = 0x2c,
    JRADDIUSP = 0x30
};

/* R6 POOL16C encoding of minor opcode field (bits 0..5) */

enum {
    R6_NOT16    = 0x00,
    R6_AND16    = 0x01,
    R6_LWM16    = 0x02,
    R6_JRC16    = 0x03,
    MOVEP       = 0x04,
    MOVEP_07    = 0x07,
    R6_XOR16    = 0x08,
    R6_OR16     = 0x09,
    R6_SWM16    = 0x0a,
    JALRC16     = 0x0b,
    MOVEP_0C    = 0x0c,
    MOVEP_0F    = 0x0f,
    JRCADDIUSP  = 0x13,
    R6_BREAK16  = 0x1b,
    R6_SDBBP16  = 0x3b
};

/* POOL16D encoding of minor opcode field */

enum {
    ADDIUS5 = 0x0,
    ADDIUSP = 0x1
};

/* POOL16E encoding of minor opcode field */

enum {
    ADDIUR2 = 0x0,
    ADDIUR1SP = 0x1
};

static int mmreg (int r)
{
    static const int map[] = { 16, 17, 2, 3, 4, 5, 6, 7 };

    return map[r];
}

/* Used for 16-bit store instructions.  */
static int mmreg2 (int r)
{
    static const int map[] = { 0, 17, 2, 3, 4, 5, 6, 7 };

    return map[r];
}

#define uMIPS_RD(op) ((op >> 7) & 0x7)
#define uMIPS_RS(op) ((op >> 4) & 0x7)
#define uMIPS_RS2(op) uMIPS_RS(op)
#define uMIPS_RS1(op) ((op >> 1) & 0x7)
#define uMIPS_RD5(op) ((op >> 5) & 0x1f)
#define uMIPS_RS5(op) (op & 0x1f)

/* Signed immediate */
#define SIMM(op, start, width)                                          \
    ((int32_t)(((op >> start) & ((~0U) >> (32-width)))                 \
               << (32-width))                                           \
     >> (32-width))
/* Zero-extended immediate */
#define ZIMM(op, start, width) ((op >> start) & ((~0U) >> (32-width)))

static void gen_addiur1sp(DisasContext *ctx)
{
    int rd = mmreg(uMIPS_RD(ctx->opcode));

    gen_arith_imm(ctx, OPC_ADDIU, rd, 29, ((ctx->opcode >> 1) & 0x3f) << 2);
}

static void gen_addiur2(DisasContext *ctx)
{
    static const int decoded_imm[] = { 1, 4, 8, 12, 16, 20, 24, -1 };
    int rd = mmreg(uMIPS_RD(ctx->opcode));
    int rs = mmreg(uMIPS_RS(ctx->opcode));

    gen_arith_imm(ctx, OPC_ADDIU, rd, rs, decoded_imm[ZIMM(ctx->opcode, 1, 3)]);
}

static void gen_addiusp(DisasContext *ctx)
{
    int encoded = ZIMM(ctx->opcode, 1, 9);
    int decoded;

    if (encoded <= 1) {
        decoded = 256 + encoded;
    } else if (encoded <= 255) {
        decoded = encoded;
    } else if (encoded <= 509) {
        decoded = encoded - 512;
    } else {
        decoded = encoded - 768;
    }

    gen_arith_imm(ctx, OPC_ADDIU, 29, 29, decoded << 2);
}

static void gen_addius5(DisasContext *ctx)
{
    int imm = SIMM(ctx->opcode, 1, 4);
    int rd = (ctx->opcode >> 5) & 0x1f;

    gen_arith_imm(ctx, OPC_ADDIU, rd, rd, imm);
}

static void gen_andi16(DisasContext *ctx)
{
    static const int decoded_imm[] = { 128, 1, 2, 3, 4, 7, 8, 15, 16,
                                 31, 32, 63, 64, 255, 32768, 65535 };
    int rd = mmreg(uMIPS_RD(ctx->opcode));
    int rs = mmreg(uMIPS_RS(ctx->opcode));
    int encoded = ZIMM(ctx->opcode, 0, 4);

    gen_logic_imm(ctx, OPC_ANDI, rd, rs, decoded_imm[encoded]);
}

static void gen_ldst_multiple (DisasContext *ctx, uint32_t opc, int reglist,
                               int base, int16_t offset)
{
    TCGv t0, t1;
    TCGv_i32 t2;

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    t0 = tcg_temp_new();

    gen_base_offset_addr(ctx, t0, base, offset);

    t1 = tcg_const_tl(reglist);
    t2 = tcg_const_i32(ctx->mem_idx);

    save_cpu_state(ctx, 1);
    switch (opc) {
    case LWM32:
        gen_helper_lwm(cpu_env, t0, t1, t2);
        break;
    case SWM32:
        gen_helper_swm(cpu_env, t0, t1, t2);
        break;
#ifdef TARGET_MIPS64
    case LDM:
        gen_helper_ldm(cpu_env, t0, t1, t2);
        break;
    case SDM:
        gen_helper_sdm(cpu_env, t0, t1, t2);
        break;
#endif
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free_i32(t2);
}


static void gen_pool16c_insn(DisasContext *ctx)
{
    int rd = mmreg((ctx->opcode >> 3) & 0x7);
    int rs = mmreg(ctx->opcode & 0x7);

    switch (((ctx->opcode) >> 4) & 0x3f) {
    case NOT16 + 0:
    case NOT16 + 1:
    case NOT16 + 2:
    case NOT16 + 3:
        gen_logic(ctx, OPC_NOR, rd, rs, 0);
        break;
    case XOR16 + 0:
    case XOR16 + 1:
    case XOR16 + 2:
    case XOR16 + 3:
        gen_logic(ctx, OPC_XOR, rd, rd, rs);
        break;
    case AND16 + 0:
    case AND16 + 1:
    case AND16 + 2:
    case AND16 + 3:
        gen_logic(ctx, OPC_AND, rd, rd, rs);
        break;
    case OR16 + 0:
    case OR16 + 1:
    case OR16 + 2:
    case OR16 + 3:
        gen_logic(ctx, OPC_OR, rd, rd, rs);
        break;
    case LWM16 + 0:
    case LWM16 + 1:
    case LWM16 + 2:
    case LWM16 + 3:
        {
            static const int lwm_convert[] = { 0x11, 0x12, 0x13, 0x14 };
            int offset = ZIMM(ctx->opcode, 0, 4);

            gen_ldst_multiple(ctx, LWM32, lwm_convert[(ctx->opcode >> 4) & 0x3],
                              29, offset << 2);
        }
        break;
    case SWM16 + 0:
    case SWM16 + 1:
    case SWM16 + 2:
    case SWM16 + 3:
        {
            static const int swm_convert[] = { 0x11, 0x12, 0x13, 0x14 };
            int offset = ZIMM(ctx->opcode, 0, 4);

            gen_ldst_multiple(ctx, SWM32, swm_convert[(ctx->opcode >> 4) & 0x3],
                              29, offset << 2);
        }
        break;
    case JR16 + 0:
    case JR16 + 1:
        {
            int reg = ctx->opcode & 0x1f;

            gen_compute_branch(ctx, OPC_JR, 2, reg, 0, 0, 4);
        }
        break;
    case JRC16 + 0:
    case JRC16 + 1:
        {
            int reg = ctx->opcode & 0x1f;
            gen_compute_branch(ctx, OPC_JR, 2, reg, 0, 0, 0);
            /* Let normal delay slot handling in our caller take us
               to the branch target.  */
        }
        break;
    case JALR16 + 0:
    case JALR16 + 1:
        gen_compute_branch(ctx, OPC_JALR, 2, ctx->opcode & 0x1f, 31, 0, 4);
        ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        break;
    case JALR16S + 0:
    case JALR16S + 1:
        gen_compute_branch(ctx, OPC_JALR, 2, ctx->opcode & 0x1f, 31, 0, 2);
        ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        break;
    case MFHI16 + 0:
    case MFHI16 + 1:
        gen_HILO(ctx, OPC_MFHI, 0, uMIPS_RS5(ctx->opcode));
        break;
    case MFLO16 + 0:
    case MFLO16 + 1:
        gen_HILO(ctx, OPC_MFLO, 0, uMIPS_RS5(ctx->opcode));
        break;
    case BREAK16:
        generate_exception_end(ctx, EXCP_BREAK);
        break;
    case SDBBP16:
        if (is_uhi(extract32(ctx->opcode, 0, 4))) {
            gen_helper_do_semihosting(cpu_env);
        } else {
            /* XXX: not clear which exception should be raised
             *      when in debug mode...
             */
            check_insn(ctx, ISA_MIPS32);
            generate_exception_end(ctx, EXCP_DBp);
        }
        break;
    case JRADDIUSP + 0:
    case JRADDIUSP + 1:
        {
            int imm = ZIMM(ctx->opcode, 0, 5);
            gen_compute_branch(ctx, OPC_JR, 2, 31, 0, 0, 0);
            gen_arith_imm(ctx, OPC_ADDIU, 29, 29, imm << 2);
            /* Let normal delay slot handling in our caller take us
               to the branch target.  */
        }
        break;
    default:
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static inline void gen_movep(DisasContext *ctx, int enc_dest, int enc_rt,
                             int enc_rs)
{
    int rd, rs, re, rt;
    static const int rd_enc[] = { 5, 5, 6, 4, 4, 4, 4, 4 };
    static const int re_enc[] = { 6, 7, 7, 21, 22, 5, 6, 7 };
    static const int rs_rt_enc[] = { 0, 17, 2, 3, 16, 18, 19, 20 };
    rd = rd_enc[enc_dest];
    re = re_enc[enc_dest];
    rs = rs_rt_enc[enc_rs];
    rt = rs_rt_enc[enc_rt];
    if (rs) {
        tcg_gen_mov_tl(cpu_gpr[rd], cpu_gpr[rs]);
    } else {
        tcg_gen_movi_tl(cpu_gpr[rd], 0);
    }
    if (rt) {
        tcg_gen_mov_tl(cpu_gpr[re], cpu_gpr[rt]);
    } else {
        tcg_gen_movi_tl(cpu_gpr[re], 0);
    }
}

static void gen_pool16c_r6_insn(DisasContext *ctx)
{
    int rt = mmreg((ctx->opcode >> 7) & 0x7);
    int rs = mmreg((ctx->opcode >> 4) & 0x7);

    switch (ctx->opcode & 0xf) {
    case R6_NOT16:
        gen_logic(ctx, OPC_NOR, rt, rs, 0);
        break;
    case R6_AND16:
        gen_logic(ctx, OPC_AND, rt, rt, rs);
        break;
    case R6_LWM16:
        {
            int lwm_converted = 0x11 + extract32(ctx->opcode, 8, 2);
            int offset = extract32(ctx->opcode, 4, 4);
            gen_ldst_multiple(ctx, LWM32, lwm_converted, 29, offset << 2);
        }
        break;
    case R6_JRC16: /* JRCADDIUSP */
        if ((ctx->opcode >> 4) & 1) {
            /* JRCADDIUSP */
            int imm = extract32(ctx->opcode, 5, 5);
            gen_compute_branch(ctx, OPC_JR, 2, 31, 0, 0, 0);
            gen_arith_imm(ctx, OPC_ADDIU, 29, 29, imm << 2);
        } else {
            /* JRC16 */
            int rs = extract32(ctx->opcode, 5, 5);
            gen_compute_branch(ctx, OPC_JR, 2, rs, 0, 0, 0);
        }
        break;
    case MOVEP ... MOVEP_07:
    case MOVEP_0C ... MOVEP_0F:
        {
            int enc_dest = uMIPS_RD(ctx->opcode);
            int enc_rt = uMIPS_RS2(ctx->opcode);
            int enc_rs = (ctx->opcode & 3) | ((ctx->opcode >> 1) & 4);
            gen_movep(ctx, enc_dest, enc_rt, enc_rs);
        }
        break;
    case R6_XOR16:
        gen_logic(ctx, OPC_XOR, rt, rt, rs);
        break;
    case R6_OR16:
        gen_logic(ctx, OPC_OR, rt, rt, rs);
        break;
    case R6_SWM16:
        {
            int swm_converted = 0x11 + extract32(ctx->opcode, 8, 2);
            int offset = extract32(ctx->opcode, 4, 4);
            gen_ldst_multiple(ctx, SWM32, swm_converted, 29, offset << 2);
        }
        break;
    case JALRC16: /* BREAK16, SDBBP16 */
        switch (ctx->opcode & 0x3f) {
        case JALRC16:
        case JALRC16 + 0x20:
            /* JALRC16 */
            gen_compute_branch(ctx, OPC_JALR, 2, (ctx->opcode >> 5) & 0x1f,
                               31, 0, 0);
            break;
        case R6_BREAK16:
            /* BREAK16 */
            generate_exception(ctx, EXCP_BREAK);
            break;
        case R6_SDBBP16:
            /* SDBBP16 */
            if (is_uhi(extract32(ctx->opcode, 6, 4))) {
                gen_helper_do_semihosting(cpu_env);
            } else {
                if (ctx->hflags & MIPS_HFLAG_SBRI) {
                    generate_exception(ctx, EXCP_RI);
                } else {
                    generate_exception(ctx, EXCP_DBp);
                }
            }
            break;
        }
        break;
    default:
        generate_exception(ctx, EXCP_RI);
        break;
    }
}

static void gen_ldxs (DisasContext *ctx, int base, int index, int rd)
{
    TCGv t0 = tcg_temp_new();
    TCGv t1 = tcg_temp_new();

    gen_load_gpr(t0, base);

    if (index != 0) {
        gen_load_gpr(t1, index);
        tcg_gen_shli_tl(t1, t1, 2);
        gen_op_addr_add(ctx, t0, t1, t0);
    }

    tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TESL);
    gen_store_gpr(t1, rd);

    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_ldst_pair (DisasContext *ctx, uint32_t opc, int rd,
                           int base, int16_t offset)
{
    TCGv t0, t1;

    if (ctx->hflags & MIPS_HFLAG_BMASK || rd == 31) {
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();

    gen_base_offset_addr(ctx, t0, base, offset);

    switch (opc) {
    case LWP:
        if (rd == base) {
            generate_exception_end(ctx, EXCP_RI);
            return;
        }
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TESL);
        gen_store_gpr(t1, rd);
        tcg_gen_movi_tl(t1, 4);
        gen_op_addr_add(ctx, t0, t0, t1);
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TESL);
        gen_store_gpr(t1, rd+1);
        break;
    case SWP:
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);
        tcg_gen_movi_tl(t1, 4);
        gen_op_addr_add(ctx, t0, t0, t1);
        gen_load_gpr(t1, rd+1);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEUL);
        break;
#ifdef TARGET_MIPS64
    case LDP:
        if (rd == base) {
            generate_exception_end(ctx, EXCP_RI);
            return;
        }
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TEQ);
        gen_store_gpr(t1, rd);
        tcg_gen_movi_tl(t1, 8);
        gen_op_addr_add(ctx, t0, t0, t1);
        tcg_gen_qemu_ld_tl(t1, t0, ctx->mem_idx, MO_TEQ);
        gen_store_gpr(t1, rd+1);
        break;
    case SDP:
        gen_load_gpr(t1, rd);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEQ);
        tcg_gen_movi_tl(t1, 8);
        gen_op_addr_add(ctx, t0, t0, t1);
        gen_load_gpr(t1, rd+1);
        tcg_gen_qemu_st_tl(t1, t0, ctx->mem_idx, MO_TEQ);
        break;
#endif
    }
    tcg_temp_free(t0);
    tcg_temp_free(t1);
}

static void gen_sync(int stype)
{
    TCGBar tcg_mo = TCG_BAR_SC;

    switch (stype) {
    case 0x4: /* SYNC_WMB */
        tcg_mo |= TCG_MO_ST_ST;
        break;
    case 0x10: /* SYNC_MB */
        tcg_mo |= TCG_MO_ALL;
        break;
    case 0x11: /* SYNC_ACQUIRE */
        tcg_mo |= TCG_MO_LD_LD | TCG_MO_LD_ST;
        break;
    case 0x12: /* SYNC_RELEASE */
        tcg_mo |= TCG_MO_ST_ST | TCG_MO_LD_ST;
        break;
    case 0x13: /* SYNC_RMB */
        tcg_mo |= TCG_MO_LD_LD;
        break;
    default:
        tcg_mo |= TCG_MO_ALL;
        break;
    }

    tcg_gen_mb(tcg_mo);
}

static void gen_pool32axf (CPUMIPSState *env, DisasContext *ctx, int rt, int rs)
{
    int extension = (ctx->opcode >> 6) & 0x3f;
    int minor = (ctx->opcode >> 12) & 0xf;
    uint32_t mips32_op;

    switch (extension) {
    case TEQ:
        mips32_op = OPC_TEQ;
        goto do_trap;
    case TGE:
        mips32_op = OPC_TGE;
        goto do_trap;
    case TGEU:
        mips32_op = OPC_TGEU;
        goto do_trap;
    case TLT:
        mips32_op = OPC_TLT;
        goto do_trap;
    case TLTU:
        mips32_op = OPC_TLTU;
        goto do_trap;
    case TNE:
        mips32_op = OPC_TNE;
    do_trap:
        gen_trap(ctx, mips32_op, rs, rt, -1);
        break;
#ifndef CONFIG_USER_ONLY
    case MFC0:
    case MFC0 + 32:
        check_cp0_enabled(ctx);
        if (rt == 0) {
            /* Treat as NOP. */
            break;
        }
        gen_mfc0(ctx, cpu_gpr[rt], rs, (ctx->opcode >> 11) & 0x7);
        break;
    case MTC0:
    case MTC0 + 32:
        check_cp0_enabled(ctx);
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_mtc0(ctx, t0, rs, (ctx->opcode >> 11) & 0x7);
            tcg_temp_free(t0);
        }
        break;
#endif
    case 0x2a:
        switch (minor & 3) {
        case MADD_ACC:
            gen_muldiv(ctx, OPC_MADD, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MADDU_ACC:
            gen_muldiv(ctx, OPC_MADDU, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MSUB_ACC:
            gen_muldiv(ctx, OPC_MSUB, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MSUBU_ACC:
            gen_muldiv(ctx, OPC_MSUBU, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x32:
        switch (minor & 3) {
        case MULT_ACC:
            gen_muldiv(ctx, OPC_MULT, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        case MULTU_ACC:
            gen_muldiv(ctx, OPC_MULTU, (ctx->opcode >> 14) & 3, rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x2c:
        switch (minor) {
        case BITSWAP:
            check_insn(ctx, ISA_MIPS32R6);
            gen_bitswap(ctx, OPC_BITSWAP, rs, rt);
            break;
        case SEB:
            gen_bshfl(ctx, OPC_SEB, rs, rt);
            break;
        case SEH:
            gen_bshfl(ctx, OPC_SEH, rs, rt);
            break;
        case CLO:
            mips32_op = OPC_CLO;
            goto do_cl;
        case CLZ:
            mips32_op = OPC_CLZ;
        do_cl:
            check_insn(ctx, ISA_MIPS32);
            gen_cl(ctx, mips32_op, rt, rs);
            break;
        case RDHWR:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_rdhwr(ctx, rt, rs, 0);
            break;
        case WSBH:
            gen_bshfl(ctx, OPC_WSBH, rs, rt);
            break;
        case MULT:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_MULT;
            goto do_mul;
        case MULTU:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_MULTU;
            goto do_mul;
        case DIV:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_DIV;
            goto do_div;
        case DIVU:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_DIVU;
            goto do_div;
        do_div:
            check_insn(ctx, ISA_MIPS32);
            gen_muldiv(ctx, mips32_op, 0, rs, rt);
            break;
        case MADD:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_MADD;
            goto do_mul;
        case MADDU:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_MADDU;
            goto do_mul;
        case MSUB:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_MSUB;
            goto do_mul;
        case MSUBU:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_MSUBU;
        do_mul:
            check_insn(ctx, ISA_MIPS32);
            gen_muldiv(ctx, mips32_op, 0, rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x34:
        switch (minor) {
        case MFC2:
        case MTC2:
        case MFHC2:
        case MTHC2:
        case CFC2:
        case CTC2:
            generate_exception_err(ctx, EXCP_CpU, 2);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x3c:
        switch (minor) {
        case JALR:    /* JALRC */
        case JALR_HB: /* JALRC_HB */
            if (ctx->insn_flags & ISA_MIPS32R6) {
                /* JALRC, JALRC_HB */
                gen_compute_branch(ctx, OPC_JALR, 4, rs, rt, 0, 0);
            } else {
                /* JALR, JALR_HB */
                gen_compute_branch(ctx, OPC_JALR, 4, rs, rt, 0, 4);
                ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            }
            break;
        case JALRS:
        case JALRS_HB:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_JALR, 4, rs, rt, 0, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x05:
        switch (minor) {
        case RDPGPR:
            check_cp0_enabled(ctx);
            check_insn(ctx, ISA_MIPS32R2);
            gen_load_srsgpr(rs, rt);
            break;
        case WRPGPR:
            check_cp0_enabled(ctx);
            check_insn(ctx, ISA_MIPS32R2);
            gen_store_srsgpr(rs, rt);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
#ifndef CONFIG_USER_ONLY
    case 0x0d:
        switch (minor) {
        case TLBP:
            mips32_op = OPC_TLBP;
            goto do_cp0;
        case TLBR:
            mips32_op = OPC_TLBR;
            goto do_cp0;
        case TLBWI:
            mips32_op = OPC_TLBWI;
            goto do_cp0;
        case TLBWR:
            mips32_op = OPC_TLBWR;
            goto do_cp0;
        case TLBINV:
            mips32_op = OPC_TLBINV;
            goto do_cp0;
        case TLBINVF:
            mips32_op = OPC_TLBINVF;
            goto do_cp0;
        case WAIT:
            mips32_op = OPC_WAIT;
            goto do_cp0;
        case DERET:
            mips32_op = OPC_DERET;
            goto do_cp0;
        case ERET:
            mips32_op = OPC_ERET;
        do_cp0:
            gen_cp0(env, ctx, mips32_op, rt, rs);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x1d:
        switch (minor) {
        case DI:
            check_cp0_enabled(ctx);
            {
                TCGv t0 = tcg_temp_new();

                save_cpu_state(ctx, 1);
                gen_helper_di(t0, cpu_env);
                gen_store_gpr(t0, rs);
                /* Stop translation as we may have switched the execution mode */
                ctx->bstate = BS_STOP;
                tcg_temp_free(t0);
            }
            break;
        case EI:
            check_cp0_enabled(ctx);
            {
                TCGv t0 = tcg_temp_new();

                save_cpu_state(ctx, 1);
                gen_helper_ei(t0, cpu_env);
                gen_store_gpr(t0, rs);
                /* BS_STOP isn't sufficient, we need to ensure we break out
                   of translated code to check for pending interrupts.  */
                gen_save_pc(ctx->pc + 4);
                ctx->bstate = BS_EXCP;
                tcg_temp_free(t0);
            }
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
#endif
    case 0x2d:
        switch (minor) {
        case SYNC:
            gen_sync(extract32(ctx->opcode, 16, 5));
            break;
        case SYSCALL:
            generate_exception_end(ctx, EXCP_SYSCALL);
            break;
        case SDBBP:
            if (is_uhi(extract32(ctx->opcode, 16, 10))) {
                gen_helper_do_semihosting(cpu_env);
            } else {
                check_insn(ctx, ISA_MIPS32);
                if (ctx->hflags & MIPS_HFLAG_SBRI) {
                    generate_exception_end(ctx, EXCP_RI);
                } else {
                    generate_exception_end(ctx, EXCP_DBp);
                }
            }
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x01:
        switch (minor & 3) {
        case MFHI_ACC:
            gen_HILO(ctx, OPC_MFHI, minor >> 2, rs);
            break;
        case MFLO_ACC:
            gen_HILO(ctx, OPC_MFLO, minor >> 2, rs);
            break;
        case MTHI_ACC:
            gen_HILO(ctx, OPC_MTHI, minor >> 2, rs);
            break;
        case MTLO_ACC:
            gen_HILO(ctx, OPC_MTLO, minor >> 2, rs);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    case 0x35:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        switch (minor) {
        case MFHI32:
            gen_HILO(ctx, OPC_MFHI, 0, rs);
            break;
        case MFLO32:
            gen_HILO(ctx, OPC_MFLO, 0, rs);
            break;
        case MTHI32:
            gen_HILO(ctx, OPC_MTHI, 0, rs);
            break;
        case MTLO32:
            gen_HILO(ctx, OPC_MTLO, 0, rs);
            break;
        default:
            goto pool32axf_invalid;
        }
        break;
    default:
    pool32axf_invalid:
        MIPS_INVAL("pool32axf");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

/* Values for microMIPS fmt field.  Variable-width, depending on which
   formats the instruction supports.  */

enum {
    FMT_SD_S = 0,
    FMT_SD_D = 1,

    FMT_SDPS_S = 0,
    FMT_SDPS_D = 1,
    FMT_SDPS_PS = 2,

    FMT_SWL_S = 0,
    FMT_SWL_W = 1,
    FMT_SWL_L = 2,

    FMT_DWL_D = 0,
    FMT_DWL_W = 1,
    FMT_DWL_L = 2
};

static void gen_pool32fxf(DisasContext *ctx, int rt, int rs)
{
    int extension = (ctx->opcode >> 6) & 0x3ff;
    uint32_t mips32_op;

#define FLOAT_1BIT_FMT(opc, fmt) (fmt << 8) | opc
#define FLOAT_2BIT_FMT(opc, fmt) (fmt << 7) | opc
#define COND_FLOAT_MOV(opc, cond) (cond << 7) | opc

    switch (extension) {
    case FLOAT_1BIT_FMT(CFC1, 0):
        mips32_op = OPC_CFC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(CTC1, 0):
        mips32_op = OPC_CTC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MFC1, 0):
        mips32_op = OPC_MFC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MTC1, 0):
        mips32_op = OPC_MTC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MFHC1, 0):
        mips32_op = OPC_MFHC1;
        goto do_cp1;
    case FLOAT_1BIT_FMT(MTHC1, 0):
        mips32_op = OPC_MTHC1;
    do_cp1:
        gen_cp1(ctx, mips32_op, rt, rs);
        break;

        /* Reciprocal square root */
    case FLOAT_1BIT_FMT(RSQRT_FMT, FMT_SD_S):
        mips32_op = OPC_RSQRT_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(RSQRT_FMT, FMT_SD_D):
        mips32_op = OPC_RSQRT_D;
        goto do_unaryfp;

        /* Square root */
    case FLOAT_1BIT_FMT(SQRT_FMT, FMT_SD_S):
        mips32_op = OPC_SQRT_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(SQRT_FMT, FMT_SD_D):
        mips32_op = OPC_SQRT_D;
        goto do_unaryfp;

        /* Reciprocal */
    case FLOAT_1BIT_FMT(RECIP_FMT, FMT_SD_S):
        mips32_op = OPC_RECIP_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(RECIP_FMT, FMT_SD_D):
        mips32_op = OPC_RECIP_D;
        goto do_unaryfp;

        /* Floor */
    case FLOAT_1BIT_FMT(FLOOR_L, FMT_SD_S):
        mips32_op = OPC_FLOOR_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(FLOOR_L, FMT_SD_D):
        mips32_op = OPC_FLOOR_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(FLOOR_W, FMT_SD_S):
        mips32_op = OPC_FLOOR_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(FLOOR_W, FMT_SD_D):
        mips32_op = OPC_FLOOR_W_D;
        goto do_unaryfp;

        /* Ceiling */
    case FLOAT_1BIT_FMT(CEIL_L, FMT_SD_S):
        mips32_op = OPC_CEIL_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CEIL_L, FMT_SD_D):
        mips32_op = OPC_CEIL_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CEIL_W, FMT_SD_S):
        mips32_op = OPC_CEIL_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CEIL_W, FMT_SD_D):
        mips32_op = OPC_CEIL_W_D;
        goto do_unaryfp;

        /* Truncation */
    case FLOAT_1BIT_FMT(TRUNC_L, FMT_SD_S):
        mips32_op = OPC_TRUNC_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(TRUNC_L, FMT_SD_D):
        mips32_op = OPC_TRUNC_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(TRUNC_W, FMT_SD_S):
        mips32_op = OPC_TRUNC_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(TRUNC_W, FMT_SD_D):
        mips32_op = OPC_TRUNC_W_D;
        goto do_unaryfp;

        /* Round */
    case FLOAT_1BIT_FMT(ROUND_L, FMT_SD_S):
        mips32_op = OPC_ROUND_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(ROUND_L, FMT_SD_D):
        mips32_op = OPC_ROUND_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(ROUND_W, FMT_SD_S):
        mips32_op = OPC_ROUND_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(ROUND_W, FMT_SD_D):
        mips32_op = OPC_ROUND_W_D;
        goto do_unaryfp;

        /* Integer to floating-point conversion */
    case FLOAT_1BIT_FMT(CVT_L, FMT_SD_S):
        mips32_op = OPC_CVT_L_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_L, FMT_SD_D):
        mips32_op = OPC_CVT_L_D;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_W, FMT_SD_S):
        mips32_op = OPC_CVT_W_S;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_W, FMT_SD_D):
        mips32_op = OPC_CVT_W_D;
        goto do_unaryfp;

        /* Paired-foo conversions */
    case FLOAT_1BIT_FMT(CVT_S_PL, 0):
        mips32_op = OPC_CVT_S_PL;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_S_PU, 0):
        mips32_op = OPC_CVT_S_PU;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_PW_PS, 0):
        mips32_op = OPC_CVT_PW_PS;
        goto do_unaryfp;
    case FLOAT_1BIT_FMT(CVT_PS_PW, 0):
        mips32_op = OPC_CVT_PS_PW;
        goto do_unaryfp;

        /* Floating-point moves */
    case FLOAT_2BIT_FMT(MOV_FMT, FMT_SDPS_S):
        mips32_op = OPC_MOV_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(MOV_FMT, FMT_SDPS_D):
        mips32_op = OPC_MOV_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(MOV_FMT, FMT_SDPS_PS):
        mips32_op = OPC_MOV_PS;
        goto do_unaryfp;

        /* Absolute value */
    case FLOAT_2BIT_FMT(ABS_FMT, FMT_SDPS_S):
        mips32_op = OPC_ABS_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(ABS_FMT, FMT_SDPS_D):
        mips32_op = OPC_ABS_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(ABS_FMT, FMT_SDPS_PS):
        mips32_op = OPC_ABS_PS;
        goto do_unaryfp;

        /* Negation */
    case FLOAT_2BIT_FMT(NEG_FMT, FMT_SDPS_S):
        mips32_op = OPC_NEG_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(NEG_FMT, FMT_SDPS_D):
        mips32_op = OPC_NEG_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(NEG_FMT, FMT_SDPS_PS):
        mips32_op = OPC_NEG_PS;
        goto do_unaryfp;

        /* Reciprocal square root step */
    case FLOAT_2BIT_FMT(RSQRT1_FMT, FMT_SDPS_S):
        mips32_op = OPC_RSQRT1_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RSQRT1_FMT, FMT_SDPS_D):
        mips32_op = OPC_RSQRT1_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RSQRT1_FMT, FMT_SDPS_PS):
        mips32_op = OPC_RSQRT1_PS;
        goto do_unaryfp;

        /* Reciprocal step */
    case FLOAT_2BIT_FMT(RECIP1_FMT, FMT_SDPS_S):
        mips32_op = OPC_RECIP1_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RECIP1_FMT, FMT_SDPS_D):
        mips32_op = OPC_RECIP1_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(RECIP1_FMT, FMT_SDPS_PS):
        mips32_op = OPC_RECIP1_PS;
        goto do_unaryfp;

        /* Conversions from double */
    case FLOAT_2BIT_FMT(CVT_D, FMT_SWL_S):
        mips32_op = OPC_CVT_D_S;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_D, FMT_SWL_W):
        mips32_op = OPC_CVT_D_W;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_D, FMT_SWL_L):
        mips32_op = OPC_CVT_D_L;
        goto do_unaryfp;

        /* Conversions from single */
    case FLOAT_2BIT_FMT(CVT_S, FMT_DWL_D):
        mips32_op = OPC_CVT_S_D;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_S, FMT_DWL_W):
        mips32_op = OPC_CVT_S_W;
        goto do_unaryfp;
    case FLOAT_2BIT_FMT(CVT_S, FMT_DWL_L):
        mips32_op = OPC_CVT_S_L;
    do_unaryfp:
        gen_farith(ctx, mips32_op, -1, rs, rt, 0);
        break;

        /* Conditional moves on floating-point codes */
    case COND_FLOAT_MOV(MOVT, 0):
    case COND_FLOAT_MOV(MOVT, 1):
    case COND_FLOAT_MOV(MOVT, 2):
    case COND_FLOAT_MOV(MOVT, 3):
    case COND_FLOAT_MOV(MOVT, 4):
    case COND_FLOAT_MOV(MOVT, 5):
    case COND_FLOAT_MOV(MOVT, 6):
    case COND_FLOAT_MOV(MOVT, 7):
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        gen_movci(ctx, rt, rs, (ctx->opcode >> 13) & 0x7, 1);
        break;
    case COND_FLOAT_MOV(MOVF, 0):
    case COND_FLOAT_MOV(MOVF, 1):
    case COND_FLOAT_MOV(MOVF, 2):
    case COND_FLOAT_MOV(MOVF, 3):
    case COND_FLOAT_MOV(MOVF, 4):
    case COND_FLOAT_MOV(MOVF, 5):
    case COND_FLOAT_MOV(MOVF, 6):
    case COND_FLOAT_MOV(MOVF, 7):
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        gen_movci(ctx, rt, rs, (ctx->opcode >> 13) & 0x7, 0);
        break;
    default:
        MIPS_INVAL("pool32fxf");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void decode_micromips32_opc(CPUMIPSState *env, DisasContext *ctx)
{
    int32_t offset;
    uint16_t insn;
    int rt, rs, rd, rr;
    int16_t imm;
    uint32_t op, minor, minor2, mips32_op;
    uint32_t cond, fmt, cc;

    insn = cpu_lduw_code(env, ctx->pc + 2);
    ctx->opcode = (ctx->opcode << 16) | insn;

    rt = (ctx->opcode >> 21) & 0x1f;
    rs = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    rr = (ctx->opcode >> 6) & 0x1f;
    imm = (int16_t) ctx->opcode;

    op = (ctx->opcode >> 26) & 0x3f;
    switch (op) {
    case POOL32A:
        minor = ctx->opcode & 0x3f;
        switch (minor) {
        case 0x00:
            minor = (ctx->opcode >> 6) & 0xf;
            switch (minor) {
            case SLL32:
                mips32_op = OPC_SLL;
                goto do_shifti;
            case SRA:
                mips32_op = OPC_SRA;
                goto do_shifti;
            case SRL32:
                mips32_op = OPC_SRL;
                goto do_shifti;
            case ROTR:
                mips32_op = OPC_ROTR;
            do_shifti:
                gen_shift_imm(ctx, mips32_op, rt, rs, rd);
                break;
            case SELEQZ:
                check_insn(ctx, ISA_MIPS32R6);
                gen_cond_move(ctx, OPC_SELEQZ, rd, rs, rt);
                break;
            case SELNEZ:
                check_insn(ctx, ISA_MIPS32R6);
                gen_cond_move(ctx, OPC_SELNEZ, rd, rs, rt);
                break;
            case R6_RDHWR:
                check_insn(ctx, ISA_MIPS32R6);
                gen_rdhwr(ctx, rt, rs, extract32(ctx->opcode, 11, 3));
                break;
            default:
                goto pool32a_invalid;
            }
            break;
        case 0x10:
            minor = (ctx->opcode >> 6) & 0xf;
            switch (minor) {
                /* Arithmetic */
            case ADD:
                mips32_op = OPC_ADD;
                goto do_arith;
            case ADDU32:
                mips32_op = OPC_ADDU;
                goto do_arith;
            case SUB:
                mips32_op = OPC_SUB;
                goto do_arith;
            case SUBU32:
                mips32_op = OPC_SUBU;
                goto do_arith;
            case MUL:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MUL;
            do_arith:
                gen_arith(ctx, mips32_op, rd, rs, rt);
                break;
                /* Shifts */
            case SLLV:
                mips32_op = OPC_SLLV;
                goto do_shift;
            case SRLV:
                mips32_op = OPC_SRLV;
                goto do_shift;
            case SRAV:
                mips32_op = OPC_SRAV;
                goto do_shift;
            case ROTRV:
                mips32_op = OPC_ROTRV;
            do_shift:
                gen_shift(ctx, mips32_op, rd, rs, rt);
                break;
                /* Logical operations */
            case AND:
                mips32_op = OPC_AND;
                goto do_logic;
            case OR32:
                mips32_op = OPC_OR;
                goto do_logic;
            case NOR:
                mips32_op = OPC_NOR;
                goto do_logic;
            case XOR32:
                mips32_op = OPC_XOR;
            do_logic:
                gen_logic(ctx, mips32_op, rd, rs, rt);
                break;
                /* Set less than */
            case SLT:
                mips32_op = OPC_SLT;
                goto do_slt;
            case SLTU:
                mips32_op = OPC_SLTU;
            do_slt:
                gen_slt(ctx, mips32_op, rd, rs, rt);
                break;
            default:
                goto pool32a_invalid;
            }
            break;
        case 0x18:
            minor = (ctx->opcode >> 6) & 0xf;
            switch (minor) {
                /* Conditional moves */
            case MOVN: /* MUL */
                if (ctx->insn_flags & ISA_MIPS32R6) {
                    /* MUL */
                    gen_r6_muldiv(ctx, R6_OPC_MUL, rd, rs, rt);
                } else {
                    /* MOVN */
                    gen_cond_move(ctx, OPC_MOVN, rd, rs, rt);
                }
                break;
            case MOVZ: /* MUH */
                if (ctx->insn_flags & ISA_MIPS32R6) {
                    /* MUH */
                    gen_r6_muldiv(ctx, R6_OPC_MUH, rd, rs, rt);
                } else {
                    /* MOVZ */
                    gen_cond_move(ctx, OPC_MOVZ, rd, rs, rt);
                }
                break;
            case MULU:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_muldiv(ctx, R6_OPC_MULU, rd, rs, rt);
                break;
            case MUHU:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_muldiv(ctx, R6_OPC_MUHU, rd, rs, rt);
                break;
            case LWXS: /* DIV */
                if (ctx->insn_flags & ISA_MIPS32R6) {
                    /* DIV */
                    gen_r6_muldiv(ctx, R6_OPC_DIV, rd, rs, rt);
                } else {
                    /* LWXS */
                    gen_ldxs(ctx, rs, rt, rd);
                }
                break;
            case MOD:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_muldiv(ctx, R6_OPC_MOD, rd, rs, rt);
                break;
            case R6_DIVU:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_muldiv(ctx, R6_OPC_DIVU, rd, rs, rt);
                break;
            case MODU:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_muldiv(ctx, R6_OPC_MODU, rd, rs, rt);
                break;
            default:
                goto pool32a_invalid;
            }
            break;
        case INS:
            gen_bitops(ctx, OPC_INS, rt, rs, rr, rd);
            return;
        case LSA:
            check_insn(ctx, ISA_MIPS32R6);
            gen_lsa(ctx, OPC_LSA, rd, rs, rt,
                    extract32(ctx->opcode, 9, 2));
            break;
        case ALIGN:
            check_insn(ctx, ISA_MIPS32R6);
            gen_align(ctx, OPC_ALIGN, rd, rs, rt,
                      extract32(ctx->opcode, 9, 2));
            break;
        case EXT:
            gen_bitops(ctx, OPC_EXT, rt, rs, rr, rd);
            return;
        case POOL32AXF:
            gen_pool32axf(env, ctx, rt, rs);
            break;
        case BREAK32:
            generate_exception_end(ctx, EXCP_BREAK);
            break;
        case SIGRIE:
            check_insn(ctx, ISA_MIPS32R6);
            generate_exception_end(ctx, EXCP_RI);
            break;
        default:
        pool32a_invalid:
                MIPS_INVAL("pool32a");
                generate_exception_end(ctx, EXCP_RI);
                break;
        }
        break;
    case POOL32B:
        minor = (ctx->opcode >> 12) & 0xf;
        switch (minor) {
        case CACHE:
            check_cp0_enabled(ctx);
            if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
                gen_cache_operation(ctx, rt, rs, imm);
            }
            break;
        case LWC2:
        case SWC2:
            /* COP2: Not implemented. */
            generate_exception_err(ctx, EXCP_CpU, 2);
            break;
#ifdef TARGET_MIPS64
        case LDP:
        case SDP:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            /* Fallthrough */
#endif
        case LWP:
        case SWP:
            gen_ldst_pair(ctx, minor, rt, rs, SIMM(ctx->opcode, 0, 12));
            break;
#ifdef TARGET_MIPS64
        case LDM:
        case SDM:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            /* Fallthrough */
#endif
        case LWM32:
        case SWM32:
            gen_ldst_multiple(ctx, minor, rt, rs, SIMM(ctx->opcode, 0, 12));
            break;
        default:
            MIPS_INVAL("pool32b");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case POOL32F:
        if (ctx->CP0_Config1 & (1 << CP0C1_FP)) {
            minor = ctx->opcode & 0x3f;
            check_cp1_enabled(ctx);
            switch (minor) {
            case ALNV_PS:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_ALNV_PS;
                goto do_madd;
            case MADD_S:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MADD_S;
                goto do_madd;
            case MADD_D:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MADD_D;
                goto do_madd;
            case MADD_PS:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MADD_PS;
                goto do_madd;
            case MSUB_S:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MSUB_S;
                goto do_madd;
            case MSUB_D:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MSUB_D;
                goto do_madd;
            case MSUB_PS:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_MSUB_PS;
                goto do_madd;
            case NMADD_S:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_NMADD_S;
                goto do_madd;
            case NMADD_D:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_NMADD_D;
                goto do_madd;
            case NMADD_PS:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_NMADD_PS;
                goto do_madd;
            case NMSUB_S:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_NMSUB_S;
                goto do_madd;
            case NMSUB_D:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_NMSUB_D;
                goto do_madd;
            case NMSUB_PS:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_NMSUB_PS;
            do_madd:
                gen_flt3_arith(ctx, mips32_op, rd, rr, rs, rt);
                break;
            case CABS_COND_FMT:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                cond = (ctx->opcode >> 6) & 0xf;
                cc = (ctx->opcode >> 13) & 0x7;
                fmt = (ctx->opcode >> 10) & 0x3;
                switch (fmt) {
                case 0x0:
                    gen_cmpabs_s(ctx, cond, rt, rs, cc);
                    break;
                case 0x1:
                    gen_cmpabs_d(ctx, cond, rt, rs, cc);
                    break;
                case 0x2:
                    gen_cmpabs_ps(ctx, cond, rt, rs, cc);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case C_COND_FMT:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                cond = (ctx->opcode >> 6) & 0xf;
                cc = (ctx->opcode >> 13) & 0x7;
                fmt = (ctx->opcode >> 10) & 0x3;
                switch (fmt) {
                case 0x0:
                    gen_cmp_s(ctx, cond, rt, rs, cc);
                    break;
                case 0x1:
                    gen_cmp_d(ctx, cond, rt, rs, cc);
                    break;
                case 0x2:
                    gen_cmp_ps(ctx, cond, rt, rs, cc);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case CMP_CONDN_S:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_cmp_s(ctx, (ctx->opcode >> 6) & 0x1f, rt, rs, rd);
                break;
            case CMP_CONDN_D:
                check_insn(ctx, ISA_MIPS32R6);
                gen_r6_cmp_d(ctx, (ctx->opcode >> 6) & 0x1f, rt, rs, rd);
                break;
            case POOL32FXF:
                gen_pool32fxf(ctx, rt, rs);
                break;
            case 0x00:
                /* PLL foo */
                switch ((ctx->opcode >> 6) & 0x7) {
                case PLL_PS:
                    mips32_op = OPC_PLL_PS;
                    goto do_ps;
                case PLU_PS:
                    mips32_op = OPC_PLU_PS;
                    goto do_ps;
                case PUL_PS:
                    mips32_op = OPC_PUL_PS;
                    goto do_ps;
                case PUU_PS:
                    mips32_op = OPC_PUU_PS;
                    goto do_ps;
                case CVT_PS_S:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_CVT_PS_S;
                do_ps:
                    gen_farith(ctx, mips32_op, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case MIN_FMT:
                check_insn(ctx, ISA_MIPS32R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MIN_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MIN_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x08:
                /* [LS][WDU]XC1 */
                switch ((ctx->opcode >> 6) & 0x7) {
                case LWXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_LWXC1;
                    goto do_ldst_cp1;
                case SWXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_SWXC1;
                    goto do_ldst_cp1;
                case LDXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_LDXC1;
                    goto do_ldst_cp1;
                case SDXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_SDXC1;
                    goto do_ldst_cp1;
                case LUXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_LUXC1;
                    goto do_ldst_cp1;
                case SUXC1:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    mips32_op = OPC_SUXC1;
                do_ldst_cp1:
                    gen_flt3_ldst(ctx, mips32_op, rd, rd, rt, rs);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case MAX_FMT:
                check_insn(ctx, ISA_MIPS32R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MAX_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MAX_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x18:
                /* 3D insns */
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                fmt = (ctx->opcode >> 9) & 0x3;
                switch ((ctx->opcode >> 6) & 0x7) {
                case RSQRT2_FMT:
                    switch (fmt) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_RSQRT2_S;
                        goto do_3d;
                    case FMT_SDPS_D:
                        mips32_op = OPC_RSQRT2_D;
                        goto do_3d;
                    case FMT_SDPS_PS:
                        mips32_op = OPC_RSQRT2_PS;
                        goto do_3d;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case RECIP2_FMT:
                    switch (fmt) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_RECIP2_S;
                        goto do_3d;
                    case FMT_SDPS_D:
                        mips32_op = OPC_RECIP2_D;
                        goto do_3d;
                    case FMT_SDPS_PS:
                        mips32_op = OPC_RECIP2_PS;
                        goto do_3d;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case ADDR_PS:
                    mips32_op = OPC_ADDR_PS;
                    goto do_3d;
                case MULR_PS:
                    mips32_op = OPC_MULR_PS;
                do_3d:
                    gen_farith(ctx, mips32_op, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x20:
                /* MOV[FT].fmt, PREFX, RINT.fmt, CLASS.fmt*/
                cc = (ctx->opcode >> 13) & 0x7;
                fmt = (ctx->opcode >> 9) & 0x3;
                switch ((ctx->opcode >> 6) & 0x7) {
                case MOVF_FMT: /* RINT_FMT */
                    if (ctx->insn_flags & ISA_MIPS32R6) {
                        /* RINT_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_farith(ctx, OPC_RINT_S, 0, rt, rs, 0);
                            break;
                        case FMT_SDPS_D:
                            gen_farith(ctx, OPC_RINT_D, 0, rt, rs, 0);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVF_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_movcf_s(ctx, rs, rt, cc, 0);
                            break;
                        case FMT_SDPS_D:
                            gen_movcf_d(ctx, rs, rt, cc, 0);
                            break;
                        case FMT_SDPS_PS:
                            check_ps(ctx);
                            gen_movcf_ps(ctx, rs, rt, cc, 0);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    }
                    break;
                case MOVT_FMT: /* CLASS_FMT */
                    if (ctx->insn_flags & ISA_MIPS32R6) {
                        /* CLASS_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_farith(ctx, OPC_CLASS_S, 0, rt, rs, 0);
                            break;
                        case FMT_SDPS_D:
                            gen_farith(ctx, OPC_CLASS_D, 0, rt, rs, 0);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVT_FMT */
                        switch (fmt) {
                        case FMT_SDPS_S:
                            gen_movcf_s(ctx, rs, rt, cc, 1);
                            break;
                        case FMT_SDPS_D:
                            gen_movcf_d(ctx, rs, rt, cc, 1);
                            break;
                        case FMT_SDPS_PS:
                            check_ps(ctx);
                            gen_movcf_ps(ctx, rs, rt, cc, 1);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    }
                    break;
                case PREFX:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
#define FINSN_3ARG_SDPS(prfx)                           \
                switch ((ctx->opcode >> 8) & 0x3) {     \
                case FMT_SDPS_S:                        \
                    mips32_op = OPC_##prfx##_S;         \
                    goto do_fpop;                       \
                case FMT_SDPS_D:                        \
                    mips32_op = OPC_##prfx##_D;         \
                    goto do_fpop;                       \
                case FMT_SDPS_PS:                       \
                    check_ps(ctx);                      \
                    mips32_op = OPC_##prfx##_PS;        \
                    goto do_fpop;                       \
                default:                                \
                    goto pool32f_invalid;               \
                }
            case MINA_FMT:
                check_insn(ctx, ISA_MIPS32R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MINA_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MINA_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case MAXA_FMT:
                check_insn(ctx, ISA_MIPS32R6);
                switch ((ctx->opcode >> 9) & 0x3) {
                case FMT_SDPS_S:
                    gen_farith(ctx, OPC_MAXA_S, rt, rs, rd, 0);
                    break;
                case FMT_SDPS_D:
                    gen_farith(ctx, OPC_MAXA_D, rt, rs, rd, 0);
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x30:
                /* regular FP ops */
                switch ((ctx->opcode >> 6) & 0x3) {
                case ADD_FMT:
                    FINSN_3ARG_SDPS(ADD);
                    break;
                case SUB_FMT:
                    FINSN_3ARG_SDPS(SUB);
                    break;
                case MUL_FMT:
                    FINSN_3ARG_SDPS(MUL);
                    break;
                case DIV_FMT:
                    fmt = (ctx->opcode >> 8) & 0x3;
                    if (fmt == 1) {
                        mips32_op = OPC_DIV_D;
                    } else if (fmt == 0) {
                        mips32_op = OPC_DIV_S;
                    } else {
                        goto pool32f_invalid;
                    }
                    goto do_fpop;
                default:
                    goto pool32f_invalid;
                }
                break;
            case 0x38:
                /* cmovs */
                switch ((ctx->opcode >> 6) & 0x7) {
                case MOVN_FMT: /* SELNEZ_FMT */
                    if (ctx->insn_flags & ISA_MIPS32R6) {
                        /* SELNEZ_FMT */
                        switch ((ctx->opcode >> 9) & 0x3) {
                        case FMT_SDPS_S:
                            gen_sel_s(ctx, OPC_SELNEZ_S, rd, rt, rs);
                            break;
                        case FMT_SDPS_D:
                            gen_sel_d(ctx, OPC_SELNEZ_D, rd, rt, rs);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVN_FMT */
                        FINSN_3ARG_SDPS(MOVN);
                    }
                    break;
                case MOVN_FMT_04:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    FINSN_3ARG_SDPS(MOVN);
                    break;
                case MOVZ_FMT: /* SELEQZ_FMT */
                    if (ctx->insn_flags & ISA_MIPS32R6) {
                        /* SELEQZ_FMT */
                        switch ((ctx->opcode >> 9) & 0x3) {
                        case FMT_SDPS_S:
                            gen_sel_s(ctx, OPC_SELEQZ_S, rd, rt, rs);
                            break;
                        case FMT_SDPS_D:
                            gen_sel_d(ctx, OPC_SELEQZ_D, rd, rt, rs);
                            break;
                        default:
                            goto pool32f_invalid;
                        }
                    } else {
                        /* MOVZ_FMT */
                        FINSN_3ARG_SDPS(MOVZ);
                    }
                    break;
                case MOVZ_FMT_05:
                    check_insn_opc_removed(ctx, ISA_MIPS32R6);
                    FINSN_3ARG_SDPS(MOVZ);
                    break;
                case SEL_FMT:
                    check_insn(ctx, ISA_MIPS32R6);
                    switch ((ctx->opcode >> 9) & 0x3) {
                    case FMT_SDPS_S:
                        gen_sel_s(ctx, OPC_SEL_S, rd, rt, rs);
                        break;
                    case FMT_SDPS_D:
                        gen_sel_d(ctx, OPC_SEL_D, rd, rt, rs);
                        break;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case MADDF_FMT:
                    check_insn(ctx, ISA_MIPS32R6);
                    switch ((ctx->opcode >> 9) & 0x3) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_MADDF_S;
                        goto do_fpop;
                    case FMT_SDPS_D:
                        mips32_op = OPC_MADDF_D;
                        goto do_fpop;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                case MSUBF_FMT:
                    check_insn(ctx, ISA_MIPS32R6);
                    switch ((ctx->opcode >> 9) & 0x3) {
                    case FMT_SDPS_S:
                        mips32_op = OPC_MSUBF_S;
                        goto do_fpop;
                    case FMT_SDPS_D:
                        mips32_op = OPC_MSUBF_D;
                        goto do_fpop;
                    default:
                        goto pool32f_invalid;
                    }
                    break;
                default:
                    goto pool32f_invalid;
                }
                break;
            do_fpop:
                gen_farith(ctx, mips32_op, rt, rs, rd, 0);
                break;
            default:
            pool32f_invalid:
                MIPS_INVAL("pool32f");
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;
    case POOL32I:
        minor = (ctx->opcode >> 21) & 0x1f;
        switch (minor) {
        case BLTZ:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BLTZ, 4, rs, -1, imm << 1, 4);
            break;
        case BLTZAL:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BLTZAL, 4, rs, -1, imm << 1, 4);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BLTZALS:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BLTZAL, 4, rs, -1, imm << 1, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BGEZ:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BGEZ, 4, rs, -1, imm << 1, 4);
            break;
        case BGEZAL:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BGEZAL, 4, rs, -1, imm << 1, 4);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BGEZALS:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BGEZAL, 4, rs, -1, imm << 1, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
            break;
        case BLEZ:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BLEZ, 4, rs, -1, imm << 1, 4);
            break;
        case BGTZ:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, OPC_BGTZ, 4, rs, -1, imm << 1, 4);
            break;

            /* Traps */
        case TLTI: /* BC1EQZC */
            if (ctx->insn_flags & ISA_MIPS32R6) {
                /* BC1EQZC */
                check_cp1_enabled(ctx);
                gen_compute_branch1_r6(ctx, OPC_BC1EQZ, rs, imm << 1, 0);
            } else {
                /* TLTI */
                mips32_op = OPC_TLTI;
                goto do_trapi;
            }
            break;
        case TGEI: /* BC1NEZC */
            if (ctx->insn_flags & ISA_MIPS32R6) {
                /* BC1NEZC */
                check_cp1_enabled(ctx);
                gen_compute_branch1_r6(ctx, OPC_BC1NEZ, rs, imm << 1, 0);
            } else {
                /* TGEI */
                mips32_op = OPC_TGEI;
                goto do_trapi;
            }
            break;
        case TLTIU:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_TLTIU;
            goto do_trapi;
        case TGEIU:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_TGEIU;
            goto do_trapi;
        case TNEI: /* SYNCI */
            if (ctx->insn_flags & ISA_MIPS32R6) {
                /* SYNCI */
                /* Break the TB to be able to sync copied instructions
                   immediately */
                ctx->bstate = BS_STOP;
            } else {
                /* TNEI */
                mips32_op = OPC_TNEI;
                goto do_trapi;
            }
            break;
        case TEQI:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_TEQI;
        do_trapi:
            gen_trap(ctx, mips32_op, rs, -1, imm);
            break;

        case BNEZC:
        case BEQZC:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch(ctx, minor == BNEZC ? OPC_BNE : OPC_BEQ,
                               4, rs, 0, imm << 1, 0);
            /* Compact branches don't have a delay slot, so just let
               the normal delay slot handling take us to the branch
               target. */
            break;
        case LUI:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_logic_imm(ctx, OPC_LUI, rs, 0, imm);
            break;
        case SYNCI:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            /* Break the TB to be able to sync copied instructions
               immediately */
            ctx->bstate = BS_STOP;
            break;
        case BC2F:
        case BC2T:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            /* COP2: Not implemented. */
            generate_exception_err(ctx, EXCP_CpU, 2);
            break;
        case BC1F:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = (ctx->opcode & (1 << 16)) ? OPC_BC1FANY2 : OPC_BC1F;
            goto do_cp1branch;
        case BC1T:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = (ctx->opcode & (1 << 16)) ? OPC_BC1TANY2 : OPC_BC1T;
            goto do_cp1branch;
        case BC1ANY4F:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_BC1FANY4;
            goto do_cp1mips3d;
        case BC1ANY4T:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_BC1TANY4;
        do_cp1mips3d:
            check_cop1x(ctx);
            check_insn(ctx, ASE_MIPS3D);
            /* Fall through */
        do_cp1branch:
            if (env->CP0_Config1 & (1 << CP0C1_FP)) {
                check_cp1_enabled(ctx);
                gen_compute_branch1(ctx, mips32_op,
                                    (ctx->opcode >> 18) & 0x7, imm << 1);
            } else {
                generate_exception_err(ctx, EXCP_CpU, 1);
            }
            break;
        case BPOSGE64:
        case BPOSGE32:
            /* MIPS DSP: not implemented */
            /* Fall through */
        default:
            MIPS_INVAL("pool32i");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case POOL32C:
        minor = (ctx->opcode >> 12) & 0xf;
        offset = sextract32(ctx->opcode, 0,
                            (ctx->insn_flags & ISA_MIPS32R6) ? 9 : 12);
        switch (minor) {
        case LWL:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_LWL;
            goto do_ld_lr;
        case SWL:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_SWL;
            goto do_st_lr;
        case LWR:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_LWR;
            goto do_ld_lr;
        case SWR:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_SWR;
            goto do_st_lr;
#if defined(TARGET_MIPS64)
        case LDL:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_LDL;
            goto do_ld_lr;
        case SDL:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_SDL;
            goto do_st_lr;
        case LDR:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_LDR;
            goto do_ld_lr;
        case SDR:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            mips32_op = OPC_SDR;
            goto do_st_lr;
        case LWU:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            mips32_op = OPC_LWU;
            goto do_ld_lr;
        case LLD:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            mips32_op = OPC_LLD;
            goto do_ld_lr;
#endif
        case LL:
            mips32_op = OPC_LL;
            goto do_ld_lr;
        do_ld_lr:
            gen_ld(ctx, mips32_op, rt, rs, offset);
            break;
        do_st_lr:
            gen_st(ctx, mips32_op, rt, rs, offset);
            break;
        case SC:
            gen_st_cond(ctx, OPC_SC, rt, rs, offset);
            break;
#if defined(TARGET_MIPS64)
        case SCD:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_st_cond(ctx, OPC_SCD, rt, rs, offset);
            break;
#endif
        case LD_EVA:
            if (!ctx->eva) {
                MIPS_INVAL("pool32c ld-eva");
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
            check_cp0_enabled(ctx);

            minor2 = (ctx->opcode >> 9) & 0x7;
            offset = sextract32(ctx->opcode, 0, 9);
            switch (minor2) {
            case LBUE:
                mips32_op = OPC_LBUE;
                goto do_ld_lr;
            case LHUE:
                mips32_op = OPC_LHUE;
                goto do_ld_lr;
            case LWLE:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_LWLE;
                goto do_ld_lr;
            case LWRE:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_LWRE;
                goto do_ld_lr;
            case LBE:
                mips32_op = OPC_LBE;
                goto do_ld_lr;
            case LHE:
                mips32_op = OPC_LHE;
                goto do_ld_lr;
            case LLE:
                mips32_op = OPC_LLE;
                goto do_ld_lr;
            case LWE:
                mips32_op = OPC_LWE;
                goto do_ld_lr;
            };
            break;
        case ST_EVA:
            if (!ctx->eva) {
                MIPS_INVAL("pool32c st-eva");
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
            check_cp0_enabled(ctx);

            minor2 = (ctx->opcode >> 9) & 0x7;
            offset = sextract32(ctx->opcode, 0, 9);
            switch (minor2) {
            case SWLE:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_SWLE;
                goto do_st_lr;
            case SWRE:
                check_insn_opc_removed(ctx, ISA_MIPS32R6);
                mips32_op = OPC_SWRE;
                goto do_st_lr;
            case PREFE:
                /* Treat as no-op */
                if ((ctx->insn_flags & ISA_MIPS32R6) && (rt >= 24)) {
                    /* hint codes 24-31 are reserved and signal RI */
                    generate_exception(ctx, EXCP_RI);
                }
                break;
            case CACHEE:
                /* Treat as no-op */
                if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
                    gen_cache_operation(ctx, rt, rs, offset);
                }
                break;
            case SBE:
                mips32_op = OPC_SBE;
                goto do_st_lr;
            case SHE:
                mips32_op = OPC_SHE;
                goto do_st_lr;
            case SCE:
                gen_st_cond(ctx, OPC_SCE, rt, rs, offset);
                break;
            case SWE:
                mips32_op = OPC_SWE;
                goto do_st_lr;
            };
            break;
        case PREF:
            /* Treat as no-op */
            if ((ctx->insn_flags & ISA_MIPS32R6) && (rt >= 24)) {
                /* hint codes 24-31 are reserved and signal RI */
                generate_exception(ctx, EXCP_RI);
            }
            break;
        default:
            MIPS_INVAL("pool32c");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case ADDI32: /* AUI, LUI */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* AUI, LUI */
            gen_logic_imm(ctx, OPC_LUI, rt, rs, imm);
        } else {
            /* ADDI32 */
            mips32_op = OPC_ADDI;
            goto do_addi;
        }
        break;
    case ADDIU32:
        mips32_op = OPC_ADDIU;
    do_addi:
        gen_arith_imm(ctx, mips32_op, rt, rs, imm);
        break;

        /* Logical operations */
    case ORI32:
        mips32_op = OPC_ORI;
        goto do_logici;
    case XORI32:
        mips32_op = OPC_XORI;
        goto do_logici;
    case ANDI32:
        mips32_op = OPC_ANDI;
    do_logici:
        gen_logic_imm(ctx, mips32_op, rt, rs, imm);
        break;

        /* Set less than immediate */
    case SLTI32:
        mips32_op = OPC_SLTI;
        goto do_slti;
    case SLTIU32:
        mips32_op = OPC_SLTIU;
    do_slti:
        gen_slt_imm(ctx, mips32_op, rt, rs, imm);
        break;
    case JALX32:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 2;
        gen_compute_branch(ctx, OPC_JALX, 4, rt, rs, offset, 4);
        ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        break;
    case JALS32: /* BOVC, BEQC, BEQZALC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            if (rs >= rt) {
                /* BOVC */
                mips32_op = OPC_BOVC;
            } else if (rs < rt && rs == 0) {
                /* BEQZALC */
                mips32_op = OPC_BEQZALC;
            } else {
                /* BEQC */
                mips32_op = OPC_BEQC;
            }
            gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        } else {
            /* JALS32 */
            offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 1;
            gen_compute_branch(ctx, OPC_JAL, 4, rt, rs, offset, 2);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        }
        break;
    case BEQ32: /* BC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* BC */
            gen_compute_compact_branch(ctx, OPC_BC, 0, 0,
                                       sextract32(ctx->opcode << 1, 0, 27));
        } else {
            /* BEQ32 */
            gen_compute_branch(ctx, OPC_BEQ, 4, rt, rs, imm << 1, 4);
        }
        break;
    case BNE32: /* BALC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* BALC */
            gen_compute_compact_branch(ctx, OPC_BALC, 0, 0,
                                       sextract32(ctx->opcode << 1, 0, 27));
        } else {
            /* BNE32 */
            gen_compute_branch(ctx, OPC_BNE, 4, rt, rs, imm << 1, 4);
        }
        break;
    case J32: /* BGTZC, BLTZC, BLTC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            if (rs == 0 && rt != 0) {
                /* BGTZC */
                mips32_op = OPC_BGTZC;
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* BLTZC */
                mips32_op = OPC_BLTZC;
            } else {
                /* BLTC */
                mips32_op = OPC_BLTC;
            }
            gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        } else {
            /* J32 */
            gen_compute_branch(ctx, OPC_J, 4, rt, rs,
                               (int32_t)(ctx->opcode & 0x3FFFFFF) << 1, 4);
        }
        break;
    case JAL32: /* BLEZC, BGEZC, BGEC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            if (rs == 0 && rt != 0) {
                /* BLEZC */
                mips32_op = OPC_BLEZC;
            } else if (rs != 0 && rt != 0 && rs == rt) {
                /* BGEZC */
                mips32_op = OPC_BGEZC;
            } else {
                /* BGEC */
                mips32_op = OPC_BGEC;
            }
            gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        } else {
            /* JAL32 */
            gen_compute_branch(ctx, OPC_JAL, 4, rt, rs,
                               (int32_t)(ctx->opcode & 0x3FFFFFF) << 1, 4);
            ctx->hflags |= MIPS_HFLAG_BDS_STRICT;
        }
        break;
        /* Floating point (COP1) */
    case LWC132:
        mips32_op = OPC_LWC1;
        goto do_cop1;
    case LDC132:
        mips32_op = OPC_LDC1;
        goto do_cop1;
    case SWC132:
        mips32_op = OPC_SWC1;
        goto do_cop1;
    case SDC132:
        mips32_op = OPC_SDC1;
    do_cop1:
        gen_cop1_ldst(ctx, mips32_op, rt, rs, imm);
        break;
    case ADDIUPC: /* PCREL: ADDIUPC, AUIPC, ALUIPC, LWPC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* PCREL: ADDIUPC, AUIPC, ALUIPC, LWPC */
            switch ((ctx->opcode >> 16) & 0x1f) {
            case ADDIUPC_00 ... ADDIUPC_07:
                gen_pcrel(ctx, OPC_ADDIUPC, ctx->pc & ~0x3, rt);
                break;
            case AUIPC:
                gen_pcrel(ctx, OPC_AUIPC, ctx->pc, rt);
                break;
            case ALUIPC:
                gen_pcrel(ctx, OPC_ALUIPC, ctx->pc, rt);
                break;
            case LWPC_08 ... LWPC_0F:
                gen_pcrel(ctx, R6_OPC_LWPC, ctx->pc & ~0x3, rt);
                break;
            default:
                generate_exception(ctx, EXCP_RI);
                break;
            }
        } else {
            /* ADDIUPC */
            int reg = mmreg(ZIMM(ctx->opcode, 23, 3));
            int offset = SIMM(ctx->opcode, 0, 23) << 2;

            gen_addiupc(ctx, reg, offset, 0, 0);
        }
        break;
    case BNVC: /* BNEC, BNEZALC */
        check_insn(ctx, ISA_MIPS32R6);
        if (rs >= rt) {
            /* BNVC */
            mips32_op = OPC_BNVC;
        } else if (rs < rt && rs == 0) {
            /* BNEZALC */
            mips32_op = OPC_BNEZALC;
        } else {
            /* BNEC */
            mips32_op = OPC_BNEC;
        }
        gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        break;
    case R6_BNEZC: /* JIALC */
        check_insn(ctx, ISA_MIPS32R6);
        if (rt != 0) {
            /* BNEZC */
            gen_compute_compact_branch(ctx, OPC_BNEZC, rt, 0,
                                       sextract32(ctx->opcode << 1, 0, 22));
        } else {
            /* JIALC */
            gen_compute_compact_branch(ctx, OPC_JIALC, 0, rs, imm);
        }
        break;
    case R6_BEQZC: /* JIC */
        check_insn(ctx, ISA_MIPS32R6);
        if (rt != 0) {
            /* BEQZC */
            gen_compute_compact_branch(ctx, OPC_BEQZC, rt, 0,
                                       sextract32(ctx->opcode << 1, 0, 22));
        } else {
            /* JIC */
            gen_compute_compact_branch(ctx, OPC_JIC, 0, rs, imm);
        }
        break;
    case BLEZALC: /* BGEZALC, BGEUC */
        check_insn(ctx, ISA_MIPS32R6);
        if (rs == 0 && rt != 0) {
            /* BLEZALC */
            mips32_op = OPC_BLEZALC;
        } else if (rs != 0 && rt != 0 && rs == rt) {
            /* BGEZALC */
            mips32_op = OPC_BGEZALC;
        } else {
            /* BGEUC */
            mips32_op = OPC_BGEUC;
        }
        gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        break;
    case BGTZALC: /* BLTZALC, BLTUC */
        check_insn(ctx, ISA_MIPS32R6);
        if (rs == 0 && rt != 0) {
            /* BGTZALC */
            mips32_op = OPC_BGTZALC;
        } else if (rs != 0 && rt != 0 && rs == rt) {
            /* BLTZALC */
            mips32_op = OPC_BLTZALC;
        } else {
            /* BLTUC */
            mips32_op = OPC_BLTUC;
        }
        gen_compute_compact_branch(ctx, mips32_op, rs, rt, imm << 1);
        break;
        /* Loads and stores */
    case LB32:
        mips32_op = OPC_LB;
        goto do_ld;
    case LBU32:
        mips32_op = OPC_LBU;
        goto do_ld;
    case LH32:
        mips32_op = OPC_LH;
        goto do_ld;
    case LHU32:
        mips32_op = OPC_LHU;
        goto do_ld;
    case LW32:
        mips32_op = OPC_LW;
        goto do_ld;
#ifdef TARGET_MIPS64
    case LD32:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        mips32_op = OPC_LD;
        goto do_ld;
    case SD32:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        mips32_op = OPC_SD;
        goto do_st;
#endif
    case SB32:
        mips32_op = OPC_SB;
        goto do_st;
    case SH32:
        mips32_op = OPC_SH;
        goto do_st;
    case SW32:
        mips32_op = OPC_SW;
        goto do_st;
    do_ld:
        gen_ld(ctx, mips32_op, rt, rs, imm);
        break;
    do_st:
        gen_st(ctx, mips32_op, rt, rs, imm);
        break;
    default:
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static int decode_micromips_opc (CPUMIPSState *env, DisasContext *ctx)
{
    uint32_t op;

    /* make sure instructions are on a halfword boundary */
    if (ctx->pc & 0x1) {
        env->CP0_BadVAddr = ctx->pc;
        generate_exception_end(ctx, EXCP_AdEL);
        return 2;
    }

    op = (ctx->opcode >> 10) & 0x3f;
    /* Enforce properly-sized instructions in a delay slot */
    if (ctx->hflags & MIPS_HFLAG_BDS_STRICT) {
        switch (op & 0x7) { /* MSB-3..MSB-5 */
        case 0:
        /* POOL32A, POOL32B, POOL32I, POOL32C */
        case 4:
        /* ADDI32, ADDIU32, ORI32, XORI32, SLTI32, SLTIU32, ANDI32, JALX32 */
        case 5:
        /* LBU32, LHU32, POOL32F, JALS32, BEQ32, BNE32, J32, JAL32 */
        case 6:
        /* SB32, SH32, ADDIUPC, SWC132, SDC132, SW32 */
        case 7:
        /* LB32, LH32, LWC132, LDC132, LW32 */
            if (ctx->hflags & MIPS_HFLAG_BDS16) {
                generate_exception_end(ctx, EXCP_RI);
                return 2;
            }
            break;
        case 1:
        /* POOL16A, POOL16B, POOL16C, LWGP16, POOL16F */
        case 2:
        /* LBU16, LHU16, LWSP16, LW16, SB16, SH16, SWSP16, SW16 */
        case 3:
        /* MOVE16, ANDI16, POOL16D, POOL16E, BEQZ16, BNEZ16, B16, LI16 */
            if (ctx->hflags & MIPS_HFLAG_BDS32) {
                generate_exception_end(ctx, EXCP_RI);
                return 2;
            }
            break;
        }
    }

    switch (op) {
    case POOL16A:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rs1 = mmreg(uMIPS_RS1(ctx->opcode));
            int rs2 = mmreg(uMIPS_RS2(ctx->opcode));
            uint32_t opc = 0;

            switch (ctx->opcode & 0x1) {
            case ADDU16:
                opc = OPC_ADDU;
                break;
            case SUBU16:
                opc = OPC_SUBU;
                break;
            }
            if (ctx->insn_flags & ISA_MIPS32R6) {
                /* In the Release 6 the register number location in
                 * the instruction encoding has changed.
                 */
                gen_arith(ctx, opc, rs1, rd, rs2);
            } else {
                gen_arith(ctx, opc, rd, rs1, rs2);
            }
        }
        break;
    case POOL16B:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rs = mmreg(uMIPS_RS(ctx->opcode));
            int amount = (ctx->opcode >> 1) & 0x7;
            uint32_t opc = 0;
            amount = amount == 0 ? 8 : amount;

            switch (ctx->opcode & 0x1) {
            case SLL16:
                opc = OPC_SLL;
                break;
            case SRL16:
                opc = OPC_SRL;
                break;
            }

            gen_shift_imm(ctx, opc, rd, rs, amount);
        }
        break;
    case POOL16C:
        if (ctx->insn_flags & ISA_MIPS32R6) {
            gen_pool16c_r6_insn(ctx);
        } else {
            gen_pool16c_insn(ctx);
        }
        break;
    case LWGP16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = 28;            /* GP */
            int16_t offset = SIMM(ctx->opcode, 0, 7) << 2;

            gen_ld(ctx, OPC_LW, rd, rb, offset);
        }
        break;
    case POOL16F:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        if (ctx->opcode & 1) {
            generate_exception_end(ctx, EXCP_RI);
        } else {
            /* MOVEP */
            int enc_dest = uMIPS_RD(ctx->opcode);
            int enc_rt = uMIPS_RS2(ctx->opcode);
            int enc_rs = uMIPS_RS1(ctx->opcode);
            gen_movep(ctx, enc_dest, enc_rt, enc_rs);
        }
        break;
    case LBU16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4);
            offset = (offset == 0xf ? -1 : offset);

            gen_ld(ctx, OPC_LBU, rd, rb, offset);
        }
        break;
    case LHU16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 1;

            gen_ld(ctx, OPC_LHU, rd, rb, offset);
        }
        break;
    case LWSP16:
        {
            int rd = (ctx->opcode >> 5) & 0x1f;
            int rb = 29;            /* SP */
            int16_t offset = ZIMM(ctx->opcode, 0, 5) << 2;

            gen_ld(ctx, OPC_LW, rd, rb, offset);
        }
        break;
    case LW16:
        {
            int rd = mmreg(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 2;

            gen_ld(ctx, OPC_LW, rd, rb, offset);
        }
        break;
    case SB16:
        {
            int rd = mmreg2(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4);

            gen_st(ctx, OPC_SB, rd, rb, offset);
        }
        break;
    case SH16:
        {
            int rd = mmreg2(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 1;

            gen_st(ctx, OPC_SH, rd, rb, offset);
        }
        break;
    case SWSP16:
        {
            int rd = (ctx->opcode >> 5) & 0x1f;
            int rb = 29;            /* SP */
            int16_t offset = ZIMM(ctx->opcode, 0, 5) << 2;

            gen_st(ctx, OPC_SW, rd, rb, offset);
        }
        break;
    case SW16:
        {
            int rd = mmreg2(uMIPS_RD(ctx->opcode));
            int rb = mmreg(uMIPS_RS(ctx->opcode));
            int16_t offset = ZIMM(ctx->opcode, 0, 4) << 2;

            gen_st(ctx, OPC_SW, rd, rb, offset);
        }
        break;
    case MOVE16:
        {
            int rd = uMIPS_RD5(ctx->opcode);
            int rs = uMIPS_RS5(ctx->opcode);

            gen_arith(ctx, OPC_ADDU, rd, rs, 0);
        }
        break;
    case ANDI16:
        gen_andi16(ctx);
        break;
    case POOL16D:
        switch (ctx->opcode & 0x1) {
        case ADDIUS5:
            gen_addius5(ctx);
            break;
        case ADDIUSP:
            gen_addiusp(ctx);
            break;
        }
        break;
    case POOL16E:
        switch (ctx->opcode & 0x1) {
        case ADDIUR2:
            gen_addiur2(ctx);
            break;
        case ADDIUR1SP:
            gen_addiur1sp(ctx);
            break;
        }
        break;
    case B16: /* BC16 */
        gen_compute_branch(ctx, OPC_BEQ, 2, 0, 0,
                           sextract32(ctx->opcode, 0, 10) << 1,
                           (ctx->insn_flags & ISA_MIPS32R6) ? 0 : 4);
        break;
    case BNEZ16: /* BNEZC16 */
    case BEQZ16: /* BEQZC16 */
        gen_compute_branch(ctx, op == BNEZ16 ? OPC_BNE : OPC_BEQ, 2,
                           mmreg(uMIPS_RD(ctx->opcode)),
                           0, sextract32(ctx->opcode, 0, 7) << 1,
                           (ctx->insn_flags & ISA_MIPS32R6) ? 0 : 4);

        break;
    case LI16:
        {
            int reg = mmreg(uMIPS_RD(ctx->opcode));
            int imm = ZIMM(ctx->opcode, 0, 7);

            imm = (imm == 0x7f ? -1 : imm);
            tcg_gen_movi_tl(cpu_gpr[reg], imm);
        }
        break;
    case RES_29:
    case RES_31:
    case RES_39:
        generate_exception_end(ctx, EXCP_RI);
        break;
    default:
        decode_micromips32_opc(env, ctx);
        return 4;
    }

    return 2;
}

/* SmartMIPS extension to MIPS32 */

#if defined(TARGET_MIPS64)

/* MDMX extension to MIPS64 */

#endif

/* MIPSDSP functions. */
static void gen_mipsdsp_ld(DisasContext *ctx, uint32_t opc,
                           int rd, int base, int offset)
{
    TCGv t0;

    check_dsp(ctx);
    t0 = tcg_temp_new();

    if (base == 0) {
        gen_load_gpr(t0, offset);
    } else if (offset == 0) {
        gen_load_gpr(t0, base);
    } else {
        gen_op_addr_add(ctx, t0, cpu_gpr[base], cpu_gpr[offset]);
    }

    switch (opc) {
    case OPC_LBUX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_UB);
        gen_store_gpr(t0, rd);
        break;
    case OPC_LHX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESW);
        gen_store_gpr(t0, rd);
        break;
    case OPC_LWX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TESL);
        gen_store_gpr(t0, rd);
        break;
#if defined(TARGET_MIPS64)
    case OPC_LDX:
        tcg_gen_qemu_ld_tl(t0, t0, ctx->mem_idx, MO_TEQ);
        gen_store_gpr(t0, rd);
        break;
#endif
    }
    tcg_temp_free(t0);
}

static void gen_mipsdsp_arith(DisasContext *ctx, uint32_t op1, uint32_t op2,
                              int ret, int v1, int v2)
{
    TCGv v1_t;
    TCGv v2_t;

    if (ret == 0) {
        /* Treat as NOP. */
        return;
    }

    v1_t = tcg_temp_new();
    v2_t = tcg_temp_new();

    gen_load_gpr(v1_t, v1);
    gen_load_gpr(v2_t, v2);

    switch (op1) {
    /* OPC_MULT_G_2E is equal OPC_ADDUH_QB_DSP */
    case OPC_MULT_G_2E:
        check_dspr2(ctx);
        switch (op2) {
        case OPC_ADDUH_QB:
            gen_helper_adduh_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDUH_R_QB:
            gen_helper_adduh_r_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDQH_PH:
            gen_helper_addqh_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDQH_R_PH:
            gen_helper_addqh_r_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDQH_W:
            gen_helper_addqh_w(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDQH_R_W:
            gen_helper_addqh_r_w(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBUH_QB:
            gen_helper_subuh_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBUH_R_QB:
            gen_helper_subuh_r_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBQH_PH:
            gen_helper_subqh_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBQH_R_PH:
            gen_helper_subqh_r_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBQH_W:
            gen_helper_subqh_w(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBQH_R_W:
            gen_helper_subqh_r_w(cpu_gpr[ret], v1_t, v2_t);
            break;
        }
        break;
    case OPC_ABSQ_S_PH_DSP:
        switch (op2) {
        case OPC_ABSQ_S_QB:
            check_dspr2(ctx);
            gen_helper_absq_s_qb(cpu_gpr[ret], v2_t, cpu_env);
            break;
        case OPC_ABSQ_S_PH:
            check_dsp(ctx);
            gen_helper_absq_s_ph(cpu_gpr[ret], v2_t, cpu_env);
            break;
        case OPC_ABSQ_S_W:
            check_dsp(ctx);
            gen_helper_absq_s_w(cpu_gpr[ret], v2_t, cpu_env);
            break;
        case OPC_PRECEQ_W_PHL:
            check_dsp(ctx);
            tcg_gen_andi_tl(cpu_gpr[ret], v2_t, 0xFFFF0000);
            tcg_gen_ext32s_tl(cpu_gpr[ret], cpu_gpr[ret]);
            break;
        case OPC_PRECEQ_W_PHR:
            check_dsp(ctx);
            tcg_gen_andi_tl(cpu_gpr[ret], v2_t, 0x0000FFFF);
            tcg_gen_shli_tl(cpu_gpr[ret], cpu_gpr[ret], 16);
            tcg_gen_ext32s_tl(cpu_gpr[ret], cpu_gpr[ret]);
            break;
        case OPC_PRECEQU_PH_QBL:
            check_dsp(ctx);
            gen_helper_precequ_ph_qbl(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_PH_QBR:
            check_dsp(ctx);
            gen_helper_precequ_ph_qbr(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_PH_QBLA:
            check_dsp(ctx);
            gen_helper_precequ_ph_qbla(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_PH_QBRA:
            check_dsp(ctx);
            gen_helper_precequ_ph_qbra(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_PH_QBL:
            check_dsp(ctx);
            gen_helper_preceu_ph_qbl(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_PH_QBR:
            check_dsp(ctx);
            gen_helper_preceu_ph_qbr(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_PH_QBLA:
            check_dsp(ctx);
            gen_helper_preceu_ph_qbla(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_PH_QBRA:
            check_dsp(ctx);
            gen_helper_preceu_ph_qbra(cpu_gpr[ret], v2_t);
            break;
        }
        break;
    case OPC_ADDU_QB_DSP:
        switch (op2) {
        case OPC_ADDQ_PH:
            check_dsp(ctx);
            gen_helper_addq_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDQ_S_PH:
            check_dsp(ctx);
            gen_helper_addq_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDQ_S_W:
            check_dsp(ctx);
            gen_helper_addq_s_w(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_QB:
            check_dsp(ctx);
            gen_helper_addu_qb(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_S_QB:
            check_dsp(ctx);
            gen_helper_addu_s_qb(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_PH:
            check_dspr2(ctx);
            gen_helper_addu_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_S_PH:
            check_dspr2(ctx);
            gen_helper_addu_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBQ_PH:
            check_dsp(ctx);
            gen_helper_subq_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBQ_S_PH:
            check_dsp(ctx);
            gen_helper_subq_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBQ_S_W:
            check_dsp(ctx);
            gen_helper_subq_s_w(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_QB:
            check_dsp(ctx);
            gen_helper_subu_qb(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_S_QB:
            check_dsp(ctx);
            gen_helper_subu_s_qb(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_PH:
            check_dspr2(ctx);
            gen_helper_subu_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_S_PH:
            check_dspr2(ctx);
            gen_helper_subu_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDSC:
            check_dsp(ctx);
            gen_helper_addsc(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDWC:
            check_dsp(ctx);
            gen_helper_addwc(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MODSUB:
            check_dsp(ctx);
            gen_helper_modsub(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_RADDU_W_QB:
            check_dsp(ctx);
            gen_helper_raddu_w_qb(cpu_gpr[ret], v1_t);
            break;
        }
        break;
    case OPC_CMPU_EQ_QB_DSP:
        switch (op2) {
        case OPC_PRECR_QB_PH:
            check_dspr2(ctx);
            gen_helper_precr_qb_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECRQ_QB_PH:
            check_dsp(ctx);
            gen_helper_precrq_qb_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECR_SRA_PH_W:
            check_dspr2(ctx);
            {
                TCGv_i32 sa_t = tcg_const_i32(v2);
                gen_helper_precr_sra_ph_w(cpu_gpr[ret], sa_t, v1_t,
                                          cpu_gpr[ret]);
                tcg_temp_free_i32(sa_t);
                break;
            }
        case OPC_PRECR_SRA_R_PH_W:
            check_dspr2(ctx);
            {
                TCGv_i32 sa_t = tcg_const_i32(v2);
                gen_helper_precr_sra_r_ph_w(cpu_gpr[ret], sa_t, v1_t,
                                            cpu_gpr[ret]);
                tcg_temp_free_i32(sa_t);
                break;
            }
        case OPC_PRECRQ_PH_W:
            check_dsp(ctx);
            gen_helper_precrq_ph_w(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECRQ_RS_PH_W:
            check_dsp(ctx);
            gen_helper_precrq_rs_ph_w(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_PRECRQU_S_QB_PH:
            check_dsp(ctx);
            gen_helper_precrqu_s_qb_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_ABSQ_S_QH_DSP:
        switch (op2) {
        case OPC_PRECEQ_L_PWL:
            check_dsp(ctx);
            tcg_gen_andi_tl(cpu_gpr[ret], v2_t, 0xFFFFFFFF00000000ull);
            break;
        case OPC_PRECEQ_L_PWR:
            check_dsp(ctx);
            tcg_gen_shli_tl(cpu_gpr[ret], v2_t, 32);
            break;
        case OPC_PRECEQ_PW_QHL:
            check_dsp(ctx);
            gen_helper_preceq_pw_qhl(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQ_PW_QHR:
            check_dsp(ctx);
            gen_helper_preceq_pw_qhr(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQ_PW_QHLA:
            check_dsp(ctx);
            gen_helper_preceq_pw_qhla(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQ_PW_QHRA:
            check_dsp(ctx);
            gen_helper_preceq_pw_qhra(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_QH_OBL:
            check_dsp(ctx);
            gen_helper_precequ_qh_obl(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_QH_OBR:
            check_dsp(ctx);
            gen_helper_precequ_qh_obr(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_QH_OBLA:
            check_dsp(ctx);
            gen_helper_precequ_qh_obla(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEQU_QH_OBRA:
            check_dsp(ctx);
            gen_helper_precequ_qh_obra(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_QH_OBL:
            check_dsp(ctx);
            gen_helper_preceu_qh_obl(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_QH_OBR:
            check_dsp(ctx);
            gen_helper_preceu_qh_obr(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_QH_OBLA:
            check_dsp(ctx);
            gen_helper_preceu_qh_obla(cpu_gpr[ret], v2_t);
            break;
        case OPC_PRECEU_QH_OBRA:
            check_dsp(ctx);
            gen_helper_preceu_qh_obra(cpu_gpr[ret], v2_t);
            break;
        case OPC_ABSQ_S_OB:
            check_dspr2(ctx);
            gen_helper_absq_s_ob(cpu_gpr[ret], v2_t, cpu_env);
            break;
        case OPC_ABSQ_S_PW:
            check_dsp(ctx);
            gen_helper_absq_s_pw(cpu_gpr[ret], v2_t, cpu_env);
            break;
        case OPC_ABSQ_S_QH:
            check_dsp(ctx);
            gen_helper_absq_s_qh(cpu_gpr[ret], v2_t, cpu_env);
            break;
        }
        break;
    case OPC_ADDU_OB_DSP:
        switch (op2) {
        case OPC_RADDU_L_OB:
            check_dsp(ctx);
            gen_helper_raddu_l_ob(cpu_gpr[ret], v1_t);
            break;
        case OPC_SUBQ_PW:
            check_dsp(ctx);
            gen_helper_subq_pw(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBQ_S_PW:
            check_dsp(ctx);
            gen_helper_subq_s_pw(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBQ_QH:
            check_dsp(ctx);
            gen_helper_subq_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBQ_S_QH:
            check_dsp(ctx);
            gen_helper_subq_s_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_OB:
            check_dsp(ctx);
            gen_helper_subu_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_S_OB:
            check_dsp(ctx);
            gen_helper_subu_s_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_QH:
            check_dspr2(ctx);
            gen_helper_subu_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBU_S_QH:
            check_dspr2(ctx);
            gen_helper_subu_s_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_SUBUH_OB:
            check_dspr2(ctx);
            gen_helper_subuh_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_SUBUH_R_OB:
            check_dspr2(ctx);
            gen_helper_subuh_r_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDQ_PW:
            check_dsp(ctx);
            gen_helper_addq_pw(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDQ_S_PW:
            check_dsp(ctx);
            gen_helper_addq_s_pw(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDQ_QH:
            check_dsp(ctx);
            gen_helper_addq_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDQ_S_QH:
            check_dsp(ctx);
            gen_helper_addq_s_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_OB:
            check_dsp(ctx);
            gen_helper_addu_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_S_OB:
            check_dsp(ctx);
            gen_helper_addu_s_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_QH:
            check_dspr2(ctx);
            gen_helper_addu_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDU_S_QH:
            check_dspr2(ctx);
            gen_helper_addu_s_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_ADDUH_OB:
            check_dspr2(ctx);
            gen_helper_adduh_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_ADDUH_R_OB:
            check_dspr2(ctx);
            gen_helper_adduh_r_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        }
        break;
    case OPC_CMPU_EQ_OB_DSP:
        switch (op2) {
        case OPC_PRECR_OB_QH:
            check_dspr2(ctx);
            gen_helper_precr_ob_qh(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECR_SRA_QH_PW:
            check_dspr2(ctx);
            {
                TCGv_i32 ret_t = tcg_const_i32(ret);
                gen_helper_precr_sra_qh_pw(v2_t, v1_t, v2_t, ret_t);
                tcg_temp_free_i32(ret_t);
                break;
            }
        case OPC_PRECR_SRA_R_QH_PW:
            check_dspr2(ctx);
            {
                TCGv_i32 sa_v = tcg_const_i32(ret);
                gen_helper_precr_sra_r_qh_pw(v2_t, v1_t, v2_t, sa_v);
                tcg_temp_free_i32(sa_v);
                break;
            }
        case OPC_PRECRQ_OB_QH:
            check_dsp(ctx);
            gen_helper_precrq_ob_qh(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECRQ_PW_L:
            check_dsp(ctx);
            gen_helper_precrq_pw_l(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECRQ_QH_PW:
            check_dsp(ctx);
            gen_helper_precrq_qh_pw(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PRECRQ_RS_QH_PW:
            check_dsp(ctx);
            gen_helper_precrq_rs_qh_pw(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_PRECRQU_S_OB_QH:
            check_dsp(ctx);
            gen_helper_precrqu_s_ob_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        }
        break;
#endif
    }

    tcg_temp_free(v1_t);
    tcg_temp_free(v2_t);
}

static void gen_mipsdsp_shift(DisasContext *ctx, uint32_t opc,
                              int ret, int v1, int v2)
{
    uint32_t op2;
    TCGv t0;
    TCGv v1_t;
    TCGv v2_t;

    if (ret == 0) {
        /* Treat as NOP. */
        return;
    }

    t0 = tcg_temp_new();
    v1_t = tcg_temp_new();
    v2_t = tcg_temp_new();

    tcg_gen_movi_tl(t0, v1);
    gen_load_gpr(v1_t, v1);
    gen_load_gpr(v2_t, v2);

    switch (opc) {
    case OPC_SHLL_QB_DSP:
        {
            op2 = MASK_SHLL_QB(ctx->opcode);
            switch (op2) {
            case OPC_SHLL_QB:
                check_dsp(ctx);
                gen_helper_shll_qb(cpu_gpr[ret], t0, v2_t, cpu_env);
                break;
            case OPC_SHLLV_QB:
                check_dsp(ctx);
                gen_helper_shll_qb(cpu_gpr[ret], v1_t, v2_t, cpu_env);
                break;
            case OPC_SHLL_PH:
                check_dsp(ctx);
                gen_helper_shll_ph(cpu_gpr[ret], t0, v2_t, cpu_env);
                break;
            case OPC_SHLLV_PH:
                check_dsp(ctx);
                gen_helper_shll_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
                break;
            case OPC_SHLL_S_PH:
                check_dsp(ctx);
                gen_helper_shll_s_ph(cpu_gpr[ret], t0, v2_t, cpu_env);
                break;
            case OPC_SHLLV_S_PH:
                check_dsp(ctx);
                gen_helper_shll_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
                break;
            case OPC_SHLL_S_W:
                check_dsp(ctx);
                gen_helper_shll_s_w(cpu_gpr[ret], t0, v2_t, cpu_env);
                break;
            case OPC_SHLLV_S_W:
                check_dsp(ctx);
                gen_helper_shll_s_w(cpu_gpr[ret], v1_t, v2_t, cpu_env);
                break;
            case OPC_SHRL_QB:
                check_dsp(ctx);
                gen_helper_shrl_qb(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRLV_QB:
                check_dsp(ctx);
                gen_helper_shrl_qb(cpu_gpr[ret], v1_t, v2_t);
                break;
            case OPC_SHRL_PH:
                check_dspr2(ctx);
                gen_helper_shrl_ph(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRLV_PH:
                check_dspr2(ctx);
                gen_helper_shrl_ph(cpu_gpr[ret], v1_t, v2_t);
                break;
            case OPC_SHRA_QB:
                check_dspr2(ctx);
                gen_helper_shra_qb(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRA_R_QB:
                check_dspr2(ctx);
                gen_helper_shra_r_qb(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRAV_QB:
                check_dspr2(ctx);
                gen_helper_shra_qb(cpu_gpr[ret], v1_t, v2_t);
                break;
            case OPC_SHRAV_R_QB:
                check_dspr2(ctx);
                gen_helper_shra_r_qb(cpu_gpr[ret], v1_t, v2_t);
                break;
            case OPC_SHRA_PH:
                check_dsp(ctx);
                gen_helper_shra_ph(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRA_R_PH:
                check_dsp(ctx);
                gen_helper_shra_r_ph(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRAV_PH:
                check_dsp(ctx);
                gen_helper_shra_ph(cpu_gpr[ret], v1_t, v2_t);
                break;
            case OPC_SHRAV_R_PH:
                check_dsp(ctx);
                gen_helper_shra_r_ph(cpu_gpr[ret], v1_t, v2_t);
                break;
            case OPC_SHRA_R_W:
                check_dsp(ctx);
                gen_helper_shra_r_w(cpu_gpr[ret], t0, v2_t);
                break;
            case OPC_SHRAV_R_W:
                check_dsp(ctx);
                gen_helper_shra_r_w(cpu_gpr[ret], v1_t, v2_t);
                break;
            default:            /* Invalid */
                MIPS_INVAL("MASK SHLL.QB");
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
            break;
        }
#ifdef TARGET_MIPS64
    case OPC_SHLL_OB_DSP:
        op2 = MASK_SHLL_OB(ctx->opcode);
        switch (op2) {
        case OPC_SHLL_PW:
            check_dsp(ctx);
            gen_helper_shll_pw(cpu_gpr[ret], v2_t, t0, cpu_env);
            break;
        case OPC_SHLLV_PW:
            check_dsp(ctx);
            gen_helper_shll_pw(cpu_gpr[ret], v2_t, v1_t, cpu_env);
            break;
        case OPC_SHLL_S_PW:
            check_dsp(ctx);
            gen_helper_shll_s_pw(cpu_gpr[ret], v2_t, t0, cpu_env);
            break;
        case OPC_SHLLV_S_PW:
            check_dsp(ctx);
            gen_helper_shll_s_pw(cpu_gpr[ret], v2_t, v1_t, cpu_env);
            break;
        case OPC_SHLL_OB:
            check_dsp(ctx);
            gen_helper_shll_ob(cpu_gpr[ret], v2_t, t0, cpu_env);
            break;
        case OPC_SHLLV_OB:
            check_dsp(ctx);
            gen_helper_shll_ob(cpu_gpr[ret], v2_t, v1_t, cpu_env);
            break;
        case OPC_SHLL_QH:
            check_dsp(ctx);
            gen_helper_shll_qh(cpu_gpr[ret], v2_t, t0, cpu_env);
            break;
        case OPC_SHLLV_QH:
            check_dsp(ctx);
            gen_helper_shll_qh(cpu_gpr[ret], v2_t, v1_t, cpu_env);
            break;
        case OPC_SHLL_S_QH:
            check_dsp(ctx);
            gen_helper_shll_s_qh(cpu_gpr[ret], v2_t, t0, cpu_env);
            break;
        case OPC_SHLLV_S_QH:
            check_dsp(ctx);
            gen_helper_shll_s_qh(cpu_gpr[ret], v2_t, v1_t, cpu_env);
            break;
        case OPC_SHRA_OB:
            check_dspr2(ctx);
            gen_helper_shra_ob(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRAV_OB:
            check_dspr2(ctx);
            gen_helper_shra_ob(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRA_R_OB:
            check_dspr2(ctx);
            gen_helper_shra_r_ob(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRAV_R_OB:
            check_dspr2(ctx);
            gen_helper_shra_r_ob(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRA_PW:
            check_dsp(ctx);
            gen_helper_shra_pw(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRAV_PW:
            check_dsp(ctx);
            gen_helper_shra_pw(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRA_R_PW:
            check_dsp(ctx);
            gen_helper_shra_r_pw(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRAV_R_PW:
            check_dsp(ctx);
            gen_helper_shra_r_pw(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRA_QH:
            check_dsp(ctx);
            gen_helper_shra_qh(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRAV_QH:
            check_dsp(ctx);
            gen_helper_shra_qh(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRA_R_QH:
            check_dsp(ctx);
            gen_helper_shra_r_qh(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRAV_R_QH:
            check_dsp(ctx);
            gen_helper_shra_r_qh(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRL_OB:
            check_dsp(ctx);
            gen_helper_shrl_ob(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRLV_OB:
            check_dsp(ctx);
            gen_helper_shrl_ob(cpu_gpr[ret], v2_t, v1_t);
            break;
        case OPC_SHRL_QH:
            check_dspr2(ctx);
            gen_helper_shrl_qh(cpu_gpr[ret], v2_t, t0);
            break;
        case OPC_SHRLV_QH:
            check_dspr2(ctx);
            gen_helper_shrl_qh(cpu_gpr[ret], v2_t, v1_t);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK SHLL.OB");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    }

    tcg_temp_free(t0);
    tcg_temp_free(v1_t);
    tcg_temp_free(v2_t);
}

static void gen_mipsdsp_multiply(DisasContext *ctx, uint32_t op1, uint32_t op2,
                                 int ret, int v1, int v2, int check_ret)
{
    TCGv_i32 t0;
    TCGv v1_t;
    TCGv v2_t;

    if ((ret == 0) && (check_ret == 1)) {
        /* Treat as NOP. */
        return;
    }

    t0 = tcg_temp_new_i32();
    v1_t = tcg_temp_new();
    v2_t = tcg_temp_new();

    tcg_gen_movi_i32(t0, ret);
    gen_load_gpr(v1_t, v1);
    gen_load_gpr(v2_t, v2);

    switch (op1) {
    /* OPC_MULT_G_2E, OPC_ADDUH_QB_DSP, OPC_MUL_PH_DSP have
     * the same mask and op1. */
    case OPC_MULT_G_2E:
        check_dspr2(ctx);
        switch (op2) {
        case  OPC_MUL_PH:
            gen_helper_mul_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case  OPC_MUL_S_PH:
            gen_helper_mul_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULQ_S_W:
            gen_helper_mulq_s_w(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULQ_RS_W:
            gen_helper_mulq_rs_w(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        }
        break;
    case OPC_DPA_W_PH_DSP:
        switch (op2) {
        case OPC_DPAU_H_QBL:
            check_dsp(ctx);
            gen_helper_dpau_h_qbl(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPAU_H_QBR:
            check_dsp(ctx);
            gen_helper_dpau_h_qbr(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSU_H_QBL:
            check_dsp(ctx);
            gen_helper_dpsu_h_qbl(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSU_H_QBR:
            check_dsp(ctx);
            gen_helper_dpsu_h_qbr(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPA_W_PH:
            check_dspr2(ctx);
            gen_helper_dpa_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPAX_W_PH:
            check_dspr2(ctx);
            gen_helper_dpax_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPAQ_S_W_PH:
            check_dsp(ctx);
            gen_helper_dpaq_s_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPAQX_S_W_PH:
            check_dspr2(ctx);
            gen_helper_dpaqx_s_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPAQX_SA_W_PH:
            check_dspr2(ctx);
            gen_helper_dpaqx_sa_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPS_W_PH:
            check_dspr2(ctx);
            gen_helper_dps_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSX_W_PH:
            check_dspr2(ctx);
            gen_helper_dpsx_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSQ_S_W_PH:
            check_dsp(ctx);
            gen_helper_dpsq_s_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSQX_S_W_PH:
            check_dspr2(ctx);
            gen_helper_dpsqx_s_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSQX_SA_W_PH:
            check_dspr2(ctx);
            gen_helper_dpsqx_sa_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_MULSAQ_S_W_PH:
            check_dsp(ctx);
            gen_helper_mulsaq_s_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPAQ_SA_L_W:
            check_dsp(ctx);
            gen_helper_dpaq_sa_l_w(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_DPSQ_SA_L_W:
            check_dsp(ctx);
            gen_helper_dpsq_sa_l_w(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_MAQ_S_W_PHL:
            check_dsp(ctx);
            gen_helper_maq_s_w_phl(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_MAQ_S_W_PHR:
            check_dsp(ctx);
            gen_helper_maq_s_w_phr(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_MAQ_SA_W_PHL:
            check_dsp(ctx);
            gen_helper_maq_sa_w_phl(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_MAQ_SA_W_PHR:
            check_dsp(ctx);
            gen_helper_maq_sa_w_phr(t0, v1_t, v2_t, cpu_env);
            break;
        case OPC_MULSA_W_PH:
            check_dspr2(ctx);
            gen_helper_mulsa_w_ph(t0, v1_t, v2_t, cpu_env);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_DPAQ_W_QH_DSP:
        {
            int ac = ret & 0x03;
            tcg_gen_movi_i32(t0, ac);

            switch (op2) {
            case OPC_DMADD:
                check_dsp(ctx);
                gen_helper_dmadd(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DMADDU:
                check_dsp(ctx);
                gen_helper_dmaddu(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DMSUB:
                check_dsp(ctx);
                gen_helper_dmsub(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DMSUBU:
                check_dsp(ctx);
                gen_helper_dmsubu(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPA_W_QH:
                check_dspr2(ctx);
                gen_helper_dpa_w_qh(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPAQ_S_W_QH:
                check_dsp(ctx);
                gen_helper_dpaq_s_w_qh(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPAQ_SA_L_PW:
                check_dsp(ctx);
                gen_helper_dpaq_sa_l_pw(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPAU_H_OBL:
                check_dsp(ctx);
                gen_helper_dpau_h_obl(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPAU_H_OBR:
                check_dsp(ctx);
                gen_helper_dpau_h_obr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPS_W_QH:
                check_dspr2(ctx);
                gen_helper_dps_w_qh(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPSQ_S_W_QH:
                check_dsp(ctx);
                gen_helper_dpsq_s_w_qh(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPSQ_SA_L_PW:
                check_dsp(ctx);
                gen_helper_dpsq_sa_l_pw(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPSU_H_OBL:
                check_dsp(ctx);
                gen_helper_dpsu_h_obl(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_DPSU_H_OBR:
                check_dsp(ctx);
                gen_helper_dpsu_h_obr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_S_L_PWL:
                check_dsp(ctx);
                gen_helper_maq_s_l_pwl(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_S_L_PWR:
                check_dsp(ctx);
                gen_helper_maq_s_l_pwr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_S_W_QHLL:
                check_dsp(ctx);
                gen_helper_maq_s_w_qhll(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_SA_W_QHLL:
                check_dsp(ctx);
                gen_helper_maq_sa_w_qhll(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_S_W_QHLR:
                check_dsp(ctx);
                gen_helper_maq_s_w_qhlr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_SA_W_QHLR:
                check_dsp(ctx);
                gen_helper_maq_sa_w_qhlr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_S_W_QHRL:
                check_dsp(ctx);
                gen_helper_maq_s_w_qhrl(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_SA_W_QHRL:
                check_dsp(ctx);
                gen_helper_maq_sa_w_qhrl(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_S_W_QHRR:
                check_dsp(ctx);
                gen_helper_maq_s_w_qhrr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MAQ_SA_W_QHRR:
                check_dsp(ctx);
                gen_helper_maq_sa_w_qhrr(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MULSAQ_S_L_PW:
                check_dsp(ctx);
                gen_helper_mulsaq_s_l_pw(v1_t, v2_t, t0, cpu_env);
                break;
            case OPC_MULSAQ_S_W_QH:
                check_dsp(ctx);
                gen_helper_mulsaq_s_w_qh(v1_t, v2_t, t0, cpu_env);
                break;
            }
        }
        break;
#endif
    case OPC_ADDU_QB_DSP:
        switch (op2) {
        case OPC_MULEU_S_PH_QBL:
            check_dsp(ctx);
            gen_helper_muleu_s_ph_qbl(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULEU_S_PH_QBR:
            check_dsp(ctx);
            gen_helper_muleu_s_ph_qbr(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULQ_RS_PH:
            check_dsp(ctx);
            gen_helper_mulq_rs_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULEQ_S_W_PHL:
            check_dsp(ctx);
            gen_helper_muleq_s_w_phl(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULEQ_S_W_PHR:
            check_dsp(ctx);
            gen_helper_muleq_s_w_phr(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULQ_S_PH:
            check_dspr2(ctx);
            gen_helper_mulq_s_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_ADDU_OB_DSP:
        switch (op2) {
        case OPC_MULEQ_S_PW_QHL:
            check_dsp(ctx);
            gen_helper_muleq_s_pw_qhl(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULEQ_S_PW_QHR:
            check_dsp(ctx);
            gen_helper_muleq_s_pw_qhr(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULEU_S_QH_OBL:
            check_dsp(ctx);
            gen_helper_muleu_s_qh_obl(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULEU_S_QH_OBR:
            check_dsp(ctx);
            gen_helper_muleu_s_qh_obr(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_MULQ_RS_QH:
            check_dsp(ctx);
            gen_helper_mulq_rs_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        }
        break;
#endif
    }

    tcg_temp_free_i32(t0);
    tcg_temp_free(v1_t);
    tcg_temp_free(v2_t);
}

static void gen_mipsdsp_bitinsn(DisasContext *ctx, uint32_t op1, uint32_t op2,
                                int ret, int val)
{
    int16_t imm;
    TCGv t0;
    TCGv val_t;

    if (ret == 0) {
        /* Treat as NOP. */
        return;
    }

    t0 = tcg_temp_new();
    val_t = tcg_temp_new();
    gen_load_gpr(val_t, val);

    switch (op1) {
    case OPC_ABSQ_S_PH_DSP:
        switch (op2) {
        case OPC_BITREV:
            check_dsp(ctx);
            gen_helper_bitrev(cpu_gpr[ret], val_t);
            break;
        case OPC_REPL_QB:
            check_dsp(ctx);
            {
                target_long result;
                imm = (ctx->opcode >> 16) & 0xFF;
                result = (uint32_t)imm << 24 |
                         (uint32_t)imm << 16 |
                         (uint32_t)imm << 8  |
                         (uint32_t)imm;
                result = (int32_t)result;
                tcg_gen_movi_tl(cpu_gpr[ret], result);
            }
            break;
        case OPC_REPLV_QB:
            check_dsp(ctx);
            tcg_gen_ext8u_tl(cpu_gpr[ret], val_t);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 8);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 16);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            tcg_gen_ext32s_tl(cpu_gpr[ret], cpu_gpr[ret]);
            break;
        case OPC_REPL_PH:
            check_dsp(ctx);
            {
                imm = (ctx->opcode >> 16) & 0x03FF;
                imm = (int16_t)(imm << 6) >> 6;
                tcg_gen_movi_tl(cpu_gpr[ret], \
                                (target_long)((int32_t)imm << 16 | \
                                (uint16_t)imm));
            }
            break;
        case OPC_REPLV_PH:
            check_dsp(ctx);
            tcg_gen_ext16u_tl(cpu_gpr[ret], val_t);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 16);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            tcg_gen_ext32s_tl(cpu_gpr[ret], cpu_gpr[ret]);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_ABSQ_S_QH_DSP:
        switch (op2) {
        case OPC_REPL_OB:
            check_dsp(ctx);
            {
                target_long temp;

                imm = (ctx->opcode >> 16) & 0xFF;
                temp = ((uint64_t)imm << 8) | (uint64_t)imm;
                temp = (temp << 16) | temp;
                temp = (temp << 32) | temp;
                tcg_gen_movi_tl(cpu_gpr[ret], temp);
                break;
            }
        case OPC_REPL_PW:
            check_dsp(ctx);
            {
                target_long temp;

                imm = (ctx->opcode >> 16) & 0x03FF;
                imm = (int16_t)(imm << 6) >> 6;
                temp = ((target_long)imm << 32) \
                       | ((target_long)imm & 0xFFFFFFFF);
                tcg_gen_movi_tl(cpu_gpr[ret], temp);
                break;
            }
        case OPC_REPL_QH:
            check_dsp(ctx);
            {
                target_long temp;

                imm = (ctx->opcode >> 16) & 0x03FF;
                imm = (int16_t)(imm << 6) >> 6;

                temp = ((uint64_t)(uint16_t)imm << 48) |
                       ((uint64_t)(uint16_t)imm << 32) |
                       ((uint64_t)(uint16_t)imm << 16) |
                       (uint64_t)(uint16_t)imm;
                tcg_gen_movi_tl(cpu_gpr[ret], temp);
                break;
            }
        case OPC_REPLV_OB:
            check_dsp(ctx);
            tcg_gen_ext8u_tl(cpu_gpr[ret], val_t);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 8);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 16);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 32);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            break;
        case OPC_REPLV_PW:
            check_dsp(ctx);
            tcg_gen_ext32u_i64(cpu_gpr[ret], val_t);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 32);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            break;
        case OPC_REPLV_QH:
            check_dsp(ctx);
            tcg_gen_ext16u_tl(cpu_gpr[ret], val_t);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 16);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            tcg_gen_shli_tl(t0, cpu_gpr[ret], 32);
            tcg_gen_or_tl(cpu_gpr[ret], cpu_gpr[ret], t0);
            break;
        }
        break;
#endif
    }
    tcg_temp_free(t0);
    tcg_temp_free(val_t);
}

static void gen_mipsdsp_add_cmp_pick(DisasContext *ctx,
                                     uint32_t op1, uint32_t op2,
                                     int ret, int v1, int v2, int check_ret)
{
    TCGv t1;
    TCGv v1_t;
    TCGv v2_t;

    if ((ret == 0) && (check_ret == 1)) {
        /* Treat as NOP. */
        return;
    }

    t1 = tcg_temp_new();
    v1_t = tcg_temp_new();
    v2_t = tcg_temp_new();

    gen_load_gpr(v1_t, v1);
    gen_load_gpr(v2_t, v2);

    switch (op1) {
    case OPC_CMPU_EQ_QB_DSP:
        switch (op2) {
        case OPC_CMPU_EQ_QB:
            check_dsp(ctx);
            gen_helper_cmpu_eq_qb(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPU_LT_QB:
            check_dsp(ctx);
            gen_helper_cmpu_lt_qb(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPU_LE_QB:
            check_dsp(ctx);
            gen_helper_cmpu_le_qb(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPGU_EQ_QB:
            check_dsp(ctx);
            gen_helper_cmpgu_eq_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_CMPGU_LT_QB:
            check_dsp(ctx);
            gen_helper_cmpgu_lt_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_CMPGU_LE_QB:
            check_dsp(ctx);
            gen_helper_cmpgu_le_qb(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_CMPGDU_EQ_QB:
            check_dspr2(ctx);
            gen_helper_cmpgu_eq_qb(t1, v1_t, v2_t);
            tcg_gen_mov_tl(cpu_gpr[ret], t1);
            tcg_gen_andi_tl(cpu_dspctrl, cpu_dspctrl, 0xF0FFFFFF);
            tcg_gen_shli_tl(t1, t1, 24);
            tcg_gen_or_tl(cpu_dspctrl, cpu_dspctrl, t1);
            break;
        case OPC_CMPGDU_LT_QB:
            check_dspr2(ctx);
            gen_helper_cmpgu_lt_qb(t1, v1_t, v2_t);
            tcg_gen_mov_tl(cpu_gpr[ret], t1);
            tcg_gen_andi_tl(cpu_dspctrl, cpu_dspctrl, 0xF0FFFFFF);
            tcg_gen_shli_tl(t1, t1, 24);
            tcg_gen_or_tl(cpu_dspctrl, cpu_dspctrl, t1);
            break;
        case OPC_CMPGDU_LE_QB:
            check_dspr2(ctx);
            gen_helper_cmpgu_le_qb(t1, v1_t, v2_t);
            tcg_gen_mov_tl(cpu_gpr[ret], t1);
            tcg_gen_andi_tl(cpu_dspctrl, cpu_dspctrl, 0xF0FFFFFF);
            tcg_gen_shli_tl(t1, t1, 24);
            tcg_gen_or_tl(cpu_dspctrl, cpu_dspctrl, t1);
            break;
        case OPC_CMP_EQ_PH:
            check_dsp(ctx);
            gen_helper_cmp_eq_ph(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_LT_PH:
            check_dsp(ctx);
            gen_helper_cmp_lt_ph(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_LE_PH:
            check_dsp(ctx);
            gen_helper_cmp_le_ph(v1_t, v2_t, cpu_env);
            break;
        case OPC_PICK_QB:
            check_dsp(ctx);
            gen_helper_pick_qb(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_PICK_PH:
            check_dsp(ctx);
            gen_helper_pick_ph(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_PACKRL_PH:
            check_dsp(ctx);
            gen_helper_packrl_ph(cpu_gpr[ret], v1_t, v2_t);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_CMPU_EQ_OB_DSP:
        switch (op2) {
        case OPC_CMP_EQ_PW:
            check_dsp(ctx);
            gen_helper_cmp_eq_pw(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_LT_PW:
            check_dsp(ctx);
            gen_helper_cmp_lt_pw(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_LE_PW:
            check_dsp(ctx);
            gen_helper_cmp_le_pw(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_EQ_QH:
            check_dsp(ctx);
            gen_helper_cmp_eq_qh(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_LT_QH:
            check_dsp(ctx);
            gen_helper_cmp_lt_qh(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMP_LE_QH:
            check_dsp(ctx);
            gen_helper_cmp_le_qh(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPGDU_EQ_OB:
            check_dspr2(ctx);
            gen_helper_cmpgdu_eq_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPGDU_LT_OB:
            check_dspr2(ctx);
            gen_helper_cmpgdu_lt_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPGDU_LE_OB:
            check_dspr2(ctx);
            gen_helper_cmpgdu_le_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPGU_EQ_OB:
            check_dsp(ctx);
            gen_helper_cmpgu_eq_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_CMPGU_LT_OB:
            check_dsp(ctx);
            gen_helper_cmpgu_lt_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_CMPGU_LE_OB:
            check_dsp(ctx);
            gen_helper_cmpgu_le_ob(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_CMPU_EQ_OB:
            check_dsp(ctx);
            gen_helper_cmpu_eq_ob(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPU_LT_OB:
            check_dsp(ctx);
            gen_helper_cmpu_lt_ob(v1_t, v2_t, cpu_env);
            break;
        case OPC_CMPU_LE_OB:
            check_dsp(ctx);
            gen_helper_cmpu_le_ob(v1_t, v2_t, cpu_env);
            break;
        case OPC_PACKRL_PW:
            check_dsp(ctx);
            gen_helper_packrl_pw(cpu_gpr[ret], v1_t, v2_t);
            break;
        case OPC_PICK_OB:
            check_dsp(ctx);
            gen_helper_pick_ob(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_PICK_PW:
            check_dsp(ctx);
            gen_helper_pick_pw(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        case OPC_PICK_QH:
            check_dsp(ctx);
            gen_helper_pick_qh(cpu_gpr[ret], v1_t, v2_t, cpu_env);
            break;
        }
        break;
#endif
    }

    tcg_temp_free(t1);
    tcg_temp_free(v1_t);
    tcg_temp_free(v2_t);
}

static void gen_mipsdsp_append(CPUMIPSState *env, DisasContext *ctx,
                               uint32_t op1, int rt, int rs, int sa)
{
    TCGv t0;

    check_dspr2(ctx);

    if (rt == 0) {
        /* Treat as NOP. */
        return;
    }

    t0 = tcg_temp_new();
    gen_load_gpr(t0, rs);

    switch (op1) {
    case OPC_APPEND_DSP:
        switch (MASK_APPEND(ctx->opcode)) {
        case OPC_APPEND:
            if (sa != 0) {
                tcg_gen_deposit_tl(cpu_gpr[rt], t0, cpu_gpr[rt], sa, 32 - sa);
            }
            tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
            break;
        case OPC_PREPEND:
            if (sa != 0) {
                tcg_gen_ext32u_tl(cpu_gpr[rt], cpu_gpr[rt]);
                tcg_gen_shri_tl(cpu_gpr[rt], cpu_gpr[rt], sa);
                tcg_gen_shli_tl(t0, t0, 32 - sa);
                tcg_gen_or_tl(cpu_gpr[rt], cpu_gpr[rt], t0);
            }
            tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
            break;
        case OPC_BALIGN:
            sa &= 3;
            if (sa != 0 && sa != 2) {
                tcg_gen_shli_tl(cpu_gpr[rt], cpu_gpr[rt], 8 * sa);
                tcg_gen_ext32u_tl(t0, t0);
                tcg_gen_shri_tl(t0, t0, 8 * (4 - sa));
                tcg_gen_or_tl(cpu_gpr[rt], cpu_gpr[rt], t0);
            }
            tcg_gen_ext32s_tl(cpu_gpr[rt], cpu_gpr[rt]);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK APPEND");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_DAPPEND_DSP:
        switch (MASK_DAPPEND(ctx->opcode)) {
        case OPC_DAPPEND:
            if (sa != 0) {
                tcg_gen_deposit_tl(cpu_gpr[rt], t0, cpu_gpr[rt], sa, 64 - sa);
            }
            break;
        case OPC_PREPENDD:
            tcg_gen_shri_tl(cpu_gpr[rt], cpu_gpr[rt], 0x20 | sa);
            tcg_gen_shli_tl(t0, t0, 64 - (0x20 | sa));
            tcg_gen_or_tl(cpu_gpr[rt], t0, t0);
            break;
        case OPC_PREPENDW:
            if (sa != 0) {
                tcg_gen_shri_tl(cpu_gpr[rt], cpu_gpr[rt], sa);
                tcg_gen_shli_tl(t0, t0, 64 - sa);
                tcg_gen_or_tl(cpu_gpr[rt], cpu_gpr[rt], t0);
            }
            break;
        case OPC_DBALIGN:
            sa &= 7;
            if (sa != 0 && sa != 2 && sa != 4) {
                tcg_gen_shli_tl(cpu_gpr[rt], cpu_gpr[rt], 8 * sa);
                tcg_gen_shri_tl(t0, t0, 8 * (8 - sa));
                tcg_gen_or_tl(cpu_gpr[rt], cpu_gpr[rt], t0);
            }
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK DAPPEND");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    }
    tcg_temp_free(t0);
}

static void gen_mipsdsp_accinsn(DisasContext *ctx, uint32_t op1, uint32_t op2,
                                int ret, int v1, int v2, int check_ret)

{
    TCGv t0;
    TCGv t1;
    TCGv v1_t;
    TCGv v2_t;
    int16_t imm;

    if ((ret == 0) && (check_ret == 1)) {
        /* Treat as NOP. */
        return;
    }

    t0 = tcg_temp_new();
    t1 = tcg_temp_new();
    v1_t = tcg_temp_new();
    v2_t = tcg_temp_new();

    gen_load_gpr(v1_t, v1);
    gen_load_gpr(v2_t, v2);

    switch (op1) {
    case OPC_EXTR_W_DSP:
        check_dsp(ctx);
        switch (op2) {
        case OPC_EXTR_W:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extr_w(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_EXTR_R_W:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extr_r_w(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_EXTR_RS_W:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extr_rs_w(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_EXTR_S_H:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extr_s_h(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_EXTRV_S_H:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_extr_s_h(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_EXTRV_W:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_extr_w(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_EXTRV_R_W:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_extr_r_w(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_EXTRV_RS_W:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_extr_rs_w(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_EXTP:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extp(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_EXTPV:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_extp(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_EXTPDP:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_extpdp(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_EXTPDPV:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_extpdp(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_SHILO:
            imm = (ctx->opcode >> 20) & 0x3F;
            tcg_gen_movi_tl(t0, ret);
            tcg_gen_movi_tl(t1, imm);
            gen_helper_shilo(t0, t1, cpu_env);
            break;
        case OPC_SHILOV:
            tcg_gen_movi_tl(t0, ret);
            gen_helper_shilo(t0, v1_t, cpu_env);
            break;
        case OPC_MTHLIP:
            tcg_gen_movi_tl(t0, ret);
            gen_helper_mthlip(t0, v1_t, cpu_env);
            break;
        case OPC_WRDSP:
            imm = (ctx->opcode >> 11) & 0x3FF;
            tcg_gen_movi_tl(t0, imm);
            gen_helper_wrdsp(v1_t, t0, cpu_env);
            break;
        case OPC_RDDSP:
            imm = (ctx->opcode >> 16) & 0x03FF;
            tcg_gen_movi_tl(t0, imm);
            gen_helper_rddsp(cpu_gpr[ret], t0, cpu_env);
            break;
        }
        break;
#ifdef TARGET_MIPS64
    case OPC_DEXTR_W_DSP:
        check_dsp(ctx);
        switch (op2) {
        case OPC_DMTHLIP:
            tcg_gen_movi_tl(t0, ret);
            gen_helper_dmthlip(v1_t, t0, cpu_env);
            break;
        case OPC_DSHILO:
            {
                int shift = (ctx->opcode >> 19) & 0x7F;
                int ac = (ctx->opcode >> 11) & 0x03;
                tcg_gen_movi_tl(t0, shift);
                tcg_gen_movi_tl(t1, ac);
                gen_helper_dshilo(t0, t1, cpu_env);
                break;
            }
        case OPC_DSHILOV:
            {
                int ac = (ctx->opcode >> 11) & 0x03;
                tcg_gen_movi_tl(t0, ac);
                gen_helper_dshilo(v1_t, t0, cpu_env);
                break;
            }
        case OPC_DEXTP:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);

            gen_helper_dextp(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTPV:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextp(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTPDP:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextpdp(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTPDPV:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextpdp(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTR_L:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_l(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTR_R_L:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_r_l(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTR_RS_L:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_rs_l(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTR_W:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_w(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTR_R_W:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_r_w(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTR_RS_W:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_rs_w(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTR_S_H:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_s_h(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTRV_S_H:
            tcg_gen_movi_tl(t0, v2);
            tcg_gen_movi_tl(t1, v1);
            gen_helper_dextr_s_h(cpu_gpr[ret], t0, t1, cpu_env);
            break;
        case OPC_DEXTRV_L:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextr_l(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTRV_R_L:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextr_r_l(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTRV_RS_L:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextr_rs_l(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTRV_W:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextr_w(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTRV_R_W:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextr_r_w(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        case OPC_DEXTRV_RS_W:
            tcg_gen_movi_tl(t0, v2);
            gen_helper_dextr_rs_w(cpu_gpr[ret], t0, v1_t, cpu_env);
            break;
        }
        break;
#endif
    }

    tcg_temp_free(t0);
    tcg_temp_free(t1);
    tcg_temp_free(v1_t);
    tcg_temp_free(v2_t);
}

/* End MIPSDSP functions. */

static void decode_opc_special_r6(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd, sa;
    uint32_t op1, op2;

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;

    op1 = MASK_SPECIAL(ctx->opcode);
    switch (op1) {
    case OPC_LSA:
        gen_lsa(ctx, op1, rd, rs, rt, extract32(ctx->opcode, 6, 2));
        break;
    case OPC_MULT ... OPC_DIVU:
        op2 = MASK_R6_MULDIV(ctx->opcode);
        switch (op2) {
        case R6_OPC_MUL:
        case R6_OPC_MUH:
        case R6_OPC_MULU:
        case R6_OPC_MUHU:
        case R6_OPC_DIV:
        case R6_OPC_MOD:
        case R6_OPC_DIVU:
        case R6_OPC_MODU:
            gen_r6_muldiv(ctx, op2, rd, rs, rt);
            break;
        default:
            MIPS_INVAL("special_r6 muldiv");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SELEQZ:
    case OPC_SELNEZ:
        gen_cond_move(ctx, op1, rd, rs, rt);
        break;
    case R6_OPC_CLO:
    case R6_OPC_CLZ:
        if (rt == 0 && sa == 1) {
            /* Major opcode and function field is shared with preR6 MFHI/MTHI.
               We need additionally to check other fields */
            gen_cl(ctx, op1, rd, rs);
        } else {
            generate_exception_end(ctx, EXCP_RI);
        }
        break;
    case R6_OPC_SDBBP:
        if (is_uhi(extract32(ctx->opcode, 6, 20))) {
            gen_helper_do_semihosting(cpu_env);
        } else {
            if (ctx->hflags & MIPS_HFLAG_SBRI) {
                generate_exception_end(ctx, EXCP_RI);
            } else {
                generate_exception_end(ctx, EXCP_DBp);
            }
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DLSA:
        check_mips_64(ctx);
        gen_lsa(ctx, op1, rd, rs, rt, extract32(ctx->opcode, 6, 2));
        break;
    case R6_OPC_DCLO:
    case R6_OPC_DCLZ:
        if (rt == 0 && sa == 1) {
            /* Major opcode and function field is shared with preR6 MFHI/MTHI.
               We need additionally to check other fields */
            check_mips_64(ctx);
            gen_cl(ctx, op1, rd, rs);
        } else {
            generate_exception_end(ctx, EXCP_RI);
        }
        break;
    case OPC_DMULT ... OPC_DDIVU:
        op2 = MASK_R6_MULDIV(ctx->opcode);
        switch (op2) {
        case R6_OPC_DMUL:
        case R6_OPC_DMUH:
        case R6_OPC_DMULU:
        case R6_OPC_DMUHU:
        case R6_OPC_DDIV:
        case R6_OPC_DMOD:
        case R6_OPC_DDIVU:
        case R6_OPC_DMODU:
            check_mips_64(ctx);
            gen_r6_muldiv(ctx, op2, rd, rs, rt);
            break;
        default:
            MIPS_INVAL("special_r6 muldiv");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
#endif
    default:            /* Invalid */
        MIPS_INVAL("special_r6");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void decode_opc_special_legacy(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd, sa;
    uint32_t op1;

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;

    op1 = MASK_SPECIAL(ctx->opcode);
    switch (op1) {
    case OPC_MOVN:         /* Conditional move */
    case OPC_MOVZ:
        check_insn(ctx, ISA_MIPS4 | ISA_MIPS32 |
                   INSN_LOONGSON2E | INSN_LOONGSON2F);
        gen_cond_move(ctx, op1, rd, rs, rt);
        break;
    case OPC_MFHI:          /* Move from HI/LO */
    case OPC_MFLO:
        gen_HILO(ctx, op1, rs & 3, rd);
        break;
    case OPC_MTHI:
    case OPC_MTLO:          /* Move to HI/LO */
        gen_HILO(ctx, op1, rd & 3, rs);
        break;
    case OPC_MOVCI:
        check_insn(ctx, ISA_MIPS4 | ISA_MIPS32);
        if (env->CP0_Config1 & (1 << CP0C1_FP)) {
            check_cp1_enabled(ctx);
            gen_movci(ctx, rd, rs, (ctx->opcode >> 18) & 0x7,
                      (ctx->opcode >> 16) & 1);
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;
    case OPC_MULT:
    case OPC_MULTU:
        if (sa) {
            check_insn(ctx, INSN_VR54XX);
            op1 = MASK_MUL_VR54XX(ctx->opcode);
            gen_mul_vr54xx(ctx, op1, rd, rs, rt);
        } else {
            gen_muldiv(ctx, op1, rd & 3, rs, rt);
        }
        break;
    case OPC_DIV:
    case OPC_DIVU:
        gen_muldiv(ctx, op1, 0, rs, rt);
        break;
#if defined(TARGET_MIPS64)
    case OPC_DMULT ... OPC_DDIVU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_muldiv(ctx, op1, 0, rs, rt);
        break;
#endif
    case OPC_JR:
        gen_compute_branch(ctx, op1, 4, rs, rd, sa, 4);
        break;
    case OPC_SPIM:
#ifdef MIPS_STRICT_STANDARD
        MIPS_INVAL("SPIM");
        generate_exception_end(ctx, EXCP_RI);
#else
        /* Implemented as RI exception for now. */
        MIPS_INVAL("spim (unofficial)");
        generate_exception_end(ctx, EXCP_RI);
#endif
        break;
    default:            /* Invalid */
        MIPS_INVAL("special_legacy");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void decode_opc_special(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd, sa;
    uint32_t op1;

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;

    op1 = MASK_SPECIAL(ctx->opcode);
    switch (op1) {
    case OPC_SLL:          /* Shift with immediate */
        if (sa == 5 && rd == 0 &&
            rs == 0 && rt == 0) { /* PAUSE */
            if ((ctx->insn_flags & ISA_MIPS32R6) &&
                (ctx->hflags & MIPS_HFLAG_BMASK)) {
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
        }
        /* Fallthrough */
    case OPC_SRA:
        gen_shift_imm(ctx, op1, rd, rt, sa);
        break;
    case OPC_SRL:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 1:
            /* rotr is decoded as srl on non-R2 CPUs */
            if (ctx->insn_flags & ISA_MIPS32R2) {
                op1 = OPC_ROTR;
            }
            /* Fallthrough */
        case 0:
            gen_shift_imm(ctx, op1, rd, rt, sa);
            break;
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_ADD ... OPC_SUBU:
        gen_arith(ctx, op1, rd, rs, rt);
        break;
    case OPC_SLLV:         /* Shifts */
    case OPC_SRAV:
        gen_shift(ctx, op1, rd, rs, rt);
        break;
    case OPC_SRLV:
        switch ((ctx->opcode >> 6) & 0x1f) {
        case 1:
            /* rotrv is decoded as srlv on non-R2 CPUs */
            if (ctx->insn_flags & ISA_MIPS32R2) {
                op1 = OPC_ROTRV;
            }
            /* Fallthrough */
        case 0:
            gen_shift(ctx, op1, rd, rs, rt);
            break;
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SLT:          /* Set on less than */
    case OPC_SLTU:
        gen_slt(ctx, op1, rd, rs, rt);
        break;
    case OPC_AND:          /* Logic*/
    case OPC_OR:
    case OPC_NOR:
    case OPC_XOR:
        gen_logic(ctx, op1, rd, rs, rt);
        break;
    case OPC_JALR:
        gen_compute_branch(ctx, op1, 4, rs, rd, sa, 4);
        break;
    case OPC_TGE ... OPC_TEQ: /* Traps */
    case OPC_TNE:
        check_insn(ctx, ISA_MIPS2);
        gen_trap(ctx, op1, rs, rt, -1);
        break;
    case OPC_LSA: /* OPC_PMON */
        if ((ctx->insn_flags & ISA_MIPS32R6) ||
            (env->CP0_Config3 & (1 << CP0C3_MSAP))) {
            decode_opc_special_r6(env, ctx);
        } else {
            /* Pmon entry point, also R4010 selsl */
#ifdef MIPS_STRICT_STANDARD
            MIPS_INVAL("PMON / selsl");
            generate_exception_end(ctx, EXCP_RI);
#else
            gen_helper_0e0i(pmon, sa);
#endif
        }
        break;
    case OPC_SYSCALL:
        generate_exception_end(ctx, EXCP_SYSCALL);
        break;
    case OPC_BREAK:
        generate_exception_end(ctx, EXCP_BREAK);
        break;
    case OPC_SYNC:
        check_insn(ctx, ISA_MIPS2);
        gen_sync(extract32(ctx->opcode, 6, 5));
        break;

#if defined(TARGET_MIPS64)
        /* MIPS64 specific opcodes */
    case OPC_DSLL:
    case OPC_DSRA:
    case OPC_DSLL32:
    case OPC_DSRA32:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_shift_imm(ctx, op1, rd, rt, sa);
        break;
    case OPC_DSRL:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 1:
            /* drotr is decoded as dsrl on non-R2 CPUs */
            if (ctx->insn_flags & ISA_MIPS32R2) {
                op1 = OPC_DROTR;
            }
            /* Fallthrough */
        case 0:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, op1, rd, rt, sa);
            break;
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DSRL32:
        switch ((ctx->opcode >> 21) & 0x1f) {
        case 1:
            /* drotr32 is decoded as dsrl32 on non-R2 CPUs */
            if (ctx->insn_flags & ISA_MIPS32R2) {
                op1 = OPC_DROTR32;
            }
            /* Fallthrough */
        case 0:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift_imm(ctx, op1, rd, rt, sa);
            break;
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DADD ... OPC_DSUBU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_arith(ctx, op1, rd, rs, rt);
        break;
    case OPC_DSLLV:
    case OPC_DSRAV:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_shift(ctx, op1, rd, rs, rt);
        break;
    case OPC_DSRLV:
        switch ((ctx->opcode >> 6) & 0x1f) {
        case 1:
            /* drotrv is decoded as dsrlv on non-R2 CPUs */
            if (ctx->insn_flags & ISA_MIPS32R2) {
                op1 = OPC_DROTRV;
            }
            /* Fallthrough */
        case 0:
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_shift(ctx, op1, rd, rs, rt);
            break;
        default:
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DLSA:
        if ((ctx->insn_flags & ISA_MIPS32R6) ||
            (env->CP0_Config3 & (1 << CP0C3_MSAP))) {
            decode_opc_special_r6(env, ctx);
        }
        break;
#endif
    default:
        if (ctx->insn_flags & ISA_MIPS32R6) {
            decode_opc_special_r6(env, ctx);
        } else {
            decode_opc_special_legacy(env, ctx);
        }
    }
}

static void decode_opc_special2_legacy(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd;
    uint32_t op1;

    check_insn_opc_removed(ctx, ISA_MIPS32R6);

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;

    op1 = MASK_SPECIAL2(ctx->opcode);
    switch (op1) {
    case OPC_MADD ... OPC_MADDU: /* Multiply and add/sub */
    case OPC_MSUB ... OPC_MSUBU:
        check_insn(ctx, ISA_MIPS32);
        gen_muldiv(ctx, op1, rd & 3, rs, rt);
        break;
    case OPC_MUL:
        gen_arith(ctx, op1, rd, rs, rt);
        break;
    case OPC_DIV_G_2F:
    case OPC_DIVU_G_2F:
    case OPC_MULT_G_2F:
    case OPC_MULTU_G_2F:
    case OPC_MOD_G_2F:
    case OPC_MODU_G_2F:
        check_insn(ctx, INSN_LOONGSON2F);
        gen_loongson_integer(ctx, op1, rd, rs, rt);
        break;
    case OPC_CLO:
    case OPC_CLZ:
        check_insn(ctx, ISA_MIPS32);
        gen_cl(ctx, op1, rd, rs);
        break;
    case OPC_SDBBP:
        if (is_uhi(extract32(ctx->opcode, 6, 20))) {
            gen_helper_do_semihosting(cpu_env);
        } else {
            /* XXX: not clear which exception should be raised
             *      when in debug mode...
             */
            check_insn(ctx, ISA_MIPS32);
            generate_exception_end(ctx, EXCP_DBp);
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DCLO:
    case OPC_DCLZ:
        check_insn(ctx, ISA_MIPS64);
        check_mips_64(ctx);
        gen_cl(ctx, op1, rd, rs);
        break;
    case OPC_DMULT_G_2F:
    case OPC_DMULTU_G_2F:
    case OPC_DDIV_G_2F:
    case OPC_DDIVU_G_2F:
    case OPC_DMOD_G_2F:
    case OPC_DMODU_G_2F:
        check_insn(ctx, INSN_LOONGSON2F);
        gen_loongson_integer(ctx, op1, rd, rs, rt);
        break;
#endif
    default:            /* Invalid */
        MIPS_INVAL("special2_legacy");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void decode_opc_special3_r6(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd, sa;
    uint32_t op1, op2;
    int16_t imm;

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;
    imm = (int16_t)ctx->opcode >> 7;

    op1 = MASK_SPECIAL3(ctx->opcode);
    switch (op1) {
    case R6_OPC_PREF:
        if (rt >= 24) {
            /* hint codes 24-31 are reserved and signal RI */
            generate_exception_end(ctx, EXCP_RI);
        }
        /* Treat as NOP. */
        break;
    case R6_OPC_CACHE:
        check_cp0_enabled(ctx);
        if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
            gen_cache_operation(ctx, rt, rs, imm);
        }
        break;
    case R6_OPC_SC:
        gen_st_cond(ctx, op1, rt, rs, imm);
        break;
    case R6_OPC_LL:
        gen_ld(ctx, op1, rt, rs, imm);
        break;
    case OPC_BSHFL:
        {
            if (rd == 0) {
                /* Treat as NOP. */
                break;
            }
            op2 = MASK_BSHFL(ctx->opcode);
            switch (op2) {
            case OPC_ALIGN ... OPC_ALIGN_END:
                gen_align(ctx, OPC_ALIGN, rd, rs, rt, sa & 3);
                break;
            case OPC_BITSWAP:
                gen_bitswap(ctx, op2, rd, rt);
                break;
            }
        }
        break;
#if defined(TARGET_MIPS64)
    case R6_OPC_SCD:
        gen_st_cond(ctx, op1, rt, rs, imm);
        break;
    case R6_OPC_LLD:
        gen_ld(ctx, op1, rt, rs, imm);
        break;
    case OPC_DBSHFL:
        check_mips_64(ctx);
        {
            if (rd == 0) {
                /* Treat as NOP. */
                break;
            }
            op2 = MASK_DBSHFL(ctx->opcode);
            switch (op2) {
            case OPC_DALIGN ... OPC_DALIGN_END:
                gen_align(ctx, OPC_DALIGN, rd, rs, rt, sa & 7);
                break;
            case OPC_DBITSWAP:
                gen_bitswap(ctx, op2, rd, rt);
                break;
            }

        }
        break;
#endif
    default:            /* Invalid */
        MIPS_INVAL("special3_r6");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void decode_opc_special3_legacy(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd;
    uint32_t op1, op2;

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;

    op1 = MASK_SPECIAL3(ctx->opcode);
    switch (op1) {
    case OPC_DIV_G_2E ... OPC_DIVU_G_2E:
    case OPC_MOD_G_2E ... OPC_MODU_G_2E:
    case OPC_MULT_G_2E ... OPC_MULTU_G_2E:
        /* OPC_MULT_G_2E, OPC_ADDUH_QB_DSP, OPC_MUL_PH_DSP have
         * the same mask and op1. */
        if ((ctx->insn_flags & ASE_DSPR2) && (op1 == OPC_MULT_G_2E)) {
            op2 = MASK_ADDUH_QB(ctx->opcode);
            switch (op2) {
            case OPC_ADDUH_QB:
            case OPC_ADDUH_R_QB:
            case OPC_ADDQH_PH:
            case OPC_ADDQH_R_PH:
            case OPC_ADDQH_W:
            case OPC_ADDQH_R_W:
            case OPC_SUBUH_QB:
            case OPC_SUBUH_R_QB:
            case OPC_SUBQH_PH:
            case OPC_SUBQH_R_PH:
            case OPC_SUBQH_W:
            case OPC_SUBQH_R_W:
                gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
                break;
            case OPC_MUL_PH:
            case OPC_MUL_S_PH:
            case OPC_MULQ_S_W:
            case OPC_MULQ_RS_W:
                gen_mipsdsp_multiply(ctx, op1, op2, rd, rs, rt, 1);
                break;
            default:
                MIPS_INVAL("MASK ADDUH.QB");
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
        } else if (ctx->insn_flags & INSN_LOONGSON2E) {
            gen_loongson_integer(ctx, op1, rd, rs, rt);
        } else {
            generate_exception_end(ctx, EXCP_RI);
        }
        break;
    case OPC_LX_DSP:
        op2 = MASK_LX(ctx->opcode);
        switch (op2) {
#if defined(TARGET_MIPS64)
        case OPC_LDX:
#endif
        case OPC_LBUX:
        case OPC_LHX:
        case OPC_LWX:
            gen_mipsdsp_ld(ctx, op2, rd, rs, rt);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK LX");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_ABSQ_S_PH_DSP:
        op2 = MASK_ABSQ_S_PH(ctx->opcode);
        switch (op2) {
        case OPC_ABSQ_S_QB:
        case OPC_ABSQ_S_PH:
        case OPC_ABSQ_S_W:
        case OPC_PRECEQ_W_PHL:
        case OPC_PRECEQ_W_PHR:
        case OPC_PRECEQU_PH_QBL:
        case OPC_PRECEQU_PH_QBR:
        case OPC_PRECEQU_PH_QBLA:
        case OPC_PRECEQU_PH_QBRA:
        case OPC_PRECEU_PH_QBL:
        case OPC_PRECEU_PH_QBR:
        case OPC_PRECEU_PH_QBLA:
        case OPC_PRECEU_PH_QBRA:
            gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
            break;
        case OPC_BITREV:
        case OPC_REPL_QB:
        case OPC_REPLV_QB:
        case OPC_REPL_PH:
        case OPC_REPLV_PH:
            gen_mipsdsp_bitinsn(ctx, op1, op2, rd, rt);
            break;
        default:
            MIPS_INVAL("MASK ABSQ_S.PH");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_ADDU_QB_DSP:
        op2 = MASK_ADDU_QB(ctx->opcode);
        switch (op2) {
        case OPC_ADDQ_PH:
        case OPC_ADDQ_S_PH:
        case OPC_ADDQ_S_W:
        case OPC_ADDU_QB:
        case OPC_ADDU_S_QB:
        case OPC_ADDU_PH:
        case OPC_ADDU_S_PH:
        case OPC_SUBQ_PH:
        case OPC_SUBQ_S_PH:
        case OPC_SUBQ_S_W:
        case OPC_SUBU_QB:
        case OPC_SUBU_S_QB:
        case OPC_SUBU_PH:
        case OPC_SUBU_S_PH:
        case OPC_ADDSC:
        case OPC_ADDWC:
        case OPC_MODSUB:
        case OPC_RADDU_W_QB:
            gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
            break;
        case OPC_MULEU_S_PH_QBL:
        case OPC_MULEU_S_PH_QBR:
        case OPC_MULQ_RS_PH:
        case OPC_MULEQ_S_W_PHL:
        case OPC_MULEQ_S_W_PHR:
        case OPC_MULQ_S_PH:
            gen_mipsdsp_multiply(ctx, op1, op2, rd, rs, rt, 1);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK ADDU.QB");
            generate_exception_end(ctx, EXCP_RI);
            break;

        }
        break;
    case OPC_CMPU_EQ_QB_DSP:
        op2 = MASK_CMPU_EQ_QB(ctx->opcode);
        switch (op2) {
        case OPC_PRECR_SRA_PH_W:
        case OPC_PRECR_SRA_R_PH_W:
            gen_mipsdsp_arith(ctx, op1, op2, rt, rs, rd);
            break;
        case OPC_PRECR_QB_PH:
        case OPC_PRECRQ_QB_PH:
        case OPC_PRECRQ_PH_W:
        case OPC_PRECRQ_RS_PH_W:
        case OPC_PRECRQU_S_QB_PH:
            gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
            break;
        case OPC_CMPU_EQ_QB:
        case OPC_CMPU_LT_QB:
        case OPC_CMPU_LE_QB:
        case OPC_CMP_EQ_PH:
        case OPC_CMP_LT_PH:
        case OPC_CMP_LE_PH:
            gen_mipsdsp_add_cmp_pick(ctx, op1, op2, rd, rs, rt, 0);
            break;
        case OPC_CMPGU_EQ_QB:
        case OPC_CMPGU_LT_QB:
        case OPC_CMPGU_LE_QB:
        case OPC_CMPGDU_EQ_QB:
        case OPC_CMPGDU_LT_QB:
        case OPC_CMPGDU_LE_QB:
        case OPC_PICK_QB:
        case OPC_PICK_PH:
        case OPC_PACKRL_PH:
            gen_mipsdsp_add_cmp_pick(ctx, op1, op2, rd, rs, rt, 1);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK CMPU.EQ.QB");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SHLL_QB_DSP:
        gen_mipsdsp_shift(ctx, op1, rd, rs, rt);
        break;
    case OPC_DPA_W_PH_DSP:
        op2 = MASK_DPA_W_PH(ctx->opcode);
        switch (op2) {
        case OPC_DPAU_H_QBL:
        case OPC_DPAU_H_QBR:
        case OPC_DPSU_H_QBL:
        case OPC_DPSU_H_QBR:
        case OPC_DPA_W_PH:
        case OPC_DPAX_W_PH:
        case OPC_DPAQ_S_W_PH:
        case OPC_DPAQX_S_W_PH:
        case OPC_DPAQX_SA_W_PH:
        case OPC_DPS_W_PH:
        case OPC_DPSX_W_PH:
        case OPC_DPSQ_S_W_PH:
        case OPC_DPSQX_S_W_PH:
        case OPC_DPSQX_SA_W_PH:
        case OPC_MULSAQ_S_W_PH:
        case OPC_DPAQ_SA_L_W:
        case OPC_DPSQ_SA_L_W:
        case OPC_MAQ_S_W_PHL:
        case OPC_MAQ_S_W_PHR:
        case OPC_MAQ_SA_W_PHL:
        case OPC_MAQ_SA_W_PHR:
        case OPC_MULSA_W_PH:
            gen_mipsdsp_multiply(ctx, op1, op2, rd, rs, rt, 0);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK DPAW.PH");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_INSV_DSP:
        op2 = MASK_INSV(ctx->opcode);
        switch (op2) {
        case OPC_INSV:
            check_dsp(ctx);
            {
                TCGv t0, t1;

                if (rt == 0) {
                    break;
                }

                t0 = tcg_temp_new();
                t1 = tcg_temp_new();

                gen_load_gpr(t0, rt);
                gen_load_gpr(t1, rs);

                gen_helper_insv(cpu_gpr[rt], cpu_env, t1, t0);

                tcg_temp_free(t0);
                tcg_temp_free(t1);
                break;
            }
        default:            /* Invalid */
            MIPS_INVAL("MASK INSV");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_APPEND_DSP:
        gen_mipsdsp_append(env, ctx, op1, rt, rs, rd);
        break;
    case OPC_EXTR_W_DSP:
        op2 = MASK_EXTR_W(ctx->opcode);
        switch (op2) {
        case OPC_EXTR_W:
        case OPC_EXTR_R_W:
        case OPC_EXTR_RS_W:
        case OPC_EXTR_S_H:
        case OPC_EXTRV_S_H:
        case OPC_EXTRV_W:
        case OPC_EXTRV_R_W:
        case OPC_EXTRV_RS_W:
        case OPC_EXTP:
        case OPC_EXTPV:
        case OPC_EXTPDP:
        case OPC_EXTPDPV:
            gen_mipsdsp_accinsn(ctx, op1, op2, rt, rs, rd, 1);
            break;
        case OPC_RDDSP:
            gen_mipsdsp_accinsn(ctx, op1, op2, rd, rs, rt, 1);
            break;
        case OPC_SHILO:
        case OPC_SHILOV:
        case OPC_MTHLIP:
        case OPC_WRDSP:
            gen_mipsdsp_accinsn(ctx, op1, op2, rd, rs, rt, 0);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK EXTR.W");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DDIV_G_2E ... OPC_DDIVU_G_2E:
    case OPC_DMULT_G_2E ... OPC_DMULTU_G_2E:
    case OPC_DMOD_G_2E ... OPC_DMODU_G_2E:
        check_insn(ctx, INSN_LOONGSON2E);
        gen_loongson_integer(ctx, op1, rd, rs, rt);
        break;
    case OPC_ABSQ_S_QH_DSP:
        op2 = MASK_ABSQ_S_QH(ctx->opcode);
        switch (op2) {
        case OPC_PRECEQ_L_PWL:
        case OPC_PRECEQ_L_PWR:
        case OPC_PRECEQ_PW_QHL:
        case OPC_PRECEQ_PW_QHR:
        case OPC_PRECEQ_PW_QHLA:
        case OPC_PRECEQ_PW_QHRA:
        case OPC_PRECEQU_QH_OBL:
        case OPC_PRECEQU_QH_OBR:
        case OPC_PRECEQU_QH_OBLA:
        case OPC_PRECEQU_QH_OBRA:
        case OPC_PRECEU_QH_OBL:
        case OPC_PRECEU_QH_OBR:
        case OPC_PRECEU_QH_OBLA:
        case OPC_PRECEU_QH_OBRA:
        case OPC_ABSQ_S_OB:
        case OPC_ABSQ_S_PW:
        case OPC_ABSQ_S_QH:
            gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
            break;
        case OPC_REPL_OB:
        case OPC_REPL_PW:
        case OPC_REPL_QH:
        case OPC_REPLV_OB:
        case OPC_REPLV_PW:
        case OPC_REPLV_QH:
            gen_mipsdsp_bitinsn(ctx, op1, op2, rd, rt);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK ABSQ_S.QH");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_ADDU_OB_DSP:
        op2 = MASK_ADDU_OB(ctx->opcode);
        switch (op2) {
        case OPC_RADDU_L_OB:
        case OPC_SUBQ_PW:
        case OPC_SUBQ_S_PW:
        case OPC_SUBQ_QH:
        case OPC_SUBQ_S_QH:
        case OPC_SUBU_OB:
        case OPC_SUBU_S_OB:
        case OPC_SUBU_QH:
        case OPC_SUBU_S_QH:
        case OPC_SUBUH_OB:
        case OPC_SUBUH_R_OB:
        case OPC_ADDQ_PW:
        case OPC_ADDQ_S_PW:
        case OPC_ADDQ_QH:
        case OPC_ADDQ_S_QH:
        case OPC_ADDU_OB:
        case OPC_ADDU_S_OB:
        case OPC_ADDU_QH:
        case OPC_ADDU_S_QH:
        case OPC_ADDUH_OB:
        case OPC_ADDUH_R_OB:
            gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
            break;
        case OPC_MULEQ_S_PW_QHL:
        case OPC_MULEQ_S_PW_QHR:
        case OPC_MULEU_S_QH_OBL:
        case OPC_MULEU_S_QH_OBR:
        case OPC_MULQ_RS_QH:
            gen_mipsdsp_multiply(ctx, op1, op2, rd, rs, rt, 1);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK ADDU.OB");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_CMPU_EQ_OB_DSP:
        op2 = MASK_CMPU_EQ_OB(ctx->opcode);
        switch (op2) {
        case OPC_PRECR_SRA_QH_PW:
        case OPC_PRECR_SRA_R_QH_PW:
            /* Return value is rt. */
            gen_mipsdsp_arith(ctx, op1, op2, rt, rs, rd);
            break;
        case OPC_PRECR_OB_QH:
        case OPC_PRECRQ_OB_QH:
        case OPC_PRECRQ_PW_L:
        case OPC_PRECRQ_QH_PW:
        case OPC_PRECRQ_RS_QH_PW:
        case OPC_PRECRQU_S_OB_QH:
            gen_mipsdsp_arith(ctx, op1, op2, rd, rs, rt);
            break;
        case OPC_CMPU_EQ_OB:
        case OPC_CMPU_LT_OB:
        case OPC_CMPU_LE_OB:
        case OPC_CMP_EQ_QH:
        case OPC_CMP_LT_QH:
        case OPC_CMP_LE_QH:
        case OPC_CMP_EQ_PW:
        case OPC_CMP_LT_PW:
        case OPC_CMP_LE_PW:
            gen_mipsdsp_add_cmp_pick(ctx, op1, op2, rd, rs, rt, 0);
            break;
        case OPC_CMPGDU_EQ_OB:
        case OPC_CMPGDU_LT_OB:
        case OPC_CMPGDU_LE_OB:
        case OPC_CMPGU_EQ_OB:
        case OPC_CMPGU_LT_OB:
        case OPC_CMPGU_LE_OB:
        case OPC_PACKRL_PW:
        case OPC_PICK_OB:
        case OPC_PICK_PW:
        case OPC_PICK_QH:
            gen_mipsdsp_add_cmp_pick(ctx, op1, op2, rd, rs, rt, 1);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK CMPU_EQ.OB");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DAPPEND_DSP:
        gen_mipsdsp_append(env, ctx, op1, rt, rs, rd);
        break;
    case OPC_DEXTR_W_DSP:
        op2 = MASK_DEXTR_W(ctx->opcode);
        switch (op2) {
        case OPC_DEXTP:
        case OPC_DEXTPDP:
        case OPC_DEXTPDPV:
        case OPC_DEXTPV:
        case OPC_DEXTR_L:
        case OPC_DEXTR_R_L:
        case OPC_DEXTR_RS_L:
        case OPC_DEXTR_W:
        case OPC_DEXTR_R_W:
        case OPC_DEXTR_RS_W:
        case OPC_DEXTR_S_H:
        case OPC_DEXTRV_L:
        case OPC_DEXTRV_R_L:
        case OPC_DEXTRV_RS_L:
        case OPC_DEXTRV_S_H:
        case OPC_DEXTRV_W:
        case OPC_DEXTRV_R_W:
        case OPC_DEXTRV_RS_W:
            gen_mipsdsp_accinsn(ctx, op1, op2, rt, rs, rd, 1);
            break;
        case OPC_DMTHLIP:
        case OPC_DSHILO:
        case OPC_DSHILOV:
            gen_mipsdsp_accinsn(ctx, op1, op2, rd, rs, rt, 0);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK EXTR.W");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DPAQ_W_QH_DSP:
        op2 = MASK_DPAQ_W_QH(ctx->opcode);
        switch (op2) {
        case OPC_DPAU_H_OBL:
        case OPC_DPAU_H_OBR:
        case OPC_DPSU_H_OBL:
        case OPC_DPSU_H_OBR:
        case OPC_DPA_W_QH:
        case OPC_DPAQ_S_W_QH:
        case OPC_DPS_W_QH:
        case OPC_DPSQ_S_W_QH:
        case OPC_MULSAQ_S_W_QH:
        case OPC_DPAQ_SA_L_PW:
        case OPC_DPSQ_SA_L_PW:
        case OPC_MULSAQ_S_L_PW:
            gen_mipsdsp_multiply(ctx, op1, op2, rd, rs, rt, 0);
            break;
        case OPC_MAQ_S_W_QHLL:
        case OPC_MAQ_S_W_QHLR:
        case OPC_MAQ_S_W_QHRL:
        case OPC_MAQ_S_W_QHRR:
        case OPC_MAQ_SA_W_QHLL:
        case OPC_MAQ_SA_W_QHLR:
        case OPC_MAQ_SA_W_QHRL:
        case OPC_MAQ_SA_W_QHRR:
        case OPC_MAQ_S_L_PWL:
        case OPC_MAQ_S_L_PWR:
        case OPC_DMADD:
        case OPC_DMADDU:
        case OPC_DMSUB:
        case OPC_DMSUBU:
            gen_mipsdsp_multiply(ctx, op1, op2, rd, rs, rt, 0);
            break;
        default:            /* Invalid */
            MIPS_INVAL("MASK DPAQ.W.QH");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_DINSV_DSP:
        op2 = MASK_INSV(ctx->opcode);
        switch (op2) {
        case OPC_DINSV:
        {
            TCGv t0, t1;

            if (rt == 0) {
                break;
            }
            check_dsp(ctx);

            t0 = tcg_temp_new();
            t1 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_load_gpr(t1, rs);

            gen_helper_dinsv(cpu_gpr[rt], cpu_env, t1, t0);

            tcg_temp_free(t0);
            tcg_temp_free(t1);
            break;
        }
        default:            /* Invalid */
            MIPS_INVAL("MASK DINSV");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_SHLL_OB_DSP:
        gen_mipsdsp_shift(ctx, op1, rd, rs, rt);
        break;
#endif
    default:            /* Invalid */
        MIPS_INVAL("special3_legacy");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void decode_opc_special3(CPUMIPSState *env, DisasContext *ctx)
{
    int rs, rt, rd, sa;
    uint32_t op1, op2;
    int16_t imm;

    rs = (ctx->opcode >> 21) & 0x1f;
    rt = (ctx->opcode >> 16) & 0x1f;
    rd = (ctx->opcode >> 11) & 0x1f;
    sa = (ctx->opcode >> 6) & 0x1f;
    imm = sextract32(ctx->opcode, 7, 9);

    op1 = MASK_SPECIAL3(ctx->opcode);

    /*
     * EVA loads and stores overlap Loongson 2E instructions decoded by
     * decode_opc_special3_legacy(), so be careful to allow their decoding when
     * EVA is absent.
     */
    if (ctx->eva) {
        switch (op1) {
        case OPC_LWLE ... OPC_LWRE:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            /* fall through */
        case OPC_LBUE ... OPC_LHUE:
        case OPC_LBE ... OPC_LWE:
            check_cp0_enabled(ctx);
            gen_ld(ctx, op1, rt, rs, imm);
            return;
        case OPC_SWLE ... OPC_SWRE:
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            /* fall through */
        case OPC_SBE ... OPC_SHE:
        case OPC_SWE:
            check_cp0_enabled(ctx);
            gen_st(ctx, op1, rt, rs, imm);
            return;
        case OPC_SCE:
            check_cp0_enabled(ctx);
            gen_st_cond(ctx, op1, rt, rs, imm);
            return;
        case OPC_CACHEE:
            check_cp0_enabled(ctx);
            if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
                gen_cache_operation(ctx, rt, rs, imm);
            }
            /* Treat as NOP. */
            return;
        case OPC_PREFE:
            check_cp0_enabled(ctx);
            /* Treat as NOP. */
            return;
        }
    }

    switch (op1) {
    case OPC_EXT:
    case OPC_INS:
        check_insn(ctx, ISA_MIPS32R2);
        gen_bitops(ctx, op1, rt, rs, sa, rd);
        break;
    case OPC_BSHFL:
        op2 = MASK_BSHFL(ctx->opcode);
        switch (op2) {
        case OPC_ALIGN ... OPC_ALIGN_END:
        case OPC_BITSWAP:
            check_insn(ctx, ISA_MIPS32R6);
            decode_opc_special3_r6(env, ctx);
            break;
        default:
            check_insn(ctx, ISA_MIPS32R2);
            gen_bshfl(ctx, op2, rt, rd);
            break;
        }
        break;
#if defined(TARGET_MIPS64)
    case OPC_DEXTM ... OPC_DEXT:
    case OPC_DINSM ... OPC_DINS:
        check_insn(ctx, ISA_MIPS64R2);
        check_mips_64(ctx);
        gen_bitops(ctx, op1, rt, rs, sa, rd);
        break;
    case OPC_DBSHFL:
        op2 = MASK_DBSHFL(ctx->opcode);
        switch (op2) {
        case OPC_DALIGN ... OPC_DALIGN_END:
        case OPC_DBITSWAP:
            check_insn(ctx, ISA_MIPS32R6);
            decode_opc_special3_r6(env, ctx);
            break;
        default:
            check_insn(ctx, ISA_MIPS64R2);
            check_mips_64(ctx);
            op2 = MASK_DBSHFL(ctx->opcode);
            gen_bshfl(ctx, op2, rt, rd);
            break;
        }
        break;
#endif
    case OPC_RDHWR:
        gen_rdhwr(ctx, rt, rd, extract32(ctx->opcode, 6, 3));
        break;
    case OPC_FORK:
        check_insn(ctx, ASE_MT);
        {
            TCGv t0 = tcg_temp_new();
            TCGv t1 = tcg_temp_new();

            gen_load_gpr(t0, rt);
            gen_load_gpr(t1, rs);
            gen_helper_fork(t0, t1);
            tcg_temp_free(t0);
            tcg_temp_free(t1);
        }
        break;
    case OPC_YIELD:
        check_insn(ctx, ASE_MT);
        {
            TCGv t0 = tcg_temp_new();

            gen_load_gpr(t0, rs);
            gen_helper_yield(t0, cpu_env, t0);
            gen_store_gpr(t0, rd);
            tcg_temp_free(t0);
        }
        break;
    default:
        if (ctx->insn_flags & ISA_MIPS32R6) {
            decode_opc_special3_r6(env, ctx);
        } else {
            decode_opc_special3_legacy(env, ctx);
        }
    }
}

/* MIPS SIMD Architecture (MSA)  */
static inline int check_msa_access(DisasContext *ctx)
{
    if (unlikely((ctx->hflags & MIPS_HFLAG_FPU) &&
                 !(ctx->hflags & MIPS_HFLAG_F64))) {
        generate_exception_end(ctx, EXCP_RI);
        return 0;
    }

    if (unlikely(!(ctx->hflags & MIPS_HFLAG_MSA))) {
        if (ctx->insn_flags & ASE_MSA) {
            generate_exception_end(ctx, EXCP_MSADIS);
            return 0;
        } else {
            generate_exception_end(ctx, EXCP_RI);
            return 0;
        }
    }
    return 1;
}

static void gen_check_zero_element(TCGv tresult, uint8_t df, uint8_t wt)
{
    /* generates tcg ops to check if any element is 0 */
    /* Note this function only works with MSA_WRLEN = 128 */
    uint64_t eval_zero_or_big = 0;
    uint64_t eval_big = 0;
    TCGv_i64 t0 = tcg_temp_new_i64();
    TCGv_i64 t1 = tcg_temp_new_i64();
    switch (df) {
    case DF_BYTE:
        eval_zero_or_big = 0x0101010101010101ULL;
        eval_big = 0x8080808080808080ULL;
        break;
    case DF_HALF:
        eval_zero_or_big = 0x0001000100010001ULL;
        eval_big = 0x8000800080008000ULL;
        break;
    case DF_WORD:
        eval_zero_or_big = 0x0000000100000001ULL;
        eval_big = 0x8000000080000000ULL;
        break;
    case DF_DOUBLE:
        eval_zero_or_big = 0x0000000000000001ULL;
        eval_big = 0x8000000000000000ULL;
        break;
    }
    tcg_gen_subi_i64(t0, msa_wr_d[wt<<1], eval_zero_or_big);
    tcg_gen_andc_i64(t0, t0, msa_wr_d[wt<<1]);
    tcg_gen_andi_i64(t0, t0, eval_big);
    tcg_gen_subi_i64(t1, msa_wr_d[(wt<<1)+1], eval_zero_or_big);
    tcg_gen_andc_i64(t1, t1, msa_wr_d[(wt<<1)+1]);
    tcg_gen_andi_i64(t1, t1, eval_big);
    tcg_gen_or_i64(t0, t0, t1);
    /* if all bits are zero then all elements are not zero */
    /* if some bit is non-zero then some element is zero */
    tcg_gen_setcondi_i64(TCG_COND_NE, t0, t0, 0);
    tcg_gen_trunc_i64_tl(tresult, t0);
    tcg_temp_free_i64(t0);
    tcg_temp_free_i64(t1);
}

static void gen_msa_branch(CPUMIPSState *env, DisasContext *ctx, uint32_t op1)
{
    uint8_t df = (ctx->opcode >> 21) & 0x3;
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    int64_t s16 = (int16_t)ctx->opcode;

    check_msa_access(ctx);

    if (ctx->hflags & MIPS_HFLAG_BMASK) {
        generate_exception_end(ctx, EXCP_RI);
        return;
    }
    switch (op1) {
    case OPC_BZ_V:
    case OPC_BNZ_V:
        {
            TCGv_i64 t0 = tcg_temp_new_i64();
            tcg_gen_or_i64(t0, msa_wr_d[wt<<1], msa_wr_d[(wt<<1)+1]);
            tcg_gen_setcondi_i64((op1 == OPC_BZ_V) ?
                    TCG_COND_EQ : TCG_COND_NE, t0, t0, 0);
            tcg_gen_trunc_i64_tl(bcond, t0);
            tcg_temp_free_i64(t0);
        }
        break;
    case OPC_BZ_B:
    case OPC_BZ_H:
    case OPC_BZ_W:
    case OPC_BZ_D:
        gen_check_zero_element(bcond, df, wt);
        break;
    case OPC_BNZ_B:
    case OPC_BNZ_H:
    case OPC_BNZ_W:
    case OPC_BNZ_D:
        gen_check_zero_element(bcond, df, wt);
        tcg_gen_setcondi_tl(TCG_COND_EQ, bcond, bcond, 0);
        break;
    }

    ctx->btarget = ctx->pc + (s16 << 2) + 4;

    ctx->hflags |= MIPS_HFLAG_BC;
    ctx->hflags |= MIPS_HFLAG_BDS32;
}

static void gen_msa_i8(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_I8(op)    (MASK_MSA_MINOR(op) | (op & (0x03 << 24)))
    uint8_t i8 = (ctx->opcode >> 16) & 0xff;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 ti8 = tcg_const_i32(i8);

    switch (MASK_MSA_I8(ctx->opcode)) {
    case OPC_ANDI_B:
        gen_helper_msa_andi_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_ORI_B:
        gen_helper_msa_ori_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_NORI_B:
        gen_helper_msa_nori_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_XORI_B:
        gen_helper_msa_xori_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_BMNZI_B:
        gen_helper_msa_bmnzi_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_BMZI_B:
        gen_helper_msa_bmzi_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_BSELI_B:
        gen_helper_msa_bseli_b(cpu_env, twd, tws, ti8);
        break;
    case OPC_SHF_B:
    case OPC_SHF_H:
    case OPC_SHF_W:
        {
            uint8_t df = (ctx->opcode >> 24) & 0x3;
            if (df == DF_DOUBLE) {
                generate_exception_end(ctx, EXCP_RI);
            } else {
                TCGv_i32 tdf = tcg_const_i32(df);
                gen_helper_msa_shf_df(cpu_env, tdf, twd, tws, ti8);
                tcg_temp_free_i32(tdf);
            }
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(ti8);
}

static void gen_msa_i5(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_I5(op)    (MASK_MSA_MINOR(op) | (op & (0x7 << 23)))
    uint8_t df = (ctx->opcode >> 21) & 0x3;
    int8_t s5 = (int8_t) sextract32(ctx->opcode, 16, 5);
    uint8_t u5 = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tdf = tcg_const_i32(df);
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 timm = tcg_temp_new_i32();
    tcg_gen_movi_i32(timm, u5);

    switch (MASK_MSA_I5(ctx->opcode)) {
    case OPC_ADDVI_df:
        gen_helper_msa_addvi_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_SUBVI_df:
        gen_helper_msa_subvi_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MAXI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_maxi_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MAXI_U_df:
        gen_helper_msa_maxi_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MINI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_mini_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_MINI_U_df:
        gen_helper_msa_mini_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CEQI_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_ceqi_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLTI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_clti_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLTI_U_df:
        gen_helper_msa_clti_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLEI_S_df:
        tcg_gen_movi_i32(timm, s5);
        gen_helper_msa_clei_s_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_CLEI_U_df:
        gen_helper_msa_clei_u_df(cpu_env, tdf, twd, tws, timm);
        break;
    case OPC_LDI_df:
        {
            int32_t s10 = sextract32(ctx->opcode, 11, 10);
            tcg_gen_movi_i32(timm, s10);
            gen_helper_msa_ldi_df(cpu_env, tdf, twd, timm);
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free_i32(tdf);
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(timm);
}

static void gen_msa_bit(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_BIT(op)    (MASK_MSA_MINOR(op) | (op & (0x7 << 23)))
    uint8_t dfm = (ctx->opcode >> 16) & 0x7f;
    uint32_t df = 0, m = 0;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tdf;
    TCGv_i32 tm;
    TCGv_i32 twd;
    TCGv_i32 tws;

    if ((dfm & 0x40) == 0x00) {
        m = dfm & 0x3f;
        df = DF_DOUBLE;
    } else if ((dfm & 0x60) == 0x40) {
        m = dfm & 0x1f;
        df = DF_WORD;
    } else if ((dfm & 0x70) == 0x60) {
        m = dfm & 0x0f;
        df = DF_HALF;
    } else if ((dfm & 0x78) == 0x70) {
        m = dfm & 0x7;
        df = DF_BYTE;
    } else {
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    tdf = tcg_const_i32(df);
    tm  = tcg_const_i32(m);
    twd = tcg_const_i32(wd);
    tws = tcg_const_i32(ws);

    switch (MASK_MSA_BIT(ctx->opcode)) {
    case OPC_SLLI_df:
        gen_helper_msa_slli_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRAI_df:
        gen_helper_msa_srai_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRLI_df:
        gen_helper_msa_srli_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BCLRI_df:
        gen_helper_msa_bclri_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BSETI_df:
        gen_helper_msa_bseti_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BNEGI_df:
        gen_helper_msa_bnegi_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BINSLI_df:
        gen_helper_msa_binsli_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_BINSRI_df:
        gen_helper_msa_binsri_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SAT_S_df:
        gen_helper_msa_sat_s_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SAT_U_df:
        gen_helper_msa_sat_u_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRARI_df:
        gen_helper_msa_srari_df(cpu_env, tdf, twd, tws, tm);
        break;
    case OPC_SRLRI_df:
        gen_helper_msa_srlri_df(cpu_env, tdf, twd, tws, tm);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free_i32(tdf);
    tcg_temp_free_i32(tm);
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
}

static void gen_msa_3r(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_3R(op)    (MASK_MSA_MINOR(op) | (op & (0x7 << 23)))
    uint8_t df = (ctx->opcode >> 21) & 0x3;
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tdf = tcg_const_i32(df);
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);

    switch (MASK_MSA_3R(ctx->opcode)) {
    case OPC_SLL_df:
        gen_helper_msa_sll_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ADDV_df:
        gen_helper_msa_addv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_CEQ_df:
        gen_helper_msa_ceq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ADD_A_df:
        gen_helper_msa_add_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBS_S_df:
        gen_helper_msa_subs_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MULV_df:
        gen_helper_msa_mulv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SLD_df:
        gen_helper_msa_sld_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_VSHF_df:
        gen_helper_msa_vshf_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SRA_df:
        gen_helper_msa_sra_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBV_df:
        gen_helper_msa_subv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ADDS_A_df:
        gen_helper_msa_adds_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBS_U_df:
        gen_helper_msa_subs_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MADDV_df:
        gen_helper_msa_maddv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SPLAT_df:
        gen_helper_msa_splat_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SRAR_df:
        gen_helper_msa_srar_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SRL_df:
        gen_helper_msa_srl_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MAX_S_df:
        gen_helper_msa_max_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_CLT_S_df:
        gen_helper_msa_clt_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ADDS_S_df:
        gen_helper_msa_adds_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBSUS_U_df:
        gen_helper_msa_subsus_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MSUBV_df:
        gen_helper_msa_msubv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_PCKEV_df:
        gen_helper_msa_pckev_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SRLR_df:
        gen_helper_msa_srlr_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_BCLR_df:
        gen_helper_msa_bclr_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MAX_U_df:
        gen_helper_msa_max_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_CLT_U_df:
        gen_helper_msa_clt_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ADDS_U_df:
        gen_helper_msa_adds_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_SUBSUU_S_df:
        gen_helper_msa_subsuu_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_PCKOD_df:
        gen_helper_msa_pckod_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_BSET_df:
        gen_helper_msa_bset_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MIN_S_df:
        gen_helper_msa_min_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_CLE_S_df:
        gen_helper_msa_cle_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_AVE_S_df:
        gen_helper_msa_ave_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ASUB_S_df:
        gen_helper_msa_asub_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_DIV_S_df:
        gen_helper_msa_div_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ILVL_df:
        gen_helper_msa_ilvl_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_BNEG_df:
        gen_helper_msa_bneg_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MIN_U_df:
        gen_helper_msa_min_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_CLE_U_df:
        gen_helper_msa_cle_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_AVE_U_df:
        gen_helper_msa_ave_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ASUB_U_df:
        gen_helper_msa_asub_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_DIV_U_df:
        gen_helper_msa_div_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ILVR_df:
        gen_helper_msa_ilvr_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_BINSL_df:
        gen_helper_msa_binsl_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MAX_A_df:
        gen_helper_msa_max_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_AVER_S_df:
        gen_helper_msa_aver_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MOD_S_df:
        gen_helper_msa_mod_s_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ILVEV_df:
        gen_helper_msa_ilvev_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_BINSR_df:
        gen_helper_msa_binsr_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MIN_A_df:
        gen_helper_msa_min_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_AVER_U_df:
        gen_helper_msa_aver_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MOD_U_df:
        gen_helper_msa_mod_u_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_ILVOD_df:
        gen_helper_msa_ilvod_df(cpu_env, tdf, twd, tws, twt);
        break;

    case OPC_DOTP_S_df:
    case OPC_DOTP_U_df:
    case OPC_DPADD_S_df:
    case OPC_DPADD_U_df:
    case OPC_DPSUB_S_df:
    case OPC_HADD_S_df:
    case OPC_DPSUB_U_df:
    case OPC_HADD_U_df:
    case OPC_HSUB_S_df:
    case OPC_HSUB_U_df:
        if (df == DF_BYTE) {
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        switch (MASK_MSA_3R(ctx->opcode)) {
        case OPC_DOTP_S_df:
            gen_helper_msa_dotp_s_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_DOTP_U_df:
            gen_helper_msa_dotp_u_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_DPADD_S_df:
            gen_helper_msa_dpadd_s_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_DPADD_U_df:
            gen_helper_msa_dpadd_u_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_DPSUB_S_df:
            gen_helper_msa_dpsub_s_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_HADD_S_df:
            gen_helper_msa_hadd_s_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_DPSUB_U_df:
            gen_helper_msa_dpsub_u_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_HADD_U_df:
            gen_helper_msa_hadd_u_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_HSUB_S_df:
            gen_helper_msa_hsub_s_df(cpu_env, tdf, twd, tws, twt);
            break;
        case OPC_HSUB_U_df:
            gen_helper_msa_hsub_u_df(cpu_env, tdf, twd, tws, twt);
            break;
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_elm_3e(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_ELM_DF3E(op)   (MASK_MSA_MINOR(op) | (op & (0x3FF << 16)))
    uint8_t source = (ctx->opcode >> 11) & 0x1f;
    uint8_t dest = (ctx->opcode >> 6) & 0x1f;
    TCGv telm = tcg_temp_new();
    TCGv_i32 tsr = tcg_const_i32(source);
    TCGv_i32 tdt = tcg_const_i32(dest);

    switch (MASK_MSA_ELM_DF3E(ctx->opcode)) {
    case OPC_CTCMSA:
        gen_load_gpr(telm, source);
        gen_helper_msa_ctcmsa(cpu_env, telm, tdt);
        break;
    case OPC_CFCMSA:
        gen_helper_msa_cfcmsa(telm, cpu_env, tsr);
        gen_store_gpr(telm, dest);
        break;
    case OPC_MOVE_V:
        gen_helper_msa_move_v(cpu_env, tdt, tsr);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free(telm);
    tcg_temp_free_i32(tdt);
    tcg_temp_free_i32(tsr);
}

static void gen_msa_elm_df(CPUMIPSState *env, DisasContext *ctx, uint32_t df,
        uint32_t n)
{
#define MASK_MSA_ELM(op)    (MASK_MSA_MINOR(op) | (op & (0xf << 22)))
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tn  = tcg_const_i32(n);
    TCGv_i32 tdf = tcg_const_i32(df);

    switch (MASK_MSA_ELM(ctx->opcode)) {
    case OPC_SLDI_df:
        gen_helper_msa_sldi_df(cpu_env, tdf, twd, tws, tn);
        break;
    case OPC_SPLATI_df:
        gen_helper_msa_splati_df(cpu_env, tdf, twd, tws, tn);
        break;
    case OPC_INSVE_df:
        gen_helper_msa_insve_df(cpu_env, tdf, twd, tws, tn);
        break;
    case OPC_COPY_S_df:
    case OPC_COPY_U_df:
    case OPC_INSERT_df:
#if !defined(TARGET_MIPS64)
        /* Double format valid only for MIPS64 */
        if (df == DF_DOUBLE) {
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
#endif
        switch (MASK_MSA_ELM(ctx->opcode)) {
        case OPC_COPY_S_df:
            if (likely(wd != 0)) {
                gen_helper_msa_copy_s_df(cpu_env, tdf, twd, tws, tn);
            }
            break;
        case OPC_COPY_U_df:
            if (likely(wd != 0)) {
                gen_helper_msa_copy_u_df(cpu_env, tdf, twd, tws, tn);
            }
            break;
        case OPC_INSERT_df:
            gen_helper_msa_insert_df(cpu_env, tdf, twd, tws, tn);
            break;
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
    }
    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(tn);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_elm(CPUMIPSState *env, DisasContext *ctx)
{
    uint8_t dfn = (ctx->opcode >> 16) & 0x3f;
    uint32_t df = 0, n = 0;

    if ((dfn & 0x30) == 0x00) {
        n = dfn & 0x0f;
        df = DF_BYTE;
    } else if ((dfn & 0x38) == 0x20) {
        n = dfn & 0x07;
        df = DF_HALF;
    } else if ((dfn & 0x3c) == 0x30) {
        n = dfn & 0x03;
        df = DF_WORD;
    } else if ((dfn & 0x3e) == 0x38) {
        n = dfn & 0x01;
        df = DF_DOUBLE;
    } else if (dfn == 0x3E) {
        /* CTCMSA, CFCMSA, MOVE.V */
        gen_msa_elm_3e(env, ctx);
        return;
    } else {
        generate_exception_end(ctx, EXCP_RI);
        return;
    }

    gen_msa_elm_df(env, ctx, df, n);
}

static void gen_msa_3rf(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_3RF(op)    (MASK_MSA_MINOR(op) | (op & (0xf << 22)))
    uint8_t df = (ctx->opcode >> 21) & 0x1;
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;

    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);
    TCGv_i32 tdf = tcg_temp_new_i32();

    /* adjust df value for floating-point instruction */
    tcg_gen_movi_i32(tdf, df + 2);

    switch (MASK_MSA_3RF(ctx->opcode)) {
    case OPC_FCAF_df:
        gen_helper_msa_fcaf_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FADD_df:
        gen_helper_msa_fadd_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCUN_df:
        gen_helper_msa_fcun_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUB_df:
        gen_helper_msa_fsub_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCOR_df:
        gen_helper_msa_fcor_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCEQ_df:
        gen_helper_msa_fceq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMUL_df:
        gen_helper_msa_fmul_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCUNE_df:
        gen_helper_msa_fcune_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCUEQ_df:
        gen_helper_msa_fcueq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FDIV_df:
        gen_helper_msa_fdiv_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCNE_df:
        gen_helper_msa_fcne_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCLT_df:
        gen_helper_msa_fclt_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMADD_df:
        gen_helper_msa_fmadd_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MUL_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_mul_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCULT_df:
        gen_helper_msa_fcult_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMSUB_df:
        gen_helper_msa_fmsub_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MADD_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_madd_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCLE_df:
        gen_helper_msa_fcle_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MSUB_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_msub_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FCULE_df:
        gen_helper_msa_fcule_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FEXP2_df:
        gen_helper_msa_fexp2_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSAF_df:
        gen_helper_msa_fsaf_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FEXDO_df:
        gen_helper_msa_fexdo_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUN_df:
        gen_helper_msa_fsun_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSOR_df:
        gen_helper_msa_fsor_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSEQ_df:
        gen_helper_msa_fseq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FTQ_df:
        gen_helper_msa_ftq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUNE_df:
        gen_helper_msa_fsune_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSUEQ_df:
        gen_helper_msa_fsueq_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSNE_df:
        gen_helper_msa_fsne_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSLT_df:
        gen_helper_msa_fslt_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMIN_df:
        gen_helper_msa_fmin_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MULR_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_mulr_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSULT_df:
        gen_helper_msa_fsult_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMIN_A_df:
        gen_helper_msa_fmin_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MADDR_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_maddr_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSLE_df:
        gen_helper_msa_fsle_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMAX_df:
        gen_helper_msa_fmax_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_MSUBR_Q_df:
        tcg_gen_movi_i32(tdf, df + 1);
        gen_helper_msa_msubr_q_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FSULE_df:
        gen_helper_msa_fsule_df(cpu_env, tdf, twd, tws, twt);
        break;
    case OPC_FMAX_A_df:
        gen_helper_msa_fmax_a_df(cpu_env, tdf, twd, tws, twt);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_2r(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_2R(op)     (MASK_MSA_MINOR(op) | (op & (0x1f << 21)) | \
                            (op & (0x7 << 18)))
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;
    uint8_t df = (ctx->opcode >> 16) & 0x3;
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);
    TCGv_i32 tdf = tcg_const_i32(df);

    switch (MASK_MSA_2R(ctx->opcode)) {
    case OPC_FILL_df:
#if !defined(TARGET_MIPS64)
        /* Double format valid only for MIPS64 */
        if (df == DF_DOUBLE) {
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
#endif
        gen_helper_msa_fill_df(cpu_env, tdf, twd, tws); /* trs */
        break;
    case OPC_PCNT_df:
        gen_helper_msa_pcnt_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_NLOC_df:
        gen_helper_msa_nloc_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_NLZC_df:
        gen_helper_msa_nlzc_df(cpu_env, tdf, twd, tws);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_2rf(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_2RF(op)    (MASK_MSA_MINOR(op) | (op & (0x1f << 21)) | \
                            (op & (0xf << 17)))
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;
    uint8_t df = (ctx->opcode >> 16) & 0x1;
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);
    /* adjust df value for floating-point instruction */
    TCGv_i32 tdf = tcg_const_i32(df + 2);

    switch (MASK_MSA_2RF(ctx->opcode)) {
    case OPC_FCLASS_df:
        gen_helper_msa_fclass_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTRUNC_S_df:
        gen_helper_msa_ftrunc_s_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTRUNC_U_df:
        gen_helper_msa_ftrunc_u_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FSQRT_df:
        gen_helper_msa_fsqrt_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FRSQRT_df:
        gen_helper_msa_frsqrt_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FRCP_df:
        gen_helper_msa_frcp_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FRINT_df:
        gen_helper_msa_frint_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FLOG2_df:
        gen_helper_msa_flog2_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FEXUPL_df:
        gen_helper_msa_fexupl_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FEXUPR_df:
        gen_helper_msa_fexupr_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFQL_df:
        gen_helper_msa_ffql_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFQR_df:
        gen_helper_msa_ffqr_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTINT_S_df:
        gen_helper_msa_ftint_s_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FTINT_U_df:
        gen_helper_msa_ftint_u_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFINT_S_df:
        gen_helper_msa_ffint_s_df(cpu_env, tdf, twd, tws);
        break;
    case OPC_FFINT_U_df:
        gen_helper_msa_ffint_u_df(cpu_env, tdf, twd, tws);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
    tcg_temp_free_i32(tdf);
}

static void gen_msa_vec_v(CPUMIPSState *env, DisasContext *ctx)
{
#define MASK_MSA_VEC(op)    (MASK_MSA_MINOR(op) | (op & (0x1f << 21)))
    uint8_t wt = (ctx->opcode >> 16) & 0x1f;
    uint8_t ws = (ctx->opcode >> 11) & 0x1f;
    uint8_t wd = (ctx->opcode >> 6) & 0x1f;
    TCGv_i32 twd = tcg_const_i32(wd);
    TCGv_i32 tws = tcg_const_i32(ws);
    TCGv_i32 twt = tcg_const_i32(wt);

    switch (MASK_MSA_VEC(ctx->opcode)) {
    case OPC_AND_V:
        gen_helper_msa_and_v(cpu_env, twd, tws, twt);
        break;
    case OPC_OR_V:
        gen_helper_msa_or_v(cpu_env, twd, tws, twt);
        break;
    case OPC_NOR_V:
        gen_helper_msa_nor_v(cpu_env, twd, tws, twt);
        break;
    case OPC_XOR_V:
        gen_helper_msa_xor_v(cpu_env, twd, tws, twt);
        break;
    case OPC_BMNZ_V:
        gen_helper_msa_bmnz_v(cpu_env, twd, tws, twt);
        break;
    case OPC_BMZ_V:
        gen_helper_msa_bmz_v(cpu_env, twd, tws, twt);
        break;
    case OPC_BSEL_V:
        gen_helper_msa_bsel_v(cpu_env, twd, tws, twt);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

    tcg_temp_free_i32(twd);
    tcg_temp_free_i32(tws);
    tcg_temp_free_i32(twt);
}

static void gen_msa_vec(CPUMIPSState *env, DisasContext *ctx)
{
    switch (MASK_MSA_VEC(ctx->opcode)) {
    case OPC_AND_V:
    case OPC_OR_V:
    case OPC_NOR_V:
    case OPC_XOR_V:
    case OPC_BMNZ_V:
    case OPC_BMZ_V:
    case OPC_BSEL_V:
        gen_msa_vec_v(env, ctx);
        break;
    case OPC_MSA_2R:
        gen_msa_2r(env, ctx);
        break;
    case OPC_MSA_2RF:
        gen_msa_2rf(env, ctx);
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

static void gen_msa(CPUMIPSState *env, DisasContext *ctx)
{
    uint32_t opcode = ctx->opcode;
    check_insn(ctx, ASE_MSA);
    check_msa_access(ctx);

    switch (MASK_MSA_MINOR(opcode)) {
    case OPC_MSA_I8_00:
    case OPC_MSA_I8_01:
    case OPC_MSA_I8_02:
        gen_msa_i8(env, ctx);
        break;
    case OPC_MSA_I5_06:
    case OPC_MSA_I5_07:
        gen_msa_i5(env, ctx);
        break;
    case OPC_MSA_BIT_09:
    case OPC_MSA_BIT_0A:
        gen_msa_bit(env, ctx);
        break;
    case OPC_MSA_3R_0D:
    case OPC_MSA_3R_0E:
    case OPC_MSA_3R_0F:
    case OPC_MSA_3R_10:
    case OPC_MSA_3R_11:
    case OPC_MSA_3R_12:
    case OPC_MSA_3R_13:
    case OPC_MSA_3R_14:
    case OPC_MSA_3R_15:
        gen_msa_3r(env, ctx);
        break;
    case OPC_MSA_ELM:
        gen_msa_elm(env, ctx);
        break;
    case OPC_MSA_3RF_1A:
    case OPC_MSA_3RF_1B:
    case OPC_MSA_3RF_1C:
        gen_msa_3rf(env, ctx);
        break;
    case OPC_MSA_VEC:
        gen_msa_vec(env, ctx);
        break;
    case OPC_LD_B:
    case OPC_LD_H:
    case OPC_LD_W:
    case OPC_LD_D:
    case OPC_ST_B:
    case OPC_ST_H:
    case OPC_ST_W:
    case OPC_ST_D:
        {
            int32_t s10 = sextract32(ctx->opcode, 16, 10);
            uint8_t rs = (ctx->opcode >> 11) & 0x1f;
            uint8_t wd = (ctx->opcode >> 6) & 0x1f;
            uint8_t df = (ctx->opcode >> 0) & 0x3;

            TCGv_i32 twd = tcg_const_i32(wd);
            TCGv taddr = tcg_temp_new();
            gen_base_offset_addr(ctx, taddr, rs, s10 << df);

            switch (MASK_MSA_MINOR(opcode)) {
            case OPC_LD_B:
                gen_helper_msa_ld_b(cpu_env, twd, taddr);
                break;
            case OPC_LD_H:
                gen_helper_msa_ld_h(cpu_env, twd, taddr);
                break;
            case OPC_LD_W:
                gen_helper_msa_ld_w(cpu_env, twd, taddr);
                break;
            case OPC_LD_D:
                gen_helper_msa_ld_d(cpu_env, twd, taddr);
                break;
            case OPC_ST_B:
                gen_helper_msa_st_b(cpu_env, twd, taddr);
                break;
            case OPC_ST_H:
                gen_helper_msa_st_h(cpu_env, twd, taddr);
                break;
            case OPC_ST_W:
                gen_helper_msa_st_w(cpu_env, twd, taddr);
                break;
            case OPC_ST_D:
                gen_helper_msa_st_d(cpu_env, twd, taddr);
                break;
            }

            tcg_temp_free_i32(twd);
            tcg_temp_free(taddr);
        }
        break;
    default:
        MIPS_INVAL("MSA instruction");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }

}

static void decode_opc(CPUMIPSState *env, DisasContext *ctx)
{
    int32_t offset;
    int rs, rt, rd, sa;
    uint32_t op, op1;
    int16_t imm;

    /* make sure instructions are on a word boundary */
    if (ctx->pc & 0x3) {
        env->CP0_BadVAddr = ctx->pc;
        generate_exception_err(ctx, EXCP_AdEL, EXCP_INST_NOTAVAIL);
        return;
    }

    /* Handle blikely not taken case */
    if ((ctx->hflags & MIPS_HFLAG_BMASK_BASE) == MIPS_HFLAG_BL) {
        TCGLabel *l1 = gen_new_label();

        tcg_gen_brcondi_tl(TCG_COND_NE, bcond, 0, l1);
        tcg_gen_movi_i32(hflags, ctx->hflags & ~MIPS_HFLAG_BMASK);
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
        decode_opc_special(env, ctx);
        break;
    case OPC_SPECIAL2:
        decode_opc_special2_legacy(env, ctx);
        break;
    case OPC_SPECIAL3:
        decode_opc_special3(env, ctx);
        break;
    case OPC_REGIMM:
        op1 = MASK_REGIMM(ctx->opcode);
        switch (op1) {
        case OPC_BLTZL: /* REGIMM branches */
        case OPC_BGEZL:
        case OPC_BLTZALL:
        case OPC_BGEZALL:
            check_insn(ctx, ISA_MIPS2);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            /* Fallthrough */
        case OPC_BLTZ:
        case OPC_BGEZ:
            gen_compute_branch(ctx, op1, 4, rs, -1, imm << 2, 4);
            break;
        case OPC_BLTZAL:
        case OPC_BGEZAL:
            if (ctx->insn_flags & ISA_MIPS32R6) {
                if (rs == 0) {
                    /* OPC_NAL, OPC_BAL */
                    gen_compute_branch(ctx, op1, 4, 0, -1, imm << 2, 4);
                } else {
                    generate_exception_end(ctx, EXCP_RI);
                }
            } else {
                gen_compute_branch(ctx, op1, 4, rs, -1, imm << 2, 4);
            }
            break;
        case OPC_TGEI ... OPC_TEQI: /* REGIMM traps */
        case OPC_TNEI:
            check_insn(ctx, ISA_MIPS2);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_trap(ctx, op1, rs, -1, imm);
            break;
        case OPC_SIGRIE:
            check_insn(ctx, ISA_MIPS32R6);
            generate_exception_end(ctx, EXCP_RI);
            break;
        case OPC_SYNCI:
            check_insn(ctx, ISA_MIPS32R2);
            /* Break the TB to be able to sync copied instructions
               immediately */
            ctx->bstate = BS_STOP;
            break;
        case OPC_BPOSGE32:    /* MIPS DSP branch */
#if defined(TARGET_MIPS64)
        case OPC_BPOSGE64:
#endif
            check_dsp(ctx);
            gen_compute_branch(ctx, op1, 4, -1, -2, (int32_t)imm << 2, 4);
            break;
#if defined(TARGET_MIPS64)
        case OPC_DAHI:
            check_insn(ctx, ISA_MIPS32R6);
            check_mips_64(ctx);
            if (rs != 0) {
                tcg_gen_addi_tl(cpu_gpr[rs], cpu_gpr[rs], (int64_t)imm << 32);
            }
            break;
        case OPC_DATI:
            check_insn(ctx, ISA_MIPS32R6);
            check_mips_64(ctx);
            if (rs != 0) {
                tcg_gen_addi_tl(cpu_gpr[rs], cpu_gpr[rs], (int64_t)imm << 48);
            }
            break;
#endif
        default:            /* Invalid */
            MIPS_INVAL("regimm");
            generate_exception_end(ctx, EXCP_RI);
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
        case OPC_MFHC0:
        case OPC_MTHC0:
#if defined(TARGET_MIPS64)
        case OPC_DMFC0:
        case OPC_DMTC0:
#endif
#ifndef CONFIG_USER_ONLY
            gen_cp0(env, ctx, op1, rt, rd);
#endif /* !CONFIG_USER_ONLY */
            break;
        case OPC_C0_FIRST ... OPC_C0_LAST:
#ifndef CONFIG_USER_ONLY
            gen_cp0(env, ctx, MASK_C0(ctx->opcode), rt, rd);
#endif /* !CONFIG_USER_ONLY */
            break;
        case OPC_MFMC0:
#ifndef CONFIG_USER_ONLY
            {
                uint32_t op2;
                TCGv t0 = tcg_temp_new();

                op2 = MASK_MFMC0(ctx->opcode);
                switch (op2) {
                case OPC_DMT:
                    check_insn(ctx, ASE_MT);
                    gen_helper_dmt(t0);
                    gen_store_gpr(t0, rt);
                    break;
                case OPC_EMT:
                    check_insn(ctx, ASE_MT);
                    gen_helper_emt(t0);
                    gen_store_gpr(t0, rt);
                    break;
                case OPC_DVPE:
                    check_insn(ctx, ASE_MT);
                    gen_helper_dvpe(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                    break;
                case OPC_EVPE:
                    check_insn(ctx, ASE_MT);
                    gen_helper_evpe(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                    break;
                case OPC_DVP:
                    check_insn(ctx, ISA_MIPS32R6);
                    if (ctx->vp) {
                        gen_helper_dvp(t0, cpu_env);
                        gen_store_gpr(t0, rt);
                    }
                    break;
                case OPC_EVP:
                    check_insn(ctx, ISA_MIPS32R6);
                    if (ctx->vp) {
                        gen_helper_evp(t0, cpu_env);
                        gen_store_gpr(t0, rt);
                    }
                    break;
                case OPC_DI:
                    check_insn(ctx, ISA_MIPS32R2);
                    save_cpu_state(ctx, 1);
                    gen_helper_di(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                    /* Stop translation as we may have switched
                       the execution mode.  */
                    ctx->bstate = BS_STOP;
                    break;
                case OPC_EI:
                    check_insn(ctx, ISA_MIPS32R2);
                    save_cpu_state(ctx, 1);
                    gen_helper_ei(t0, cpu_env);
                    gen_store_gpr(t0, rt);
                    /* BS_STOP isn't sufficient, we need to ensure we break out
                       of translated code to check for pending interrupts.  */
                    gen_save_pc(ctx->pc + 4);
                    ctx->bstate = BS_EXCP;
                    break;
                default:            /* Invalid */
                    MIPS_INVAL("mfmc0");
                    generate_exception_end(ctx, EXCP_RI);
                    break;
                }
                tcg_temp_free(t0);
            }
#endif /* !CONFIG_USER_ONLY */
            break;
        case OPC_RDPGPR:
            check_insn(ctx, ISA_MIPS32R2);
            gen_load_srsgpr(rt, rd);
            break;
        case OPC_WRPGPR:
            check_insn(ctx, ISA_MIPS32R2);
            gen_store_srsgpr(rt, rd);
            break;
        default:
            MIPS_INVAL("cp0");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;
    case OPC_BOVC: /* OPC_BEQZALC, OPC_BEQC, OPC_ADDI */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_BOVC, OPC_BEQZALC, OPC_BEQC */
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        } else {
            /* OPC_ADDI */
            /* Arithmetic with immediate opcode */
            gen_arith_imm(ctx, op, rt, rs, imm);
        }
        break;
    case OPC_ADDIU:
         gen_arith_imm(ctx, op, rt, rs, imm);
         break;
    case OPC_SLTI: /* Set on less than with immediate opcode */
    case OPC_SLTIU:
         gen_slt_imm(ctx, op, rt, rs, imm);
         break;
    case OPC_ANDI: /* Arithmetic with immediate opcode */
    case OPC_LUI: /* OPC_AUI */
    case OPC_ORI:
    case OPC_XORI:
         gen_logic_imm(ctx, op, rt, rs, imm);
         break;
    case OPC_J ... OPC_JAL: /* Jump */
         offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 2;
         gen_compute_branch(ctx, op, 4, rs, rt, offset, 4);
         break;
    /* Branch */
    case OPC_BLEZC: /* OPC_BGEZC, OPC_BGEC, OPC_BLEZL */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            if (rt == 0) {
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
            /* OPC_BLEZC, OPC_BGEZC, OPC_BGEC */
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        } else {
            /* OPC_BLEZL */
            gen_compute_branch(ctx, op, 4, rs, rt, imm << 2, 4);
        }
        break;
    case OPC_BGTZC: /* OPC_BLTZC, OPC_BLTC, OPC_BGTZL */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            if (rt == 0) {
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
            /* OPC_BGTZC, OPC_BLTZC, OPC_BLTC */
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        } else {
            /* OPC_BGTZL */
            gen_compute_branch(ctx, op, 4, rs, rt, imm << 2, 4);
        }
        break;
    case OPC_BLEZALC: /* OPC_BGEZALC, OPC_BGEUC, OPC_BLEZ */
        if (rt == 0) {
            /* OPC_BLEZ */
            gen_compute_branch(ctx, op, 4, rs, rt, imm << 2, 4);
        } else {
            check_insn(ctx, ISA_MIPS32R6);
            /* OPC_BLEZALC, OPC_BGEZALC, OPC_BGEUC */
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        }
        break;
    case OPC_BGTZALC: /* OPC_BLTZALC, OPC_BLTUC, OPC_BGTZ */
        if (rt == 0) {
            /* OPC_BGTZ */
            gen_compute_branch(ctx, op, 4, rs, rt, imm << 2, 4);
        } else {
            check_insn(ctx, ISA_MIPS32R6);
            /* OPC_BGTZALC, OPC_BLTZALC, OPC_BLTUC */
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        }
        break;
    case OPC_BEQL:
    case OPC_BNEL:
        check_insn(ctx, ISA_MIPS2);
         check_insn_opc_removed(ctx, ISA_MIPS32R6);
        /* Fallthrough */
    case OPC_BEQ:
    case OPC_BNE:
         gen_compute_branch(ctx, op, 4, rs, rt, imm << 2, 4);
         break;
    case OPC_LL: /* Load and stores */
        check_insn(ctx, ISA_MIPS2);
        /* Fallthrough */
    case OPC_LWL:
    case OPC_LWR:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
         /* Fallthrough */
    case OPC_LB ... OPC_LH:
    case OPC_LW ... OPC_LHU:
         gen_ld(ctx, op, rt, rs, imm);
         break;
    case OPC_SWL:
    case OPC_SWR:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        /* fall through */
    case OPC_SB ... OPC_SH:
    case OPC_SW:
         gen_st(ctx, op, rt, rs, imm);
         break;
    case OPC_SC:
        check_insn(ctx, ISA_MIPS2);
         check_insn_opc_removed(ctx, ISA_MIPS32R6);
         gen_st_cond(ctx, op, rt, rs, imm);
         break;
    case OPC_CACHE:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        check_cp0_enabled(ctx);
        check_insn(ctx, ISA_MIPS3 | ISA_MIPS32);
        if (ctx->hflags & MIPS_HFLAG_ITC_CACHE) {
            gen_cache_operation(ctx, rt, rs, imm);
        }
        /* Treat as NOP. */
        break;
    case OPC_PREF:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        check_insn(ctx, ISA_MIPS4 | ISA_MIPS32);
        /* Treat as NOP. */
        break;

    /* Floating point (COP1). */
    case OPC_LWC1:
    case OPC_LDC1:
    case OPC_SWC1:
    case OPC_SDC1:
        gen_cop1_ldst(ctx, op, rt, rs, imm);
        break;

    case OPC_CP1:
        op1 = MASK_CP1(ctx->opcode);

        switch (op1) {
        case OPC_MFHC1:
        case OPC_MTHC1:
            check_cp1_enabled(ctx);
            check_insn(ctx, ISA_MIPS32R2);
        case OPC_MFC1:
        case OPC_CFC1:
        case OPC_MTC1:
        case OPC_CTC1:
            check_cp1_enabled(ctx);
            gen_cp1(ctx, op1, rt, rd);
            break;
#if defined(TARGET_MIPS64)
        case OPC_DMFC1:
        case OPC_DMTC1:
            check_cp1_enabled(ctx);
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_cp1(ctx, op1, rt, rd);
            break;
#endif
        case OPC_BC1EQZ: /* OPC_BC1ANY2 */
            check_cp1_enabled(ctx);
            if (ctx->insn_flags & ISA_MIPS32R6) {
                /* OPC_BC1EQZ */
                gen_compute_branch1_r6(ctx, MASK_CP1(ctx->opcode),
                                       rt, imm << 2, 4);
            } else {
                /* OPC_BC1ANY2 */
                check_cop1x(ctx);
                check_insn(ctx, ASE_MIPS3D);
                gen_compute_branch1(ctx, MASK_BC1(ctx->opcode),
                                    (rt >> 2) & 0x7, imm << 2);
            }
            break;
        case OPC_BC1NEZ:
            check_cp1_enabled(ctx);
            check_insn(ctx, ISA_MIPS32R6);
            gen_compute_branch1_r6(ctx, MASK_CP1(ctx->opcode),
                                   rt, imm << 2, 4);
            break;
        case OPC_BC1ANY4:
            check_cp1_enabled(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            check_cop1x(ctx);
            check_insn(ctx, ASE_MIPS3D);
            /* fall through */
        case OPC_BC1:
            check_cp1_enabled(ctx);
            check_insn_opc_removed(ctx, ISA_MIPS32R6);
            gen_compute_branch1(ctx, MASK_BC1(ctx->opcode),
                                (rt >> 2) & 0x7, imm << 2);
            break;
        case OPC_PS_FMT:
            check_ps(ctx);
            /* fall through */
        case OPC_S_FMT:
        case OPC_D_FMT:
            check_cp1_enabled(ctx);
            gen_farith(ctx, ctx->opcode & FOP(0x3f, 0x1f), rt, rd, sa,
                       (imm >> 8) & 0x7);
            break;
        case OPC_W_FMT:
        case OPC_L_FMT:
        {
            int r6_op = ctx->opcode & FOP(0x3f, 0x1f);
            check_cp1_enabled(ctx);
            if (ctx->insn_flags & ISA_MIPS32R6) {
                switch (r6_op) {
                case R6_OPC_CMP_AF_S:
                case R6_OPC_CMP_UN_S:
                case R6_OPC_CMP_EQ_S:
                case R6_OPC_CMP_UEQ_S:
                case R6_OPC_CMP_LT_S:
                case R6_OPC_CMP_ULT_S:
                case R6_OPC_CMP_LE_S:
                case R6_OPC_CMP_ULE_S:
                case R6_OPC_CMP_SAF_S:
                case R6_OPC_CMP_SUN_S:
                case R6_OPC_CMP_SEQ_S:
                case R6_OPC_CMP_SEUQ_S:
                case R6_OPC_CMP_SLT_S:
                case R6_OPC_CMP_SULT_S:
                case R6_OPC_CMP_SLE_S:
                case R6_OPC_CMP_SULE_S:
                case R6_OPC_CMP_OR_S:
                case R6_OPC_CMP_UNE_S:
                case R6_OPC_CMP_NE_S:
                case R6_OPC_CMP_SOR_S:
                case R6_OPC_CMP_SUNE_S:
                case R6_OPC_CMP_SNE_S:
                    gen_r6_cmp_s(ctx, ctx->opcode & 0x1f, rt, rd, sa);
                    break;
                case R6_OPC_CMP_AF_D:
                case R6_OPC_CMP_UN_D:
                case R6_OPC_CMP_EQ_D:
                case R6_OPC_CMP_UEQ_D:
                case R6_OPC_CMP_LT_D:
                case R6_OPC_CMP_ULT_D:
                case R6_OPC_CMP_LE_D:
                case R6_OPC_CMP_ULE_D:
                case R6_OPC_CMP_SAF_D:
                case R6_OPC_CMP_SUN_D:
                case R6_OPC_CMP_SEQ_D:
                case R6_OPC_CMP_SEUQ_D:
                case R6_OPC_CMP_SLT_D:
                case R6_OPC_CMP_SULT_D:
                case R6_OPC_CMP_SLE_D:
                case R6_OPC_CMP_SULE_D:
                case R6_OPC_CMP_OR_D:
                case R6_OPC_CMP_UNE_D:
                case R6_OPC_CMP_NE_D:
                case R6_OPC_CMP_SOR_D:
                case R6_OPC_CMP_SUNE_D:
                case R6_OPC_CMP_SNE_D:
                    gen_r6_cmp_d(ctx, ctx->opcode & 0x1f, rt, rd, sa);
                    break;
                default:
                    gen_farith(ctx, ctx->opcode & FOP(0x3f, 0x1f),
                               rt, rd, sa, (imm >> 8) & 0x7);

                    break;
                }
            } else {
                gen_farith(ctx, ctx->opcode & FOP(0x3f, 0x1f), rt, rd, sa,
                           (imm >> 8) & 0x7);
            }
            break;
        }
        case OPC_BZ_V:
        case OPC_BNZ_V:
        case OPC_BZ_B:
        case OPC_BZ_H:
        case OPC_BZ_W:
        case OPC_BZ_D:
        case OPC_BNZ_B:
        case OPC_BNZ_H:
        case OPC_BNZ_W:
        case OPC_BNZ_D:
            check_insn(ctx, ASE_MSA);
            gen_msa_branch(env, ctx, op1);
            break;
        default:
            MIPS_INVAL("cp1");
            generate_exception_end(ctx, EXCP_RI);
            break;
        }
        break;

    /* Compact branches [R6] and COP2 [non-R6] */
    case OPC_BC: /* OPC_LWC2 */
    case OPC_BALC: /* OPC_SWC2 */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_BC, OPC_BALC */
            gen_compute_compact_branch(ctx, op, 0, 0,
                                       sextract32(ctx->opcode << 2, 0, 28));
        } else {
            /* OPC_LWC2, OPC_SWC2 */
            /* COP2: Not implemented. */
            generate_exception_err(ctx, EXCP_CpU, 2);
        }
        break;
    case OPC_BEQZC: /* OPC_JIC, OPC_LDC2 */
    case OPC_BNEZC: /* OPC_JIALC, OPC_SDC2 */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            if (rs != 0) {
                /* OPC_BEQZC, OPC_BNEZC */
                gen_compute_compact_branch(ctx, op, rs, 0,
                                           sextract32(ctx->opcode << 2, 0, 23));
            } else {
                /* OPC_JIC, OPC_JIALC */
                gen_compute_compact_branch(ctx, op, 0, rt, imm);
            }
        } else {
            /* OPC_LWC2, OPC_SWC2 */
            /* COP2: Not implemented. */
            generate_exception_err(ctx, EXCP_CpU, 2);
        }
        break;
    case OPC_CP2:
        check_insn(ctx, INSN_LOONGSON2F);
        /* Note that these instructions use different fields.  */
        gen_loongson_multimedia(ctx, sa, rd, rt);
        break;

    case OPC_CP3:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        if (ctx->CP0_Config1 & (1 << CP0C1_FP)) {
            check_cp1_enabled(ctx);
            op1 = MASK_CP3(ctx->opcode);
            switch (op1) {
            case OPC_LUXC1:
            case OPC_SUXC1:
                check_insn(ctx, ISA_MIPS5 | ISA_MIPS32R2);
                /* Fallthrough */
            case OPC_LWXC1:
            case OPC_LDXC1:
            case OPC_SWXC1:
            case OPC_SDXC1:
                check_insn(ctx, ISA_MIPS4 | ISA_MIPS32R2);
                gen_flt3_ldst(ctx, op1, sa, rd, rs, rt);
                break;
            case OPC_PREFX:
                check_insn(ctx, ISA_MIPS4 | ISA_MIPS32R2);
                /* Treat as NOP. */
                break;
            case OPC_ALNV_PS:
                check_insn(ctx, ISA_MIPS5 | ISA_MIPS32R2);
                /* Fallthrough */
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
                check_insn(ctx, ISA_MIPS4 | ISA_MIPS32R2);
                gen_flt3_arith(ctx, op1, sa, rs, rd, rt);
                break;
            default:
                MIPS_INVAL("cp3");
                generate_exception_end(ctx, EXCP_RI);
                break;
            }
        } else {
            generate_exception_err(ctx, EXCP_CpU, 1);
        }
        break;

#if defined(TARGET_MIPS64)
    /* MIPS64 opcodes */
    case OPC_LDL ... OPC_LDR:
    case OPC_LLD:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        /* fall through */
    case OPC_LWU:
    case OPC_LD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_ld(ctx, op, rt, rs, imm);
        break;
    case OPC_SDL ... OPC_SDR:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        /* fall through */
    case OPC_SD:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_st(ctx, op, rt, rs, imm);
        break;
    case OPC_SCD:
        check_insn_opc_removed(ctx, ISA_MIPS32R6);
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_st_cond(ctx, op, rt, rs, imm);
        break;
    case OPC_BNVC: /* OPC_BNEZALC, OPC_BNEC, OPC_DADDI */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            /* OPC_BNVC, OPC_BNEZALC, OPC_BNEC */
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        } else {
            /* OPC_DADDI */
            check_insn(ctx, ISA_MIPS3);
            check_mips_64(ctx);
            gen_arith_imm(ctx, op, rt, rs, imm);
        }
        break;
    case OPC_DADDIU:
        check_insn(ctx, ISA_MIPS3);
        check_mips_64(ctx);
        gen_arith_imm(ctx, op, rt, rs, imm);
        break;
#else
    case OPC_BNVC: /* OPC_BNEZALC, OPC_BNEC */
        if (ctx->insn_flags & ISA_MIPS32R6) {
            gen_compute_compact_branch(ctx, op, rs, rt, imm << 2);
        } else {
            MIPS_INVAL("major opcode");
            generate_exception_end(ctx, EXCP_RI);
        }
        break;
#endif
    case OPC_DAUI: /* OPC_JALX */
        if (ctx->insn_flags & ISA_MIPS32R6) {
#if defined(TARGET_MIPS64)
            /* OPC_DAUI */
            check_mips_64(ctx);
            if (rs == 0) {
                generate_exception(ctx, EXCP_RI);
            } else if (rt != 0) {
                TCGv t0 = tcg_temp_new();
                gen_load_gpr(t0, rs);
                tcg_gen_addi_tl(cpu_gpr[rt], t0, imm << 16);
                tcg_temp_free(t0);
            }
#else
            generate_exception_end(ctx, EXCP_RI);
            MIPS_INVAL("major opcode");
#endif
        } else {
            /* OPC_JALX */
            check_insn(ctx, ASE_MIPS16 | ASE_MICROMIPS);
            offset = (int32_t)(ctx->opcode & 0x3FFFFFF) << 2;
            gen_compute_branch(ctx, op, 4, rs, rt, offset, 4);
        }
        break;
    case OPC_MSA: /* OPC_MDMX */
        /* MDMX: Not implemented. */
        gen_msa(env, ctx);
        break;
    case OPC_PCREL:
        check_insn(ctx, ISA_MIPS32R6);
        gen_pcrel(ctx, ctx->opcode, ctx->pc, rs);
        break;
    default:            /* Invalid */
        MIPS_INVAL("major opcode");
        generate_exception_end(ctx, EXCP_RI);
        break;
    }
}

void gen_intermediate_code(CPUState *cs, struct TranslationBlock *tb)
{
    CPUMIPSState *env = cs->env_ptr;
    DisasContext ctx;
    target_ulong pc_start;
    target_ulong next_page_start;
    int num_insns;
    int max_insns;
    int insn_bytes;
    int is_slot;

    pc_start = tb->pc;
    next_page_start = (pc_start & TARGET_PAGE_MASK) + TARGET_PAGE_SIZE;
    ctx.pc = pc_start;
    ctx.saved_pc = -1;
    ctx.singlestep_enabled = cs->singlestep_enabled;
    ctx.insn_flags = env->insn_flags;
    ctx.CP0_Config1 = env->CP0_Config1;
    ctx.tb = tb;
    ctx.bstate = BS_NONE;
    ctx.btarget = 0;
    ctx.kscrexist = (env->CP0_Config4 >> CP0C4_KScrExist) & 0xff;
    ctx.rxi = (env->CP0_Config3 >> CP0C3_RXI) & 1;
    ctx.ie = (env->CP0_Config4 >> CP0C4_IE) & 3;
    ctx.bi = (env->CP0_Config3 >> CP0C3_BI) & 1;
    ctx.bp = (env->CP0_Config3 >> CP0C3_BP) & 1;
    ctx.PAMask = env->PAMask;
    ctx.mvh = (env->CP0_Config5 >> CP0C5_MVH) & 1;
    ctx.eva = (env->CP0_Config5 >> CP0C5_EVA) & 1;
    ctx.sc = (env->CP0_Config3 >> CP0C3_SC) & 1;
    ctx.CP0_LLAddr_shift = env->CP0_LLAddr_shift;
    ctx.cmgcr = (env->CP0_Config3 >> CP0C3_CMGCR) & 1;
    /* Restore delay slot state from the tb context.  */
    ctx.hflags = (uint32_t)tb->flags; /* FIXME: maybe use 64 bits here? */
    ctx.ulri = (env->CP0_Config3 >> CP0C3_ULRI) & 1;
    ctx.ps = ((env->active_fpu.fcr0 >> FCR0_PS) & 1) ||
             (env->insn_flags & (INSN_LOONGSON2E | INSN_LOONGSON2F));
    ctx.vp = (env->CP0_Config5 >> CP0C5_VP) & 1;
    ctx.mrp = (env->CP0_Config5 >> CP0C5_MRP) & 1;
    ctx.nan2008 = (env->active_fpu.fcr31 >> FCR31_NAN2008) & 1;
    ctx.abs2008 = (env->active_fpu.fcr31 >> FCR31_ABS2008) & 1;
    restore_cpu_state(env, &ctx);
#ifdef CONFIG_USER_ONLY
        ctx.mem_idx = MIPS_HFLAG_UM;
#else
        ctx.mem_idx = hflags_mmu_index(ctx.hflags);
#endif
    ctx.default_tcg_memop_mask = (ctx.insn_flags & ISA_MIPS32R6) ?
                                 MO_UNALN : MO_ALIGN;
    num_insns = 0;
    max_insns = tb->cflags & CF_COUNT_MASK;
    if (max_insns == 0) {
        max_insns = CF_COUNT_MASK;
    }
    if (max_insns > TCG_MAX_INSNS) {
        max_insns = TCG_MAX_INSNS;
    }

    LOG_DISAS("\ntb %p idx %d hflags %04x\n", tb, ctx.mem_idx, ctx.hflags);
    gen_tb_start(tb);
    while (ctx.bstate == BS_NONE) {
        tcg_gen_insn_start(ctx.pc, ctx.hflags & MIPS_HFLAG_BMASK, ctx.btarget);
        num_insns++;

        if (unlikely(cpu_breakpoint_test(cs, ctx.pc, BP_ANY))) {
            save_cpu_state(&ctx, 1);
            ctx.bstate = BS_BRANCH;
            gen_helper_raise_exception_debug(cpu_env);
            /* The address covered by the breakpoint must be included in
               [tb->pc, tb->pc + tb->size) in order to for it to be
               properly cleared -- thus we increment the PC here so that
               the logic setting tb->size below does the right thing.  */
            ctx.pc += 4;
            goto done_generating;
        }

        if (num_insns == max_insns && (tb->cflags & CF_LAST_IO)) {
            gen_io_start();
        }

        is_slot = ctx.hflags & MIPS_HFLAG_BMASK;
        if (!(ctx.hflags & MIPS_HFLAG_M16)) {
            ctx.opcode = cpu_ldl_code(env, ctx.pc);
            insn_bytes = 4;
            decode_opc(env, &ctx);
        } else if (ctx.insn_flags & ASE_MICROMIPS) {
            ctx.opcode = cpu_lduw_code(env, ctx.pc);
            insn_bytes = decode_micromips_opc(env, &ctx);
        } else if (ctx.insn_flags & ASE_MIPS16) {
            ctx.opcode = cpu_lduw_code(env, ctx.pc);
            insn_bytes = decode_mips16_opc(env, &ctx);
        } else {
            generate_exception_end(&ctx, EXCP_RI);
            break;
        }

        if (ctx.hflags & MIPS_HFLAG_BMASK) {
            if (!(ctx.hflags & (MIPS_HFLAG_BDS16 | MIPS_HFLAG_BDS32 |
                                MIPS_HFLAG_FBNSLOT))) {
                /* force to generate branch as there is neither delay nor
                   forbidden slot */
                is_slot = 1;
            }
            if ((ctx.hflags & MIPS_HFLAG_M16) &&
                (ctx.hflags & MIPS_HFLAG_FBNSLOT)) {
                /* Force to generate branch as microMIPS R6 doesn't restrict
                   branches in the forbidden slot. */
                is_slot = 1;
            }
        }
        if (is_slot) {
            gen_branch(&ctx, insn_bytes);
        }
        ctx.pc += insn_bytes;

        /* Execute a branch and its delay slot as a single instruction.
           This is what GDB expects and is consistent with what the
           hardware does (e.g. if a delay slot instruction faults, the
           reported PC is the PC of the branch).  */
        if (cs->singlestep_enabled && (ctx.hflags & MIPS_HFLAG_BMASK) == 0) {
            break;
        }

        if (ctx.pc >= next_page_start) {
            break;
        }

        if (tcg_op_buf_full()) {
            break;
        }

        if (num_insns >= max_insns)
            break;

        if (singlestep)
            break;
    }
    if (tb->cflags & CF_LAST_IO) {
        gen_io_end();
    }
    if (cs->singlestep_enabled && ctx.bstate != BS_BRANCH) {
        save_cpu_state(&ctx, ctx.bstate != BS_EXCP);
        gen_helper_raise_exception_debug(cpu_env);
    } else {
        switch (ctx.bstate) {
        case BS_STOP:
            gen_goto_tb(&ctx, 0, ctx.pc);
            break;
        case BS_NONE:
            save_cpu_state(&ctx, 0);
            gen_goto_tb(&ctx, 0, ctx.pc);
            break;
        case BS_EXCP:
            tcg_gen_exit_tb(0);
            break;
        case BS_BRANCH:
        default:
            break;
        }
    }
done_generating:
    gen_tb_end(tb, num_insns);

    tb->size = ctx.pc - pc_start;
    tb->icount = num_insns;

#ifdef DEBUG_DISAS
    LOG_DISAS("\n");
    if (qemu_loglevel_mask(CPU_LOG_TB_IN_ASM)
        && qemu_log_in_addr_range(pc_start)) {
        qemu_log_lock();
        qemu_log("IN: %s\n", lookup_symbol(pc_start));
        log_target_disas(cs, pc_start, ctx.pc - pc_start, 0);
        qemu_log("\n");
        qemu_log_unlock();
    }
#endif
}

static void fpu_dump_state(CPUMIPSState *env, FILE *f, fprintf_function fpu_fprintf,
                           int flags)
{
    int i;
    int is_fpu64 = !!(env->hflags & MIPS_HFLAG_F64);

#define printfpr(fp)                                                    \
    do {                                                                \
        if (is_fpu64)                                                   \
            fpu_fprintf(f, "w:%08x d:%016" PRIx64                       \
                        " fd:%13g fs:%13g psu: %13g\n",                 \
                        (fp)->w[FP_ENDIAN_IDX], (fp)->d,                \
                        (double)(fp)->fd,                               \
                        (double)(fp)->fs[FP_ENDIAN_IDX],                \
                        (double)(fp)->fs[!FP_ENDIAN_IDX]);              \
        else {                                                          \
            fpr_t tmp;                                                  \
            tmp.w[FP_ENDIAN_IDX] = (fp)->w[FP_ENDIAN_IDX];              \
            tmp.w[!FP_ENDIAN_IDX] = ((fp) + 1)->w[FP_ENDIAN_IDX];       \
            fpu_fprintf(f, "w:%08x d:%016" PRIx64                       \
                        " fd:%13g fs:%13g psu:%13g\n",                  \
                        tmp.w[FP_ENDIAN_IDX], tmp.d,                    \
                        (double)tmp.fd,                                 \
                        (double)tmp.fs[FP_ENDIAN_IDX],                  \
                        (double)tmp.fs[!FP_ENDIAN_IDX]);                \
        }                                                               \
    } while(0)


    fpu_fprintf(f, "CP1 FCR0 0x%08x  FCR31 0x%08x  SR.FR %d  fp_status 0x%02x\n",
                env->active_fpu.fcr0, env->active_fpu.fcr31, is_fpu64,
                get_float_exception_flags(&env->active_fpu.fp_status));
    for (i = 0; i < 32; (is_fpu64) ? i++ : (i += 2)) {
        fpu_fprintf(f, "%3s: ", fregnames[i]);
        printfpr(&env->active_fpu.fpr[i]);
    }

#undef printfpr
}

void mips_cpu_dump_state(CPUState *cs, FILE *f, fprintf_function cpu_fprintf,
                         int flags)
{
    MIPSCPU *cpu = MIPS_CPU(cs);
    CPUMIPSState *env = &cpu->env;
    int i;

    cpu_fprintf(f, "pc=0x" TARGET_FMT_lx " HI=0x" TARGET_FMT_lx
                " LO=0x" TARGET_FMT_lx " ds %04x "
                TARGET_FMT_lx " " TARGET_FMT_ld "\n",
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
    cpu_fprintf(f, "    Config0 0x%08x Config1 0x%08x LLAddr 0x%016"
                PRIx64 "\n",
                env->CP0_Config0, env->CP0_Config1, env->lladdr);
    cpu_fprintf(f, "    Config2 0x%08x Config3 0x%08x\n",
                env->CP0_Config2, env->CP0_Config3);
    cpu_fprintf(f, "    Config4 0x%08x Config5 0x%08x\n",
                env->CP0_Config4, env->CP0_Config5);
    if (env->hflags & MIPS_HFLAG_FPU)
        fpu_dump_state(env, f, cpu_fprintf, flags);
}

void mips_tcg_init(void)
{
    int i;
    static int inited;

    /* Initialize various static tables. */
    if (inited)
        return;

    cpu_env = tcg_global_reg_new_ptr(TCG_AREG0, "env");
    tcg_ctx.tcg_env = cpu_env;

    TCGV_UNUSED(cpu_gpr[0]);
    for (i = 1; i < 32; i++)
        cpu_gpr[i] = tcg_global_mem_new(cpu_env,
                                        offsetof(CPUMIPSState, active_tc.gpr[i]),
                                        regnames[i]);

    for (i = 0; i < 32; i++) {
        int off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[0]);
        msa_wr_d[i * 2] =
                tcg_global_mem_new_i64(cpu_env, off, msaregnames[i * 2]);
        /* The scalar floating-point unit (FPU) registers are mapped on
         * the MSA vector registers. */
        fpu_f64[i] = msa_wr_d[i * 2];
        off = offsetof(CPUMIPSState, active_fpu.fpr[i].wr.d[1]);
        msa_wr_d[i * 2 + 1] =
                tcg_global_mem_new_i64(cpu_env, off, msaregnames[i * 2 + 1]);
    }

    cpu_PC = tcg_global_mem_new(cpu_env,
                                offsetof(CPUMIPSState, active_tc.PC), "PC");
    for (i = 0; i < MIPS_DSP_ACC; i++) {
        cpu_HI[i] = tcg_global_mem_new(cpu_env,
                                       offsetof(CPUMIPSState, active_tc.HI[i]),
                                       regnames_HI[i]);
        cpu_LO[i] = tcg_global_mem_new(cpu_env,
                                       offsetof(CPUMIPSState, active_tc.LO[i]),
                                       regnames_LO[i]);
    }
    cpu_dspctrl = tcg_global_mem_new(cpu_env,
                                     offsetof(CPUMIPSState, active_tc.DSPControl),
                                     "DSPControl");
    bcond = tcg_global_mem_new(cpu_env,
                               offsetof(CPUMIPSState, bcond), "bcond");
    btarget = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUMIPSState, btarget), "btarget");
    hflags = tcg_global_mem_new_i32(cpu_env,
                                    offsetof(CPUMIPSState, hflags), "hflags");

    fpu_fcr0 = tcg_global_mem_new_i32(cpu_env,
                                      offsetof(CPUMIPSState, active_fpu.fcr0),
                                      "fcr0");
    fpu_fcr31 = tcg_global_mem_new_i32(cpu_env,
                                       offsetof(CPUMIPSState, active_fpu.fcr31),
                                       "fcr31");

    inited = 1;
}

#include "translate_init.c"

MIPSCPU *cpu_mips_init(const char *cpu_model)
{
    MIPSCPU *cpu;
    CPUMIPSState *env;
    const mips_def_t *def;

    def = cpu_mips_find_by_name(cpu_model);
    if (!def)
        return NULL;
    cpu = MIPS_CPU(object_new(TYPE_MIPS_CPU));
    env = &cpu->env;
    env->cpu_model = def;
    env->exception_base = (int32_t)0xBFC00000;

#ifndef CONFIG_USER_ONLY
    mmu_init(env, def);
#endif
    fpu_init(env, def);
    mvp_init(env, def);

    object_property_set_bool(OBJECT(cpu), true, "realized", NULL);

    return cpu;
}

bool cpu_supports_cps_smp(const char *cpu_model)
{
    const mips_def_t *def = cpu_mips_find_by_name(cpu_model);
    if (!def) {
        return false;
    }

    return (def->CP0_Config3 & (1 << CP0C3_CMGCR)) != 0;
}

bool cpu_supports_isa(const char *cpu_model, unsigned int isa)
{
    const mips_def_t *def = cpu_mips_find_by_name(cpu_model);
    if (!def) {
        return false;
    }

    return (def->insn_flags & isa) != 0;
}

void cpu_set_exception_base(int vp_index, target_ulong address)
{
    MIPSCPU *vp = MIPS_CPU(qemu_get_cpu(vp_index));
    vp->env.exception_base = address;
}

void cpu_state_reset(CPUMIPSState *env)
{
    MIPSCPU *cpu = mips_env_get_cpu(env);
    CPUState *cs = CPU(cpu);

    /* Reset registers to their default values */
    env->CP0_PRid = env->cpu_model->CP0_PRid;
    env->CP0_Config0 = env->cpu_model->CP0_Config0;
#ifdef TARGET_WORDS_BIGENDIAN
    env->CP0_Config0 |= (1 << CP0C0_BE);
#endif
    env->CP0_Config1 = env->cpu_model->CP0_Config1;
    env->CP0_Config2 = env->cpu_model->CP0_Config2;
    env->CP0_Config3 = env->cpu_model->CP0_Config3;
    env->CP0_Config4 = env->cpu_model->CP0_Config4;
    env->CP0_Config4_rw_bitmask = env->cpu_model->CP0_Config4_rw_bitmask;
    env->CP0_Config5 = env->cpu_model->CP0_Config5;
    env->CP0_Config5_rw_bitmask = env->cpu_model->CP0_Config5_rw_bitmask;
    env->CP0_Config6 = env->cpu_model->CP0_Config6;
    env->CP0_Config7 = env->cpu_model->CP0_Config7;
    env->CP0_LLAddr_rw_bitmask = env->cpu_model->CP0_LLAddr_rw_bitmask
                                 << env->cpu_model->CP0_LLAddr_shift;
    env->CP0_LLAddr_shift = env->cpu_model->CP0_LLAddr_shift;
    env->SYNCI_Step = env->cpu_model->SYNCI_Step;
    env->CCRes = env->cpu_model->CCRes;
    env->CP0_Status_rw_bitmask = env->cpu_model->CP0_Status_rw_bitmask;
    env->CP0_TCStatus_rw_bitmask = env->cpu_model->CP0_TCStatus_rw_bitmask;
    env->CP0_SRSCtl = env->cpu_model->CP0_SRSCtl;
    env->current_tc = 0;
    env->SEGBITS = env->cpu_model->SEGBITS;
    env->SEGMask = (target_ulong)((1ULL << env->cpu_model->SEGBITS) - 1);
#if defined(TARGET_MIPS64)
    if (env->cpu_model->insn_flags & ISA_MIPS3) {
        env->SEGMask |= 3ULL << 62;
    }
#endif
    env->PABITS = env->cpu_model->PABITS;
    env->CP0_SRSConf0_rw_bitmask = env->cpu_model->CP0_SRSConf0_rw_bitmask;
    env->CP0_SRSConf0 = env->cpu_model->CP0_SRSConf0;
    env->CP0_SRSConf1_rw_bitmask = env->cpu_model->CP0_SRSConf1_rw_bitmask;
    env->CP0_SRSConf1 = env->cpu_model->CP0_SRSConf1;
    env->CP0_SRSConf2_rw_bitmask = env->cpu_model->CP0_SRSConf2_rw_bitmask;
    env->CP0_SRSConf2 = env->cpu_model->CP0_SRSConf2;
    env->CP0_SRSConf3_rw_bitmask = env->cpu_model->CP0_SRSConf3_rw_bitmask;
    env->CP0_SRSConf3 = env->cpu_model->CP0_SRSConf3;
    env->CP0_SRSConf4_rw_bitmask = env->cpu_model->CP0_SRSConf4_rw_bitmask;
    env->CP0_SRSConf4 = env->cpu_model->CP0_SRSConf4;
    env->CP0_PageGrain_rw_bitmask = env->cpu_model->CP0_PageGrain_rw_bitmask;
    env->CP0_PageGrain = env->cpu_model->CP0_PageGrain;
    env->CP0_EBaseWG_rw_bitmask = env->cpu_model->CP0_EBaseWG_rw_bitmask;
    env->active_fpu.fcr0 = env->cpu_model->CP1_fcr0;
    env->active_fpu.fcr31_rw_bitmask = env->cpu_model->CP1_fcr31_rw_bitmask;
    env->active_fpu.fcr31 = env->cpu_model->CP1_fcr31;
    env->msair = env->cpu_model->MSAIR;
    env->insn_flags = env->cpu_model->insn_flags;

#if defined(CONFIG_USER_ONLY)
    env->CP0_Status = (MIPS_HFLAG_UM << CP0St_KSU);
# ifdef TARGET_MIPS64
    /* Enable 64-bit register mode.  */
    env->CP0_Status |= (1 << CP0St_PX);
# endif
# ifdef TARGET_ABI_MIPSN64
    /* Enable 64-bit address mode.  */
    env->CP0_Status |= (1 << CP0St_UX);
# endif
    /* Enable access to the CPUNum, SYNCI_Step, CC, and CCRes RDHWR
       hardware registers.  */
    env->CP0_HWREna |= 0x0000000F;
    if (env->CP0_Config1 & (1 << CP0C1_FP)) {
        env->CP0_Status |= (1 << CP0St_CU1);
    }
    if (env->CP0_Config3 & (1 << CP0C3_DSPP)) {
        env->CP0_Status |= (1 << CP0St_MX);
    }
# if defined(TARGET_MIPS64)
    /* For MIPS64, init FR bit to 1 if FPU unit is there and bit is writable. */
    if ((env->CP0_Config1 & (1 << CP0C1_FP)) &&
        (env->CP0_Status_rw_bitmask & (1 << CP0St_FR))) {
        env->CP0_Status |= (1 << CP0St_FR);
    }
# endif
#else
    if (env->hflags & MIPS_HFLAG_BMASK) {
        /* If the exception was raised from a delay slot,
           come back to the jump.  */
        env->CP0_ErrorEPC = (env->active_tc.PC
                             - (env->hflags & MIPS_HFLAG_B16 ? 2 : 4));
    } else {
        env->CP0_ErrorEPC = env->active_tc.PC;
    }
    env->active_tc.PC = env->exception_base;
    env->CP0_Random = env->tlb->nb_tlb - 1;
    env->tlb->tlb_in_use = env->tlb->nb_tlb;
    env->CP0_Wired = 0;
    env->CP0_GlobalNumber = (cs->cpu_index & 0xFF) << CP0GN_VPId;
    env->CP0_EBase = (cs->cpu_index & 0x3FF);
    if (mips_um_ksegs_enabled()) {
        env->CP0_EBase |= 0x40000000;
    } else {
        env->CP0_EBase |= (int32_t)0x80000000;
    }
    if (env->CP0_Config3 & (1 << CP0C3_CMGCR)) {
        env->CP0_CMGCRBase = 0x1fbf8000 >> 4;
    }
    env->CP0_EntryHi_ASID_mask = (env->CP0_Config4 & (1 << CP0C4_AE)) ?
                                 0x3ff : 0xff;
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

    cpu_mips_store_count(env, 1);

    if (env->CP0_Config3 & (1 << CP0C3_MT)) {
        int i;

        /* Only TC0 on VPE 0 starts as active.  */
        for (i = 0; i < ARRAY_SIZE(env->tcs); i++) {
            env->tcs[i].CP0_TCBind = cs->cpu_index << CP0TCBd_CurVPE;
            env->tcs[i].CP0_TCHalt = 1;
        }
        env->active_tc.CP0_TCHalt = 1;
        cs->halted = 1;

        if (cs->cpu_index == 0) {
            /* VPE0 starts up enabled.  */
            env->mvp->CP0_MVPControl |= (1 << CP0MVPCo_EVP);
            env->CP0_VPEConf0 |= (1 << CP0VPEC0_MVP) | (1 << CP0VPEC0_VPA);

            /* TC0 starts up unhalted.  */
            cs->halted = 0;
            env->active_tc.CP0_TCHalt = 0;
            env->tcs[0].CP0_TCHalt = 0;
            /* With thread 0 active.  */
            env->active_tc.CP0_TCStatus = (1 << CP0TCSt_A);
            env->tcs[0].CP0_TCStatus = (1 << CP0TCSt_A);
        }
    }

    /*
     * Configure default legacy segmentation control. We use this regardless of
     * whether segmentation control is presented to the guest.
     */
    /* KSeg3 (seg0 0xE0000000..0xFFFFFFFF) */
    env->CP0_SegCtl0 =   (CP0SC_AM_MK << CP0SC_AM);
    /* KSeg2 (seg1 0xC0000000..0xDFFFFFFF) */
    env->CP0_SegCtl0 |= ((CP0SC_AM_MSK << CP0SC_AM)) << 16;
    /* KSeg1 (seg2 0xA0000000..0x9FFFFFFF) */
    env->CP0_SegCtl1 =   (0 << CP0SC_PA) | (CP0SC_AM_UK << CP0SC_AM) |
                         (2 << CP0SC_C);
    /* KSeg0 (seg3 0x80000000..0x9FFFFFFF) */
    env->CP0_SegCtl1 |= ((0 << CP0SC_PA) | (CP0SC_AM_UK << CP0SC_AM) |
                         (3 << CP0SC_C)) << 16;
    /* USeg (seg4 0x40000000..0x7FFFFFFF) */
    env->CP0_SegCtl2 =   (2 << CP0SC_PA) | (CP0SC_AM_MUSK << CP0SC_AM) |
                         (1 << CP0SC_EU) | (2 << CP0SC_C);
    /* USeg (seg5 0x00000000..0x3FFFFFFF) */
    env->CP0_SegCtl2 |= ((0 << CP0SC_PA) | (CP0SC_AM_MUSK << CP0SC_AM) |
                         (1 << CP0SC_EU) | (2 << CP0SC_C)) << 16;
    /* XKPhys (note, SegCtl2.XR = 0, so XAM won't be used) */
    env->CP0_SegCtl1 |= (CP0SC_AM_UK << CP0SC1_XAM);
#endif
    if ((env->insn_flags & ISA_MIPS32R6) &&
        (env->active_fpu.fcr0 & (1 << FCR0_F64))) {
        /* Status.FR = 0 mode in 64-bit FPU not allowed in R6 */
        env->CP0_Status |= (1 << CP0St_FR);
    }

    /* MSA */
    if (env->CP0_Config3 & (1 << CP0C3_MSAP)) {
        msa_reset(env);
    }

    compute_hflags(env);
    restore_fp_status(env);
    restore_pamask(env);
    cs->exception_index = EXCP_NONE;

    if (semihosting_get_argc()) {
        /* UHI interface can be used to obtain argc and argv */
        env->active_tc.gpr[4] = -1;
    }
}

void restore_state_to_opc(CPUMIPSState *env, TranslationBlock *tb,
                          target_ulong *data)
{
    env->active_tc.PC = data[0];
    env->hflags &= ~MIPS_HFLAG_BMASK;
    env->hflags |= data[1];
    switch (env->hflags & MIPS_HFLAG_BMASK_BASE) {
    case MIPS_HFLAG_BR:
        break;
    case MIPS_HFLAG_BC:
    case MIPS_HFLAG_BL:
    case MIPS_HFLAG_B:
        env->btarget = data[2];
        break;
    }
}
