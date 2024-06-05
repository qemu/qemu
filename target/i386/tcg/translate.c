/*
 *  i386 translation
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
#include "qemu/osdep.h"

#include "qemu/host-utils.h"
#include "cpu.h"
#include "exec/exec-all.h"
#include "tcg/tcg-op.h"
#include "tcg/tcg-op-gvec.h"
#include "exec/translator.h"
#include "fpu/softfloat.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "helper-tcg.h"

#include "exec/log.h"

#define HELPER_H "helper.h"
#include "exec/helper-info.c.inc"
#undef  HELPER_H

/* Fixes for Windows namespace pollution.  */
#undef IN
#undef OUT

#define PREFIX_REPZ   0x01
#define PREFIX_REPNZ  0x02
#define PREFIX_LOCK   0x04
#define PREFIX_DATA   0x08
#define PREFIX_ADR    0x10
#define PREFIX_VEX    0x20
#define PREFIX_REX    0x40

#ifdef TARGET_X86_64
# define ctztl  ctz64
# define clztl  clz64
#else
# define ctztl  ctz32
# define clztl  clz32
#endif

/* For a switch indexed by MODRM, match all memory operands for a given OP.  */
#define CASE_MODRM_MEM_OP(OP) \
    case (0 << 6) | (OP << 3) | 0 ... (0 << 6) | (OP << 3) | 7: \
    case (1 << 6) | (OP << 3) | 0 ... (1 << 6) | (OP << 3) | 7: \
    case (2 << 6) | (OP << 3) | 0 ... (2 << 6) | (OP << 3) | 7

#define CASE_MODRM_OP(OP) \
    case (0 << 6) | (OP << 3) | 0 ... (0 << 6) | (OP << 3) | 7: \
    case (1 << 6) | (OP << 3) | 0 ... (1 << 6) | (OP << 3) | 7: \
    case (2 << 6) | (OP << 3) | 0 ... (2 << 6) | (OP << 3) | 7: \
    case (3 << 6) | (OP << 3) | 0 ... (3 << 6) | (OP << 3) | 7

//#define MACRO_TEST   1

/* global register indexes */
static TCGv cpu_cc_dst, cpu_cc_src, cpu_cc_src2;
static TCGv cpu_eip;
static TCGv_i32 cpu_cc_op;
static TCGv cpu_regs[CPU_NB_REGS];
static TCGv cpu_seg_base[6];
static TCGv_i64 cpu_bndl[4];
static TCGv_i64 cpu_bndu[4];

typedef struct DisasContext {
    DisasContextBase base;

    target_ulong pc;       /* pc = eip + cs_base */
    target_ulong cs_base;  /* base of CS segment */
    target_ulong pc_save;

    MemOp aflag;
    MemOp dflag;

    int8_t override; /* -1 if no override, else R_CS, R_DS, etc */
    uint8_t prefix;

    bool has_modrm;
    uint8_t modrm;

#ifndef CONFIG_USER_ONLY
    uint8_t cpl;   /* code priv level */
    uint8_t iopl;  /* i/o priv level */
#endif
    uint8_t vex_l;  /* vex vector length */
    uint8_t vex_v;  /* vex vvvv register, without 1's complement.  */
    uint8_t popl_esp_hack; /* for correct popl with esp base handling */
    uint8_t rip_offset; /* only used in x86_64, but left for simplicity */

#ifdef TARGET_X86_64
    uint8_t rex_r;
    uint8_t rex_x;
    uint8_t rex_b;
#endif
    bool vex_w; /* used by AVX even on 32-bit processors */
    bool jmp_opt; /* use direct block chaining for direct jumps */
    bool repz_opt; /* optimize jumps within repz instructions */
    bool cc_op_dirty;

    CCOp cc_op;  /* current CC operation */
    int mem_index; /* select memory access functions */
    uint32_t flags; /* all execution flags */
    int cpuid_features;
    int cpuid_ext_features;
    int cpuid_ext2_features;
    int cpuid_ext3_features;
    int cpuid_7_0_ebx_features;
    int cpuid_7_0_ecx_features;
    int cpuid_7_1_eax_features;
    int cpuid_xsave_features;

    /* TCG local temps */
    TCGv cc_srcT;
    TCGv A0;
    TCGv T0;
    TCGv T1;

    /* TCG local register indexes (only used inside old micro ops) */
    TCGv tmp0;
    TCGv tmp4;
    TCGv_i32 tmp2_i32;
    TCGv_i32 tmp3_i32;
    TCGv_i64 tmp1_i64;

    sigjmp_buf jmpbuf;
    TCGOp *prev_insn_start;
    TCGOp *prev_insn_end;
} DisasContext;

/*
 * Point EIP to next instruction before ending translation.
 * For instructions that can change hflags.
 */
#define DISAS_EOB_NEXT         DISAS_TARGET_0

/*
 * Point EIP to next instruction and set HF_INHIBIT_IRQ if not
 * already set.  For instructions that activate interrupt shadow.
 */
#define DISAS_EOB_INHIBIT_IRQ  DISAS_TARGET_1

/*
 * Return to the main loop; EIP might have already been updated
 * but even in that case do not use lookup_and_goto_ptr().
 */
#define DISAS_EOB_ONLY         DISAS_TARGET_2

/*
 * EIP has already been updated.  For jumps that wish to use
 * lookup_and_goto_ptr()
 */
#define DISAS_JUMP             DISAS_TARGET_3

/*
 * EIP has already been updated.  Use updated value of
 * EFLAGS.TF to determine singlestep trap (SYSCALL/SYSRET).
 */
#define DISAS_EOB_RECHECK_TF   DISAS_TARGET_4

/* The environment in which user-only runs is constrained. */
#ifdef CONFIG_USER_ONLY
#define PE(S)     true
#define CPL(S)    3
#define IOPL(S)   0
#define SVME(S)   false
#define GUEST(S)  false
#else
#define PE(S)     (((S)->flags & HF_PE_MASK) != 0)
#define CPL(S)    ((S)->cpl)
#define IOPL(S)   ((S)->iopl)
#define SVME(S)   (((S)->flags & HF_SVME_MASK) != 0)
#define GUEST(S)  (((S)->flags & HF_GUEST_MASK) != 0)
#endif
#if defined(CONFIG_USER_ONLY) && defined(TARGET_X86_64)
#define VM86(S)   false
#define CODE32(S) true
#define SS32(S)   true
#define ADDSEG(S) false
#else
#define VM86(S)   (((S)->flags & HF_VM_MASK) != 0)
#define CODE32(S) (((S)->flags & HF_CS32_MASK) != 0)
#define SS32(S)   (((S)->flags & HF_SS32_MASK) != 0)
#define ADDSEG(S) (((S)->flags & HF_ADDSEG_MASK) != 0)
#endif
#if !defined(TARGET_X86_64)
#define CODE64(S) false
#elif defined(CONFIG_USER_ONLY)
#define CODE64(S) true
#else
#define CODE64(S) (((S)->flags & HF_CS64_MASK) != 0)
#endif
#if defined(CONFIG_USER_ONLY) || defined(TARGET_X86_64)
#define LMA(S)    (((S)->flags & HF_LMA_MASK) != 0)
#else
#define LMA(S)    false
#endif

#ifdef TARGET_X86_64
#define REX_PREFIX(S)  (((S)->prefix & PREFIX_REX) != 0)
#define REX_W(S)       ((S)->vex_w)
#define REX_R(S)       ((S)->rex_r + 0)
#define REX_X(S)       ((S)->rex_x + 0)
#define REX_B(S)       ((S)->rex_b + 0)
#else
#define REX_PREFIX(S)  false
#define REX_W(S)       false
#define REX_R(S)       0
#define REX_X(S)       0
#define REX_B(S)       0
#endif

/*
 * Many sysemu-only helpers are not reachable for user-only.
 * Define stub generators here, so that we need not either sprinkle
 * ifdefs through the translator, nor provide the helper function.
 */
#define STUB_HELPER(NAME, ...) \
    static inline void gen_helper_##NAME(__VA_ARGS__) \
    { qemu_build_not_reached(); }

#ifdef CONFIG_USER_ONLY
STUB_HELPER(clgi, TCGv_env env)
STUB_HELPER(flush_page, TCGv_env env, TCGv addr)
STUB_HELPER(inb, TCGv ret, TCGv_env env, TCGv_i32 port)
STUB_HELPER(inw, TCGv ret, TCGv_env env, TCGv_i32 port)
STUB_HELPER(inl, TCGv ret, TCGv_env env, TCGv_i32 port)
STUB_HELPER(monitor, TCGv_env env, TCGv addr)
STUB_HELPER(mwait, TCGv_env env, TCGv_i32 pc_ofs)
STUB_HELPER(outb, TCGv_env env, TCGv_i32 port, TCGv_i32 val)
STUB_HELPER(outw, TCGv_env env, TCGv_i32 port, TCGv_i32 val)
STUB_HELPER(outl, TCGv_env env, TCGv_i32 port, TCGv_i32 val)
STUB_HELPER(rdmsr, TCGv_env env)
STUB_HELPER(read_crN, TCGv ret, TCGv_env env, TCGv_i32 reg)
STUB_HELPER(get_dr, TCGv ret, TCGv_env env, TCGv_i32 reg)
STUB_HELPER(set_dr, TCGv_env env, TCGv_i32 reg, TCGv val)
STUB_HELPER(stgi, TCGv_env env)
STUB_HELPER(svm_check_intercept, TCGv_env env, TCGv_i32 type)
STUB_HELPER(vmload, TCGv_env env, TCGv_i32 aflag)
STUB_HELPER(vmmcall, TCGv_env env)
STUB_HELPER(vmrun, TCGv_env env, TCGv_i32 aflag, TCGv_i32 pc_ofs)
STUB_HELPER(vmsave, TCGv_env env, TCGv_i32 aflag)
STUB_HELPER(write_crN, TCGv_env env, TCGv_i32 reg, TCGv val)
STUB_HELPER(wrmsr, TCGv_env env)
#endif

static void gen_jmp_rel(DisasContext *s, MemOp ot, int diff, int tb_num);
static void gen_jmp_rel_csize(DisasContext *s, int diff, int tb_num);
static void gen_exception_gpf(DisasContext *s);

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
    JCC_O,
    JCC_B,
    JCC_Z,
    JCC_BE,
    JCC_S,
    JCC_P,
    JCC_L,
    JCC_LE,
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

    OR_TMP0 = 16,    /* temporary operand register */
    OR_TMP1,
    OR_A0, /* temporary register used when doing address evaluation */
};

enum {
    USES_CC_DST  = 1,
    USES_CC_SRC  = 2,
    USES_CC_SRC2 = 4,
    USES_CC_SRCT = 8,
};

/* Bit set if the global variable is live after setting CC_OP to X.  */
static const uint8_t cc_op_live[CC_OP_NB] = {
    [CC_OP_DYNAMIC] = USES_CC_DST | USES_CC_SRC | USES_CC_SRC2,
    [CC_OP_EFLAGS] = USES_CC_SRC,
    [CC_OP_MULB ... CC_OP_MULQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_ADDB ... CC_OP_ADDQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_ADCB ... CC_OP_ADCQ] = USES_CC_DST | USES_CC_SRC | USES_CC_SRC2,
    [CC_OP_SUBB ... CC_OP_SUBQ] = USES_CC_DST | USES_CC_SRC | USES_CC_SRCT,
    [CC_OP_SBBB ... CC_OP_SBBQ] = USES_CC_DST | USES_CC_SRC | USES_CC_SRC2,
    [CC_OP_LOGICB ... CC_OP_LOGICQ] = USES_CC_DST,
    [CC_OP_INCB ... CC_OP_INCQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_DECB ... CC_OP_DECQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_SHLB ... CC_OP_SHLQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_SARB ... CC_OP_SARQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_BMILGB ... CC_OP_BMILGQ] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_ADCX] = USES_CC_DST | USES_CC_SRC,
    [CC_OP_ADOX] = USES_CC_SRC | USES_CC_SRC2,
    [CC_OP_ADCOX] = USES_CC_DST | USES_CC_SRC | USES_CC_SRC2,
    [CC_OP_CLR] = 0,
    [CC_OP_POPCNT] = USES_CC_SRC,
};

static void set_cc_op_1(DisasContext *s, CCOp op, bool dirty)
{
    int dead;

    if (s->cc_op == op) {
        return;
    }

    /* Discard CC computation that will no longer be used.  */
    dead = cc_op_live[s->cc_op] & ~cc_op_live[op];
    if (dead & USES_CC_DST) {
        tcg_gen_discard_tl(cpu_cc_dst);
    }
    if (dead & USES_CC_SRC) {
        tcg_gen_discard_tl(cpu_cc_src);
    }
    if (dead & USES_CC_SRC2) {
        tcg_gen_discard_tl(cpu_cc_src2);
    }
    if (dead & USES_CC_SRCT) {
        tcg_gen_discard_tl(s->cc_srcT);
    }

    if (dirty && s->cc_op == CC_OP_DYNAMIC) {
        tcg_gen_discard_i32(cpu_cc_op);
    }
    s->cc_op_dirty = dirty;
    s->cc_op = op;
}

static void set_cc_op(DisasContext *s, CCOp op)
{
    /*
     * The DYNAMIC setting is translator only, everything else
     * will be spilled later.
     */
    set_cc_op_1(s, op, op != CC_OP_DYNAMIC);
}

static void assume_cc_op(DisasContext *s, CCOp op)
{
    set_cc_op_1(s, op, false);
}

static void gen_update_cc_op(DisasContext *s)
{
    if (s->cc_op_dirty) {
        tcg_gen_movi_i32(cpu_cc_op, s->cc_op);
        s->cc_op_dirty = false;
    }
}

#ifdef TARGET_X86_64

#define NB_OP_SIZES 4

#else /* !TARGET_X86_64 */

#define NB_OP_SIZES 3

#endif /* !TARGET_X86_64 */

#if HOST_BIG_ENDIAN
#define REG_B_OFFSET (sizeof(target_ulong) - 1)
#define REG_H_OFFSET (sizeof(target_ulong) - 2)
#define REG_W_OFFSET (sizeof(target_ulong) - 2)
#define REG_L_OFFSET (sizeof(target_ulong) - 4)
#define REG_LH_OFFSET (sizeof(target_ulong) - 8)
#else
#define REG_B_OFFSET 0
#define REG_H_OFFSET 1
#define REG_W_OFFSET 0
#define REG_L_OFFSET 0
#define REG_LH_OFFSET 4
#endif

/* In instruction encodings for byte register accesses the
 * register number usually indicates "low 8 bits of register N";
 * however there are some special cases where N 4..7 indicates
 * [AH, CH, DH, BH], ie "bits 15..8 of register N-4". Return
 * true for this special case, false otherwise.
 */
static inline bool byte_reg_is_xH(DisasContext *s, int reg)
{
    /* Any time the REX prefix is present, byte registers are uniform */
    if (reg < 4 || REX_PREFIX(s)) {
        return false;
    }
    return true;
}

/* Select the size of a push/pop operation.  */
static inline MemOp mo_pushpop(DisasContext *s, MemOp ot)
{
    if (CODE64(s)) {
        return ot == MO_16 ? MO_16 : MO_64;
    } else {
        return ot;
    }
}

/* Select the size of the stack pointer.  */
static inline MemOp mo_stacksize(DisasContext *s)
{
    return CODE64(s) ? MO_64 : SS32(s) ? MO_32 : MO_16;
}

/* Select size 8 if lsb of B is clear, else OT.  Used for decoding
   byte vs word opcodes.  */
static inline MemOp mo_b_d(int b, MemOp ot)
{
    return b & 1 ? ot : MO_8;
}

/* Compute the result of writing t0 to the OT-sized register REG.
 *
 * If DEST is NULL, store the result into the register and return the
 * register's TCGv.
 *
 * If DEST is not NULL, store the result into DEST and return the
 * register's TCGv.
 */
static TCGv gen_op_deposit_reg_v(DisasContext *s, MemOp ot, int reg, TCGv dest, TCGv t0)
{
    switch(ot) {
    case MO_8:
        if (byte_reg_is_xH(s, reg)) {
            dest = dest ? dest : cpu_regs[reg - 4];
            tcg_gen_deposit_tl(dest, cpu_regs[reg - 4], t0, 8, 8);
            return cpu_regs[reg - 4];
        }
        dest = dest ? dest : cpu_regs[reg];
        tcg_gen_deposit_tl(dest, cpu_regs[reg], t0, 0, 8);
        break;
    case MO_16:
        dest = dest ? dest : cpu_regs[reg];
        tcg_gen_deposit_tl(dest, cpu_regs[reg], t0, 0, 16);
        break;
    case MO_32:
        /* For x86_64, this sets the higher half of register to zero.
           For i386, this is equivalent to a mov. */
        dest = dest ? dest : cpu_regs[reg];
        tcg_gen_ext32u_tl(dest, t0);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        dest = dest ? dest : cpu_regs[reg];
        tcg_gen_mov_tl(dest, t0);
        break;
#endif
    default:
        g_assert_not_reached();
    }
    return cpu_regs[reg];
}

static void gen_op_mov_reg_v(DisasContext *s, MemOp ot, int reg, TCGv t0)
{
    gen_op_deposit_reg_v(s, ot, reg, NULL, t0);
}

static inline
void gen_op_mov_v_reg(DisasContext *s, MemOp ot, TCGv t0, int reg)
{
    if (ot == MO_8 && byte_reg_is_xH(s, reg)) {
        tcg_gen_extract_tl(t0, cpu_regs[reg - 4], 8, 8);
    } else {
        tcg_gen_mov_tl(t0, cpu_regs[reg]);
    }
}

static void gen_add_A0_im(DisasContext *s, int val)
{
    tcg_gen_addi_tl(s->A0, s->A0, val);
    if (!CODE64(s)) {
        tcg_gen_ext32u_tl(s->A0, s->A0);
    }
}

static inline void gen_op_jmp_v(DisasContext *s, TCGv dest)
{
    tcg_gen_mov_tl(cpu_eip, dest);
    s->pc_save = -1;
}

static inline
void gen_op_add_reg_im(DisasContext *s, MemOp size, int reg, int32_t val)
{
    tcg_gen_addi_tl(s->tmp0, cpu_regs[reg], val);
    gen_op_mov_reg_v(s, size, reg, s->tmp0);
}

static inline void gen_op_add_reg(DisasContext *s, MemOp size, int reg, TCGv val)
{
    tcg_gen_add_tl(s->tmp0, cpu_regs[reg], val);
    gen_op_mov_reg_v(s, size, reg, s->tmp0);
}

static inline void gen_op_ld_v(DisasContext *s, int idx, TCGv t0, TCGv a0)
{
    tcg_gen_qemu_ld_tl(t0, a0, s->mem_index, idx | MO_LE);
}

static inline void gen_op_st_v(DisasContext *s, int idx, TCGv t0, TCGv a0)
{
    tcg_gen_qemu_st_tl(t0, a0, s->mem_index, idx | MO_LE);
}

static inline void gen_op_st_rm_T0_A0(DisasContext *s, int idx, int d)
{
    if (d == OR_TMP0) {
        gen_op_st_v(s, idx, s->T0, s->A0);
    } else {
        gen_op_mov_reg_v(s, idx, d, s->T0);
    }
}

static void gen_update_eip_cur(DisasContext *s)
{
    assert(s->pc_save != -1);
    if (tb_cflags(s->base.tb) & CF_PCREL) {
        tcg_gen_addi_tl(cpu_eip, cpu_eip, s->base.pc_next - s->pc_save);
    } else if (CODE64(s)) {
        tcg_gen_movi_tl(cpu_eip, s->base.pc_next);
    } else {
        tcg_gen_movi_tl(cpu_eip, (uint32_t)(s->base.pc_next - s->cs_base));
    }
    s->pc_save = s->base.pc_next;
}

static int cur_insn_len(DisasContext *s)
{
    return s->pc - s->base.pc_next;
}

static TCGv_i32 cur_insn_len_i32(DisasContext *s)
{
    return tcg_constant_i32(cur_insn_len(s));
}

static TCGv_i32 eip_next_i32(DisasContext *s)
{
    assert(s->pc_save != -1);
    /*
     * This function has two users: lcall_real (always 16-bit mode), and
     * iret_protected (16, 32, or 64-bit mode).  IRET only uses the value
     * when EFLAGS.NT is set, which is illegal in 64-bit mode, which is
     * why passing a 32-bit value isn't broken.  To avoid using this where
     * we shouldn't, return -1 in 64-bit mode so that execution goes into
     * the weeds quickly.
     */
    if (CODE64(s)) {
        return tcg_constant_i32(-1);
    }
    if (tb_cflags(s->base.tb) & CF_PCREL) {
        TCGv_i32 ret = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(ret, cpu_eip);
        tcg_gen_addi_i32(ret, ret, s->pc - s->pc_save);
        return ret;
    } else {
        return tcg_constant_i32(s->pc - s->cs_base);
    }
}

static TCGv eip_next_tl(DisasContext *s)
{
    assert(s->pc_save != -1);
    if (tb_cflags(s->base.tb) & CF_PCREL) {
        TCGv ret = tcg_temp_new();
        tcg_gen_addi_tl(ret, cpu_eip, s->pc - s->pc_save);
        return ret;
    } else if (CODE64(s)) {
        return tcg_constant_tl(s->pc);
    } else {
        return tcg_constant_tl((uint32_t)(s->pc - s->cs_base));
    }
}

static TCGv eip_cur_tl(DisasContext *s)
{
    assert(s->pc_save != -1);
    if (tb_cflags(s->base.tb) & CF_PCREL) {
        TCGv ret = tcg_temp_new();
        tcg_gen_addi_tl(ret, cpu_eip, s->base.pc_next - s->pc_save);
        return ret;
    } else if (CODE64(s)) {
        return tcg_constant_tl(s->base.pc_next);
    } else {
        return tcg_constant_tl((uint32_t)(s->base.pc_next - s->cs_base));
    }
}

/* Compute SEG:REG into DEST.  SEG is selected from the override segment
   (OVR_SEG) and the default segment (DEF_SEG).  OVR_SEG may be -1 to
   indicate no override.  */
static void gen_lea_v_seg_dest(DisasContext *s, MemOp aflag, TCGv dest, TCGv a0,
                               int def_seg, int ovr_seg)
{
    switch (aflag) {
#ifdef TARGET_X86_64
    case MO_64:
        if (ovr_seg < 0) {
            tcg_gen_mov_tl(dest, a0);
            return;
        }
        break;
#endif
    case MO_32:
        /* 32 bit address */
        if (ovr_seg < 0 && ADDSEG(s)) {
            ovr_seg = def_seg;
        }
        if (ovr_seg < 0) {
            tcg_gen_ext32u_tl(dest, a0);
            return;
        }
        break;
    case MO_16:
        /* 16 bit address */
        tcg_gen_ext16u_tl(dest, a0);
        a0 = dest;
        if (ovr_seg < 0) {
            if (ADDSEG(s)) {
                ovr_seg = def_seg;
            } else {
                return;
            }
        }
        break;
    default:
        g_assert_not_reached();
    }

    if (ovr_seg >= 0) {
        TCGv seg = cpu_seg_base[ovr_seg];

        if (aflag == MO_64) {
            tcg_gen_add_tl(dest, a0, seg);
        } else if (CODE64(s)) {
            tcg_gen_ext32u_tl(dest, a0);
            tcg_gen_add_tl(dest, dest, seg);
        } else {
            tcg_gen_add_tl(dest, a0, seg);
            tcg_gen_ext32u_tl(dest, dest);
        }
    }
}

static void gen_lea_v_seg(DisasContext *s, TCGv a0,
                          int def_seg, int ovr_seg)
{
    gen_lea_v_seg_dest(s, s->aflag, s->A0, a0, def_seg, ovr_seg);
}

static inline void gen_string_movl_A0_ESI(DisasContext *s)
{
    gen_lea_v_seg(s, cpu_regs[R_ESI], R_DS, s->override);
}

static inline void gen_string_movl_A0_EDI(DisasContext *s)
{
    gen_lea_v_seg(s, cpu_regs[R_EDI], R_ES, -1);
}

static inline TCGv gen_compute_Dshift(DisasContext *s, MemOp ot)
{
    TCGv dshift = tcg_temp_new();
    tcg_gen_ld32s_tl(dshift, tcg_env, offsetof(CPUX86State, df));
    tcg_gen_shli_tl(dshift, dshift, ot);
    return dshift;
};

static TCGv gen_ext_tl(TCGv dst, TCGv src, MemOp size, bool sign)
{
    if (size == MO_TL) {
        return src;
    }
    if (!dst) {
        dst = tcg_temp_new();
    }
    tcg_gen_ext_tl(dst, src, size | (sign ? MO_SIGN : 0));
    return dst;
}

static void gen_extu(MemOp ot, TCGv reg)
{
    gen_ext_tl(reg, reg, ot, false);
}

static void gen_exts(MemOp ot, TCGv reg)
{
    gen_ext_tl(reg, reg, ot, true);
}

static void gen_op_j_ecx(DisasContext *s, TCGCond cond, TCGLabel *label1)
{
    TCGv tmp = gen_ext_tl(NULL, cpu_regs[R_ECX], s->aflag, false);

    tcg_gen_brcondi_tl(cond, tmp, 0, label1);
}

static inline void gen_op_jz_ecx(DisasContext *s, TCGLabel *label1)
{
    gen_op_j_ecx(s, TCG_COND_EQ, label1);
}

static inline void gen_op_jnz_ecx(DisasContext *s, TCGLabel *label1)
{
    gen_op_j_ecx(s, TCG_COND_NE, label1);
}

static void gen_helper_in_func(MemOp ot, TCGv v, TCGv_i32 n)
{
    switch (ot) {
    case MO_8:
        gen_helper_inb(v, tcg_env, n);
        break;
    case MO_16:
        gen_helper_inw(v, tcg_env, n);
        break;
    case MO_32:
        gen_helper_inl(v, tcg_env, n);
        break;
    default:
        g_assert_not_reached();
    }
}

static void gen_helper_out_func(MemOp ot, TCGv_i32 v, TCGv_i32 n)
{
    switch (ot) {
    case MO_8:
        gen_helper_outb(tcg_env, v, n);
        break;
    case MO_16:
        gen_helper_outw(tcg_env, v, n);
        break;
    case MO_32:
        gen_helper_outl(tcg_env, v, n);
        break;
    default:
        g_assert_not_reached();
    }
}

/*
 * Validate that access to [port, port + 1<<ot) is allowed.
 * Raise #GP, or VMM exit if not.
 */
static bool gen_check_io(DisasContext *s, MemOp ot, TCGv_i32 port,
                         uint32_t svm_flags)
{
#ifdef CONFIG_USER_ONLY
    /*
     * We do not implement the ioperm(2) syscall, so the TSS check
     * will always fail.
     */
    gen_exception_gpf(s);
    return false;
#else
    if (PE(s) && (CPL(s) > IOPL(s) || VM86(s))) {
        gen_helper_check_io(tcg_env, port, tcg_constant_i32(1 << ot));
    }
    if (GUEST(s)) {
        gen_update_cc_op(s);
        gen_update_eip_cur(s);
        if (s->prefix & (PREFIX_REPZ | PREFIX_REPNZ)) {
            svm_flags |= SVM_IOIO_REP_MASK;
        }
        svm_flags |= 1 << (SVM_IOIO_SIZE_SHIFT + ot);
        gen_helper_svm_check_io(tcg_env, port,
                                tcg_constant_i32(svm_flags),
                                cur_insn_len_i32(s));
    }
    return true;
#endif
}

static void gen_movs(DisasContext *s, MemOp ot)
{
    TCGv dshift;

    gen_string_movl_A0_ESI(s);
    gen_op_ld_v(s, ot, s->T0, s->A0);
    gen_string_movl_A0_EDI(s);
    gen_op_st_v(s, ot, s->T0, s->A0);

    dshift = gen_compute_Dshift(s, ot);
    gen_op_add_reg(s, s->aflag, R_ESI, dshift);
    gen_op_add_reg(s, s->aflag, R_EDI, dshift);
}

static void gen_op_update1_cc(DisasContext *s)
{
    tcg_gen_mov_tl(cpu_cc_dst, s->T0);
}

static void gen_op_update2_cc(DisasContext *s)
{
    tcg_gen_mov_tl(cpu_cc_src, s->T1);
    tcg_gen_mov_tl(cpu_cc_dst, s->T0);
}

/* compute all eflags to reg */
static void gen_mov_eflags(DisasContext *s, TCGv reg)
{
    TCGv dst, src1, src2;
    TCGv_i32 cc_op;
    int live, dead;

    if (s->cc_op == CC_OP_EFLAGS) {
        tcg_gen_mov_tl(reg, cpu_cc_src);
        return;
    }
    if (s->cc_op == CC_OP_CLR) {
        tcg_gen_movi_tl(reg, CC_Z | CC_P);
        return;
    }

    dst = cpu_cc_dst;
    src1 = cpu_cc_src;
    src2 = cpu_cc_src2;

    /* Take care to not read values that are not live.  */
    live = cc_op_live[s->cc_op] & ~USES_CC_SRCT;
    dead = live ^ (USES_CC_DST | USES_CC_SRC | USES_CC_SRC2);
    if (dead) {
        TCGv zero = tcg_constant_tl(0);
        if (dead & USES_CC_DST) {
            dst = zero;
        }
        if (dead & USES_CC_SRC) {
            src1 = zero;
        }
        if (dead & USES_CC_SRC2) {
            src2 = zero;
        }
    }

    if (s->cc_op != CC_OP_DYNAMIC) {
        cc_op = tcg_constant_i32(s->cc_op);
    } else {
        cc_op = cpu_cc_op;
    }
    gen_helper_cc_compute_all(reg, dst, src1, src2, cc_op);
}

/* compute all eflags to cc_src */
static void gen_compute_eflags(DisasContext *s)
{
    gen_mov_eflags(s, cpu_cc_src);
    set_cc_op(s, CC_OP_EFLAGS);
}

typedef struct CCPrepare {
    TCGCond cond;
    TCGv reg;
    TCGv reg2;
    target_ulong imm;
    bool use_reg2;
    bool no_setcond;
} CCPrepare;

static CCPrepare gen_prepare_sign_nz(TCGv src, MemOp size)
{
    if (size == MO_TL) {
        return (CCPrepare) { .cond = TCG_COND_LT, .reg = src };
    } else {
        return (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = src,
                             .imm = 1ull << ((8 << size) - 1) };
    }
}

/* compute eflags.C, trying to store it in reg if not NULL */
static CCPrepare gen_prepare_eflags_c(DisasContext *s, TCGv reg)
{
    MemOp size;

    switch (s->cc_op) {
    case CC_OP_SUBB ... CC_OP_SUBQ:
        /* (DATA_TYPE)CC_SRCT < (DATA_TYPE)CC_SRC */
        size = s->cc_op - CC_OP_SUBB;
        gen_ext_tl(s->cc_srcT, s->cc_srcT, size, false);
        gen_ext_tl(cpu_cc_src, cpu_cc_src, size, false);
        return (CCPrepare) { .cond = TCG_COND_LTU, .reg = s->cc_srcT,
                             .reg2 = cpu_cc_src, .use_reg2 = true };

    case CC_OP_ADDB ... CC_OP_ADDQ:
        /* (DATA_TYPE)CC_DST < (DATA_TYPE)CC_SRC */
        size = s->cc_op - CC_OP_ADDB;
        gen_ext_tl(cpu_cc_dst, cpu_cc_dst, size, false);
        gen_ext_tl(cpu_cc_src, cpu_cc_src, size, false);
        return (CCPrepare) { .cond = TCG_COND_LTU, .reg = cpu_cc_dst,
                             .reg2 = cpu_cc_src, .use_reg2 = true };

    case CC_OP_LOGICB ... CC_OP_LOGICQ:
    case CC_OP_CLR:
    case CC_OP_POPCNT:
        return (CCPrepare) { .cond = TCG_COND_NEVER };

    case CC_OP_INCB ... CC_OP_INCQ:
    case CC_OP_DECB ... CC_OP_DECQ:
        return (CCPrepare) { .cond = TCG_COND_NE, .reg = cpu_cc_src,
                             .no_setcond = true };

    case CC_OP_SHLB ... CC_OP_SHLQ:
        /* (CC_SRC >> (DATA_BITS - 1)) & 1 */
        size = s->cc_op - CC_OP_SHLB;
        return gen_prepare_sign_nz(cpu_cc_src, size);

    case CC_OP_MULB ... CC_OP_MULQ:
        return (CCPrepare) { .cond = TCG_COND_NE,
                             .reg = cpu_cc_src };

    case CC_OP_BMILGB ... CC_OP_BMILGQ:
        size = s->cc_op - CC_OP_BMILGB;
        gen_ext_tl(cpu_cc_src, cpu_cc_src, size, false);
        return (CCPrepare) { .cond = TCG_COND_EQ, .reg = cpu_cc_src };

    case CC_OP_ADCX:
    case CC_OP_ADCOX:
        return (CCPrepare) { .cond = TCG_COND_NE, .reg = cpu_cc_dst,
                             .no_setcond = true };

    case CC_OP_EFLAGS:
    case CC_OP_SARB ... CC_OP_SARQ:
        /* CC_SRC & 1 */
        return (CCPrepare) { .cond = TCG_COND_TSTNE,
                             .reg = cpu_cc_src, .imm = CC_C };

    default:
       /* The need to compute only C from CC_OP_DYNAMIC is important
          in efficiently implementing e.g. INC at the start of a TB.  */
       gen_update_cc_op(s);
       if (!reg) {
           reg = tcg_temp_new();
       }
       gen_helper_cc_compute_c(reg, cpu_cc_dst, cpu_cc_src,
                               cpu_cc_src2, cpu_cc_op);
       return (CCPrepare) { .cond = TCG_COND_NE, .reg = reg,
                            .no_setcond = true };
    }
}

/* compute eflags.P, trying to store it in reg if not NULL */
static CCPrepare gen_prepare_eflags_p(DisasContext *s, TCGv reg)
{
    gen_compute_eflags(s);
    return (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = cpu_cc_src,
                         .imm = CC_P };
}

/* compute eflags.S, trying to store it in reg if not NULL */
static CCPrepare gen_prepare_eflags_s(DisasContext *s, TCGv reg)
{
    switch (s->cc_op) {
    case CC_OP_DYNAMIC:
        gen_compute_eflags(s);
        /* FALLTHRU */
    case CC_OP_EFLAGS:
    case CC_OP_ADCX:
    case CC_OP_ADOX:
    case CC_OP_ADCOX:
        return (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = cpu_cc_src,
                             .imm = CC_S };
    case CC_OP_CLR:
    case CC_OP_POPCNT:
        return (CCPrepare) { .cond = TCG_COND_NEVER };
    default:
        {
            MemOp size = (s->cc_op - CC_OP_ADDB) & 3;
            return gen_prepare_sign_nz(cpu_cc_dst, size);
        }
    }
}

/* compute eflags.O, trying to store it in reg if not NULL */
static CCPrepare gen_prepare_eflags_o(DisasContext *s, TCGv reg)
{
    switch (s->cc_op) {
    case CC_OP_ADOX:
    case CC_OP_ADCOX:
        return (CCPrepare) { .cond = TCG_COND_NE, .reg = cpu_cc_src2,
                             .no_setcond = true };
    case CC_OP_CLR:
    case CC_OP_POPCNT:
        return (CCPrepare) { .cond = TCG_COND_NEVER };
    case CC_OP_MULB ... CC_OP_MULQ:
        return (CCPrepare) { .cond = TCG_COND_NE, .reg = cpu_cc_src };
    default:
        gen_compute_eflags(s);
        return (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = cpu_cc_src,
                             .imm = CC_O };
    }
}

/* compute eflags.Z, trying to store it in reg if not NULL */
static CCPrepare gen_prepare_eflags_z(DisasContext *s, TCGv reg)
{
    switch (s->cc_op) {
    case CC_OP_DYNAMIC:
        gen_compute_eflags(s);
        /* FALLTHRU */
    case CC_OP_EFLAGS:
    case CC_OP_ADCX:
    case CC_OP_ADOX:
    case CC_OP_ADCOX:
        return (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = cpu_cc_src,
                             .imm = CC_Z };
    case CC_OP_CLR:
        return (CCPrepare) { .cond = TCG_COND_ALWAYS };
    case CC_OP_POPCNT:
        return (CCPrepare) { .cond = TCG_COND_EQ, .reg = cpu_cc_src };
    default:
        {
            MemOp size = (s->cc_op - CC_OP_ADDB) & 3;
            if (size == MO_TL) {
                return (CCPrepare) { .cond = TCG_COND_EQ, .reg = cpu_cc_dst };
            } else {
                return (CCPrepare) { .cond = TCG_COND_TSTEQ, .reg = cpu_cc_dst,
                                     .imm = (1ull << (8 << size)) - 1 };
            }
        }
    }
}

/* return how to compute jump opcode 'b'.  'reg' can be clobbered
 * if needed; it may be used for CCPrepare.reg if that will
 * provide more freedom in the translation of a subsequent setcond. */
static CCPrepare gen_prepare_cc(DisasContext *s, int b, TCGv reg)
{
    int inv, jcc_op, cond;
    MemOp size;
    CCPrepare cc;

    inv = b & 1;
    jcc_op = (b >> 1) & 7;

    switch (s->cc_op) {
    case CC_OP_SUBB ... CC_OP_SUBQ:
        /* We optimize relational operators for the cmp/jcc case.  */
        size = s->cc_op - CC_OP_SUBB;
        switch (jcc_op) {
        case JCC_BE:
            gen_ext_tl(s->cc_srcT, s->cc_srcT, size, false);
            gen_ext_tl(cpu_cc_src, cpu_cc_src, size, false);
            cc = (CCPrepare) { .cond = TCG_COND_LEU, .reg = s->cc_srcT,
                               .reg2 = cpu_cc_src, .use_reg2 = true };
            break;
        case JCC_L:
            cond = TCG_COND_LT;
            goto fast_jcc_l;
        case JCC_LE:
            cond = TCG_COND_LE;
        fast_jcc_l:
            gen_ext_tl(s->cc_srcT, s->cc_srcT, size, true);
            gen_ext_tl(cpu_cc_src, cpu_cc_src, size, true);
            cc = (CCPrepare) { .cond = cond, .reg = s->cc_srcT,
                               .reg2 = cpu_cc_src, .use_reg2 = true };
            break;

        default:
            goto slow_jcc;
        }
        break;

    default:
    slow_jcc:
        /* This actually generates good code for JC, JZ and JS.  */
        switch (jcc_op) {
        case JCC_O:
            cc = gen_prepare_eflags_o(s, reg);
            break;
        case JCC_B:
            cc = gen_prepare_eflags_c(s, reg);
            break;
        case JCC_Z:
            cc = gen_prepare_eflags_z(s, reg);
            break;
        case JCC_BE:
            gen_compute_eflags(s);
            cc = (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = cpu_cc_src,
                               .imm = CC_Z | CC_C };
            break;
        case JCC_S:
            cc = gen_prepare_eflags_s(s, reg);
            break;
        case JCC_P:
            cc = gen_prepare_eflags_p(s, reg);
            break;
        case JCC_L:
            gen_compute_eflags(s);
            if (!reg || reg == cpu_cc_src) {
                reg = tcg_temp_new();
            }
            tcg_gen_addi_tl(reg, cpu_cc_src, CC_O - CC_S);
            cc = (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = reg,
                               .imm = CC_O };
            break;
        default:
        case JCC_LE:
            gen_compute_eflags(s);
            if (!reg || reg == cpu_cc_src) {
                reg = tcg_temp_new();
            }
            tcg_gen_addi_tl(reg, cpu_cc_src, CC_O - CC_S);
            cc = (CCPrepare) { .cond = TCG_COND_TSTNE, .reg = reg,
                               .imm = CC_O | CC_Z };
            break;
        }
        break;
    }

    if (inv) {
        cc.cond = tcg_invert_cond(cc.cond);
    }
    return cc;
}

static void gen_setcc1(DisasContext *s, int b, TCGv reg)
{
    CCPrepare cc = gen_prepare_cc(s, b, reg);

    if (cc.no_setcond) {
        if (cc.cond == TCG_COND_EQ) {
            tcg_gen_xori_tl(reg, cc.reg, 1);
        } else {
            tcg_gen_mov_tl(reg, cc.reg);
        }
        return;
    }

    if (cc.use_reg2) {
        tcg_gen_setcond_tl(cc.cond, reg, cc.reg, cc.reg2);
    } else {
        tcg_gen_setcondi_tl(cc.cond, reg, cc.reg, cc.imm);
    }
}

static inline void gen_compute_eflags_c(DisasContext *s, TCGv reg)
{
    gen_setcc1(s, JCC_B << 1, reg);
}

/* generate a conditional jump to label 'l1' according to jump opcode
   value 'b'. In the fast case, T0 is guaranteed not to be used. */
static inline void gen_jcc1_noeob(DisasContext *s, int b, TCGLabel *l1)
{
    CCPrepare cc = gen_prepare_cc(s, b, NULL);

    if (cc.use_reg2) {
        tcg_gen_brcond_tl(cc.cond, cc.reg, cc.reg2, l1);
    } else {
        tcg_gen_brcondi_tl(cc.cond, cc.reg, cc.imm, l1);
    }
}

/* Generate a conditional jump to label 'l1' according to jump opcode
   value 'b'. In the fast case, T0 is guaranteed not to be used.
   One or both of the branches will call gen_jmp_rel, so ensure
   cc_op is clean.  */
static inline void gen_jcc1(DisasContext *s, int b, TCGLabel *l1)
{
    CCPrepare cc = gen_prepare_cc(s, b, NULL);

    gen_update_cc_op(s);
    if (cc.use_reg2) {
        tcg_gen_brcond_tl(cc.cond, cc.reg, cc.reg2, l1);
    } else {
        tcg_gen_brcondi_tl(cc.cond, cc.reg, cc.imm, l1);
    }
}

/* XXX: does not work with gdbstub "ice" single step - not a
   serious problem.  The caller can jump to the returned label
   to stop the REP but, if the flags have changed, it has to call
   gen_update_cc_op before doing so.  */
static TCGLabel *gen_jz_ecx_string(DisasContext *s)
{
    TCGLabel *l1 = gen_new_label();
    TCGLabel *l2 = gen_new_label();

    gen_update_cc_op(s);
    gen_op_jnz_ecx(s, l1);
    gen_set_label(l2);
    gen_jmp_rel_csize(s, 0, 1);
    gen_set_label(l1);
    return l2;
}

static void gen_stos(DisasContext *s, MemOp ot)
{
    gen_string_movl_A0_EDI(s);
    gen_op_st_v(s, ot, s->T0, s->A0);
    gen_op_add_reg(s, s->aflag, R_EDI, gen_compute_Dshift(s, ot));
}

static void gen_lods(DisasContext *s, MemOp ot)
{
    gen_string_movl_A0_ESI(s);
    gen_op_ld_v(s, ot, s->T0, s->A0);
    gen_op_mov_reg_v(s, ot, R_EAX, s->T0);
    gen_op_add_reg(s, s->aflag, R_ESI, gen_compute_Dshift(s, ot));
}

static void gen_scas(DisasContext *s, MemOp ot)
{
    gen_string_movl_A0_EDI(s);
    gen_op_ld_v(s, ot, s->T1, s->A0);
    tcg_gen_mov_tl(cpu_cc_src, s->T1);
    tcg_gen_mov_tl(s->cc_srcT, s->T0);
    tcg_gen_sub_tl(cpu_cc_dst, s->T0, s->T1);
    set_cc_op(s, CC_OP_SUBB + ot);

    gen_op_add_reg(s, s->aflag, R_EDI, gen_compute_Dshift(s, ot));
}

static void gen_cmps(DisasContext *s, MemOp ot)
{
    TCGv dshift;

    gen_string_movl_A0_EDI(s);
    gen_op_ld_v(s, ot, s->T1, s->A0);
    gen_string_movl_A0_ESI(s);
    gen_op_ld_v(s, ot, s->T0, s->A0);
    tcg_gen_mov_tl(cpu_cc_src, s->T1);
    tcg_gen_mov_tl(s->cc_srcT, s->T0);
    tcg_gen_sub_tl(cpu_cc_dst, s->T0, s->T1);
    set_cc_op(s, CC_OP_SUBB + ot);

    dshift = gen_compute_Dshift(s, ot);
    gen_op_add_reg(s, s->aflag, R_ESI, dshift);
    gen_op_add_reg(s, s->aflag, R_EDI, dshift);
}

static void gen_bpt_io(DisasContext *s, TCGv_i32 t_port, int ot)
{
    if (s->flags & HF_IOBPT_MASK) {
#ifdef CONFIG_USER_ONLY
        /* user-mode cpu should not be in IOBPT mode */
        g_assert_not_reached();
#else
        TCGv_i32 t_size = tcg_constant_i32(1 << ot);
        TCGv t_next = eip_next_tl(s);
        gen_helper_bpt_io(tcg_env, t_port, t_size, t_next);
#endif /* CONFIG_USER_ONLY */
    }
}

static void gen_ins(DisasContext *s, MemOp ot)
{
    gen_string_movl_A0_EDI(s);
    /* Note: we must do this dummy write first to be restartable in
       case of page fault. */
    tcg_gen_movi_tl(s->T0, 0);
    gen_op_st_v(s, ot, s->T0, s->A0);
    tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_EDX]);
    tcg_gen_andi_i32(s->tmp2_i32, s->tmp2_i32, 0xffff);
    gen_helper_in_func(ot, s->T0, s->tmp2_i32);
    gen_op_st_v(s, ot, s->T0, s->A0);
    gen_op_add_reg(s, s->aflag, R_EDI, gen_compute_Dshift(s, ot));
    gen_bpt_io(s, s->tmp2_i32, ot);
}

static void gen_outs(DisasContext *s, MemOp ot)
{
    gen_string_movl_A0_ESI(s);
    gen_op_ld_v(s, ot, s->T0, s->A0);

    tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_EDX]);
    tcg_gen_andi_i32(s->tmp2_i32, s->tmp2_i32, 0xffff);
    tcg_gen_trunc_tl_i32(s->tmp3_i32, s->T0);
    gen_helper_out_func(ot, s->tmp2_i32, s->tmp3_i32);
    gen_op_add_reg(s, s->aflag, R_ESI, gen_compute_Dshift(s, ot));
    gen_bpt_io(s, s->tmp2_i32, ot);
}

/* Generate jumps to current or next instruction */
static void gen_repz(DisasContext *s, MemOp ot,
                     void (*fn)(DisasContext *s, MemOp ot))
{
    TCGLabel *l2;
    l2 = gen_jz_ecx_string(s);
    fn(s, ot);
    gen_op_add_reg_im(s, s->aflag, R_ECX, -1);
    /*
     * A loop would cause two single step exceptions if ECX = 1
     * before rep string_insn
     */
    if (s->repz_opt) {
        gen_op_jz_ecx(s, l2);
    }
    gen_jmp_rel_csize(s, -cur_insn_len(s), 0);
}

static void gen_repz_nz(DisasContext *s, MemOp ot,
                        void (*fn)(DisasContext *s, MemOp ot))
{
    TCGLabel *l2;
    int nz = (s->prefix & PREFIX_REPNZ) ? 1 : 0;

    l2 = gen_jz_ecx_string(s);
    fn(s, ot);
    gen_op_add_reg_im(s, s->aflag, R_ECX, -1);
    gen_jcc1(s, (JCC_Z << 1) | (nz ^ 1), l2);
    if (s->repz_opt) {
        gen_op_jz_ecx(s, l2);
    }
    /*
     * Only one iteration is done at a time, so the translation
     * block ends unconditionally after this instruction and there
     * is no control flow junction - no need to set CC_OP_DYNAMIC.
     */
    gen_jmp_rel_csize(s, -cur_insn_len(s), 0);
}

static void gen_helper_fp_arith_ST0_FT0(int op)
{
    switch (op) {
    case 0:
        gen_helper_fadd_ST0_FT0(tcg_env);
        break;
    case 1:
        gen_helper_fmul_ST0_FT0(tcg_env);
        break;
    case 2:
        gen_helper_fcom_ST0_FT0(tcg_env);
        break;
    case 3:
        gen_helper_fcom_ST0_FT0(tcg_env);
        break;
    case 4:
        gen_helper_fsub_ST0_FT0(tcg_env);
        break;
    case 5:
        gen_helper_fsubr_ST0_FT0(tcg_env);
        break;
    case 6:
        gen_helper_fdiv_ST0_FT0(tcg_env);
        break;
    case 7:
        gen_helper_fdivr_ST0_FT0(tcg_env);
        break;
    }
}

/* NOTE the exception in "r" op ordering */
static void gen_helper_fp_arith_STN_ST0(int op, int opreg)
{
    TCGv_i32 tmp = tcg_constant_i32(opreg);
    switch (op) {
    case 0:
        gen_helper_fadd_STN_ST0(tcg_env, tmp);
        break;
    case 1:
        gen_helper_fmul_STN_ST0(tcg_env, tmp);
        break;
    case 4:
        gen_helper_fsubr_STN_ST0(tcg_env, tmp);
        break;
    case 5:
        gen_helper_fsub_STN_ST0(tcg_env, tmp);
        break;
    case 6:
        gen_helper_fdivr_STN_ST0(tcg_env, tmp);
        break;
    case 7:
        gen_helper_fdiv_STN_ST0(tcg_env, tmp);
        break;
    }
}

static void gen_exception(DisasContext *s, int trapno)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_raise_exception(tcg_env, tcg_constant_i32(trapno));
    s->base.is_jmp = DISAS_NORETURN;
}

/* Generate #UD for the current instruction.  The assumption here is that
   the instruction is known, but it isn't allowed in the current cpu mode.  */
static void gen_illegal_opcode(DisasContext *s)
{
    gen_exception(s, EXCP06_ILLOP);
}

/* Generate #GP for the current instruction. */
static void gen_exception_gpf(DisasContext *s)
{
    gen_exception(s, EXCP0D_GPF);
}

/* Check for cpl == 0; if not, raise #GP and return false. */
static bool check_cpl0(DisasContext *s)
{
    if (CPL(s) == 0) {
        return true;
    }
    gen_exception_gpf(s);
    return false;
}

static void gen_shift_flags(DisasContext *s, MemOp ot, TCGv result,
                            TCGv shm1, TCGv count, bool is_right)
{
    TCGv_i32 z32, s32, oldop;
    TCGv z_tl;

    /* Store the results into the CC variables.  If we know that the
       variable must be dead, store unconditionally.  Otherwise we'll
       need to not disrupt the current contents.  */
    z_tl = tcg_constant_tl(0);
    if (cc_op_live[s->cc_op] & USES_CC_DST) {
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_cc_dst, count, z_tl,
                           result, cpu_cc_dst);
    } else {
        tcg_gen_mov_tl(cpu_cc_dst, result);
    }
    if (cc_op_live[s->cc_op] & USES_CC_SRC) {
        tcg_gen_movcond_tl(TCG_COND_NE, cpu_cc_src, count, z_tl,
                           shm1, cpu_cc_src);
    } else {
        tcg_gen_mov_tl(cpu_cc_src, shm1);
    }

    /* Get the two potential CC_OP values into temporaries.  */
    tcg_gen_movi_i32(s->tmp2_i32, (is_right ? CC_OP_SARB : CC_OP_SHLB) + ot);
    if (s->cc_op == CC_OP_DYNAMIC) {
        oldop = cpu_cc_op;
    } else {
        tcg_gen_movi_i32(s->tmp3_i32, s->cc_op);
        oldop = s->tmp3_i32;
    }

    /* Conditionally store the CC_OP value.  */
    z32 = tcg_constant_i32(0);
    s32 = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(s32, count);
    tcg_gen_movcond_i32(TCG_COND_NE, cpu_cc_op, s32, z32, s->tmp2_i32, oldop);

    /* The CC_OP value is no longer predictable.  */
    set_cc_op(s, CC_OP_DYNAMIC);
}

/* XXX: add faster immediate case */
static void gen_shiftd_rm_T1(DisasContext *s, MemOp ot, int op1,
                             bool is_right, TCGv count_in)
{
    target_ulong mask = (ot == MO_64 ? 63 : 31);
    TCGv count;

    /* load */
    if (op1 == OR_TMP0) {
        gen_op_ld_v(s, ot, s->T0, s->A0);
    } else {
        gen_op_mov_v_reg(s, ot, s->T0, op1);
    }

    count = tcg_temp_new();
    tcg_gen_andi_tl(count, count_in, mask);

    switch (ot) {
    case MO_16:
        /* Note: we implement the Intel behaviour for shift count > 16.
           This means "shrdw C, B, A" shifts A:B:A >> C.  Build the B:A
           portion by constructing it as a 32-bit value.  */
        if (is_right) {
            tcg_gen_deposit_tl(s->tmp0, s->T0, s->T1, 16, 16);
            tcg_gen_mov_tl(s->T1, s->T0);
            tcg_gen_mov_tl(s->T0, s->tmp0);
        } else {
            tcg_gen_deposit_tl(s->T1, s->T0, s->T1, 16, 16);
        }
        /*
         * If TARGET_X86_64 defined then fall through into MO_32 case,
         * otherwise fall through default case.
         */
    case MO_32:
#ifdef TARGET_X86_64
        /* Concatenate the two 32-bit values and use a 64-bit shift.  */
        tcg_gen_subi_tl(s->tmp0, count, 1);
        if (is_right) {
            tcg_gen_concat_tl_i64(s->T0, s->T0, s->T1);
            tcg_gen_shr_i64(s->tmp0, s->T0, s->tmp0);
            tcg_gen_shr_i64(s->T0, s->T0, count);
        } else {
            tcg_gen_concat_tl_i64(s->T0, s->T1, s->T0);
            tcg_gen_shl_i64(s->tmp0, s->T0, s->tmp0);
            tcg_gen_shl_i64(s->T0, s->T0, count);
            tcg_gen_shri_i64(s->tmp0, s->tmp0, 32);
            tcg_gen_shri_i64(s->T0, s->T0, 32);
        }
        break;
#endif
    default:
        tcg_gen_subi_tl(s->tmp0, count, 1);
        if (is_right) {
            tcg_gen_shr_tl(s->tmp0, s->T0, s->tmp0);

            tcg_gen_subfi_tl(s->tmp4, mask + 1, count);
            tcg_gen_shr_tl(s->T0, s->T0, count);
            tcg_gen_shl_tl(s->T1, s->T1, s->tmp4);
        } else {
            tcg_gen_shl_tl(s->tmp0, s->T0, s->tmp0);
            if (ot == MO_16) {
                /* Only needed if count > 16, for Intel behaviour.  */
                tcg_gen_subfi_tl(s->tmp4, 33, count);
                tcg_gen_shr_tl(s->tmp4, s->T1, s->tmp4);
                tcg_gen_or_tl(s->tmp0, s->tmp0, s->tmp4);
            }

            tcg_gen_subfi_tl(s->tmp4, mask + 1, count);
            tcg_gen_shl_tl(s->T0, s->T0, count);
            tcg_gen_shr_tl(s->T1, s->T1, s->tmp4);
        }
        tcg_gen_movi_tl(s->tmp4, 0);
        tcg_gen_movcond_tl(TCG_COND_EQ, s->T1, count, s->tmp4,
                           s->tmp4, s->T1);
        tcg_gen_or_tl(s->T0, s->T0, s->T1);
        break;
    }

    /* store */
    gen_op_st_rm_T0_A0(s, ot, op1);

    gen_shift_flags(s, ot, s->T0, s->tmp0, count, is_right);
}

#define X86_MAX_INSN_LENGTH 15

static uint64_t advance_pc(CPUX86State *env, DisasContext *s, int num_bytes)
{
    uint64_t pc = s->pc;

    /* This is a subsequent insn that crosses a page boundary.  */
    if (s->base.num_insns > 1 &&
        !is_same_page(&s->base, s->pc + num_bytes - 1)) {
        siglongjmp(s->jmpbuf, 2);
    }

    s->pc += num_bytes;
    if (unlikely(cur_insn_len(s) > X86_MAX_INSN_LENGTH)) {
        /* If the instruction's 16th byte is on a different page than the 1st, a
         * page fault on the second page wins over the general protection fault
         * caused by the instruction being too long.
         * This can happen even if the operand is only one byte long!
         */
        if (((s->pc - 1) ^ (pc - 1)) & TARGET_PAGE_MASK) {
            (void)translator_ldub(env, &s->base,
                                  (s->pc - 1) & TARGET_PAGE_MASK);
        }
        siglongjmp(s->jmpbuf, 1);
    }

    return pc;
}

static inline uint8_t x86_ldub_code(CPUX86State *env, DisasContext *s)
{
    return translator_ldub(env, &s->base, advance_pc(env, s, 1));
}

static inline uint16_t x86_lduw_code(CPUX86State *env, DisasContext *s)
{
    return translator_lduw(env, &s->base, advance_pc(env, s, 2));
}

static inline uint32_t x86_ldl_code(CPUX86State *env, DisasContext *s)
{
    return translator_ldl(env, &s->base, advance_pc(env, s, 4));
}

#ifdef TARGET_X86_64
static inline uint64_t x86_ldq_code(CPUX86State *env, DisasContext *s)
{
    return translator_ldq(env, &s->base, advance_pc(env, s, 8));
}
#endif

/* Decompose an address.  */

typedef struct AddressParts {
    int def_seg;
    int base;
    int index;
    int scale;
    target_long disp;
} AddressParts;

static AddressParts gen_lea_modrm_0(CPUX86State *env, DisasContext *s,
                                    int modrm)
{
    int def_seg, base, index, scale, mod, rm;
    target_long disp;
    bool havesib;

    def_seg = R_DS;
    index = -1;
    scale = 0;
    disp = 0;

    mod = (modrm >> 6) & 3;
    rm = modrm & 7;
    base = rm | REX_B(s);

    if (mod == 3) {
        /* Normally filtered out earlier, but including this path
           simplifies multi-byte nop, as well as bndcl, bndcu, bndcn.  */
        goto done;
    }

    switch (s->aflag) {
    case MO_64:
    case MO_32:
        havesib = 0;
        if (rm == 4) {
            int code = x86_ldub_code(env, s);
            scale = (code >> 6) & 3;
            index = ((code >> 3) & 7) | REX_X(s);
            if (index == 4) {
                index = -1;  /* no index */
            }
            base = (code & 7) | REX_B(s);
            havesib = 1;
        }

        switch (mod) {
        case 0:
            if ((base & 7) == 5) {
                base = -1;
                disp = (int32_t)x86_ldl_code(env, s);
                if (CODE64(s) && !havesib) {
                    base = -2;
                    disp += s->pc + s->rip_offset;
                }
            }
            break;
        case 1:
            disp = (int8_t)x86_ldub_code(env, s);
            break;
        default:
        case 2:
            disp = (int32_t)x86_ldl_code(env, s);
            break;
        }

        /* For correct popl handling with esp.  */
        if (base == R_ESP && s->popl_esp_hack) {
            disp += s->popl_esp_hack;
        }
        if (base == R_EBP || base == R_ESP) {
            def_seg = R_SS;
        }
        break;

    case MO_16:
        if (mod == 0) {
            if (rm == 6) {
                base = -1;
                disp = x86_lduw_code(env, s);
                break;
            }
        } else if (mod == 1) {
            disp = (int8_t)x86_ldub_code(env, s);
        } else {
            disp = (int16_t)x86_lduw_code(env, s);
        }

        switch (rm) {
        case 0:
            base = R_EBX;
            index = R_ESI;
            break;
        case 1:
            base = R_EBX;
            index = R_EDI;
            break;
        case 2:
            base = R_EBP;
            index = R_ESI;
            def_seg = R_SS;
            break;
        case 3:
            base = R_EBP;
            index = R_EDI;
            def_seg = R_SS;
            break;
        case 4:
            base = R_ESI;
            break;
        case 5:
            base = R_EDI;
            break;
        case 6:
            base = R_EBP;
            def_seg = R_SS;
            break;
        default:
        case 7:
            base = R_EBX;
            break;
        }
        break;

    default:
        g_assert_not_reached();
    }

 done:
    return (AddressParts){ def_seg, base, index, scale, disp };
}

/* Compute the address, with a minimum number of TCG ops.  */
static TCGv gen_lea_modrm_1(DisasContext *s, AddressParts a, bool is_vsib)
{
    TCGv ea = NULL;

    if (a.index >= 0 && !is_vsib) {
        if (a.scale == 0) {
            ea = cpu_regs[a.index];
        } else {
            tcg_gen_shli_tl(s->A0, cpu_regs[a.index], a.scale);
            ea = s->A0;
        }
        if (a.base >= 0) {
            tcg_gen_add_tl(s->A0, ea, cpu_regs[a.base]);
            ea = s->A0;
        }
    } else if (a.base >= 0) {
        ea = cpu_regs[a.base];
    }
    if (!ea) {
        if (tb_cflags(s->base.tb) & CF_PCREL && a.base == -2) {
            /* With cpu_eip ~= pc_save, the expression is pc-relative. */
            tcg_gen_addi_tl(s->A0, cpu_eip, a.disp - s->pc_save);
        } else {
            tcg_gen_movi_tl(s->A0, a.disp);
        }
        ea = s->A0;
    } else if (a.disp != 0) {
        tcg_gen_addi_tl(s->A0, ea, a.disp);
        ea = s->A0;
    }

    return ea;
}

static void gen_lea_modrm(CPUX86State *env, DisasContext *s, int modrm)
{
    AddressParts a = gen_lea_modrm_0(env, s, modrm);
    TCGv ea = gen_lea_modrm_1(s, a, false);
    gen_lea_v_seg(s, ea, a.def_seg, s->override);
}

static void gen_nop_modrm(CPUX86State *env, DisasContext *s, int modrm)
{
    (void)gen_lea_modrm_0(env, s, modrm);
}

/* Used for BNDCL, BNDCU, BNDCN.  */
static void gen_bndck(CPUX86State *env, DisasContext *s, int modrm,
                      TCGCond cond, TCGv_i64 bndv)
{
    AddressParts a = gen_lea_modrm_0(env, s, modrm);
    TCGv ea = gen_lea_modrm_1(s, a, false);

    tcg_gen_extu_tl_i64(s->tmp1_i64, ea);
    if (!CODE64(s)) {
        tcg_gen_ext32u_i64(s->tmp1_i64, s->tmp1_i64);
    }
    tcg_gen_setcond_i64(cond, s->tmp1_i64, s->tmp1_i64, bndv);
    tcg_gen_extrl_i64_i32(s->tmp2_i32, s->tmp1_i64);
    gen_helper_bndck(tcg_env, s->tmp2_i32);
}

/* generate modrm load of memory or register. */
static void gen_ld_modrm(CPUX86State *env, DisasContext *s, int modrm, MemOp ot)
{
    int mod, rm;

    mod = (modrm >> 6) & 3;
    rm = (modrm & 7) | REX_B(s);
    if (mod == 3) {
        gen_op_mov_v_reg(s, ot, s->T0, rm);
    } else {
        gen_lea_modrm(env, s, modrm);
        gen_op_ld_v(s, ot, s->T0, s->A0);
    }
}

/* generate modrm store of memory or register. */
static void gen_st_modrm(CPUX86State *env, DisasContext *s, int modrm, MemOp ot)
{
    int mod, rm;

    mod = (modrm >> 6) & 3;
    rm = (modrm & 7) | REX_B(s);
    if (mod == 3) {
        gen_op_mov_reg_v(s, ot, rm, s->T0);
    } else {
        gen_lea_modrm(env, s, modrm);
        gen_op_st_v(s, ot, s->T0, s->A0);
    }
}

static target_ulong insn_get_addr(CPUX86State *env, DisasContext *s, MemOp ot)
{
    target_ulong ret;

    switch (ot) {
    case MO_8:
        ret = x86_ldub_code(env, s);
        break;
    case MO_16:
        ret = x86_lduw_code(env, s);
        break;
    case MO_32:
        ret = x86_ldl_code(env, s);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        ret = x86_ldq_code(env, s);
        break;
#endif
    default:
        g_assert_not_reached();
    }
    return ret;
}

static inline uint32_t insn_get(CPUX86State *env, DisasContext *s, MemOp ot)
{
    uint32_t ret;

    switch (ot) {
    case MO_8:
        ret = x86_ldub_code(env, s);
        break;
    case MO_16:
        ret = x86_lduw_code(env, s);
        break;
    case MO_32:
#ifdef TARGET_X86_64
    case MO_64:
#endif
        ret = x86_ldl_code(env, s);
        break;
    default:
        g_assert_not_reached();
    }
    return ret;
}

static target_long insn_get_signed(CPUX86State *env, DisasContext *s, MemOp ot)
{
    target_long ret;

    switch (ot) {
    case MO_8:
        ret = (int8_t) x86_ldub_code(env, s);
        break;
    case MO_16:
        ret = (int16_t) x86_lduw_code(env, s);
        break;
    case MO_32:
        ret = (int32_t) x86_ldl_code(env, s);
        break;
#ifdef TARGET_X86_64
    case MO_64:
        ret = x86_ldq_code(env, s);
        break;
#endif
    default:
        g_assert_not_reached();
    }
    return ret;
}

static void gen_conditional_jump_labels(DisasContext *s, target_long diff,
                                        TCGLabel *not_taken, TCGLabel *taken)
{
    if (not_taken) {
        gen_set_label(not_taken);
    }
    gen_jmp_rel_csize(s, 0, 1);

    gen_set_label(taken);
    gen_jmp_rel(s, s->dflag, diff, 0);
}

static void gen_jcc(DisasContext *s, int b, int diff)
{
    TCGLabel *l1 = gen_new_label();

    gen_jcc1(s, b, l1);
    gen_conditional_jump_labels(s, diff, NULL, l1);
}

static void gen_cmovcc1(DisasContext *s, int b, TCGv dest, TCGv src)
{
    CCPrepare cc = gen_prepare_cc(s, b, NULL);

    if (!cc.use_reg2) {
        cc.reg2 = tcg_constant_tl(cc.imm);
    }

    tcg_gen_movcond_tl(cc.cond, dest, cc.reg, cc.reg2, src, dest);
}

static void gen_op_movl_seg_real(DisasContext *s, X86Seg seg_reg, TCGv seg)
{
    TCGv selector = tcg_temp_new();
    tcg_gen_ext16u_tl(selector, seg);
    tcg_gen_st32_tl(selector, tcg_env,
                    offsetof(CPUX86State,segs[seg_reg].selector));
    tcg_gen_shli_tl(cpu_seg_base[seg_reg], selector, 4);
}

/* move SRC to seg_reg and compute if the CPU state may change. Never
   call this function with seg_reg == R_CS */
static void gen_movl_seg(DisasContext *s, X86Seg seg_reg, TCGv src)
{
    if (PE(s) && !VM86(s)) {
        tcg_gen_trunc_tl_i32(s->tmp2_i32, src);
        gen_helper_load_seg(tcg_env, tcg_constant_i32(seg_reg), s->tmp2_i32);
        /* abort translation because the addseg value may change or
           because ss32 may change. For R_SS, translation must always
           stop as a special handling must be done to disable hardware
           interrupts for the next instruction */
        if (seg_reg == R_SS) {
            s->base.is_jmp = DISAS_EOB_INHIBIT_IRQ;
        } else if (CODE32(s) && seg_reg < R_FS) {
            s->base.is_jmp = DISAS_EOB_NEXT;
        }
    } else {
        gen_op_movl_seg_real(s, seg_reg, src);
        if (seg_reg == R_SS) {
            s->base.is_jmp = DISAS_EOB_INHIBIT_IRQ;
        }
    }
}

static void gen_far_call(DisasContext *s)
{
    TCGv_i32 new_cs = tcg_temp_new_i32();
    tcg_gen_trunc_tl_i32(new_cs, s->T1);
    if (PE(s) && !VM86(s)) {
        gen_helper_lcall_protected(tcg_env, new_cs, s->T0,
                                   tcg_constant_i32(s->dflag - 1),
                                   eip_next_tl(s));
    } else {
        TCGv_i32 new_eip = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(new_eip, s->T0);
        gen_helper_lcall_real(tcg_env, new_cs, new_eip,
                              tcg_constant_i32(s->dflag - 1),
                              eip_next_i32(s));
    }
    s->base.is_jmp = DISAS_JUMP;
}

static void gen_far_jmp(DisasContext *s)
{
    if (PE(s) && !VM86(s)) {
        TCGv_i32 new_cs = tcg_temp_new_i32();
        tcg_gen_trunc_tl_i32(new_cs, s->T1);
        gen_helper_ljmp_protected(tcg_env, new_cs, s->T0,
                                  eip_next_tl(s));
    } else {
        gen_op_movl_seg_real(s, R_CS, s->T1);
        gen_op_jmp_v(s, s->T0);
    }
    s->base.is_jmp = DISAS_JUMP;
}

static void gen_svm_check_intercept(DisasContext *s, uint32_t type)
{
    /* no SVM activated; fast case */
    if (likely(!GUEST(s))) {
        return;
    }
    gen_helper_svm_check_intercept(tcg_env, tcg_constant_i32(type));
}

static inline void gen_stack_update(DisasContext *s, int addend)
{
    gen_op_add_reg_im(s, mo_stacksize(s), R_ESP, addend);
}

static void gen_lea_ss_ofs(DisasContext *s, TCGv dest, TCGv src, target_ulong offset)
{
    if (offset) {
        tcg_gen_addi_tl(dest, src, offset);
        src = dest;
    }
    gen_lea_v_seg_dest(s, mo_stacksize(s), dest, src, R_SS, -1);
}

/* Generate a push. It depends on ss32, addseg and dflag.  */
static void gen_push_v(DisasContext *s, TCGv val)
{
    MemOp d_ot = mo_pushpop(s, s->dflag);
    MemOp a_ot = mo_stacksize(s);
    int size = 1 << d_ot;
    TCGv new_esp = tcg_temp_new();

    tcg_gen_subi_tl(new_esp, cpu_regs[R_ESP], size);

    /* Now reduce the value to the address size and apply SS base.  */
    gen_lea_ss_ofs(s, s->A0, new_esp, 0);
    gen_op_st_v(s, d_ot, val, s->A0);
    gen_op_mov_reg_v(s, a_ot, R_ESP, new_esp);
}

/* two step pop is necessary for precise exceptions */
static MemOp gen_pop_T0(DisasContext *s)
{
    MemOp d_ot = mo_pushpop(s, s->dflag);

    gen_lea_ss_ofs(s, s->T0, cpu_regs[R_ESP], 0);
    gen_op_ld_v(s, d_ot, s->T0, s->T0);

    return d_ot;
}

static inline void gen_pop_update(DisasContext *s, MemOp ot)
{
    gen_stack_update(s, 1 << ot);
}

static void gen_pusha(DisasContext *s)
{
    MemOp d_ot = s->dflag;
    int size = 1 << d_ot;
    int i;

    for (i = 0; i < 8; i++) {
        gen_lea_ss_ofs(s, s->A0, cpu_regs[R_ESP], (i - 8) * size);
        gen_op_st_v(s, d_ot, cpu_regs[7 - i], s->A0);
    }

    gen_stack_update(s, -8 * size);
}

static void gen_popa(DisasContext *s)
{
    MemOp d_ot = s->dflag;
    int size = 1 << d_ot;
    int i;

    for (i = 0; i < 8; i++) {
        /* ESP is not reloaded */
        if (7 - i == R_ESP) {
            continue;
        }
        gen_lea_ss_ofs(s, s->A0, cpu_regs[R_ESP], i * size);
        gen_op_ld_v(s, d_ot, s->T0, s->A0);
        gen_op_mov_reg_v(s, d_ot, 7 - i, s->T0);
    }

    gen_stack_update(s, 8 * size);
}

static void gen_enter(DisasContext *s, int esp_addend, int level)
{
    MemOp d_ot = mo_pushpop(s, s->dflag);
    MemOp a_ot = mo_stacksize(s);
    int size = 1 << d_ot;

    /* Push BP; compute FrameTemp into T1.  */
    tcg_gen_subi_tl(s->T1, cpu_regs[R_ESP], size);
    gen_lea_ss_ofs(s, s->A0, s->T1, 0);
    gen_op_st_v(s, d_ot, cpu_regs[R_EBP], s->A0);

    level &= 31;
    if (level != 0) {
        int i;

        /* Copy level-1 pointers from the previous frame.  */
        for (i = 1; i < level; ++i) {
            gen_lea_ss_ofs(s, s->A0, cpu_regs[R_EBP], -size * i);
            gen_op_ld_v(s, d_ot, s->tmp0, s->A0);

            gen_lea_ss_ofs(s, s->A0, s->T1, -size * i);
            gen_op_st_v(s, d_ot, s->tmp0, s->A0);
        }

        /* Push the current FrameTemp as the last level.  */
        gen_lea_ss_ofs(s, s->A0, s->T1, -size * level);
        gen_op_st_v(s, d_ot, s->T1, s->A0);
    }

    /* Copy the FrameTemp value to EBP.  */
    gen_op_mov_reg_v(s, a_ot, R_EBP, s->T1);

    /* Compute the final value of ESP.  */
    tcg_gen_subi_tl(s->T1, s->T1, esp_addend + size * level);
    gen_op_mov_reg_v(s, a_ot, R_ESP, s->T1);
}

static void gen_leave(DisasContext *s)
{
    MemOp d_ot = mo_pushpop(s, s->dflag);
    MemOp a_ot = mo_stacksize(s);

    gen_lea_ss_ofs(s, s->A0, cpu_regs[R_EBP], 0);
    gen_op_ld_v(s, d_ot, s->T0, s->A0);

    tcg_gen_addi_tl(s->T1, cpu_regs[R_EBP], 1 << d_ot);

    gen_op_mov_reg_v(s, d_ot, R_EBP, s->T0);
    gen_op_mov_reg_v(s, a_ot, R_ESP, s->T1);
}

/* Similarly, except that the assumption here is that we don't decode
   the instruction at all -- either a missing opcode, an unimplemented
   feature, or just a bogus instruction stream.  */
static void gen_unknown_opcode(CPUX86State *env, DisasContext *s)
{
    gen_illegal_opcode(s);

    if (qemu_loglevel_mask(LOG_UNIMP)) {
        FILE *logfile = qemu_log_trylock();
        if (logfile) {
            target_ulong pc = s->base.pc_next, end = s->pc;

            fprintf(logfile, "ILLOPC: " TARGET_FMT_lx ":", pc);
            for (; pc < end; ++pc) {
                fprintf(logfile, " %02x", translator_ldub(env, &s->base, pc));
            }
            fprintf(logfile, "\n");
            qemu_log_unlock(logfile);
        }
    }
}

/* an interrupt is different from an exception because of the
   privilege checks */
static void gen_interrupt(DisasContext *s, uint8_t intno)
{
    gen_update_cc_op(s);
    gen_update_eip_cur(s);
    gen_helper_raise_interrupt(tcg_env, tcg_constant_i32(intno),
                               cur_insn_len_i32(s));
    s->base.is_jmp = DISAS_NORETURN;
}

static void gen_set_hflag(DisasContext *s, uint32_t mask)
{
    if ((s->flags & mask) == 0) {
        TCGv_i32 t = tcg_temp_new_i32();
        tcg_gen_ld_i32(t, tcg_env, offsetof(CPUX86State, hflags));
        tcg_gen_ori_i32(t, t, mask);
        tcg_gen_st_i32(t, tcg_env, offsetof(CPUX86State, hflags));
        s->flags |= mask;
    }
}

static void gen_reset_hflag(DisasContext *s, uint32_t mask)
{
    if (s->flags & mask) {
        TCGv_i32 t = tcg_temp_new_i32();
        tcg_gen_ld_i32(t, tcg_env, offsetof(CPUX86State, hflags));
        tcg_gen_andi_i32(t, t, ~mask);
        tcg_gen_st_i32(t, tcg_env, offsetof(CPUX86State, hflags));
        s->flags &= ~mask;
    }
}

static void gen_set_eflags(DisasContext *s, target_ulong mask)
{
    TCGv t = tcg_temp_new();

    tcg_gen_ld_tl(t, tcg_env, offsetof(CPUX86State, eflags));
    tcg_gen_ori_tl(t, t, mask);
    tcg_gen_st_tl(t, tcg_env, offsetof(CPUX86State, eflags));
}

static void gen_reset_eflags(DisasContext *s, target_ulong mask)
{
    TCGv t = tcg_temp_new();

    tcg_gen_ld_tl(t, tcg_env, offsetof(CPUX86State, eflags));
    tcg_gen_andi_tl(t, t, ~mask);
    tcg_gen_st_tl(t, tcg_env, offsetof(CPUX86State, eflags));
}

/* Clear BND registers during legacy branches.  */
static void gen_bnd_jmp(DisasContext *s)
{
    /* Clear the registers only if BND prefix is missing, MPX is enabled,
       and if the BNDREGs are known to be in use (non-zero) already.
       The helper itself will check BNDPRESERVE at runtime.  */
    if ((s->prefix & PREFIX_REPNZ) == 0
        && (s->flags & HF_MPX_EN_MASK) != 0
        && (s->flags & HF_MPX_IU_MASK) != 0) {
        gen_helper_bnd_jmp(tcg_env);
    }
}

/*
 * Generate an end of block, including common tasks such as generating
 * single step traps, resetting the RF flag, and handling the interrupt
 * shadow.
 */
static void
gen_eob(DisasContext *s, int mode)
{
    bool inhibit_reset;

    gen_update_cc_op(s);

    /* If several instructions disable interrupts, only the first does it.  */
    inhibit_reset = false;
    if (s->flags & HF_INHIBIT_IRQ_MASK) {
        gen_reset_hflag(s, HF_INHIBIT_IRQ_MASK);
        inhibit_reset = true;
    } else if (mode == DISAS_EOB_INHIBIT_IRQ) {
        gen_set_hflag(s, HF_INHIBIT_IRQ_MASK);
    }

    if (s->base.tb->flags & HF_RF_MASK) {
        gen_reset_eflags(s, RF_MASK);
    }
    if (mode == DISAS_EOB_RECHECK_TF) {
        gen_helper_rechecking_single_step(tcg_env);
        tcg_gen_exit_tb(NULL, 0);
    } else if ((s->flags & HF_TF_MASK) && mode != DISAS_EOB_INHIBIT_IRQ) {
        gen_helper_single_step(tcg_env);
    } else if (mode == DISAS_JUMP &&
               /* give irqs a chance to happen */
               !inhibit_reset) {
        tcg_gen_lookup_and_goto_ptr();
    } else {
        tcg_gen_exit_tb(NULL, 0);
    }

    s->base.is_jmp = DISAS_NORETURN;
}

/* Jump to eip+diff, truncating the result to OT. */
static void gen_jmp_rel(DisasContext *s, MemOp ot, int diff, int tb_num)
{
    bool use_goto_tb = s->jmp_opt;
    target_ulong mask = -1;
    target_ulong new_pc = s->pc + diff;
    target_ulong new_eip = new_pc - s->cs_base;

    assert(!s->cc_op_dirty);

    /* In 64-bit mode, operand size is fixed at 64 bits. */
    if (!CODE64(s)) {
        if (ot == MO_16) {
            mask = 0xffff;
            if (tb_cflags(s->base.tb) & CF_PCREL && CODE32(s)) {
                use_goto_tb = false;
            }
        } else {
            mask = 0xffffffff;
        }
    }
    new_eip &= mask;

    if (tb_cflags(s->base.tb) & CF_PCREL) {
        tcg_gen_addi_tl(cpu_eip, cpu_eip, new_pc - s->pc_save);
        /*
         * If we can prove the branch does not leave the page and we have
         * no extra masking to apply (data16 branch in code32, see above),
         * then we have also proven that the addition does not wrap.
         */
        if (!use_goto_tb || !is_same_page(&s->base, new_pc)) {
            tcg_gen_andi_tl(cpu_eip, cpu_eip, mask);
            use_goto_tb = false;
        }
    } else if (!CODE64(s)) {
        new_pc = (uint32_t)(new_eip + s->cs_base);
    }

    if (use_goto_tb && translator_use_goto_tb(&s->base, new_pc)) {
        /* jump to same page: we can use a direct jump */
        tcg_gen_goto_tb(tb_num);
        if (!(tb_cflags(s->base.tb) & CF_PCREL)) {
            tcg_gen_movi_tl(cpu_eip, new_eip);
        }
        tcg_gen_exit_tb(s->base.tb, tb_num);
        s->base.is_jmp = DISAS_NORETURN;
    } else {
        if (!(tb_cflags(s->base.tb) & CF_PCREL)) {
            tcg_gen_movi_tl(cpu_eip, new_eip);
        }
        if (s->jmp_opt) {
            gen_eob(s, DISAS_JUMP);   /* jump to another page */
        } else {
            gen_eob(s, DISAS_EOB_ONLY);  /* exit to main loop */
        }
    }
}

/* Jump to eip+diff, truncating to the current code size. */
static void gen_jmp_rel_csize(DisasContext *s, int diff, int tb_num)
{
    /* CODE64 ignores the OT argument, so we need not consider it. */
    gen_jmp_rel(s, CODE32(s) ? MO_32 : MO_16, diff, tb_num);
}

static inline void gen_ldq_env_A0(DisasContext *s, int offset)
{
    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
    tcg_gen_st_i64(s->tmp1_i64, tcg_env, offset);
}

static inline void gen_stq_env_A0(DisasContext *s, int offset)
{
    tcg_gen_ld_i64(s->tmp1_i64, tcg_env, offset);
    tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0, s->mem_index, MO_LEUQ);
}

static inline void gen_ldo_env_A0(DisasContext *s, int offset, bool align)
{
    MemOp atom = (s->cpuid_ext_features & CPUID_EXT_AVX
                  ? MO_ATOM_IFALIGN : MO_ATOM_IFALIGN_PAIR);
    MemOp mop = MO_128 | MO_LE | atom | (align ? MO_ALIGN_16 : 0);
    int mem_index = s->mem_index;
    TCGv_i128 t = tcg_temp_new_i128();

    tcg_gen_qemu_ld_i128(t, s->A0, mem_index, mop);
    tcg_gen_st_i128(t, tcg_env, offset);
}

static inline void gen_sto_env_A0(DisasContext *s, int offset, bool align)
{
    MemOp atom = (s->cpuid_ext_features & CPUID_EXT_AVX
                  ? MO_ATOM_IFALIGN : MO_ATOM_IFALIGN_PAIR);
    MemOp mop = MO_128 | MO_LE | atom | (align ? MO_ALIGN_16 : 0);
    int mem_index = s->mem_index;
    TCGv_i128 t = tcg_temp_new_i128();

    tcg_gen_ld_i128(t, tcg_env, offset);
    tcg_gen_qemu_st_i128(t, s->A0, mem_index, mop);
}

static void gen_ldy_env_A0(DisasContext *s, int offset, bool align)
{
    MemOp mop = MO_128 | MO_LE | MO_ATOM_IFALIGN_PAIR;
    int mem_index = s->mem_index;
    TCGv_i128 t0 = tcg_temp_new_i128();
    TCGv_i128 t1 = tcg_temp_new_i128();

    tcg_gen_qemu_ld_i128(t0, s->A0, mem_index, mop | (align ? MO_ALIGN_32 : 0));
    tcg_gen_addi_tl(s->tmp0, s->A0, 16);
    tcg_gen_qemu_ld_i128(t1, s->tmp0, mem_index, mop);

    tcg_gen_st_i128(t0, tcg_env, offset + offsetof(YMMReg, YMM_X(0)));
    tcg_gen_st_i128(t1, tcg_env, offset + offsetof(YMMReg, YMM_X(1)));
}

static void gen_sty_env_A0(DisasContext *s, int offset, bool align)
{
    MemOp mop = MO_128 | MO_LE | MO_ATOM_IFALIGN_PAIR;
    int mem_index = s->mem_index;
    TCGv_i128 t = tcg_temp_new_i128();

    tcg_gen_ld_i128(t, tcg_env, offset + offsetof(YMMReg, YMM_X(0)));
    tcg_gen_qemu_st_i128(t, s->A0, mem_index, mop | (align ? MO_ALIGN_32 : 0));
    tcg_gen_addi_tl(s->tmp0, s->A0, 16);
    tcg_gen_ld_i128(t, tcg_env, offset + offsetof(YMMReg, YMM_X(1)));
    tcg_gen_qemu_st_i128(t, s->tmp0, mem_index, mop);
}

static void gen_cmpxchg8b(DisasContext *s, CPUX86State *env, int modrm)
{
    TCGv_i64 cmp, val, old;
    TCGv Z;

    gen_lea_modrm(env, s, modrm);

    cmp = tcg_temp_new_i64();
    val = tcg_temp_new_i64();
    old = tcg_temp_new_i64();

    /* Construct the comparison values from the register pair. */
    tcg_gen_concat_tl_i64(cmp, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    tcg_gen_concat_tl_i64(val, cpu_regs[R_EBX], cpu_regs[R_ECX]);

    /* Only require atomic with LOCK; non-parallel handled in generator. */
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_cmpxchg_i64(old, s->A0, cmp, val, s->mem_index, MO_TEUQ);
    } else {
        tcg_gen_nonatomic_cmpxchg_i64(old, s->A0, cmp, val,
                                      s->mem_index, MO_TEUQ);
    }

    /* Set tmp0 to match the required value of Z. */
    tcg_gen_setcond_i64(TCG_COND_EQ, cmp, old, cmp);
    Z = tcg_temp_new();
    tcg_gen_trunc_i64_tl(Z, cmp);

    /*
     * Extract the result values for the register pair.
     * For 32-bit, we may do this unconditionally, because on success (Z=1),
     * the old value matches the previous value in EDX:EAX.  For x86_64,
     * the store must be conditional, because we must leave the source
     * registers unchanged on success, and zero-extend the writeback
     * on failure (Z=0).
     */
    if (TARGET_LONG_BITS == 32) {
        tcg_gen_extr_i64_tl(cpu_regs[R_EAX], cpu_regs[R_EDX], old);
    } else {
        TCGv zero = tcg_constant_tl(0);

        tcg_gen_extr_i64_tl(s->T0, s->T1, old);
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_regs[R_EAX], Z, zero,
                           s->T0, cpu_regs[R_EAX]);
        tcg_gen_movcond_tl(TCG_COND_EQ, cpu_regs[R_EDX], Z, zero,
                           s->T1, cpu_regs[R_EDX]);
    }

    /* Update Z. */
    gen_compute_eflags(s);
    tcg_gen_deposit_tl(cpu_cc_src, cpu_cc_src, Z, ctz32(CC_Z), 1);
}

#ifdef TARGET_X86_64
static void gen_cmpxchg16b(DisasContext *s, CPUX86State *env, int modrm)
{
    MemOp mop = MO_TE | MO_128 | MO_ALIGN;
    TCGv_i64 t0, t1;
    TCGv_i128 cmp, val;

    gen_lea_modrm(env, s, modrm);

    cmp = tcg_temp_new_i128();
    val = tcg_temp_new_i128();
    tcg_gen_concat_i64_i128(cmp, cpu_regs[R_EAX], cpu_regs[R_EDX]);
    tcg_gen_concat_i64_i128(val, cpu_regs[R_EBX], cpu_regs[R_ECX]);

    /* Only require atomic with LOCK; non-parallel handled in generator. */
    if (s->prefix & PREFIX_LOCK) {
        tcg_gen_atomic_cmpxchg_i128(val, s->A0, cmp, val, s->mem_index, mop);
    } else {
        tcg_gen_nonatomic_cmpxchg_i128(val, s->A0, cmp, val, s->mem_index, mop);
    }

    tcg_gen_extr_i128_i64(s->T0, s->T1, val);

    /* Determine success after the fact. */
    t0 = tcg_temp_new_i64();
    t1 = tcg_temp_new_i64();
    tcg_gen_xor_i64(t0, s->T0, cpu_regs[R_EAX]);
    tcg_gen_xor_i64(t1, s->T1, cpu_regs[R_EDX]);
    tcg_gen_or_i64(t0, t0, t1);

    /* Update Z. */
    gen_compute_eflags(s);
    tcg_gen_setcondi_i64(TCG_COND_EQ, t0, t0, 0);
    tcg_gen_deposit_tl(cpu_cc_src, cpu_cc_src, t0, ctz32(CC_Z), 1);

    /*
     * Extract the result values for the register pair.  We may do this
     * unconditionally, because on success (Z=1), the old value matches
     * the previous value in RDX:RAX.
     */
    tcg_gen_mov_i64(cpu_regs[R_EAX], s->T0);
    tcg_gen_mov_i64(cpu_regs[R_EDX], s->T1);
}
#endif

static bool disas_insn_x87(DisasContext *s, CPUState *cpu, int b)
{
    CPUX86State *env = cpu_env(cpu);
    bool update_fip = true;
    int modrm, mod, rm, op;

    if (s->flags & (HF_EM_MASK | HF_TS_MASK)) {
        /* if CR0.EM or CR0.TS are set, generate an FPU exception */
        /* XXX: what to do if illegal op ? */
        gen_exception(s, EXCP07_PREX);
        return true;
    }
    modrm = x86_ldub_code(env, s);
    mod = (modrm >> 6) & 3;
    rm = modrm & 7;
    op = ((b & 7) << 3) | ((modrm >> 3) & 7);
    if (mod != 3) {
        /* memory op */
        AddressParts a = gen_lea_modrm_0(env, s, modrm);
        TCGv ea = gen_lea_modrm_1(s, a, false);
        TCGv last_addr = tcg_temp_new();
        bool update_fdp = true;

        tcg_gen_mov_tl(last_addr, ea);
        gen_lea_v_seg(s, ea, a.def_seg, s->override);

        switch (op) {
        case 0x00 ... 0x07: /* fxxxs */
        case 0x10 ... 0x17: /* fixxxl */
        case 0x20 ... 0x27: /* fxxxl */
        case 0x30 ... 0x37: /* fixxx */
            {
                int op1;
                op1 = op & 7;

                switch (op >> 4) {
                case 0:
                    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    gen_helper_flds_FT0(tcg_env, s->tmp2_i32);
                    break;
                case 1:
                    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    gen_helper_fildl_FT0(tcg_env, s->tmp2_i32);
                    break;
                case 2:
                    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0,
                                        s->mem_index, MO_LEUQ);
                    gen_helper_fldl_FT0(tcg_env, s->tmp1_i64);
                    break;
                case 3:
                default:
                    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LESW);
                    gen_helper_fildl_FT0(tcg_env, s->tmp2_i32);
                    break;
                }

                gen_helper_fp_arith_ST0_FT0(op1);
                if (op1 == 3) {
                    /* fcomp needs pop */
                    gen_helper_fpop(tcg_env);
                }
            }
            break;
        case 0x08: /* flds */
        case 0x0a: /* fsts */
        case 0x0b: /* fstps */
        case 0x18 ... 0x1b: /* fildl, fisttpl, fistl, fistpl */
        case 0x28 ... 0x2b: /* fldl, fisttpll, fstl, fstpl */
        case 0x38 ... 0x3b: /* filds, fisttps, fists, fistps */
            switch (op & 7) {
            case 0:
                switch (op >> 4) {
                case 0:
                    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    gen_helper_flds_ST0(tcg_env, s->tmp2_i32);
                    break;
                case 1:
                    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    gen_helper_fildl_ST0(tcg_env, s->tmp2_i32);
                    break;
                case 2:
                    tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0,
                                        s->mem_index, MO_LEUQ);
                    gen_helper_fldl_ST0(tcg_env, s->tmp1_i64);
                    break;
                case 3:
                default:
                    tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LESW);
                    gen_helper_fildl_ST0(tcg_env, s->tmp2_i32);
                    break;
                }
                break;
            case 1:
                /* XXX: the corresponding CPUID bit must be tested ! */
                switch (op >> 4) {
                case 1:
                    gen_helper_fisttl_ST0(s->tmp2_i32, tcg_env);
                    tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    break;
                case 2:
                    gen_helper_fisttll_ST0(s->tmp1_i64, tcg_env);
                    tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0,
                                        s->mem_index, MO_LEUQ);
                    break;
                case 3:
                default:
                    gen_helper_fistt_ST0(s->tmp2_i32, tcg_env);
                    tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUW);
                    break;
                }
                gen_helper_fpop(tcg_env);
                break;
            default:
                switch (op >> 4) {
                case 0:
                    gen_helper_fsts_ST0(s->tmp2_i32, tcg_env);
                    tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    break;
                case 1:
                    gen_helper_fistl_ST0(s->tmp2_i32, tcg_env);
                    tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUL);
                    break;
                case 2:
                    gen_helper_fstl_ST0(s->tmp1_i64, tcg_env);
                    tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0,
                                        s->mem_index, MO_LEUQ);
                    break;
                case 3:
                default:
                    gen_helper_fist_ST0(s->tmp2_i32, tcg_env);
                    tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                        s->mem_index, MO_LEUW);
                    break;
                }
                if ((op & 7) == 3) {
                    gen_helper_fpop(tcg_env);
                }
                break;
            }
            break;
        case 0x0c: /* fldenv mem */
            gen_helper_fldenv(tcg_env, s->A0,
                              tcg_constant_i32(s->dflag - 1));
            update_fip = update_fdp = false;
            break;
        case 0x0d: /* fldcw mem */
            tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0,
                                s->mem_index, MO_LEUW);
            gen_helper_fldcw(tcg_env, s->tmp2_i32);
            update_fip = update_fdp = false;
            break;
        case 0x0e: /* fnstenv mem */
            gen_helper_fstenv(tcg_env, s->A0,
                              tcg_constant_i32(s->dflag - 1));
            update_fip = update_fdp = false;
            break;
        case 0x0f: /* fnstcw mem */
            gen_helper_fnstcw(s->tmp2_i32, tcg_env);
            tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                s->mem_index, MO_LEUW);
            update_fip = update_fdp = false;
            break;
        case 0x1d: /* fldt mem */
            gen_helper_fldt_ST0(tcg_env, s->A0);
            break;
        case 0x1f: /* fstpt mem */
            gen_helper_fstt_ST0(tcg_env, s->A0);
            gen_helper_fpop(tcg_env);
            break;
        case 0x2c: /* frstor mem */
            gen_helper_frstor(tcg_env, s->A0,
                              tcg_constant_i32(s->dflag - 1));
            update_fip = update_fdp = false;
            break;
        case 0x2e: /* fnsave mem */
            gen_helper_fsave(tcg_env, s->A0,
                             tcg_constant_i32(s->dflag - 1));
            update_fip = update_fdp = false;
            break;
        case 0x2f: /* fnstsw mem */
            gen_helper_fnstsw(s->tmp2_i32, tcg_env);
            tcg_gen_qemu_st_i32(s->tmp2_i32, s->A0,
                                s->mem_index, MO_LEUW);
            update_fip = update_fdp = false;
            break;
        case 0x3c: /* fbld */
            gen_helper_fbld_ST0(tcg_env, s->A0);
            break;
        case 0x3e: /* fbstp */
            gen_helper_fbst_ST0(tcg_env, s->A0);
            gen_helper_fpop(tcg_env);
            break;
        case 0x3d: /* fildll */
            tcg_gen_qemu_ld_i64(s->tmp1_i64, s->A0,
                                s->mem_index, MO_LEUQ);
            gen_helper_fildll_ST0(tcg_env, s->tmp1_i64);
            break;
        case 0x3f: /* fistpll */
            gen_helper_fistll_ST0(s->tmp1_i64, tcg_env);
            tcg_gen_qemu_st_i64(s->tmp1_i64, s->A0,
                                s->mem_index, MO_LEUQ);
            gen_helper_fpop(tcg_env);
            break;
        default:
            return false;
        }

        if (update_fdp) {
            int last_seg = s->override >= 0 ? s->override : a.def_seg;

            tcg_gen_ld_i32(s->tmp2_i32, tcg_env,
                           offsetof(CPUX86State,
                                    segs[last_seg].selector));
            tcg_gen_st16_i32(s->tmp2_i32, tcg_env,
                             offsetof(CPUX86State, fpds));
            tcg_gen_st_tl(last_addr, tcg_env,
                          offsetof(CPUX86State, fpdp));
        }
    } else {
        /* register float ops */
        int opreg = rm;

        switch (op) {
        case 0x08: /* fld sti */
            gen_helper_fpush(tcg_env);
            gen_helper_fmov_ST0_STN(tcg_env,
                                    tcg_constant_i32((opreg + 1) & 7));
            break;
        case 0x09: /* fxchg sti */
        case 0x29: /* fxchg4 sti, undocumented op */
        case 0x39: /* fxchg7 sti, undocumented op */
            gen_helper_fxchg_ST0_STN(tcg_env, tcg_constant_i32(opreg));
            break;
        case 0x0a: /* grp d9/2 */
            switch (rm) {
            case 0: /* fnop */
                /*
                 * check exceptions (FreeBSD FPU probe)
                 * needs to be treated as I/O because of ferr_irq
                 */
                translator_io_start(&s->base);
                gen_helper_fwait(tcg_env);
                update_fip = false;
                break;
            default:
                return false;
            }
            break;
        case 0x0c: /* grp d9/4 */
            switch (rm) {
            case 0: /* fchs */
                gen_helper_fchs_ST0(tcg_env);
                break;
            case 1: /* fabs */
                gen_helper_fabs_ST0(tcg_env);
                break;
            case 4: /* ftst */
                gen_helper_fldz_FT0(tcg_env);
                gen_helper_fcom_ST0_FT0(tcg_env);
                break;
            case 5: /* fxam */
                gen_helper_fxam_ST0(tcg_env);
                break;
            default:
                return false;
            }
            break;
        case 0x0d: /* grp d9/5 */
            {
                switch (rm) {
                case 0:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fld1_ST0(tcg_env);
                    break;
                case 1:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fldl2t_ST0(tcg_env);
                    break;
                case 2:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fldl2e_ST0(tcg_env);
                    break;
                case 3:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fldpi_ST0(tcg_env);
                    break;
                case 4:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fldlg2_ST0(tcg_env);
                    break;
                case 5:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fldln2_ST0(tcg_env);
                    break;
                case 6:
                    gen_helper_fpush(tcg_env);
                    gen_helper_fldz_ST0(tcg_env);
                    break;
                default:
                    return false;
                }
            }
            break;
        case 0x0e: /* grp d9/6 */
            switch (rm) {
            case 0: /* f2xm1 */
                gen_helper_f2xm1(tcg_env);
                break;
            case 1: /* fyl2x */
                gen_helper_fyl2x(tcg_env);
                break;
            case 2: /* fptan */
                gen_helper_fptan(tcg_env);
                break;
            case 3: /* fpatan */
                gen_helper_fpatan(tcg_env);
                break;
            case 4: /* fxtract */
                gen_helper_fxtract(tcg_env);
                break;
            case 5: /* fprem1 */
                gen_helper_fprem1(tcg_env);
                break;
            case 6: /* fdecstp */
                gen_helper_fdecstp(tcg_env);
                break;
            default:
            case 7: /* fincstp */
                gen_helper_fincstp(tcg_env);
                break;
            }
            break;
        case 0x0f: /* grp d9/7 */
            switch (rm) {
            case 0: /* fprem */
                gen_helper_fprem(tcg_env);
                break;
            case 1: /* fyl2xp1 */
                gen_helper_fyl2xp1(tcg_env);
                break;
            case 2: /* fsqrt */
                gen_helper_fsqrt(tcg_env);
                break;
            case 3: /* fsincos */
                gen_helper_fsincos(tcg_env);
                break;
            case 5: /* fscale */
                gen_helper_fscale(tcg_env);
                break;
            case 4: /* frndint */
                gen_helper_frndint(tcg_env);
                break;
            case 6: /* fsin */
                gen_helper_fsin(tcg_env);
                break;
            default:
            case 7: /* fcos */
                gen_helper_fcos(tcg_env);
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
                    gen_helper_fp_arith_STN_ST0(op1, opreg);
                    if (op >= 0x30) {
                        gen_helper_fpop(tcg_env);
                    }
                } else {
                    gen_helper_fmov_FT0_STN(tcg_env,
                                            tcg_constant_i32(opreg));
                    gen_helper_fp_arith_ST0_FT0(op1);
                }
            }
            break;
        case 0x02: /* fcom */
        case 0x22: /* fcom2, undocumented op */
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fcom_ST0_FT0(tcg_env);
            break;
        case 0x03: /* fcomp */
        case 0x23: /* fcomp3, undocumented op */
        case 0x32: /* fcomp5, undocumented op */
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fcom_ST0_FT0(tcg_env);
            gen_helper_fpop(tcg_env);
            break;
        case 0x15: /* da/5 */
            switch (rm) {
            case 1: /* fucompp */
                gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(1));
                gen_helper_fucom_ST0_FT0(tcg_env);
                gen_helper_fpop(tcg_env);
                gen_helper_fpop(tcg_env);
                break;
            default:
                return false;
            }
            break;
        case 0x1c:
            switch (rm) {
            case 0: /* feni (287 only, just do nop here) */
                break;
            case 1: /* fdisi (287 only, just do nop here) */
                break;
            case 2: /* fclex */
                gen_helper_fclex(tcg_env);
                update_fip = false;
                break;
            case 3: /* fninit */
                gen_helper_fninit(tcg_env);
                update_fip = false;
                break;
            case 4: /* fsetpm (287 only, just do nop here) */
                break;
            default:
                return false;
            }
            break;
        case 0x1d: /* fucomi */
            if (!(s->cpuid_features & CPUID_CMOV)) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fucomi_ST0_FT0(tcg_env);
            assume_cc_op(s, CC_OP_EFLAGS);
            break;
        case 0x1e: /* fcomi */
            if (!(s->cpuid_features & CPUID_CMOV)) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fcomi_ST0_FT0(tcg_env);
            assume_cc_op(s, CC_OP_EFLAGS);
            break;
        case 0x28: /* ffree sti */
            gen_helper_ffree_STN(tcg_env, tcg_constant_i32(opreg));
            break;
        case 0x2a: /* fst sti */
            gen_helper_fmov_STN_ST0(tcg_env, tcg_constant_i32(opreg));
            break;
        case 0x2b: /* fstp sti */
        case 0x0b: /* fstp1 sti, undocumented op */
        case 0x3a: /* fstp8 sti, undocumented op */
        case 0x3b: /* fstp9 sti, undocumented op */
            gen_helper_fmov_STN_ST0(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fpop(tcg_env);
            break;
        case 0x2c: /* fucom st(i) */
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fucom_ST0_FT0(tcg_env);
            break;
        case 0x2d: /* fucomp st(i) */
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fucom_ST0_FT0(tcg_env);
            gen_helper_fpop(tcg_env);
            break;
        case 0x33: /* de/3 */
            switch (rm) {
            case 1: /* fcompp */
                gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(1));
                gen_helper_fcom_ST0_FT0(tcg_env);
                gen_helper_fpop(tcg_env);
                gen_helper_fpop(tcg_env);
                break;
            default:
                return false;
            }
            break;
        case 0x38: /* ffreep sti, undocumented op */
            gen_helper_ffree_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fpop(tcg_env);
            break;
        case 0x3c: /* df/4 */
            switch (rm) {
            case 0:
                gen_helper_fnstsw(s->tmp2_i32, tcg_env);
                tcg_gen_extu_i32_tl(s->T0, s->tmp2_i32);
                gen_op_mov_reg_v(s, MO_16, R_EAX, s->T0);
                break;
            default:
                return false;
            }
            break;
        case 0x3d: /* fucomip */
            if (!(s->cpuid_features & CPUID_CMOV)) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fucomi_ST0_FT0(tcg_env);
            gen_helper_fpop(tcg_env);
            assume_cc_op(s, CC_OP_EFLAGS);
            break;
        case 0x3e: /* fcomip */
            if (!(s->cpuid_features & CPUID_CMOV)) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_helper_fmov_FT0_STN(tcg_env, tcg_constant_i32(opreg));
            gen_helper_fcomi_ST0_FT0(tcg_env);
            gen_helper_fpop(tcg_env);
            assume_cc_op(s, CC_OP_EFLAGS);
            break;
        case 0x10 ... 0x13: /* fcmovxx */
        case 0x18 ... 0x1b:
            {
                int op1;
                TCGLabel *l1;
                static const uint8_t fcmov_cc[8] = {
                    (JCC_B << 1),
                    (JCC_Z << 1),
                    (JCC_BE << 1),
                    (JCC_P << 1),
                };

                if (!(s->cpuid_features & CPUID_CMOV)) {
                    goto illegal_op;
                }
                op1 = fcmov_cc[op & 3] | (((op >> 3) & 1) ^ 1);
                l1 = gen_new_label();
                gen_jcc1_noeob(s, op1, l1);
                gen_helper_fmov_ST0_STN(tcg_env,
                                        tcg_constant_i32(opreg));
                gen_set_label(l1);
            }
            break;
        default:
            return false;
        }
    }

    if (update_fip) {
        tcg_gen_ld_i32(s->tmp2_i32, tcg_env,
                       offsetof(CPUX86State, segs[R_CS].selector));
        tcg_gen_st16_i32(s->tmp2_i32, tcg_env,
                         offsetof(CPUX86State, fpcs));
        tcg_gen_st_tl(eip_cur_tl(s),
                      tcg_env, offsetof(CPUX86State, fpip));
    }
    return true;

 illegal_op:
    gen_illegal_opcode(s);
    return true;
}

static void disas_insn_old(DisasContext *s, CPUState *cpu, int b)
{
    CPUX86State *env = cpu_env(cpu);
    int prefixes = s->prefix;
    MemOp dflag = s->dflag;
    int shift;
    MemOp ot;
    int modrm, reg, rm, mod, op, opreg, val;

    /* now check op code */
    switch (b) {
        /**************************/
        /* arith & logic */
    case 0x1c0:
    case 0x1c1: /* xadd Ev, Gv */
        ot = mo_b_d(b, dflag);
        modrm = x86_ldub_code(env, s);
        reg = ((modrm >> 3) & 7) | REX_R(s);
        mod = (modrm >> 6) & 3;
        gen_op_mov_v_reg(s, ot, s->T0, reg);
        if (mod == 3) {
            rm = (modrm & 7) | REX_B(s);
            gen_op_mov_v_reg(s, ot, s->T1, rm);
            tcg_gen_add_tl(s->T0, s->T0, s->T1);
            gen_op_mov_reg_v(s, ot, reg, s->T1);
            gen_op_mov_reg_v(s, ot, rm, s->T0);
        } else {
            gen_lea_modrm(env, s, modrm);
            if (s->prefix & PREFIX_LOCK) {
                tcg_gen_atomic_fetch_add_tl(s->T1, s->A0, s->T0,
                                            s->mem_index, ot | MO_LE);
                tcg_gen_add_tl(s->T0, s->T0, s->T1);
            } else {
                gen_op_ld_v(s, ot, s->T1, s->A0);
                tcg_gen_add_tl(s->T0, s->T0, s->T1);
                gen_op_st_v(s, ot, s->T0, s->A0);
            }
            gen_op_mov_reg_v(s, ot, reg, s->T1);
        }
        gen_op_update2_cc(s);
        set_cc_op(s, CC_OP_ADDB + ot);
        break;
    case 0x1b0:
    case 0x1b1: /* cmpxchg Ev, Gv */
        {
            TCGv oldv, newv, cmpv, dest;

            ot = mo_b_d(b, dflag);
            modrm = x86_ldub_code(env, s);
            reg = ((modrm >> 3) & 7) | REX_R(s);
            mod = (modrm >> 6) & 3;
            oldv = tcg_temp_new();
            newv = tcg_temp_new();
            cmpv = tcg_temp_new();
            gen_op_mov_v_reg(s, ot, newv, reg);
            tcg_gen_mov_tl(cmpv, cpu_regs[R_EAX]);
            gen_extu(ot, cmpv);
            if (s->prefix & PREFIX_LOCK) {
                if (mod == 3) {
                    goto illegal_op;
                }
                gen_lea_modrm(env, s, modrm);
                tcg_gen_atomic_cmpxchg_tl(oldv, s->A0, cmpv, newv,
                                          s->mem_index, ot | MO_LE);
            } else {
                if (mod == 3) {
                    rm = (modrm & 7) | REX_B(s);
                    gen_op_mov_v_reg(s, ot, oldv, rm);
                    gen_extu(ot, oldv);

                    /*
                     * Unlike the memory case, where "the destination operand receives
                     * a write cycle without regard to the result of the comparison",
                     * rm must not be touched altogether if the write fails, including
                     * not zero-extending it on 64-bit processors.  So, precompute
                     * the result of a successful writeback and perform the movcond
                     * directly on cpu_regs.  Also need to write accumulator first, in
                     * case rm is part of RAX too.
                     */
                    dest = gen_op_deposit_reg_v(s, ot, rm, newv, newv);
                    tcg_gen_movcond_tl(TCG_COND_EQ, dest, oldv, cmpv, newv, dest);
                } else {
                    gen_lea_modrm(env, s, modrm);
                    gen_op_ld_v(s, ot, oldv, s->A0);

                    /*
                     * Perform an unconditional store cycle like physical cpu;
                     * must be before changing accumulator to ensure
                     * idempotency if the store faults and the instruction
                     * is restarted
                     */
                    tcg_gen_movcond_tl(TCG_COND_EQ, newv, oldv, cmpv, newv, oldv);
                    gen_op_st_v(s, ot, newv, s->A0);
                }
            }
	    /*
	     * Write EAX only if the cmpxchg fails; reuse newv as the destination,
	     * since it's dead here.
	     */
            dest = gen_op_deposit_reg_v(s, ot, R_EAX, newv, oldv);
            tcg_gen_movcond_tl(TCG_COND_EQ, dest, oldv, cmpv, dest, newv);
            tcg_gen_mov_tl(cpu_cc_src, oldv);
            tcg_gen_mov_tl(s->cc_srcT, cmpv);
            tcg_gen_sub_tl(cpu_cc_dst, cmpv, oldv);
            set_cc_op(s, CC_OP_SUBB + ot);
        }
        break;
    case 0x1c7: /* cmpxchg8b */
        modrm = x86_ldub_code(env, s);
        mod = (modrm >> 6) & 3;
        switch ((modrm >> 3) & 7) {
        case 1: /* CMPXCHG8, CMPXCHG16 */
            if (mod == 3) {
                goto illegal_op;
            }
#ifdef TARGET_X86_64
            if (dflag == MO_64) {
                if (!(s->cpuid_ext_features & CPUID_EXT_CX16)) {
                    goto illegal_op;
                }
                gen_cmpxchg16b(s, env, modrm);
                break;
            }
#endif
            if (!(s->cpuid_features & CPUID_CX8)) {
                goto illegal_op;
            }
            gen_cmpxchg8b(s, env, modrm);
            break;

        case 7: /* RDSEED, RDPID with f3 prefix */
            if (mod != 3 ||
                (s->prefix & (PREFIX_LOCK | PREFIX_REPNZ))) {
                goto illegal_op;
            }
            if (s->prefix & PREFIX_REPZ) {
                if (!(s->cpuid_7_0_ecx_features & CPUID_7_0_ECX_RDPID)) {
                    goto illegal_op;
                }
                gen_helper_rdpid(s->T0, tcg_env);
                rm = (modrm & 7) | REX_B(s);
                gen_op_mov_reg_v(s, dflag, rm, s->T0);
                break;
            } else {
                if (!(s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_RDSEED)) {
                    goto illegal_op;
                }
                goto do_rdrand;
            }

        case 6: /* RDRAND */
            if (mod != 3 ||
                (s->prefix & (PREFIX_LOCK | PREFIX_REPZ | PREFIX_REPNZ)) ||
                !(s->cpuid_ext_features & CPUID_EXT_RDRAND)) {
                goto illegal_op;
            }
        do_rdrand:
            translator_io_start(&s->base);
            gen_helper_rdrand(s->T0, tcg_env);
            rm = (modrm & 7) | REX_B(s);
            gen_op_mov_reg_v(s, dflag, rm, s->T0);
            assume_cc_op(s, CC_OP_EFLAGS);
            break;

        default:
            goto illegal_op;
        }
        break;

        /**************************/
        /* shifts */
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
        ot = dflag;
        modrm = x86_ldub_code(env, s);
        mod = (modrm >> 6) & 3;
        rm = (modrm & 7) | REX_B(s);
        reg = ((modrm >> 3) & 7) | REX_R(s);
        if (mod != 3) {
            gen_lea_modrm(env, s, modrm);
            opreg = OR_TMP0;
        } else {
            opreg = rm;
        }
        gen_op_mov_v_reg(s, ot, s->T1, reg);

        if (shift) {
            TCGv imm = tcg_constant_tl(x86_ldub_code(env, s));
            gen_shiftd_rm_T1(s, ot, opreg, op, imm);
        } else {
            gen_shiftd_rm_T1(s, ot, opreg, op, cpu_regs[R_ECX]);
        }
        break;

        /************************/
        /* bit operations */
    case 0x1ba: /* bt/bts/btr/btc Gv, im */
        ot = dflag;
        modrm = x86_ldub_code(env, s);
        op = (modrm >> 3) & 7;
        mod = (modrm >> 6) & 3;
        rm = (modrm & 7) | REX_B(s);
        if (mod != 3) {
            s->rip_offset = 1;
            gen_lea_modrm(env, s, modrm);
            if (!(s->prefix & PREFIX_LOCK)) {
                gen_op_ld_v(s, ot, s->T0, s->A0);
            }
        } else {
            gen_op_mov_v_reg(s, ot, s->T0, rm);
        }
        /* load shift */
        val = x86_ldub_code(env, s);
        tcg_gen_movi_tl(s->T1, val);
        if (op < 4)
            goto unknown_op;
        op -= 4;
        goto bt_op;
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
        ot = dflag;
        modrm = x86_ldub_code(env, s);
        reg = ((modrm >> 3) & 7) | REX_R(s);
        mod = (modrm >> 6) & 3;
        rm = (modrm & 7) | REX_B(s);
        gen_op_mov_v_reg(s, MO_32, s->T1, reg);
        if (mod != 3) {
            AddressParts a = gen_lea_modrm_0(env, s, modrm);
            /* specific case: we need to add a displacement */
            gen_exts(ot, s->T1);
            tcg_gen_sari_tl(s->tmp0, s->T1, 3 + ot);
            tcg_gen_shli_tl(s->tmp0, s->tmp0, ot);
            tcg_gen_add_tl(s->A0, gen_lea_modrm_1(s, a, false), s->tmp0);
            gen_lea_v_seg(s, s->A0, a.def_seg, s->override);
            if (!(s->prefix & PREFIX_LOCK)) {
                gen_op_ld_v(s, ot, s->T0, s->A0);
            }
        } else {
            gen_op_mov_v_reg(s, ot, s->T0, rm);
        }
    bt_op:
        tcg_gen_andi_tl(s->T1, s->T1, (1 << (3 + ot)) - 1);
        tcg_gen_movi_tl(s->tmp0, 1);
        tcg_gen_shl_tl(s->tmp0, s->tmp0, s->T1);
        if (s->prefix & PREFIX_LOCK) {
            switch (op) {
            case 0: /* bt */
                /* Needs no atomic ops; we suppressed the normal
                   memory load for LOCK above so do it now.  */
                gen_op_ld_v(s, ot, s->T0, s->A0);
                break;
            case 1: /* bts */
                tcg_gen_atomic_fetch_or_tl(s->T0, s->A0, s->tmp0,
                                           s->mem_index, ot | MO_LE);
                break;
            case 2: /* btr */
                tcg_gen_not_tl(s->tmp0, s->tmp0);
                tcg_gen_atomic_fetch_and_tl(s->T0, s->A0, s->tmp0,
                                            s->mem_index, ot | MO_LE);
                break;
            default:
            case 3: /* btc */
                tcg_gen_atomic_fetch_xor_tl(s->T0, s->A0, s->tmp0,
                                            s->mem_index, ot | MO_LE);
                break;
            }
            tcg_gen_shr_tl(s->tmp4, s->T0, s->T1);
        } else {
            tcg_gen_shr_tl(s->tmp4, s->T0, s->T1);
            switch (op) {
            case 0: /* bt */
                /* Data already loaded; nothing to do.  */
                break;
            case 1: /* bts */
                tcg_gen_or_tl(s->T0, s->T0, s->tmp0);
                break;
            case 2: /* btr */
                tcg_gen_andc_tl(s->T0, s->T0, s->tmp0);
                break;
            default:
            case 3: /* btc */
                tcg_gen_xor_tl(s->T0, s->T0, s->tmp0);
                break;
            }
            if (op != 0) {
                if (mod != 3) {
                    gen_op_st_v(s, ot, s->T0, s->A0);
                } else {
                    gen_op_mov_reg_v(s, ot, rm, s->T0);
                }
            }
        }

        /* Delay all CC updates until after the store above.  Note that
           C is the result of the test, Z is unchanged, and the others
           are all undefined.  */
        switch (s->cc_op) {
        case CC_OP_MULB ... CC_OP_MULQ:
        case CC_OP_ADDB ... CC_OP_ADDQ:
        case CC_OP_ADCB ... CC_OP_ADCQ:
        case CC_OP_SUBB ... CC_OP_SUBQ:
        case CC_OP_SBBB ... CC_OP_SBBQ:
        case CC_OP_LOGICB ... CC_OP_LOGICQ:
        case CC_OP_INCB ... CC_OP_INCQ:
        case CC_OP_DECB ... CC_OP_DECQ:
        case CC_OP_SHLB ... CC_OP_SHLQ:
        case CC_OP_SARB ... CC_OP_SARQ:
        case CC_OP_BMILGB ... CC_OP_BMILGQ:
            /* Z was going to be computed from the non-zero status of CC_DST.
               We can get that same Z value (and the new C value) by leaving
               CC_DST alone, setting CC_SRC, and using a CC_OP_SAR of the
               same width.  */
            tcg_gen_mov_tl(cpu_cc_src, s->tmp4);
            set_cc_op(s, ((s->cc_op - CC_OP_MULB) & 3) + CC_OP_SARB);
            break;
        default:
            /* Otherwise, generate EFLAGS and replace the C bit.  */
            gen_compute_eflags(s);
            tcg_gen_deposit_tl(cpu_cc_src, cpu_cc_src, s->tmp4,
                               ctz32(CC_C), 1);
            break;
        }
        break;
    case 0x1bc: /* bsf / tzcnt */
    case 0x1bd: /* bsr / lzcnt */
        ot = dflag;
        modrm = x86_ldub_code(env, s);
        reg = ((modrm >> 3) & 7) | REX_R(s);
        gen_ld_modrm(env, s, modrm, ot);
        gen_extu(ot, s->T0);

        /* Note that lzcnt and tzcnt are in different extensions.  */
        if ((prefixes & PREFIX_REPZ)
            && (b & 1
                ? s->cpuid_ext3_features & CPUID_EXT3_ABM
                : s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_BMI1)) {
            int size = 8 << ot;
            /* For lzcnt/tzcnt, C bit is defined related to the input. */
            tcg_gen_mov_tl(cpu_cc_src, s->T0);
            if (b & 1) {
                /* For lzcnt, reduce the target_ulong result by the
                   number of zeros that we expect to find at the top.  */
                tcg_gen_clzi_tl(s->T0, s->T0, TARGET_LONG_BITS);
                tcg_gen_subi_tl(s->T0, s->T0, TARGET_LONG_BITS - size);
            } else {
                /* For tzcnt, a zero input must return the operand size.  */
                tcg_gen_ctzi_tl(s->T0, s->T0, size);
            }
            /* For lzcnt/tzcnt, Z bit is defined related to the result.  */
            gen_op_update1_cc(s);
            set_cc_op(s, CC_OP_BMILGB + ot);
        } else {
            /* For bsr/bsf, only the Z bit is defined and it is related
               to the input and not the result.  */
            tcg_gen_mov_tl(cpu_cc_dst, s->T0);
            set_cc_op(s, CC_OP_LOGICB + ot);

            /* ??? The manual says that the output is undefined when the
               input is zero, but real hardware leaves it unchanged, and
               real programs appear to depend on that.  Accomplish this
               by passing the output as the value to return upon zero.  */
            if (b & 1) {
                /* For bsr, return the bit index of the first 1 bit,
                   not the count of leading zeros.  */
                tcg_gen_xori_tl(s->T1, cpu_regs[reg], TARGET_LONG_BITS - 1);
                tcg_gen_clz_tl(s->T0, s->T0, s->T1);
                tcg_gen_xori_tl(s->T0, s->T0, TARGET_LONG_BITS - 1);
            } else {
                tcg_gen_ctz_tl(s->T0, s->T0, cpu_regs[reg]);
            }
        }
        gen_op_mov_reg_v(s, ot, reg, s->T0);
        break;
    case 0x130: /* wrmsr */
    case 0x132: /* rdmsr */
        if (check_cpl0(s)) {
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            if (b & 2) {
                gen_helper_rdmsr(tcg_env);
            } else {
                gen_helper_wrmsr(tcg_env);
                s->base.is_jmp = DISAS_EOB_NEXT;
            }
        }
        break;
    case 0x131: /* rdtsc */
        gen_update_cc_op(s);
        gen_update_eip_cur(s);
        translator_io_start(&s->base);
        gen_helper_rdtsc(tcg_env);
        break;
    case 0x133: /* rdpmc */
        gen_update_cc_op(s);
        gen_update_eip_cur(s);
        gen_helper_rdpmc(tcg_env);
        s->base.is_jmp = DISAS_NORETURN;
        break;
    case 0x134: /* sysenter */
        /* For AMD SYSENTER is not valid in long mode */
        if (LMA(s) && env->cpuid_vendor1 != CPUID_VENDOR_INTEL_1) {
            goto illegal_op;
        }
        if (!PE(s)) {
            gen_exception_gpf(s);
        } else {
            gen_helper_sysenter(tcg_env);
            s->base.is_jmp = DISAS_EOB_ONLY;
        }
        break;
    case 0x135: /* sysexit */
        /* For AMD SYSEXIT is not valid in long mode */
        if (LMA(s) && env->cpuid_vendor1 != CPUID_VENDOR_INTEL_1) {
            goto illegal_op;
        }
        if (!PE(s) || CPL(s) != 0) {
            gen_exception_gpf(s);
        } else {
            gen_helper_sysexit(tcg_env, tcg_constant_i32(dflag - 1));
            s->base.is_jmp = DISAS_EOB_ONLY;
        }
        break;
    case 0x105: /* syscall */
        /* For Intel SYSCALL is only valid in long mode */
        if (!LMA(s) && env->cpuid_vendor1 == CPUID_VENDOR_INTEL_1) {
            goto illegal_op;
        }
        gen_update_cc_op(s);
        gen_update_eip_cur(s);
        gen_helper_syscall(tcg_env, cur_insn_len_i32(s));
        /* condition codes are modified only in long mode */
        if (LMA(s)) {
            assume_cc_op(s, CC_OP_EFLAGS);
        }
        /* TF handling for the syscall insn is different. The TF bit is  checked
           after the syscall insn completes. This allows #DB to not be
           generated after one has entered CPL0 if TF is set in FMASK.  */
        s->base.is_jmp = DISAS_EOB_RECHECK_TF;
        break;
    case 0x107: /* sysret */
        /* For Intel SYSRET is only valid in long mode */
        if (!LMA(s) && env->cpuid_vendor1 == CPUID_VENDOR_INTEL_1) {
            goto illegal_op;
        }
        if (!PE(s) || CPL(s) != 0) {
            gen_exception_gpf(s);
        } else {
            gen_helper_sysret(tcg_env, tcg_constant_i32(dflag - 1));
            /* condition codes are modified only in long mode */
            if (LMA(s)) {
                assume_cc_op(s, CC_OP_EFLAGS);
            }
            /* TF handling for the sysret insn is different. The TF bit is
               checked after the sysret insn completes. This allows #DB to be
               generated "as if" the syscall insn in userspace has just
               completed.  */
            s->base.is_jmp = DISAS_EOB_RECHECK_TF;
        }
        break;
    case 0x1a2: /* cpuid */
        gen_update_cc_op(s);
        gen_update_eip_cur(s);
        gen_helper_cpuid(tcg_env);
        break;
    case 0x100:
        modrm = x86_ldub_code(env, s);
        mod = (modrm >> 6) & 3;
        op = (modrm >> 3) & 7;
        switch(op) {
        case 0: /* sldt */
            if (!PE(s) || VM86(s))
                goto illegal_op;
            if (s->flags & HF_UMIP_MASK && !check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_LDTR_READ);
            tcg_gen_ld32u_tl(s->T0, tcg_env,
                             offsetof(CPUX86State, ldt.selector));
            ot = mod == 3 ? dflag : MO_16;
            gen_st_modrm(env, s, modrm, ot);
            break;
        case 2: /* lldt */
            if (!PE(s) || VM86(s))
                goto illegal_op;
            if (check_cpl0(s)) {
                gen_svm_check_intercept(s, SVM_EXIT_LDTR_WRITE);
                gen_ld_modrm(env, s, modrm, MO_16);
                tcg_gen_trunc_tl_i32(s->tmp2_i32, s->T0);
                gen_helper_lldt(tcg_env, s->tmp2_i32);
            }
            break;
        case 1: /* str */
            if (!PE(s) || VM86(s))
                goto illegal_op;
            if (s->flags & HF_UMIP_MASK && !check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_TR_READ);
            tcg_gen_ld32u_tl(s->T0, tcg_env,
                             offsetof(CPUX86State, tr.selector));
            ot = mod == 3 ? dflag : MO_16;
            gen_st_modrm(env, s, modrm, ot);
            break;
        case 3: /* ltr */
            if (!PE(s) || VM86(s))
                goto illegal_op;
            if (check_cpl0(s)) {
                gen_svm_check_intercept(s, SVM_EXIT_TR_WRITE);
                gen_ld_modrm(env, s, modrm, MO_16);
                tcg_gen_trunc_tl_i32(s->tmp2_i32, s->T0);
                gen_helper_ltr(tcg_env, s->tmp2_i32);
            }
            break;
        case 4: /* verr */
        case 5: /* verw */
            if (!PE(s) || VM86(s))
                goto illegal_op;
            gen_ld_modrm(env, s, modrm, MO_16);
            gen_update_cc_op(s);
            if (op == 4) {
                gen_helper_verr(tcg_env, s->T0);
            } else {
                gen_helper_verw(tcg_env, s->T0);
            }
            assume_cc_op(s, CC_OP_EFLAGS);
            break;
        default:
            goto unknown_op;
        }
        break;

    case 0x101:
        modrm = x86_ldub_code(env, s);
        switch (modrm) {
        CASE_MODRM_MEM_OP(0): /* sgdt */
            if (s->flags & HF_UMIP_MASK && !check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_GDTR_READ);
            gen_lea_modrm(env, s, modrm);
            tcg_gen_ld32u_tl(s->T0,
                             tcg_env, offsetof(CPUX86State, gdt.limit));
            gen_op_st_v(s, MO_16, s->T0, s->A0);
            gen_add_A0_im(s, 2);
            tcg_gen_ld_tl(s->T0, tcg_env, offsetof(CPUX86State, gdt.base));
            /*
             * NB: Despite a confusing description in Intel CPU documentation,
             *     all 32-bits are written regardless of operand size.
             */
            gen_op_st_v(s, CODE64(s) + MO_32, s->T0, s->A0);
            break;

        case 0xc8: /* monitor */
            if (!(s->cpuid_ext_features & CPUID_EXT_MONITOR) || CPL(s) != 0) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_lea_v_seg(s, cpu_regs[R_EAX], R_DS, s->override);
            gen_helper_monitor(tcg_env, s->A0);
            break;

        case 0xc9: /* mwait */
            if (!(s->cpuid_ext_features & CPUID_EXT_MONITOR) || CPL(s) != 0) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_helper_mwait(tcg_env, cur_insn_len_i32(s));
            s->base.is_jmp = DISAS_NORETURN;
            break;

        case 0xca: /* clac */
            if (!(s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_SMAP)
                || CPL(s) != 0) {
                goto illegal_op;
            }
            gen_reset_eflags(s, AC_MASK);
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        case 0xcb: /* stac */
            if (!(s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_SMAP)
                || CPL(s) != 0) {
                goto illegal_op;
            }
            gen_set_eflags(s, AC_MASK);
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        CASE_MODRM_MEM_OP(1): /* sidt */
            if (s->flags & HF_UMIP_MASK && !check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_IDTR_READ);
            gen_lea_modrm(env, s, modrm);
            tcg_gen_ld32u_tl(s->T0, tcg_env, offsetof(CPUX86State, idt.limit));
            gen_op_st_v(s, MO_16, s->T0, s->A0);
            gen_add_A0_im(s, 2);
            tcg_gen_ld_tl(s->T0, tcg_env, offsetof(CPUX86State, idt.base));
            /*
             * NB: Despite a confusing description in Intel CPU documentation,
             *     all 32-bits are written regardless of operand size.
             */
            gen_op_st_v(s, CODE64(s) + MO_32, s->T0, s->A0);
            break;

        case 0xd0: /* xgetbv */
            if ((s->cpuid_ext_features & CPUID_EXT_XSAVE) == 0
                || (s->prefix & (PREFIX_LOCK | PREFIX_DATA
                                 | PREFIX_REPZ | PREFIX_REPNZ))) {
                goto illegal_op;
            }
            tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_ECX]);
            gen_helper_xgetbv(s->tmp1_i64, tcg_env, s->tmp2_i32);
            tcg_gen_extr_i64_tl(cpu_regs[R_EAX], cpu_regs[R_EDX], s->tmp1_i64);
            break;

        case 0xd1: /* xsetbv */
            if ((s->cpuid_ext_features & CPUID_EXT_XSAVE) == 0
                || (s->prefix & (PREFIX_LOCK | PREFIX_DATA
                                 | PREFIX_REPZ | PREFIX_REPNZ))) {
                goto illegal_op;
            }
            gen_svm_check_intercept(s, SVM_EXIT_XSETBV);
            if (!check_cpl0(s)) {
                break;
            }
            tcg_gen_concat_tl_i64(s->tmp1_i64, cpu_regs[R_EAX],
                                  cpu_regs[R_EDX]);
            tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_ECX]);
            gen_helper_xsetbv(tcg_env, s->tmp2_i32, s->tmp1_i64);
            /* End TB because translation flags may change.  */
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        case 0xd8: /* VMRUN */
            if (!SVME(s) || !PE(s)) {
                goto illegal_op;
            }
            if (!check_cpl0(s)) {
                break;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_helper_vmrun(tcg_env, tcg_constant_i32(s->aflag - 1),
                             cur_insn_len_i32(s));
            tcg_gen_exit_tb(NULL, 0);
            s->base.is_jmp = DISAS_NORETURN;
            break;

        case 0xd9: /* VMMCALL */
            if (!SVME(s)) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_helper_vmmcall(tcg_env);
            break;

        case 0xda: /* VMLOAD */
            if (!SVME(s) || !PE(s)) {
                goto illegal_op;
            }
            if (!check_cpl0(s)) {
                break;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_helper_vmload(tcg_env, tcg_constant_i32(s->aflag - 1));
            break;

        case 0xdb: /* VMSAVE */
            if (!SVME(s) || !PE(s)) {
                goto illegal_op;
            }
            if (!check_cpl0(s)) {
                break;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_helper_vmsave(tcg_env, tcg_constant_i32(s->aflag - 1));
            break;

        case 0xdc: /* STGI */
            if ((!SVME(s) && !(s->cpuid_ext3_features & CPUID_EXT3_SKINIT))
                || !PE(s)) {
                goto illegal_op;
            }
            if (!check_cpl0(s)) {
                break;
            }
            gen_update_cc_op(s);
            gen_helper_stgi(tcg_env);
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        case 0xdd: /* CLGI */
            if (!SVME(s) || !PE(s)) {
                goto illegal_op;
            }
            if (!check_cpl0(s)) {
                break;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            gen_helper_clgi(tcg_env);
            break;

        case 0xde: /* SKINIT */
            if ((!SVME(s) && !(s->cpuid_ext3_features & CPUID_EXT3_SKINIT))
                || !PE(s)) {
                goto illegal_op;
            }
            gen_svm_check_intercept(s, SVM_EXIT_SKINIT);
            /* If not intercepted, not implemented -- raise #UD. */
            goto illegal_op;

        case 0xdf: /* INVLPGA */
            if (!SVME(s) || !PE(s)) {
                goto illegal_op;
            }
            if (!check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_INVLPGA);
            if (s->aflag == MO_64) {
                tcg_gen_mov_tl(s->A0, cpu_regs[R_EAX]);
            } else {
                tcg_gen_ext32u_tl(s->A0, cpu_regs[R_EAX]);
            }
            gen_helper_flush_page(tcg_env, s->A0);
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        CASE_MODRM_MEM_OP(2): /* lgdt */
            if (!check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_GDTR_WRITE);
            gen_lea_modrm(env, s, modrm);
            gen_op_ld_v(s, MO_16, s->T1, s->A0);
            gen_add_A0_im(s, 2);
            gen_op_ld_v(s, CODE64(s) + MO_32, s->T0, s->A0);
            if (dflag == MO_16) {
                tcg_gen_andi_tl(s->T0, s->T0, 0xffffff);
            }
            tcg_gen_st_tl(s->T0, tcg_env, offsetof(CPUX86State, gdt.base));
            tcg_gen_st32_tl(s->T1, tcg_env, offsetof(CPUX86State, gdt.limit));
            break;

        CASE_MODRM_MEM_OP(3): /* lidt */
            if (!check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_IDTR_WRITE);
            gen_lea_modrm(env, s, modrm);
            gen_op_ld_v(s, MO_16, s->T1, s->A0);
            gen_add_A0_im(s, 2);
            gen_op_ld_v(s, CODE64(s) + MO_32, s->T0, s->A0);
            if (dflag == MO_16) {
                tcg_gen_andi_tl(s->T0, s->T0, 0xffffff);
            }
            tcg_gen_st_tl(s->T0, tcg_env, offsetof(CPUX86State, idt.base));
            tcg_gen_st32_tl(s->T1, tcg_env, offsetof(CPUX86State, idt.limit));
            break;

        CASE_MODRM_OP(4): /* smsw */
            if (s->flags & HF_UMIP_MASK && !check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_READ_CR0);
            tcg_gen_ld_tl(s->T0, tcg_env, offsetof(CPUX86State, cr[0]));
            /*
             * In 32-bit mode, the higher 16 bits of the destination
             * register are undefined.  In practice CR0[31:0] is stored
             * just like in 64-bit mode.
             */
            mod = (modrm >> 6) & 3;
            ot = (mod != 3 ? MO_16 : s->dflag);
            gen_st_modrm(env, s, modrm, ot);
            break;
        case 0xee: /* rdpkru */
            if (s->prefix & (PREFIX_LOCK | PREFIX_DATA
                             | PREFIX_REPZ | PREFIX_REPNZ)) {
                goto illegal_op;
            }
            tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_ECX]);
            gen_helper_rdpkru(s->tmp1_i64, tcg_env, s->tmp2_i32);
            tcg_gen_extr_i64_tl(cpu_regs[R_EAX], cpu_regs[R_EDX], s->tmp1_i64);
            break;
        case 0xef: /* wrpkru */
            if (s->prefix & (PREFIX_LOCK | PREFIX_DATA
                             | PREFIX_REPZ | PREFIX_REPNZ)) {
                goto illegal_op;
            }
            tcg_gen_concat_tl_i64(s->tmp1_i64, cpu_regs[R_EAX],
                                  cpu_regs[R_EDX]);
            tcg_gen_trunc_tl_i32(s->tmp2_i32, cpu_regs[R_ECX]);
            gen_helper_wrpkru(tcg_env, s->tmp2_i32, s->tmp1_i64);
            break;

        CASE_MODRM_OP(6): /* lmsw */
            if (!check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_WRITE_CR0);
            gen_ld_modrm(env, s, modrm, MO_16);
            /*
             * Only the 4 lower bits of CR0 are modified.
             * PE cannot be set to zero if already set to one.
             */
            tcg_gen_ld_tl(s->T1, tcg_env, offsetof(CPUX86State, cr[0]));
            tcg_gen_andi_tl(s->T0, s->T0, 0xf);
            tcg_gen_andi_tl(s->T1, s->T1, ~0xe);
            tcg_gen_or_tl(s->T0, s->T0, s->T1);
            gen_helper_write_crN(tcg_env, tcg_constant_i32(0), s->T0);
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        CASE_MODRM_MEM_OP(7): /* invlpg */
            if (!check_cpl0(s)) {
                break;
            }
            gen_svm_check_intercept(s, SVM_EXIT_INVLPG);
            gen_lea_modrm(env, s, modrm);
            gen_helper_flush_page(tcg_env, s->A0);
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        case 0xf8: /* swapgs */
#ifdef TARGET_X86_64
            if (CODE64(s)) {
                if (check_cpl0(s)) {
                    tcg_gen_mov_tl(s->T0, cpu_seg_base[R_GS]);
                    tcg_gen_ld_tl(cpu_seg_base[R_GS], tcg_env,
                                  offsetof(CPUX86State, kernelgsbase));
                    tcg_gen_st_tl(s->T0, tcg_env,
                                  offsetof(CPUX86State, kernelgsbase));
                }
                break;
            }
#endif
            goto illegal_op;

        case 0xf9: /* rdtscp */
            if (!(s->cpuid_ext2_features & CPUID_EXT2_RDTSCP)) {
                goto illegal_op;
            }
            gen_update_cc_op(s);
            gen_update_eip_cur(s);
            translator_io_start(&s->base);
            gen_helper_rdtsc(tcg_env);
            gen_helper_rdpid(s->T0, tcg_env);
            gen_op_mov_reg_v(s, dflag, R_ECX, s->T0);
            break;

        default:
            goto unknown_op;
        }
        break;

    case 0x108: /* invd */
    case 0x109: /* wbinvd; wbnoinvd with REPZ prefix */
        if (check_cpl0(s)) {
            gen_svm_check_intercept(s, (b & 1) ? SVM_EXIT_WBINVD : SVM_EXIT_INVD);
            /* nothing to do */
        }
        break;
    case 0x102: /* lar */
    case 0x103: /* lsl */
        {
            TCGLabel *label1;
            TCGv t0;
            if (!PE(s) || VM86(s))
                goto illegal_op;
            ot = dflag != MO_16 ? MO_32 : MO_16;
            modrm = x86_ldub_code(env, s);
            reg = ((modrm >> 3) & 7) | REX_R(s);
            gen_ld_modrm(env, s, modrm, MO_16);
            t0 = tcg_temp_new();
            gen_update_cc_op(s);
            if (b == 0x102) {
                gen_helper_lar(t0, tcg_env, s->T0);
            } else {
                gen_helper_lsl(t0, tcg_env, s->T0);
            }
            tcg_gen_andi_tl(s->tmp0, cpu_cc_src, CC_Z);
            label1 = gen_new_label();
            tcg_gen_brcondi_tl(TCG_COND_EQ, s->tmp0, 0, label1);
            gen_op_mov_reg_v(s, ot, reg, t0);
            gen_set_label(label1);
            set_cc_op(s, CC_OP_EFLAGS);
        }
        break;
    case 0x11a:
        modrm = x86_ldub_code(env, s);
        if (s->flags & HF_MPX_EN_MASK) {
            mod = (modrm >> 6) & 3;
            reg = ((modrm >> 3) & 7) | REX_R(s);
            if (prefixes & PREFIX_REPZ) {
                /* bndcl */
                if (reg >= 4
                    || (prefixes & PREFIX_LOCK)
                    || s->aflag == MO_16) {
                    goto illegal_op;
                }
                gen_bndck(env, s, modrm, TCG_COND_LTU, cpu_bndl[reg]);
            } else if (prefixes & PREFIX_REPNZ) {
                /* bndcu */
                if (reg >= 4
                    || (prefixes & PREFIX_LOCK)
                    || s->aflag == MO_16) {
                    goto illegal_op;
                }
                TCGv_i64 notu = tcg_temp_new_i64();
                tcg_gen_not_i64(notu, cpu_bndu[reg]);
                gen_bndck(env, s, modrm, TCG_COND_GTU, notu);
            } else if (prefixes & PREFIX_DATA) {
                /* bndmov -- from reg/mem */
                if (reg >= 4 || s->aflag == MO_16) {
                    goto illegal_op;
                }
                if (mod == 3) {
                    int reg2 = (modrm & 7) | REX_B(s);
                    if (reg2 >= 4 || (prefixes & PREFIX_LOCK)) {
                        goto illegal_op;
                    }
                    if (s->flags & HF_MPX_IU_MASK) {
                        tcg_gen_mov_i64(cpu_bndl[reg], cpu_bndl[reg2]);
                        tcg_gen_mov_i64(cpu_bndu[reg], cpu_bndu[reg2]);
                    }
                } else {
                    gen_lea_modrm(env, s, modrm);
                    if (CODE64(s)) {
                        tcg_gen_qemu_ld_i64(cpu_bndl[reg], s->A0,
                                            s->mem_index, MO_LEUQ);
                        tcg_gen_addi_tl(s->A0, s->A0, 8);
                        tcg_gen_qemu_ld_i64(cpu_bndu[reg], s->A0,
                                            s->mem_index, MO_LEUQ);
                    } else {
                        tcg_gen_qemu_ld_i64(cpu_bndl[reg], s->A0,
                                            s->mem_index, MO_LEUL);
                        tcg_gen_addi_tl(s->A0, s->A0, 4);
                        tcg_gen_qemu_ld_i64(cpu_bndu[reg], s->A0,
                                            s->mem_index, MO_LEUL);
                    }
                    /* bnd registers are now in-use */
                    gen_set_hflag(s, HF_MPX_IU_MASK);
                }
            } else if (mod != 3) {
                /* bndldx */
                AddressParts a = gen_lea_modrm_0(env, s, modrm);
                if (reg >= 4
                    || (prefixes & PREFIX_LOCK)
                    || s->aflag == MO_16
                    || a.base < -1) {
                    goto illegal_op;
                }
                if (a.base >= 0) {
                    tcg_gen_addi_tl(s->A0, cpu_regs[a.base], a.disp);
                } else {
                    tcg_gen_movi_tl(s->A0, 0);
                }
                gen_lea_v_seg(s, s->A0, a.def_seg, s->override);
                if (a.index >= 0) {
                    tcg_gen_mov_tl(s->T0, cpu_regs[a.index]);
                } else {
                    tcg_gen_movi_tl(s->T0, 0);
                }
                if (CODE64(s)) {
                    gen_helper_bndldx64(cpu_bndl[reg], tcg_env, s->A0, s->T0);
                    tcg_gen_ld_i64(cpu_bndu[reg], tcg_env,
                                   offsetof(CPUX86State, mmx_t0.MMX_Q(0)));
                } else {
                    gen_helper_bndldx32(cpu_bndu[reg], tcg_env, s->A0, s->T0);
                    tcg_gen_ext32u_i64(cpu_bndl[reg], cpu_bndu[reg]);
                    tcg_gen_shri_i64(cpu_bndu[reg], cpu_bndu[reg], 32);
                }
                gen_set_hflag(s, HF_MPX_IU_MASK);
            }
        }
        gen_nop_modrm(env, s, modrm);
        break;
    case 0x11b:
        modrm = x86_ldub_code(env, s);
        if (s->flags & HF_MPX_EN_MASK) {
            mod = (modrm >> 6) & 3;
            reg = ((modrm >> 3) & 7) | REX_R(s);
            if (mod != 3 && (prefixes & PREFIX_REPZ)) {
                /* bndmk */
                if (reg >= 4
                    || (prefixes & PREFIX_LOCK)
                    || s->aflag == MO_16) {
                    goto illegal_op;
                }
                AddressParts a = gen_lea_modrm_0(env, s, modrm);
                if (a.base >= 0) {
                    tcg_gen_extu_tl_i64(cpu_bndl[reg], cpu_regs[a.base]);
                    if (!CODE64(s)) {
                        tcg_gen_ext32u_i64(cpu_bndl[reg], cpu_bndl[reg]);
                    }
                } else if (a.base == -1) {
                    /* no base register has lower bound of 0 */
                    tcg_gen_movi_i64(cpu_bndl[reg], 0);
                } else {
                    /* rip-relative generates #ud */
                    goto illegal_op;
                }
                tcg_gen_not_tl(s->A0, gen_lea_modrm_1(s, a, false));
                if (!CODE64(s)) {
                    tcg_gen_ext32u_tl(s->A0, s->A0);
                }
                tcg_gen_extu_tl_i64(cpu_bndu[reg], s->A0);
                /* bnd registers are now in-use */
                gen_set_hflag(s, HF_MPX_IU_MASK);
                break;
            } else if (prefixes & PREFIX_REPNZ) {
                /* bndcn */
                if (reg >= 4
                    || (prefixes & PREFIX_LOCK)
                    || s->aflag == MO_16) {
                    goto illegal_op;
                }
                gen_bndck(env, s, modrm, TCG_COND_GTU, cpu_bndu[reg]);
            } else if (prefixes & PREFIX_DATA) {
                /* bndmov -- to reg/mem */
                if (reg >= 4 || s->aflag == MO_16) {
                    goto illegal_op;
                }
                if (mod == 3) {
                    int reg2 = (modrm & 7) | REX_B(s);
                    if (reg2 >= 4 || (prefixes & PREFIX_LOCK)) {
                        goto illegal_op;
                    }
                    if (s->flags & HF_MPX_IU_MASK) {
                        tcg_gen_mov_i64(cpu_bndl[reg2], cpu_bndl[reg]);
                        tcg_gen_mov_i64(cpu_bndu[reg2], cpu_bndu[reg]);
                    }
                } else {
                    gen_lea_modrm(env, s, modrm);
                    if (CODE64(s)) {
                        tcg_gen_qemu_st_i64(cpu_bndl[reg], s->A0,
                                            s->mem_index, MO_LEUQ);
                        tcg_gen_addi_tl(s->A0, s->A0, 8);
                        tcg_gen_qemu_st_i64(cpu_bndu[reg], s->A0,
                                            s->mem_index, MO_LEUQ);
                    } else {
                        tcg_gen_qemu_st_i64(cpu_bndl[reg], s->A0,
                                            s->mem_index, MO_LEUL);
                        tcg_gen_addi_tl(s->A0, s->A0, 4);
                        tcg_gen_qemu_st_i64(cpu_bndu[reg], s->A0,
                                            s->mem_index, MO_LEUL);
                    }
                }
            } else if (mod != 3) {
                /* bndstx */
                AddressParts a = gen_lea_modrm_0(env, s, modrm);
                if (reg >= 4
                    || (prefixes & PREFIX_LOCK)
                    || s->aflag == MO_16
                    || a.base < -1) {
                    goto illegal_op;
                }
                if (a.base >= 0) {
                    tcg_gen_addi_tl(s->A0, cpu_regs[a.base], a.disp);
                } else {
                    tcg_gen_movi_tl(s->A0, 0);
                }
                gen_lea_v_seg(s, s->A0, a.def_seg, s->override);
                if (a.index >= 0) {
                    tcg_gen_mov_tl(s->T0, cpu_regs[a.index]);
                } else {
                    tcg_gen_movi_tl(s->T0, 0);
                }
                if (CODE64(s)) {
                    gen_helper_bndstx64(tcg_env, s->A0, s->T0,
                                        cpu_bndl[reg], cpu_bndu[reg]);
                } else {
                    gen_helper_bndstx32(tcg_env, s->A0, s->T0,
                                        cpu_bndl[reg], cpu_bndu[reg]);
                }
            }
        }
        gen_nop_modrm(env, s, modrm);
        break;

    case 0x120: /* mov reg, crN */
    case 0x122: /* mov crN, reg */
        if (!check_cpl0(s)) {
            break;
        }
        modrm = x86_ldub_code(env, s);
        /*
         * Ignore the mod bits (assume (modrm&0xc0)==0xc0).
         * AMD documentation (24594.pdf) and testing of Intel 386 and 486
         * processors all show that the mod bits are assumed to be 1's,
         * regardless of actual values.
         */
        rm = (modrm & 7) | REX_B(s);
        reg = ((modrm >> 3) & 7) | REX_R(s);
        switch (reg) {
        case 0:
            if ((prefixes & PREFIX_LOCK) &&
                (s->cpuid_ext3_features & CPUID_EXT3_CR8LEG)) {
                reg = 8;
            }
            break;
        case 2:
        case 3:
        case 4:
        case 8:
            break;
        default:
            goto unknown_op;
        }
        ot  = (CODE64(s) ? MO_64 : MO_32);

        translator_io_start(&s->base);
        if (b & 2) {
            gen_svm_check_intercept(s, SVM_EXIT_WRITE_CR0 + reg);
            gen_op_mov_v_reg(s, ot, s->T0, rm);
            gen_helper_write_crN(tcg_env, tcg_constant_i32(reg), s->T0);
            s->base.is_jmp = DISAS_EOB_NEXT;
        } else {
            gen_svm_check_intercept(s, SVM_EXIT_READ_CR0 + reg);
            gen_helper_read_crN(s->T0, tcg_env, tcg_constant_i32(reg));
            gen_op_mov_reg_v(s, ot, rm, s->T0);
        }
        break;

    case 0x121: /* mov reg, drN */
    case 0x123: /* mov drN, reg */
        if (check_cpl0(s)) {
            modrm = x86_ldub_code(env, s);
            /* Ignore the mod bits (assume (modrm&0xc0)==0xc0).
             * AMD documentation (24594.pdf) and testing of
             * intel 386 and 486 processors all show that the mod bits
             * are assumed to be 1's, regardless of actual values.
             */
            rm = (modrm & 7) | REX_B(s);
            reg = ((modrm >> 3) & 7) | REX_R(s);
            if (CODE64(s))
                ot = MO_64;
            else
                ot = MO_32;
            if (reg >= 8) {
                goto illegal_op;
            }
            if (b & 2) {
                gen_svm_check_intercept(s, SVM_EXIT_WRITE_DR0 + reg);
                gen_op_mov_v_reg(s, ot, s->T0, rm);
                tcg_gen_movi_i32(s->tmp2_i32, reg);
                gen_helper_set_dr(tcg_env, s->tmp2_i32, s->T0);
                s->base.is_jmp = DISAS_EOB_NEXT;
            } else {
                gen_svm_check_intercept(s, SVM_EXIT_READ_DR0 + reg);
                tcg_gen_movi_i32(s->tmp2_i32, reg);
                gen_helper_get_dr(s->T0, tcg_env, s->tmp2_i32);
                gen_op_mov_reg_v(s, ot, rm, s->T0);
            }
        }
        break;
    case 0x106: /* clts */
        if (check_cpl0(s)) {
            gen_svm_check_intercept(s, SVM_EXIT_WRITE_CR0);
            gen_helper_clts(tcg_env);
            /* abort block because static cpu state changed */
            s->base.is_jmp = DISAS_EOB_NEXT;
        }
        break;
    /* MMX/3DNow!/SSE/SSE2/SSE3/SSSE3/SSE4 support */
    case 0x1ae:
        modrm = x86_ldub_code(env, s);
        switch (modrm) {
        CASE_MODRM_MEM_OP(0): /* fxsave */
            if (!(s->cpuid_features & CPUID_FXSR)
                || (prefixes & PREFIX_LOCK)) {
                goto illegal_op;
            }
            if ((s->flags & HF_EM_MASK) || (s->flags & HF_TS_MASK)) {
                gen_exception(s, EXCP07_PREX);
                break;
            }
            gen_lea_modrm(env, s, modrm);
            gen_helper_fxsave(tcg_env, s->A0);
            break;

        CASE_MODRM_MEM_OP(1): /* fxrstor */
            if (!(s->cpuid_features & CPUID_FXSR)
                || (prefixes & PREFIX_LOCK)) {
                goto illegal_op;
            }
            if ((s->flags & HF_EM_MASK) || (s->flags & HF_TS_MASK)) {
                gen_exception(s, EXCP07_PREX);
                break;
            }
            gen_lea_modrm(env, s, modrm);
            gen_helper_fxrstor(tcg_env, s->A0);
            break;

        CASE_MODRM_MEM_OP(2): /* ldmxcsr */
            if ((s->flags & HF_EM_MASK) || !(s->flags & HF_OSFXSR_MASK)) {
                goto illegal_op;
            }
            if (s->flags & HF_TS_MASK) {
                gen_exception(s, EXCP07_PREX);
                break;
            }
            gen_lea_modrm(env, s, modrm);
            tcg_gen_qemu_ld_i32(s->tmp2_i32, s->A0, s->mem_index, MO_LEUL);
            gen_helper_ldmxcsr(tcg_env, s->tmp2_i32);
            break;

        CASE_MODRM_MEM_OP(3): /* stmxcsr */
            if ((s->flags & HF_EM_MASK) || !(s->flags & HF_OSFXSR_MASK)) {
                goto illegal_op;
            }
            if (s->flags & HF_TS_MASK) {
                gen_exception(s, EXCP07_PREX);
                break;
            }
            gen_helper_update_mxcsr(tcg_env);
            gen_lea_modrm(env, s, modrm);
            tcg_gen_ld32u_tl(s->T0, tcg_env, offsetof(CPUX86State, mxcsr));
            gen_op_st_v(s, MO_32, s->T0, s->A0);
            break;

        CASE_MODRM_MEM_OP(4): /* xsave */
            if ((s->cpuid_ext_features & CPUID_EXT_XSAVE) == 0
                || (prefixes & (PREFIX_LOCK | PREFIX_DATA
                                | PREFIX_REPZ | PREFIX_REPNZ))) {
                goto illegal_op;
            }
            gen_lea_modrm(env, s, modrm);
            tcg_gen_concat_tl_i64(s->tmp1_i64, cpu_regs[R_EAX],
                                  cpu_regs[R_EDX]);
            gen_helper_xsave(tcg_env, s->A0, s->tmp1_i64);
            break;

        CASE_MODRM_MEM_OP(5): /* xrstor */
            if ((s->cpuid_ext_features & CPUID_EXT_XSAVE) == 0
                || (prefixes & (PREFIX_LOCK | PREFIX_DATA
                                | PREFIX_REPZ | PREFIX_REPNZ))) {
                goto illegal_op;
            }
            gen_lea_modrm(env, s, modrm);
            tcg_gen_concat_tl_i64(s->tmp1_i64, cpu_regs[R_EAX],
                                  cpu_regs[R_EDX]);
            gen_helper_xrstor(tcg_env, s->A0, s->tmp1_i64);
            /* XRSTOR is how MPX is enabled, which changes how
               we translate.  Thus we need to end the TB.  */
            s->base.is_jmp = DISAS_EOB_NEXT;
            break;

        CASE_MODRM_MEM_OP(6): /* xsaveopt / clwb */
            if (prefixes & PREFIX_LOCK) {
                goto illegal_op;
            }
            if (prefixes & PREFIX_DATA) {
                /* clwb */
                if (!(s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_CLWB)) {
                    goto illegal_op;
                }
                gen_nop_modrm(env, s, modrm);
            } else {
                /* xsaveopt */
                if ((s->cpuid_ext_features & CPUID_EXT_XSAVE) == 0
                    || (s->cpuid_xsave_features & CPUID_XSAVE_XSAVEOPT) == 0
                    || (prefixes & (PREFIX_REPZ | PREFIX_REPNZ))) {
                    goto illegal_op;
                }
                gen_lea_modrm(env, s, modrm);
                tcg_gen_concat_tl_i64(s->tmp1_i64, cpu_regs[R_EAX],
                                      cpu_regs[R_EDX]);
                gen_helper_xsaveopt(tcg_env, s->A0, s->tmp1_i64);
            }
            break;

        CASE_MODRM_MEM_OP(7): /* clflush / clflushopt */
            if (prefixes & PREFIX_LOCK) {
                goto illegal_op;
            }
            if (prefixes & PREFIX_DATA) {
                /* clflushopt */
                if (!(s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_CLFLUSHOPT)) {
                    goto illegal_op;
                }
            } else {
                /* clflush */
                if ((s->prefix & (PREFIX_REPZ | PREFIX_REPNZ))
                    || !(s->cpuid_features & CPUID_CLFLUSH)) {
                    goto illegal_op;
                }
            }
            gen_nop_modrm(env, s, modrm);
            break;

        case 0xc0 ... 0xc7: /* rdfsbase (f3 0f ae /0) */
        case 0xc8 ... 0xcf: /* rdgsbase (f3 0f ae /1) */
        case 0xd0 ... 0xd7: /* wrfsbase (f3 0f ae /2) */
        case 0xd8 ... 0xdf: /* wrgsbase (f3 0f ae /3) */
            if (CODE64(s)
                && (prefixes & PREFIX_REPZ)
                && !(prefixes & PREFIX_LOCK)
                && (s->cpuid_7_0_ebx_features & CPUID_7_0_EBX_FSGSBASE)) {
                TCGv base, treg, src, dst;

                /* Preserve hflags bits by testing CR4 at runtime.  */
                tcg_gen_movi_i32(s->tmp2_i32, CR4_FSGSBASE_MASK);
                gen_helper_cr4_testbit(tcg_env, s->tmp2_i32);

                base = cpu_seg_base[modrm & 8 ? R_GS : R_FS];
                treg = cpu_regs[(modrm & 7) | REX_B(s)];

                if (modrm & 0x10) {
                    /* wr*base */
                    dst = base, src = treg;
                } else {
                    /* rd*base */
                    dst = treg, src = base;
                }

                if (s->dflag == MO_32) {
                    tcg_gen_ext32u_tl(dst, src);
                } else {
                    tcg_gen_mov_tl(dst, src);
                }
                break;
            }
            goto unknown_op;

        case 0xf8 ... 0xff: /* sfence */
            if (!(s->cpuid_features & CPUID_SSE)
                || (prefixes & PREFIX_LOCK)) {
                goto illegal_op;
            }
            tcg_gen_mb(TCG_MO_ST_ST | TCG_BAR_SC);
            break;
        case 0xe8 ... 0xef: /* lfence */
            if (!(s->cpuid_features & CPUID_SSE)
                || (prefixes & PREFIX_LOCK)) {
                goto illegal_op;
            }
            tcg_gen_mb(TCG_MO_LD_LD | TCG_BAR_SC);
            break;
        case 0xf0 ... 0xf7: /* mfence */
            if (!(s->cpuid_features & CPUID_SSE2)
                || (prefixes & PREFIX_LOCK)) {
                goto illegal_op;
            }
            tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
            break;

        default:
            goto unknown_op;
        }
        break;

    case 0x1aa: /* rsm */
        gen_svm_check_intercept(s, SVM_EXIT_RSM);
        if (!(s->flags & HF_SMM_MASK))
            goto illegal_op;
#ifdef CONFIG_USER_ONLY
        /* we should not be in SMM mode */
        g_assert_not_reached();
#else
        gen_helper_rsm(tcg_env);
        assume_cc_op(s, CC_OP_EFLAGS);
#endif /* CONFIG_USER_ONLY */
        s->base.is_jmp = DISAS_EOB_ONLY;
        break;
    case 0x1b8: /* SSE4.2 popcnt */
        if ((prefixes & (PREFIX_REPZ | PREFIX_LOCK | PREFIX_REPNZ)) !=
             PREFIX_REPZ)
            goto illegal_op;
        if (!(s->cpuid_ext_features & CPUID_EXT_POPCNT))
            goto illegal_op;

        modrm = x86_ldub_code(env, s);
        reg = ((modrm >> 3) & 7) | REX_R(s);

        ot = dflag;
        gen_ld_modrm(env, s, modrm, ot);
        gen_extu(ot, s->T0);
        tcg_gen_mov_tl(cpu_cc_src, s->T0);
        tcg_gen_ctpop_tl(s->T0, s->T0);
        gen_op_mov_reg_v(s, ot, reg, s->T0);

        set_cc_op(s, CC_OP_POPCNT);
        break;
    default:
        g_assert_not_reached();
    }
    return;
 illegal_op:
    gen_illegal_opcode(s);
    return;
 unknown_op:
    gen_unknown_opcode(env, s);
}

#include "decode-new.h"
#include "emit.c.inc"
#include "decode-new.c.inc"

void tcg_x86_init(void)
{
    static const char reg_names[CPU_NB_REGS][4] = {
#ifdef TARGET_X86_64
        [R_EAX] = "rax",
        [R_EBX] = "rbx",
        [R_ECX] = "rcx",
        [R_EDX] = "rdx",
        [R_ESI] = "rsi",
        [R_EDI] = "rdi",
        [R_EBP] = "rbp",
        [R_ESP] = "rsp",
        [8]  = "r8",
        [9]  = "r9",
        [10] = "r10",
        [11] = "r11",
        [12] = "r12",
        [13] = "r13",
        [14] = "r14",
        [15] = "r15",
#else
        [R_EAX] = "eax",
        [R_EBX] = "ebx",
        [R_ECX] = "ecx",
        [R_EDX] = "edx",
        [R_ESI] = "esi",
        [R_EDI] = "edi",
        [R_EBP] = "ebp",
        [R_ESP] = "esp",
#endif
    };
    static const char eip_name[] = {
#ifdef TARGET_X86_64
        "rip"
#else
        "eip"
#endif
    };
    static const char seg_base_names[6][8] = {
        [R_CS] = "cs_base",
        [R_DS] = "ds_base",
        [R_ES] = "es_base",
        [R_FS] = "fs_base",
        [R_GS] = "gs_base",
        [R_SS] = "ss_base",
    };
    static const char bnd_regl_names[4][8] = {
        "bnd0_lb", "bnd1_lb", "bnd2_lb", "bnd3_lb"
    };
    static const char bnd_regu_names[4][8] = {
        "bnd0_ub", "bnd1_ub", "bnd2_ub", "bnd3_ub"
    };
    int i;

    cpu_cc_op = tcg_global_mem_new_i32(tcg_env,
                                       offsetof(CPUX86State, cc_op), "cc_op");
    cpu_cc_dst = tcg_global_mem_new(tcg_env, offsetof(CPUX86State, cc_dst),
                                    "cc_dst");
    cpu_cc_src = tcg_global_mem_new(tcg_env, offsetof(CPUX86State, cc_src),
                                    "cc_src");
    cpu_cc_src2 = tcg_global_mem_new(tcg_env, offsetof(CPUX86State, cc_src2),
                                     "cc_src2");
    cpu_eip = tcg_global_mem_new(tcg_env, offsetof(CPUX86State, eip), eip_name);

    for (i = 0; i < CPU_NB_REGS; ++i) {
        cpu_regs[i] = tcg_global_mem_new(tcg_env,
                                         offsetof(CPUX86State, regs[i]),
                                         reg_names[i]);
    }

    for (i = 0; i < 6; ++i) {
        cpu_seg_base[i]
            = tcg_global_mem_new(tcg_env,
                                 offsetof(CPUX86State, segs[i].base),
                                 seg_base_names[i]);
    }

    for (i = 0; i < 4; ++i) {
        cpu_bndl[i]
            = tcg_global_mem_new_i64(tcg_env,
                                     offsetof(CPUX86State, bnd_regs[i].lb),
                                     bnd_regl_names[i]);
        cpu_bndu[i]
            = tcg_global_mem_new_i64(tcg_env,
                                     offsetof(CPUX86State, bnd_regs[i].ub),
                                     bnd_regu_names[i]);
    }
}

static void i386_tr_init_disas_context(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    CPUX86State *env = cpu_env(cpu);
    uint32_t flags = dc->base.tb->flags;
    uint32_t cflags = tb_cflags(dc->base.tb);
    int cpl = (flags >> HF_CPL_SHIFT) & 3;
    int iopl = (flags >> IOPL_SHIFT) & 3;

    dc->cs_base = dc->base.tb->cs_base;
    dc->pc_save = dc->base.pc_next;
    dc->flags = flags;
#ifndef CONFIG_USER_ONLY
    dc->cpl = cpl;
    dc->iopl = iopl;
#endif

    /* We make some simplifying assumptions; validate they're correct. */
    g_assert(PE(dc) == ((flags & HF_PE_MASK) != 0));
    g_assert(CPL(dc) == cpl);
    g_assert(IOPL(dc) == iopl);
    g_assert(VM86(dc) == ((flags & HF_VM_MASK) != 0));
    g_assert(CODE32(dc) == ((flags & HF_CS32_MASK) != 0));
    g_assert(CODE64(dc) == ((flags & HF_CS64_MASK) != 0));
    g_assert(SS32(dc) == ((flags & HF_SS32_MASK) != 0));
    g_assert(LMA(dc) == ((flags & HF_LMA_MASK) != 0));
    g_assert(ADDSEG(dc) == ((flags & HF_ADDSEG_MASK) != 0));
    g_assert(SVME(dc) == ((flags & HF_SVME_MASK) != 0));
    g_assert(GUEST(dc) == ((flags & HF_GUEST_MASK) != 0));

    dc->cc_op = CC_OP_DYNAMIC;
    dc->cc_op_dirty = false;
    /* select memory access functions */
    dc->mem_index = cpu_mmu_index(cpu, false);
    dc->cpuid_features = env->features[FEAT_1_EDX];
    dc->cpuid_ext_features = env->features[FEAT_1_ECX];
    dc->cpuid_ext2_features = env->features[FEAT_8000_0001_EDX];
    dc->cpuid_ext3_features = env->features[FEAT_8000_0001_ECX];
    dc->cpuid_7_0_ebx_features = env->features[FEAT_7_0_EBX];
    dc->cpuid_7_0_ecx_features = env->features[FEAT_7_0_ECX];
    dc->cpuid_7_1_eax_features = env->features[FEAT_7_1_EAX];
    dc->cpuid_xsave_features = env->features[FEAT_XSAVE];
    dc->jmp_opt = !((cflags & CF_NO_GOTO_TB) ||
                    (flags & (HF_RF_MASK | HF_TF_MASK | HF_INHIBIT_IRQ_MASK)));
    /*
     * If jmp_opt, we want to handle each string instruction individually.
     * For icount also disable repz optimization so that each iteration
     * is accounted separately.
     */
    dc->repz_opt = !dc->jmp_opt && !(cflags & CF_USE_ICOUNT);

    dc->T0 = tcg_temp_new();
    dc->T1 = tcg_temp_new();
    dc->A0 = tcg_temp_new();

    dc->tmp0 = tcg_temp_new();
    dc->tmp1_i64 = tcg_temp_new_i64();
    dc->tmp2_i32 = tcg_temp_new_i32();
    dc->tmp3_i32 = tcg_temp_new_i32();
    dc->tmp4 = tcg_temp_new();
    dc->cc_srcT = tcg_temp_new();
}

static void i386_tr_tb_start(DisasContextBase *db, CPUState *cpu)
{
}

static void i386_tr_insn_start(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    target_ulong pc_arg = dc->base.pc_next;

    dc->prev_insn_start = dc->base.insn_start;
    dc->prev_insn_end = tcg_last_op();
    if (tb_cflags(dcbase->tb) & CF_PCREL) {
        pc_arg &= ~TARGET_PAGE_MASK;
    }
    tcg_gen_insn_start(pc_arg, dc->cc_op);
}

static void i386_tr_translate_insn(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);
    bool orig_cc_op_dirty = dc->cc_op_dirty;
    CCOp orig_cc_op = dc->cc_op;
    target_ulong orig_pc_save = dc->pc_save;

#ifdef TARGET_VSYSCALL_PAGE
    /*
     * Detect entry into the vsyscall page and invoke the syscall.
     */
    if ((dc->base.pc_next & TARGET_PAGE_MASK) == TARGET_VSYSCALL_PAGE) {
        gen_exception(dc, EXCP_VSYSCALL);
        dc->base.pc_next = dc->pc + 1;
        return;
    }
#endif

    switch (sigsetjmp(dc->jmpbuf, 0)) {
    case 0:
        disas_insn(dc, cpu);
        break;
    case 1:
        gen_exception_gpf(dc);
        break;
    case 2:
        /* Restore state that may affect the next instruction. */
        dc->pc = dc->base.pc_next;
        /*
         * TODO: These save/restore can be removed after the table-based
         * decoder is complete; we will be decoding the insn completely
         * before any code generation that might affect these variables.
         */
        dc->cc_op_dirty = orig_cc_op_dirty;
        dc->cc_op = orig_cc_op;
        dc->pc_save = orig_pc_save;
        /* END TODO */
        dc->base.num_insns--;
        tcg_remove_ops_after(dc->prev_insn_end);
        dc->base.insn_start = dc->prev_insn_start;
        dc->base.is_jmp = DISAS_TOO_MANY;
        return;
    default:
        g_assert_not_reached();
    }

    /*
     * Instruction decoding completed (possibly with #GP if the
     * 15-byte boundary was exceeded).
     */
    dc->base.pc_next = dc->pc;
    if (dc->base.is_jmp == DISAS_NEXT) {
        if (dc->flags & (HF_TF_MASK | HF_INHIBIT_IRQ_MASK)) {
            /*
             * If single step mode, we generate only one instruction and
             * generate an exception.
             * If irq were inhibited with HF_INHIBIT_IRQ_MASK, we clear
             * the flag and abort the translation to give the irqs a
             * chance to happen.
             */
            dc->base.is_jmp = DISAS_EOB_NEXT;
        } else if (!is_same_page(&dc->base, dc->base.pc_next)) {
            dc->base.is_jmp = DISAS_TOO_MANY;
        }
    }
}

static void i386_tr_tb_stop(DisasContextBase *dcbase, CPUState *cpu)
{
    DisasContext *dc = container_of(dcbase, DisasContext, base);

    switch (dc->base.is_jmp) {
    case DISAS_NORETURN:
        break;
    case DISAS_TOO_MANY:
        gen_update_cc_op(dc);
        gen_jmp_rel_csize(dc, 0, 0);
        break;
    case DISAS_EOB_NEXT:
    case DISAS_EOB_INHIBIT_IRQ:
        assert(dc->base.pc_next == dc->pc);
        gen_update_eip_cur(dc);
        /* fall through */
    case DISAS_EOB_ONLY:
    case DISAS_EOB_RECHECK_TF:
    case DISAS_JUMP:
        gen_eob(dc, dc->base.is_jmp);
        break;
    default:
        g_assert_not_reached();
    }
}

static const TranslatorOps i386_tr_ops = {
    .init_disas_context = i386_tr_init_disas_context,
    .tb_start           = i386_tr_tb_start,
    .insn_start         = i386_tr_insn_start,
    .translate_insn     = i386_tr_translate_insn,
    .tb_stop            = i386_tr_tb_stop,
};

/* generate intermediate code for basic block 'tb'.  */
void gen_intermediate_code(CPUState *cpu, TranslationBlock *tb, int *max_insns,
                           vaddr pc, void *host_pc)
{
    DisasContext dc;

    translator_loop(cpu, tb, max_insns, pc, host_pc, &i386_tr_ops, &dc.base);
}
