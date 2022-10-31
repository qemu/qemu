/*
 * Decode table flags, mostly based on Intel SDM.
 *
 *  Copyright (c) 2022 Red Hat, Inc.
 *
 * Author: Paolo Bonzini <pbonzini@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

typedef enum X86OpType {
    X86_TYPE_None,

    X86_TYPE_A, /* Implicit */
    X86_TYPE_B, /* VEX.vvvv selects a GPR */
    X86_TYPE_C, /* REG in the modrm byte selects a control register */
    X86_TYPE_D, /* REG in the modrm byte selects a debug register */
    X86_TYPE_E, /* ALU modrm operand */
    X86_TYPE_F, /* EFLAGS/RFLAGS */
    X86_TYPE_G, /* REG in the modrm byte selects a GPR */
    X86_TYPE_H, /* For AVX, VEX.vvvv selects an XMM/YMM register */
    X86_TYPE_I, /* Immediate */
    X86_TYPE_J, /* Relative offset for a jump */
    X86_TYPE_L, /* The upper 4 bits of the immediate select a 128-bit register */
    X86_TYPE_M, /* modrm byte selects a memory operand */
    X86_TYPE_N, /* R/M in the modrm byte selects an MMX register */
    X86_TYPE_O, /* Absolute address encoded in the instruction */
    X86_TYPE_P, /* reg in the modrm byte selects an MMX register */
    X86_TYPE_Q, /* MMX modrm operand */
    X86_TYPE_R, /* R/M in the modrm byte selects a register */
    X86_TYPE_S, /* reg selects a segment register */
    X86_TYPE_U, /* R/M in the modrm byte selects an XMM/YMM register */
    X86_TYPE_V, /* reg in the modrm byte selects an XMM/YMM register */
    X86_TYPE_W, /* XMM/YMM modrm operand */
    X86_TYPE_X, /* string source */
    X86_TYPE_Y, /* string destination */

    /* Custom */
    X86_TYPE_WM, /* modrm byte selects an XMM/YMM memory operand */
    X86_TYPE_2op, /* 2-operand RMW instruction */
    X86_TYPE_LoBits, /* encoded in bits 0-2 of the operand + REX.B */
    X86_TYPE_0, /* Hard-coded GPRs (RAX..RDI) */
    X86_TYPE_1,
    X86_TYPE_2,
    X86_TYPE_3,
    X86_TYPE_4,
    X86_TYPE_5,
    X86_TYPE_6,
    X86_TYPE_7,
    X86_TYPE_ES, /* Hard-coded segment registers */
    X86_TYPE_CS,
    X86_TYPE_SS,
    X86_TYPE_DS,
    X86_TYPE_FS,
    X86_TYPE_GS,
} X86OpType;

typedef enum X86OpSize {
    X86_SIZE_None,

    X86_SIZE_a,  /* BOUND operand */
    X86_SIZE_b,  /* byte */
    X86_SIZE_d,  /* 32-bit */
    X86_SIZE_dq, /* SSE/AVX 128-bit */
    X86_SIZE_p,  /* Far pointer */
    X86_SIZE_pd, /* SSE/AVX packed double precision */
    X86_SIZE_pi, /* MMX */
    X86_SIZE_ps, /* SSE/AVX packed single precision */
    X86_SIZE_q,  /* 64-bit */
    X86_SIZE_qq, /* AVX 256-bit */
    X86_SIZE_s,  /* Descriptor */
    X86_SIZE_sd, /* SSE/AVX scalar double precision */
    X86_SIZE_ss, /* SSE/AVX scalar single precision */
    X86_SIZE_si, /* 32-bit GPR */
    X86_SIZE_v,  /* 16/32/64-bit, based on operand size */
    X86_SIZE_w,  /* 16-bit */
    X86_SIZE_x,  /* 128/256-bit, based on operand size */
    X86_SIZE_y,  /* 32/64-bit, based on operand size */
    X86_SIZE_z,  /* 16-bit for 16-bit operand size, else 32-bit */

    /* Custom */
    X86_SIZE_d64,
    X86_SIZE_f64,
    X86_SIZE_ph, /* SSE/AVX packed half precision */
} X86OpSize;

typedef enum X86CPUIDFeature {
    X86_FEAT_None,
    X86_FEAT_3DNOW,
    X86_FEAT_ADX,
    X86_FEAT_AES,
    X86_FEAT_AVX,
    X86_FEAT_AVX2,
    X86_FEAT_BMI1,
    X86_FEAT_BMI2,
    X86_FEAT_F16C,
    X86_FEAT_FMA,
    X86_FEAT_MOVBE,
    X86_FEAT_PCLMULQDQ,
    X86_FEAT_SSE,
    X86_FEAT_SSE2,
    X86_FEAT_SSE3,
    X86_FEAT_SSSE3,
    X86_FEAT_SSE41,
    X86_FEAT_SSE42,
    X86_FEAT_SSE4A,
} X86CPUIDFeature;

/* Execution flags */

typedef enum X86OpUnit {
    X86_OP_SKIP,    /* not valid or managed by emission function */
    X86_OP_SEG,     /* segment selector */
    X86_OP_CR,      /* control register */
    X86_OP_DR,      /* debug register */
    X86_OP_INT,     /* loaded into/stored from s->T0/T1 */
    X86_OP_IMM,     /* immediate */
    X86_OP_SSE,     /* address in either s->ptrX or s->A0 depending on has_ea */
    X86_OP_MMX,     /* address in either s->ptrX or s->A0 depending on has_ea */
} X86OpUnit;

typedef enum X86InsnSpecial {
    X86_SPECIAL_None,

    /* Always locked if it has a memory operand (XCHG) */
    X86_SPECIAL_Locked,

    /* Fault outside protected mode */
    X86_SPECIAL_ProtMode,

    /*
     * Register operand 0/2 is zero extended to 32 bits.  Rd/Mb or Rd/Mw
     * in the manual.
     */
    X86_SPECIAL_ZExtOp0,
    X86_SPECIAL_ZExtOp2,

    /*
     * Register operand 2 is extended to full width, while a memory operand
     * is doubled in size if VEX.L=1.
     */
    X86_SPECIAL_AVXExtMov,

    /*
     * MMX instruction exists with no prefix; if there is no prefix, V/H/W/U operands
     * become P/P/Q/N, and size "x" becomes "q".
     */
    X86_SPECIAL_MMX,

    /* Illegal or exclusive to 64-bit mode */
    X86_SPECIAL_i64,
    X86_SPECIAL_o64,
} X86InsnSpecial;

/*
 * Special cases for instructions that operate on XMM/YMM registers.  Intel
 * retconned all of them to have VEX exception classes other than 0 and 13, so
 * all these only matter for instructions that have a VEX exception class.
 * Based on tables in the "AVX and SSE Instruction Exception Specification"
 * section of the manual.
 */
typedef enum X86VEXSpecial {
    /* Legacy SSE instructions that allow unaligned operands */
    X86_VEX_SSEUnaligned,

    /*
     * Used for instructions that distinguish the XMM operand type with an
     * instruction prefix; legacy SSE encodings will allow unaligned operands
     * for scalar operands only (identified by a REP prefix).  In this case,
     * the decoding table uses "x" for the vector operands instead of specifying
     * pd/ps/sd/ss individually.
     */
    X86_VEX_REPScalar,

    /*
     * VEX instructions that only support 256-bit operands with AVX2 (Table 2-17
     * column 3).  Columns 2 and 4 (instructions limited to 256- and 127-bit
     * operands respectively) are implicit in the presence of dq and qq
     * operands, and thus handled by decode_op_size.
     */
    X86_VEX_AVX2_256,
} X86VEXSpecial;


typedef struct X86OpEntry  X86OpEntry;
typedef struct X86DecodedInsn X86DecodedInsn;

/* Decode function for multibyte opcodes.  */
typedef void (*X86DecodeFunc)(DisasContext *s, CPUX86State *env, X86OpEntry *entry, uint8_t *b);

/* Code generation function.  */
typedef void (*X86GenFunc)(DisasContext *s, CPUX86State *env, X86DecodedInsn *decode);

struct X86OpEntry {
    /* Based on the is_decode flags.  */
    union {
        X86GenFunc gen;
        X86DecodeFunc decode;
    };
    /* op0 is always written, op1 and op2 are always read.  */
    X86OpType    op0:8;
    X86OpSize    s0:8;
    X86OpType    op1:8;
    X86OpSize    s1:8;
    X86OpType    op2:8;
    X86OpSize    s2:8;
    /* Must be I and b respectively if present.  */
    X86OpType    op3:8;
    X86OpSize    s3:8;

    X86InsnSpecial special:8;
    X86CPUIDFeature cpuid:8;
    unsigned     vex_class:8;
    X86VEXSpecial vex_special:8;
    uint16_t     valid_prefix:16;
    bool         is_decode:1;
};

typedef struct X86DecodedOp {
    int8_t n;
    MemOp ot;     /* For b/c/d/p/s/q/v/w/y/z */
    X86OpUnit unit;
    bool has_ea;
    int offset;   /* For MMX and SSE */

    /*
     * This field is used internally by macros OP0_PTR/OP1_PTR/OP2_PTR,
     * do not access directly!
     */
    TCGv_ptr v_ptr;
} X86DecodedOp;

struct X86DecodedInsn {
    X86OpEntry e;
    X86DecodedOp op[3];
    target_ulong immediate;
    AddressParts mem;

    uint8_t b;
};

