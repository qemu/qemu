#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>

/* dump all code */
#define DEBUG_DISAS
#define DEBUG_LOGFILE "/tmp/gemu.log"

#ifdef DEBUG_DISAS
#include "dis-asm.h"
#endif

#define IN_OP_I386
#include "cpu-i386.h"

static uint8_t *gen_code_ptr;
int __op_param1, __op_param2, __op_param3;

#ifdef DEBUG_DISAS
static FILE *logfile = NULL;
#endif

/* supress that */
static void error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

#define PREFIX_REPZ 1
#define PREFIX_REPNZ 2
#define PREFIX_LOCK 4
#define PREFIX_CS 8
#define PREFIX_SS 0x10
#define PREFIX_DS 0x20
#define PREFIX_ES 0x40
#define PREFIX_FS 0x80
#define PREFIX_GS 0x100
#define PREFIX_DATA 0x200
#define PREFIX_ADR 0x400
#define PREFIX_FWAIT 0x800

typedef struct DisasContext {
    /* current insn context */
    int prefix;
    int aflag, dflag;
    uint8_t *pc; /* current pc */
    int cc_op; /* current CC operation */
    int f_st;
} DisasContext;

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

#include "op-i386.h"

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

    /* I386 float registers */
    OR_ST0,
    OR_ST1,
    OR_ST2,
    OR_ST3,
    OR_ST4,
    OR_ST5,
    OR_ST6,
    OR_ST7,
    OR_TMP0,    /* temporary operand register */
    OR_TMP1,
    OR_A0, /* temporary register used when doing address evaluation */
    OR_EFLAGS,  /* cpu flags */
    OR_ITMP0, /* used for byte/word insertion */
    OR_ITMP1, /* used for byte/word insertion */
    OR_ITMP2, /* used for byte/word insertion */
    OR_FTMP0, /* float temporary */
    OR_DF,    /* D flag, for string ops */
    OR_ZERO, /* fixed zero register */
    OR_IM, /* dummy immediate value register */
    NB_OREGS,
};

#if 0
static const double tab_const[7] = {
    1.0, 
    3.32192809488736234789, /* log2(10) */
    M_LOG2E,
    M_PI,
    0.30102999566398119521, /* log10(2) */
    M_LN2,
    0.0
};
#endif

typedef void (GenOpFunc)(void);
typedef void (GenOpFunc1)(long);
typedef void (GenOpFunc2)(long, long);
                    
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

static GenOpFunc *gen_op_arith_T0_T1_cc[8] = {
    gen_op_addl_T0_T1_cc,
    gen_op_orl_T0_T1_cc,
    gen_op_adcl_T0_T1_cc,
    gen_op_sbbl_T0_T1_cc,
    gen_op_andl_T0_T1_cc,
    gen_op_subl_T0_T1_cc,
    gen_op_xorl_T0_T1_cc,
    gen_op_cmpl_T0_T1_cc,
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

static GenOpFunc *gen_op_shift_T0_T1_cc[3][8] = {
    [OT_BYTE] = {
        gen_op_rolb_T0_T1_cc,
        gen_op_rorb_T0_T1_cc,
        gen_op_rclb_T0_T1_cc,
        gen_op_rcrb_T0_T1_cc,
        gen_op_shlb_T0_T1_cc,
        gen_op_shrb_T0_T1_cc,
        gen_op_shlb_T0_T1_cc,
        gen_op_sarb_T0_T1_cc,
    },
    [OT_WORD] = {
        gen_op_rolw_T0_T1_cc,
        gen_op_rorw_T0_T1_cc,
        gen_op_rclw_T0_T1_cc,
        gen_op_rcrw_T0_T1_cc,
        gen_op_shlw_T0_T1_cc,
        gen_op_shrw_T0_T1_cc,
        gen_op_shlw_T0_T1_cc,
        gen_op_sarw_T0_T1_cc,
    },
    [OT_LONG] = {
        gen_op_roll_T0_T1_cc,
        gen_op_rorl_T0_T1_cc,
        gen_op_rcll_T0_T1_cc,
        gen_op_rcrl_T0_T1_cc,
        gen_op_shll_T0_T1_cc,
        gen_op_shrl_T0_T1_cc,
        gen_op_shll_T0_T1_cc,
        gen_op_sarl_T0_T1_cc,
    },
};

static GenOpFunc *gen_op_lds_T0_A0[3] = {
    gen_op_ldsb_T0_A0,
    gen_op_ldsw_T0_A0,
};

static GenOpFunc *gen_op_ldu_T0_A0[3] = {
    gen_op_ldub_T0_A0,
    gen_op_lduw_T0_A0,
};

/* sign does not matter */
static GenOpFunc *gen_op_ld_T0_A0[3] = {
    gen_op_ldub_T0_A0,
    gen_op_lduw_T0_A0,
    gen_op_ldl_T0_A0,
};

static GenOpFunc *gen_op_ld_T1_A0[3] = {
    gen_op_ldub_T1_A0,
    gen_op_lduw_T1_A0,
    gen_op_ldl_T1_A0,
};

static GenOpFunc *gen_op_st_T0_A0[3] = {
    gen_op_stb_T0_A0,
    gen_op_stw_T0_A0,
    gen_op_stl_T0_A0,
};

static GenOpFunc *gen_op_movs[6] = {
    gen_op_movsb,
    gen_op_movsw,
    gen_op_movsl,
    gen_op_rep_movsb,
    gen_op_rep_movsw,
    gen_op_rep_movsl,
};

static GenOpFunc *gen_op_stos[6] = {
    gen_op_stosb,
    gen_op_stosw,
    gen_op_stosl,
    gen_op_rep_stosb,
    gen_op_rep_stosw,
    gen_op_rep_stosl,
};

static GenOpFunc *gen_op_lods[6] = {
    gen_op_lodsb,
    gen_op_lodsw,
    gen_op_lodsl,
    gen_op_rep_lodsb,
    gen_op_rep_lodsw,
    gen_op_rep_lodsl,
};

static GenOpFunc *gen_op_scas[9] = {
    gen_op_scasb,
    gen_op_scasw,
    gen_op_scasl,
    gen_op_repz_scasb,
    gen_op_repz_scasw,
    gen_op_repz_scasl,
    gen_op_repnz_scasb,
    gen_op_repnz_scasw,
    gen_op_repnz_scasl,
};

static GenOpFunc *gen_op_cmps[9] = {
    gen_op_cmpsb,
    gen_op_cmpsw,
    gen_op_cmpsl,
    gen_op_repz_cmpsb,
    gen_op_repz_cmpsw,
    gen_op_repz_cmpsl,
    gen_op_repnz_cmpsb,
    gen_op_repnz_cmpsw,
    gen_op_repnz_cmpsl,
};

static GenOpFunc *gen_op_ins[6] = {
    gen_op_insb,
    gen_op_insw,
    gen_op_insl,
    gen_op_rep_insb,
    gen_op_rep_insw,
    gen_op_rep_insl,
};


static GenOpFunc *gen_op_outs[6] = {
    gen_op_outsb,
    gen_op_outsw,
    gen_op_outsl,
    gen_op_rep_outsb,
    gen_op_rep_outsw,
    gen_op_rep_outsl,
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

static GenOpFunc2 *gen_jcc_slow[8] = {
    gen_op_jo_cc,
    gen_op_jb_cc,
    gen_op_jz_cc,
    gen_op_jbe_cc,
    gen_op_js_cc,
    gen_op_jp_cc,
    gen_op_jl_cc,
    gen_op_jle_cc,
};
    
static GenOpFunc2 *gen_jcc_sub[3][8] = {
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

static GenOpFunc1 *gen_op_fp_arith_STN_ST0[8] = {
    gen_op_fadd_STN_ST0,
    gen_op_fmul_STN_ST0,
    NULL,
    NULL,
    gen_op_fsub_STN_ST0,
    gen_op_fsubr_STN_ST0,
    gen_op_fdiv_STN_ST0,
    gen_op_fdivr_STN_ST0,
};

static void gen_op(DisasContext *s1, int op, int ot, int d, int s)
{
    if (d != OR_TMP0)
        gen_op_mov_TN_reg[ot][0][d]();
    if (s != OR_TMP1)
        gen_op_mov_TN_reg[ot][1][s]();
    if ((op == OP_ADCL || op == OP_SBBL) && s1->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s1->cc_op);
    gen_op_arith_T0_T1_cc[op]();
    if (d != OR_TMP0 && op != OP_CMPL)
        gen_op_mov_reg_T0[ot][d]();
    s1->cc_op = cc_op_arithb[op] + ot;
}

static void gen_opi(DisasContext *s1, int op, int ot, int d, int c)
{
    gen_op_movl_T1_im(c);
    gen_op(s1, op, ot, d, OR_TMP0);
}

static void gen_inc(DisasContext *s1, int ot, int d, int c)
{
    if (d != OR_TMP0)
        gen_op_mov_TN_reg[ot][0][d]();
    if (s1->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(s1->cc_op);
    if (c > 0)
        gen_op_incl_T0_cc();
    else
        gen_op_decl_T0_cc();
    if (d != OR_TMP0)
        gen_op_mov_reg_T0[ot][d]();
}

static void gen_shift(DisasContext *s1, int op, int ot, int d, int s)
{
    if (d != OR_TMP0)
        gen_op_mov_TN_reg[ot][0][d]();
    if (s != OR_TMP1)
        gen_op_mov_TN_reg[ot][1][s]();
    switch(op) {
    case OP_ROL:
    case OP_ROR:
    case OP_RCL:
    case OP_RCR:
        /* only C and O are modified, so we must update flags dynamically */
        if (s1->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s1->cc_op);
        gen_op_shift_T0_T1_cc[ot][op]();
        break;
    default:
        gen_op_shift_T0_T1_cc[ot][op]();
        break;
    }
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
    int havebase;
    int base, disp;
    int index = 0;
    int scale = 0;
    int reg1, reg2, opreg;
    int mod, rm, code;

#ifdef DEBUG_DISAS
    fprintf(logfile, "modrm=0x%x\n", modrm);
#endif
    mod = (modrm >> 6) & 3;
    rm = modrm & 7;

    if (s->aflag) {

        havesib = 0;
        havebase = 1;
        base = rm;
        
        if (base == 4) {
            havesib = 1;
            code = ldub(s->pc++);
#ifdef DEBUG_DISAS
            fprintf(logfile, "sib=0x%x\n", code);
#endif
            scale = (code >> 6) & 3;
            index = (code >> 3) & 7;
            base = code & 7;
        }

        switch (mod) {
        case 0:
            if (base == 5) {
                havebase = 0;
                disp = ldl(s->pc);
                s->pc += 4;
            } else {
                disp = 0;
            }
            break;
        case 1:
            disp = (int8_t)ldub(s->pc++);
            break;
        default:
        case 2:
            disp = ldl(s->pc);
            s->pc += 4;
            break;
        }

        reg1 = OR_ZERO;
        reg2 = OR_ZERO;
          
        if (havebase || (havesib && (index != 4 || scale != 0))) {
            if (havebase)
                reg1 = OR_EAX + base;
            if (havesib && index != 4) {
                if (havebase)
                    reg2 = index + OR_EAX;
                else
                    reg1 = index + OR_EAX;
            }
        }
        /* XXX: disp only ? */
        if (reg2 == OR_ZERO) {
            /* op: disp + (reg1 << scale) */
            if (reg1 == OR_ZERO) {
                gen_op_movl_A0_im(disp);
            } else if (scale == 0 && disp == 0) {
                gen_op_movl_A0_reg[reg1]();
            } else {
                gen_op_movl_A0_im(disp);
                gen_op_addl_A0_reg_sN[scale][reg1]();
            }
        } else {
            /* op: disp + reg1 + (reg2 << scale) */
            if (disp != 0) {
                gen_op_movl_A0_im(disp);
                gen_op_addl_A0_reg_sN[0][reg1]();
            } else {
                gen_op_movl_A0_reg[reg1]();
            }
            gen_op_addl_A0_reg_sN[scale][reg2]();
        }
        opreg = OR_A0;
    } else {
        fprintf(stderr, "16 bit addressing not supported\n");
        disp = 0;
        opreg = 0;
    }
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
            gen_op_st_T0_A0[ot]();
        } else {
            gen_op_ld_T0_A0[ot]();
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
        ret = ldub(s->pc);
        s->pc++;
        break;
    case OT_WORD:
        ret = lduw(s->pc);
        s->pc += 2;
        break;
    default:
    case OT_LONG:
        ret = ldl(s->pc);
        s->pc += 4;
        break;
    }
    return ret;
}

static void gen_jcc(DisasContext *s, int b, int val)
{
    int inv, jcc_op;
    GenOpFunc2 *func;

    inv = b & 1;
    jcc_op = (b >> 1) & 7;
    switch(s->cc_op) {
        /* we optimize the cmp/jcc case */
    case CC_OP_SUBB:
    case CC_OP_SUBW:
    case CC_OP_SUBL:
        func = gen_jcc_sub[s->cc_op - CC_OP_SUBB][jcc_op];
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
            func = gen_jcc_sub[(s->cc_op - CC_OP_ADDB) % 3][jcc_op];
            break;
        case JCC_S:
            func = gen_jcc_sub[(s->cc_op - CC_OP_ADDB) % 3][jcc_op];
            break;
        default:
            goto slow_jcc;
        }
        break;
    default:
    slow_jcc:
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
        func = gen_jcc_slow[jcc_op];
        break;
    }
    if (!inv) {
        func(val, (long)s->pc);
    } else {
        func((long)s->pc, val);
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
            func = gen_setcc_sub[s->cc_op - CC_OP_ADDB][jcc_op];
            break;
        case JCC_S:
            func = gen_setcc_sub[s->cc_op - CC_OP_ADDB][jcc_op];
            break;
        default:
            goto slow_jcc;
        }
        break;
    default:
    slow_jcc:
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
        func = gen_setcc_slow[jcc_op];
        break;
    }
    func();
    if (inv) {
        gen_op_xor_T0_1();
    }
}

/* return the next pc address. Return -1 if no insn found. *is_jmp_ptr
   is set to true if the instruction sets the PC (last instruction of
   a basic block) */
long disas_insn(DisasContext *s, uint8_t *pc_start, int *is_jmp_ptr)
{
    int b, prefixes, aflag, dflag;
    int shift, ot;
    int modrm, reg, rm, mod, reg_addr, op, opreg, offset_addr, val;

    s->pc = pc_start;
    prefixes = 0;
    aflag = 1;
    dflag = 1;
    //    cur_pc = s->pc; /* for insn generation */
 next_byte:
    b = ldub(s->pc);
#ifdef DEBUG_DISAS
    fprintf(logfile, "ib=0x%02x\n", b);
#endif
    if (b < 0)
        return -1;
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
        prefixes |= PREFIX_CS;
        goto next_byte;
    case 0x36:
        prefixes |= PREFIX_SS;
        goto next_byte;
    case 0x3e:
        prefixes |= PREFIX_DS;
        goto next_byte;
    case 0x26:
        prefixes |= PREFIX_ES;
        goto next_byte;
    case 0x64:
        prefixes |= PREFIX_FS;
        goto next_byte;
    case 0x65:
        prefixes |= PREFIX_GS;
        goto next_byte;
    case 0x66:
        prefixes |= PREFIX_DATA;
        goto next_byte;
    case 0x67:
        prefixes |= PREFIX_ADR;
        goto next_byte;
    case 0x9b:
        prefixes |= PREFIX_FWAIT;
        goto next_byte;
    }

    if (prefixes & PREFIX_DATA)
        dflag ^= 1;
    if (prefixes & PREFIX_ADR)
        aflag ^= 1;

    s->prefix = prefixes;
    s->aflag = aflag;
    s->dflag = dflag;

    /* now check op code */
 reswitch:
    switch(b) {
    case 0x0f:
        /**************************/
        /* extended op code */
        b = ldub(s->pc++) | 0x100;
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
                modrm = ldub(s->pc++);
                reg = ((modrm >> 3) & 7) + OR_EAX;
                mod = (modrm >> 6) & 3;
                rm = modrm & 7;
                if (mod != 3) {
                    gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                    gen_op_ld_T0_A0[ot]();
                    opreg = OR_TMP0;
                } else {
                    opreg = OR_EAX + rm;
                }
                gen_op(s, op, ot, opreg, reg);
                if (mod != 3 && op != 7) {
                    gen_op_st_T0_A0[ot]();
                }
                break;
            case 1: /* OP Gv, Ev */
                modrm = ldub(s->pc++);
                mod = (modrm >> 6) & 3;
                reg = ((modrm >> 3) & 7) + OR_EAX;
                rm = modrm & 7;
                if (mod != 3) {
                    gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                    gen_op_ld_T1_A0[ot]();
                    opreg = OR_TMP1;
                } else {
                    opreg = OR_EAX + rm;
                }
                gen_op(s, op, ot, reg, opreg);
                break;
            case 2: /* OP A, Iv */
                val = insn_get(s, ot);
                gen_opi(s, op, ot, OR_EAX, val);
                break;
            }
        }
        break;

    case 0x80: /* GRP1 */
    case 0x81:
    case 0x83:
        {
            int val;

            if ((b & 1) == 0)
                ot = OT_BYTE;
            else
                ot = dflag ? OT_LONG : OT_WORD;
            
            modrm = ldub(s->pc++);
            mod = (modrm >> 6) & 3;
            rm = modrm & 7;
            op = (modrm >> 3) & 7;
            
            if (mod != 3) {
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                gen_op_ld_T0_A0[ot]();
                opreg = OR_TMP0;
            } else {
                opreg = rm + OR_EAX;
            }

            switch(b) {
            default:
            case 0x80:
            case 0x81:
                val = insn_get(s, ot);
                break;
            case 0x83:
                val = (int8_t)insn_get(s, OT_BYTE);
                break;
            }

            gen_opi(s, op, ot, opreg, val);
            if (op != 7 && mod != 3) {
                gen_op_st_T0_A0[ot]();
            }
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

        modrm = ldub(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        op = (modrm >> 3) & 7;
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot]();
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
                gen_op_st_T0_A0[ot]();
            } else {
                gen_op_mov_reg_T0[ot][rm]();
            }
            break;
        case 3: /* neg */
            gen_op_negl_T0_cc();
            if (mod != 3) {
                gen_op_st_T0_A0[ot]();
            } else {
                gen_op_mov_reg_T0[ot][rm]();
            }
            s->cc_op = CC_OP_SUBB + ot;
            break;
        case 4: /* mul */
            switch(ot) {
            case OT_BYTE:
                gen_op_mulb_AL_T0();
                break;
            case OT_WORD:
                gen_op_mulw_AX_T0();
                break;
            default:
            case OT_LONG:
                gen_op_mull_EAX_T0();
                break;
            }
            s->cc_op = CC_OP_MUL;
            break;
        case 5: /* imul */
            switch(ot) {
            case OT_BYTE:
                gen_op_imulb_AL_T0();
                break;
            case OT_WORD:
                gen_op_imulw_AX_T0();
                break;
            default:
            case OT_LONG:
                gen_op_imull_EAX_T0();
                break;
            }
            s->cc_op = CC_OP_MUL;
            break;
        case 6: /* div */
            switch(ot) {
            case OT_BYTE:
                gen_op_divb_AL_T0();
                break;
            case OT_WORD:
                gen_op_divw_AX_T0();
                break;
            default:
            case OT_LONG:
                gen_op_divl_EAX_T0();
                break;
            }
            break;
        case 7: /* idiv */
            switch(ot) {
            case OT_BYTE:
                gen_op_idivb_AL_T0();
                break;
            case OT_WORD:
                gen_op_idivw_AX_T0();
                break;
            default:
            case OT_LONG:
                gen_op_idivl_EAX_T0();
                break;
            }
            break;
        default:
            error("GRP3: bad instruction");
            return -1;
        }
        break;

    case 0xfe: /* GRP4 */
    case 0xff: /* GRP5 */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub(s->pc++);
        mod = (modrm >> 6) & 3;
        rm = modrm & 7;
        op = (modrm >> 3) & 7;
        if (op >= 2 && b == 0xfe) {
            error("GRP4: bad instruction");
            return -1;
        }
        if (mod != 3) {
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
            gen_op_ld_T0_A0[ot]();
        } else {
            gen_op_mov_TN_reg[ot][0][rm]();
        }

        switch(op) {
        case 0: /* inc Ev */
            gen_inc(s, ot, OR_TMP0, 1);
            if (mod != 3)
                gen_op_st_T0_A0[ot]();
            break;
        case 1: /* dec Ev */
            gen_inc(s, ot, OR_TMP0, -1);
            if (mod != 3)
                gen_op_st_T0_A0[ot]();
            break;
        case 2: /* call Ev */
            gen_op_movl_T1_im((long)s->pc);
            gen_op_pushl_T1();
            gen_op_jmp_T0();
            *is_jmp_ptr = 1;
            break;
        case 4: /* jmp Ev */
            gen_op_jmp_T0();
            *is_jmp_ptr = 1;
            break;
        case 6: /* push Ev */
            gen_op_pushl_T0();
            break;
        default:
            error("GRP5: bad instruction");
            return -1;
        }
        break;

    case 0x84: /* test Ev, Gv */
    case 0x85: 
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;

        modrm = ldub(s->pc++);
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
        modrm = ldub(s->pc++);
        reg = ((modrm >> 3) & 7) + OR_EAX;
        
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        if (b == 0x69) {
            val = insn_get(s, ot);
            gen_op_movl_T1_im(val);
        } else if (b == 0x6b) {
            val = insn_get(s, OT_BYTE);
            gen_op_movl_T1_im(val);
        } else {
            gen_op_mov_TN_reg[ot][1][reg]();
        }

        if (ot == OT_LONG) {
            op_imull_T0_T1();
        } else {
            op_imulw_T0_T1();
        }
        gen_op_mov_reg_T0[ot][reg]();
        s->cc_op = CC_OP_MUL;
        break;
        
        /**************************/
        /* push/pop */
    case 0x50 ... 0x57: /* push */
        gen_op_mov_TN_reg[OT_LONG][0][b & 7]();
        gen_op_pushl_T0();
        break;
    case 0x58 ... 0x5f: /* pop */
        gen_op_popl_T0();
        gen_op_mov_reg_T0[OT_LONG][b & 7]();
        break;
    case 0x68: /* push Iv */
    case 0x6a:
        ot = dflag ? OT_LONG : OT_WORD;
        if (b == 0x68)
            val = insn_get(s, ot);
        else
            val = (int8_t)insn_get(s, OT_BYTE);
        gen_op_movl_T0_im(val);
        gen_op_pushl_T0();
        break;
    case 0x8f: /* pop Ev */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub(s->pc++);
        gen_op_popl_T0();
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 1);
        break;
    case 0xc9: /* leave */
        gen_op_mov_TN_reg[OT_LONG][0][R_EBP]();
        gen_op_mov_reg_T0[OT_LONG][R_ESP]();
        gen_op_popl_T0();
        gen_op_mov_reg_T0[OT_LONG][R_EBP]();
        break;
        /**************************/
        /* mov */
    case 0x88:
    case 0x89: /* mov Gv, Ev */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub(s->pc++);
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
        modrm = ldub(s->pc++);
        mod = (modrm >> 6) & 3;
        if (mod != 3)
            gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        val = insn_get(s, ot);
        gen_op_movl_T0_im(val);
        if (mod != 3)
            gen_op_st_T0_A0[ot]();
        else
            gen_op_mov_reg_T0[ot][modrm & 7]();
        break;
    case 0x8a:
    case 0x8b: /* mov Ev, Gv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub(s->pc++);
        reg = (modrm >> 3) & 7;
        
        gen_ldst_modrm(s, modrm, ot, OR_TMP0, 0);
        gen_op_mov_reg_T0[ot][reg]();
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
            modrm = ldub(s->pc++);
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
                    gen_op_lds_T0_A0[ot]();
                } else {
                    gen_op_ldu_T0_A0[ot]();
                }
                gen_op_mov_reg_T0[d_ot][reg]();
            }
        }
        break;

    case 0x8d: /* lea */
        ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub(s->pc++);
        reg = (modrm >> 3) & 7;

        gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
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
            
        if ((b & 2) == 0) {
            gen_op_ld_T0_A0[ot]();
            gen_op_mov_reg_T0[ot][R_EAX]();
        } else {
            gen_op_mov_TN_reg[ot][0][R_EAX]();
            gen_op_st_T0_A0[ot]();
        }
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
        gen_op_mov_TN_reg[ot][0][reg]();
        gen_op_mov_TN_reg[ot][1][R_EAX]();
        gen_op_mov_reg_T0[ot][R_EAX]();
        gen_op_mov_reg_T1[ot][reg]();
        break;
    case 0x86:
    case 0x87: /* xchg Ev, Gv */
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        modrm = ldub(s->pc++);
        reg = (modrm >> 3) & 7;

        gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
        gen_op_mov_TN_reg[ot][0][reg]();
        gen_op_ld_T1_A0[ot]();
        gen_op_st_T0_A0[ot]();
        gen_op_mov_reg_T1[ot][reg]();
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
            
            modrm = ldub(s->pc++);
            mod = (modrm >> 6) & 3;
            rm = modrm & 7;
            op = (modrm >> 3) & 7;
            
            if (mod != 3) {
                gen_lea_modrm(s, modrm, &reg_addr, &offset_addr);
                gen_op_ld_T0_A0[ot]();
                opreg = OR_TMP0;
            } else {
                opreg = rm + OR_EAX;
            }

            /* simpler op */
            if (shift == 0) {
                gen_shift(s, op, ot, opreg, OR_ECX);
            } else {
                if (shift == 2) {
                    shift = ldub(s->pc++);
                }
                gen_shifti(s, op, ot, opreg, shift);
            }

            if (mod != 3) {
                gen_op_st_T0_A0[ot]();
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

        /************************/
        /* floats */
    case 0xd8 ... 0xdf: 
        modrm = ldub(s->pc++);
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
                    gen_op_fpush();
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
#if 0
            case 0x2f: /* fnstsw mem */
                gen_insn3(OP_FNSTS, OR_TMP0, OR_ZERO, OR_ZERO);
                gen_st(OP_STW, OR_TMP0, reg_addr, offset_addr);
                break;

            case 0x3c: /* fbld */
            case 0x3e: /* fbstp */
                error("float BCD not hanlded");
                return -1;
#endif
            case 0x3d: /* fildll */
                gen_op_fpush();
                gen_op_fildll_ST0_A0();
                break;
            case 0x3f: /* fistpll */
                gen_op_fistll_ST0_A0();
                gen_op_fpop();
                break;
            default:
                error("unhandled memory FP\n");
                return -1;
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
                gen_op_fxchg_ST0_STN((opreg + 1) & 7);
                break;
            case 0x0a: /* grp d9/2 */
                switch(rm) {
                case 0: /* fnop */
                    break;
                default:
                    error("unhandled FP GRP d9/2\n");
                    return -1;
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
                    return -1;
                }
                break;
            case 0x0d: /* grp d9/5 */
                {
                    switch(rm) {
                    case 0:
                        gen_op_fld1_ST0();
                        break;
                    case 1:
                        gen_op_fld2t_ST0();
                        break;
                    case 2:
                        gen_op_fld2e_ST0();
                        break;
                    case 3:
                        gen_op_fldpi_ST0();
                        break;
                    case 4:
                        gen_op_fldlg2_ST0();
                        break;
                    case 5:
                        gen_op_fldln2_ST0();
                        break;
                    case 6:
                        gen_op_fldz_ST0();
                        break;
                    default:
                        return -1;
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
                    } else {
                        gen_op_fmov_FT0_STN(opreg);
                        gen_op_fp_arith_ST0_FT0[op1]();
                    }
                    if (op >= 0x30)
                        gen_op_fpop();
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
                    gen_op_fcom_ST0_FT0();
                    gen_op_fpop();
                    gen_op_fpop();
                    break;
                default:
                    return -1;
                }
                break;
            case 0x2a: /* fst sti */
                gen_op_fmov_STN_ST0(opreg);
                break;
            case 0x2b: /* fstp sti */
                gen_op_fmov_STN_ST0(opreg);
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
                    return -1;
                }
                break;
            case 0x3c: /* df/4 */
                switch(rm) {
#if 0
                case 0:
                    gen_insn3(OP_FNSTS, OR_EAX, OR_ZERO, OR_ZERO);
                    break;
#endif
                default:
                    return -1;
                }
                break;
            default:
                error("unhandled FP\n");
                return -1;
            }
        }
        break;
        /************************/
        /* string ops */
    case 0xa4: /* movsS */
    case 0xa5:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPZ) {
            gen_op_movs[3 + ot]();
        } else {
            gen_op_movs[ot]();
        }
        break;
        
    case 0xaa: /* stosS */
    case 0xab:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPZ) {
            gen_op_stos[3 + ot]();
        } else {
            gen_op_stos[ot]();
        }
        break;
    case 0xac: /* lodsS */
    case 0xad:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPZ) {
            gen_op_lods[3 + ot]();
        } else {
            gen_op_lods[ot]();
        }
        break;
    case 0xae: /* scasS */
    case 0xaf:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPNZ) {
            gen_op_scas[6 + ot]();
        } else if (prefixes & PREFIX_REPZ) {
            gen_op_scas[3 + ot]();
        } else {
            gen_op_scas[ot]();
        }
        break;

    case 0xa6: /* cmpsS */
    case 0xa7:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPNZ) {
            gen_op_cmps[6 + ot]();
        } else if (prefixes & PREFIX_REPZ) {
            gen_op_cmps[3 + ot]();
        } else {
            gen_op_cmps[ot]();
        }
        break;
        
        /************************/
        /* port I/O */
    case 0x6c: /* insS */
    case 0x6d:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPZ) {
            gen_op_ins[3 + ot]();
        } else {
            gen_op_ins[ot]();
        }
        break;
    case 0x6e: /* outsS */
    case 0x6f:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        if (prefixes & PREFIX_REPZ) {
            gen_op_outs[3 + ot]();
        } else {
            gen_op_outs[ot]();
        }
        break;
    case 0xe4:
    case 0xe5:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        val = ldub(s->pc++);
        gen_op_movl_T0_im(val);
        gen_op_in[ot]();
        gen_op_mov_reg_T1[ot][R_EAX]();
        break;
    case 0xe6:
    case 0xe7:
        if ((b & 1) == 0)
            ot = OT_BYTE;
        else
            ot = dflag ? OT_LONG : OT_WORD;
        val = ldub(s->pc++);
        gen_op_movl_T0_im(val);
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
        gen_op_mov_TN_reg[ot][1][R_EAX]();
        gen_op_out[ot]();
        break;

        /************************/
        /* control */
    case 0xc2: /* ret im */
        /* XXX: handle stack pop ? */
        val = ldsw(s->pc);
        s->pc += 2;
        gen_op_popl_T0();
        gen_op_addl_ESP_im(val);
        gen_op_jmp_T0();
        *is_jmp_ptr = 1;
        break;
    case 0xc3: /* ret */
        gen_op_popl_T0();
        gen_op_jmp_T0();
        *is_jmp_ptr = 1;
        break;
    case 0xe8: /* call */
        val = insn_get(s, OT_LONG);
        val += (long)s->pc;
        gen_op_movl_T1_im((long)s->pc);
        gen_op_pushl_T1();
        gen_op_jmp_im(val);
        *is_jmp_ptr = 1;
        break;
    case 0xe9: /* jmp */
        val = insn_get(s, OT_LONG);
        val += (long)s->pc;
        gen_op_jmp_im(val);
        *is_jmp_ptr = 1;
        break;
    case 0xeb: /* jmp Jb */
        val = (int8_t)insn_get(s, OT_BYTE);
        val += (long)s->pc;
        gen_op_jmp_im(val);
        *is_jmp_ptr = 1;
        break;
    case 0x70 ... 0x7f: /* jcc Jb */
        val = (int8_t)insn_get(s, OT_BYTE);
        val += (long)s->pc;
        goto do_jcc;
    case 0x180 ... 0x18f: /* jcc Jv */
        if (dflag) {
            val = insn_get(s, OT_LONG);
        } else {
            val = (int16_t)insn_get(s, OT_WORD); 
        }
        val += (long)s->pc; /* XXX: fix 16 bit wrap */
    do_jcc:
        gen_jcc(s, b, val);
        *is_jmp_ptr = 1;
        break;

    case 0x190 ... 0x19f:
        modrm = ldub(s->pc++);
        gen_setcc(s, b);
        gen_ldst_modrm(s, modrm, OT_BYTE, OR_TMP0, 1);
        break;

        /************************/
        /* flags */
    case 0x9c: /* pushf */
        gen_op_movl_T0_eflags();
        gen_op_pushl_T0();
        break;
    case 0x9d: /* popf */
        gen_op_popl_T0();
        gen_op_movl_eflags_T0();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0x9e: /* sahf */
        gen_op_mov_TN_reg[OT_BYTE][0][R_AH]();
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
        gen_op_movb_eflags_T0();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0x9f: /* lahf */
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
        gen_op_movl_T0_eflags();
        gen_op_mov_reg_T0[OT_BYTE][R_AH]();
        break;
    case 0xf5: /* cmc */
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
        gen_op_cmc();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0xf8: /* clc */
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
        gen_op_clc();
        s->cc_op = CC_OP_EFLAGS;
        break;
    case 0xf9: /* stc */
        if (s->cc_op != CC_OP_DYNAMIC)
            op_set_cc_op(s->cc_op);
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
        /* misc */
    case 0x90: /* nop */
        break;
    case 0xcc: /* int3 */
        gen_op_int3((long)pc_start);
        *is_jmp_ptr = 1;
        break;
    case 0xcd: /* int N */
        val = ldub(s->pc++);
        /* XXX: currently we ignore the interrupt number */
        gen_op_int_im((long)pc_start);
        *is_jmp_ptr = 1;
        break;
    case 0xce: /* into */
        if (s->cc_op != CC_OP_DYNAMIC)
            gen_op_set_cc_op(s->cc_op);
        gen_op_into((long)pc_start, (long)s->pc);
        *is_jmp_ptr = 1;
        break;
#if 0
    case 0x1a2: /* cpuid */
        gen_insn0(OP_ASM);
        break;
#endif
    default:
        error("unknown opcode %x", b);
        return -1;
    }
    return (long)s->pc;
}

/* return the next pc */
int cpu_x86_gen_code(uint8_t *gen_code_buf, int *gen_code_size_ptr,
                     uint8_t *pc_start)
{
    DisasContext dc1, *dc = &dc1;
    int is_jmp;
    long ret;
#ifdef DEBUG_DISAS
    struct disassemble_info disasm_info;
#endif

    dc->cc_op = CC_OP_DYNAMIC;
    gen_code_ptr = gen_code_buf;
    gen_start();

#ifdef DEBUG_DISAS
    if (!logfile) {
        logfile = fopen(DEBUG_LOGFILE, "w");
        if (!logfile) {
            perror(DEBUG_LOGFILE);
            exit(1);
        }
        setvbuf(logfile, NULL, _IOLBF, 0);
    }

    INIT_DISASSEMBLE_INFO(disasm_info, logfile, fprintf);
    disasm_info.buffer = pc_start;
    disasm_info.buffer_vma = (unsigned long)pc_start;
    disasm_info.buffer_length = 15;
#if 0        
    disasm_info.flavour = bfd_get_flavour (abfd);
    disasm_info.arch = bfd_get_arch (abfd);
    disasm_info.mach = bfd_get_mach (abfd);
#endif
#ifdef WORDS_BIGENDIAN
    disasm_info.endian = BFD_ENDIAN_BIG;
#else
    disasm_info.endian = BFD_ENDIAN_LITTLE;
#endif        
    fprintf(logfile, "IN:\n");
    fprintf(logfile, "0x%08lx:  ", (long)pc_start);
    print_insn_i386((unsigned long)pc_start, &disasm_info);
    fprintf(logfile, "\n\n");
#endif
    is_jmp = 0;
    ret = disas_insn(dc, pc_start, &is_jmp);
    if (ret == -1) 
        error("unknown instruction at PC=0x%x", pc_start);
    /* we must store the eflags state if it is not already done */
    if (dc->cc_op != CC_OP_DYNAMIC)
        gen_op_set_cc_op(dc->cc_op);
    if (!is_jmp) {
        /* we add an additionnal jmp to update the simulated PC */
        gen_op_jmp_im(ret);
    }
    gen_end();
    *gen_code_size_ptr = gen_code_ptr - gen_code_buf;

#ifdef DEBUG_DISAS
    {
        uint8_t *pc;
        int count;

        pc = gen_code_buf;
        disasm_info.buffer = pc;
        disasm_info.buffer_vma = (unsigned long)pc;
        disasm_info.buffer_length = *gen_code_size_ptr;
        fprintf(logfile, "OUT: [size=%d]\n", *gen_code_size_ptr);
        while (pc < gen_code_ptr) {
            fprintf(logfile, "0x%08lx:  ", (long)pc);
            count = print_insn_i386((unsigned long)pc, &disasm_info);
            fprintf(logfile, "\n");
            pc += count;
        }
        fprintf(logfile, "\n");
    }
#endif
    return 0;
}

CPUX86State *cpu_x86_init(void)
{
    CPUX86State *env;
    int i;

    env = malloc(sizeof(CPUX86State));
    if (!env)
        return NULL;
    memset(env, 0, sizeof(CPUX86State));
    /* basic FPU init */
    for(i = 0;i < 8; i++)
        env->fptags[i] = 1;
    env->fpuc = 0x37f;
    /* flags setup */
    env->cc_op = CC_OP_EFLAGS;
    env->df = 1;
    return env;
}

void cpu_x86_close(CPUX86State *env)
{
    free(env);
}
