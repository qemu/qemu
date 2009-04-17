/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2009 Ulrich Hecht <uli@suse.de>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "%r0",
    "%r1",
    "%r2",
    "%r3",
    "%r4",
    "%r5",
    "%r6",
    "%r7",
    "%r8",
    "%r9",
    "%r10",
    "%r11",
    "%r12",
    "%r13",
    "%r14",
    "%r15"
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
    TCG_REG_R11,
    TCG_REG_R12,
    TCG_REG_R13,
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R0,
    TCG_REG_R1,
    TCG_REG_R2,
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
};

static const int tcg_target_call_iarg_regs[4] = {
    TCG_REG_R2, TCG_REG_R3, TCG_REG_R4, TCG_REG_R5
};
static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_R2, TCG_REG_R3
};

static void patch_reloc(uint8_t *code_ptr, int type,
                tcg_target_long value, tcg_target_long addend)
{
    switch (type) {
    case R_390_PC32DBL:
        //fprintf(stderr,"patching %p to 0x%lx (0x%lx)\n", code_ptr, value, (value - ((tcg_target_long)code_ptr + addend)) >> 1);
        *(uint32_t*)code_ptr = (value - ((tcg_target_long)code_ptr + addend)) >> 1;
        break;
    default:
        tcg_abort();
        break;
    }
}

/* maximum number of register used for input function arguments */
static inline int tcg_target_get_call_iarg_regs_count(int flags)
{
    return 4;
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;
    
    ct->ct |= TCG_CT_REG;
    tcg_regset_set32(ct->u.regs, 0, 0xffff);
    ct_str = *pct_str;
    switch(ct_str[0]) {
    case 'L':                   /* qemu_ld constraint */
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R2);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R3);
#endif
        break;
    case 'S':                   /* qemu_st constraint */
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R2);
#ifdef CONFIG_SOFTMMU
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R3);
        tcg_regset_reset_reg (ct->u.regs, TCG_REG_R4);
#endif
        break;
    case 'R':			/* not R0 */
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R0);
        break;
    default:
        break;
    }
    ct_str++;
    *pct_str = ct_str;

    return 0;
}

/* Test if a constant matches the constraint. */
/* No idea what to do here, just eat everything. */
static inline int tcg_target_const_match(tcg_target_long val,
                const TCGArgConstraint *arg_ct)
{
    int ct;
    //fprintf(stderr,"tcg_target_const_match %ld ct %d\n",val,arg_ct->ct);
    ct = arg_ct->ct;
    if (ct & TCG_CT_CONST)
        return 1;
    else
        return 0;
}

#ifdef CONFIG_SOFTMMU

#include "../../softmmu_defs.h"

static void *qemu_ld_helpers[4] = {
    __ldb_mmu,
    __ldw_mmu,
    __ldl_mmu,
    __ldq_mmu,
};

static void *qemu_st_helpers[4] = {
    __stb_mmu,
    __stw_mmu,
    __stl_mmu,
    __stq_mmu,
};
#endif

static uint8_t *tb_ret_addr;

/* signed/unsigned is handled by using COMPARE and COMPARE LOGICAL,
   respectively */
static const uint8_t tcg_cond_to_s390_cond[10] = {
    [TCG_COND_EQ] = 8,
    [TCG_COND_LT] = 4,
    [TCG_COND_LTU] = 4,
    [TCG_COND_LE] = 8 | 4,
    [TCG_COND_LEU] = 8 | 4,
    [TCG_COND_GT] = 2,
    [TCG_COND_GTU] = 2,
    [TCG_COND_GE] = 8 | 2,
    [TCG_COND_GEU] = 8 | 2,
    [TCG_COND_NE] = 4 | 2 | 1,
};

/* emit load/store (and then some) instructions (E3 prefix) */
static inline void tcg_out_e3(TCGContext* s, int op, int r1, int r2, int disp)
{
    tcg_out16(s, 0xe300 | (r1 << 4));
    tcg_out32(s, op | (r2 << 28) | ((disp & 0xfff) << 16) | ((disp >> 12) << 8));
}
#define E3_LG	  0x04
#define E3_LRVG	  0x0f
#define E3_LGF	  0x14
#define E3_LGH	  0x15
#define E3_LLGF   0x16
#define E3_LRV	  0x1e
#define E3_LRVH   0x1f
#define E3_CG	  0x20
#define E3_STG    0x24
#define E3_STRVG  0x2f
#define E3_STRV   0x3e
#define E3_STRVH  0x3f
#define E3_STHY   0x70
#define E3_STCY   0x72
#define E3_LGB	  0x77
#define E3_LLGC   0x90
#define E3_LLGH   0x91

/* emit 64-bit register/register insns (B9 prefix) */
static inline void tcg_out_b9(TCGContext* s, int op, int r1, int r2)
{
    tcg_out32(s, 0xb9000000 | (op << 16) | (r1 << 4) | r2);
}
#define B9_LGR   0x04
#define B9_AGR   0x08
#define B9_MSGR  0x0c
#define B9_LGFR  0x14
#define B9_LLGFR 0x16
#define B9_NGR   0x80
#define B9_OGR	 0x81
#define B9_XGR   0x82

/* emit (mostly) 32-bit register/register insns */
static inline void tcg_out_rr(TCGContext* s, int op, int r1, int r2)
{
    tcg_out16(s, (op << 8) | (r1 << 4) | r2);
}
#define RR_BASR 0x0d
#define RR_NR   0x14
#define RR_CLR	0x15
#define RR_OR	0x16
#define RR_XR   0x17
#define RR_LR   0x18
#define RR_CR	0x19
#define RR_AR	0x1a
#define RR_SR	0x1b

/* emit 64-bit shifts (EB prefix) */
static inline void tcg_out_sh64(TCGContext* s, int op, int r0, int r1, int r2, int imm)
{
    tcg_out16(s, 0xeb00 | (r0 << 4) | r1);
    tcg_out32(s, op | (r2 << 28) | ((imm & 0xfff) << 16) | ((imm >> 12) << 8));
}
#define SH64_REG_NONE 0 /* use immediate only (not R0!) */
#define SH64_SRAG 0x0a
#define SH64_SRLG 0x0c
#define SH64_SLLG 0x0d

/* emit 32-bit shifts */
static inline void tcg_out_sh32(TCGContext* s, int op, int r0, int r1, int imm)
{
    tcg_out32(s, 0x80000000 | (op << 24) | (r0 << 20) | (r1 << 12) | imm);
}
#define SH32_REG_NONE 0 /* use immediate only (not R0!) */
#define SH32_SRL 0x8
#define SH32_SLL 0x9
#define SH32_SRA 0xa

/* branch to relative address (long) */
static inline void tcg_out_brasl(TCGContext* s, int r, tcg_target_long raddr)
{
    tcg_out16(s, 0xc005 | (r << 4));
    tcg_out32(s, raddr >> 1);
}

/* store 8/16/32 bits */
static inline void tcg_out_store(TCGContext* s, int op, int r0, int r1, int off)
{
    tcg_out32(s, (op << 24) | (r0 << 20) | (r1 << 12) | off);
}
#define ST_STH 0x40
#define ST_STC 0x42
#define ST_ST  0x50

/* load a register with an immediate value */
static inline void tcg_out_movi(TCGContext *s, TCGType type,
                int ret, tcg_target_long arg)
{
    //fprintf(stderr,"tcg_out_movi ret 0x%x arg 0x%lx\n",ret,arg);
    if(arg >= -0x8000 && arg < 0x8000) { /* signed immediate load */
        /* lghi %rret, arg */
        tcg_out32(s, 0xa7090000 | (ret << 20) | (arg & 0xffff));
    }
    else if (!(arg & 0xffffffffffff0000UL)) {
        /* llill %rret, arg */
        tcg_out32(s, 0xa50f0000 | (ret << 20) | arg);
    }
    else if (!(arg & 0xffffffff00000000UL)) {
        /* llill %rret, arg */
        tcg_out32(s, 0xa50f0000 | (ret << 20) | (arg & 0xffff));
        /* iilh %rret, arg */
        tcg_out32(s, 0xa5020000 | (ret << 20) | (arg >> 16));
    }
    else {
        /* branch over constant and store its address in R13 */
        tcg_out_brasl(s, TCG_REG_R13, 14);
        /* 64-bit constant */
        tcg_out32(s,arg >> 32);
        tcg_out32(s,arg);
        /* load constant to ret */
        tcg_out_e3(s, E3_LG, ret, TCG_REG_R13, 0);
    }
}

/* load data without address translation or endianness conversion */
static inline void tcg_out_ld(TCGContext *s, TCGType type, int arg,
                int arg1, tcg_target_long arg2)
{
    int op;
    //fprintf(stderr,"tcg_out_ld type %d arg %d arg1 %d arg2 %ld\n",type,arg,arg1,arg2);
    
    if (type == TCG_TYPE_I32) op = E3_LLGF;	/* 32-bit zero-extended */
    else op = E3_LG;				/* 64-bit */
    
    if(arg2 < -0x80000 || arg2 > 0x7ffff) {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, arg2);	/* load the displacement */
        tcg_out_b9(s, B9_AGR, TCG_REG_R13, arg1);		/* add the address */
        tcg_out_e3(s, op, arg, TCG_REG_R13, 0);			/* load the data */
    }
    else {
        tcg_out_e3(s, op, arg, arg1, arg2);		/* load the data */
    }
}

/* load data with address translation (if applicable) and endianness conversion */
static void tcg_out_qemu_ld(TCGContext* s, const TCGArg* args, int opc)
{
    int addr_reg, data_reg, mem_index, s_bits;
#if defined(CONFIG_SOFTMMU)
    uint16_t *label1_ptr, *label2_ptr;
#endif
        
    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;
    
    s_bits = opc & 3;
    
    int arg0 = TCG_REG_R2;
#ifdef CONFIG_SOFTMMU
    int arg1 = TCG_REG_R3;
#endif
    
    /* fprintf(stderr,"tcg_out_qemu_ld opc %d data_reg %d addr_reg %d mem_index %d s_bits %d\n",
            opc, data_reg, addr_reg, mem_index, s_bits); */
    
#ifdef CONFIG_SOFTMMU
    tcg_out_b9(s, B9_LGR, arg1, addr_reg);
    tcg_out_b9(s, B9_LGR, arg0, addr_reg);
    
    tcg_out_sh64(s, SH64_SRLG, arg1, addr_reg, SH64_REG_NONE, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tcg_out_b9(s, B9_NGR, arg0, TCG_REG_R13);
    
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    tcg_out_b9(s, B9_NGR, arg1, TCG_REG_R13);

    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, offsetof(CPUState, tlb_table[mem_index][0].addr_read));
    tcg_out_b9(s, B9_AGR, arg1, TCG_REG_R13);
    
    tcg_out_b9(s, B9_AGR, arg1, TCG_AREG0);
    
    tcg_out_e3(s, E3_CG, arg0, arg1, 0);
    
    label1_ptr = (uint16_t*)s->code_ptr;
    tcg_out32(s, 0xa7840000); /* je label1 (offset will be patched in later) */
    
    /* call load helper */
    tcg_out_b9(s, B9_LGR, arg0, addr_reg);
    tcg_out_movi(s, TCG_TYPE_I32, arg1, mem_index);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, (tcg_target_ulong)qemu_ld_helpers[s_bits]);
    tcg_out_rr(s, RR_BASR, TCG_REG_R14, TCG_REG_R13);

    /* sign extension */
    switch(opc) {
        case 0 | 4:
            tcg_out_sh64(s, SH64_SLLG, data_reg, arg0, SH64_REG_NONE, 56);
            tcg_out_sh64(s, SH64_SRAG, data_reg, data_reg, SH64_REG_NONE, 56);
            break;
        case 1 | 4:
	    tcg_out_sh64(s, SH64_SLLG, data_reg, arg0, SH64_REG_NONE, 48);
            tcg_out_sh64(s, SH64_SRAG, data_reg, data_reg, SH64_REG_NONE, 48);
            break;
        case 2 | 4:
            tcg_out_b9(s, B9_LGFR, data_reg, arg0);
            break;
        case 0: case 1: case 2: case 3: default:
            /* unsigned -> just copy */
            tcg_out_b9(s, B9_LGR, data_reg, arg0);
            break;
    }
    
    /* jump to label2 (end) */
    label2_ptr = (uint16_t*)s->code_ptr;
    tcg_out32(s, 0xa7d50000); /* bras %r13, label2 */
    
    /* this is label1, patch branch */
    *(label1_ptr + 1) = ((unsigned long)s->code_ptr - (unsigned long)label1_ptr) >> 1;
    
    tcg_out_e3(s, E3_LG, arg1, arg1, offsetof(CPUTLBEntry, addend) - offsetof(CPUTLBEntry, addr_read));

#if TARGET_LONG_BITS == 32
    /* zero upper 32 bits */
    tcg_out_b9(s, B9_LLGFR, arg0, addr_reg);
#else
    /* just copy */
    tcg_out_b9(s, B9_LGR, arg0, addr_reg);
#endif
    tcg_out_b9(s, B9_AGR, arg0, arg1);

#else /* CONFIG_SOFTMMU */
    /* user mode, no address translation required */
    arg0 = addr_reg;
#endif

    switch(opc) {
        case 0:	/* unsigned byte */
            tcg_out_e3(s, E3_LLGC, data_reg, arg0, 0);
            break;
        case 0 | 4:	/* signed byte */
            tcg_out_e3(s, E3_LGB, data_reg, arg0, 0);
            break;
        case 1:	/* unsigned short */
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_e3(s, E3_LLGH, data_reg, arg0, 0);
#else
            /* swapped unsigned halfword load with upper bits zeroed */
            tcg_out_e3(s, E3_LRVH, data_reg, arg0, 0);
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, 0xffffL);
            tcg_out_b9(s, B9_NGR, data_reg, 13);
#endif
            break;
        case 1 | 4:	/* signed short */
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_e3(s, E3_LGH, data_reg, arg0, 0);
#else
            /* swapped sign-extended halfword load */
            tcg_out_e3(s, E3_LRVH, data_reg, arg0, 0);
            tcg_out_sh64(s, SH64_SLLG, data_reg, data_reg, SH64_REG_NONE, 48);
            tcg_out_sh64(s, SH64_SRAG, data_reg, data_reg, SH64_REG_NONE, 48);
#endif
            break;
        case 2:	/* unsigned int */
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_e3(s, E3_LLGF, data_reg, arg0, 0);
#else
            /* swapped unsigned int load with upper bits zeroed */
            tcg_out_e3(s, E3_LRV, data_reg, arg0, 0);
            tcg_out_b9(s, B9_LLGFR, data_reg, data_reg);
#endif
            break;
        case 2 | 4: /* signed int */
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_e3(s, E3_LGF, data_reg, arg0, 0);
#else
            /* swapped sign-extended int load */
            tcg_out_e3(s, E3_LRV, data_reg, arg0, 0);
            tcg_out_b9(s, B9_LGFR, data_reg, data_reg);
#endif
            break;
        case 3:	/* long (64 bit) */
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_e3(s, E3_LG, data_reg, arg0, 0);
#else
            tcg_out_e3(s, E3_LRVG, data_reg, arg0, 0);
#endif
            break;
        default:
            tcg_abort();
    }
    
#ifdef CONFIG_SOFTMMU
    /* this is label2, patch branch */
    *(label2_ptr + 1) = ((unsigned long)s->code_ptr - (unsigned long)label2_ptr) >> 1;
#endif
}

static void tcg_out_qemu_st(TCGContext* s, const TCGArg* args, int opc)
{
    int addr_reg, data_reg, mem_index, s_bits;
#if defined(CONFIG_SOFTMMU)
    uint16_t *label1_ptr, *label2_ptr;
#endif
        
    data_reg = *args++;
    addr_reg = *args++;
    mem_index = *args;
    
    s_bits = opc;
    
    int arg0 = TCG_REG_R2;
#ifdef CONFIG_SOFTMMU
    int arg1 = TCG_REG_R3;
    int arg2 = TCG_REG_R4;
#endif
    
    /* fprintf(stderr,"tcg_out_qemu_st opc %d data_reg %d addr_reg %d mem_index %d s_bits %d\n",
            opc, data_reg, addr_reg, mem_index, s_bits); */
    
#ifdef CONFIG_SOFTMMU
    tcg_out_b9(s, B9_LGR, arg1, addr_reg);
    tcg_out_b9(s, B9_LGR, arg0, addr_reg);
    
    tcg_out_sh64(s, SH64_SRLG, arg1, addr_reg, SH64_REG_NONE, TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS);
    
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, TARGET_PAGE_MASK | ((1 << s_bits) - 1));
    tcg_out_b9(s, B9_NGR, arg0, TCG_REG_R13);
    
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, (CPU_TLB_SIZE - 1) << CPU_TLB_ENTRY_BITS);
    tcg_out_b9(s, B9_NGR, arg1, TCG_REG_R13);

    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, offsetof(CPUState, tlb_table[mem_index][0].addr_write));
    tcg_out_b9(s, B9_AGR, arg1, TCG_REG_R13);
    
    tcg_out_b9(s, B9_AGR, arg1, TCG_AREG0);
    
    tcg_out_e3(s, E3_CG, arg0, arg1, 0);
    
    tcg_out_b9(s, B9_LGR, arg0, addr_reg);
    
    /* jump to label1 */
    label1_ptr = (uint16_t*)s->code_ptr;
    tcg_out32(s, 0xa7840000); /* je label1 */
    
    /* call store helper */
    tcg_out_b9(s, B9_LGR, arg1, data_reg);
    tcg_out_movi(s, TCG_TYPE_I32, arg2, mem_index);
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, (tcg_target_ulong)qemu_st_helpers[s_bits]);
    tcg_out_rr(s, RR_BASR, TCG_REG_R14, TCG_REG_R13);

    /* jump to label2 (end) */
    label2_ptr = (uint16_t*)s->code_ptr;
    tcg_out32(s, 0xa7d50000); /* bras %r13, label2 */
    
    /* this is label1, patch branch */
    *(label1_ptr + 1) = ((unsigned long)s->code_ptr - (unsigned long)label1_ptr) >> 1;
    
    tcg_out_e3(s, E3_LG, arg1, arg1, offsetof(CPUTLBEntry, addend) - offsetof(CPUTLBEntry, addr_write));
    
#if TARGET_LONG_BITS == 32
    /* zero upper 32 bits */
    tcg_out_b9(s, B9_LLGFR, arg0, addr_reg);
#else
    /* just copy */
    tcg_out_b9(s, B9_LGR, arg0, addr_reg);
#endif
    tcg_out_b9(s, B9_AGR, arg0, arg1);

#else /* CONFIG_SOFTMMU */
    /* user mode, no address translation required */
    arg0 = addr_reg;
#endif

    switch(opc) {
        case 0:
            tcg_out_store(s, ST_STC, data_reg, arg0, 0);
            break;
        case 1:
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_store(s, ST_STH, data_reg, arg0, 0);
#else
            tcg_out_e3(s, E3_STRVH, data_reg, arg0, 0);
#endif
            break;
        case 2:
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_store(s, ST_ST, data_reg, arg0, 0);
#else
            tcg_out_e3(s, E3_STRV, data_reg, arg0, 0);
#endif
            break;
        case 3:
#ifdef TARGET_WORDS_BIGENDIAN
            tcg_out_e3(s, E3_STG, data_reg, arg0, 0);
#else
            tcg_out_e3(s, E3_STRVG, data_reg, arg0, 0);
#endif
            break;
        default:
            tcg_abort();
    }
    
#ifdef CONFIG_SOFTMMU
    /* this is label2, patch branch */
    *(label2_ptr + 1) = ((unsigned long)s->code_ptr - (unsigned long)label2_ptr) >> 1;
#endif
}

static inline void tcg_out_st(TCGContext *s, TCGType type, int arg,
                              int arg1, tcg_target_long arg2)
{
    //fprintf(stderr,"tcg_out_st arg 0x%x arg1 0x%x arg2 0x%lx\n",arg,arg1,arg2);
    if (type == TCG_TYPE_I32) {
        if(((long)arg2) < -0x800 || ((long)arg2) > 0x7ff) {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, arg2);
            tcg_out_b9(s, B9_AGR, 13, arg1);
            tcg_out_store(s, ST_ST, arg, TCG_REG_R13, 0);
        }
        else tcg_out_store(s, ST_ST, arg, arg1, arg2);
    }
    else {
        if(((long)arg2) < -0x80000 || ((long)arg2) > 0x7ffff) tcg_abort();
        tcg_out_e3(s, E3_STG, arg, arg1, arg2);
        tcg_abort(); // untested
    }
}

static inline void tcg_out_op(TCGContext *s, int opc,
                const TCGArg *args, const int *const_args)
{
    TCGLabel* l;
    int op;
    int op2;
    switch (opc) {
    case INDEX_op_exit_tb:
        //fprintf(stderr,"op 0x%x exit_tb 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, args[0]);	/* return value */
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, (unsigned long)tb_ret_addr);
        tcg_out16(s,0x7fd); /* br %r13 */
        break;
    case INDEX_op_goto_tb:
        //fprintf(stderr,"op 0x%x goto_tb 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if(s->tb_jmp_offset) {
            tcg_abort();
        }
        else {
            tcg_target_long off = ((tcg_target_long)(s->tb_next + args[0]) - (tcg_target_long)s->code_ptr) >> 1;
            if (off > -0x80000000L && off < 0x7fffffffL) { /* load address relative to PC */
                /* larl %r13, off */
                tcg_out16(s,0xc0d0); tcg_out32(s,off);
            }
            else { /* too far for larl */
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, (tcg_target_long)(s->tb_next + args[0]));
            }
            tcg_out_ld(s, TCG_TYPE_PTR, TCG_REG_R13, TCG_REG_R13, 0);   /* load address stored at s->tb_next + args[0] */
            tcg_out_rr(s, RR_BASR, TCG_REG_R13, TCG_REG_R13);		/* and go there */
        }
        s->tb_next_offset[args[0]] = s->code_ptr - s->code_buf;
        break;
    case INDEX_op_call:
        //fprintf(stderr,"op 0x%x call 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if(const_args[0]) {
            tcg_target_long off = (args[0] - (tcg_target_long)s->code_ptr + 4) >> 1; /* FIXME: + 4? Where did that come from? */
            if (off > -0x80000000 && off < 0x7fffffff) { /* relative call */
                tcg_out_brasl(s, TCG_REG_R14, off << 1);
                tcg_abort(); // untested
            }
            else { /* too far for a relative call, load full address */
                tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, args[0]);
                tcg_out_rr(s, RR_BASR, TCG_REG_R14, TCG_REG_R13);
            }
        }
        else {	/* call function in register args[0] */
            tcg_out_rr(s, RR_BASR, TCG_REG_R14, args[0]);
        }
        break;
    case INDEX_op_jmp:
        fprintf(stderr,"op 0x%x jmp 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();
        break;
    case INDEX_op_ld8u_i32:
        //fprintf(stderr,"op 0x%x ld8u_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if ((long)args[2] > -0x80000 && (long)args[2] < 0x7ffff) {
            tcg_out_e3(s, E3_LLGC, args[0], args[1], args[2]);
        }
        else {	/* displacement too large, have to calculate address manually */
            tcg_abort();
        }
        break;
    case INDEX_op_ld8s_i32:
        fprintf(stderr,"op 0x%x ld8s_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();
        break;
    case INDEX_op_ld16u_i32:
        fprintf(stderr,"op 0x%x ld16u_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if ((long)args[2] > -0x80000 && (long)args[2] < 0x7ffff) {
            tcg_out_e3(s, E3_LLGH, args[0], args[1], args[2]);
        }
        else {	/* displacement too large, have to calculate address manually */
            tcg_abort();
        }
        break;
    case INDEX_op_ld16s_i32:
        fprintf(stderr,"op 0x%x ld16s_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();
        break;
    case INDEX_op_ld_i32:
        tcg_out_ld(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;
    case INDEX_op_st8_i32:
        //fprintf(stderr,"op 0x%x st8_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if(((long)args[2]) >= -0x800 && ((long)args[2]) < 0x800)
            tcg_out_store(s, ST_STC, args[0], args[1], args[2]);
        else if(((long)args[2]) >= -0x80000 && ((long)args[2]) < 0x80000) {
            tcg_out_e3(s, E3_STCY, args[0], args[1], args[2]); /* FIXME: requires long displacement facility */
            tcg_abort(); // untested
        }
        else tcg_abort();
        break;
    case INDEX_op_st16_i32:
        //fprintf(stderr,"op 0x%x st16_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if(((long)args[2]) >= -0x800 && ((long)args[2]) < 0x800)
            tcg_out_store(s, ST_STH, args[0], args[1], args[2]);
        else if(((long)args[2]) >= -0x80000 && ((long)args[2]) < 0x80000) {
            tcg_out_e3(s, E3_STHY, args[0], args[1], args[2]);	/* FIXME: requires long displacement facility */
            tcg_abort(); // untested
        }
        else tcg_abort();
        break;
    case INDEX_op_st_i32:
        tcg_out_st(s, TCG_TYPE_I32, args[0], args[1], args[2]);
        break;
    case INDEX_op_mov_i32:
        fprintf(stderr,"op 0x%x mov_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();
        break;
    case INDEX_op_movi_i32:
        fprintf(stderr,"op 0x%x movi_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();
        break;
    case INDEX_op_add_i32:
        if(args[0] == args[1]) {
            tcg_out_rr(s, RR_AR, args[1], args[2]);
        }
        else if(args[0] == args[2]) {
            tcg_out_rr(s, RR_AR, args[0], args[1]);
        }
        else {
            tcg_out_rr(s, RR_LR, args[0], args[1]);
            tcg_out_rr(s, RR_AR, args[0], args[2]);
        }
        break;
    case INDEX_op_sub_i32:
        //fprintf(stderr,"op 0x%x sub_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if(args[0] == args[1]) {
            tcg_out_rr(s, RR_SR, args[1], args[2]); /* sr %ra0/1, %ra2 */
        }
        else if(args[0] == args[2]) {
            tcg_out_rr(s, RR_LR, TCG_REG_R13, args[2]); /* lr %r13, %raa0/2 */
            tcg_out_rr(s, RR_LR, args[0], args[1]); /* lr %ra0/2, %ra1 */
            tcg_out_rr(s, RR_SR, args[0], TCG_REG_R13); /* sr %ra0/2, %r13 */
        }
        else {
            tcg_out_rr(s, RR_LR, args[0], args[1]);	/* lr %ra0, %ra1 */
            tcg_out_rr(s, RR_SR, args[0], args[2]);	/* sr %ra0, %ra2 */
        }
        break;

    case INDEX_op_sub_i64:
        fprintf(stderr,"op 0x%x sub_i64 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();
    case INDEX_op_add_i64:
        fprintf(stderr,"op 0x%x add_i64 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        tcg_abort();

    case INDEX_op_and_i32:
        op = RR_NR;
do_logic_i32:
        if(args[0] == args[1])
            tcg_out_rr(s, op, args[1], args[2]); /* xr %ra0/1, %ra2 */
        else if(args[0] == args[2])
            tcg_out_rr(s, op, args[0], args[1]); /* xr %ra0/2, %ra1 */
        else {
            tcg_out_rr(s, RR_LR, args[0], args[1]); /* lr %ra0, %ra1 */
            tcg_out_rr(s, op, args[0], args[2]); /* xr %ra0, %ra2 */
        }
        break;
    case INDEX_op_or_i32: op = RR_OR; goto do_logic_i32;
    case INDEX_op_xor_i32: op = RR_XR; goto do_logic_i32;

    case INDEX_op_and_i64:
        //fprintf(stderr,"op 0x%x and_i64 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        op = B9_NGR;
do_logic_i64:
        if (args[0] == args[1])
            tcg_out_b9(s, op, args[0], args[2]);
        else if (args[0] == args[2])
            tcg_out_b9(s, op, args[0], args[1]);
        else {
            tcg_out_b9(s, B9_LGR, args[0], args[1]);
            tcg_out_b9(s, op, args[0], args[2]);
            tcg_abort(); // untested
        }
        break;
    case INDEX_op_or_i64: op = B9_OGR; goto do_logic_i64;
    case INDEX_op_xor_i64: op = B9_XGR; goto do_logic_i64;
    
    case INDEX_op_neg_i32:
        //fprintf(stderr,"op 0x%x neg_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        /* FIXME: optimize args[0] != args[1] case */
        tcg_out_rr(s, RR_LR, 13, args[1]);
        tcg_out32(s, 0xa7090000 | (args[0] << 20)); /* lhi %ra0, 0 */
        tcg_out_rr(s, RR_SR, args[0], 13);
        break;

    case INDEX_op_mul_i32:
        //fprintf(stderr,"op 0x%x mul_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if (args[0] == args[1])
          tcg_out32(s, 0xb2520000 | (args[0] << 4) | args[2]); /* msr %ra0/1, %ra2 */
        else if (args[0] == args[2])
          tcg_out32(s, 0xb2520000 | (args[0] << 4) | args[1]); /* msr %ra0/2, %ra1 */
        else tcg_abort();
        break;
    case INDEX_op_mul_i64:
        //fprintf(stderr,"op 0x%x mul_i64 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if(args[0] == args[1]) {
            tcg_out_b9(s, B9_MSGR, args[0], args[2]);
        }
        else if(args[0] == args[2]) {
            tcg_out_b9(s, B9_MSGR, args[0], args[1]);
        }
        else tcg_abort();
        break;

    case INDEX_op_shl_i32:
        op = SH32_SLL; op2 = SH64_SLLG;
do_shift32:
        if(const_args[2]) {
            if(args[0] == args[1])
                tcg_out_sh32(s, op, args[0], SH32_REG_NONE, args[2]);
            else {
                tcg_out_rr(s, RR_LR, args[0], args[1]);
                tcg_out_sh32(s, op, args[0], SH32_REG_NONE, args[2]);
            }
        }
        else {
            if(args[0] == args[1])
                tcg_out_sh32(s, op, args[0], args[2], 0);
            else
                tcg_out_sh64(s, op2, args[0], args[1], args[2], 0);
        }
        break;
    case INDEX_op_shr_i32: op = SH32_SRL; op2 = SH64_SRLG; goto do_shift32;
    case INDEX_op_sar_i32: op = SH32_SRA; op2 = SH64_SRAG; goto do_shift32;

    case INDEX_op_shl_i64:
        op = SH64_SLLG;
do_shift64:
        if(const_args[2]) {
            tcg_out_sh64(s, op, args[0], args[1], SH64_REG_NONE, args[2]);
        }
        else {
            tcg_out_sh64(s, op, args[0], args[1], args[2], 0);
            tcg_abort(); // untested
        }
        break;
    case INDEX_op_shr_i64: op = SH64_SRLG; goto do_shift64;
    case INDEX_op_sar_i64: op = SH64_SRAG; goto do_shift64;

    case INDEX_op_br:
        //fprintf(stderr,"op 0x%x br 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        l = &s->labels[args[0]];
        if (l->has_value) {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, l->u.value);
        }
        else {
            /* larl %r13, ... */
            tcg_out16(s, 0xc0d0);
            tcg_out_reloc(s, s->code_ptr, R_390_PC32DBL, args[0], -2);
            s->code_ptr += 4;
        }
        tcg_out_rr(s, RR_BASR, TCG_REG_R13, TCG_REG_R13);
        break;
    case INDEX_op_brcond_i32:
        //fprintf(stderr,"op 0x%x brcond_i32 0x%lx 0x%lx 0x%lx\n",opc,args[0],args[1],args[2]);
        if (args[2] > TCG_COND_GT) { /* unsigned */
          tcg_out_rr(s, RR_CLR, args[0], args[1]); /* clr %ra0, %ra1 */
        }
        else { /* signed */
          tcg_out_rr(s, RR_CR, args[0], args[1]); /* cr %ra0, %ra1 */
        }
        l = &s->labels[args[3]];
        if(l->has_value) {
            tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R13, l->u.value);
        }
        else {
            /* larl %r13, ... */
            tcg_out16(s, 0xc0d0);
            tcg_out_reloc(s, s->code_ptr, R_390_PC32DBL, args[3], -2);
            s->code_ptr += 4;
        }
        tcg_out16(s, 0x070d | (tcg_cond_to_s390_cond[args[2]] << 4)); /* bcr cond,%r13 */
        break;

    case INDEX_op_qemu_ld8u: tcg_out_qemu_ld(s, args, 0); break;
    case INDEX_op_qemu_ld8s: tcg_out_qemu_ld(s, args, 0 | 4); break;
    case INDEX_op_qemu_ld16u: tcg_out_qemu_ld(s, args, 1); break;
    case INDEX_op_qemu_ld16s: tcg_out_qemu_ld(s, args, 1 | 4); break;
    case INDEX_op_qemu_ld32u: tcg_out_qemu_ld(s, args, 2); break;
    case INDEX_op_qemu_ld32s: tcg_out_qemu_ld(s, args, 2 | 4); break;
    case INDEX_op_qemu_ld64: tcg_out_qemu_ld(s, args, 3); break;
    case INDEX_op_qemu_st8: tcg_out_qemu_st(s, args, 0); break;
    case INDEX_op_qemu_st16: tcg_out_qemu_st(s, args, 1); break;
    case INDEX_op_qemu_st32: tcg_out_qemu_st(s, args, 2); break;
    case INDEX_op_qemu_st64: tcg_out_qemu_st(s, args, 3); break;

    default:
        fprintf(stderr,"unimplemented opc 0x%x\n",opc);
        tcg_abort();
    }
}

static const TCGTargetOpDef s390_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
    { INDEX_op_jmp, { "ri" } },
    { INDEX_op_br, { } },

    { INDEX_op_mov_i32, { "r", "r" } },
    { INDEX_op_movi_i32, { "r" } },

    { INDEX_op_ld8u_i32, { "r", "r" } },
    { INDEX_op_ld8s_i32, { "r", "r" } },
    { INDEX_op_ld16u_i32, { "r", "r" } },
    { INDEX_op_ld16s_i32, { "r", "r" } },
    { INDEX_op_ld_i32, { "r", "r" } },
    { INDEX_op_st8_i32, { "r", "r" } },
    { INDEX_op_st16_i32, { "r", "r" } },
    { INDEX_op_st_i32, { "r", "r" } },

    { INDEX_op_add_i32, { "r", "r", "r" } },
    { INDEX_op_sub_i32, { "r", "r", "r" } },
    { INDEX_op_mul_i32, { "r", "r", "r" } },

    { INDEX_op_and_i32, { "r", "r", "r" } },
    { INDEX_op_or_i32, { "r", "r", "r" } },
    { INDEX_op_xor_i32, { "r", "r", "r" } },
    { INDEX_op_neg_i32, { "r", "r" } },

    { INDEX_op_shl_i32, { "r", "r", "Ri" } },
    { INDEX_op_shr_i32, { "r", "r", "Ri" } },
    { INDEX_op_sar_i32, { "r", "r", "Ri" } },

    { INDEX_op_brcond_i32, { "r", "r" } },

    { INDEX_op_qemu_ld8u, { "r", "L" } },
    { INDEX_op_qemu_ld8s, { "r", "L" } },
    { INDEX_op_qemu_ld16u, { "r", "L" } },
    { INDEX_op_qemu_ld16s, { "r", "L" } },
    { INDEX_op_qemu_ld32u, { "r", "L" } },

    { INDEX_op_qemu_st8, { "S", "S" } },
    { INDEX_op_qemu_st16, { "S", "S" } },
    { INDEX_op_qemu_st32, { "S", "S" } },

#if defined(__s390x__)
    { INDEX_op_mov_i64, { "r", "r" } },
    { INDEX_op_movi_i64, { "r" } },

    { INDEX_op_ld8u_i64, { "r", "r" } },
    { INDEX_op_ld8s_i64, { "r", "r" } },
    { INDEX_op_ld16u_i64, { "r", "r" } },
    { INDEX_op_ld16s_i64, { "r", "r" } },
    { INDEX_op_ld32u_i64, { "r", "r" } },
    { INDEX_op_ld32s_i64, { "r", "r" } },
    { INDEX_op_ld_i64, { "r", "r" } },

    { INDEX_op_st8_i64, { "r", "r" } },
    { INDEX_op_st16_i64, { "r", "r" } },
    { INDEX_op_st32_i64, { "r", "r" } },
    { INDEX_op_st_i64, { "r", "r" } },

    { INDEX_op_qemu_ld64, { "L", "L" } },
    { INDEX_op_qemu_st64, { "S", "S" } },

    { INDEX_op_add_i64, { "r", "r", "r" } },
    { INDEX_op_mul_i64, { "r", "r", "r" } },
    { INDEX_op_sub_i64, { "r", "r", "r" } },

    { INDEX_op_and_i64, { "r", "r", "r" } },
    { INDEX_op_or_i64, { "r", "r", "r" } },
    { INDEX_op_xor_i64, { "r", "r", "r" } },

    { INDEX_op_shl_i64, { "r", "r", "Ri" } },
    { INDEX_op_shr_i64, { "r", "r", "Ri" } },
    { INDEX_op_sar_i64, { "r", "r", "Ri" } },

    { INDEX_op_brcond_i64, { "r", "ri" } },
#endif

    { -1 },
};

void tcg_target_init(TCGContext *s)
{
    /* fail safe */
    if ((1 << CPU_TLB_ENTRY_BITS) != sizeof(CPUTLBEntry))
        tcg_abort();

    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffff);
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I64], 0, 0xffff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_R0) |
                     (1 << TCG_REG_R1) |
                     (1 << TCG_REG_R2) |
                     (1 << TCG_REG_R3) |
                     (1 << TCG_REG_R4) |
                     (1 << TCG_REG_R5) |
                     (1 << TCG_REG_R14)); /* link register */
    
    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R13); /* frequently used as a temporary */

    tcg_add_target_add_op_defs(s390_op_defs);
}

void tcg_target_qemu_prologue(TCGContext *s)
{
    tcg_out16(s,0xeb6f);tcg_out32(s,0xf0300024); /* stmg %r6,%r15,48(%r15) (save registers) */
    tcg_out32(s, 0xa7fbff60); /* aghi %r15,-160 (stack frame) */
    tcg_out16(s,0x7f2); /* br %r2 (go to TB) */
    tb_ret_addr = s->code_ptr;
    tcg_out16(s,0xeb6f);tcg_out32(s, 0xf0d00004); /* lmg %r6,%r15,208(%r15) (restore registers) */
    tcg_out16(s,0x7fe); /* br %r14 (return) */
}


static inline void tcg_out_mov(TCGContext *s, int ret, int arg)
{
    tcg_out_b9(s, B9_LGR, ret, arg);
}

static inline void tcg_out_addi(TCGContext *s, int reg, tcg_target_long val)
{
    tcg_abort();
}
