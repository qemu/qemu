/*
 *  Simple LatticeMico32 disassembler.
 *
 *  Copyright (c) 2012 Michael Walle <michael@walle.cc>
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
 *
 */

#include "qemu/osdep.h"
#include "disas/bfd.h"

typedef enum {
    LM32_OP_SRUI = 0, LM32_OP_NORI, LM32_OP_MULI, LM32_OP_SH, LM32_OP_LB,
    LM32_OP_SRI, LM32_OP_XORI, LM32_OP_LH, LM32_OP_ANDI, LM32_OP_XNORI,
    LM32_OP_LW, LM32_OP_LHU, LM32_OP_SB, LM32_OP_ADDI, LM32_OP_ORI,
    LM32_OP_SLI, LM32_OP_LBU, LM32_OP_BE, LM32_OP_BG, LM32_OP_BGE,
    LM32_OP_BGEU, LM32_OP_BGU, LM32_OP_SW, LM32_OP_BNE, LM32_OP_ANDHI,
    LM32_OP_CMPEI, LM32_OP_CMPGI, LM32_OP_CMPGEI, LM32_OP_CMPGEUI,
    LM32_OP_CMPGUI, LM32_OP_ORHI, LM32_OP_CMPNEI, LM32_OP_SRU, LM32_OP_NOR,
    LM32_OP_MUL, LM32_OP_DIVU, LM32_OP_RCSR, LM32_OP_SR, LM32_OP_XOR,
    LM32_OP_ILL0, LM32_OP_AND, LM32_OP_XNOR, LM32_OP_ILL1, LM32_OP_SCALL,
    LM32_OP_SEXTB, LM32_OP_ADD, LM32_OP_OR, LM32_OP_SL, LM32_OP_B,
    LM32_OP_MODU, LM32_OP_SUB, LM32_OP_ILL2, LM32_OP_WCSR, LM32_OP_ILL3,
    LM32_OP_CALL, LM32_OP_SEXTH, LM32_OP_BI, LM32_OP_CMPE, LM32_OP_CMPG,
    LM32_OP_CMPGE, LM32_OP_CMPGEU, LM32_OP_CMPGU, LM32_OP_CALLI, LM32_OP_CMPNE,
} Lm32Opcode;

typedef enum {
    FMT_INVALID = 0, FMT_RRI5, FMT_RRI16, FMT_IMM26, FMT_LOAD, FMT_STORE,
    FMT_RRR, FMT_R, FMT_RNR, FMT_CRN, FMT_CNR, FMT_BREAK,
} Lm32OpcodeFmt;

typedef enum {
    LM32_CSR_IE = 0, LM32_CSR_IM, LM32_CSR_IP, LM32_CSR_ICC, LM32_CSR_DCC,
    LM32_CSR_CC, LM32_CSR_CFG, LM32_CSR_EBA, LM32_CSR_DC, LM32_CSR_DEBA,
    LM32_CSR_CFG2, LM32_CSR_JTX = 0xe, LM32_CSR_JRX, LM32_CSR_BP0,
    LM32_CSR_BP1, LM32_CSR_BP2, LM32_CSR_BP3, LM32_CSR_WP0 = 0x18,
    LM32_CSR_WP1, LM32_CSR_WP2, LM32_CSR_WP3,
} Lm32CsrNum;

typedef struct {
    int csr;
    const char *name;
} Lm32CsrInfo;

static const Lm32CsrInfo lm32_csr_info[] = {
    {LM32_CSR_IE,   "ie", },
    {LM32_CSR_IM,   "im", },
    {LM32_CSR_IP,   "ip", },
    {LM32_CSR_ICC,  "icc", },
    {LM32_CSR_DCC,  "dcc", },
    {LM32_CSR_CC,   "cc", },
    {LM32_CSR_CFG,  "cfg", },
    {LM32_CSR_EBA,  "eba", },
    {LM32_CSR_DC,   "dc", },
    {LM32_CSR_DEBA, "deba", },
    {LM32_CSR_CFG2, "cfg2", },
    {LM32_CSR_JTX,  "jtx", },
    {LM32_CSR_JRX,  "jrx", },
    {LM32_CSR_BP0,  "bp0", },
    {LM32_CSR_BP1,  "bp1", },
    {LM32_CSR_BP2,  "bp2", },
    {LM32_CSR_BP3,  "bp3", },
    {LM32_CSR_WP0,  "wp0", },
    {LM32_CSR_WP1,  "wp1", },
    {LM32_CSR_WP2,  "wp2", },
    {LM32_CSR_WP3,  "wp3", },
};

static const Lm32CsrInfo *find_csr_info(int csr)
{
    const Lm32CsrInfo *info;
    int i;

    for (i = 0; i < ARRAY_SIZE(lm32_csr_info); i++) {
        info = &lm32_csr_info[i];
        if (csr == info->csr) {
            return info;
        }
    }

    return NULL;
}

typedef struct {
    int reg;
    const char *name;
} Lm32RegInfo;

typedef enum {
    LM32_REG_R0 = 0, LM32_REG_R1, LM32_REG_R2, LM32_REG_R3, LM32_REG_R4,
    LM32_REG_R5, LM32_REG_R6, LM32_REG_R7, LM32_REG_R8, LM32_REG_R9,
    LM32_REG_R10, LM32_REG_R11, LM32_REG_R12, LM32_REG_R13, LM32_REG_R14,
    LM32_REG_R15, LM32_REG_R16, LM32_REG_R17, LM32_REG_R18, LM32_REG_R19,
    LM32_REG_R20, LM32_REG_R21, LM32_REG_R22, LM32_REG_R23, LM32_REG_R24,
    LM32_REG_R25, LM32_REG_GP, LM32_REG_FP, LM32_REG_SP, LM32_REG_RA,
    LM32_REG_EA, LM32_REG_BA,
} Lm32RegNum;

static const Lm32RegInfo lm32_reg_info[] = {
    {LM32_REG_R0,  "r0", },
    {LM32_REG_R1,  "r1", },
    {LM32_REG_R2,  "r2", },
    {LM32_REG_R3,  "r3", },
    {LM32_REG_R4,  "r4", },
    {LM32_REG_R5,  "r5", },
    {LM32_REG_R6,  "r6", },
    {LM32_REG_R7,  "r7", },
    {LM32_REG_R8,  "r8", },
    {LM32_REG_R9,  "r9", },
    {LM32_REG_R10, "r10", },
    {LM32_REG_R11, "r11", },
    {LM32_REG_R12, "r12", },
    {LM32_REG_R13, "r13", },
    {LM32_REG_R14, "r14", },
    {LM32_REG_R15, "r15", },
    {LM32_REG_R16, "r16", },
    {LM32_REG_R17, "r17", },
    {LM32_REG_R18, "r18", },
    {LM32_REG_R19, "r19", },
    {LM32_REG_R20, "r20", },
    {LM32_REG_R21, "r21", },
    {LM32_REG_R22, "r22", },
    {LM32_REG_R23, "r23", },
    {LM32_REG_R24, "r24", },
    {LM32_REG_R25, "r25", },
    {LM32_REG_GP,  "gp", },
    {LM32_REG_FP,  "fp", },
    {LM32_REG_SP,  "sp", },
    {LM32_REG_RA,  "ra", },
    {LM32_REG_EA,  "ea", },
    {LM32_REG_BA,  "ba", },
};

static const Lm32RegInfo *find_reg_info(int reg)
{
    assert(ARRAY_SIZE(lm32_reg_info) == 32);
    return &lm32_reg_info[reg & 0x1f];
}

typedef struct {
    struct {
        uint32_t code;
        uint32_t mask;
    } op;
    const char *name;
    const char *args_fmt;
} Lm32OpcodeInfo;

static const Lm32OpcodeInfo lm32_opcode_info[] = {
    /* pseudo instructions */
    {{0x34000000, 0xffffffff}, "nop",   NULL},
    {{0xac000002, 0xffffffff}, "break", NULL},
    {{0xac000003, 0xffffffff}, "scall", NULL},
    {{0xc3e00000, 0xffffffff}, "bret",  NULL},
    {{0xc3c00000, 0xffffffff}, "eret",  NULL},
    {{0xc3a00000, 0xffffffff}, "ret",   NULL},
    {{0xa4000000, 0xfc1f07ff}, "not",   "%2, %0"},
    {{0xb8000000, 0xfc1f07ff}, "mv",    "%2, %0"},
    {{0x71e00000, 0xffe00000}, "mvhi",  "%1, %u"},
    {{0x34000000, 0xffe00000}, "mvi",   "%1, %s"},

#define _O(op) {op << 26, 0x3f << 26}
    /* regular opcodes */
    {_O(LM32_OP_ADD),     "add",     "%2, %0, %1"  },
    {_O(LM32_OP_ADDI),    "addi",    "%1, %0, %s"  },
    {_O(LM32_OP_AND),     "and",     "%2, %0, %1"  },
    {_O(LM32_OP_ANDHI),   "andhi",   "%1, %0, %u"  },
    {_O(LM32_OP_ANDI),    "andi",    "%1, %0, %u"  },
    {_O(LM32_OP_B),       "b",       "%0",         },
    {_O(LM32_OP_BE),      "be",      "%1, %0, %r"  },
    {_O(LM32_OP_BG),      "bg",      "%1, %0, %r"  },
    {_O(LM32_OP_BGE),     "bge",     "%1, %0, %r"  },
    {_O(LM32_OP_BGEU),    "bgeu",    "%1, %0, %r"  },
    {_O(LM32_OP_BGU),     "bgu",     "%1, %0, %r"  },
    {_O(LM32_OP_BI),      "bi",      "%R",         },
    {_O(LM32_OP_BNE),     "bne",     "%1, %0, %r"  },
    {_O(LM32_OP_CALL),    "call",    "%0",         },
    {_O(LM32_OP_CALLI),   "calli",   "%R",         },
    {_O(LM32_OP_CMPE),    "cmpe",    "%2, %0, %1"  },
    {_O(LM32_OP_CMPEI),   "cmpei",   "%1, %0, %s"  },
    {_O(LM32_OP_CMPG),    "cmpg",    "%2, %0, %1"  },
    {_O(LM32_OP_CMPGE),   "cmpge",   "%2, %0, %1"  },
    {_O(LM32_OP_CMPGEI),  "cmpgei",  "%1, %0, %s"  },
    {_O(LM32_OP_CMPGEU),  "cmpgeu",  "%2, %0, %1"  },
    {_O(LM32_OP_CMPGEUI), "cmpgeui", "%1, %0, %s"  },
    {_O(LM32_OP_CMPGI),   "cmpgi",   "%1, %0, %s"  },
    {_O(LM32_OP_CMPGU),   "cmpgu",   "%2, %0, %1"  },
    {_O(LM32_OP_CMPGUI),  "cmpgui",  "%1, %0, %s"  },
    {_O(LM32_OP_CMPNE),   "cmpne",   "%2, %0, %1"  },
    {_O(LM32_OP_CMPNEI),  "cmpnei",  "%1, %0, %s"  },
    {_O(LM32_OP_DIVU),    "divu",    "%2, %0, %1"  },
    {_O(LM32_OP_LB),      "lb",      "%1, (%0+%s)" },
    {_O(LM32_OP_LBU),     "lbu",     "%1, (%0+%s)" },
    {_O(LM32_OP_LH),      "lh",      "%1, (%0+%s)" },
    {_O(LM32_OP_LHU),     "lhu",     "%1, (%0+%s)" },
    {_O(LM32_OP_LW),      "lw",      "%1, (%0+%s)" },
    {_O(LM32_OP_MODU),    "modu",    "%2, %0, %1"  },
    {_O(LM32_OP_MULI),    "muli",    "%1, %0, %s"  },
    {_O(LM32_OP_MUL),     "mul",     "%2, %0, %1"  },
    {_O(LM32_OP_NORI),    "nori",    "%1, %0, %u"  },
    {_O(LM32_OP_NOR),     "nor",     "%2, %0, %1"  },
    {_O(LM32_OP_ORHI),    "orhi",    "%1, %0, %u"  },
    {_O(LM32_OP_ORI),     "ori",     "%1, %0, %u"  },
    {_O(LM32_OP_OR),      "or",      "%2, %0, %1"  },
    {_O(LM32_OP_RCSR),    "rcsr",    "%2, %c",     },
    {_O(LM32_OP_SB),      "sb",      "(%0+%s), %1" },
    {_O(LM32_OP_SEXTB),   "sextb",   "%2, %0",     },
    {_O(LM32_OP_SEXTH),   "sexth",   "%2, %0",     },
    {_O(LM32_OP_SH),      "sh",      "(%0+%s), %1" },
    {_O(LM32_OP_SLI),     "sli",     "%1, %0, %h"  },
    {_O(LM32_OP_SL),      "sl",      "%2, %0, %1"  },
    {_O(LM32_OP_SRI),     "sri",     "%1, %0, %h"  },
    {_O(LM32_OP_SR),      "sr",      "%2, %0, %1"  },
    {_O(LM32_OP_SRUI),    "srui",    "%1, %0, %d"  },
    {_O(LM32_OP_SRU),     "sru",     "%2, %0, %s"  },
    {_O(LM32_OP_SUB),     "sub",     "%2, %0, %s"  },
    {_O(LM32_OP_SW),      "sw",      "(%0+%s), %1" },
    {_O(LM32_OP_WCSR),    "wcsr",    "%c, %1",     },
    {_O(LM32_OP_XNORI),   "xnori",   "%1, %0, %u"  },
    {_O(LM32_OP_XNOR),    "xnor",    "%2, %0, %1"  },
    {_O(LM32_OP_XORI),    "xori",    "%1, %0, %u"  },
    {_O(LM32_OP_XOR),     "xor",     "%2, %0, %1"  },
#undef _O
};

static const Lm32OpcodeInfo *find_opcode_info(uint32_t opcode)
{
    const Lm32OpcodeInfo *info;
    int i;
    for (i = 0; i < ARRAY_SIZE(lm32_opcode_info); i++) {
        info = &lm32_opcode_info[i];
        if ((opcode & info->op.mask) == info->op.code) {
            return info;
        }
    }

    return NULL;
}

int print_insn_lm32(bfd_vma memaddr, struct disassemble_info *info)
{
    fprintf_function fprintf_fn = info->fprintf_func;
    void *stream = info->stream;
    int rc;
    uint8_t insn[4];
    const Lm32OpcodeInfo *opc_info;
    uint32_t op;
    const char *args_fmt;

    rc = info->read_memory_func(memaddr, insn, 4, info);
    if (rc != 0) {
        info->memory_error_func(rc, memaddr, info);
        return -1;
    }

    fprintf_fn(stream, "%02x %02x %02x %02x    ",
            insn[0], insn[1], insn[2], insn[3]);

    op = bfd_getb32(insn);
    opc_info = find_opcode_info(op);
    if (opc_info) {
        fprintf_fn(stream, "%-8s ", opc_info->name);
        args_fmt = opc_info->args_fmt;
        while (args_fmt && *args_fmt) {
            if (*args_fmt == '%') {
                switch (*(++args_fmt)) {
                case '0': {
                    uint8_t r0;
                    const char *r0_name;
                    r0 = (op >> 21) & 0x1f;
                    r0_name = find_reg_info(r0)->name;
                    fprintf_fn(stream, "%s", r0_name);
                    break;
                }
                case '1': {
                    uint8_t r1;
                    const char *r1_name;
                    r1 = (op >> 16) & 0x1f;
                    r1_name = find_reg_info(r1)->name;
                    fprintf_fn(stream, "%s", r1_name);
                    break;
                }
                case '2': {
                    uint8_t r2;
                    const char *r2_name;
                    r2 = (op >> 11) & 0x1f;
                    r2_name = find_reg_info(r2)->name;
                    fprintf_fn(stream, "%s", r2_name);
                    break;
                }
                case 'c': {
                    uint8_t csr;
                    const Lm32CsrInfo *info;
                    csr = (op >> 21) & 0x1f;
                    info = find_csr_info(csr);
                    if (info) {
                        fprintf_fn(stream, "%s", info->name);
                    } else {
                        fprintf_fn(stream, "0x%x", csr);
                    }
                    break;
                }
                case 'u': {
                    uint16_t u16;
                    u16 = op & 0xffff;
                    fprintf_fn(stream, "0x%x", u16);
                    break;
                }
                case 's': {
                    int16_t s16;
                    s16 = (int16_t)(op & 0xffff);
                    fprintf_fn(stream, "%d", s16);
                    break;
                }
                case 'r': {
                    uint32_t rela;
                    rela = memaddr + (((int16_t)(op & 0xffff)) << 2);
                    fprintf_fn(stream, "%x", rela);
                    break;
                }
                case 'R': {
                    uint32_t rela;
                    int32_t imm26;
                    imm26 = (int32_t)((op & 0x3ffffff) << 6) >> 4;
                    rela = memaddr + imm26;
                    fprintf_fn(stream, "%x", rela);
                    break;
                }
                case 'h': {
                    uint8_t u5;
                    u5 = (op & 0x1f);
                    fprintf_fn(stream, "%d", u5);
                    break;
                }
                default:
                    break;
                }
            } else {
                fprintf_fn(stream, "%c", *args_fmt);
            }
            args_fmt++;
        }
    } else {
        fprintf_fn(stream, ".word 0x%x", op);
    }

    return 4;
}
