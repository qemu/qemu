/*
 * Copyright (C) 2016 Veertu Inc,
 * Copyright (C) 2017 Google Inc,
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

#include "qemu/osdep.h"

#include "qemu-common.h"
#include "panic.h"
#include "x86_decode.h"
#include "vmx.h"
#include "x86_mmu.h"
#include "x86_descr.h"

#define OPCODE_ESCAPE   0xf

static void decode_invalid(CPUX86State *env, struct x86_decode *decode)
{
    printf("%llx: failed to decode instruction ", env->hvf_emul->fetch_rip -
           decode->len);
    for (int i = 0; i < decode->opcode_len; i++) {
        printf("%x ", decode->opcode[i]);
    }
    printf("\n");
    VM_PANIC("decoder failed\n");
}

uint64_t sign(uint64_t val, int size)
{
    switch (size) {
    case 1:
        val = (int8_t)val;
        break;
    case 2:
        val = (int16_t)val;
        break;
    case 4:
        val = (int32_t)val;
        break;
    case 8:
        val = (int64_t)val;
        break;
    default:
        VM_PANIC_EX("%s invalid size %d\n", __func__, size);
        break;
    }
    return val;
}

static inline uint64_t decode_bytes(CPUX86State *env, struct x86_decode *decode,
                                    int size)
{
    target_ulong val = 0;
    
    switch (size) {
    case 1:
    case 2:
    case 4:
    case 8:
        break;
    default:
        VM_PANIC_EX("%s invalid size %d\n", __func__, size);
        break;
    }
    target_ulong va  = linear_rip(ENV_GET_CPU(env), RIP(env)) + decode->len;
    vmx_read_mem(ENV_GET_CPU(env), &val, va, size);
    decode->len += size;
    
    return val;
}

static inline uint8_t decode_byte(CPUX86State *env, struct x86_decode *decode)
{
    return (uint8_t)decode_bytes(env, decode, 1);
}

static inline uint16_t decode_word(CPUX86State *env, struct x86_decode *decode)
{
    return (uint16_t)decode_bytes(env, decode, 2);
}

static inline uint32_t decode_dword(CPUX86State *env, struct x86_decode *decode)
{
    return (uint32_t)decode_bytes(env, decode, 4);
}

static inline uint64_t decode_qword(CPUX86State *env, struct x86_decode *decode)
{
    return decode_bytes(env, decode, 8);
}

static void decode_modrm_rm(CPUX86State *env, struct x86_decode *decode,
                            struct x86_decode_op *op)
{
    op->type = X86_VAR_RM;
}

static void decode_modrm_reg(CPUX86State *env, struct x86_decode *decode,
                             struct x86_decode_op *op)
{
    op->type = X86_VAR_REG;
    op->reg = decode->modrm.reg;
    op->ptr = get_reg_ref(env, op->reg, decode->rex.rex, decode->rex.r,
                          decode->operand_size);
}

static void decode_rax(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op)
{
    op->type = X86_VAR_REG;
    op->reg = R_EAX;
    op->ptr = get_reg_ref(env, op->reg, decode->rex.rex, 0,
                          decode->operand_size);
}

static inline void decode_immediate(CPUX86State *env, struct x86_decode *decode,
                                    struct x86_decode_op *var, int size)
{
    var->type = X86_VAR_IMMEDIATE;
    var->size = size;
    switch (size) {
    case 1:
        var->val = decode_byte(env, decode);
        break;
    case 2:
        var->val = decode_word(env, decode);
        break;
    case 4:
        var->val = decode_dword(env, decode);
        break;
    case 8:
        var->val = decode_qword(env, decode);
        break;
    default:
        VM_PANIC_EX("bad size %d\n", size);
    }
}

static void decode_imm8(CPUX86State *env, struct x86_decode *decode,
                        struct x86_decode_op *op)
{
    decode_immediate(env, decode, op, 1);
    op->type = X86_VAR_IMMEDIATE;
}

static void decode_imm8_signed(CPUX86State *env, struct x86_decode *decode,
                               struct x86_decode_op *op)
{
    decode_immediate(env, decode, op, 1);
    op->val = sign(op->val, 1);
    op->type = X86_VAR_IMMEDIATE;
}

static void decode_imm16(CPUX86State *env, struct x86_decode *decode,
                         struct x86_decode_op *op)
{
    decode_immediate(env, decode, op, 2);
    op->type = X86_VAR_IMMEDIATE;
}


static void decode_imm(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op)
{
    if (8 == decode->operand_size) {
        decode_immediate(env, decode, op, 4);
        op->val = sign(op->val, decode->operand_size);
    } else {
        decode_immediate(env, decode, op, decode->operand_size);
    }
    op->type = X86_VAR_IMMEDIATE;
}

static void decode_imm_signed(CPUX86State *env, struct x86_decode *decode,
                              struct x86_decode_op *op)
{
    decode_immediate(env, decode, op, decode->operand_size);
    op->val = sign(op->val, decode->operand_size);
    op->type = X86_VAR_IMMEDIATE;
}

static void decode_imm_1(CPUX86State *env, struct x86_decode *decode,
                         struct x86_decode_op *op)
{
    op->type = X86_VAR_IMMEDIATE;
    op->val = 1;
}

static void decode_imm_0(CPUX86State *env, struct x86_decode *decode,
                         struct x86_decode_op *op)
{
    op->type = X86_VAR_IMMEDIATE;
    op->val = 0;
}


static void decode_pushseg(CPUX86State *env, struct x86_decode *decode)
{
    uint8_t op = (decode->opcode_len > 1) ? decode->opcode[1] : decode->opcode[0];
    
    decode->op[0].type = X86_VAR_REG;
    switch (op) {
    case 0xe:
        decode->op[0].reg = R_CS;
        break;
    case 0x16:
        decode->op[0].reg = R_SS;
        break;
    case 0x1e:
        decode->op[0].reg = R_DS;
        break;
    case 0x06:
        decode->op[0].reg = R_ES;
        break;
    case 0xa0:
        decode->op[0].reg = R_FS;
        break;
    case 0xa8:
        decode->op[0].reg = R_GS;
        break;
    }
}

static void decode_popseg(CPUX86State *env, struct x86_decode *decode)
{
    uint8_t op = (decode->opcode_len > 1) ? decode->opcode[1] : decode->opcode[0];
    
    decode->op[0].type = X86_VAR_REG;
    switch (op) {
    case 0xf:
        decode->op[0].reg = R_CS;
        break;
    case 0x17:
        decode->op[0].reg = R_SS;
        break;
    case 0x1f:
        decode->op[0].reg = R_DS;
        break;
    case 0x07:
        decode->op[0].reg = R_ES;
        break;
    case 0xa1:
        decode->op[0].reg = R_FS;
        break;
    case 0xa9:
        decode->op[0].reg = R_GS;
        break;
    }
}

static void decode_incgroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0x40;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
}

static void decode_decgroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0x48;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
}

static void decode_incgroup2(CPUX86State *env, struct x86_decode *decode)
{
    if (!decode->modrm.reg) {
        decode->cmd = X86_DECODE_CMD_INC;
    } else if (1 == decode->modrm.reg) {
        decode->cmd = X86_DECODE_CMD_DEC;
    }
}

static void decode_pushgroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0x50;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
}

static void decode_popgroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0x58;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
}

static void decode_jxx(CPUX86State *env, struct x86_decode *decode)
{
    decode->displacement = decode_bytes(env, decode, decode->operand_size);
    decode->displacement_size = decode->operand_size;
}

static void decode_farjmp(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_IMMEDIATE;
    decode->op[0].val = decode_bytes(env, decode, decode->operand_size);
    decode->displacement = decode_word(env, decode);
}

static void decode_addgroup(CPUX86State *env, struct x86_decode *decode)
{
    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_ADD,
        X86_DECODE_CMD_OR,
        X86_DECODE_CMD_ADC,
        X86_DECODE_CMD_SBB,
        X86_DECODE_CMD_AND,
        X86_DECODE_CMD_SUB,
        X86_DECODE_CMD_XOR,
        X86_DECODE_CMD_CMP
    };
    decode->cmd = group[decode->modrm.reg];
}

static void decode_rotgroup(CPUX86State *env, struct x86_decode *decode)
{
    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_ROL,
        X86_DECODE_CMD_ROR,
        X86_DECODE_CMD_RCL,
        X86_DECODE_CMD_RCR,
        X86_DECODE_CMD_SHL,
        X86_DECODE_CMD_SHR,
        X86_DECODE_CMD_SHL,
        X86_DECODE_CMD_SAR
    };
    decode->cmd = group[decode->modrm.reg];
}

static void decode_f7group(CPUX86State *env, struct x86_decode *decode)
{
    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_TST,
        X86_DECODE_CMD_TST,
        X86_DECODE_CMD_NOT,
        X86_DECODE_CMD_NEG,
        X86_DECODE_CMD_MUL,
        X86_DECODE_CMD_IMUL_1,
        X86_DECODE_CMD_DIV,
        X86_DECODE_CMD_IDIV
    };
    decode->cmd = group[decode->modrm.reg];
    decode_modrm_rm(env, decode, &decode->op[0]);

    switch (decode->modrm.reg) {
    case 0:
    case 1:
        decode_imm(env, decode, &decode->op[1]);
        break;
    case 2:
        break;
    case 3:
        decode->op[1].type = X86_VAR_IMMEDIATE;
        decode->op[1].val = 0;
        break;
    default:
        break;
    }
}

static void decode_xchgroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0x90;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
}

static void decode_movgroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0xb8;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
    decode_immediate(env, decode, &decode->op[1], decode->operand_size);
}

static void fetch_moffs(CPUX86State *env, struct x86_decode *decode,
                        struct x86_decode_op *op)
{
    op->type = X86_VAR_OFFSET;
    op->ptr = decode_bytes(env, decode, decode->addressing_size);
}

static void decode_movgroup8(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[0] - 0xb0;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
    decode_immediate(env, decode, &decode->op[1], decode->operand_size);
}

static void decode_rcx(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op)
{
    op->type = X86_VAR_REG;
    op->reg = R_ECX;
    op->ptr = get_reg_ref(env, op->reg, decode->rex.rex, decode->rex.b,
                          decode->operand_size);
}

struct decode_tbl {
    uint8_t opcode;
    enum x86_decode_cmd cmd;
    uint8_t operand_size;
    bool is_modrm;
    void (*decode_op1)(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op1);
    void (*decode_op2)(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op2);
    void (*decode_op3)(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op3);
    void (*decode_op4)(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op4);
    void (*decode_postfix)(CPUX86State *env, struct x86_decode *decode);
    uint32_t flags_mask;
};

struct decode_x87_tbl {
    uint8_t opcode;
    uint8_t modrm_reg;
    uint8_t modrm_mod;
    enum x86_decode_cmd cmd;
    uint8_t operand_size;
    bool rev;
    bool pop;
    void (*decode_op1)(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op1);
    void (*decode_op2)(CPUX86State *env, struct x86_decode *decode,
                       struct x86_decode_op *op2);
    void (*decode_postfix)(CPUX86State *env, struct x86_decode *decode);
    uint32_t flags_mask;
};

struct decode_tbl invl_inst = {0x0, 0, 0, false, NULL, NULL, NULL, NULL,
                               decode_invalid};

struct decode_tbl _decode_tbl1[256];
struct decode_tbl _decode_tbl2[256];
struct decode_x87_tbl _decode_tbl3[256];

static void decode_x87_ins(CPUX86State *env, struct x86_decode *decode)
{
    struct decode_x87_tbl *decoder;
    
    decode->is_fpu = true;
    int mode = decode->modrm.mod == 3 ? 1 : 0;
    int index = ((decode->opcode[0] & 0xf) << 4) | (mode << 3) |
                 decode->modrm.reg;

    decoder = &_decode_tbl3[index];
    
    decode->cmd = decoder->cmd;
    if (decoder->operand_size) {
        decode->operand_size = decoder->operand_size;
    }
    decode->flags_mask = decoder->flags_mask;
    decode->fpop_stack = decoder->pop;
    decode->frev = decoder->rev;
    
    if (decoder->decode_op1) {
        decoder->decode_op1(env, decode, &decode->op[0]);
    }
    if (decoder->decode_op2) {
        decoder->decode_op2(env, decode, &decode->op[1]);
    }
    if (decoder->decode_postfix) {
        decoder->decode_postfix(env, decode);
    }

    VM_PANIC_ON_EX(!decode->cmd, "x87 opcode %x %x (%x %x) not decoded\n",
                   decode->opcode[0], decode->modrm.modrm, decoder->modrm_reg,
                   decoder->modrm_mod);
}

static void decode_ffgroup(CPUX86State *env, struct x86_decode *decode)
{
    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_INC,
        X86_DECODE_CMD_DEC,
        X86_DECODE_CMD_CALL_NEAR_ABS_INDIRECT,
        X86_DECODE_CMD_CALL_FAR_ABS_INDIRECT,
        X86_DECODE_CMD_JMP_NEAR_ABS_INDIRECT,
        X86_DECODE_CMD_JMP_FAR_ABS_INDIRECT,
        X86_DECODE_CMD_PUSH,
        X86_DECODE_CMD_INVL,
        X86_DECODE_CMD_INVL
    };
    decode->cmd = group[decode->modrm.reg];
    if (decode->modrm.reg > 2) {
        decode->flags_mask = 0;
    }
}

static void decode_sldtgroup(CPUX86State *env, struct x86_decode *decode)
{

    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_SLDT,
        X86_DECODE_CMD_STR,
        X86_DECODE_CMD_LLDT,
        X86_DECODE_CMD_LTR,
        X86_DECODE_CMD_VERR,
        X86_DECODE_CMD_VERW,
        X86_DECODE_CMD_INVL,
        X86_DECODE_CMD_INVL
    };
    decode->cmd = group[decode->modrm.reg];
    printf("%llx: decode_sldtgroup: %d\n", env->hvf_emul->fetch_rip,
            decode->modrm.reg);
}

static void decode_lidtgroup(CPUX86State *env, struct x86_decode *decode)
{
    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_SGDT,
        X86_DECODE_CMD_SIDT,
        X86_DECODE_CMD_LGDT,
        X86_DECODE_CMD_LIDT,
        X86_DECODE_CMD_SMSW,
        X86_DECODE_CMD_LMSW,
        X86_DECODE_CMD_LMSW,
        X86_DECODE_CMD_INVLPG
    };
    decode->cmd = group[decode->modrm.reg];
    if (0xf9 == decode->modrm.modrm) {
        decode->opcode[decode->len++] = decode->modrm.modrm;
        decode->cmd = X86_DECODE_CMD_RDTSCP;
    }
}

static void decode_btgroup(CPUX86State *env, struct x86_decode *decode)
{
    enum x86_decode_cmd group[] = {
        X86_DECODE_CMD_INVL,
        X86_DECODE_CMD_INVL,
        X86_DECODE_CMD_INVL,
        X86_DECODE_CMD_INVL,
        X86_DECODE_CMD_BT,
        X86_DECODE_CMD_BTS,
        X86_DECODE_CMD_BTR,
        X86_DECODE_CMD_BTC
    };
    decode->cmd = group[decode->modrm.reg];
}

static void decode_x87_general(CPUX86State *env, struct x86_decode *decode)
{
    decode->is_fpu = true;
}

static void decode_x87_modrm_floatp(CPUX86State *env, struct x86_decode *decode,
                                    struct x86_decode_op *op)
{
    op->type = X87_VAR_FLOATP;
}

static void decode_x87_modrm_intp(CPUX86State *env, struct x86_decode *decode,
                                  struct x86_decode_op *op)
{
    op->type = X87_VAR_INTP;
}

static void decode_x87_modrm_bytep(CPUX86State *env, struct x86_decode *decode,
                                   struct x86_decode_op *op)
{
    op->type = X87_VAR_BYTEP;
}

static void decode_x87_modrm_st0(CPUX86State *env, struct x86_decode *decode,
                                 struct x86_decode_op *op)
{
    op->type = X87_VAR_REG;
    op->reg = 0;
}

static void decode_decode_x87_modrm_st0(CPUX86State *env,
                                        struct x86_decode *decode,
                                        struct x86_decode_op *op)
{
    op->type = X87_VAR_REG;
    op->reg = decode->modrm.modrm & 7;
}


static void decode_aegroup(CPUX86State *env, struct x86_decode *decode)
{
    decode->is_fpu = true;
    switch (decode->modrm.reg) {
    case 0:
        decode->cmd = X86_DECODE_CMD_FXSAVE;
        decode_x87_modrm_bytep(env, decode, &decode->op[0]);
        break;
    case 1:
        decode_x87_modrm_bytep(env, decode, &decode->op[0]);
        decode->cmd = X86_DECODE_CMD_FXRSTOR;
        break;
    case 5:
        if (decode->modrm.modrm == 0xe8) {
            decode->cmd = X86_DECODE_CMD_LFENCE;
        } else {
            VM_PANIC("xrstor");
        }
        break;
    case 6:
        VM_PANIC_ON(decode->modrm.modrm != 0xf0);
        decode->cmd = X86_DECODE_CMD_MFENCE;
        break;
    case 7:
        if (decode->modrm.modrm == 0xf8) {
            decode->cmd = X86_DECODE_CMD_SFENCE;
        } else {
            decode->cmd = X86_DECODE_CMD_CLFLUSH;
        }
        break;
    default:
        VM_PANIC_EX("0xae: reg %d\n", decode->modrm.reg);
        break;
    }
}

static void decode_bswap(CPUX86State *env, struct x86_decode *decode)
{
    decode->op[0].type = X86_VAR_REG;
    decode->op[0].reg = decode->opcode[1] - 0xc8;
    decode->op[0].ptr = get_reg_ref(env, decode->op[0].reg, decode->rex.rex,
                                    decode->rex.b, decode->operand_size);
}

static void decode_d9_4(CPUX86State *env, struct x86_decode *decode)
{
    switch (decode->modrm.modrm) {
    case 0xe0:
        /* FCHS */
        decode->cmd = X86_DECODE_CMD_FCHS;
        break;
    case 0xe1:
        decode->cmd = X86_DECODE_CMD_FABS;
        break;
    case 0xe4:
        VM_PANIC("FTST");
        break;
    case 0xe5:
        /* FXAM */
        decode->cmd = X86_DECODE_CMD_FXAM;
        break;
    default:
        VM_PANIC("FLDENV");
        break;
    }
}

static void decode_db_4(CPUX86State *env, struct x86_decode *decode)
{
    switch (decode->modrm.modrm) {
    case 0xe0:
        VM_PANIC_EX("unhandled FNENI: %x %x\n", decode->opcode[0],
                    decode->modrm.modrm);
        break;
    case 0xe1:
        VM_PANIC_EX("unhandled FNDISI: %x %x\n", decode->opcode[0],
                    decode->modrm.modrm);
        break;
    case 0xe2:
        VM_PANIC_EX("unhandled FCLEX: %x %x\n", decode->opcode[0],
                    decode->modrm.modrm);
        break;
    case 0xe3:
        decode->cmd = X86_DECODE_CMD_FNINIT;
        break;
    case 0xe4:
        decode->cmd = X86_DECODE_CMD_FNSETPM;
        break;
    default:
        VM_PANIC_EX("unhandled fpu opcode: %x %x\n", decode->opcode[0],
                    decode->modrm.modrm);
        break;
    }
}


#define RFLAGS_MASK_NONE    0
#define RFLAGS_MASK_OSZAPC  (RFLAGS_OF | RFLAGS_SF | RFLAGS_ZF | RFLAGS_AF | \
                             RFLAGS_PF | RFLAGS_CF)
#define RFLAGS_MASK_LAHF    (RFLAGS_SF | RFLAGS_ZF | RFLAGS_AF | RFLAGS_PF | \
                             RFLAGS_CF)
#define RFLAGS_MASK_CF      (RFLAGS_CF)
#define RFLAGS_MASK_IF      (RFLAGS_IF)
#define RFLAGS_MASK_TF      (RFLAGS_TF)
#define RFLAGS_MASK_DF      (RFLAGS_DF)
#define RFLAGS_MASK_ZF      (RFLAGS_ZF)

struct decode_tbl _1op_inst[] = {
    {0x0, X86_DECODE_CMD_ADD, 1, true, decode_modrm_rm, decode_modrm_reg, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x1, X86_DECODE_CMD_ADD, 0, true, decode_modrm_rm, decode_modrm_reg, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x2, X86_DECODE_CMD_ADD, 1, true, decode_modrm_reg, decode_modrm_rm, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x3, X86_DECODE_CMD_ADD, 0, true, decode_modrm_reg, decode_modrm_rm, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x4, X86_DECODE_CMD_ADD, 1, false, decode_rax, decode_imm8, NULL, NULL,
     NULL, RFLAGS_MASK_OSZAPC},
    {0x5, X86_DECODE_CMD_ADD, 0, false, decode_rax, decode_imm, NULL, NULL,
     NULL, RFLAGS_MASK_OSZAPC},
    {0x6, X86_DECODE_CMD_PUSH_SEG, 0, false, false, NULL, NULL, NULL,
     decode_pushseg, RFLAGS_MASK_NONE},
    {0x7, X86_DECODE_CMD_POP_SEG, 0, false, false, NULL, NULL, NULL,
     decode_popseg, RFLAGS_MASK_NONE},
    {0x8, X86_DECODE_CMD_OR, 1, true, decode_modrm_rm, decode_modrm_reg, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x9, X86_DECODE_CMD_OR, 0, true, decode_modrm_rm, decode_modrm_reg, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xa, X86_DECODE_CMD_OR, 1, true, decode_modrm_reg, decode_modrm_rm, NULL,
     NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xb, X86_DECODE_CMD_OR, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xc, X86_DECODE_CMD_OR, 1, false, decode_rax, decode_imm8,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xd, X86_DECODE_CMD_OR, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xe, X86_DECODE_CMD_PUSH_SEG, 0, false, false,
     NULL, NULL, NULL, decode_pushseg, RFLAGS_MASK_NONE},
    {0xf, X86_DECODE_CMD_POP_SEG, 0, false, false,
     NULL, NULL, NULL, decode_popseg, RFLAGS_MASK_NONE},

    {0x10, X86_DECODE_CMD_ADC, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x11, X86_DECODE_CMD_ADC, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x12, X86_DECODE_CMD_ADC, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x13, X86_DECODE_CMD_ADC, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x14, X86_DECODE_CMD_ADC, 1, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x15, X86_DECODE_CMD_ADC, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0x16, X86_DECODE_CMD_PUSH_SEG, 0, false, false,
     NULL, NULL, NULL, decode_pushseg, RFLAGS_MASK_NONE},
    {0x17, X86_DECODE_CMD_POP_SEG, 0, false, false,
     NULL, NULL, NULL, decode_popseg, RFLAGS_MASK_NONE},

    {0x18, X86_DECODE_CMD_SBB, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x19, X86_DECODE_CMD_SBB, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x1a, X86_DECODE_CMD_SBB, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x1b, X86_DECODE_CMD_SBB, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x1c, X86_DECODE_CMD_SBB, 1, false, decode_rax, decode_imm8,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x1d, X86_DECODE_CMD_SBB, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0x1e, X86_DECODE_CMD_PUSH_SEG, 0, false, false,
     NULL, NULL, NULL, decode_pushseg, RFLAGS_MASK_NONE},
    {0x1f, X86_DECODE_CMD_POP_SEG, 0, false, false,
     NULL, NULL, NULL, decode_popseg, RFLAGS_MASK_NONE},

    {0x20, X86_DECODE_CMD_AND, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x21, X86_DECODE_CMD_AND, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x22, X86_DECODE_CMD_AND, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x23, X86_DECODE_CMD_AND, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x24, X86_DECODE_CMD_AND, 1, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x25, X86_DECODE_CMD_AND, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x28, X86_DECODE_CMD_SUB, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x29, X86_DECODE_CMD_SUB, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x2a, X86_DECODE_CMD_SUB, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x2b, X86_DECODE_CMD_SUB, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x2c, X86_DECODE_CMD_SUB, 1, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x2d, X86_DECODE_CMD_SUB, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x2f, X86_DECODE_CMD_DAS, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x30, X86_DECODE_CMD_XOR, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x31, X86_DECODE_CMD_XOR, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x32, X86_DECODE_CMD_XOR, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x33, X86_DECODE_CMD_XOR, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x34, X86_DECODE_CMD_XOR, 1, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x35, X86_DECODE_CMD_XOR, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0x38, X86_DECODE_CMD_CMP, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x39, X86_DECODE_CMD_CMP, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x3a, X86_DECODE_CMD_CMP, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x3b, X86_DECODE_CMD_CMP, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x3c, X86_DECODE_CMD_CMP, 1, false, decode_rax, decode_imm8,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x3d, X86_DECODE_CMD_CMP, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0x3f, X86_DECODE_CMD_AAS, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0x40, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x41, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x42, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x43, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x44, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x45, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x46, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},
    {0x47, X86_DECODE_CMD_INC, 0, false,
     NULL, NULL, NULL, NULL, decode_incgroup, RFLAGS_MASK_OSZAPC},

    {0x48, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x49, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x4a, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x4b, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x4c, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x4d, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x4e, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},
    {0x4f, X86_DECODE_CMD_DEC, 0, false,
     NULL, NULL, NULL, NULL, decode_decgroup, RFLAGS_MASK_OSZAPC},

    {0x50, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x51, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x52, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x53, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x54, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x55, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x56, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},
    {0x57, X86_DECODE_CMD_PUSH, 0, false,
     NULL, NULL, NULL, NULL, decode_pushgroup, RFLAGS_MASK_NONE},

    {0x58, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x59, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x5a, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x5b, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x5c, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x5d, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x5e, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},
    {0x5f, X86_DECODE_CMD_POP, 0, false,
     NULL, NULL, NULL, NULL, decode_popgroup, RFLAGS_MASK_NONE},

    {0x60, X86_DECODE_CMD_PUSHA, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x61, X86_DECODE_CMD_POPA, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0x68, X86_DECODE_CMD_PUSH, 0, false, decode_imm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x6a, X86_DECODE_CMD_PUSH, 0, false, decode_imm8_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x69, X86_DECODE_CMD_IMUL_3, 0, true, decode_modrm_reg,
     decode_modrm_rm, decode_imm, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x6b, X86_DECODE_CMD_IMUL_3, 0, true, decode_modrm_reg, decode_modrm_rm,
     decode_imm8_signed, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0x6c, X86_DECODE_CMD_INS, 1, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x6d, X86_DECODE_CMD_INS, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x6e, X86_DECODE_CMD_OUTS, 1, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x6f, X86_DECODE_CMD_OUTS, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0x70, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x71, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x72, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x73, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x74, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x75, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x76, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x77, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x78, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x79, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x7a, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x7b, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x7c, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x7d, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x7e, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x7f, X86_DECODE_CMD_JXX, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},

    {0x80, X86_DECODE_CMD_INVL, 1, true, decode_modrm_rm, decode_imm8,
     NULL, NULL, decode_addgroup, RFLAGS_MASK_OSZAPC},
    {0x81, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm, decode_imm,
     NULL, NULL, decode_addgroup, RFLAGS_MASK_OSZAPC},
    {0x82, X86_DECODE_CMD_INVL, 1, true, decode_modrm_rm, decode_imm8,
     NULL, NULL, decode_addgroup, RFLAGS_MASK_OSZAPC},
    {0x83, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm, decode_imm8_signed,
     NULL, NULL, decode_addgroup, RFLAGS_MASK_OSZAPC},
    {0x84, X86_DECODE_CMD_TST, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x85, X86_DECODE_CMD_TST, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0x86, X86_DECODE_CMD_XCHG, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x87, X86_DECODE_CMD_XCHG, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x88, X86_DECODE_CMD_MOV, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x89, X86_DECODE_CMD_MOV, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x8a, X86_DECODE_CMD_MOV, 1, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x8b, X86_DECODE_CMD_MOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x8c, X86_DECODE_CMD_MOV_FROM_SEG, 0, true, decode_modrm_rm,
     decode_modrm_reg, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x8d, X86_DECODE_CMD_LEA, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x8e, X86_DECODE_CMD_MOV_TO_SEG, 0, true, decode_modrm_reg,
     decode_modrm_rm, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x8f, X86_DECODE_CMD_POP, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0x90, X86_DECODE_CMD_NOP, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x91, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},
    {0x92, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},
    {0x93, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},
    {0x94, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},
    {0x95, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},
    {0x96, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},
    {0x97, X86_DECODE_CMD_XCHG, 0, false, NULL, decode_rax,
     NULL, NULL, decode_xchgroup, RFLAGS_MASK_NONE},

    {0x98, X86_DECODE_CMD_CBW, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x99, X86_DECODE_CMD_CWD, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0x9a, X86_DECODE_CMD_CALL_FAR, 0, false, NULL,
     NULL, NULL, NULL, decode_farjmp, RFLAGS_MASK_NONE},

    {0x9c, X86_DECODE_CMD_PUSHF, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    /*{0x9d, X86_DECODE_CMD_POPF, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_POPF},*/
    {0x9e, X86_DECODE_CMD_SAHF, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9f, X86_DECODE_CMD_LAHF, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_LAHF},

    {0xa0, X86_DECODE_CMD_MOV, 1, false, decode_rax, fetch_moffs,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa1, X86_DECODE_CMD_MOV, 0, false, decode_rax, fetch_moffs,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa2, X86_DECODE_CMD_MOV, 1, false, fetch_moffs, decode_rax,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa3, X86_DECODE_CMD_MOV, 0, false, fetch_moffs, decode_rax,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xa4, X86_DECODE_CMD_MOVS, 1, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa5, X86_DECODE_CMD_MOVS, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa6, X86_DECODE_CMD_CMPS, 1, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xa7, X86_DECODE_CMD_CMPS, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xaa, X86_DECODE_CMD_STOS, 1, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xab, X86_DECODE_CMD_STOS, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xac, X86_DECODE_CMD_LODS, 1, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xad, X86_DECODE_CMD_LODS, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xae, X86_DECODE_CMD_SCAS, 1, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xaf, X86_DECODE_CMD_SCAS, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xa8, X86_DECODE_CMD_TST, 1, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xa9, X86_DECODE_CMD_TST, 0, false, decode_rax, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xb0, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb1, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb2, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb3, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb4, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb5, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb6, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},
    {0xb7, X86_DECODE_CMD_MOV, 1, false, NULL,
     NULL, NULL, NULL, decode_movgroup8, RFLAGS_MASK_NONE},

    {0xb8, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xb9, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xba, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xbb, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xbc, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xbd, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xbe, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},
    {0xbf, X86_DECODE_CMD_MOV, 0, false, NULL,
     NULL, NULL, NULL, decode_movgroup, RFLAGS_MASK_NONE},

    {0xc0, X86_DECODE_CMD_INVL, 1, true, decode_modrm_rm, decode_imm8,
     NULL, NULL, decode_rotgroup, RFLAGS_MASK_OSZAPC},
    {0xc1, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm, decode_imm8,
     NULL, NULL, decode_rotgroup, RFLAGS_MASK_OSZAPC},

    {0xc2, X86_DECODE_RET_NEAR, 0, false, decode_imm16,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xc3, X86_DECODE_RET_NEAR, 0, false, NULL,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xc4, X86_DECODE_CMD_LES, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xc5, X86_DECODE_CMD_LDS, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xc6, X86_DECODE_CMD_MOV, 1, true, decode_modrm_rm, decode_imm8,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xc7, X86_DECODE_CMD_MOV, 0, true, decode_modrm_rm, decode_imm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xc8, X86_DECODE_CMD_ENTER, 0, false, decode_imm16, decode_imm8,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xc9, X86_DECODE_CMD_LEAVE, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xca, X86_DECODE_RET_FAR, 0, false, decode_imm16, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xcb, X86_DECODE_RET_FAR, 0, false, decode_imm_0, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xcd, X86_DECODE_CMD_INT, 0, false, decode_imm8, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    /*{0xcf, X86_DECODE_CMD_IRET, 0, false, NULL, NULL,
     NULL, NULL, NULL, RFLAGS_MASK_IRET},*/

    {0xd0, X86_DECODE_CMD_INVL, 1, true, decode_modrm_rm, decode_imm_1,
     NULL, NULL, decode_rotgroup, RFLAGS_MASK_OSZAPC},
    {0xd1, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm, decode_imm_1,
     NULL, NULL, decode_rotgroup, RFLAGS_MASK_OSZAPC},
    {0xd2, X86_DECODE_CMD_INVL, 1, true, decode_modrm_rm, decode_rcx,
     NULL, NULL, decode_rotgroup, RFLAGS_MASK_OSZAPC},
    {0xd3, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm, decode_rcx,
     NULL, NULL, decode_rotgroup, RFLAGS_MASK_OSZAPC},

    {0xd4, X86_DECODE_CMD_AAM, 0, false, decode_imm8,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xd5, X86_DECODE_CMD_AAD, 0, false, decode_imm8,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xd7, X86_DECODE_CMD_XLAT, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xd8, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xd9, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xda, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xdb, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xdc, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xdd, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xde, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},
    {0xdf, X86_DECODE_CMD_INVL, 0, true, NULL,
     NULL, NULL, NULL, decode_x87_ins, RFLAGS_MASK_NONE},

    {0xe0, X86_DECODE_CMD_LOOP, 0, false, decode_imm8_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe1, X86_DECODE_CMD_LOOP, 0, false, decode_imm8_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe2, X86_DECODE_CMD_LOOP, 0, false, decode_imm8_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xe3, X86_DECODE_CMD_JCXZ, 1, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},

    {0xe4, X86_DECODE_CMD_IN, 1, false, decode_imm8,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe5, X86_DECODE_CMD_IN, 0, false, decode_imm8,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe6, X86_DECODE_CMD_OUT, 1, false, decode_imm8,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe7, X86_DECODE_CMD_OUT, 0, false, decode_imm8,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe8, X86_DECODE_CMD_CALL_NEAR, 0, false, decode_imm_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xe9, X86_DECODE_CMD_JMP_NEAR, 0, false, decode_imm_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xea, X86_DECODE_CMD_JMP_FAR, 0, false,
     NULL, NULL, NULL, NULL, decode_farjmp, RFLAGS_MASK_NONE},
    {0xeb, X86_DECODE_CMD_JMP_NEAR, 1, false, decode_imm8_signed,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xec, X86_DECODE_CMD_IN, 1, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xed, X86_DECODE_CMD_IN, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xee, X86_DECODE_CMD_OUT, 1, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xef, X86_DECODE_CMD_OUT, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xf4, X86_DECODE_CMD_HLT, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xf5, X86_DECODE_CMD_CMC, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_CF},

    {0xf6, X86_DECODE_CMD_INVL, 1, true,
     NULL, NULL, NULL, NULL, decode_f7group, RFLAGS_MASK_OSZAPC},
    {0xf7, X86_DECODE_CMD_INVL, 0, true,
     NULL, NULL, NULL, NULL, decode_f7group, RFLAGS_MASK_OSZAPC},

    {0xf8, X86_DECODE_CMD_CLC, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_CF},
    {0xf9, X86_DECODE_CMD_STC, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_CF},

    {0xfa, X86_DECODE_CMD_CLI, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_IF},
    {0xfb, X86_DECODE_CMD_STI, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_IF},
    {0xfc, X86_DECODE_CMD_CLD, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_DF},
    {0xfd, X86_DECODE_CMD_STD, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_DF},
    {0xfe, X86_DECODE_CMD_INVL, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, decode_incgroup2, RFLAGS_MASK_OSZAPC},
    {0xff, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, decode_ffgroup, RFLAGS_MASK_OSZAPC},
};

struct decode_tbl _2op_inst[] = {
    {0x0, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, decode_sldtgroup, RFLAGS_MASK_NONE},
    {0x1, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, decode_lidtgroup, RFLAGS_MASK_NONE},
    {0x6, X86_DECODE_CMD_CLTS, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_TF},
    {0x9, X86_DECODE_CMD_WBINVD, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x18, X86_DECODE_CMD_PREFETCH, 0, true,
     NULL, NULL, NULL, NULL, decode_x87_general, RFLAGS_MASK_NONE},
    {0x1f, X86_DECODE_CMD_NOP, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x20, X86_DECODE_CMD_MOV_FROM_CR, 0, true, decode_modrm_rm,
     decode_modrm_reg, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x21, X86_DECODE_CMD_MOV_FROM_DR, 0, true, decode_modrm_rm,
     decode_modrm_reg, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x22, X86_DECODE_CMD_MOV_TO_CR, 0, true, decode_modrm_reg,
     decode_modrm_rm, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x23, X86_DECODE_CMD_MOV_TO_DR, 0, true, decode_modrm_reg,
     decode_modrm_rm, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x30, X86_DECODE_CMD_WRMSR, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x31, X86_DECODE_CMD_RDTSC, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x32, X86_DECODE_CMD_RDMSR, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x40, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x41, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x42, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x43, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x44, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x45, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x46, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x47, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x48, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x49, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x4a, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x4b, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x4c, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x4d, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x4e, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x4f, X86_DECODE_CMD_CMOV, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x77, X86_DECODE_CMD_EMMS, 0, false,
     NULL, NULL, NULL, NULL, decode_x87_general, RFLAGS_MASK_NONE},
    {0x82, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x83, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x84, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x85, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x86, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x87, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x88, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x89, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x8a, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x8b, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x8c, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x8d, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x8e, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x8f, X86_DECODE_CMD_JXX, 0, false,
     NULL, NULL, NULL, NULL, decode_jxx, RFLAGS_MASK_NONE},
    {0x90, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x91, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x92, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x93, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x94, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x95, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x96, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x97, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x98, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x99, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9a, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9b, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9c, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9d, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9e, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0x9f, X86_DECODE_CMD_SETXX, 1, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xb0, X86_DECODE_CMD_CMPXCHG, 1, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xb1, X86_DECODE_CMD_CMPXCHG, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},

    {0xb6, X86_DECODE_CMD_MOVZX, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xb7, X86_DECODE_CMD_MOVZX, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xb8, X86_DECODE_CMD_POPCNT, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xbe, X86_DECODE_CMD_MOVSX, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xbf, X86_DECODE_CMD_MOVSX, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa0, X86_DECODE_CMD_PUSH_SEG, 0, false, false,
     NULL, NULL, NULL, decode_pushseg, RFLAGS_MASK_NONE},
    {0xa1, X86_DECODE_CMD_POP_SEG, 0, false, false,
     NULL, NULL, NULL, decode_popseg, RFLAGS_MASK_NONE},
    {0xa2, X86_DECODE_CMD_CPUID, 0, false,
     NULL, NULL, NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xa3, X86_DECODE_CMD_BT, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_CF},
    {0xa4, X86_DECODE_CMD_SHLD, 0, true, decode_modrm_rm, decode_modrm_reg,
     decode_imm8, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xa5, X86_DECODE_CMD_SHLD, 0, true, decode_modrm_rm, decode_modrm_reg,
     decode_rcx, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xa8, X86_DECODE_CMD_PUSH_SEG, 0, false, false,
     NULL, NULL, NULL, decode_pushseg, RFLAGS_MASK_NONE},
    {0xa9, X86_DECODE_CMD_POP_SEG, 0, false, false,
     NULL, NULL, NULL, decode_popseg, RFLAGS_MASK_NONE},
    {0xab, X86_DECODE_CMD_BTS, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_CF},
    {0xac, X86_DECODE_CMD_SHRD, 0, true, decode_modrm_rm, decode_modrm_reg,
     decode_imm8, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xad, X86_DECODE_CMD_SHRD, 0, true, decode_modrm_rm, decode_modrm_reg,
     decode_rcx, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xae, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, decode_aegroup, RFLAGS_MASK_NONE},

    {0xaf, X86_DECODE_CMD_IMUL_2, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xb2, X86_DECODE_CMD_LSS, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_NONE},
    {0xb3, X86_DECODE_CMD_BTR, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xba, X86_DECODE_CMD_INVL, 0, true, decode_modrm_rm, decode_imm8,
     NULL, NULL, decode_btgroup, RFLAGS_MASK_OSZAPC},
    {0xbb, X86_DECODE_CMD_BTC, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xbc, X86_DECODE_CMD_BSF, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},
    {0xbd, X86_DECODE_CMD_BSR, 0, true, decode_modrm_reg, decode_modrm_rm,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xc1, X86_DECODE_CMD_XADD, 0, true, decode_modrm_rm, decode_modrm_reg,
     NULL, NULL, NULL, RFLAGS_MASK_OSZAPC},

    {0xc7, X86_DECODE_CMD_CMPXCHG8B, 0, true, decode_modrm_rm,
     NULL, NULL, NULL, NULL, RFLAGS_MASK_ZF},

    {0xc8, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xc9, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xca, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xcb, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xcc, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xcd, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xce, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
    {0xcf, X86_DECODE_CMD_BSWAP, 0, false,
     NULL, NULL, NULL, NULL, decode_bswap, RFLAGS_MASK_NONE},
};

struct decode_x87_tbl invl_inst_x87 = {0x0, 0, 0, 0, 0, false, false, NULL,
                                       NULL, decode_invalid, 0};

struct decode_x87_tbl _x87_inst[] = {
    {0xd8, 0, 3, X86_DECODE_CMD_FADD, 10, false, false,
     decode_x87_modrm_st0, decode_decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd8, 0, 0, X86_DECODE_CMD_FADD, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xd8, 1, 3, X86_DECODE_CMD_FMUL, 10, false, false, decode_x87_modrm_st0,
     decode_decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd8, 1, 0, X86_DECODE_CMD_FMUL, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xd8, 4, 3, X86_DECODE_CMD_FSUB, 10, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd8, 4, 0, X86_DECODE_CMD_FSUB, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xd8, 5, 3, X86_DECODE_CMD_FSUB, 10, true, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd8, 5, 0, X86_DECODE_CMD_FSUB, 4, true, false, decode_x87_modrm_st0,
     decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xd8, 6, 3, X86_DECODE_CMD_FDIV, 10, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd8, 6, 0, X86_DECODE_CMD_FDIV, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xd8, 7, 3, X86_DECODE_CMD_FDIV, 10, true, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd8, 7, 0, X86_DECODE_CMD_FDIV, 4, true, false, decode_x87_modrm_st0,
     decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},

    {0xd9, 0, 3, X86_DECODE_CMD_FLD, 10, false, false,
     decode_x87_modrm_st0, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 0, 0, X86_DECODE_CMD_FLD, 4, false, false,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 1, 3, X86_DECODE_CMD_FXCH, 10, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xd9, 1, 0, X86_DECODE_CMD_INVL, 10, false, false,
     decode_x87_modrm_st0, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 2, 3, X86_DECODE_CMD_INVL, 10, false, false,
     decode_x87_modrm_st0, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 2, 0, X86_DECODE_CMD_FST, 4, false, false,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 3, 3, X86_DECODE_CMD_INVL, 10, false, false,
     decode_x87_modrm_st0, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 3, 0, X86_DECODE_CMD_FST, 4, false, true,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 4, 3, X86_DECODE_CMD_INVL, 10, false, false,
     decode_x87_modrm_st0, NULL, decode_d9_4, RFLAGS_MASK_NONE},
    {0xd9, 4, 0, X86_DECODE_CMD_INVL, 4, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 5, 3, X86_DECODE_CMD_FLDxx, 10, false, false, NULL, NULL, NULL,
     RFLAGS_MASK_NONE},
    {0xd9, 5, 0, X86_DECODE_CMD_FLDCW, 2, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},

    {0xd9, 7, 3, X86_DECODE_CMD_FNSTCW, 2, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},
    {0xd9, 7, 0, X86_DECODE_CMD_FNSTCW, 2, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},

    {0xda, 0, 3, X86_DECODE_CMD_FCMOV, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xda, 0, 0, X86_DECODE_CMD_FADD, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xda, 1, 3, X86_DECODE_CMD_FCMOV, 10, false, false, decode_x87_modrm_st0,
     decode_decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xda, 1, 0, X86_DECODE_CMD_FMUL, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xda, 2, 3, X86_DECODE_CMD_FCMOV, 10, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xda, 3, 3, X86_DECODE_CMD_FCMOV, 10, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xda, 4, 3, X86_DECODE_CMD_INVL, 10, false, false, NULL, NULL, NULL,
     RFLAGS_MASK_NONE},
    {0xda, 4, 0, X86_DECODE_CMD_FSUB, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xda, 5, 3, X86_DECODE_CMD_FUCOM, 10, false, true, decode_x87_modrm_st0,
     decode_decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xda, 5, 0, X86_DECODE_CMD_FSUB, 4, true, false, decode_x87_modrm_st0,
     decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xda, 6, 3, X86_DECODE_CMD_INVL, 10, false, false, NULL, NULL, NULL,
     RFLAGS_MASK_NONE},
    {0xda, 6, 0, X86_DECODE_CMD_FDIV, 4, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xda, 7, 3, X86_DECODE_CMD_INVL, 10, false, false, NULL, NULL, NULL,
     RFLAGS_MASK_NONE},
    {0xda, 7, 0, X86_DECODE_CMD_FDIV, 4, true, false, decode_x87_modrm_st0,
     decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},

    {0xdb, 0, 3, X86_DECODE_CMD_FCMOV, 10, false, false, decode_x87_modrm_st0,
     decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdb, 0, 0, X86_DECODE_CMD_FLD, 4, false, false,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdb, 1, 3, X86_DECODE_CMD_FCMOV, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdb, 2, 3, X86_DECODE_CMD_FCMOV, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdb, 2, 0, X86_DECODE_CMD_FST, 4, false, false,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdb, 3, 3, X86_DECODE_CMD_FCMOV, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdb, 3, 0, X86_DECODE_CMD_FST, 4, false, true,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdb, 4, 3, X86_DECODE_CMD_INVL, 10, false, false, NULL, NULL,
     decode_db_4, RFLAGS_MASK_NONE},
    {0xdb, 4, 0, X86_DECODE_CMD_INVL, 10, false, false, NULL, NULL, NULL,
     RFLAGS_MASK_NONE},
    {0xdb, 5, 3, X86_DECODE_CMD_FUCOMI, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdb, 5, 0, X86_DECODE_CMD_FLD, 10, false, false,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdb, 7, 0, X86_DECODE_CMD_FST, 10, false, true,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},

    {0xdc, 0, 3, X86_DECODE_CMD_FADD, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdc, 0, 0, X86_DECODE_CMD_FADD, 8, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xdc, 1, 3, X86_DECODE_CMD_FMUL, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdc, 1, 0, X86_DECODE_CMD_FMUL, 8, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xdc, 4, 3, X86_DECODE_CMD_FSUB, 10, true, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdc, 4, 0, X86_DECODE_CMD_FSUB, 8, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xdc, 5, 3, X86_DECODE_CMD_FSUB, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdc, 5, 0, X86_DECODE_CMD_FSUB, 8, true, false,
     decode_x87_modrm_st0, decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xdc, 6, 3, X86_DECODE_CMD_FDIV, 10, true, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdc, 6, 0, X86_DECODE_CMD_FDIV, 8, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},
    {0xdc, 7, 3, X86_DECODE_CMD_FDIV, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdc, 7, 0, X86_DECODE_CMD_FDIV, 8, true, false,
     decode_x87_modrm_st0, decode_x87_modrm_floatp, NULL, RFLAGS_MASK_NONE},

    {0xdd, 0, 0, X86_DECODE_CMD_FLD, 8, false, false,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 1, 3, X86_DECODE_CMD_FXCH, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdd, 2, 3, X86_DECODE_CMD_FST, 10, false, false,
     decode_x87_modrm_st0, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 2, 0, X86_DECODE_CMD_FST, 8, false, false,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 3, 3, X86_DECODE_CMD_FST, 10, false, true,
     decode_x87_modrm_st0, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 3, 0, X86_DECODE_CMD_FST, 8, false, true,
     decode_x87_modrm_floatp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 4, 3, X86_DECODE_CMD_FUCOM, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdd, 4, 0, X86_DECODE_CMD_FRSTOR, 8, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 5, 3, X86_DECODE_CMD_FUCOM, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdd, 7, 0, X86_DECODE_CMD_FNSTSW, 0, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdd, 7, 3, X86_DECODE_CMD_FNSTSW, 0, false, false,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},

    {0xde, 0, 3, X86_DECODE_CMD_FADD, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xde, 0, 0, X86_DECODE_CMD_FADD, 2, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xde, 1, 3, X86_DECODE_CMD_FMUL, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xde, 1, 0, X86_DECODE_CMD_FMUL, 2, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xde, 4, 3, X86_DECODE_CMD_FSUB, 10, true, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xde, 4, 0, X86_DECODE_CMD_FSUB, 2, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xde, 5, 3, X86_DECODE_CMD_FSUB, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xde, 5, 0, X86_DECODE_CMD_FSUB, 2, true, false,
     decode_x87_modrm_st0, decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xde, 6, 3, X86_DECODE_CMD_FDIV, 10, true, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xde, 6, 0, X86_DECODE_CMD_FDIV, 2, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},
    {0xde, 7, 3, X86_DECODE_CMD_FDIV, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xde, 7, 0, X86_DECODE_CMD_FDIV, 2, true, false,
     decode_x87_modrm_st0, decode_x87_modrm_intp, NULL, RFLAGS_MASK_NONE},

    {0xdf, 0, 0, X86_DECODE_CMD_FLD, 2, false, false,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdf, 1, 3, X86_DECODE_CMD_FXCH, 10, false, false,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdf, 2, 3, X86_DECODE_CMD_FST, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdf, 2, 0, X86_DECODE_CMD_FST, 2, false, false,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdf, 3, 3, X86_DECODE_CMD_FST, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdf, 3, 0, X86_DECODE_CMD_FST, 2, false, true,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdf, 4, 3, X86_DECODE_CMD_FNSTSW, 2, false, true,
     decode_x87_modrm_bytep, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdf, 5, 3, X86_DECODE_CMD_FUCOMI, 10, false, true,
     decode_x87_modrm_st0, decode_x87_modrm_st0, NULL, RFLAGS_MASK_NONE},
    {0xdf, 5, 0, X86_DECODE_CMD_FLD, 8, false, false,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
    {0xdf, 7, 0, X86_DECODE_CMD_FST, 8, false, true,
     decode_x87_modrm_intp, NULL, NULL, RFLAGS_MASK_NONE},
};

void calc_modrm_operand16(CPUX86State *env, struct x86_decode *decode,
                          struct x86_decode_op *op)
{
    target_ulong ptr = 0;
    X86Seg seg = R_DS;

    if (!decode->modrm.mod && 6 == decode->modrm.rm) {
        op->ptr = (uint16_t)decode->displacement;
        goto calc_addr;
    }

    if (decode->displacement_size) {
        ptr = sign(decode->displacement, decode->displacement_size);
    }

    switch (decode->modrm.rm) {
    case 0:
        ptr += BX(env) + SI(env);
        break;
    case 1:
        ptr += BX(env) + DI(env);
        break;
    case 2:
        ptr += BP(env) + SI(env);
        seg = R_SS;
        break;
    case 3:
        ptr += BP(env) + DI(env);
        seg = R_SS;
        break;
    case 4:
        ptr += SI(env);
        break;
    case 5:
        ptr += DI(env);
        break;
    case 6:
        ptr += BP(env);
        seg = R_SS;
        break;
    case 7:
        ptr += BX(env);
        break;
    }
calc_addr:
    if (X86_DECODE_CMD_LEA == decode->cmd) {
        op->ptr = (uint16_t)ptr;
    } else {
        op->ptr = decode_linear_addr(env, decode, (uint16_t)ptr, seg);
    }
}

target_ulong get_reg_ref(CPUX86State *env, int reg, int rex, int is_extended,
                         int size)
{
    target_ulong ptr = 0;
    int which = 0;

    if (is_extended) {
        reg |= R_R8;
    }


    switch (size) {
    case 1:
        if (is_extended || reg < 4 || rex) {
            which = 1;
            ptr = (target_ulong)&RL(env, reg);
        } else {
            which = 2;
            ptr = (target_ulong)&RH(env, reg - 4);
        }
        break;
    default:
        which = 3;
        ptr = (target_ulong)&RRX(env, reg);
        break;
    }
    return ptr;
}

target_ulong get_reg_val(CPUX86State *env, int reg, int rex, int is_extended,
                         int size)
{
    target_ulong val = 0;
    memcpy(&val, (void *)get_reg_ref(env, reg, rex, is_extended, size), size);
    return val;
}

static target_ulong get_sib_val(CPUX86State *env, struct x86_decode *decode,
                          X86Seg *sel)
{
    target_ulong base = 0;
    target_ulong scaled_index = 0;
    int addr_size = decode->addressing_size;
    int base_reg = decode->sib.base;
    int index_reg = decode->sib.index;

    *sel = R_DS;

    if (decode->modrm.mod || base_reg != R_EBP) {
        if (decode->rex.b) {
            base_reg |= R_R8;
        }
        if (base_reg == R_ESP || base_reg == R_EBP) {
            *sel = R_SS;
        }
        base = get_reg_val(env, decode->sib.base, decode->rex.rex,
                           decode->rex.b, addr_size);
    }

    if (decode->rex.x) {
        index_reg |= R_R8;
    }

    if (index_reg != R_ESP) {
        scaled_index = get_reg_val(env, index_reg, decode->rex.rex,
                                   decode->rex.x, addr_size) <<
                                   decode->sib.scale;
    }
    return base + scaled_index;
}

void calc_modrm_operand32(CPUX86State *env, struct x86_decode *decode,
                          struct x86_decode_op *op)
{
    X86Seg seg = R_DS;
    target_ulong ptr = 0;
    int addr_size = decode->addressing_size;

    if (decode->displacement_size) {
        ptr = sign(decode->displacement, decode->displacement_size);
    }

    if (4 == decode->modrm.rm) {
        ptr += get_sib_val(env, decode, &seg);
    } else if (!decode->modrm.mod && 5 == decode->modrm.rm) {
        if (x86_is_long_mode(ENV_GET_CPU(env))) {
            ptr += RIP(env) + decode->len;
        } else {
            ptr = decode->displacement;
        }
    } else {
        if (decode->modrm.rm == R_EBP || decode->modrm.rm == R_ESP) {
            seg = R_SS;
        }
        ptr += get_reg_val(env, decode->modrm.rm, decode->rex.rex,
                           decode->rex.b, addr_size);
    }

    if (X86_DECODE_CMD_LEA == decode->cmd) {
        op->ptr = (uint32_t)ptr;
    } else {
        op->ptr = decode_linear_addr(env, decode, (uint32_t)ptr, seg);
    }
}

void calc_modrm_operand64(CPUX86State *env, struct x86_decode *decode,
                          struct x86_decode_op *op)
{
    X86Seg seg = R_DS;
    int32_t offset = 0;
    int mod = decode->modrm.mod;
    int rm = decode->modrm.rm;
    target_ulong ptr;
    int src = decode->modrm.rm;

    if (decode->displacement_size) {
        offset = sign(decode->displacement, decode->displacement_size);
    }

    if (4 == rm) {
        ptr = get_sib_val(env, decode, &seg) + offset;
    } else if (0 == mod && 5 == rm) {
        ptr = RIP(env) + decode->len + (int32_t) offset;
    } else {
        ptr = get_reg_val(env, src, decode->rex.rex, decode->rex.b, 8) +
              (int64_t) offset;
    }

    if (X86_DECODE_CMD_LEA == decode->cmd) {
        op->ptr = ptr;
    } else {
        op->ptr = decode_linear_addr(env, decode, ptr, seg);
    }
}


void calc_modrm_operand(CPUX86State *env, struct x86_decode *decode,
                        struct x86_decode_op *op)
{
    if (3 == decode->modrm.mod) {
        op->reg = decode->modrm.reg;
        op->type = X86_VAR_REG;
        op->ptr = get_reg_ref(env, decode->modrm.rm, decode->rex.rex,
                              decode->rex.b, decode->operand_size);
        return;
    }

    switch (decode->addressing_size) {
    case 2:
        calc_modrm_operand16(env, decode, op);
        break;
    case 4:
        calc_modrm_operand32(env, decode, op);
        break;
    case 8:
        calc_modrm_operand64(env, decode, op);
        break;
    default:
        VM_PANIC_EX("unsupported address size %d\n", decode->addressing_size);
        break;
    }
}

static void decode_prefix(CPUX86State *env, struct x86_decode *decode)
{
    while (1) {
        uint8_t byte = decode_byte(env, decode);
        switch (byte) {
        case PREFIX_LOCK:
            decode->lock = byte;
            break;
        case PREFIX_REPN:
        case PREFIX_REP:
            decode->rep = byte;
            break;
        case PREFIX_CS_SEG_OVEERIDE:
        case PREFIX_SS_SEG_OVEERIDE:
        case PREFIX_DS_SEG_OVEERIDE:
        case PREFIX_ES_SEG_OVEERIDE:
        case PREFIX_FS_SEG_OVEERIDE:
        case PREFIX_GS_SEG_OVEERIDE:
            decode->segment_override = byte;
            break;
        case PREFIX_OP_SIZE_OVERRIDE:
            decode->op_size_override = byte;
            break;
        case PREFIX_ADDR_SIZE_OVERRIDE:
            decode->addr_size_override = byte;
            break;
        case PREFIX_REX ... (PREFIX_REX + 0xf):
            if (x86_is_long_mode(ENV_GET_CPU(env))) {
                decode->rex.rex = byte;
                break;
            }
            /* fall through when not in long mode */
        default:
            decode->len--;
            return;
        }
    }
}

void set_addressing_size(CPUX86State *env, struct x86_decode *decode)
{
    decode->addressing_size = -1;
    if (x86_is_real(ENV_GET_CPU(env)) || x86_is_v8086(ENV_GET_CPU(env))) {
        if (decode->addr_size_override) {
            decode->addressing_size = 4;
        } else {
            decode->addressing_size = 2;
        }
    } else if (!x86_is_long_mode(ENV_GET_CPU(env))) {
        /* protected */
        struct vmx_segment cs;
        vmx_read_segment_descriptor(ENV_GET_CPU(env), &cs, R_CS);
        /* check db */
        if ((cs.ar >> 14) & 1) {
            if (decode->addr_size_override) {
                decode->addressing_size = 2;
            } else {
                decode->addressing_size = 4;
            }
        } else {
            if (decode->addr_size_override) {
                decode->addressing_size = 4;
            } else {
                decode->addressing_size = 2;
            }
        }
    } else {
        /* long */
        if (decode->addr_size_override) {
            decode->addressing_size = 4;
        } else {
            decode->addressing_size = 8;
        }
    }
}

void set_operand_size(CPUX86State *env, struct x86_decode *decode)
{
    decode->operand_size = -1;
    if (x86_is_real(ENV_GET_CPU(env)) || x86_is_v8086(ENV_GET_CPU(env))) {
        if (decode->op_size_override) {
            decode->operand_size = 4;
        } else {
            decode->operand_size = 2;
        }
    } else if (!x86_is_long_mode(ENV_GET_CPU(env))) {
        /* protected */
        struct vmx_segment cs;
        vmx_read_segment_descriptor(ENV_GET_CPU(env), &cs, R_CS);
        /* check db */
        if ((cs.ar >> 14) & 1) {
            if (decode->op_size_override) {
                decode->operand_size = 2;
            } else{
                decode->operand_size = 4;
            }
        } else {
            if (decode->op_size_override) {
                decode->operand_size = 4;
            } else {
                decode->operand_size = 2;
            }
        }
    } else {
        /* long */
        if (decode->op_size_override) {
            decode->operand_size = 2;
        } else {
            decode->operand_size = 4;
        }

        if (decode->rex.w) {
            decode->operand_size = 8;
        }
    }
}

static void decode_sib(CPUX86State *env, struct x86_decode *decode)
{
    if ((decode->modrm.mod != 3) && (4 == decode->modrm.rm) &&
        (decode->addressing_size != 2)) {
        decode->sib.sib = decode_byte(env, decode);
        decode->sib_present = true;
    }
}

/* 16 bit modrm */
int disp16_tbl[4][8] = {
    {0, 0, 0, 0, 0, 0, 2, 0},
    {1, 1, 1, 1, 1, 1, 1, 1},
    {2, 2, 2, 2, 2, 2, 2, 2},
    {0, 0, 0, 0, 0, 0, 0, 0}
};

/* 32/64-bit modrm */
int disp32_tbl[4][8] = {
    {0, 0, 0, 0, -1, 4, 0, 0},
    {1, 1, 1, 1, 1, 1, 1, 1},
    {4, 4, 4, 4, 4, 4, 4, 4},
    {0, 0, 0, 0, 0, 0, 0, 0}
};

static inline void decode_displacement(CPUX86State *env, struct x86_decode *decode)
{
    int addressing_size = decode->addressing_size;
    int mod = decode->modrm.mod;
    int rm = decode->modrm.rm;
    
    decode->displacement_size = 0;
    switch (addressing_size) {
    case 2:
        decode->displacement_size = disp16_tbl[mod][rm];
        if (decode->displacement_size) {
            decode->displacement = (uint16_t)decode_bytes(env, decode,
                                    decode->displacement_size);
        }
        break;
    case 4:
    case 8:
        if (-1 == disp32_tbl[mod][rm]) {
            if (5 == decode->sib.base) {
                decode->displacement_size = 4;
            }
        } else {
            decode->displacement_size = disp32_tbl[mod][rm];
        }

        if (decode->displacement_size) {
            decode->displacement = (uint32_t)decode_bytes(env, decode,
                                                decode->displacement_size);
        }
        break;
    }
}

static inline void decode_modrm(CPUX86State *env, struct x86_decode *decode)
{
    decode->modrm.modrm = decode_byte(env, decode);
    decode->is_modrm = true;

    decode_sib(env, decode);
    decode_displacement(env, decode);
}

static inline void decode_opcode_general(CPUX86State *env,
                                         struct x86_decode *decode,
                                         uint8_t opcode,
                                         struct decode_tbl *inst_decoder)
{
    decode->cmd = inst_decoder->cmd;
    if (inst_decoder->operand_size) {
        decode->operand_size = inst_decoder->operand_size;
    }
    decode->flags_mask = inst_decoder->flags_mask;

    if (inst_decoder->is_modrm) {
        decode_modrm(env, decode);
    }
    if (inst_decoder->decode_op1) {
        inst_decoder->decode_op1(env, decode, &decode->op[0]);
    }
    if (inst_decoder->decode_op2) {
        inst_decoder->decode_op2(env, decode, &decode->op[1]);
    }
    if (inst_decoder->decode_op3) {
        inst_decoder->decode_op3(env, decode, &decode->op[2]);
    }
    if (inst_decoder->decode_op4) {
        inst_decoder->decode_op4(env, decode, &decode->op[3]);
    }
    if (inst_decoder->decode_postfix) {
        inst_decoder->decode_postfix(env, decode);
    }
}

static inline void decode_opcode_1(CPUX86State *env, struct x86_decode *decode,
                                   uint8_t opcode)
{
    struct decode_tbl *inst_decoder = &_decode_tbl1[opcode];
    decode_opcode_general(env, decode, opcode, inst_decoder);
}


static inline void decode_opcode_2(CPUX86State *env, struct x86_decode *decode,
                                   uint8_t opcode)
{
    struct decode_tbl *inst_decoder = &_decode_tbl2[opcode];
    decode_opcode_general(env, decode, opcode, inst_decoder);
}

static void decode_opcodes(CPUX86State *env, struct x86_decode *decode)
{
    uint8_t opcode;

    opcode = decode_byte(env, decode);
    decode->opcode[decode->opcode_len++] = opcode;
    if (opcode != OPCODE_ESCAPE) {
        decode_opcode_1(env, decode, opcode);
    } else {
        opcode = decode_byte(env, decode);
        decode->opcode[decode->opcode_len++] = opcode;
        decode_opcode_2(env, decode, opcode);
    }
}

uint32_t decode_instruction(CPUX86State *env, struct x86_decode *decode)
{
    memset(decode, 0, sizeof(*decode));
    decode_prefix(env, decode);
    set_addressing_size(env, decode);
    set_operand_size(env, decode);

    decode_opcodes(env, decode);

    return decode->len;
}

void init_decoder()
{
    int i;
    
    for (i = 0; i < ARRAY_SIZE(_decode_tbl2); i++) {
        memcpy(_decode_tbl1, &invl_inst, sizeof(invl_inst));
    }
    for (i = 0; i < ARRAY_SIZE(_decode_tbl2); i++) {
        memcpy(_decode_tbl2, &invl_inst, sizeof(invl_inst));
    }
    for (i = 0; i < ARRAY_SIZE(_decode_tbl3); i++) {
        memcpy(_decode_tbl3, &invl_inst, sizeof(invl_inst_x87));
    
    }
    for (i = 0; i < ARRAY_SIZE(_1op_inst); i++) {
        _decode_tbl1[_1op_inst[i].opcode] = _1op_inst[i];
    }
    for (i = 0; i < ARRAY_SIZE(_2op_inst); i++) {
        _decode_tbl2[_2op_inst[i].opcode] = _2op_inst[i];
    }
    for (i = 0; i < ARRAY_SIZE(_x87_inst); i++) {
        int index = ((_x87_inst[i].opcode & 0xf) << 4) |
                    ((_x87_inst[i].modrm_mod & 1) << 3) |
                    _x87_inst[i].modrm_reg;
        _decode_tbl3[index] = _x87_inst[i];
    }
}


const char *decode_cmd_to_string(enum x86_decode_cmd cmd)
{
    static const char *cmds[] = {"INVL", "PUSH", "PUSH_SEG", "POP", "POP_SEG",
        "MOV", "MOVSX", "MOVZX", "CALL_NEAR", "CALL_NEAR_ABS_INDIRECT",
        "CALL_FAR_ABS_INDIRECT", "CMD_CALL_FAR", "RET_NEAR", "RET_FAR", "ADD",
        "OR", "ADC", "SBB", "AND", "SUB", "XOR", "CMP", "INC", "DEC", "TST",
        "NOT", "NEG", "JMP_NEAR", "JMP_NEAR_ABS_INDIRECT", "JMP_FAR",
        "JMP_FAR_ABS_INDIRECT", "LEA", "JXX", "JCXZ", "SETXX", "MOV_TO_SEG",
        "MOV_FROM_SEG", "CLI", "STI", "CLD", "STD", "STC", "CLC", "OUT", "IN",
        "INS", "OUTS", "LIDT", "SIDT", "LGDT", "SGDT", "SMSW", "LMSW",
        "RDTSCP", "INVLPG", "MOV_TO_CR", "MOV_FROM_CR", "MOV_TO_DR",
        "MOV_FROM_DR", "PUSHF", "POPF", "CPUID", "ROL", "ROR", "RCL", "RCR",
        "SHL", "SAL", "SHR", "SHRD", "SHLD", "SAR", "DIV", "IDIV", "MUL",
        "IMUL_3", "IMUL_2", "IMUL_1", "MOVS", "CMPS", "SCAS", "LODS", "STOS",
        "BSWAP", "XCHG", "RDTSC", "RDMSR", "WRMSR", "ENTER", "LEAVE", "BT",
        "BTS", "BTC", "BTR", "BSF", "BSR", "IRET", "INT", "POPA", "PUSHA",
        "CWD", "CBW", "DAS", "AAD", "AAM", "AAS", "LOOP", "SLDT", "STR", "LLDT",
        "LTR", "VERR", "VERW", "SAHF", "LAHF", "WBINVD", "LDS", "LSS", "LES",
        "LGS", "LFS", "CMC", "XLAT", "NOP", "CMOV", "CLTS", "XADD", "HLT",
        "CMPXCHG8B", "CMPXCHG", "POPCNT", "FNINIT", "FLD", "FLDxx", "FNSTCW",
        "FNSTSW", "FNSETPM", "FSAVE", "FRSTOR", "FXSAVE", "FXRSTOR", "FDIV",
        "FMUL", "FSUB", "FADD", "EMMS", "MFENCE", "SFENCE", "LFENCE",
        "PREFETCH", "FST", "FABS", "FUCOM", "FUCOMI", "FLDCW",
        "FXCH", "FCHS", "FCMOV", "FRNDINT", "FXAM", "LAST"};
    return cmds[cmd];
}

target_ulong decode_linear_addr(CPUX86State *env, struct x86_decode *decode,
                               target_ulong addr, X86Seg seg)
{
    switch (decode->segment_override) {
    case PREFIX_CS_SEG_OVEERIDE:
        seg = R_CS;
        break;
    case PREFIX_SS_SEG_OVEERIDE:
        seg = R_SS;
        break;
    case PREFIX_DS_SEG_OVEERIDE:
        seg = R_DS;
        break;
    case PREFIX_ES_SEG_OVEERIDE:
        seg = R_ES;
        break;
    case PREFIX_FS_SEG_OVEERIDE:
        seg = R_FS;
        break;
    case PREFIX_GS_SEG_OVEERIDE:
        seg = R_GS;
        break;
    default:
        break;
    }
    return linear_addr_size(ENV_GET_CPU(env), addr, decode->addressing_size, seg);
}
