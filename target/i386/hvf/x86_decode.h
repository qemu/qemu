/*
 * Copyright (C) 2016 Veertu Inc,
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef HVF_X86_DECODE_H
#define HVF_X86_DECODE_H 1

#include "cpu.h"
#include "x86.h"

typedef enum x86_prefix {
    /* group 1 */
    PREFIX_LOCK =                  0xf0,
    PREFIX_REPN =                  0xf2,
    PREFIX_REP =                   0xf3,
    /* group 2 */
    PREFIX_CS_SEG_OVEERIDE =       0x2e,
    PREFIX_SS_SEG_OVEERIDE =       0x36,
    PREFIX_DS_SEG_OVEERIDE =       0x3e,
    PREFIX_ES_SEG_OVEERIDE =       0x26,
    PREFIX_FS_SEG_OVEERIDE =       0x64,
    PREFIX_GS_SEG_OVEERIDE =       0x65,
    /* group 3 */
    PREFIX_OP_SIZE_OVERRIDE =      0x66,
    /* group 4 */
    PREFIX_ADDR_SIZE_OVERRIDE =    0x67,

    PREFIX_REX                   = 0x40,
} x86_prefix;

enum x86_decode_cmd {
    X86_DECODE_CMD_INVL = 0,
    
    X86_DECODE_CMD_PUSH,
    X86_DECODE_CMD_PUSH_SEG,
    X86_DECODE_CMD_POP,
    X86_DECODE_CMD_POP_SEG,
    X86_DECODE_CMD_MOV,
    X86_DECODE_CMD_MOVSX,
    X86_DECODE_CMD_MOVZX,
    X86_DECODE_CMD_CALL_NEAR,
    X86_DECODE_CMD_CALL_NEAR_ABS_INDIRECT,
    X86_DECODE_CMD_CALL_FAR_ABS_INDIRECT,
    X86_DECODE_CMD_CALL_FAR,
    X86_DECODE_RET_NEAR,
    X86_DECODE_RET_FAR,
    X86_DECODE_CMD_ADD,
    X86_DECODE_CMD_OR,
    X86_DECODE_CMD_ADC,
    X86_DECODE_CMD_SBB,
    X86_DECODE_CMD_AND,
    X86_DECODE_CMD_SUB,
    X86_DECODE_CMD_XOR,
    X86_DECODE_CMD_CMP,
    X86_DECODE_CMD_INC,
    X86_DECODE_CMD_DEC,
    X86_DECODE_CMD_TST,
    X86_DECODE_CMD_NOT,
    X86_DECODE_CMD_NEG,
    X86_DECODE_CMD_JMP_NEAR,
    X86_DECODE_CMD_JMP_NEAR_ABS_INDIRECT,
    X86_DECODE_CMD_JMP_FAR,
    X86_DECODE_CMD_JMP_FAR_ABS_INDIRECT,
    X86_DECODE_CMD_LEA,
    X86_DECODE_CMD_JXX,
    X86_DECODE_CMD_JCXZ,
    X86_DECODE_CMD_SETXX,
    X86_DECODE_CMD_MOV_TO_SEG,
    X86_DECODE_CMD_MOV_FROM_SEG,
    X86_DECODE_CMD_CLI,
    X86_DECODE_CMD_STI,
    X86_DECODE_CMD_CLD,
    X86_DECODE_CMD_STD,
    X86_DECODE_CMD_STC,
    X86_DECODE_CMD_CLC,
    X86_DECODE_CMD_OUT,
    X86_DECODE_CMD_IN,
    X86_DECODE_CMD_INS,
    X86_DECODE_CMD_OUTS,
    X86_DECODE_CMD_LIDT,
    X86_DECODE_CMD_SIDT,
    X86_DECODE_CMD_LGDT,
    X86_DECODE_CMD_SGDT,
    X86_DECODE_CMD_SMSW,
    X86_DECODE_CMD_LMSW,
    X86_DECODE_CMD_RDTSCP,
    X86_DECODE_CMD_INVLPG,
    X86_DECODE_CMD_MOV_TO_CR,
    X86_DECODE_CMD_MOV_FROM_CR,
    X86_DECODE_CMD_MOV_TO_DR,
    X86_DECODE_CMD_MOV_FROM_DR,
    X86_DECODE_CMD_PUSHF,
    X86_DECODE_CMD_POPF,
    X86_DECODE_CMD_CPUID,
    X86_DECODE_CMD_ROL,
    X86_DECODE_CMD_ROR,
    X86_DECODE_CMD_RCL,
    X86_DECODE_CMD_RCR,
    X86_DECODE_CMD_SHL,
    X86_DECODE_CMD_SAL,
    X86_DECODE_CMD_SHR,
    X86_DECODE_CMD_SHRD,
    X86_DECODE_CMD_SHLD,
    X86_DECODE_CMD_SAR,
    X86_DECODE_CMD_DIV,
    X86_DECODE_CMD_IDIV,
    X86_DECODE_CMD_MUL,
    X86_DECODE_CMD_IMUL_3,
    X86_DECODE_CMD_IMUL_2,
    X86_DECODE_CMD_IMUL_1,
    X86_DECODE_CMD_MOVS,
    X86_DECODE_CMD_CMPS,
    X86_DECODE_CMD_SCAS,
    X86_DECODE_CMD_LODS,
    X86_DECODE_CMD_STOS,
    X86_DECODE_CMD_BSWAP,
    X86_DECODE_CMD_XCHG,
    X86_DECODE_CMD_RDTSC,
    X86_DECODE_CMD_RDMSR,
    X86_DECODE_CMD_WRMSR,
    X86_DECODE_CMD_ENTER,
    X86_DECODE_CMD_LEAVE,
    X86_DECODE_CMD_BT,
    X86_DECODE_CMD_BTS,
    X86_DECODE_CMD_BTC,
    X86_DECODE_CMD_BTR,
    X86_DECODE_CMD_BSF,
    X86_DECODE_CMD_BSR,
    X86_DECODE_CMD_IRET,
    X86_DECODE_CMD_INT,
    X86_DECODE_CMD_POPA,
    X86_DECODE_CMD_PUSHA,
    X86_DECODE_CMD_CWD,
    X86_DECODE_CMD_CBW,
    X86_DECODE_CMD_DAS,
    X86_DECODE_CMD_AAD,
    X86_DECODE_CMD_AAM,
    X86_DECODE_CMD_AAS,
    X86_DECODE_CMD_LOOP,
    X86_DECODE_CMD_SLDT,
    X86_DECODE_CMD_STR,
    X86_DECODE_CMD_LLDT,
    X86_DECODE_CMD_LTR,
    X86_DECODE_CMD_VERR,
    X86_DECODE_CMD_VERW,
    X86_DECODE_CMD_SAHF,
    X86_DECODE_CMD_LAHF,
    X86_DECODE_CMD_WBINVD,
    X86_DECODE_CMD_LDS,
    X86_DECODE_CMD_LSS,
    X86_DECODE_CMD_LES,
    X86_DECODE_XMD_LGS,
    X86_DECODE_CMD_LFS,
    X86_DECODE_CMD_CMC,
    X86_DECODE_CMD_XLAT,
    X86_DECODE_CMD_NOP,
    X86_DECODE_CMD_CMOV,
    X86_DECODE_CMD_CLTS,
    X86_DECODE_CMD_XADD,
    X86_DECODE_CMD_HLT,
    X86_DECODE_CMD_CMPXCHG8B,
    X86_DECODE_CMD_CMPXCHG,
    X86_DECODE_CMD_POPCNT,
    
    X86_DECODE_CMD_FNINIT,
    X86_DECODE_CMD_FLD,
    X86_DECODE_CMD_FLDxx,
    X86_DECODE_CMD_FNSTCW,
    X86_DECODE_CMD_FNSTSW,
    X86_DECODE_CMD_FNSETPM,
    X86_DECODE_CMD_FSAVE,
    X86_DECODE_CMD_FRSTOR,
    X86_DECODE_CMD_FXSAVE,
    X86_DECODE_CMD_FXRSTOR,
    X86_DECODE_CMD_FDIV,
    X86_DECODE_CMD_FMUL,
    X86_DECODE_CMD_FSUB,
    X86_DECODE_CMD_FADD,
    X86_DECODE_CMD_EMMS,
    X86_DECODE_CMD_MFENCE,
    X86_DECODE_CMD_SFENCE,
    X86_DECODE_CMD_LFENCE,
    X86_DECODE_CMD_PREFETCH,
    X86_DECODE_CMD_CLFLUSH,
    X86_DECODE_CMD_FST,
    X86_DECODE_CMD_FABS,
    X86_DECODE_CMD_FUCOM,
    X86_DECODE_CMD_FUCOMI,
    X86_DECODE_CMD_FLDCW,
    X86_DECODE_CMD_FXCH,
    X86_DECODE_CMD_FCHS,
    X86_DECODE_CMD_FCMOV,
    X86_DECODE_CMD_FRNDINT,
    X86_DECODE_CMD_FXAM,

    X86_DECODE_CMD_LAST,
};

const char *decode_cmd_to_string(enum x86_decode_cmd cmd);

typedef struct x86_modrm {
    union {
        uint8_t modrm;
        struct {
            uint8_t rm:3;
            uint8_t reg:3;
            uint8_t mod:2;
        };
    };
} __attribute__ ((__packed__)) x86_modrm;

typedef struct x86_sib {
    union {
        uint8_t sib;
        struct {
            uint8_t base:3;
            uint8_t index:3;
            uint8_t scale:2;
        };
    };
} __attribute__ ((__packed__)) x86_sib;

typedef struct x86_rex {
    union {
        uint8_t rex;
        struct {
            uint8_t b:1;
            uint8_t x:1;
            uint8_t r:1;
            uint8_t w:1;
            uint8_t unused:4;
        };
    };
} __attribute__ ((__packed__)) x86_rex;

typedef enum x86_var_type {
    X86_VAR_IMMEDIATE,
    X86_VAR_OFFSET,
    X86_VAR_REG,
    X86_VAR_RM,

    /* for floating point computations */
    X87_VAR_REG,
    X87_VAR_FLOATP,
    X87_VAR_INTP,
    X87_VAR_BYTEP,
} x86_var_type;

typedef struct x86_decode_op {
    enum x86_var_type type;
    int size;

    int reg;
    target_ulong val;

    target_ulong ptr;
} x86_decode_op;

typedef struct x86_decode {
    int len;
    uint8_t opcode[4];
    uint8_t opcode_len;
    enum x86_decode_cmd cmd;
    int addressing_size;
    int operand_size;
    int lock;
    int rep;
    int op_size_override;
    int addr_size_override;
    int segment_override;
    int control_change_inst;
    bool fwait;
    bool fpop_stack;
    bool frev;

    uint32_t displacement;
    uint8_t displacement_size;
    struct x86_rex rex;
    bool is_modrm;
    bool sib_present;
    struct x86_sib sib;
    struct x86_modrm modrm;
    struct x86_decode_op op[4];
    bool is_fpu;
    uint32_t flags_mask;

} x86_decode;

uint64_t sign(uint64_t val, int size);

uint32_t decode_instruction(CPUX86State *env, struct x86_decode *decode);

target_ulong get_reg_ref(CPUX86State *env, int reg, int rex, int is_extended,
                         int size);
target_ulong get_reg_val(CPUX86State *env, int reg, int rex, int is_extended,
                         int size);
void calc_modrm_operand(CPUX86State *env, struct x86_decode *decode,
                        struct x86_decode_op *op);
target_ulong decode_linear_addr(CPUX86State *env, struct x86_decode *decode,
                               target_ulong addr, enum X86Seg seg);

void init_decoder(void);
void calc_modrm_operand16(CPUX86State *env, struct x86_decode *decode,
                          struct x86_decode_op *op);
void calc_modrm_operand32(CPUX86State *env, struct x86_decode *decode,
                          struct x86_decode_op *op);
void calc_modrm_operand64(CPUX86State *env, struct x86_decode *decode,
                          struct x86_decode_op *op);
void set_addressing_size(CPUX86State *env, struct x86_decode *decode);
void set_operand_size(CPUX86State *env, struct x86_decode *decode);

#endif
