/*
 * RISC-V emulation for qemu: Instruction decode helpers
 *
 * Copyright (c) 2016-2017 Sagar Karandikar, sagark@eecs.berkeley.edu
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef TARGET_RISCV_INSTMAP_H
#define TARGET_RISCV_INSTMAP_H

#define MASK_OP_MAJOR(op)  (op & 0x7F)
enum {
    /* rv32i, rv64i, rv32m */
    OPC_RISC_LUI    = (0x37),
    OPC_RISC_AUIPC  = (0x17),
    OPC_RISC_JAL    = (0x6F),
    OPC_RISC_JALR   = (0x67),
    OPC_RISC_BRANCH = (0x63),
    OPC_RISC_LOAD   = (0x03),
    OPC_RISC_STORE  = (0x23),
    OPC_RISC_ARITH_IMM  = (0x13),
    OPC_RISC_ARITH      = (0x33),
    OPC_RISC_FENCE      = (0x0F),
    OPC_RISC_SYSTEM     = (0x73),

    /* rv64i, rv64m */
    OPC_RISC_ARITH_IMM_W = (0x1B),
    OPC_RISC_ARITH_W = (0x3B),

    /* rv32a, rv64a */
    OPC_RISC_ATOMIC = (0x2F),

    /* floating point */
    OPC_RISC_FP_LOAD = (0x7),
    OPC_RISC_FP_STORE = (0x27),

    OPC_RISC_FMADD = (0x43),
    OPC_RISC_FMSUB = (0x47),
    OPC_RISC_FNMSUB = (0x4B),
    OPC_RISC_FNMADD = (0x4F),

    OPC_RISC_FP_ARITH = (0x53),
};

#define MASK_OP_ARITH(op)   (MASK_OP_MAJOR(op) | (op & ((0x7 << 12) | \
                            (0x7F << 25))))
enum {
    OPC_RISC_ADD   = OPC_RISC_ARITH | (0x0 << 12) | (0x00 << 25),
    OPC_RISC_SUB   = OPC_RISC_ARITH | (0x0 << 12) | (0x20 << 25),
    OPC_RISC_SLL   = OPC_RISC_ARITH | (0x1 << 12) | (0x00 << 25),
    OPC_RISC_SLT   = OPC_RISC_ARITH | (0x2 << 12) | (0x00 << 25),
    OPC_RISC_SLTU  = OPC_RISC_ARITH | (0x3 << 12) | (0x00 << 25),
    OPC_RISC_XOR   = OPC_RISC_ARITH | (0x4 << 12) | (0x00 << 25),
    OPC_RISC_SRL   = OPC_RISC_ARITH | (0x5 << 12) | (0x00 << 25),
    OPC_RISC_SRA   = OPC_RISC_ARITH | (0x5 << 12) | (0x20 << 25),
    OPC_RISC_OR    = OPC_RISC_ARITH | (0x6 << 12) | (0x00 << 25),
    OPC_RISC_AND   = OPC_RISC_ARITH | (0x7 << 12) | (0x00 << 25),

    /* RV64M */
    OPC_RISC_MUL    = OPC_RISC_ARITH | (0x0 << 12) | (0x01 << 25),
    OPC_RISC_MULH   = OPC_RISC_ARITH | (0x1 << 12) | (0x01 << 25),
    OPC_RISC_MULHSU = OPC_RISC_ARITH | (0x2 << 12) | (0x01 << 25),
    OPC_RISC_MULHU  = OPC_RISC_ARITH | (0x3 << 12) | (0x01 << 25),

    OPC_RISC_DIV    = OPC_RISC_ARITH | (0x4 << 12) | (0x01 << 25),
    OPC_RISC_DIVU   = OPC_RISC_ARITH | (0x5 << 12) | (0x01 << 25),
    OPC_RISC_REM    = OPC_RISC_ARITH | (0x6 << 12) | (0x01 << 25),
    OPC_RISC_REMU   = OPC_RISC_ARITH | (0x7 << 12) | (0x01 << 25),
};


#define MASK_OP_ARITH_IMM(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_ADDI   = OPC_RISC_ARITH_IMM | (0x0 << 12),
    OPC_RISC_SLTI   = OPC_RISC_ARITH_IMM | (0x2 << 12),
    OPC_RISC_SLTIU  = OPC_RISC_ARITH_IMM | (0x3 << 12),
    OPC_RISC_XORI   = OPC_RISC_ARITH_IMM | (0x4 << 12),
    OPC_RISC_ORI    = OPC_RISC_ARITH_IMM | (0x6 << 12),
    OPC_RISC_ANDI   = OPC_RISC_ARITH_IMM | (0x7 << 12),
    OPC_RISC_SLLI   = OPC_RISC_ARITH_IMM | (0x1 << 12), /* additional part of
                                                           IMM */
    OPC_RISC_SHIFT_RIGHT_I = OPC_RISC_ARITH_IMM | (0x5 << 12) /* SRAI, SRLI */
};

#define MASK_OP_BRANCH(op)     (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_BEQ  = OPC_RISC_BRANCH  | (0x0  << 12),
    OPC_RISC_BNE  = OPC_RISC_BRANCH  | (0x1  << 12),
    OPC_RISC_BLT  = OPC_RISC_BRANCH  | (0x4  << 12),
    OPC_RISC_BGE  = OPC_RISC_BRANCH  | (0x5  << 12),
    OPC_RISC_BLTU = OPC_RISC_BRANCH  | (0x6  << 12),
    OPC_RISC_BGEU = OPC_RISC_BRANCH  | (0x7  << 12)
};

enum {
    OPC_RISC_ADDIW   = OPC_RISC_ARITH_IMM_W | (0x0 << 12),
    OPC_RISC_SLLIW   = OPC_RISC_ARITH_IMM_W | (0x1 << 12), /* additional part of
                                                              IMM */
    OPC_RISC_SHIFT_RIGHT_IW = OPC_RISC_ARITH_IMM_W | (0x5 << 12) /* SRAI, SRLI
                                                                  */
};

enum {
    OPC_RISC_ADDW   = OPC_RISC_ARITH_W | (0x0 << 12) | (0x00 << 25),
    OPC_RISC_SUBW   = OPC_RISC_ARITH_W | (0x0 << 12) | (0x20 << 25),
    OPC_RISC_SLLW   = OPC_RISC_ARITH_W | (0x1 << 12) | (0x00 << 25),
    OPC_RISC_SRLW   = OPC_RISC_ARITH_W | (0x5 << 12) | (0x00 << 25),
    OPC_RISC_SRAW   = OPC_RISC_ARITH_W | (0x5 << 12) | (0x20 << 25),

    /* RV64M */
    OPC_RISC_MULW   = OPC_RISC_ARITH_W | (0x0 << 12) | (0x01 << 25),
    OPC_RISC_DIVW   = OPC_RISC_ARITH_W | (0x4 << 12) | (0x01 << 25),
    OPC_RISC_DIVUW  = OPC_RISC_ARITH_W | (0x5 << 12) | (0x01 << 25),
    OPC_RISC_REMW   = OPC_RISC_ARITH_W | (0x6 << 12) | (0x01 << 25),
    OPC_RISC_REMUW  = OPC_RISC_ARITH_W | (0x7 << 12) | (0x01 << 25),
};

#define MASK_OP_LOAD(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_LB   = OPC_RISC_LOAD | (0x0 << 12),
    OPC_RISC_LH   = OPC_RISC_LOAD | (0x1 << 12),
    OPC_RISC_LW   = OPC_RISC_LOAD | (0x2 << 12),
    OPC_RISC_LD   = OPC_RISC_LOAD | (0x3 << 12),
    OPC_RISC_LBU  = OPC_RISC_LOAD | (0x4 << 12),
    OPC_RISC_LHU  = OPC_RISC_LOAD | (0x5 << 12),
    OPC_RISC_LWU  = OPC_RISC_LOAD | (0x6 << 12),
};

#define MASK_OP_STORE(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_SB   = OPC_RISC_STORE | (0x0 << 12),
    OPC_RISC_SH   = OPC_RISC_STORE | (0x1 << 12),
    OPC_RISC_SW   = OPC_RISC_STORE | (0x2 << 12),
    OPC_RISC_SD   = OPC_RISC_STORE | (0x3 << 12),
};

#define MASK_OP_JALR(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
/* no enum since OPC_RISC_JALR is the actual value */

#define MASK_OP_ATOMIC(op) \
    (MASK_OP_MAJOR(op) | (op & ((0x7 << 12) | (0x7F << 25))))
#define MASK_OP_ATOMIC_NO_AQ_RL_SZ(op) \
    (MASK_OP_MAJOR(op) | (op & (0x1F << 27)))

enum {
    OPC_RISC_LR          = OPC_RISC_ATOMIC | (0x02 << 27),
    OPC_RISC_SC          = OPC_RISC_ATOMIC | (0x03 << 27),
    OPC_RISC_AMOSWAP     = OPC_RISC_ATOMIC | (0x01 << 27),
    OPC_RISC_AMOADD      = OPC_RISC_ATOMIC | (0x00 << 27),
    OPC_RISC_AMOXOR      = OPC_RISC_ATOMIC | (0x04 << 27),
    OPC_RISC_AMOAND      = OPC_RISC_ATOMIC | (0x0C << 27),
    OPC_RISC_AMOOR       = OPC_RISC_ATOMIC | (0x08 << 27),
    OPC_RISC_AMOMIN      = OPC_RISC_ATOMIC | (0x10 << 27),
    OPC_RISC_AMOMAX      = OPC_RISC_ATOMIC | (0x14 << 27),
    OPC_RISC_AMOMINU     = OPC_RISC_ATOMIC | (0x18 << 27),
    OPC_RISC_AMOMAXU     = OPC_RISC_ATOMIC | (0x1C << 27),
};

#define MASK_OP_SYSTEM(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_ECALL       = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_EBREAK      = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_ERET        = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_MRTS        = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_MRTH        = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_HRTS        = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_WFI         = OPC_RISC_SYSTEM | (0x0 << 12),
    OPC_RISC_SFENCEVM    = OPC_RISC_SYSTEM | (0x0 << 12),

    OPC_RISC_CSRRW       = OPC_RISC_SYSTEM | (0x1 << 12),
    OPC_RISC_CSRRS       = OPC_RISC_SYSTEM | (0x2 << 12),
    OPC_RISC_CSRRC       = OPC_RISC_SYSTEM | (0x3 << 12),
    OPC_RISC_CSRRWI      = OPC_RISC_SYSTEM | (0x5 << 12),
    OPC_RISC_CSRRSI      = OPC_RISC_SYSTEM | (0x6 << 12),
    OPC_RISC_CSRRCI      = OPC_RISC_SYSTEM | (0x7 << 12),
};

#define MASK_OP_FP_LOAD(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_FLW   = OPC_RISC_FP_LOAD | (0x2 << 12),
    OPC_RISC_FLD   = OPC_RISC_FP_LOAD | (0x3 << 12),
};

#define MASK_OP_FP_STORE(op)   (MASK_OP_MAJOR(op) | (op & (0x7 << 12)))
enum {
    OPC_RISC_FSW   = OPC_RISC_FP_STORE | (0x2 << 12),
    OPC_RISC_FSD   = OPC_RISC_FP_STORE | (0x3 << 12),
};

#define MASK_OP_FP_FMADD(op)   (MASK_OP_MAJOR(op) | (op & (0x3 << 25)))
enum {
    OPC_RISC_FMADD_S = OPC_RISC_FMADD | (0x0 << 25),
    OPC_RISC_FMADD_D = OPC_RISC_FMADD | (0x1 << 25),
};

#define MASK_OP_FP_FMSUB(op)   (MASK_OP_MAJOR(op) | (op & (0x3 << 25)))
enum {
    OPC_RISC_FMSUB_S = OPC_RISC_FMSUB | (0x0 << 25),
    OPC_RISC_FMSUB_D = OPC_RISC_FMSUB | (0x1 << 25),
};

#define MASK_OP_FP_FNMADD(op)   (MASK_OP_MAJOR(op) | (op & (0x3 << 25)))
enum {
    OPC_RISC_FNMADD_S = OPC_RISC_FNMADD | (0x0 << 25),
    OPC_RISC_FNMADD_D = OPC_RISC_FNMADD | (0x1 << 25),
};

#define MASK_OP_FP_FNMSUB(op)   (MASK_OP_MAJOR(op) | (op & (0x3 << 25)))
enum {
    OPC_RISC_FNMSUB_S = OPC_RISC_FNMSUB | (0x0 << 25),
    OPC_RISC_FNMSUB_D = OPC_RISC_FNMSUB | (0x1 << 25),
};

#define MASK_OP_FP_ARITH(op)   (MASK_OP_MAJOR(op) | (op & (0x7F << 25)))
enum {
    /* float */
    OPC_RISC_FADD_S    = OPC_RISC_FP_ARITH | (0x0 << 25),
    OPC_RISC_FSUB_S    = OPC_RISC_FP_ARITH | (0x4 << 25),
    OPC_RISC_FMUL_S    = OPC_RISC_FP_ARITH | (0x8 << 25),
    OPC_RISC_FDIV_S    = OPC_RISC_FP_ARITH | (0xC << 25),

    OPC_RISC_FSGNJ_S   = OPC_RISC_FP_ARITH | (0x10 << 25),
    OPC_RISC_FSGNJN_S  = OPC_RISC_FP_ARITH | (0x10 << 25),
    OPC_RISC_FSGNJX_S  = OPC_RISC_FP_ARITH | (0x10 << 25),

    OPC_RISC_FMIN_S    = OPC_RISC_FP_ARITH | (0x14 << 25),
    OPC_RISC_FMAX_S    = OPC_RISC_FP_ARITH | (0x14 << 25),

    OPC_RISC_FSQRT_S   = OPC_RISC_FP_ARITH | (0x2C << 25),

    OPC_RISC_FEQ_S     = OPC_RISC_FP_ARITH | (0x50 << 25),
    OPC_RISC_FLT_S     = OPC_RISC_FP_ARITH | (0x50 << 25),
    OPC_RISC_FLE_S     = OPC_RISC_FP_ARITH | (0x50 << 25),

    OPC_RISC_FCVT_W_S  = OPC_RISC_FP_ARITH | (0x60 << 25),
    OPC_RISC_FCVT_WU_S = OPC_RISC_FP_ARITH | (0x60 << 25),
    OPC_RISC_FCVT_L_S  = OPC_RISC_FP_ARITH | (0x60 << 25),
    OPC_RISC_FCVT_LU_S = OPC_RISC_FP_ARITH | (0x60 << 25),

    OPC_RISC_FCVT_S_W  = OPC_RISC_FP_ARITH | (0x68 << 25),
    OPC_RISC_FCVT_S_WU = OPC_RISC_FP_ARITH | (0x68 << 25),
    OPC_RISC_FCVT_S_L  = OPC_RISC_FP_ARITH | (0x68 << 25),
    OPC_RISC_FCVT_S_LU = OPC_RISC_FP_ARITH | (0x68 << 25),

    OPC_RISC_FMV_X_S   = OPC_RISC_FP_ARITH | (0x70 << 25),
    OPC_RISC_FCLASS_S  = OPC_RISC_FP_ARITH | (0x70 << 25),

    OPC_RISC_FMV_S_X   = OPC_RISC_FP_ARITH | (0x78 << 25),

    /* double */
    OPC_RISC_FADD_D    = OPC_RISC_FP_ARITH | (0x1 << 25),
    OPC_RISC_FSUB_D    = OPC_RISC_FP_ARITH | (0x5 << 25),
    OPC_RISC_FMUL_D    = OPC_RISC_FP_ARITH | (0x9 << 25),
    OPC_RISC_FDIV_D    = OPC_RISC_FP_ARITH | (0xD << 25),

    OPC_RISC_FSGNJ_D   = OPC_RISC_FP_ARITH | (0x11 << 25),
    OPC_RISC_FSGNJN_D  = OPC_RISC_FP_ARITH | (0x11 << 25),
    OPC_RISC_FSGNJX_D  = OPC_RISC_FP_ARITH | (0x11 << 25),

    OPC_RISC_FMIN_D    = OPC_RISC_FP_ARITH | (0x15 << 25),
    OPC_RISC_FMAX_D    = OPC_RISC_FP_ARITH | (0x15 << 25),

    OPC_RISC_FCVT_S_D = OPC_RISC_FP_ARITH | (0x20 << 25),

    OPC_RISC_FCVT_D_S = OPC_RISC_FP_ARITH | (0x21 << 25),

    OPC_RISC_FSQRT_D   = OPC_RISC_FP_ARITH | (0x2D << 25),

    OPC_RISC_FEQ_D     = OPC_RISC_FP_ARITH | (0x51 << 25),
    OPC_RISC_FLT_D     = OPC_RISC_FP_ARITH | (0x51 << 25),
    OPC_RISC_FLE_D     = OPC_RISC_FP_ARITH | (0x51 << 25),

    OPC_RISC_FCVT_W_D  = OPC_RISC_FP_ARITH | (0x61 << 25),
    OPC_RISC_FCVT_WU_D = OPC_RISC_FP_ARITH | (0x61 << 25),
    OPC_RISC_FCVT_L_D  = OPC_RISC_FP_ARITH | (0x61 << 25),
    OPC_RISC_FCVT_LU_D = OPC_RISC_FP_ARITH | (0x61 << 25),

    OPC_RISC_FCVT_D_W  = OPC_RISC_FP_ARITH | (0x69 << 25),
    OPC_RISC_FCVT_D_WU = OPC_RISC_FP_ARITH | (0x69 << 25),
    OPC_RISC_FCVT_D_L  = OPC_RISC_FP_ARITH | (0x69 << 25),
    OPC_RISC_FCVT_D_LU = OPC_RISC_FP_ARITH | (0x69 << 25),

    OPC_RISC_FMV_X_D   = OPC_RISC_FP_ARITH | (0x71 << 25),
    OPC_RISC_FCLASS_D  = OPC_RISC_FP_ARITH | (0x71 << 25),

    OPC_RISC_FMV_D_X   = OPC_RISC_FP_ARITH | (0x79 << 25),
};

#define GET_B_IMM(inst) ((extract32(inst, 8, 4) << 1) \
                         | (extract32(inst, 25, 6) << 5) \
                         | (extract32(inst, 7, 1) << 11) \
                         | (sextract64(inst, 31, 1) << 12))

#define GET_STORE_IMM(inst) ((extract32(inst, 7, 5)) \
                             | (sextract64(inst, 25, 7) << 5))

#define GET_JAL_IMM(inst) ((extract32(inst, 21, 10) << 1) \
                           | (extract32(inst, 20, 1) << 11) \
                           | (extract32(inst, 12, 8) << 12) \
                           | (sextract64(inst, 31, 1) << 20))

#define GET_RM(inst)   extract32(inst, 12, 3)
#define GET_RS3(inst)  extract32(inst, 27, 5)
#define GET_RS1(inst)  extract32(inst, 15, 5)
#define GET_RS2(inst)  extract32(inst, 20, 5)
#define GET_RD(inst)   extract32(inst, 7, 5)
#define GET_IMM(inst)  sextract64(inst, 20, 12)

/* RVC decoding macros */
#define GET_C_IMM(inst)             (extract32(inst, 2, 5) \
                                    | (sextract64(inst, 12, 1) << 5))
#define GET_C_ZIMM(inst)            (extract32(inst, 2, 5) \
                                    | (extract32(inst, 12, 1) << 5))
#define GET_C_ADDI4SPN_IMM(inst)    ((extract32(inst, 6, 1) << 2) \
                                    | (extract32(inst, 5, 1) << 3) \
                                    | (extract32(inst, 11, 2) << 4) \
                                    | (extract32(inst, 7, 4) << 6))
#define GET_C_ADDI16SP_IMM(inst)    ((extract32(inst, 6, 1) << 4) \
                                    | (extract32(inst, 2, 1) << 5) \
                                    | (extract32(inst, 5, 1) << 6) \
                                    | (extract32(inst, 3, 2) << 7) \
                                    | (sextract64(inst, 12, 1) << 9))
#define GET_C_LWSP_IMM(inst)        ((extract32(inst, 4, 3) << 2) \
                                    | (extract32(inst, 12, 1) << 5) \
                                    | (extract32(inst, 2, 2) << 6))
#define GET_C_LDSP_IMM(inst)        ((extract32(inst, 5, 2) << 3) \
                                    | (extract32(inst, 12, 1) << 5) \
                                    | (extract32(inst, 2, 3) << 6))
#define GET_C_SWSP_IMM(inst)        ((extract32(inst, 9, 4) << 2) \
                                    | (extract32(inst, 7, 2) << 6))
#define GET_C_SDSP_IMM(inst)        ((extract32(inst, 10, 3) << 3) \
                                    | (extract32(inst, 7, 3) << 6))
#define GET_C_LW_IMM(inst)          ((extract32(inst, 6, 1) << 2) \
                                    | (extract32(inst, 10, 3) << 3) \
                                    | (extract32(inst, 5, 1) << 6))
#define GET_C_LD_IMM(inst)          ((extract16(inst, 10, 3) << 3) \
                                    | (extract16(inst, 5, 2) << 6))
#define GET_C_J_IMM(inst)           ((extract32(inst, 3, 3) << 1) \
                                    | (extract32(inst, 11, 1) << 4) \
                                    | (extract32(inst, 2, 1) << 5) \
                                    | (extract32(inst, 7, 1) << 6) \
                                    | (extract32(inst, 6, 1) << 7) \
                                    | (extract32(inst, 9, 2) << 8) \
                                    | (extract32(inst, 8, 1) << 10) \
                                    | (sextract64(inst, 12, 1) << 11))
#define GET_C_B_IMM(inst)           ((extract32(inst, 3, 2) << 1) \
                                    | (extract32(inst, 10, 2) << 3) \
                                    | (extract32(inst, 2, 1) << 5) \
                                    | (extract32(inst, 5, 2) << 6) \
                                    | (sextract64(inst, 12, 1) << 8))
#define GET_C_SIMM3(inst)           extract32(inst, 10, 3)
#define GET_C_RD(inst)              GET_RD(inst)
#define GET_C_RS1(inst)             GET_RD(inst)
#define GET_C_RS2(inst)             extract32(inst, 2, 5)
#define GET_C_RS1S(inst)            (8 + extract16(inst, 7, 3))
#define GET_C_RS2S(inst)            (8 + extract16(inst, 2, 3))

#endif
