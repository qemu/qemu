/*
 *  i386 translation
 * 
 *  Copyright (c) 2003 Fabrice Bellard
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
#include <signal.h>
#include <assert.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

/* XXX: move that elsewhere */
static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;

#define PREFIX_REPZ   0x01
#define PREFIX_REPNZ  0x02
#define PREFIX_LOCK   0x04
#define PREFIX_DATA   0x08
#define PREFIX_ADR    0x10

typedef struct DisasContext {
    /* current insn context */
    int override; /* -1 if no override */
    int prefix;
    int aflag, dflag;
    uint8_t *pc; /* pc = eip + cs_base */
    int is_jmp; /* 1 = means jump (stop translation), 2 means CPU
                   static state change (stop translation) */
    /* current block context */
    uint8_t *cs_base; /* base of CS segment */
    int pe;     /* protected mode */
    int code32; /* 32 bit code segment */
    int ss32;   /* 32 bit stack segment */
    int cc_op;  /* current CC operation */
    int addseg; /* non zero if either DS/ES/SS have a non zero base */
    int f_st;   /* currently unused */
    int vm86;   /* vm86 mode */
    int cpl;
    int iopl;
    int tf;     /* TF cpu flag */
    int singlestep_enabled; /* "hardware" single step enabled */
    int jmp_opt; /* use direct block chaining for direct jumps */
    int mem_index; /* select memory access functions */
    int flags; /* all execution flags */
    struct TranslationBlock *tb;
    int popl_esp_hack; /* for correct popl with esp base handling */
} DisasContext;

static void gen_eob(DisasContext *s);
static void gen_jmp(DisasContext *s, unsigned int eip);

/* i386 arith/logic operations */
enum {
    OP_ADDL, 
    OP_ORL, 
    OP_ADCL, 
    OP_SBBL,
    OP_ANDL, 
    OP_SUBL, 
    OP_XORL, 
    OP_CMPL,
};

/* i386 shift ops */
enum {
    OP_ROL, 
    OP_ROR, 
    OP_RCL, 
    OP_RCR, 
    OP_SHL, 
    OP_SHR, 
    OP_SHL1, /* undocumented */
    OP_SAR = 7,
};

enum {
#define DEF(s, n, copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS,
};

#include "gen-op.h"

/* operand size */
enum {
    OT_BYTE = 0,
    OT_WORD,
    OT_LONG, 
    OT_QUAD,
};

enum {
    /* I386 int registers */
    OR_EAX,   /* MUST be even numbered */
    OR_ECX,
    OR_EDX,
    OR_EBX,
    OR_ESP,
    OR_EBP,
    OR_ESI,
    OR_EDI,
    OR_TMP0,    /* temporary operand register */
    OR_TMP1,
    OR_A0, /* temporary register used when doing address evaluation */
    OR_ZERO, /* fixed zero register */
    NB_OREGS,
};

static GenOpFunc *gen_op_mov_reg_T0[3][8] = {
    [OT_BYTE] = {
        gen_op_movb_EAX_T0,
        gen_op_movb_ECX_T0,
        gen_op_movb_EDX_T0,
        gen_op_movb_EBX_T0,
        gen_op_movh_EAX_T0,
        gen_op_movh_ECX_T0,
        gen_op_movh_EDX_T0,
        gen_op_movh_EBX_T0,
    },
    [OT_WORD] = {
        gen_op_movw_EAX_T0,
        gen_op_movw_ECX_T0,
        gen_op_movw_EDX_T0,
        gen_op_movw_EBX_T0,
        gen_op_movw_ESP_T0,
        gen_op_movw_EBP_T0,
        gen_op_movw_ESI_T0,
        gen_op_movw_EDI_T0,
    },
    [OT_LONG] = {
        gen_op_movl_EAX_T0,
        gen_op_movl_ECX_T0,
        gen_op_movl_EDX_T0,
        gen_op_movl_EBX_T0,
        gen_op_movl_ESP_T0,
        gen_op_movl_EBP_T0,
        gen_op_movl_ESI_T0,
        gen_op_movl_EDI_T0,
    },
};

static GenOpFunc *gen_op_mov_reg_T1[3][8] = {
    [OT_BYTE] = {
        gen_op_movb_EAX_T1,
        gen_op_movb_ECX_T1,
        gen_op_movb_EDX_T1,
        gen_op_movb_EBX_T1,
        gen_op_movh_EAX_T1,
        gen_op_movh_ECX_T1,
        gen_op_movh_EDX_T1,
        gen_op_movh_EBX_T1,
    },
    [OT_WORD] = {
        gen_op_movw_EAX_T1,
        gen_op_movw_ECX_T1,
        gen_op_movw_EDX_T1,
        gen_op_movw_EBX_T1,
        gen_op_movw_ESP_T1,
        gen_op_movw_EBP_T1,
        gen_op_movw_ESI_T1,
        gen_op_movw_EDI_T1,
    },
    [OT_LONG] = {
        gen_op_movl_EAX_T1,
        gen_op_movl_ECX_T1,
        gen_op_movl_EDX_T1,
        gen_op_movl_EBX_T1,
        gen_op_movl_ESP_T1,
        gen_op_movl_EBP_T1,
        gen_op_movl_ESI_T1,
        gen_op_movl_EDI_T1,
    },
};

static GenOpFunc *gen_op_mov_reg_A0[2][8] = {
    [0] = {
        gen_op_movw_EAX_A0,
        gen_op_movw_ECX_A0,
        gen_op_movw_EDX_A0,
        gen_op_movw_EBX_A0,
        gen_op_movw_ESP_A0,
        gen_op_movw_EBP_A0,
        gen_op_movw_ESI_A0,
        gen_op_movw_EDI_A0,
    },
    [1] = {
        gen_op_movl_EAX_A0,
        gen_op_movl_ECX_A0,
        gen_op_movl_EDX_A0,
        gen_op_movl_EBX_A0,
        gen_op_movl_ESP_A0,
        gen_op_movl_EBP_A0,
        gen_op_movl_ESI_A0,
        gen_op_movl_EDI_A0,
    },
};

static GenOpFunc *gen_op_mov_TN_reg[3][2][8] = 
{
    [OT_BYTE] = {
        {
            gen_op_movl_T0_EAX,
            gen_op_movl_T0_ECX,
            gen_op_movl_T0_EDX,
            gen_op_movl_T0_EBX,
            gen_op_movh_T0_EAX,
            gen_op_movh_T0_ECX,
            gen_op_movh_T0_EDX,
            gen_op_movh_T0_EBX,
        },
        {
            gen_op_movl_T1_EAX,
            gen_op_movl_T1_ECX,
            gen_op_movl_T1_EDX,
            gen_op_movl_T1_EBX,
            gen_op_movh_T1_EAX,
            gen_op_movh_T1_ECX,
            gen_op_movh_T1_EDX,
            gen_op_movh_T1_EBX,
        },
    },
    [OT_WORD] = {
        {
            gen_op_movl_T0_EAX,
            gen_op_movl_T0_ECX,
            gen_op_movl_T0_EDX,
            gen_op_movl_T0_EBX,
            gen_op_movl_T0_ESP,
            gen_op_movl_T0_EBP,
            gen_op_movl_T0_ESI,
            gen_op_movl_T0_EDI,
        },
        {
            gen_op_movl_T1_EAX,
            gen_op_movl_T1_ECX,
            gen_op_movl_T1_EDX,
            gen_op_movl_T1_EBX,
            gen_op_movl_T1_ESP,
            gen_op_movl_T1_EBP,
            gen_op_movl_T1_ESI,
            gen_op_movl_T1_EDI,
        },
    },
    [OT_LONG] = {
        {
            gen_op_movl_T0_EAX,
            gen_op_movl_T0_ECX,
            gen_op_movl_T0_EDX,
            gen_op_movl_T0_EBX,
            gen_op_movl_T0_ESP,
            gen_op_movl_T0_EBP,
            gen_op_movl_T0_ESI,
            gen_op_movl_T0_EDI,
        },
        {
            gen_op_movl_T1_EAX,
            gen_op_movl_T1_ECX,
            gen_op_movl_T1_EDX,
            gen_op_movl_T1_EBX,
            gen_op_movl_T1_ESP,
            gen_op_movl_T1_EBP,
            gen_op_movl_T1_ESI,
            gen_op_movl_T1_EDI,
        },
    },
};

static GenOpFunc *gen_op_movl_A0_reg[8] = {
    gen_op_movl_A0_EAX,
    gen_op_movl_A0_ECX,
    gen_op_movl_A0_EDX,
    gen_op_movl_A0_EBX,
    gen_op_movl_A0_ESP,
    gen_op_movl_A0_EBP,
    gen_op_movl_A0_ESI,
    gen_op_movl_A0_EDI,
};

static GenOpFunc *gen_op_addl_A0_reg_sN[4][8] = {
    [0] = {
        gen_op_addl_A0_EAX,
        gen_op_addl_A0_ECX,
        gen_op_addl_A0_EDX,
        gen_op_addl_A0_EBX,
        gen_op_addl_A0_ESP,
        gen_op_addl_A0_EBP,
        gen_op_addl_A0_ESI,
        gen_op_addl_A0_EDI,
    },
    [1] = {
        gen_op_addl_A0_EAX_s1,
        gen_op_addl_A0_ECX_s1,
        gen_op_addl_A0_EDX_s1,
        gen_op_addl_A0_EBX_s1,
        gen_op_addl_A0_ESP_s1,
        gen_op_addl_A0_EBP_s1,
        gen_op_addl_A0_ESI_s1,
        gen_op_addl_A0_EDI_s1,
    },
    [2] = {
        gen_op_addl_A0_EAX_s2,
        gen_op_addl_A0_ECX_s2,
        gen_op_addl_A0_EDX_s2,
        gen_op_addl_A0_EBX_s2,
        gen_op_addl_A0_ESP_s2,
        gen_op_addl_A0_EBP_s2,
        gen_op_addl_A0_ESI_s2,
        gen_op_addl_A0_EDI_s2,
    },
    [3] = {
        gen_op_addl_A0_EAX_s3,
        gen_op_addl_A0_ECX_s3,
        gen_op_addl_A0_EDX_s3,
        gen_op_addl_A0_EBX_s3,
        gen_op_addl_A0_ESP_s3,
        gen_op_addl_A0_EBP_s3,
        gen_op_addl_A0_ESI_s3,
        gen_op_addl_A0_EDI_s3,
    },
};

static GenOpFunc *gen_op_cmov_reg_T1_T0[2][8] = {
    [0] = {
        gen_op_cmovw_EAX_T1_T0,
        gen_op_cmovw_ECX_T1_T0,
        gen_op_cmovw_EDX_T1_T0,
        gen_op_cmovw_EBX_T1_T0,
        gen_op_cmovw_ESP_T1_T0,
        gen_op_cmovw_EBP_T1_T0,
        gen_op_cmovw_ESI_T1_T0,
        gen_op_cmovw_EDI_T1_T0,
    },
    [1] = {
        gen_op_cmovl_EAX_T1_T0,
        gen_op_cmovl_ECX_T1_T0,
        gen_op_cmovl_EDX_T1_T0,
        gen_op_cmovl_EBX_T1_T0,
        gen_op_cmovl_ESP_T1_T0,
        gen_op_cmovl_EBP_T1_T0,
        gen_op_cmovl_ESI_T1_T0,
        gen_op_cmovl_EDI_T1_T0,
    },
};

static GenOpFunc *gen_op_arith_T0_T1_cc[8] = {
    NULL,
    gen_op_orl_T0_T1,
    NULL,
    NULL,
    gen_op_andl_T0_T1,
    NULL,
    gen_op_xorl_T0_T1,
    NULL,
};

#define DEF_ARITHC(SUFFIX)\
    {\
        gen_op_adcb ## SUFFIX ## _T0_T1_cc,\
        gen_op_sbbb ## SUFFIX ## _T0_T1_cc,\
    },\
    {\
        gen_op_adcw ## SUFFIX ## _T0_T1_cc,\
        gen_op_sbbw ## SUFFIX ## _T0_T1_cc,\
    },\
    {\
        gen_op_adcl ## SUFFIX ## _T0_T1_cc,\
        gen_op_sbbl ## SUFFIX ## _T0_T1_cc,\
    },

static GenOpFunc *gen_op_arithc_T0_T1_cc[3][2] = {
    DEF_ARITHC( )
};

static GenOpFunc *gen_op_arithc_mem_T0_T1_cc[9][2] = {
    DEF_ARITHC(_raw)
#ifndef CONFIG_USER_ONLY
    DEF_ARITHC(_kernel)
    DEF_ARITHC(_user)
#endif
};

static const int cc_op_arithb[8] = {
    CC_OP_ADDB,
    CC_OP_LOGICB,
    CC_OP_ADDB,
    CC_OP_SUBB,
    CC_OP_LOGICB,
    CC_OP_SUBB,
    CC_OP_LOGICB,
    CC_OP_SUBB,
};

#define DEF_CMPXCHG(SUFFIX)\
    gen_op_cmpxchgb ## SUFFIX ## _T0_T1_EAX_cc,\
    gen_op_cmpxchgw ## SUFFIX ## _T0_T1_EAX_cc,\
    gen_op_cmpxchgl ## SUFFIX ## _T0_T1_EAX_cc,


static GenOpFunc *gen_op_cmpxchg_T0_T1_EAX_cc[3] = {
    DEF_CMPXCHG( )
};

static GenOpFunc *gen_op_cmpxchg_mem_T0_T1_EAX_cc[9] = {
    DEF_CMPXCHG(_raw)
#ifndef CONFIG_USER_ONLY
    DEF_CMPXCHG(_kernel)
    DEF_CMPXCHG(_user)
#endif
};

#define DEF_SHIFT(SUFFIX)\
    {\
        gen_op_rolb ## SUFFIX ## _T0_T1_cc,\
        gen_op_rorb ## SUFFIX ## _T0_T1_cc,\
        gen_op_rclb ## SUFFIX ## _T0_T1_cc,\
        gen_op_rcrb ## SUFFIX ## _T0_T1_cc,\
        gen_op_shlb ## SUFFIX ## _T0_T1_cc,\
        gen_op_shrb ## SUFFIX ## _T0_T1_cc,\
        gen_op_shlb ## SUFFIX ## _T0_T1_cc,\
        gen_op_sarb ## SUFFIX ## _T0_T1_cc,\
    },\
    {\
        gen_op_rolw ## SUFFIX ## _T0_T1_cc,\
        gen_op_rorw ## SUFFIX ## _T0_T1_cc,\
        gen_op_rclw ## SUFFIX ## _T0_T1_cc,\
        gen_op_rcrw ## SUFFIX ## _T0_T1_cc,\
        gen_op_shlw ## SUFFIX ## _T0_T1_cc,\
        gen_op_shrw ## SUFFIX ## _T0_T1_cc,\
        gen_op_shlw ## SUFFIX ## _T0_T1_cc,\
        gen_op_sarw ## SUFFIX ## _T0_T1_cc,\
    },\
    {\
        gen_op_roll ## SUFFIX ## _T0_T1_cc,\
        gen_op_rorl ## SUFFIX ## _T0_T1_cc,\
        gen_op_rcll ## SUFFIX ## _T0_T1_cc,\
        gen_op_rcrl ## SUFFIX ## _T0_T1_cc,\
        gen_op_shll ## SUFFIX ## _T0_T1_cc,\
        gen_op_shrl ## SUFFIX ## _T0_T1_cc,\
        gen_op_shll ## SUFFIX ## _T0_T1_cc,\
        gen_op_sarl ## SUFFIX ## _T0_T1_cc,\
    },

static GenOpFunc *gen_op_shift_T0_T1_cc[3][8] = {
    DEF_SHIFT( )
};

static GenOpFunc *gen_op_shift_mem_T0_T1_cc[9][8] = {
    DEF_SHIFT(_raw)
#ifndef CONFIG_USER_ONLY
    DEF_SHIFT(_kernel)
    DEF_SHIFT(_user)
#endif
};

#define DEF_SHIFTD(SUFFIX, op)\
    {\
        NULL,\
        NULL,\
    },\
    {\
        gen_op_shldw ## SUFFIX ## _T0_T1_ ## op ## _cc,\
        gen_op_shrdw ## SUFFIX ## _T0_T1_ ## op ## _cc,\
    },\
    {\
        gen_op_shldl ## SUFFIX ## _T0_T1_ ## op ## _cc,\
        gen_op_shrdl ## SUFFIX ## _T0_T1_ ## op ## _cc,\
    },


static GenOpFunc1 *gen_op_shiftd_T0_T1_im_cc[3][2] = {
    DEF_SHIFTD(, im)
};

static GenOpFunc *gen_op_shiftd_T0_T1_ECX_cc[3][2] = {
    DEF_SHIFTD(, ECX)
};

static GenOpFunc1 *gen_op_shiftd_mem_T0_T1_im_cc[9][2] = {
    DEF_SHIFTD(_raw, im)
#ifndef CONFIG_USER_ONLY
    DEF_SHIFTD(_kernel, im)
    DEF_SHIFTD(_user, im)
#endif
};

static GenOpFunc *gen_op_shiftd_mem_T0_T1_ECX_cc[9][2] = {
    DEF_SHIFTD(_raw, ECX)
#ifndef CONFIG_USER_ONLY
    DEF_SHIFTD(_kernel, ECX)
    DEF_SHIFTD(_user, ECX)
#endif
};

static GenOpFunc *gen_op_btx_T0_T1_cc[2][4] = {
    [0] = {
        gen_op_btw_T0_T1_cc,
        gen_op_btsw_T0_T1_cc,
        gen_op_btrw_T0_T1_cc,
        gen_op_btcw_T0_T1_cc,
    },
    [1] = {
        gen_op_btl_T0_T1_cc,
        gen_op_btsl_T0_T1_cc,
        gen_op_btrl_T0_T1_cc,
        gen_op_btcl_T0_T1_cc,
    },
};

static GenOpFunc *gen_op_bsx_T0_cc[2][2] = {
    [0] = {
        gen_op_bsfw_T0_cc,
        gen_op_bsrw_T0_cc,
    },
    [1] = {
        gen_op_bsfl_T0_cc,
        gen_op_bsrl_T0_cc,
    },
};

static GenOpFunc *gen_op_lds_T0_A0[3 * 3] = {
    gen_op_ldsb_raw_T0_A0,
    gen_op_ldsw_raw_T0_A0,
    NULL,
#ifndef CONFIG_USER_ONLY
    gen_op_ldsb_kernel_T0_A0,
    gen_op_ldsw_kernel_T0_A0,
    NULL,

    gen_op_ldsb_user_T0_A0,
    gen_op_ldsw_user_T0_A0,
    NULL,
#endif
};

static GenOpFunc *gen_op_ldu_T0_A0[3 * 3] = {
    gen_op_ldub_raw_T0_A0,
    gen_op_lduw_raw_T0_A0,
    NULL,

#ifndef CONFIG_USER_ONLY
    gen_op_ldub_kernel_T0_A0,
    gen_op_lduw_kernel_T0_A0,
    NULL,

    gen_op_ldub_user_T0_A0,
    gen_op_lduw_user_T0_A0,
    NULL,
#endif
};

/* sign does not matter, except for lidt/lgdt call (TODO: fix it) */
static GenOpFunc *gen_op_ld_T0_A0[3 * 3] = {
    gen_op_ldub_raw_T0_A0,
    gen_op_lduw_raw_T0_A0,
    gen_op_ldl_raw_T0_A0,

#ifndef CONFIG_USER_ONLY
    gen_op_ldub_kernel_T0_A0,
    gen_op_lduw_kernel_T0_A0,
    gen_op_ldl_kernel_T0_A0,

    gen_op_ldub_user_T0_A0,
    gen_op_lduw_user_T0_A0,
    gen_op_ldl_user_T0_A0,
#endif
};

static GenOpFunc *gen_op_ld_T1_A0[3 * 3] = {
    gen_op_ldub_raw_T1_A0,
    gen_op_lduw_raw_T1_A0,
    gen_op_ldl_raw_T1_A0,

#ifndef CONFIG_USER_ONLY
    gen_op_ldub_kernel_T1_A0,
    gen_op_lduw_kernel_T1_A0,
    gen_op_ldl_kernel_T1_A0,

    gen_op_ldub_user_T1_A0,
    gen_op_lduw_user_T1_A0,
    gen_op_ldl_user_T1_A0,
#endif
};

static GenOpFunc *gen_op_st_T0_A0[3 * 3] = {
    gen_op_stb_raw_T0_A0,
    gen_op_stw_raw_T0_A0,
    gen_op_stl_raw_T0_A0,

#ifndef CONFIG_USER_ONLY
    gen_op_stb_kernel_T0_A0,
    gen_op_stw_kernel_T0_A0,
    gen_op_stl_kernel_T0_A0,

    gen_op_stb_user_T0_A0,
    gen_op_stw_user_T0_A0,
    gen_op_stl_user_T0_A0,
#endif
};

static GenOpFunc *gen_op_st_T1_A0[3 * 3] = {
    NULL,
    gen_op_stw_raw_T1_A0,
    gen_op_stl_raw_T1_A0,

#ifndef CONFIG_USER_ONLY
    NULL,
    gen_op_stw_kernel_T1_A0,
    gen_op_stl_kernel_T1_A0,

    NULL,
    gen_op_stw_user_T1_A0,
    gen_op_stl_user_T1_A0,
#endif
};

static inline void gen_string_movl_A0_ESI(DisasContext *s)
{
    int override;

    override = s->override;
    if (s->aflag) {
        /* 32 bit address */
        if (s->addseg && override < 0)
            override = R_DS;
        if (override >= 0) {
            gen_op_movl_A0_seg(offsetof(CPUX86State,segs[override].base));
            gen_op_addl_A0_reg_sN[0][R_ESI]();
        } else {
            gen_op_movl_A0_reg[R_ESI]();
        }
    } else {
        /* 16 address, always override */
        if (override < 0)
            override = R_DS;
        gen_op_movl_A0_reg[R_ESI]();
        gen_op_andl_A0_ffff();
        gen_op_addl_A0_seg(offsetof(CPUX86State,segs[override].base));
    }
}

static inline void gen_string_movl_A0_EDI(DisasContext *s)
{
    if (s->aflag) {
        if (s->addseg) {
            gen_op_movl_A0_seg(offsetof(CPUX86State,segs[R_ES].base));
            gen_op_addl_A0_reg_sN[0][R_EDI]();
        } else {
            gen_op_movl_A0_reg[R_EDI]();
        }
    } else {
        gen_op_movl_A0_reg[R_EDI]();
        gen_op_andl_A0_ffff();
        gen_op_addl_A0_seg(offsetof(CPUX86State,segs[R_ES].base));
    }
}

static GenOpFunc *gen_op_movl_T0_Dshift[3] = {
    gen_op_movl_T0_Dshiftb,
    gen_op_movl_T0_Dshiftw,
    gen_op_movl_T0_Dshiftl,
};

static GenOpFunc2 *gen_op_jz_ecx[2] = {
    gen_op_jz_ecxw,
    gen_op_jz_ecxl,
};
    
static GenOpFunc1 *gen_op_jz_ecx_im[2] = {
    gen_op_jz_ecxw_im,
    gen_op_jz_ecxl_im,
};

static GenOpFunc *gen_op_dec_ECX[2] = {
    gen_op_decw_ECX,
    gen_op_decl_ECX,
};

#ifdef USE_DIRECT_JUMP
typedef GenOpFunc GenOpFuncTB2;
#define gen_op_string_jnz_sub(nz, ot, tb) gen_op_string_jnz_sub2[nz][ot]()
#else
typedef GenOpFunc1 GenOpFuncTB2;
#define gen_op_string_jnz_sub(nz, ot, tb) gen_op_string_jnz_sub2[nz][ot](tb)
#endif

static GenOpFuncTB2 *gen_op_string_jnz_sub2[2][3] = {
    {
        gen_op_string_jnz_subb,
        gen_op_string_jnz_subw,
        gen_op_string_jnz_subl,
    },
    {
        gen_op_string_jz_subb,
        gen_op_string_jz_subw,
        gen_op_string_jz_subl,
    },
};

static GenOpFunc1 *gen_op_string_jnz_sub_im[2][3] = {
    {
        gen_op_string_jnz_subb_im,
        gen_op_string_jnz_subw_im,
        gen_op_string_jnz_subl_im,
    },
    {
        gen_op_string_jz_subb_im,
        gen_op_string_jz_subw_im,
        gen_op_string_jz_subl_im,
    },
};

static GenOpFunc *gen_op_in_DX_T0[3] = {
    gen_op_inb_DX_T0,
    gen_op_inw_DX_T0,
    gen_op_inl_DX_T0,
};

static GenOpFunc *gen_op_out_DX_T0[3] = {
    gen_op_outb_DX_T0,
    gen_op_outw_DX_T0,
    gen_op_outl_DX_T0,
};

static GenOpFunc *gen_op_in[3] = {
    gen_op_inb_T0_T1,
    gen_op_inw_T0_T1,
    gen_op_inl_T0_T1,
};

static GenOpFunc *gen_op_out[3] = {
    gen_op_outb_T0_T1,
    gen_op_outw_T0_T1,
    gen_op_outl_T0_T1,
};

static GenOpFunc *gen_check_io_T0[3] = {
    gen_op_check_iob_T0,
    gen_op_check_iow_T0,
    gen_op_check_iol_T0,
};

static GenOpFunc *gen_check_io_DX[3] = {
    gen_op_check_iob_DX,
    gen_op_check_iow_DX,
    gen_op_check_iol_DX,
};

static void gen_check_io(DisasContext *s, int ot, int use_dx, int cur_eip)
{
    if (s->pe && (s->cpl > s->iopl || s->vm86)) {
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_jmp_im(cur_eip);
        if (use_dx)
            gen_check_io_DX[ot]();
        else
            gen_check_io_T0[ot]();
    }
}

static inline void gen_movs(DisasContext *s, int ot)
{
    gen_string_movl_A0_ESI(s);
    gen_op_ld_T0_A0[ot + s->mem_index]();
    gen_string_movl_A0_EDI(s);
    gen_op_st_T0_A0[ot + s->mem_index]();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_ESI_T0();
        gen_op_addl_EDI_T0();
    } else {
        gen_op_addw_ESI_T0();
        gen_op_addw_EDI_T0();
    }
}

static inline void gen_update_cc_op(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC) {
        gen_op_set_cc_op(s->cc_op);
        s->cc_op = CC_OP_DYNAMIC;
    }
}

static inline void gen_jz_ecx_string(DisasContext *s, unsigned int next_eip)
{
    if (s->jmp_opt) {
        gen_op_jz_ecx[s->aflag]((long)s->tb, next_eip);
    } else {
        /* XXX: does not work with gdbstub "ice" single step - not a
           serious problem */
        gen_op_jz_ecx_im[s->aflag](next_eip);
    }
}

static inline void gen_stos(DisasContext *s, int ot)
{
    gen_op_mov_TN_reg[OT_LONG][0][R_EAX]();
    gen_string_movl_A0_EDI(s);
    gen_op_st_T0_A0[ot + s->mem_index]();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_EDI_T0();
    } else {
        gen_op_addw_EDI_T0();
    }
}

static inline void gen_lods(DisasContext *s, int ot)
{
    gen_string_movl_A0_ESI(s);
    gen_op_ld_T0_A0[ot + s->mem_index]();
    gen_op_mov_reg_T0[ot][R_EAX]();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_ESI_T0();
    } else {
        gen_op_addw_ESI_T0();
    }
}

static inline void gen_scas(DisasContext *s, int ot)
{
    gen_op_mov_TN_reg[OT_LONG][0][R_EAX]();
    gen_string_movl_A0_EDI(s);
    gen_op_ld_T1_A0[ot + s->mem_index]();
    gen_op_cmpl_T0_T1_cc();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_EDI_T0();
    } else {
        gen_op_addw_EDI_T0();
    }
}

static inline void gen_cmps(DisasContext *s, int ot)
{
    gen_string_movl_A0_ESI(s);
    gen_op_ld_T0_A0[ot + s->mem_index]();
    gen_string_movl_A0_EDI(s);
    gen_op_ld_T1_A0[ot + s->mem_index]();
    gen_op_cmpl_T0_T1_cc();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_ESI_T0();
        gen_op_addl_EDI_T0();
    } else {
        gen_op_addw_ESI_T0();
        gen_op_addw_EDI_T0();
    }
}

static inline void gen_ins(DisasContext *s, int ot)
{
    gen_op_in_DX_T0[ot]();
    gen_string_movl_A0_EDI(s);
    gen_op_st_T0_A0[ot + s->mem_index]();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_EDI_T0();
    } else {
        gen_op_addw_EDI_T0();
    }
}

static inline void gen_outs(DisasContext *s, int ot)
{
    gen_string_movl_A0_ESI(s);
    gen_op_ld_T0_A0[ot + s->mem_index]();
    gen_op_out_DX_T0[ot]();
    gen_op_movl_T0_Dshift[ot]();
    if (s->aflag) {
        gen_op_addl_ESI_T0();
    } else {
        gen_op_addw_ESI_T0();
    }
}

/* same method as Valgrind : we generate jumps to current or next
   instruction */
#define GEN_REPZ(op)                                                          \
static inline void gen_repz_ ## op(DisasContext *s, int ot,                   \
                                 unsigned int cur_eip, unsigned int next_eip) \
{                                                                             \
    gen_update_cc_op(s);                                                      \
    gen_jz_ecx_string(s, next_eip);                                           \
    gen_ ## op(s, ot);                                                        \
    gen_op_dec_ECX[s->aflag]();                                               \
    /* a loop would cause two single step exceptions if ECX = 1               \
       before rep string_insn */                                              \
    if (!s->jmp_opt)                                                          \
        gen_op_jz_ecx_im[s->aflag](next_eip);                                 \
    gen_jmp(s, cur_eip);                                                      \
}

#define GEN_REPZ2(op)                                                         \
static inline void gen_repz_ ## op(DisasContext *s, int ot,                   \
                                   unsigned int cur_eip,                      \
                                   unsigned int next_eip,                     \
                                   int nz)                                    \
{                                                                             \
    gen_update_cc_op(s);                                                      \
    gen_jz_ecx_string(s, next_eip);                                           \
    gen_ ## op(s, ot);                                                        \
    gen_op_dec_ECX[s->aflag]();                                               \
    gen_op_set_cc_op(CC_OP_SUBB + ot);                                        \
    if (!s->jmp_opt)                                                          \
        gen_op_string_jnz_sub_im[nz][ot](next_eip);                           \
    else                                                                      \
        gen_op_string_jnz_sub(nz, ot, (long)s->tb);                           \
    if (!s->jmp_opt)                                                          \
        gen_op_jz_ecx_im[s->aflag](next_eip);                                 \
    gen_jmp(s, cur_eip);                                                      \
}

GEN_REPZ(movs)
GEN_REPZ(stos)
GEN_REPZ(lods)
GEN_REPZ(ins)
GEN_REPZ(outs)
GEN_REPZ2(scas)
GEN_REPZ2(cmps)

enum {
    JCC_O,
    JCC_B,
    JCC_Z,
    JCC_BE,
    JCC_S,
    JCC_P,
    JCC_L,
    JCC_LE,
};

static GenOpFunc3 *gen_jcc_sub[3][8] = {
    [OT_BYTE] = {
        NULL,
        gen_op_jb_subb,
        gen_op_jz_subb,
        gen_op_jbe_subb,
        gen_op_js_subb,
        NULL,
        gen_op_jl_subb,
        gen_op_jle_subb,
    },
    [OT_WORD] = {
        NULL,
        gen_op_jb_subw,
        gen_op_jz_subw,
        gen_op_jbe_subw,
        gen_op_js_subw,
        NULL,
        gen_op_jl_subw,
        gen_op_jle_subw,
    },
    [OT_LONG] = {
        NULL,
        gen_op_jb_subl,
        gen_op_jz_subl,
        gen_op_jbe_subl,
        gen_op_js_subl,
        NULL,
        gen_op_jl_subl,
        gen_op_jle_subl,
    },
};
static GenOpFunc2 *gen_op_loop[2][4] = {
    [0] = {
        gen_op_loopnzw,
        gen_op_loopzw,
        gen_op_loopw,
        gen_op_jecxzw,
    },
    [1] = {
        gen_op_loopnzl,
        gen_op_loopzl,
        gen_op_loopl,
        gen_op_jecxzl,
    },
};

static GenOpFunc *gen_setcc_slow[8] = {
    gen_op_seto_T0_cc,
    gen_op_setb_T0_cc,
    gen_op_setz_T0_cc,
    gen_op_setbe_T0_cc,
    gen_op_sets_T0_cc,
    gen_op_setp_T0_cc,
    gen_op_setl_T0_cc,
    gen_op_setle_T0_cc,
};

static GenOpFunc *gen_setcc_sub[3][8] = {
    [OT_BYTE] = {
        NULL,
        gen_op_setb_T0_subb,
        gen_op_setz_T0_subb,
        gen_op_setbe_T0_subb,
        gen_op_sets_T0_subb,
        NULL,
        gen_op_setl_T0_subb,
        gen_op_setle_T0_subb,
    },
    [OT_WORD] = {
        NULL,
        gen_op_setb_T0_subw,
        gen_op_setz_T0_subw,
        gen_op_setbe_T0_subw,
        gen_op_sets_T0_subw,
        NULL,
        gen_op_setl_T0_subw,
        gen_op_setle_T0_subw,
    },
    [OT_LONG] = {
        NULL,
        gen_op_setb_T0_subl,
        gen_op_setz_T0_subl,
        gen_op_setbe_T0_subl,
        gen_op_sets_T0_subl,
        NULL,
        gen_op_setl_T0_subl,
        gen_op_setle_T0_subl,
    },
};

static GenOpFunc *gen_op_fp_arith_ST0_FT0[8] = {
    gen_op_fadd_ST0_FT0,
    gen_op_fmul_ST0_FT0,
    gen_op_fcom_ST0_FT0,
    gen_op_fcom_ST0_FT0,
    gen_op_fsub_ST0_FT0,
    gen_op_fsubr_ST0_FT0,
    gen_op_fdiv_ST0_FT0,
    gen_op_fdivr_ST0_FT0,
};

/* NOTE the exception in "r" op ordering */
static GenOpFunc1 *gen_op_fp_arith_STN_ST0[8] = {
    gen_op_fadd_STN_ST0,
    gen_op_fmul_STN_ST0,
    NULL,
    NULL,
    gen_op_fsubr_STN_ST0,
    gen_op_fsub_STN_ST0,
    gen_op_fdivr_STN_ST0,
    gen_op_fdiv_STN_ST0,
};

/* if d == OR_TMP0, it means memory operand (address in A0) */
static void gen_op(DisasContext *s1, int op, int ot, int d)
{
    GenOpFunc *gen_update_cc;
    
    if (d != OR_TMP0) {
        gen_op_mov_TN_reg[ot][0][d]();
    } else {
        gen_op_ld_T0_A0[ot + s1->mem_index]();
    }
    switch(op) {
    case OP_ADCL:
    case OP_SBBL:
        if (s1->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s1->cc_op);
        if (d != OR_TMP0) {
            gen_op_arithc_T0_T1_cc[ot][op - OP_ADCL]();
            gen_op_mov_reg_T0[ot][d]();
        } else {
            gen_op_arithc_mem_T0_T1_cc[ot + s1->mem_index][op - OP_ADCL]();
        }
        s1->cc_op = CC_OP_DYNAMIC;
        goto the_end;
    case OP_ADDL:
        gen_op_addl_T0_T1();
        s1->cc_op = CC_OP_ADDB + ot;
        gen_update_cc = gen_op_update2_cc;
        break;
    case OP_SUBL:
        gen_op_subl_T0_T1();
        s1->cc_op = CC_OP_SUBB + ot;
        gen_update_cc = gen_op_update2_cc;
        break;
    default:
    case OP_ANDL:
    case OP_ORL:
    case OP_XORL:
        gen_op_arith_T0_T1_cc[op]();
        s1->cc_op = CC_OP_LOGICB + ot;
        gen_update_cc = gen_op_update1_cc;
        break;
    case OP_CMPL:
        gen_op_cmpl_T0_T1_cc();
        s1->cc_op = CC_OP_SUBB + ot;
        gen_update_cc = NULL;
        break;
    }
    if (op != OP_CMPL) {
        if (d != OR_TMP0)
            gen_op_mov_reg_T0[ot][d]();
        else
            gen_op_st_T0_A0[ot + s1->mem_index]();
    }
    /* the flags update must happen after the memory write (precise
       exception support) */
    if (gen_update_cc)
        gen_update_cc();
 the_end: ;
}

/* if d == OR_TMP0, it means memory operand (address in A0) */
static void gen_inc(DisasContext *s1, int ot, int d, int c)
{
    if (d != OR_TMP0)
        gen_op_mov_TN_reg[ot][0][d]();
    else
        gen_op_ld_T0_A0[ot + s1->mem_index]();
    if (s1->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s1->cc_op);
    if (c > 0) {
        gen_op_incl_T0();
        s1->cc_op = CC_OP_INCB + ot;
    } else {
        gen_op_decl_T0();
        s1->cc_op = CC_OP_DECB + ot;
    }
    if (d != OR_TMP0)
        gen_op_mov_reg_T0[ot][d]();
    else
        gen_op_st_T0_A0[ot + s1->mem_index]();
    gen_op_update_inc_cc();
}

static void gen_shift(DisasContext *s1, int op, int ot, int d, int s)
{
    if (d != OR_TMP0)
        gen_op_mov_TN_reg[ot][0][d]();
    else
        gen_op_ld_T0_A0[ot + s1->mem_index]();
    if (s != OR_TMP1)
        gen_op_mov_TN_reg[ot][1][s]();
    /* for zero counts, flags are not updated, so must do it dynamically */
    if (s1->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s1->cc_op);
    
    if (d != OR_TMP0)
        gen_op_shift_T0_T1_cc[ot][op]();
    else
        gen_op_shift_mem_T0_T1_cc[ot + s1->mem_index][op]();
    if (d != OR_TMP0)
        gen_op_mov_reg_T0[ot][d]();
    s1->cc_op = CC_OP_DYNAMIC; /* cannot predict flags after */
}

static void gen_shifti(DisasContext *s1, int op, int ot, int d, int c)
{
    /* currently not optimized */
    gen_op_movl_T1_im(c);
    gen_shift(s1, op, ot, d, OR_TMP1);
}

static void gen_lea_modrm(DisasContext *s, int modrm, int *reg_ptr, int *offset_ptr)
{
    int havesib;
    int base, disp;
    int index;
    int scale;
    int opreg;
    int mod, rm, code, override, must_add_seg;

    override = s->override;
    must_add_seg = s->addseg;
    if (override >= 0)
        must_add_seg = 1;
    mod = (modrm >> 6) & 3;
    rm = modrm & 7;

    if (s->aflag) {

        havesib = 0;
        base = rm;
        index = 0;
        scale = 0;
        
        if (base == 4) {
            havesib = 1;
            code = ldub_code(s->pc++);
            scale = (code >> 6) & 3;
            index = (code >> 3) & 7;
            base = code & 7;
        }

        switch (mod) {
        case 0:
            if (base == 5) {
                base = -1;
                disp = ldl_code(s->pc);
                s->pc += 4;
            } else {
                disp = 0;
            }
            break;
        case 1:
            disp = (int8_t)ldub_code(s->pc++);
            break;
        default:
        case 2:
            disp = ldl_code(s->pc);
            s->pc += 4;
            break;
        }
        
        if (base >= 0) {
            /* for correct popl handling with esp */
            if (base == 4 && s->popl_esp_hack)
                disp += s->popl_esp_hack;
            gen_op_movl_A0_reg[base]();
            if (disp != 0)
                gen_op_addl_A0_im(disp);
        } else {
            gen_op_movl_A0_im(disp);
        }
        /* XXX: index == 4 is always invalid */
        if (havesib && (index != 4 || scale != 0)) {
            gen_op_addl_A0_reg_sN[scale][index]();
        }
        if (must_add_seg) {
            if (override < 0) {
                if (base == R_EBP || base == R_ESP)
                    override = R_SS;
                else
                    override = R_DS;
            }
            gen_op_addl_A0_seg(offsetof(CPUX86State,segs[override].base));
        }
    } else {
        switch (mod) {
        case 0:
            if (rm == 6) {
                disp = lduw_code(s->pc);
                s->pc += 2;
                gen_op_movl_A0_im(disp);
                rm = 0; /* avoid SS override */
                goto no_rm;
            } else {
                disp = 0;
            }
            break;
        case 1:
            disp = (int8_t)ldub_code(s->pc++);
            break;
        default:
        case 2:
            disp = lduw_code(s->pc);
            s->pc += 2;
            break;
        }
        switch(rm) {
        case 0:
            gen_op_movl_A0_reg[R_EBX]();
            gen_op_addl_A0_reg_sN[0][R_ESI]();
            break;
        case 1:
            gen_op_movl_A0_reg[R_EBX]();
            gen_op_addl_A0_reg_sN[0][R_EDI]();
            break;
        case 2:
            gen_op_movl_A0_reg[R_EBP]();
            gen_op_addl_A0_reg_sN[0][R_ESI]();
            break;
        case 3:
            gen_op_movl_A0_reg[R_EBP]();
            gen_op_addl_A0_reg_sN[0][R_EDI]();
            break;
        case 4:
            gen_op_movl_A0_reg[R_ESI]();
            break;
        case 5:
            gen_op_movl_A0_reg[R_EDI]();
            break;
        case 6:
            gen_op_movl_A0_reg[R_EBP]();
            break;
        default:
        case 7:
            gen_op_movl_A0_reg[R_EBX]();
            break;
        }
        if (disp != 0)
            gen_op_addl_A0_im(disp);
        gen_op_andl_A0_ffff();
    no_rm:
        if (must_add_seg) {
            if (override < 0) {
                if (rm == 2 || rm == 3 || rm == 6)
                    override = R_SS;
                else
                    override = R_DS;
            }
            gen_op_addl_A0_seg(offsetof(CPUX86State,segs[override].base));
        }
    }

    opreg = OR_A0;
    disp = 0;
    *reg_ptr = opreg;
    *offset_ptr = disp;
}

/* generate modrm memory load or store of 'reg'. TMP0 is used if reg !=
   OR_TMP0 */
static void gen_ldst_modrm(DisasContext *s, int modrm, int ot, int reg, int is_store)
{
    int mod, rm, opreg, disp;

    mod = (modrm >> 6) & 3;
    rm = modrm & 7;
    if (mod == 3) {
        if (is_store) {
            if (reg != OR_TMP0)
                gen_op_mov_TN_reg[ot][0][reg]();
            gen_op_mov_reg_T0[ot][rm]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
            if (reg != OR_TMP0)
                gen_op_mov_reg_T0[ot][reg]();
        }
    } else {
        gen_lea_modrm(s, modrm, &opreg, &disp);
        if (is_store) {
            if (reg != OR_TMP0)
                gen_op_mov_TN_reg[ot][0][reg]();
            gen_op_st_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_ld_T0_A0[ot + s->mem_index]();
            if (reg != OR_TMP0)
                gen_op_mov_reg_T0[ot][reg]();
        }
    }
}

static inline uint32_t insn_get(DisasContext *s, int ot)
{
    uint32_t ret;

    switch(ot) {
    case OT_BYTE:
        ret = ldub_code(s->pc);
        s->pc++;
        break;
    case OT_WORD:
        ret = lduw_code(s->pc);
        s->pc += 2;
        break;
    default:
    case OT_LONG:
        ret = ldl_code(s->pc);
        s->pc += 4;
        break;
    }
    return ret;
}

static inline void gen_jcc(DisasContext *s, int b, int val, int next_eip)
{
    TranslationBlock *tb;
    int inv, jcc_op;
    GenOpFunc3 *func;

    inv = b & 1;
    jcc_op = (b >> 1) & 7;
    
    if (s->jmp_opt) {
        switch(s->cc_op) {
            /* we optimize the cmp/jcc case */
        case CC_OP_SUBB:
        case CC_OP_SUBW:
        case CC_OP_SUBL:
            func = gen_jcc_sub[s->cc_op - CC_OP_SUBB][jcc_op];
            break;
            
            /* some jumps are easy to compute */
        case CC_OP_ADDB:
        case CC_OP_ADDW:
        case CC_OP_ADDL:
        case CC_OP_ADCB:
        case CC_OP_ADCW:
        case CC_OP_ADCL:
        case CC_OP_SBBB:
        case CC_OP_SBBW:
        case CC_OP_SBBL:
        case CC_OP_LOGICB:
        case CC_OP_LOGICW:
        case CC_OP_LOGICL:
        case CC_OP_INCB:
        case CC_OP_INCW:
        case CC_OP_INCL:
        case CC_OP_DECB:
        case CC_OP_DECW:
        case CC_OP_DECL:
        case CC_OP_SHLB:
        case CC_OP_SHLW:
        case CC_OP_SHLL:
        case CC_OP_SARB:
        case CC_OP_SARW:
        case CC_OP_SARL:
            switch(jcc_op) {
            case JCC_Z:
                func = gen_jcc_sub[(s->cc_op - CC_OP_ADDB) % 3][jcc_op];
                break;
            case JCC_S:
                func = gen_jcc_sub[(s->cc_op - CC_OP_ADDB) % 3][jcc_op];
                break;
            default:
                func = NULL;
                break;
            }
            break;
        default:
            func = NULL;
            break;
        }

        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);

        if (!func) {
            gen_setcc_slow[jcc_op]();
            func = gen_op_jcc;
        }
    
        tb = s->tb;
        if (!inv) {
            func((long)tb, val, next_eip);
        } else {
            func((long)tb, next_eip, val);
        }
        s->is_jmp = 3;
    } else {
        if (s->cc_op != CC_OP_DYNAMIC) {
            gen_op_set_cc_op(s->cc_op);
            s->cc_op = CC_OP_DYNAMIC;
        }
        gen_setcc_slow[jcc_op]();
        if (!inv) {
            gen_op_jcc_im(val, next_eip);
        } else {
            gen_op_jcc_im(next_eip, val);
        }
        gen_eob(s);
    }
}

static void gen_setcc(DisasContext *s, int b)
{
    int inv, jcc_op;
    GenOpFunc *func;

    inv = b & 1;
    jcc_op = (b >> 1) & 7;
    switch(s->cc_op) {
        /* we optimize the cmp/jcc case */
    case CC_OP_SUBB:
    case CC_OP_SUBW:
    case CC_OP_SUBL:
        func = gen_setcc_sub[s->cc_op - CC_OP_SUBB][jcc_op];
        if (!func)
            goto slow_jcc;
        break;
        
        /* some jumps are easy to compute */
    case CC_OP_ADDB:
    case CC_OP_ADDW:
    case CC_OP_ADDL:
    case CC_OP_LOGICB:
    case CC_OP_LOGICW:
    case CC_OP_LOGICL:
    case CC_OP_INCB:
    case CC_OP_INCW:
    case CC_OP_INCL:
    case CC_OP_DECB:
    case CC_OP_DECW:
    case CC_OP_DECL:
    case CC_OP_SHLB:
    case CC_OP_SHLW:
    case CC_OP_SHLL:
        switch(jcc_op) {
        case JCC_Z:
            func = gen_setcc_sub[(s->cc_op - CC_OP_ADDB) % 3][jcc_op];
            break;
        case JCC_S:
            func = gen_setcc_sub[(s->cc_op - CC_OP_ADDB) % 3][jcc_op];
            break;
        default:
            goto slow_jcc;
        }
        break;
    default:
    slow_jcc:
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        func = gen_setcc_slow[jcc_op];
        break;
    }
    func();
    if (inv) {
        gen_op_xor_T0_1();
    }
}

/* move T0 to seg_reg and compute if the CPU state may change. Never
   call this function with seg_reg == R_CS */
static void gen_movl_seg_T0(DisasContext *s, int seg_reg, unsigned int cur_eip)
{
    if (s->pe && !s->vm86) {
        /* XXX: optimize by finding processor state dynamically */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_jmp_im(cur_eip);
        gen_op_movl_seg_T0(seg_reg);
    } else {
        gen_op_movl_seg_T0_vm(offsetof(CPUX86State,segs[seg_reg]));
    }
    /* abort translation because the register may have a non zero base
       or because ss32 may change. For R_SS, translation must always
       stop as a special handling must be done to disable hardware
       interrupts for the next instruction */
    if (seg_reg == R_SS || (!s->addseg && seg_reg < R_FS))
        s->is_jmp = 3;
}

static inline void gen_stack_update(DisasContext *s, int addend)
{
    if (s->ss32) {
        if (addend == 2)
            gen_op_addl_ESP_2();
        else if (addend == 4)
            gen_op_addl_ESP_4();
        else 
            gen_op_addl_ESP_im(addend);
    } else {
        if (addend == 2)
            gen_op_addw_ESP_2();
        else if (addend == 4)
            gen_op_addw_ESP_4();
        else
            gen_op_addw_ESP_im(addend);
    }
}

/* generate a push. It depends on ss32, addseg and dflag */
static void gen_push_T0(DisasContext *s)
{
    gen_op_movl_A0_reg[R_ESP]();
    if (!s->dflag)
        gen_op_subl_A0_2();
    else
        gen_op_subl_A0_4();
    if (s->ss32) {
        if (s->addseg) {
            gen_op_movl_T1_A0();
            gen_op_addl_A0_SS();
        }
    } else {
        gen_op_andl_A0_ffff();
        gen_op_movl_T1_A0();
        gen_op_addl_A0_SS();
    }
    gen_op_st_T0_A0[s->dflag + 1 + s->mem_index]();
    if (s->ss32 && !s->addseg)
        gen_op_movl_ESP_A0();
    else
        gen_op_mov_reg_T1[s->ss32 + 1][R_ESP]();
}

/* generate a push. It depends on ss32, addseg and dflag */
/* slower version for T1, only used for call Ev */
static void gen_push_T1(DisasContext *s)
{
    gen_op_movl_A0_reg[R_ESP]();
    if (!s->dflag)
        gen_op_subl_A0_2();
    else
        gen_op_subl_A0_4();
    if (s->ss32) {
        if (s->addseg) {
            gen_op_addl_A0_SS();
        }
    } else {
        gen_op_andl_A0_ffff();
        gen_op_addl_A0_SS();
    }
    gen_op_st_T1_A0[s->dflag + 1 + s->mem_index]();
    
    if (s->ss32 && !s->addseg)
        gen_op_movl_ESP_A0();
    else
        gen_stack_update(s, (-2) << s->dflag);
}

/* two step pop is necessary for precise exceptions */
static void gen_pop_T0(DisasContext *s)
{
    gen_op_movl_A0_reg[R_ESP]();
    if (s->ss32) {
        if (s->addseg)
            gen_op_addl_A0_SS();
    } else {
        gen_op_andl_A0_ffff();
        gen_op_addl_A0_SS();
    }
    gen_op_ld_T0_A0[s->dflag + 1 + s->mem_index]();
}

static void gen_pop_update(DisasContext *s)
{
    gen_stack_update(s, 2 << s->dflag);
}

static void gen_stack_A0(DisasContext *s)
{
    gen_op_movl_A0_ESP();
    if (!s->ss32)
        gen_op_andl_A0_ffff();
    gen_op_movl_T1_A0();
    if (s->addseg)
        gen_op_addl_A0_seg(offsetof(CPUX86State,segs[R_SS].base));
}

/* NOTE: wrap around in 16 bit not fully handled */
static void gen_pusha(DisasContext *s)
{
    int i;
    gen_op_movl_A0_ESP();
    gen_op_addl_A0_im(-16 <<  s->dflag);
    if (!s->ss32)
        gen_op_andl_A0_ffff();
    gen_op_movl_T1_A0();
    if (s->addseg)
        gen_op_addl_A0_seg(offsetof(CPUX86State,segs[R_SS].base));
    for(i = 0;i < 8; i++) {
        gen_op_mov_TN_reg[OT_LONG][0][7 - i]();
        gen_op_st_T0_A0[OT_WORD + s->dflag + s->mem_index]();
        gen_op_addl_A0_im(2 <<  s->dflag);
    }
    gen_op_mov_reg_T1[OT_WORD + s->dflag][R_ESP]();
}

/* NOTE: wrap around in 16 bit not fully handled */
static void gen_popa(DisasContext *s)
{
    int i;
    gen_op_movl_A0_ESP();
    if (!s->ss32)
        gen_op_andl_A0_ffff();
    gen_op_movl_T1_A0();
    gen_op_addl_T1_im(16 <<  s->dflag);
    if (s->addseg)
        gen_op_addl_A0_seg(offsetof(CPUX86State,segs[R_SS].base));
    for(i = 0;i < 8; i++) {
        /* ESP is not reloaded */
        if (i != 3) {
            gen_op_ld_T0_A0[OT_WORD + s->dflag + s->mem_index]();
            gen_op_mov_reg_T0[OT_WORD + s->dflag][7 - i]();
        }
        gen_op_addl_A0_im(2 <<  s->dflag);
    }
    gen_op_mov_reg_T1[OT_WORD + s->dflag][R_ESP]();
}

/* NOTE: wrap around in 16 bit not fully handled */
/* XXX: check this */
static void gen_enter(DisasContext *s, int esp_addend, int level)
{
    int ot, level1, addend, opsize;

    ot = s->dflag + OT_WORD;
    level &= 0x1f;
    level1 = level;
    opsize = 2 << s->dflag;

    gen_op_movl_A0_ESP();
    gen_op_addl_A0_im(-opsize);
    if (!s->ss32)
        gen_op_andl_A0_ffff();
    gen_op_movl_T1_A0();
    if (s->addseg)
        gen_op_addl_A0_seg(offsetof(CPUX86State,segs[R_SS].base));
    /* push bp */
    gen_op_mov_TN_reg[OT_LONG][0][R_EBP]();
    gen_op_st_T0_A0[ot + s->mem_index]();
    if (level) {
        while (level--) {
            gen_op_addl_A0_im(-opsize);
            gen_op_addl_T0_im(-opsize);
            gen_op_st_T0_A0[ot + s->mem_index]();
        }
        gen_op_addl_A0_im(-opsize);
        gen_op_st_T1_A0[ot + s->mem_index]();
    }
    gen_op_mov_reg_T1[ot][R_EBP]();
    addend = -esp_addend;
    if (level1)
        addend -= opsize * (level1 + 1);
    gen_op_addl_T1_im(addend);
    gen_op_mov_reg_T1[ot][R_ESP]();
}

static void gen_exception(DisasContext *s, int trapno, unsigned int cur_eip)
{
    if (s->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s->cc_op);
    gen_op_jmp_im(cur_eip);
    gen_op_raise_exception(trapno);
    s->is_jmp = 3;
}

/* an interrupt is different from an exception because of the
   priviledge checks */
static void gen_interrupt(DisasContext *s, int intno, 
                          unsigned int cur_eip, unsigned int next_eip)
{
    if (s->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s->cc_op);
    gen_op_jmp_im(cur_eip);
    gen_op_raise_interrupt(intno, next_eip);
    s->is_jmp = 3;
}

static void gen_debug(DisasContext *s, unsigned int cur_eip)
{
    if (s->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s->cc_op);
    gen_op_jmp_im(cur_eip);
    gen_op_debug();
    s->is_jmp = 3;
}

/* generate a generic end of block. Trace exception is also generated
   if needed */
static void gen_eob(DisasContext *s)
{
    if (s->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s->cc_op);
    if (s->tb->flags & HF_INHIBIT_IRQ_MASK) {
        gen_op_reset_inhibit_irq();
    }
    if (s->singlestep_enabled) {
        gen_op_debug();
    } else if (s->tf) {
        gen_op_raise_exception(EXCP01_SSTP);
    } else {
        gen_op_movl_T0_0();
        gen_op_exit_tb();
    }
    s->is_jmp = 3;
}

/* generate a jump to eip. No segment change must happen before as a
   direct call to the next block may occur */
static void gen_jmp(DisasContext *s, unsigned int eip)
{
    TranslationBlock *tb = s->tb;

    if (s->jmp_opt) {
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_jmp((long)tb, eip);
        s->is_jmp = 3;
    } else {
        gen_op_jmp_im(eip);
        gen_eob(s);
    }
}

/* convert one instruction. s->is_jmp is set if the translation must
   be stopped. Return the next pc value */
static uint8_t *disas_insn(DisasContext *s, uint8_t *pc_start)
{
    int b, prefixes, aflag, dflag;
    int shift, ot;
    int modrm, reg, rm, mod, reg_addr, op, opreg, offset_addr, val;
    unsigned int next_eip;

    s->pc = pc_start;
    prefixes = 0;
    aflag = s->code32;
    dflag = s->code32;
    s->override = -1;
 next_byte:
    b = ldub_code(s->pc);
    s->pc++;
    /* check prefixes */
    switch (b) {
    case 0xf3:
        prefixes |= PREFIX_REPZ;
        goto next_byte;
    case 0xf2:
        prefixes |= PREFIX_REPNZ;
        goto next_byte;
    case 0xf0:
        prefixes |= PREFIX_LOCK;
        goto next_byte;
    case 0x2e:
        s->override = R_CS;
        goto next_byte;
    case 0x36:
        s->override = R_SS;
        goto next_byte;
    case 0x3e:
        s->override = R_DS;
        goto next_byte;
    case 0x26:
        s->override = R_ES;
        goto next_byte;
    case 0x64:
        s->override = R_FS;
        goto next_byte;
    case 0x65:
        s->override = R_GS;
        goto next_byte;
    case 0x66:
        prefixes |= PREFIX_DATA;
        goto next_byte;
    case 0x67:
        prefixes |= PREFIX_ADR;
        goto next_byte;
    }

    if (prefixes & PREFIX_DATA)
        dflag ^= 1;
    if (prefixes & PREFIX_ADR)
        aflag ^= 1;

    s->prefix = prefixes;
    s->aflag = aflag;
    s->dflag = dflag;

    /* lock generation */
    if (prefixes & PREFIX_LOCK)
        gen_op_lock();

    /* now check op code */
 reswitch:
    switch(b) {
    case 0x0f:
        /**************************/
        /* extended op code */
        b = ldub_code(s->pc++) | 0x100;
        goto reswitch;
        
        /**************************/
        /* arith & logic */
    case 0x00 ... 0x05:
    case 0x08 ... 0x0d:
    case 0x10 ... 0x15:
    case 0x18 ... 0x1d:
    case 0x20 ... 0x25:
    case 0x28 ... 0x2d:
    case 0x30 ... 0x35:
    case 0x38 ... 0x3d:
        {
            int op, f, val;
            op = (b >> 3) & 7;
            f = (b >> 1) & 3;

            if ((b & 1) == 0)
                ot = OT_BYTE;
            else
                ot = dflag ? OT_LONG : OT_WORD;
            
            switch(f) {
            case 0: /* OP Ev, Gv */
                modrm = ldub_code(s->pc++);
                reg = ((modrm >> 3) & 7);
                mod = (modrm >> 6) & 3;
                rm = modrm & 7;
                if (mod != 3) {
                    gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                    opreg = OR_TMP0;
                } else if (op == OP_XORL && rm == reg) {
                xor_zero:
                    /* xor reg, reg optimisation */
                    gen_op_movl_T0_0();
                    s->cc_op = CC_OP_LOGICB + ot;
                    gen_op_mov_reg_T0[ot][reg]();
                    gen_op_update1_cc();
                    break;
                } else {
                    opreg = rm;
                }
                gen_op_mov_TN_reg[ot][1][reg]();
                gen_op(s, op, ot, opreg);
                break;
            case 1: /* OP Gv, Ev */
                modrm = ldub_code(s->pc++);
                mod = (modrm >> 6) & 3;
                reg = ((modrm >> 3) & 7);
                rm = modrm & 7;
                if (mod != 3) {
                    gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                    gen_op_ld_T1_A0[ot + s->mem_index]();
                } else if (op == OP_XORL && rm == reg) {
                    goto xor_zero;
                } else {
                    gen_op_mov_TN_reg[ot][1][rm]();
                }
                gen_op(s, op, ot, reg);
                break;
            case 2: /* OP A, Iv */
                val = insn_get(s, ot);
                gen_op_movl_T1_im(val);
                gen_op(s, op, ot, OR_EAX);
                break;
            }
        }
        break;

    case 0x80: /* GRP1 */
    case 0x81:
    case 0x82:
    case 0x83:
        {
            int val;

            if ((b & 1) == 0)
                ot = OT_BYTE;
            else
                ot = dflag ? OT_LONG : OT_WORD;
            
            modrm = ldub_code(s->pc++);
            mod = (modrm >> 6) & 3;
            rm = modrm & 7;
            op = (modrm >> 3) & 7;
            
            if (mod != 3) {
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                opreg = OR_TMP0;
            } else {
                opreg = rm + OR_EAX;
            }

            switch(b) {
            default:
            case 0x80:
            case 0x81:
            case 0x82:
                val = insn_get(s, ot);
                break;
            case 0x83:
                val = (int8_t)insn_get(s, OT_BYTE);
                break;
            }
            gen_op_movl_T1_im(val);
            gen_op(s, op, ot, opreg);
        }
        break;

        /**************************/
        /* inc, dec, and other misc arith */
    case 0x40 ... 0x47: /* inc Gv */
        ot = dflag ? OT_LONG : OT_WORD;
        gen_inc(s, ot, OR_EAX + (b & 7), 1);
        break;
    case 0x48 ... 0x4f: /* dec Gv */
        ot = dflag ? OT_LONG : OT_WORD;
        gen_inc(s, ot, OR_EAX + (b & 7), -1);
        break;
    case 0xf6: /* GRP3 */
    case 0xf7:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        op = (modrm >> 3) & 7;
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }

        switch(op) {
        case 0: /* test */
            val = insn_get(s, ot);
            gen_op_movl_T1_im(val);
            gen_op_testl_T0_T1_cc();
            s->cc_op = CC_OP_LOGICB + ot;
            break;
        case 2: /* not */
            gen_op_notl_T0();
            if (mod != 3) {
                gen_op_st_T0_A0[ot + s->mem_index]();
            } else {
                gen_op_mov_reg_T0[ot][rm]();
            }
            break;
        case 3: /* neg */
            gen_op_negl_T0();
            if (mod != 3) {
                gen_op_st_T0_A0[ot + s->mem_index]();
            } else {
                gen_op_mov_reg_T0[ot][rm]();
            }
            gen_op_update_neg_cc();
            s->cc_op = CC_OP_SUBB + ot;
            break;
        case 4: /* mul */
            switch(ot) {
            case OT_BYTE:
                gen_op_mulb_AL_T0();
                s->cc_op = CC_OP_MULB;
                break;
            case OT_WORD:
                gen_op_mulw_AX_T0();
                s->cc_op = CC_OP_MULW;
                break;
            default:
            case OT_LONG:
                gen_op_mull_EAX_T0();
                s->cc_op = CC_OP_MULL;
                break;
            }
            break;
        case 5: /* imul */
            switch(ot) {
            case OT_BYTE:
                gen_op_imulb_AL_T0();
                s->cc_op = CC_OP_MULB;
                break;
            case OT_WORD:
                gen_op_imulw_AX_T0();
                s->cc_op = CC_OP_MULW;
                break;
            default:
            case OT_LONG:
                gen_op_imull_EAX_T0();
                s->cc_op = CC_OP_MULL;
                break;
            }
            break;
        case 6: /* div */
            switch(ot) {
            case OT_BYTE:
                gen_op_divb_AL_T0(pc_start - s->cs_base);
                break;
            case OT_WORD:
                gen_op_divw_AX_T0(pc_start - s->cs_base);
                break;
            default:
            case OT_LONG:
                gen_op_divl_EAX_T0(pc_start - s->cs_base);
                break;
            }
            break;
        case 7: /* idiv */
            switch(ot) {
            case OT_BYTE:
                gen_op_idivb_AL_T0(pc_start - s->cs_base);
                break;
            case OT_WORD:
                gen_op_idivw_AX_T0(pc_start - s->cs_base);
                break;
            default:
            case OT_LONG:
                gen_op_idivl_EAX_T0(pc_start - s->cs_base);
                break;
            }
            break;
        default:
            goto illegal_op;
        }
        break;

    case 0xfe: /* GRP4 */
    case 0xff: /* GRP5 */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        op = (modrm >> 3) & 7;
        if (op >= 2 && b == 0xfe) {
            goto illegal_op;
        }
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            if (op >= 2 && op != 3 && op != 5)
                gen_op_ld_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }

        switch(op) {
        case 0: /* inc Ev */
            if (mod != 3)
                opreg = OR_TMP0;
            else
                opreg = rm;
            gen_inc(s, ot, opreg, 1);
            break;
        case 1: /* dec Ev */
            if (mod != 3)
                opreg = OR_TMP0;
            else
                opreg = rm;
            gen_inc(s, ot, opreg, -1);
            break;
        case 2: /* call Ev */
            /* XXX: optimize if memory (no 'and' is necessary) */
            if (s->dflag == 0)
                gen_op_andl_T0_ffff();
            next_eip = s->pc - s->cs_base;
            gen_op_movl_T1_im(next_eip);
            gen_push_T1(s);
            gen_op_jmp_T0();
            gen_eob(s);
            break;
        case 3: /* lcall Ev */
            gen_op_ld_T1_A0[ot + s->mem_index]();
            gen_op_addl_A0_im(1 << (ot - OT_WORD + 1));
            gen_op_ldu_T0_A0[OT_WORD + s->mem_index]();
        do_lcall:
            if (s->pe && !s->vm86) {
                if (s->cc_op != CC_OP_DYNAMIC)
                    gen_op_set_cc_op(s->cc_op);
                gen_op_jmp_im(pc_start - s->cs_base);
                gen_op_lcall_protected_T0_T1(dflag, s->pc - s->cs_base);
            } else {
                gen_op_lcall_real_T0_T1(dflag, s->pc - s->cs_base);
            }
            gen_eob(s);
            break;
        case 4: /* jmp Ev */
            if (s->dflag == 0)
                gen_op_andl_T0_ffff();
            gen_op_jmp_T0();
            gen_eob(s);
            break;
        case 5: /* ljmp Ev */
            gen_op_ld_T1_A0[ot + s->mem_index]();
            gen_op_addl_A0_im(1 << (ot - OT_WORD + 1));
            gen_op_ldu_T0_A0[OT_WORD + s->mem_index]();
        do_ljmp:
            if (s->pe && !s->vm86) {
                if (s->cc_op != CC_OP_DYNAMIC)
                    gen_op_set_cc_op(s->cc_op);
                gen_op_jmp_im(pc_start - s->cs_base);
                gen_op_ljmp_protected_T0_T1(s->pc - s->cs_base);
            } else {
                gen_op_movl_seg_T0_vm(offsetof(CPUX86State,segs[R_CS]));
                gen_op_movl_T0_T1();
                gen_op_jmp_T0();
            }
            gen_eob(s);
            break;
        case 6: /* push Ev */
            gen_push_T0(s);
            break;
        default:
            goto illegal_op;
        }
        break;

    case 0x84: /* test Ev, Gv */
    case 0x85: 
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        reg = (modrm >> 3) & 7;
        
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        gen_op_mov_TN_reg[ot][1][reg + OR_EAX]();
        gen_op_testl_T0_T1_cc();
        s->cc_op = CC_OP_LOGICB + ot;
        break;
        
    case 0xa8: /* test eAX, Iv */
    case 0xa9:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        val = insn_get(s, ot);

        gen_op_mov_TN_reg[ot][0][OR_EAX]();
        gen_op_movl_T1_im(val);
        gen_op_testl_T0_T1_cc();
        s->cc_op = CC_OP_LOGICB + ot;
        break;
        
    case 0x98: /* CWDE/CBW */
        if (dflag)
            gen_op_movswl_EAX_AX();
        else
            gen_op_movsbw_AX_AL();
        break;
    case 0x99: /* CDQ/CWD */
        if (dflag)
            gen_op_movslq_EDX_EAX();
        else
            gen_op_movswl_DX_AX();
        break;
    case 0x1af: /* imul Gv, Ev */
    case 0x69: /* imul Gv, Ev, I */
    case 0x6b:
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = ((modrm >> 3) & 7) + OR_EAX;
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        if (b == 0x69) {
            val = insn_get(s, ot);
            gen_op_movl_T1_im(val);
        } else if (b == 0x6b) {
            val = (int8_t)insn_get(s, OT_BYTE);
            gen_op_movl_T1_im(val);
        } else {
            gen_op_mov_TN_reg[ot][1][reg]();
        }

        if (ot == OT_LONG) {
            gen_op_imull_T0_T1();
        } else {
            gen_op_imulw_T0_T1();
        }
        gen_op_mov_reg_T0[ot][reg]();
        s->cc_op = CC_OP_MULB + ot;
        break;
    case 0x1c0:
    case 0x1c1: /* xadd Ev, Gv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        if (mod == 3) {
            rm = modrm & 7;
            gen_op_mov_TN_reg[ot][0][reg]();
            gen_op_mov_TN_reg[ot][1][rm]();
            gen_op_addl_T0_T1();
            gen_op_mov_reg_T1[ot][reg]();
            gen_op_mov_reg_T0[ot][rm]();
        } else {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_mov_TN_reg[ot][0][reg]();
            gen_op_ld_T1_A0[ot + s->mem_index]();
            gen_op_addl_T0_T1();
            gen_op_st_T0_A0[ot + s->mem_index]();
            gen_op_mov_reg_T1[ot][reg]();
        }
        gen_op_update2_cc();
        s->cc_op = CC_OP_ADDB + ot;
        break;
    case 0x1b0:
    case 0x1b1: /* cmpxchg Ev, Gv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        gen_op_mov_TN_reg[ot][1][reg]();
        if (mod == 3) {
            rm = modrm & 7;
            gen_op_mov_TN_reg[ot][0][rm]();
            gen_op_cmpxchg_T0_T1_EAX_cc[ot]();
            gen_op_mov_reg_T0[ot][rm]();
        } else {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot + s->mem_index]();
            gen_op_cmpxchg_mem_T0_T1_EAX_cc[ot + s->mem_index]();
        }
        s->cc_op = CC_OP_SUBB + ot;
        break;
    case 0x1c7: /* cmpxchg8b */
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        gen_op_cmpxchg8b();
        s->cc_op = CC_OP_EFLAGS;
        break;
        
        /**************************/
        /* push/pop */
    case 0x50 ... 0x57: /* push */
        gen_op_mov_TN_reg[OT_LONG][0][b & 7]();
        gen_push_T0(s);
        break;
    case 0x58 ... 0x5f: /* pop */
        ot = dflag ? OT_LONG : OT_WORD;
        gen_pop_T0(s);
        /* NOTE: order is important for pop %sp */
        gen_pop_update(s);
        gen_op_mov_reg_T0[ot][b & 7]();
        break;
    case 0x60: /* pusha */
        gen_pusha(s);
        break;
    case 0x61: /* popa */
        gen_popa(s);
        break;
    case 0x68: /* push Iv */
    case 0x6a:
        ot = dflag ? OT_LONG : OT_WORD;
        if (b == 0x68)
            val = insn_get(s, ot);
        else
            val = (int8_t)insn_get(s, OT_BYTE);
        gen_op_movl_T0_im(val);
        gen_push_T0(s);
        break;
    case 0x8f: /* pop Ev */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        gen_pop_T0(s);
        if (mod == 3) {
            /* NOTE: order is important for pop %sp */
            gen_pop_update(s);
            rm = modrm & 7;
            gen_op_mov_reg_T0[ot][rm]();
        } else {
            /* NOTE: order is important too for MMU exceptions */
            s->popl_esp_hack = 2 << dflag;
            gen_ldst_modrm(s, modrm, ot, OR_TMP0, 1);
            s->popl_esp_hack = 0;
            gen_pop_update(s);
        }
        break;
    case 0xc8: /* enter */
        {
            int level;
            val = lduw_code(s->pc);
            s->pc += 2;
            level = ldub_code(s->pc++);
            gen_enter(s, val, level);
        }
        break;
    case 0xc9: /* leave */
        /* XXX: exception not precise (ESP is updated before potential exception) */
        if (s->ss32) {
            gen_op_mov_TN_reg[OT_LONG][0][R_EBP]();
            gen_op_mov_reg_T0[OT_LONG][R_ESP]();
        } else {
            gen_op_mov_TN_reg[OT_WORD][0][R_EBP]();
            gen_op_mov_reg_T0[OT_WORD][R_ESP]();
        }
        gen_pop_T0(s);
        ot = dflag ? OT_LONG : OT_WORD;
        gen_op_mov_reg_T0[ot][R_EBP]();
        gen_pop_update(s);
        break;
    case 0x06: /* push es */
    case 0x0e: /* push cs */
    case 0x16: /* push ss */
    case 0x1e: /* push ds */
        gen_op_movl_T0_seg(b >> 3);
        gen_push_T0(s);
        break;
    case 0x1a0: /* push fs */
    case 0x1a8: /* push gs */
        gen_op_movl_T0_seg((b >> 3) & 7);
        gen_push_T0(s);
        break;
    case 0x07: /* pop es */
    case 0x17: /* pop ss */
    case 0x1f: /* pop ds */
        reg = b >> 3;
        gen_pop_T0(s);
        gen_movl_seg_T0(s, reg, pc_start - s->cs_base);
        gen_pop_update(s);
        if (reg == R_SS) {
            /* if reg == SS, inhibit interrupts/trace. */
            /* If several instructions disable interrupts, only the
               _first_ does it */
            if (!(s->tb->flags & HF_INHIBIT_IRQ_MASK))
                gen_op_set_inhibit_irq();
            s->tf = 0;
        }
        if (s->is_jmp) {
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_eob(s);
        }
        break;
    case 0x1a1: /* pop fs */
    case 0x1a9: /* pop gs */
        gen_pop_T0(s);
        gen_movl_seg_T0(s, (b >> 3) & 7, pc_start - s->cs_base);
        gen_pop_update(s);
        if (s->is_jmp) {
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_eob(s);
        }
        break;

        /**************************/
        /* mov */
    case 0x88:
    case 0x89: /* mov Gv, Ev */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        
        /* generate a generic store */
        gen_ldst_modrm(s, modrm, ot, OR_EAX + reg, 1);
        break;
    case 0xc6:
    case 0xc7: /* mov Ev, Iv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod != 3)
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        val = insn_get(s, ot);
        gen_op_movl_T0_im(val);
        if (mod != 3)
            gen_op_st_T0_A0[ot + s->mem_index]();
        else
            gen_op_mov_reg_T0[ot][modrm & 7]();
        break;
    case 0x8a:
    case 0x8b: /* mov Ev, Gv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        gen_op_mov_reg_T0[ot][reg]();
        break;
    case 0x8e: /* mov seg, Gv */
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        if (reg >= 6 || reg == R_CS)
            goto illegal_op;
        gen_ldst_modrm(s, modrm, OT_WORD, OR_TMP0, 0);
        gen_movl_seg_T0(s, reg, pc_start - s->cs_base);
        if (reg == R_SS) {
            /* if reg == SS, inhibit interrupts/trace */
            /* If several instructions disable interrupts, only the
               _first_ does it */
            if (!(s->tb->flags & HF_INHIBIT_IRQ_MASK))
                gen_op_set_inhibit_irq();
            s->tf = 0;
        }
        if (s->is_jmp) {
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_eob(s);
        }
        break;
    case 0x8c: /* mov Gv, seg */
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        if (reg >= 6)
            goto illegal_op;
        gen_op_movl_T0_seg(reg);
        ot = OT_WORD;
        if (mod == 3 && dflag)
            ot = OT_LONG;
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 1);
        break;

    case 0x1b6: /* movzbS Gv, Eb */
    case 0x1b7: /* movzwS Gv, Eb */
    case 0x1be: /* movsbS Gv, Eb */
    case 0x1bf: /* movswS Gv, Eb */
        {
            int d_ot;
            /* d_ot is the size of destination */
            d_ot = dflag + OT_WORD;
            /* ot is the size of source */
            ot = (b & 1) + OT_BYTE;
            modrm = ldub_code(s->pc++);
            reg = ((modrm >> 3) & 7) + OR_EAX;
            mod = (modrm >> 6) & 3;
            rm = modrm & 7;
            
            if (mod == 3) {
                gen_op_mov_TN_reg[ot][0][rm]();
                switch(ot | (b & 8)) {
                case OT_BYTE:
                    gen_op_movzbl_T0_T0();
                    break;
                case OT_BYTE | 8:
                    gen_op_movsbl_T0_T0();
                    break;
                case OT_WORD:
                    gen_op_movzwl_T0_T0();
                    break;
                default:
                case OT_WORD | 8:
                    gen_op_movswl_T0_T0();
                    break;
                }
                gen_op_mov_reg_T0[d_ot][reg]();
            } else {
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                if (b & 8) {
                    gen_op_lds_T0_A0[ot + s->mem_index]();
                } else {
                    gen_op_ldu_T0_A0[ot + s->mem_index]();
                }
                gen_op_mov_reg_T0[d_ot][reg]();
            }
        }
        break;

    case 0x8d: /* lea */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        reg = (modrm >> 3) & 7;
        /* we must ensure that no segment is added */
        s->override = -1;
        val = s->addseg;
        s->addseg = 0;
        gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        s->addseg = val;
        gen_op_mov_reg_A0[ot - OT_WORD][reg]();
        break;
        
    case 0xa0: /* mov EAX, Ov */
    case 0xa1:
    case 0xa2: /* mov Ov, EAX */
    case 0xa3:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (s->aflag)
            offset_addr = insn_get(s, OT_LONG);
        else
            offset_addr = insn_get(s, OT_WORD);
        gen_op_movl_A0_im(offset_addr);
        /* handle override */
        {
            int override, must_add_seg;
            must_add_seg = s->addseg;
            if (s->override >= 0) {
                override = s->override;
                must_add_seg = 1;
            } else {
                override = R_DS;
            }
            if (must_add_seg) {
                gen_op_addl_A0_seg(offsetof(CPUX86State,segs[override].base));
            }
        }
        if ((b & 2) == 0) {
            gen_op_ld_T0_A0[ot + s->mem_index]();
            gen_op_mov_reg_T0[ot][R_EAX]();
        } else {
            gen_op_mov_TN_reg[ot][0][R_EAX]();
            gen_op_st_T0_A0[ot + s->mem_index]();
        }
        break;
    case 0xd7: /* xlat */
        gen_op_movl_A0_reg[R_EBX]();
        gen_op_addl_A0_AL();
        if (s->aflag == 0)
            gen_op_andl_A0_ffff();
        /* handle override */
        {
            int override, must_add_seg;
            must_add_seg = s->addseg;
            override = R_DS;
            if (s->override >= 0) {
                override = s->override;
                must_add_seg = 1;
            } else {
                override = R_DS;
            }
            if (must_add_seg) {
                gen_op_addl_A0_seg(offsetof(CPUX86State,segs[override].base));
            }
        }
        gen_op_ldu_T0_A0[OT_BYTE + s->mem_index]();
        gen_op_mov_reg_T0[OT_BYTE][R_EAX]();
        break;
    case 0xb0 ... 0xb7: /* mov R, Ib */
        val = insn_get(s, OT_BYTE);
        gen_op_movl_T0_im(val);
        gen_op_mov_reg_T0[OT_BYTE][b & 7]();
        break;
    case 0xb8 ... 0xbf: /* mov R, Iv */
        ot = dflag ? OT_LONG : OT_WORD;
        val = insn_get(s, ot);
        reg = OR_EAX + (b & 7);
        gen_op_movl_T0_im(val);
        gen_op_mov_reg_T0[ot][reg]();
        break;

    case 0x91 ... 0x97: /* xchg R, EAX */
        ot = dflag ? OT_LONG : OT_WORD;
        reg = b & 7;
        rm = R_EAX;
        goto do_xchg_reg;
    case 0x86:
    case 0x87: /* xchg Ev, Gv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        if (mod == 3) {
            rm = modrm & 7;
        do_xchg_reg:
            gen_op_mov_TN_reg[ot][0][reg]();
            gen_op_mov_TN_reg[ot][1][rm]();
            gen_op_mov_reg_T0[ot][rm]();
            gen_op_mov_reg_T1[ot][reg]();
        } else {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_mov_TN_reg[ot][0][reg]();
            /* for xchg, lock is implicit */
            if (!(prefixes & PREFIX_LOCK))
                gen_op_lock();
            gen_op_ld_T1_A0[ot + s->mem_index]();
            gen_op_st_T0_A0[ot + s->mem_index]();
            if (!(prefixes & PREFIX_LOCK))
                gen_op_unlock();
            gen_op_mov_reg_T1[ot][reg]();
        }
        break;
    case 0xc4: /* les Gv */
        op = R_ES;
        goto do_lxx;
    case 0xc5: /* lds Gv */
        op = R_DS;
        goto do_lxx;
    case 0x1b2: /* lss Gv */
        op = R_SS;
        goto do_lxx;
    case 0x1b4: /* lfs Gv */
        op = R_FS;
        goto do_lxx;
    case 0x1b5: /* lgs Gv */
        op = R_GS;
    do_lxx:
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        gen_op_ld_T1_A0[ot + s->mem_index]();
        gen_op_addl_A0_im(1 << (ot - OT_WORD + 1));
        /* load the segment first to handle exceptions properly */
        gen_op_ldu_T0_A0[OT_WORD + s->mem_index]();
        gen_movl_seg_T0(s, op, pc_start - s->cs_base);
        /* then put the data */
        gen_op_mov_reg_T1[ot][reg]();
        if (s->is_jmp) {
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_eob(s);
        }
        break;
        
        /************************/
        /* shifts */
    case 0xc0:
    case 0xc1:
        /* shift Ev,Ib */
        shift = 2;
    grp2:
        {
            if ((b & 1) == 0)
                ot = OT_BYTE;
            else
                ot = dflag ? OT_LONG : OT_WORD;
            
            modrm = ldub_code(s->pc++);
            mod = (modrm >> 6) & 3;
            rm = modrm & 7;
            op = (modrm >> 3) & 7;
            
            if (mod != 3) {
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                opreg = OR_TMP0;
            } else {
                opreg = rm + OR_EAX;
            }

            /* simpler op */
            if (shift == 0) {
                gen_shift(s, op, ot, opreg, OR_ECX);
            } else {
                if (shift == 2) {
                    shift = ldub_code(s->pc++);
                }
                gen_shifti(s, op, ot, opreg, shift);
            }
        }
        break;
    case 0xd0:
    case 0xd1:
        /* shift Ev,1 */
        shift = 1;
        goto grp2;
    case 0xd2:
    case 0xd3:
        /* shift Ev,cl */
        shift = 0;
        goto grp2;

    case 0x1a4: /* shld imm */
        op = 0;
        shift = 1;
        goto do_shiftd;
    case 0x1a5: /* shld cl */
        op = 0;
        shift = 0;
        goto do_shiftd;
    case 0x1ac: /* shrd imm */
        op = 1;
        shift = 1;
        goto do_shiftd;
    case 0x1ad: /* shrd cl */
        op = 1;
        shift = 0;
    do_shiftd:
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        reg = (modrm >> 3) & 7;
        
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }
        gen_op_mov_TN_reg[ot][1][reg]();
        
        if (shift) {
            val = ldub_code(s->pc++);
            val &= 0x1f;
            if (val) {
                if (mod == 3)
                    gen_op_shiftd_T0_T1_im_cc[ot][op](val);
                else
                    gen_op_shiftd_mem_T0_T1_im_cc[ot + s->mem_index][op](val);
                if (op == 0 && ot != OT_WORD)
                    s->cc_op = CC_OP_SHLB + ot;
                else
                    s->cc_op = CC_OP_SARB + ot;
            }
        } else {
            if (s->cc_op != CC_OP_DYNAMIC)
                gen_op_set_cc_op(s->cc_op);
            if (mod == 3)
                gen_op_shiftd_T0_T1_ECX_cc[ot][op]();
            else
                gen_op_shiftd_mem_T0_T1_ECX_cc[ot + s->mem_index][op]();
            s->cc_op = CC_OP_DYNAMIC; /* cannot predict flags after */
        }
        if (mod == 3) {
            gen_op_mov_reg_T0[ot][rm]();
        }
        break;

        /************************/
        /* floats */
    case 0xd8 ... 0xdf: 
        if (s->flags & (HF_EM_MASK | HF_TS_MASK)) {
            /* if CR0.EM or CR0.TS are set, generate an FPU exception */
            /* XXX: what to do if illegal op ? */
            gen_exception(s, EXCP07_PREX, pc_start - s->cs_base);
            break;
        }
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        op = ((b & 7) << 3) | ((modrm >> 3) & 7);
        if (mod != 3) {
            /* memory op */
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            switch(op) {
            case 0x00 ... 0x07: /* fxxxs */
            case 0x10 ... 0x17: /* fixxxl */
            case 0x20 ... 0x27: /* fxxxl */
            case 0x30 ... 0x37: /* fixxx */
                {
                    int op1;
                    op1 = op & 7;

                    switch(op >> 4) {
                    case 0:
                        gen_op_flds_FT0_A0();
                        break;
                    case 1:
                        gen_op_fildl_FT0_A0();
                        break;
                    case 2:
                        gen_op_fldl_FT0_A0();
                        break;
                    case 3:
                    default:
                        gen_op_fild_FT0_A0();
                        break;
                    }
                    
                    gen_op_fp_arith_ST0_FT0[op1]();
                    if (op1 == 3) {
                        /* fcomp needs pop */
                        gen_op_fpop();
                    }
                }
                break;
            case 0x08: /* flds */
            case 0x0a: /* fsts */
            case 0x0b: /* fstps */
            case 0x18: /* fildl */
            case 0x1a: /* fistl */
            case 0x1b: /* fistpl */
            case 0x28: /* fldl */
            case 0x2a: /* fstl */
            case 0x2b: /* fstpl */
            case 0x38: /* filds */
            case 0x3a: /* fists */
            case 0x3b: /* fistps */
                
                switch(op & 7) {
                case 0:
                    switch(op >> 4) {
                    case 0:
                        gen_op_flds_ST0_A0();
                        break;
                    case 1:
                        gen_op_fildl_ST0_A0();
                        break;
                    case 2:
                        gen_op_fldl_ST0_A0();
                        break;
                    case 3:
                    default:
                        gen_op_fild_ST0_A0();
                        break;
                    }
                    break;
                default:
                    switch(op >> 4) {
                    case 0:
                        gen_op_fsts_ST0_A0();
                        break;
                    case 1:
                        gen_op_fistl_ST0_A0();
                        break;
                    case 2:
                        gen_op_fstl_ST0_A0();
                        break;
                    case 3:
                    default:
                        gen_op_fist_ST0_A0();
                        break;
                    }
                    if ((op & 7) == 3)
                        gen_op_fpop();
                    break;
                }
                break;
            case 0x0c: /* fldenv mem */
                gen_op_fldenv_A0(s->dflag);
                break;
            case 0x0d: /* fldcw mem */
                gen_op_fldcw_A0();
                break;
            case 0x0e: /* fnstenv mem */
                gen_op_fnstenv_A0(s->dflag);
                break;
            case 0x0f: /* fnstcw mem */
                gen_op_fnstcw_A0();
                break;
            case 0x1d: /* fldt mem */
                gen_op_fldt_ST0_A0();
                break;
            case 0x1f: /* fstpt mem */
                gen_op_fstt_ST0_A0();
                gen_op_fpop();
                break;
            case 0x2c: /* frstor mem */
                gen_op_frstor_A0(s->dflag);
                break;
            case 0x2e: /* fnsave mem */
                gen_op_fnsave_A0(s->dflag);
                break;
            case 0x2f: /* fnstsw mem */
                gen_op_fnstsw_A0();
                break;
            case 0x3c: /* fbld */
                gen_op_fbld_ST0_A0();
                break;
            case 0x3e: /* fbstp */
                gen_op_fbst_ST0_A0();
                gen_op_fpop();
                break;
            case 0x3d: /* fildll */
                gen_op_fildll_ST0_A0();
                break;
            case 0x3f: /* fistpll */
                gen_op_fistll_ST0_A0();
                gen_op_fpop();
                break;
            default:
                goto illegal_op;
            }
        } else {
            /* register float ops */
            opreg = rm;

            switch(op) {
            case 0x08: /* fld sti */
                gen_op_fpush();
                gen_op_fmov_ST0_STN((opreg + 1) & 7);
                break;
            case 0x09: /* fxchg sti */
                gen_op_fxchg_ST0_STN(opreg);
                break;
            case 0x0a: /* grp d9/2 */
                switch(rm) {
                case 0: /* fnop */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x0c: /* grp d9/4 */
                switch(rm) {
                case 0: /* fchs */
                    gen_op_fchs_ST0();
                    break;
                case 1: /* fabs */
                    gen_op_fabs_ST0();
                    break;
                case 4: /* ftst */
                    gen_op_fldz_FT0();
                    gen_op_fcom_ST0_FT0();
                    break;
                case 5: /* fxam */
                    gen_op_fxam_ST0();
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x0d: /* grp d9/5 */
                {
                    switch(rm) {
                    case 0:
                        gen_op_fpush();
                        gen_op_fld1_ST0();
                        break;
                    case 1:
                        gen_op_fpush();
                        gen_op_fldl2t_ST0();
                        break;
                    case 2:
                        gen_op_fpush();
                        gen_op_fldl2e_ST0();
                        break;
                    case 3:
                        gen_op_fpush();
                        gen_op_fldpi_ST0();
                        break;
                    case 4:
                        gen_op_fpush();
                        gen_op_fldlg2_ST0();
                        break;
                    case 5:
                        gen_op_fpush();
                        gen_op_fldln2_ST0();
                        break;
                    case 6:
                        gen_op_fpush();
                        gen_op_fldz_ST0();
                        break;
                    default:
                        goto illegal_op;
                    }
                }
                break;
            case 0x0e: /* grp d9/6 */
                switch(rm) {
                case 0: /* f2xm1 */
                    gen_op_f2xm1();
                    break;
                case 1: /* fyl2x */
                    gen_op_fyl2x();
                    break;
                case 2: /* fptan */
                    gen_op_fptan();
                    break;
                case 3: /* fpatan */
                    gen_op_fpatan();
                    break;
                case 4: /* fxtract */
                    gen_op_fxtract();
                    break;
                case 5: /* fprem1 */
                    gen_op_fprem1();
                    break;
                case 6: /* fdecstp */
                    gen_op_fdecstp();
                    break;
                default:
                case 7: /* fincstp */
                    gen_op_fincstp();
                    break;
                }
                break;
            case 0x0f: /* grp d9/7 */
                switch(rm) {
                case 0: /* fprem */
                    gen_op_fprem();
                    break;
                case 1: /* fyl2xp1 */
                    gen_op_fyl2xp1();
                    break;
                case 2: /* fsqrt */
                    gen_op_fsqrt();
                    break;
                case 3: /* fsincos */
                    gen_op_fsincos();
                    break;
                case 5: /* fscale */
                    gen_op_fscale();
                    break;
                case 4: /* frndint */
                    gen_op_frndint();
                    break;
                case 6: /* fsin */
                    gen_op_fsin();
                    break;
                default:
                case 7: /* fcos */
                    gen_op_fcos();
                    break;
                }
                break;
            case 0x00: case 0x01: case 0x04 ... 0x07: /* fxxx st, sti */
            case 0x20: case 0x21: case 0x24 ... 0x27: /* fxxx sti, st */
            case 0x30: case 0x31: case 0x34 ... 0x37: /* fxxxp sti, st */
                {
                    int op1;
                    
                    op1 = op & 7;
                    if (op >= 0x20) {
                        gen_op_fp_arith_STN_ST0[op1](opreg);
                        if (op >= 0x30)
                            gen_op_fpop();
                    } else {
                        gen_op_fmov_FT0_STN(opreg);
                        gen_op_fp_arith_ST0_FT0[op1]();
                    }
                }
                break;
            case 0x02: /* fcom */
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fcom_ST0_FT0();
                break;
            case 0x03: /* fcomp */
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fcom_ST0_FT0();
                gen_op_fpop();
                break;
            case 0x15: /* da/5 */
                switch(rm) {
                case 1: /* fucompp */
                    gen_op_fmov_FT0_STN(1);
                    gen_op_fucom_ST0_FT0();
                    gen_op_fpop();
                    gen_op_fpop();
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x1c:
                switch(rm) {
                case 0: /* feni (287 only, just do nop here) */
                    break;
                case 1: /* fdisi (287 only, just do nop here) */
                    break;
                case 2: /* fclex */
                    gen_op_fclex();
                    break;
                case 3: /* fninit */
                    gen_op_fninit();
                    break;
                case 4: /* fsetpm (287 only, just do nop here) */
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x1d: /* fucomi */
                if (s->cc_op != CC_OP_DYNAMIC)
                    gen_op_set_cc_op(s->cc_op);
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fucomi_ST0_FT0();
                s->cc_op = CC_OP_EFLAGS;
                break;
            case 0x1e: /* fcomi */
                if (s->cc_op != CC_OP_DYNAMIC)
                    gen_op_set_cc_op(s->cc_op);
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fcomi_ST0_FT0();
                s->cc_op = CC_OP_EFLAGS;
                break;
            case 0x2a: /* fst sti */
                gen_op_fmov_STN_ST0(opreg);
                break;
            case 0x2b: /* fstp sti */
                gen_op_fmov_STN_ST0(opreg);
                gen_op_fpop();
                break;
            case 0x2c: /* fucom st(i) */
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fucom_ST0_FT0();
                break;
            case 0x2d: /* fucomp st(i) */
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fucom_ST0_FT0();
                gen_op_fpop();
                break;
            case 0x33: /* de/3 */
                switch(rm) {
                case 1: /* fcompp */
                    gen_op_fmov_FT0_STN(1);
                    gen_op_fcom_ST0_FT0();
                    gen_op_fpop();
                    gen_op_fpop();
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x3c: /* df/4 */
                switch(rm) {
                case 0:
                    gen_op_fnstsw_EAX();
                    break;
                default:
                    goto illegal_op;
                }
                break;
            case 0x3d: /* fucomip */
                if (s->cc_op != CC_OP_DYNAMIC)
                    gen_op_set_cc_op(s->cc_op);
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fucomi_ST0_FT0();
                gen_op_fpop();
                s->cc_op = CC_OP_EFLAGS;
                break;
            case 0x3e: /* fcomip */
                if (s->cc_op != CC_OP_DYNAMIC)
                    gen_op_set_cc_op(s->cc_op);
                gen_op_fmov_FT0_STN(opreg);
                gen_op_fcomi_ST0_FT0();
                gen_op_fpop();
                s->cc_op = CC_OP_EFLAGS;
                break;
            case 0x10 ... 0x13: /* fcmovxx */
            case 0x18 ... 0x1b:
                {
                    int op1;
                    const static uint8_t fcmov_cc[8] = {
                        (JCC_B << 1),
                        (JCC_Z << 1),
                        (JCC_BE << 1),
                        (JCC_P << 1),
                    };
                    op1 = fcmov_cc[op & 3] | ((op >> 3) & 1);
                    gen_setcc(s, op1);
                    gen_op_fcmov_ST0_STN_T0(opreg);
                }
                break;
            default:
                goto illegal_op;
            }
        }
#ifdef USE_CODE_COPY
        s->tb->cflags |= CF_TB_FP_USED;
#endif
        break;
        /************************/
        /* string ops */

    case 0xa4: /* movsS */
    case 0xa5:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        if (prefixes & (PREFIX_REPZ | PREFIX_REPNZ)) {
            gen_repz_movs(s, ot, pc_start - s->cs_base, s->pc - s->cs_base);
        } else {
            gen_movs(s, ot);
        }
        break;
        
    case 0xaa: /* stosS */
    case 0xab:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        if (prefixes & (PREFIX_REPZ | PREFIX_REPNZ)) {
            gen_repz_stos(s, ot, pc_start - s->cs_base, s->pc - s->cs_base);
        } else {
            gen_stos(s, ot);
        }
        break;
    case 0xac: /* lodsS */
    case 0xad:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & (PREFIX_REPZ | PREFIX_REPNZ)) {
            gen_repz_lods(s, ot, pc_start - s->cs_base, s->pc - s->cs_base);
        } else {
            gen_lods(s, ot);
        }
        break;
    case 0xae: /* scasS */
    case 0xaf:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
                ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPNZ) {
            gen_repz_scas(s, ot, pc_start - s->cs_base, s->pc - s->cs_base, 1);
        } else if (prefixes & PREFIX_REPZ) {
            gen_repz_scas(s, ot, pc_start - s->cs_base, s->pc - s->cs_base, 0);
        } else {
            gen_scas(s, ot);
            s->cc_op = CC_OP_SUBB + ot;
        }
        break;

    case 0xa6: /* cmpsS */
    case 0xa7:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPNZ) {
            gen_repz_cmps(s, ot, pc_start - s->cs_base, s->pc - s->cs_base, 1);
        } else if (prefixes & PREFIX_REPZ) {
            gen_repz_cmps(s, ot, pc_start - s->cs_base, s->pc - s->cs_base, 0);
        } else {
            gen_cmps(s, ot);
            s->cc_op = CC_OP_SUBB + ot;
        }
        break;
    case 0x6c: /* insS */
    case 0x6d:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        gen_check_io(s, ot, 1, pc_start - s->cs_base);
        if (prefixes & (PREFIX_REPZ | PREFIX_REPNZ)) {
            gen_repz_ins(s, ot, pc_start - s->cs_base, s->pc - s->cs_base);
        } else {
            gen_ins(s, ot);
        }
        break;
    case 0x6e: /* outsS */
    case 0x6f:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        gen_check_io(s, ot, 1, pc_start - s->cs_base);
        if (prefixes & (PREFIX_REPZ | PREFIX_REPNZ)) {
            gen_repz_outs(s, ot, pc_start - s->cs_base, s->pc - s->cs_base);
        } else {
            gen_outs(s, ot);
        }
        break;

        /************************/
        /* port I/O */
    case 0xe4:
    case 0xe5:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        val = ldub_code(s->pc++);
        gen_op_movl_T0_im(val);
        gen_check_io(s, ot, 0, pc_start - s->cs_base);
        gen_op_in[ot]();
        gen_op_mov_reg_T1[ot][R_EAX]();
        break;
    case 0xe6:
    case 0xe7:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        val = ldub_code(s->pc++);
        gen_op_movl_T0_im(val);
        gen_check_io(s, ot, 0, pc_start - s->cs_base);
        gen_op_mov_TN_reg[ot][1][R_EAX]();
        gen_op_out[ot]();
        break;
    case 0xec:
    case 0xed:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        gen_op_mov_TN_reg[OT_WORD][0][R_EDX]();
        gen_op_andl_T0_ffff();
        gen_check_io(s, ot, 0, pc_start - s->cs_base);
        gen_op_in[ot]();
        gen_op_mov_reg_T1[ot][R_EAX]();
        break;
    case 0xee:
    case 0xef:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        gen_op_mov_TN_reg[OT_WORD][0][R_EDX]();
        gen_op_andl_T0_ffff();
        gen_check_io(s, ot, 0, pc_start - s->cs_base);
        gen_op_mov_TN_reg[ot][1][R_EAX]();
        gen_op_out[ot]();
        break;

        /************************/
        /* control */
    case 0xc2: /* ret im */
        val = ldsw_code(s->pc);
        s->pc += 2;
        gen_pop_T0(s);
        gen_stack_update(s, val + (2 << s->dflag));
        if (s->dflag == 0)
            gen_op_andl_T0_ffff();
        gen_op_jmp_T0();
        gen_eob(s);
        break;
    case 0xc3: /* ret */
        gen_pop_T0(s);
        gen_pop_update(s);
        if (s->dflag == 0)
            gen_op_andl_T0_ffff();
        gen_op_jmp_T0();
        gen_eob(s);
        break;
    case 0xca: /* lret im */
        val = ldsw_code(s->pc);
        s->pc += 2;
    do_lret:
        if (s->pe && !s->vm86) {
            if (s->cc_op != CC_OP_DYNAMIC)
                gen_op_set_cc_op(s->cc_op);
            gen_op_jmp_im(pc_start - s->cs_base);
            gen_op_lret_protected(s->dflag, val);
        } else {
            gen_stack_A0(s);
            /* pop offset */
            gen_op_ld_T0_A0[1 + s->dflag + s->mem_index]();
            if (s->dflag == 0)
                gen_op_andl_T0_ffff();
            /* NOTE: keeping EIP updated is not a problem in case of
               exception */
            gen_op_jmp_T0();
            /* pop selector */
            gen_op_addl_A0_im(2 << s->dflag);
            gen_op_ld_T0_A0[1 + s->dflag + s->mem_index]();
            gen_op_movl_seg_T0_vm(offsetof(CPUX86State,segs[R_CS]));
            /* add stack offset */
            gen_stack_update(s, val + (4 << s->dflag));
        }
        gen_eob(s);
        break;
    case 0xcb: /* lret */
        val = 0;
        goto do_lret;
    case 0xcf: /* iret */
        if (!s->pe) {
            /* real mode */
            gen_op_iret_real(s->dflag);
            s->cc_op = CC_OP_EFLAGS;
        } else if (s->vm86) {
            if (s->iopl != 3) {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            } else {
                gen_op_iret_real(s->dflag);
                s->cc_op = CC_OP_EFLAGS;
            }
        } else {
            if (s->cc_op != CC_OP_DYNAMIC)
                gen_op_set_cc_op(s->cc_op);
            gen_op_jmp_im(pc_start - s->cs_base);
            gen_op_iret_protected(s->dflag, s->pc - s->cs_base);
            s->cc_op = CC_OP_EFLAGS;
        }
        gen_eob(s);
        break;
    case 0xe8: /* call im */
        {
            unsigned int next_eip;
            ot = dflag ? OT_LONG : OT_WORD;
            val = insn_get(s, ot);
            next_eip = s->pc - s->cs_base;
            val += next_eip;
            if (s->dflag == 0)
                val &= 0xffff;
            gen_op_movl_T0_im(next_eip);
            gen_push_T0(s);
            gen_jmp(s, val);
        }
        break;
    case 0x9a: /* lcall im */
        {
            unsigned int selector, offset;

            ot = dflag ? OT_LONG : OT_WORD;
            offset = insn_get(s, ot);
            selector = insn_get(s, OT_WORD);
            
            gen_op_movl_T0_im(selector);
            gen_op_movl_T1_im(offset);
        }
        goto do_lcall;
    case 0xe9: /* jmp */
        ot = dflag ? OT_LONG : OT_WORD;
        val = insn_get(s, ot);
        val += s->pc - s->cs_base;
        if (s->dflag == 0)
            val = val & 0xffff;
        gen_jmp(s, val);
        break;
    case 0xea: /* ljmp im */
        {
            unsigned int selector, offset;

            ot = dflag ? OT_LONG : OT_WORD;
            offset = insn_get(s, ot);
            selector = insn_get(s, OT_WORD);
            
            gen_op_movl_T0_im(selector);
            gen_op_movl_T1_im(offset);
        }
        goto do_ljmp;
    case 0xeb: /* jmp Jb */
        val = (int8_t)insn_get(s, OT_BYTE);
        val += s->pc - s->cs_base;
        if (s->dflag == 0)
            val = val & 0xffff;
        gen_jmp(s, val);
        break;
    case 0x70 ... 0x7f: /* jcc Jb */
        val = (int8_t)insn_get(s, OT_BYTE);
        goto do_jcc;
    case 0x180 ... 0x18f: /* jcc Jv */
        if (dflag) {
            val = insn_get(s, OT_LONG);
        } else {
            val = (int16_t)insn_get(s, OT_WORD); 
        }
    do_jcc:
        next_eip = s->pc - s->cs_base;
        val += next_eip;
        if (s->dflag == 0)
            val &= 0xffff;
        gen_jcc(s, b, val, next_eip);
        break;

    case 0x190 ... 0x19f: /* setcc Gv */
        modrm = ldub_code(s->pc++);
        gen_setcc(s, b);
        gen_ldst_modrm(s, modrm, OT_BYTE, OR_TMP0, 1);
        break;
    case 0x140 ... 0x14f: /* cmov Gv, Ev */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        gen_setcc(s, b);
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T1_A0[ot + s->mem_index]();
        } else {
            rm = modrm & 7;
            gen_op_mov_TN_reg[ot][1][rm]();
        }
        gen_op_cmov_reg_T1_T0[ot - OT_WORD][reg]();
        break;
        
        /************************/
        /* flags */
    case 0x9c: /* pushf */
        if (s->vm86 && s->iopl != 3) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            if (s->cc_op != CC_OP_DYNAMIC)
                gen_op_set_cc_op(s->cc_op);
            gen_op_movl_T0_eflags();
            gen_push_T0(s);
        }
        break;
    case 0x9d: /* popf */
        if (s->vm86 && s->iopl != 3) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            gen_pop_T0(s);
            if (s->cpl == 0) {
                if (s->dflag) {
                    gen_op_movl_eflags_T0_cpl0();
                } else {
                    gen_op_movw_eflags_T0_cpl0();
                }
            } else {
                if (s->cpl <= s->iopl) {
                    if (s->dflag) {
                        gen_op_movl_eflags_T0_io();
                    } else {
                        gen_op_movw_eflags_T0_io();
                    }
                } else {
                    if (s->dflag) {
                        gen_op_movl_eflags_T0();
                    } else {
                        gen_op_movw_eflags_T0();
                    }
                }
            }
            gen_pop_update(s);
            s->cc_op = CC_OP_EFLAGS;
            /* abort translation because TF flag may change */
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_eob(s);
        }
        break;
    case 0x9e: /* sahf */
        gen_op_mov_TN_reg[OT_BYTE][0][R_AH]();
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_movb_eflags_T0();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0x9f: /* lahf */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_movl_T0_eflags();
        gen_op_mov_reg_T0[OT_BYTE][R_AH]();
        break;
    case 0xf5: /* cmc */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_cmc();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0xf8: /* clc */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_clc();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0xf9: /* stc */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_stc();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0xfc: /* cld */
        gen_op_cld();
        break;
    case 0xfd: /* std */
        gen_op_std();
        break;

        /************************/
        /* bit operations */
    case 0x1ba: /* bt/bts/btr/btc Gv, im */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        op = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }
        /* load shift */
        val = ldub_code(s->pc++);
        gen_op_movl_T1_im(val);
        if (op < 4)
            goto illegal_op;
        op -= 4;
        gen_op_btx_T0_T1_cc[ot - OT_WORD][op]();
        s->cc_op = CC_OP_SARB + ot;
        if (op != 0) {
            if (mod != 3)
                gen_op_st_T0_A0[ot + s->mem_index]();
            else
                gen_op_mov_reg_T0[ot][rm]();
            gen_op_update_bt_cc();
        }
        break;
    case 0x1a3: /* bt Gv, Ev */
        op = 0;
        goto do_btx;
    case 0x1ab: /* bts */
        op = 1;
        goto do_btx;
    case 0x1b3: /* btr */
        op = 2;
        goto do_btx;
    case 0x1bb: /* btc */
        op = 3;
    do_btx:
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        gen_op_mov_TN_reg[OT_LONG][1][reg]();
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            /* specific case: we need to add a displacement */
            if (ot == OT_WORD)
                gen_op_add_bitw_A0_T1();
            else
                gen_op_add_bitl_A0_T1();
            gen_op_ld_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }
        gen_op_btx_T0_T1_cc[ot - OT_WORD][op]();
        s->cc_op = CC_OP_SARB + ot;
        if (op != 0) {
            if (mod != 3)
                gen_op_st_T0_A0[ot + s->mem_index]();
            else
                gen_op_mov_reg_T0[ot][rm]();
            gen_op_update_bt_cc();
        }
        break;
    case 0x1bc: /* bsf */
    case 0x1bd: /* bsr */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        gen_op_bsx_T0_cc[ot - OT_WORD][b & 1]();
        /* NOTE: we always write back the result. Intel doc says it is
           undefined if T0 == 0 */
        gen_op_mov_reg_T0[ot][reg]();
        s->cc_op = CC_OP_LOGICB + ot;
        break;
        /************************/
        /* bcd */
    case 0x27: /* daa */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_daa();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0x2f: /* das */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_das();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0x37: /* aaa */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_aaa();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0x3f: /* aas */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_aas();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0xd4: /* aam */
        val = ldub_code(s->pc++);
        gen_op_aam(val);
        s->cc_op = CC_OP_LOGICB;
        break;
    case 0xd5: /* aad */
        val = ldub_code(s->pc++);
        gen_op_aad(val);
        s->cc_op = CC_OP_LOGICB;
        break;
        /************************/
        /* misc */
    case 0x90: /* nop */
        /* XXX: correct lock test for all insn */
        if (prefixes & PREFIX_LOCK)
            goto illegal_op;
        break;
    case 0x9b: /* fwait */
        if ((s->flags & (HF_MP_MASK | HF_TS_MASK)) == 
            (HF_MP_MASK | HF_TS_MASK)) {
            gen_exception(s, EXCP07_PREX, pc_start - s->cs_base);
        }
        break;
    case 0xcc: /* int3 */
        gen_interrupt(s, EXCP03_INT3, pc_start - s->cs_base, s->pc - s->cs_base);
        break;
    case 0xcd: /* int N */
        val = ldub_code(s->pc++);
        if (s->vm86 && s->iopl != 3) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base); 
        } else {
            gen_interrupt(s, val, pc_start - s->cs_base, s->pc - s->cs_base);
        }
        break;
    case 0xce: /* into */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_into(s->pc - s->cs_base);
        break;
    case 0xf1: /* icebp (undocumented, exits to external debugger) */
        gen_debug(s, pc_start - s->cs_base);
        break;
    case 0xfa: /* cli */
        if (!s->vm86) {
            if (s->cpl <= s->iopl) {
                gen_op_cli();
            } else {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            }
        } else {
            if (s->iopl == 3) {
                gen_op_cli();
            } else {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            }
        }
        break;
    case 0xfb: /* sti */
        if (!s->vm86) {
            if (s->cpl <= s->iopl) {
            gen_sti:
                gen_op_sti();
                /* interruptions are enabled only the first insn after sti */
                /* If several instructions disable interrupts, only the
                   _first_ does it */
                if (!(s->tb->flags & HF_INHIBIT_IRQ_MASK))
                    gen_op_set_inhibit_irq();
                /* give a chance to handle pending irqs */
                gen_op_jmp_im(s->pc - s->cs_base);
                gen_eob(s);
            } else {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            }
        } else {
            if (s->iopl == 3) {
                goto gen_sti;
            } else {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            }
        }
        break;
    case 0x62: /* bound */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        if (mod == 3)
            goto illegal_op;
        gen_op_mov_reg_T0[ot][reg]();
        gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        if (ot == OT_WORD)
            gen_op_boundw(pc_start - s->cs_base);
        else
            gen_op_boundl(pc_start - s->cs_base);
        break;
    case 0x1c8 ... 0x1cf: /* bswap reg */
        reg = b & 7;
        gen_op_mov_TN_reg[OT_LONG][0][reg]();
        gen_op_bswapl_T0();
        gen_op_mov_reg_T0[OT_LONG][reg]();
        break;
    case 0xd6: /* salc */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_salc();
        break;
    case 0xe0: /* loopnz */
    case 0xe1: /* loopz */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        /* FALL THRU */
    case 0xe2: /* loop */
    case 0xe3: /* jecxz */
        val = (int8_t)insn_get(s, OT_BYTE);
        next_eip = s->pc - s->cs_base;
        val += next_eip;
        if (s->dflag == 0)
            val &= 0xffff;
        gen_op_loop[s->aflag][b & 3](val, next_eip);
        gen_eob(s);
        break;
    case 0x130: /* wrmsr */
    case 0x132: /* rdmsr */
        if (s->cpl != 0) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            if (b & 2)
                gen_op_rdmsr();
            else
                gen_op_wrmsr();
        }
        break;
    case 0x131: /* rdtsc */
        gen_op_rdtsc();
        break;
    case 0x1a2: /* cpuid */
        gen_op_cpuid();
        break;
    case 0xf4: /* hlt */
        if (s->cpl != 0) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            if (s->cc_op != CC_OP_DYNAMIC)
                gen_op_set_cc_op(s->cc_op);
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_op_hlt();
            s->is_jmp = 3;
        }
        break;
    case 0x100:
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        op = (modrm >> 3) & 7;
        switch(op) {
        case 0: /* sldt */
            if (!s->pe || s->vm86)
                goto illegal_op;
            gen_op_movl_T0_env(offsetof(CPUX86State,ldt.selector));
            ot = OT_WORD;
            if (mod == 3)
                ot += s->dflag;
            gen_ldst_modrm(s, modrm, ot, OR_TMP0, 1);
            break;
        case 2: /* lldt */
            if (!s->pe || s->vm86)
                goto illegal_op;
            if (s->cpl != 0) {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            } else {
                gen_ldst_modrm(s, modrm, OT_WORD, OR_TMP0, 0);
                gen_op_jmp_im(pc_start - s->cs_base);
                gen_op_lldt_T0();
            }
            break;
        case 1: /* str */
            if (!s->pe || s->vm86)
                goto illegal_op;
            gen_op_movl_T0_env(offsetof(CPUX86State,tr.selector));
            ot = OT_WORD;
            if (mod == 3)
                ot += s->dflag;
            gen_ldst_modrm(s, modrm, ot, OR_TMP0, 1);
            break;
        case 3: /* ltr */
            if (!s->pe || s->vm86)
                goto illegal_op;
            if (s->cpl != 0) {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            } else {
                gen_ldst_modrm(s, modrm, OT_WORD, OR_TMP0, 0);
                gen_op_jmp_im(pc_start - s->cs_base);
                gen_op_ltr_T0();
            }
            break;
        case 4: /* verr */
        case 5: /* verw */
            if (!s->pe || s->vm86)
                goto illegal_op;
            gen_ldst_modrm(s, modrm, OT_WORD, OR_TMP0, 0);
            if (s->cc_op != CC_OP_DYNAMIC)
                gen_op_set_cc_op(s->cc_op);
            if (op == 4)
                gen_op_verr();
            else
                gen_op_verw();
            s->cc_op = CC_OP_EFLAGS;
            break;
        default:
            goto illegal_op;
        }
        break;
    case 0x101:
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        op = (modrm >> 3) & 7;
        switch(op) {
        case 0: /* sgdt */
        case 1: /* sidt */
            if (mod == 3)
                goto illegal_op;
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            if (op == 0)
                gen_op_movl_T0_env(offsetof(CPUX86State,gdt.limit));
            else
                gen_op_movl_T0_env(offsetof(CPUX86State,idt.limit));
            gen_op_st_T0_A0[OT_WORD + s->mem_index]();
            gen_op_addl_A0_im(2);
            if (op == 0)
                gen_op_movl_T0_env(offsetof(CPUX86State,gdt.base));
            else
                gen_op_movl_T0_env(offsetof(CPUX86State,idt.base));
            if (!s->dflag)
                gen_op_andl_T0_im(0xffffff);
            gen_op_st_T0_A0[OT_LONG + s->mem_index]();
            break;
        case 2: /* lgdt */
        case 3: /* lidt */
            if (mod == 3)
                goto illegal_op;
            if (s->cpl != 0) {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            } else {
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                gen_op_ld_T1_A0[OT_WORD + s->mem_index]();
                gen_op_addl_A0_im(2);
                gen_op_ld_T0_A0[OT_LONG + s->mem_index]();
                if (!s->dflag)
                    gen_op_andl_T0_im(0xffffff);
                if (op == 2) {
                    gen_op_movl_env_T0(offsetof(CPUX86State,gdt.base));
                    gen_op_movl_env_T1(offsetof(CPUX86State,gdt.limit));
                } else {
                    gen_op_movl_env_T0(offsetof(CPUX86State,idt.base));
                    gen_op_movl_env_T1(offsetof(CPUX86State,idt.limit));
                }
            }
            break;
        case 4: /* smsw */
            gen_op_movl_T0_env(offsetof(CPUX86State,cr[0]));
            gen_ldst_modrm(s, modrm, OT_WORD, OR_TMP0, 1);
            break;
        case 6: /* lmsw */
            if (s->cpl != 0) {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            } else {
                gen_ldst_modrm(s, modrm, OT_WORD, OR_TMP0, 0);
                gen_op_lmsw_T0();
                gen_op_jmp_im(s->pc - s->cs_base);
                gen_eob(s);
            }
            break;
        case 7: /* invlpg */
            if (s->cpl != 0) {
                gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
            } else {
                if (mod == 3)
                    goto illegal_op;
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                gen_op_invlpg_A0();
                gen_op_jmp_im(s->pc - s->cs_base);
                gen_eob(s);
            }
            break;
        default:
            goto illegal_op;
        }
        break;
    case 0x108: /* invd */
    case 0x109: /* wbinvd */
        if (s->cpl != 0) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            /* nothing to do */
        }
        break;
    case 0x63: /* arpl */
        if (!s->pe || s->vm86)
            goto illegal_op;
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_arpl();
        s->cc_op = CC_OP_EFLAGS;
        if (mod != 3) {
            gen_op_st_T0_A0[ot + s->mem_index]();
        } else {
            gen_op_mov_reg_T0[ot][rm]();
        }
        gen_op_arpl_update();
        break;
    case 0x102: /* lar */
    case 0x103: /* lsl */
        if (!s->pe || s->vm86)
            goto illegal_op;
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub_code(s->pc++);
        reg = (modrm >> 3) & 7;
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        gen_op_mov_TN_reg[ot][1][reg]();
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        if (b == 0x102)
            gen_op_lar();
        else
            gen_op_lsl();
        s->cc_op = CC_OP_EFLAGS;
        gen_op_mov_reg_T1[ot][reg]();
        break;
    case 0x118:
        modrm = ldub_code(s->pc++);
        mod = (modrm >> 6) & 3;
        op = (modrm >> 3) & 7;
        switch(op) {
        case 0: /* prefetchnta */
        case 1: /* prefetchnt0 */
        case 2: /* prefetchnt0 */
        case 3: /* prefetchnt0 */
            if (mod == 3)
                goto illegal_op;
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            /* nothing more to do */
            break;
        default:
            goto illegal_op;
        }
        break;
    case 0x120: /* mov reg, crN */
    case 0x122: /* mov crN, reg */
        if (s->cpl != 0) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            modrm = ldub_code(s->pc++);
            if ((modrm & 0xc0) != 0xc0)
                goto illegal_op;
            rm = modrm & 7;
            reg = (modrm >> 3) & 7;
            switch(reg) {
            case 0:
            case 2:
            case 3:
            case 4:
                if (b & 2) {
                    gen_op_mov_TN_reg[OT_LONG][0][rm]();
                    gen_op_movl_crN_T0(reg);
                    gen_op_jmp_im(s->pc - s->cs_base);
                    gen_eob(s);
                } else {
                    gen_op_movl_T0_env(offsetof(CPUX86State,cr[reg]));
                    gen_op_mov_reg_T0[OT_LONG][rm]();
                }
                break;
            default:
                goto illegal_op;
            }
        }
        break;
    case 0x121: /* mov reg, drN */
    case 0x123: /* mov drN, reg */
        if (s->cpl != 0) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            modrm = ldub_code(s->pc++);
            if ((modrm & 0xc0) != 0xc0)
                goto illegal_op;
            rm = modrm & 7;
            reg = (modrm >> 3) & 7;
            /* XXX: do it dynamically with CR4.DE bit */
            if (reg == 4 || reg == 5)
                goto illegal_op;
            if (b & 2) {
                gen_op_mov_TN_reg[OT_LONG][0][rm]();
                gen_op_movl_drN_T0(reg);
                gen_op_jmp_im(s->pc - s->cs_base);
                gen_eob(s);
            } else {
                gen_op_movl_T0_env(offsetof(CPUX86State,dr[reg]));
                gen_op_mov_reg_T0[OT_LONG][rm]();
            }
        }
        break;
    case 0x106: /* clts */
        if (s->cpl != 0) {
            gen_exception(s, EXCP0D_GPF, pc_start - s->cs_base);
        } else {
            gen_op_clts();
            /* abort block because static cpu state changed */
            gen_op_jmp_im(s->pc - s->cs_base);
            gen_eob(s);
        }
        break;
    default:
        goto illegal_op;
    }
    /* lock generation */
    if (s->prefix & PREFIX_LOCK)
        gen_op_unlock();
    return s->pc;
 illegal_op:
    if (s->prefix & PREFIX_LOCK)
        gen_op_unlock();
    /* XXX: ensure that no lock was generated */
    gen_exception(s, EXCP06_ILLOP, pc_start - s->cs_base);
    return s->pc;
}

#define CC_OSZAPC (CC_O | CC_S | CC_Z | CC_A | CC_P | CC_C)
#define CC_OSZAP (CC_O | CC_S | CC_Z | CC_A | CC_P)

/* flags read by an operation */
static uint16_t opc_read_flags[NB_OPS] = { 
    [INDEX_op_aas] = CC_A,
    [INDEX_op_aaa] = CC_A,
    [INDEX_op_das] = CC_A | CC_C,
    [INDEX_op_daa] = CC_A | CC_C,

    /* subtle: due to the incl/decl implementation, C is used */
    [INDEX_op_update_inc_cc] = CC_C, 

    [INDEX_op_into] = CC_O,

    [INDEX_op_jb_subb] = CC_C,
    [INDEX_op_jb_subw] = CC_C,
    [INDEX_op_jb_subl] = CC_C,

    [INDEX_op_jz_subb] = CC_Z,
    [INDEX_op_jz_subw] = CC_Z,
    [INDEX_op_jz_subl] = CC_Z,

    [INDEX_op_jbe_subb] = CC_Z | CC_C,
    [INDEX_op_jbe_subw] = CC_Z | CC_C,
    [INDEX_op_jbe_subl] = CC_Z | CC_C,

    [INDEX_op_js_subb] = CC_S,
    [INDEX_op_js_subw] = CC_S,
    [INDEX_op_js_subl] = CC_S,

    [INDEX_op_jl_subb] = CC_O | CC_S,
    [INDEX_op_jl_subw] = CC_O | CC_S,
    [INDEX_op_jl_subl] = CC_O | CC_S,

    [INDEX_op_jle_subb] = CC_O | CC_S | CC_Z,
    [INDEX_op_jle_subw] = CC_O | CC_S | CC_Z,
    [INDEX_op_jle_subl] = CC_O | CC_S | CC_Z,

    [INDEX_op_loopnzw] = CC_Z,
    [INDEX_op_loopnzl] = CC_Z,
    [INDEX_op_loopzw] = CC_Z,
    [INDEX_op_loopzl] = CC_Z,

    [INDEX_op_seto_T0_cc] = CC_O,
    [INDEX_op_setb_T0_cc] = CC_C,
    [INDEX_op_setz_T0_cc] = CC_Z,
    [INDEX_op_setbe_T0_cc] = CC_Z | CC_C,
    [INDEX_op_sets_T0_cc] = CC_S,
    [INDEX_op_setp_T0_cc] = CC_P,
    [INDEX_op_setl_T0_cc] = CC_O | CC_S,
    [INDEX_op_setle_T0_cc] = CC_O | CC_S | CC_Z,

    [INDEX_op_setb_T0_subb] = CC_C,
    [INDEX_op_setb_T0_subw] = CC_C,
    [INDEX_op_setb_T0_subl] = CC_C,

    [INDEX_op_setz_T0_subb] = CC_Z,
    [INDEX_op_setz_T0_subw] = CC_Z,
    [INDEX_op_setz_T0_subl] = CC_Z,

    [INDEX_op_setbe_T0_subb] = CC_Z | CC_C,
    [INDEX_op_setbe_T0_subw] = CC_Z | CC_C,
    [INDEX_op_setbe_T0_subl] = CC_Z | CC_C,

    [INDEX_op_sets_T0_subb] = CC_S,
    [INDEX_op_sets_T0_subw] = CC_S,
    [INDEX_op_sets_T0_subl] = CC_S,

    [INDEX_op_setl_T0_subb] = CC_O | CC_S,
    [INDEX_op_setl_T0_subw] = CC_O | CC_S,
    [INDEX_op_setl_T0_subl] = CC_O | CC_S,

    [INDEX_op_setle_T0_subb] = CC_O | CC_S | CC_Z,
    [INDEX_op_setle_T0_subw] = CC_O | CC_S | CC_Z,
    [INDEX_op_setle_T0_subl] = CC_O | CC_S | CC_Z,

    [INDEX_op_movl_T0_eflags] = CC_OSZAPC,
    [INDEX_op_cmc] = CC_C,
    [INDEX_op_salc] = CC_C,

    /* needed for correct flag optimisation before string ops */
    [INDEX_op_jz_ecxw] = CC_OSZAPC,
    [INDEX_op_jz_ecxl] = CC_OSZAPC,
    [INDEX_op_jz_ecxw_im] = CC_OSZAPC,
    [INDEX_op_jz_ecxl_im] = CC_OSZAPC,

#define DEF_READF(SUFFIX)\
    [INDEX_op_adcb ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_adcw ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_adcl ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_sbbb ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_sbbw ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_sbbl ## SUFFIX ## _T0_T1_cc] = CC_C,\
\
    [INDEX_op_rclb ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_rclw ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_rcll ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_rcrb ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_rcrw ## SUFFIX ## _T0_T1_cc] = CC_C,\
    [INDEX_op_rcrl ## SUFFIX ## _T0_T1_cc] = CC_C,


    DEF_READF( )
    DEF_READF(_raw)
#ifndef CONFIG_USER_ONLY
    DEF_READF(_kernel)
    DEF_READF(_user)
#endif
};

/* flags written by an operation */
static uint16_t opc_write_flags[NB_OPS] = { 
    [INDEX_op_update2_cc] = CC_OSZAPC,
    [INDEX_op_update1_cc] = CC_OSZAPC,
    [INDEX_op_cmpl_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_update_neg_cc] = CC_OSZAPC,
    /* subtle: due to the incl/decl implementation, C is used */
    [INDEX_op_update_inc_cc] = CC_OSZAPC, 
    [INDEX_op_testl_T0_T1_cc] = CC_OSZAPC,

    [INDEX_op_mulb_AL_T0] = CC_OSZAPC,
    [INDEX_op_imulb_AL_T0] = CC_OSZAPC,
    [INDEX_op_mulw_AX_T0] = CC_OSZAPC,
    [INDEX_op_imulw_AX_T0] = CC_OSZAPC,
    [INDEX_op_mull_EAX_T0] = CC_OSZAPC,
    [INDEX_op_imull_EAX_T0] = CC_OSZAPC,
    [INDEX_op_imulw_T0_T1] = CC_OSZAPC,
    [INDEX_op_imull_T0_T1] = CC_OSZAPC,
    
    /* bcd */
    [INDEX_op_aam] = CC_OSZAPC,
    [INDEX_op_aad] = CC_OSZAPC,
    [INDEX_op_aas] = CC_OSZAPC,
    [INDEX_op_aaa] = CC_OSZAPC,
    [INDEX_op_das] = CC_OSZAPC,
    [INDEX_op_daa] = CC_OSZAPC,

    [INDEX_op_movb_eflags_T0] = CC_S | CC_Z | CC_A | CC_P | CC_C,
    [INDEX_op_movw_eflags_T0] = CC_OSZAPC,
    [INDEX_op_movl_eflags_T0] = CC_OSZAPC,
    [INDEX_op_movw_eflags_T0_io] = CC_OSZAPC,
    [INDEX_op_movl_eflags_T0_io] = CC_OSZAPC,
    [INDEX_op_movw_eflags_T0_cpl0] = CC_OSZAPC,
    [INDEX_op_movl_eflags_T0_cpl0] = CC_OSZAPC,
    [INDEX_op_clc] = CC_C,
    [INDEX_op_stc] = CC_C,
    [INDEX_op_cmc] = CC_C,

    [INDEX_op_btw_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btl_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btsw_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btsl_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btrw_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btrl_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btcw_T0_T1_cc] = CC_OSZAPC,
    [INDEX_op_btcl_T0_T1_cc] = CC_OSZAPC,

    [INDEX_op_bsfw_T0_cc] = CC_OSZAPC,
    [INDEX_op_bsfl_T0_cc] = CC_OSZAPC,
    [INDEX_op_bsrw_T0_cc] = CC_OSZAPC,
    [INDEX_op_bsrl_T0_cc] = CC_OSZAPC,

    [INDEX_op_cmpxchgb_T0_T1_EAX_cc] = CC_OSZAPC,
    [INDEX_op_cmpxchgw_T0_T1_EAX_cc] = CC_OSZAPC,
    [INDEX_op_cmpxchgl_T0_T1_EAX_cc] = CC_OSZAPC,

    [INDEX_op_cmpxchg8b] = CC_Z,
    [INDEX_op_lar] = CC_Z,
    [INDEX_op_lsl] = CC_Z,
    [INDEX_op_fcomi_ST0_FT0] = CC_Z | CC_P | CC_C,
    [INDEX_op_fucomi_ST0_FT0] = CC_Z | CC_P | CC_C,

#define DEF_WRITEF(SUFFIX)\
    [INDEX_op_adcb ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_adcw ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_adcl ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_sbbb ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_sbbw ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_sbbl ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
\
    [INDEX_op_rolb ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rolw ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_roll ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rorb ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rorw ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rorl ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
\
    [INDEX_op_rclb ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rclw ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rcll ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rcrb ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rcrw ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
    [INDEX_op_rcrl ## SUFFIX ## _T0_T1_cc] = CC_O | CC_C,\
\
    [INDEX_op_shlb ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_shlw ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_shll ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
\
    [INDEX_op_shrb ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_shrw ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_shrl ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
\
    [INDEX_op_sarb ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_sarw ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
    [INDEX_op_sarl ## SUFFIX ## _T0_T1_cc] = CC_OSZAPC,\
\
    [INDEX_op_shldw ## SUFFIX ## _T0_T1_ECX_cc] = CC_OSZAPC,\
    [INDEX_op_shldl ## SUFFIX ## _T0_T1_ECX_cc] = CC_OSZAPC,\
    [INDEX_op_shldw ## SUFFIX ## _T0_T1_im_cc] = CC_OSZAPC,\
    [INDEX_op_shldl ## SUFFIX ## _T0_T1_im_cc] = CC_OSZAPC,\
\
    [INDEX_op_shrdw ## SUFFIX ## _T0_T1_ECX_cc] = CC_OSZAPC,\
    [INDEX_op_shrdl ## SUFFIX ## _T0_T1_ECX_cc] = CC_OSZAPC,\
    [INDEX_op_shrdw ## SUFFIX ## _T0_T1_im_cc] = CC_OSZAPC,\
    [INDEX_op_shrdl ## SUFFIX ## _T0_T1_im_cc] = CC_OSZAPC,\
\
    [INDEX_op_cmpxchgb ## SUFFIX ## _T0_T1_EAX_cc] = CC_OSZAPC,\
    [INDEX_op_cmpxchgw ## SUFFIX ## _T0_T1_EAX_cc] = CC_OSZAPC,\
    [INDEX_op_cmpxchgl ## SUFFIX ## _T0_T1_EAX_cc] = CC_OSZAPC,


    DEF_WRITEF( )
    DEF_WRITEF(_raw)
#ifndef CONFIG_USER_ONLY
    DEF_WRITEF(_kernel)
    DEF_WRITEF(_user)
#endif
};

/* simpler form of an operation if no flags need to be generated */
static uint16_t opc_simpler[NB_OPS] = { 
    [INDEX_op_update2_cc] = INDEX_op_nop,
    [INDEX_op_update1_cc] = INDEX_op_nop,
    [INDEX_op_update_neg_cc] = INDEX_op_nop,
#if 0
    /* broken: CC_OP logic must be rewritten */
    [INDEX_op_update_inc_cc] = INDEX_op_nop,
#endif

    [INDEX_op_shlb_T0_T1_cc] = INDEX_op_shlb_T0_T1,
    [INDEX_op_shlw_T0_T1_cc] = INDEX_op_shlw_T0_T1,
    [INDEX_op_shll_T0_T1_cc] = INDEX_op_shll_T0_T1,

    [INDEX_op_shrb_T0_T1_cc] = INDEX_op_shrb_T0_T1,
    [INDEX_op_shrw_T0_T1_cc] = INDEX_op_shrw_T0_T1,
    [INDEX_op_shrl_T0_T1_cc] = INDEX_op_shrl_T0_T1,

    [INDEX_op_sarb_T0_T1_cc] = INDEX_op_sarb_T0_T1,
    [INDEX_op_sarw_T0_T1_cc] = INDEX_op_sarw_T0_T1,
    [INDEX_op_sarl_T0_T1_cc] = INDEX_op_sarl_T0_T1,

#define DEF_SIMPLER(SUFFIX)\
    [INDEX_op_rolb ## SUFFIX ## _T0_T1_cc] = INDEX_op_rolb ## SUFFIX ## _T0_T1,\
    [INDEX_op_rolw ## SUFFIX ## _T0_T1_cc] = INDEX_op_rolw ## SUFFIX ## _T0_T1,\
    [INDEX_op_roll ## SUFFIX ## _T0_T1_cc] = INDEX_op_roll ## SUFFIX ## _T0_T1,\
\
    [INDEX_op_rorb ## SUFFIX ## _T0_T1_cc] = INDEX_op_rorb ## SUFFIX ## _T0_T1,\
    [INDEX_op_rorw ## SUFFIX ## _T0_T1_cc] = INDEX_op_rorw ## SUFFIX ## _T0_T1,\
    [INDEX_op_rorl ## SUFFIX ## _T0_T1_cc] = INDEX_op_rorl ## SUFFIX ## _T0_T1,

    DEF_SIMPLER( )
    DEF_SIMPLER(_raw)
#ifndef CONFIG_USER_ONLY
    DEF_SIMPLER(_kernel)
    DEF_SIMPLER(_user)
#endif
};

void optimize_flags_init(void)
{
    int i;
    /* put default values in arrays */
    for(i = 0; i < NB_OPS; i++) {
        if (opc_simpler[i] == 0)
            opc_simpler[i] = i;
    }
}

/* CPU flags computation optimization: we move backward thru the
   generated code to see which flags are needed. The operation is
   modified if suitable */
static void optimize_flags(uint16_t *opc_buf, int opc_buf_len)
{
    uint16_t *opc_ptr;
    int live_flags, write_flags, op;

    opc_ptr = opc_buf + opc_buf_len;
    /* live_flags contains the flags needed by the next instructions
       in the code. At the end of the bloc, we consider that all the
       flags are live. */
    live_flags = CC_OSZAPC;
    while (opc_ptr > opc_buf) {
        op = *--opc_ptr;
        /* if none of the flags written by the instruction is used,
           then we can try to find a simpler instruction */
        write_flags = opc_write_flags[op];
        if ((live_flags & write_flags) == 0) {
            *opc_ptr = opc_simpler[op];
        }
        /* compute the live flags before the instruction */
        live_flags &= ~write_flags;
        live_flags |= opc_read_flags[op];
    }
}

/* generate intermediate code in gen_opc_buf and gen_opparam_buf for
   basic block 'tb'. If search_pc is TRUE, also generate PC
   information for each intermediate instruction. */
static inline int gen_intermediate_code_internal(CPUState *env,
                                                 TranslationBlock *tb, 
                                                 int search_pc)
{
    DisasContext dc1, *dc = &dc1;
    uint8_t *pc_ptr;
    uint16_t *gen_opc_end;
    int flags, j, lj;
    uint8_t *pc_start;
    uint8_t *cs_base;
    
    /* generate intermediate code */
    pc_start = (uint8_t *)tb->pc;
    cs_base = (uint8_t *)tb->cs_base;
    flags = tb->flags;

    dc->pe = (flags >> HF_PE_SHIFT) & 1;
    dc->code32 = (flags >> HF_CS32_SHIFT) & 1;
    dc->ss32 = (flags >> HF_SS32_SHIFT) & 1;
    dc->addseg = (flags >> HF_ADDSEG_SHIFT) & 1;
    dc->f_st = 0;
    dc->vm86 = (flags >> VM_SHIFT) & 1;
    dc->cpl = (flags >> HF_CPL_SHIFT) & 3;
    dc->iopl = (flags >> IOPL_SHIFT) & 3;
    dc->tf = (flags >> TF_SHIFT) & 1;
    dc->singlestep_enabled = env->singlestep_enabled;
    dc->cc_op = CC_OP_DYNAMIC;
    dc->cs_base = cs_base;
    dc->tb = tb;
    dc->popl_esp_hack = 0;
    /* select memory access functions */
    dc->mem_index = 0;
    if (flags & HF_SOFTMMU_MASK) {
        if (dc->cpl == 3)
            dc->mem_index = 6;
        else
            dc->mem_index = 3;
    }
    dc->flags = flags;
    dc->jmp_opt = !(dc->tf || env->singlestep_enabled ||
                    (flags & HF_INHIBIT_IRQ_MASK)
#ifndef CONFIG_SOFTMMU
                    || (flags & HF_SOFTMMU_MASK)
#endif
                    );
#if 0
    /* check addseg logic */
    if (!dc->addseg && (dc->vm86 || !dc->pe))
        printf("ERROR addseg\n");
#endif

    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;

    dc->is_jmp = DISAS_NEXT;
    pc_ptr = pc_start;
    lj = -1;

    for(;;) {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == (unsigned long)pc_ptr) {
                    gen_debug(dc, pc_ptr - dc->cs_base);
                    break;
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
            gen_opc_pc[lj] = (uint32_t)pc_ptr;
            gen_opc_cc_op[lj] = dc->cc_op;
            gen_opc_instr_start[lj] = 1;
        }
        pc_ptr = disas_insn(dc, pc_ptr);
        /* stop translation if indicated */
        if (dc->is_jmp)
            break;
        /* if single step mode, we generate only one instruction and
           generate an exception */
        /* if irq were inhibited with HF_INHIBIT_IRQ_MASK, we clear
           the flag and abort the translation to give the irqs a
           change to be happen */
        if (dc->tf || dc->singlestep_enabled || 
            (flags & HF_INHIBIT_IRQ_MASK)) {
            gen_op_jmp_im(pc_ptr - dc->cs_base);
            gen_eob(dc);
            break;
        }
        /* if too long translation, stop generation too */
        if (gen_opc_ptr >= gen_opc_end ||
            (pc_ptr - pc_start) >= (TARGET_PAGE_SIZE - 32)) {
            gen_op_jmp_im(pc_ptr - dc->cs_base);
            gen_eob(dc);
            break;
        }
    }
    *gen_opc_ptr = INDEX_op_end;
    /* we don't forget to fill the last values */
    if (search_pc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
    }
        
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "----------------\n");
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
	disas(logfile, pc_start, pc_ptr - pc_start, 0, !dc->code32);
        fprintf(logfile, "\n");
        if (loglevel & CPU_LOG_TB_OP) {
            fprintf(logfile, "OP:\n");
            dump_ops(gen_opc_buf, gen_opparam_buf);
            fprintf(logfile, "\n");
        }
    }
#endif

    /* optimize flag computations */
    optimize_flags(gen_opc_buf, gen_opc_ptr - gen_opc_buf);

#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_OP_OPT) {
        fprintf(logfile, "AFTER FLAGS OPT:\n");
        dump_ops(gen_opc_buf, gen_opparam_buf);
        fprintf(logfile, "\n");
    }
#endif
    if (!search_pc)
        tb->size = pc_ptr - pc_start;
    return 0;
}

int gen_intermediate_code(CPUState *env, TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 0);
}

int gen_intermediate_code_pc(CPUState *env, TranslationBlock *tb)
{
    return gen_intermediate_code_internal(env, tb, 1);
}

