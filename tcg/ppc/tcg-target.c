/*
 * Tiny Code Generator for QEMU
 *
 * Copyright (c) 2008 Fabrice Bellard
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

#include "tcg-be-ldst.h"

static tcg_insn_unit *tb_ret_addr;

#if defined _CALL_DARWIN || defined __APPLE__
#define TCG_TARGET_CALL_DARWIN
#endif

#ifdef TCG_TARGET_CALL_DARWIN
#define LINKAGE_AREA_SIZE 24
#define LR_OFFSET 8
#elif defined _CALL_AIX
#define LINKAGE_AREA_SIZE 52
#define LR_OFFSET 8
#else
#define LINKAGE_AREA_SIZE 8
#define LR_OFFSET 4
#endif

#ifndef GUEST_BASE
#define GUEST_BASE 0
#endif

#ifdef CONFIG_USE_GUEST_BASE
#define TCG_GUEST_BASE_REG 30
#else
#define TCG_GUEST_BASE_REG 0
#endif

#ifndef NDEBUG
static const char * const tcg_target_reg_names[TCG_TARGET_NB_REGS] = {
    "r0",
    "r1",
    "r2",
    "r3",
    "r4",
    "r5",
    "r6",
    "r7",
    "r8",
    "r9",
    "r10",
    "r11",
    "r12",
    "r13",
    "r14",
    "r15",
    "r16",
    "r17",
    "r18",
    "r19",
    "r20",
    "r21",
    "r22",
    "r23",
    "r24",
    "r25",
    "r26",
    "r27",
    "r28",
    "r29",
    "r30",
    "r31"
};
#endif

static const int tcg_target_reg_alloc_order[] = {
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31,
#ifdef TCG_TARGET_CALL_DARWIN
    TCG_REG_R2,
#endif
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10,
#ifndef TCG_TARGET_CALL_DARWIN
    TCG_REG_R11,
#endif
    TCG_REG_R12,
#ifndef _CALL_SYSV
    TCG_REG_R13,
#endif
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27
};

static const int tcg_target_call_iarg_regs[] = {
    TCG_REG_R3,
    TCG_REG_R4,
    TCG_REG_R5,
    TCG_REG_R6,
    TCG_REG_R7,
    TCG_REG_R8,
    TCG_REG_R9,
    TCG_REG_R10
};

static const int tcg_target_call_oarg_regs[2] = {
    TCG_REG_R3,
    TCG_REG_R4
};

static const int tcg_target_callee_save_regs[] = {
#ifdef TCG_TARGET_CALL_DARWIN
    TCG_REG_R11,
    TCG_REG_R13,
#endif
#ifdef _CALL_AIX
    TCG_REG_R13,
#endif
    TCG_REG_R14,
    TCG_REG_R15,
    TCG_REG_R16,
    TCG_REG_R17,
    TCG_REG_R18,
    TCG_REG_R19,
    TCG_REG_R20,
    TCG_REG_R21,
    TCG_REG_R22,
    TCG_REG_R23,
    TCG_REG_R24,
    TCG_REG_R25,
    TCG_REG_R26,
    TCG_REG_R27, /* currently used for the global env */
    TCG_REG_R28,
    TCG_REG_R29,
    TCG_REG_R30,
    TCG_REG_R31
};

static inline bool in_range_b(tcg_target_long target)
{
    return target == sextract32(target, 0, 26);
}

static uint32_t reloc_pc24_val(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    assert(in_range_b(disp));
    return disp & 0x3fffffc;
}

static void reloc_pc24(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    *pc = (*pc & ~0x3fffffc) | reloc_pc24_val(pc, target);
}

static uint16_t reloc_pc14_val(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_ptr_byte_diff(target, pc);
    assert(disp == (int16_t) disp);
    return disp & 0xfffc;
}

static void reloc_pc14(tcg_insn_unit *pc, tcg_insn_unit *target)
{
    *pc = (*pc & ~0xfffc) | reloc_pc14_val(pc, target);
}

static void patch_reloc(tcg_insn_unit *code_ptr, int type,
                        intptr_t value, intptr_t addend)
{
    tcg_insn_unit *target = (tcg_insn_unit *)value;

    assert(addend == 0);
    switch (type) {
    case R_PPC_REL14:
        reloc_pc14(code_ptr, target);
        break;
    case R_PPC_REL24:
        reloc_pc24(code_ptr, target);
        break;
    default:
        tcg_abort();
    }
}

/* parse target specific constraints */
static int target_parse_constraint(TCGArgConstraint *ct, const char **pct_str)
{
    const char *ct_str;

    ct_str = *pct_str;
    switch (ct_str[0]) {
    case 'A': case 'B': case 'C': case 'D':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set_reg(ct->u.regs, 3 + ct_str[0] - 'A');
        break;
    case 'r':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        break;
#ifdef CONFIG_SOFTMMU
    case 'L':                   /* qemu_ld constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R5);
#if TARGET_LONG_BITS == 64
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R6);
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R7);
#endif
#endif
        break;
    case 'K':                   /* qemu_st[8..32] constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R5);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R6);
#if TARGET_LONG_BITS == 64
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R7);
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R8);
#endif
#endif
        break;
    case 'M':                   /* qemu_st64 constraint */
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R4);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R5);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R6);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R7);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R8);
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R9);
#endif
        break;
#else
    case 'L':
    case 'K':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        break;
    case 'M':
        ct->ct |= TCG_CT_REG;
        tcg_regset_set32(ct->u.regs, 0, 0xffffffff);
        tcg_regset_reset_reg(ct->u.regs, TCG_REG_R3);
        break;
#endif
    default:
        return -1;
    }
    ct_str++;
    *pct_str = ct_str;
    return 0;
}

/* test if a constant matches the constraint */
static int tcg_target_const_match(tcg_target_long val, TCGType type,
                                  const TCGArgConstraint *arg_ct)
{
    int ct;

    ct = arg_ct->ct;
    if (ct & TCG_CT_CONST)
        return 1;
    return 0;
}

#define OPCD(opc) ((opc)<<26)
#define XO31(opc) (OPCD(31)|((opc)<<1))
#define XO19(opc) (OPCD(19)|((opc)<<1))

#define B      OPCD(18)
#define BC     OPCD(16)
#define LBZ    OPCD(34)
#define LHZ    OPCD(40)
#define LHA    OPCD(42)
#define LWZ    OPCD(32)
#define STB    OPCD(38)
#define STH    OPCD(44)
#define STW    OPCD(36)

#define ADDIC  OPCD(12)
#define ADDI   OPCD(14)
#define ADDIS  OPCD(15)
#define ORI    OPCD(24)
#define ORIS   OPCD(25)
#define XORI   OPCD(26)
#define XORIS  OPCD(27)
#define ANDI   OPCD(28)
#define ANDIS  OPCD(29)
#define MULLI  OPCD( 7)
#define CMPLI  OPCD(10)
#define CMPI   OPCD(11)
#define SUBFIC OPCD( 8)

#define LWZU   OPCD(33)
#define STWU   OPCD(37)

#define RLWIMI OPCD(20)
#define RLWINM OPCD(21)
#define RLWNM  OPCD(23)

#define BCLR   XO19( 16)
#define BCCTR  XO19(528)
#define CRAND  XO19(257)
#define CRANDC XO19(129)
#define CRNAND XO19(225)
#define CROR   XO19(449)
#define CRNOR  XO19( 33)

#define EXTSB  XO31(954)
#define EXTSH  XO31(922)
#define ADD    XO31(266)
#define ADDE   XO31(138)
#define ADDC   XO31( 10)
#define AND    XO31( 28)
#define SUBF   XO31( 40)
#define SUBFC  XO31(  8)
#define SUBFE  XO31(136)
#define OR     XO31(444)
#define XOR    XO31(316)
#define MULLW  XO31(235)
#define MULHWU XO31( 11)
#define DIVW   XO31(491)
#define DIVWU  XO31(459)
#define CMP    XO31(  0)
#define CMPL   XO31( 32)
#define LHBRX  XO31(790)
#define LWBRX  XO31(534)
#define STHBRX XO31(918)
#define STWBRX XO31(662)
#define MFSPR  XO31(339)
#define MTSPR  XO31(467)
#define SRAWI  XO31(824)
#define NEG    XO31(104)
#define MFCR   XO31( 19)
#define CNTLZW XO31( 26)
#define NOR    XO31(124)
#define ANDC   XO31( 60)
#define ORC    XO31(412)
#define EQV    XO31(284)
#define NAND   XO31(476)
#define ISEL   XO31( 15)

#define LBZX   XO31( 87)
#define LHZX   XO31(279)
#define LHAX   XO31(343)
#define LWZX   XO31( 23)
#define STBX   XO31(215)
#define STHX   XO31(407)
#define STWX   XO31(151)

#define SPR(a,b) ((((a)<<5)|(b))<<11)
#define LR     SPR(8, 0)
#define CTR    SPR(9, 0)

#define SLW    XO31( 24)
#define SRW    XO31(536)
#define SRAW   XO31(792)

#define TW     XO31(4)
#define TRAP   (TW | TO (31))

#define RT(r) ((r)<<21)
#define RS(r) ((r)<<21)
#define RA(r) ((r)<<16)
#define RB(r) ((r)<<11)
#define TO(t) ((t)<<21)
#define SH(s) ((s)<<11)
#define MB(b) ((b)<<6)
#define ME(e) ((e)<<1)
#define BO(o) ((o)<<21)

#define LK    1

#define TAB(t,a,b) (RT(t) | RA(a) | RB(b))
#define SAB(s,a,b) (RS(s) | RA(a) | RB(b))

#define BF(n)    ((n)<<23)
#define BI(n, c) (((c)+((n)*4))<<16)
#define BT(n, c) (((c)+((n)*4))<<21)
#define BA(n, c) (((c)+((n)*4))<<16)
#define BB(n, c) (((c)+((n)*4))<<11)

#define BO_COND_TRUE  BO (12)
#define BO_COND_FALSE BO (4)
#define BO_ALWAYS     BO (20)

enum {
    CR_LT,
    CR_GT,
    CR_EQ,
    CR_SO
};

static const uint32_t tcg_to_bc[] = {
    [TCG_COND_EQ]  = BC | BI (7, CR_EQ) | BO_COND_TRUE,
    [TCG_COND_NE]  = BC | BI (7, CR_EQ) | BO_COND_FALSE,
    [TCG_COND_LT]  = BC | BI (7, CR_LT) | BO_COND_TRUE,
    [TCG_COND_GE]  = BC | BI (7, CR_LT) | BO_COND_FALSE,
    [TCG_COND_LE]  = BC | BI (7, CR_GT) | BO_COND_FALSE,
    [TCG_COND_GT]  = BC | BI (7, CR_GT) | BO_COND_TRUE,
    [TCG_COND_LTU] = BC | BI (7, CR_LT) | BO_COND_TRUE,
    [TCG_COND_GEU] = BC | BI (7, CR_LT) | BO_COND_FALSE,
    [TCG_COND_LEU] = BC | BI (7, CR_GT) | BO_COND_FALSE,
    [TCG_COND_GTU] = BC | BI (7, CR_GT) | BO_COND_TRUE,
};

static void tcg_out_mov(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg)
{
    if (ret != arg) {
        tcg_out32(s, OR | SAB(arg, ret, arg));
    }
}

static void tcg_out_movi(TCGContext *s, TCGType type,
                         TCGReg ret, tcg_target_long arg)
{
    if (arg == (int16_t) arg)
        tcg_out32 (s, ADDI | RT (ret) | RA (0) | (arg & 0xffff));
    else {
        tcg_out32 (s, ADDIS | RT (ret) | RA (0) | ((arg >> 16) & 0xffff));
        if (arg & 0xffff)
            tcg_out32 (s, ORI | RS (ret) | RA (ret) | (arg & 0xffff));
    }
}

static void tcg_out_ldst (TCGContext *s, int ret, int addr,
                          int offset, int op1, int op2)
{
    if (offset == (int16_t) offset)
        tcg_out32 (s, op1 | RT (ret) | RA (addr) | (offset & 0xffff));
    else {
        tcg_out_movi (s, TCG_TYPE_I32, 0, offset);
        tcg_out32 (s, op2 | RT (ret) | RA (addr) | RB (0));
    }
}

static void tcg_out_b(TCGContext *s, int mask, tcg_insn_unit *target)
{
    ptrdiff_t disp = tcg_pcrel_diff(s, target);
    if (in_range_b(disp)) {
        tcg_out32(s, B | (disp & 0x3fffffc) | mask);
    } else {
        tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R0, (uintptr_t)target);
        tcg_out32(s, MTSPR | RS(TCG_REG_R0) | CTR);
        tcg_out32(s, BCCTR | BO_ALWAYS | mask);
    }
}

static void tcg_out_callr(TCGContext *s, TCGReg reg, int lk)
{
#ifdef _CALL_AIX
    tcg_out32(s, LWZ | RT(TCG_REG_R0) | RA(reg));
    tcg_out32(s, MTSPR | RA(TCG_REG_R0) | CTR);
    tcg_out32(s, LWZ | RT(TCG_REG_R2) | RA(reg) | 4);
#else
    tcg_out32(s, MTSPR | RS(reg) | CTR);
#endif
    tcg_out32(s, BCCTR | BO_ALWAYS | lk);
}

static void tcg_out_calli(TCGContext *s, void *target, int lk)
{
#ifdef _CALL_AIX
    tcg_out_movi(s, TCG_TYPE_PTR, TCG_REG_R2, (uintptr_t)target);
    tcg_out_callr(s, TCG_REG_R2, lk);
#else
    tcg_out_b(s, lk, target);
#endif
}

#if defined(CONFIG_SOFTMMU)

static void add_qemu_ldst_label (TCGContext *s,
                                 bool is_ld,
                                 TCGMemOp opc,
                                 int data_reg,
                                 int data_reg2,
                                 int addrlo_reg,
                                 int addrhi_reg,
                                 int mem_index,
                                 tcg_insn_unit *raddr,
                                 tcg_insn_unit *label_ptr)
{
    TCGLabelQemuLdst *label = new_ldst_label(s);

    label->is_ld = is_ld;
    label->opc = opc;
    label->datalo_reg = data_reg;
    label->datahi_reg = data_reg2;
    label->addrlo_reg = addrlo_reg;
    label->addrhi_reg = addrhi_reg;
    label->mem_index = mem_index;
    label->raddr = raddr;
    label->label_ptr[0] = label_ptr;
}

/* helper signature: helper_ret_ld_mmu(CPUState *env, target_ulong addr,
 *                                     int mmu_idx, uintptr_t ra)
 */
static void * const qemu_ld_helpers[16] = {
    [MO_UB]   = helper_ret_ldub_mmu,
    [MO_LEUW] = helper_le_lduw_mmu,
    [MO_LEUL] = helper_le_ldul_mmu,
    [MO_LEQ]  = helper_le_ldq_mmu,
    [MO_BEUW] = helper_be_lduw_mmu,
    [MO_BEUL] = helper_be_ldul_mmu,
    [MO_BEQ]  = helper_be_ldq_mmu,
};

/* helper signature: helper_ret_st_mmu(CPUState *env, target_ulong addr,
 *                                     uintxx_t val, int mmu_idx, uintptr_t ra)
 */
static void * const qemu_st_helpers[16] = {
    [MO_UB]   = helper_ret_stb_mmu,
    [MO_LEUW] = helper_le_stw_mmu,
    [MO_LEUL] = helper_le_stl_mmu,
    [MO_LEQ]  = helper_le_stq_mmu,
    [MO_BEUW] = helper_be_stw_mmu,
    [MO_BEUL] = helper_be_stl_mmu,
    [MO_BEQ]  = helper_be_stq_mmu,
};

static tcg_insn_unit *ld_trampolines[16];
static tcg_insn_unit *st_trampolines[16];

/* Perform the TLB load and compare.  Branches to the slow path, placing the
   address of the branch in *LABEL_PTR.  Loads the addend of the TLB into R0.
   Clobbers R1 and R2.  */

static void tcg_out_tlb_check(TCGContext *s, TCGReg r0, TCGReg r1, TCGReg r2,
                              TCGReg addrlo, TCGReg addrhi, TCGMemOp s_bits,
                              int mem_index, int is_load,
                              tcg_insn_unit **label_ptr)
{
    int cmp_off =
        (is_load
         ? offsetof(CPUArchState, tlb_table[mem_index][0].addr_read)
         : offsetof(CPUArchState, tlb_table[mem_index][0].addr_write));
    int add_off = offsetof(CPUArchState, tlb_table[mem_index][0].addend);
    tcg_insn_unit retranst;
    TCGReg base = TCG_AREG0;

    /* Extract the page index, shifted into place for tlb index.  */
    tcg_out32(s, (RLWINM
                  | RA(r0)
                  | RS(addrlo)
                  | SH(32 - (TARGET_PAGE_BITS - CPU_TLB_ENTRY_BITS))
                  | MB(32 - (CPU_TLB_BITS + CPU_TLB_ENTRY_BITS))
                  | ME(31 - CPU_TLB_ENTRY_BITS)));

    /* Compensate for very large offsets.  */
    if (add_off >= 0x8000) {
        /* Most target env are smaller than 32k; none are larger than 64k.
           Simplify the logic here merely to offset by 0x7ff0, giving us a
           range just shy of 64k.  Check this assumption.  */
        QEMU_BUILD_BUG_ON(offsetof(CPUArchState,
                                   tlb_table[NB_MMU_MODES - 1][1])
                          > 0x7ff0 + 0x7fff);
        tcg_out32(s, ADDI | RT(r1) | RA(base) | 0x7ff0);
        base = r1;
        cmp_off -= 0x7ff0;
        add_off -= 0x7ff0;
    }

    /* Clear the non-page, non-alignment bits from the address.  */
    tcg_out32(s, (RLWINM
                  | RA(r2)
                  | RS(addrlo)
                  | SH(0)
                  | MB((32 - s_bits) & 31)
                  | ME(31 - TARGET_PAGE_BITS)));

    tcg_out32(s, ADD | RT(r0) | RA(r0) | RB(base));
    base = r0;

    /* Load the tlb comparator.  */
    tcg_out32(s, LWZ | RT(r1) | RA(base) | (cmp_off & 0xffff));

    tcg_out32(s, CMP | BF(7) | RA(r2) | RB(r1));

    if (TARGET_LONG_BITS == 64) {
        tcg_out32(s, LWZ | RT(r1) | RA(base) | ((cmp_off + 4) & 0xffff));
    }

    /* Load the tlb addend for use on the fast path.
       Do this asap to minimize load delay.  */
    tcg_out32(s, LWZ | RT(r0) | RA(base) | (add_off & 0xffff));

    if (TARGET_LONG_BITS == 64) {
        tcg_out32(s, CMP | BF(6) | RA(addrhi) | RB(r1));
        tcg_out32(s, CRAND | BT(7, CR_EQ) | BA(6, CR_EQ) | BB(7, CR_EQ));
    }

    /* Use a conditional branch-and-link so that we load a pointer to
       somewhere within the current opcode, for passing on to the helper.
       This address cannot be used for a tail call, but it's shorter
       than forming an address from scratch.  */
    *label_ptr = s->code_ptr;
    retranst = *s->code_ptr & 0xfffc;
    tcg_out32(s, BC | BI(7, CR_EQ) | retranst | BO_COND_FALSE | LK);
}
#endif

static void tcg_out_qemu_ld(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg addrlo, datalo, datahi, rbase, addrhi __attribute__((unused));
    TCGMemOp opc, bswap;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    tcg_insn_unit *label_ptr;
#endif

    datalo = *args++;
    datahi = (is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS == 64 ? *args++ : 0);
    opc = *args++;
    bswap = opc & MO_BSWAP;

#ifdef CONFIG_SOFTMMU
    mem_index = *args;
    tcg_out_tlb_check(s, TCG_REG_R3, TCG_REG_R4, TCG_REG_R0, addrlo,
                      addrhi, opc & MO_SIZE, mem_index, 0, &label_ptr);
    rbase = TCG_REG_R3;
#else  /* !CONFIG_SOFTMMU */
    rbase = GUEST_BASE ? TCG_GUEST_BASE_REG : 0;
#endif

    switch (opc & MO_SSIZE) {
    default:
    case MO_UB:
        tcg_out32(s, LBZX | TAB(datalo, rbase, addrlo));
        break;
    case MO_SB:
        tcg_out32(s, LBZX | TAB(datalo, rbase, addrlo));
        tcg_out32(s, EXTSB | RA(datalo) | RS(datalo));
        break;
    case MO_UW:
        tcg_out32(s, (bswap ? LHBRX : LHZX) | TAB(datalo, rbase, addrlo));
        break;
    case MO_SW:
        if (bswap) {
            tcg_out32(s, LHBRX | TAB(datalo, rbase, addrlo));
            tcg_out32(s, EXTSH | RA(datalo) | RS(datalo));
        } else {
            tcg_out32(s, LHAX | TAB(datalo, rbase, addrlo));
        }
        break;
    case MO_UL:
        tcg_out32(s, (bswap ? LWBRX : LWZX) | TAB(datalo, rbase, addrlo));
        break;
    case MO_Q:
        if (bswap) {
            tcg_out32(s, ADDI | RT(TCG_REG_R0) | RA(addrlo) | 4);
            tcg_out32(s, LWBRX | TAB(datalo, rbase, addrlo));
            tcg_out32(s, LWBRX | TAB(datahi, rbase, TCG_REG_R0));
        } else if (rbase != 0) {
            tcg_out32(s, ADDI | RT(TCG_REG_R0) | RA(addrlo) | 4);
            tcg_out32(s, LWZX | TAB(datahi, rbase, addrlo));
            tcg_out32(s, LWZX | TAB(datalo, rbase, TCG_REG_R0));
        } else if (addrlo == datahi) {
            tcg_out32(s, LWZ | RT(datalo) | RA(addrlo) | 4);
            tcg_out32(s, LWZ | RT(datahi) | RA(addrlo));
        } else {
            tcg_out32(s, LWZ | RT(datahi) | RA(addrlo));
            tcg_out32(s, LWZ | RT(datalo) | RA(addrlo) | 4);
        }
        break;
    }
#ifdef CONFIG_SOFTMMU
    add_qemu_ldst_label(s, true, opc, datalo, datahi, addrlo,
                        addrhi, mem_index, s->code_ptr, label_ptr);
#endif
}

static void tcg_out_qemu_st(TCGContext *s, const TCGArg *args, bool is64)
{
    TCGReg addrlo, datalo, datahi, rbase, addrhi __attribute__((unused));
    TCGMemOp opc, bswap, s_bits;
#ifdef CONFIG_SOFTMMU
    int mem_index;
    tcg_insn_unit *label_ptr;
#endif

    datalo = *args++;
    datahi = (is64 ? *args++ : 0);
    addrlo = *args++;
    addrhi = (TARGET_LONG_BITS == 64 ? *args++ : 0);
    opc = *args++;
    bswap = opc & MO_BSWAP;
    s_bits = opc & MO_SIZE;

#ifdef CONFIG_SOFTMMU
    mem_index = *args;
    tcg_out_tlb_check(s, TCG_REG_R3, TCG_REG_R4, TCG_REG_R0, addrlo,
                      addrhi, s_bits, mem_index, 0, &label_ptr);
    rbase = TCG_REG_R3;
#else  /* !CONFIG_SOFTMMU */
    rbase = GUEST_BASE ? TCG_GUEST_BASE_REG : 0;
#endif

    switch (s_bits) {
    case MO_8:
        tcg_out32(s, STBX | SAB(datalo, rbase, addrlo));
        break;
    case MO_16:
        tcg_out32(s, (bswap ? STHBRX : STHX) | SAB(datalo, rbase, addrlo));
        break;
    case MO_32:
    default:
        tcg_out32(s, (bswap ? STWBRX : STWX) | SAB(datalo, rbase, addrlo));
        break;
    case MO_64:
        if (bswap) {
            tcg_out32(s, ADDI | RT(TCG_REG_R0) | RA(addrlo) | 4);
            tcg_out32(s, STWBRX | SAB(datalo, rbase, addrlo));
            tcg_out32(s, STWBRX | SAB(datahi, rbase, TCG_REG_R0));
        } else if (rbase != 0) {
            tcg_out32(s, ADDI | RT(TCG_REG_R0) | RA(addrlo) | 4);
            tcg_out32(s, STWX | SAB(datahi, rbase, addrlo));
            tcg_out32(s, STWX | SAB(datalo, rbase, TCG_REG_R0));
        } else {
            tcg_out32(s, STW | RS(datahi) | RA(addrlo));
            tcg_out32(s, STW | RS(datalo) | RA(addrlo) | 4);
        }
        break;
    }

#ifdef CONFIG_SOFTMMU
    add_qemu_ldst_label(s, false, opc, datalo, datahi, addrlo, addrhi,
                        mem_index, s->code_ptr, label_ptr);
#endif
}

#if defined(CONFIG_SOFTMMU)
static void tcg_out_qemu_ld_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGReg ir, datalo, datahi;
    TCGMemOp opc = l->opc;

    reloc_pc14(l->label_ptr[0], s->code_ptr);

    ir = TCG_REG_R4;
    if (TARGET_LONG_BITS == 32) {
        tcg_out_mov(s, TCG_TYPE_I32, ir++, l->addrlo_reg);
    } else {
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        ir |= 1;
#endif
        tcg_out_mov(s, TCG_TYPE_I32, ir++, l->addrhi_reg);
        tcg_out_mov(s, TCG_TYPE_I32, ir++, l->addrlo_reg);
    }
    tcg_out_movi(s, TCG_TYPE_I32, ir++, l->mem_index);
    tcg_out32(s, MFSPR | RT(ir++) | LR);
    tcg_out_b(s, LK, ld_trampolines[opc & ~MO_SIGN]);

    datalo = l->datalo_reg;
    switch (opc & MO_SSIZE) {
    case MO_SB:
        tcg_out32(s, EXTSB | RA(datalo) | RS(TCG_REG_R3));
        break;
    case MO_SW:
        tcg_out32(s, EXTSH | RA(datalo) | RS(TCG_REG_R3));
        break;
    default:
        tcg_out_mov(s, TCG_TYPE_I32, datalo, TCG_REG_R3);
        break;
    case MO_Q:
        datahi = l->datahi_reg;
        if (datalo != TCG_REG_R3) {
            tcg_out_mov(s, TCG_TYPE_I32, datalo, TCG_REG_R4);
            tcg_out_mov(s, TCG_TYPE_I32, datahi, TCG_REG_R3);
        } else if (datahi != TCG_REG_R4) {
            tcg_out_mov(s, TCG_TYPE_I32, datahi, TCG_REG_R3);
            tcg_out_mov(s, TCG_TYPE_I32, datalo, TCG_REG_R4);
        } else {
            tcg_out_mov(s, TCG_TYPE_I32, TCG_REG_R0, TCG_REG_R4);
            tcg_out_mov(s, TCG_TYPE_I32, datahi, TCG_REG_R3);
            tcg_out_mov(s, TCG_TYPE_I32, datalo, TCG_REG_R0);
        }
        break;
    }
    tcg_out_b(s, 0, l->raddr);
}

static void tcg_out_qemu_st_slow_path(TCGContext *s, TCGLabelQemuLdst *l)
{
    TCGReg ir, datalo;
    TCGMemOp opc = l->opc;

    reloc_pc14(l->label_ptr[0], s->code_ptr);

    ir = TCG_REG_R4;
    if (TARGET_LONG_BITS == 32) {
        tcg_out_mov (s, TCG_TYPE_I32, ir++, l->addrlo_reg);
    } else {
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        ir |= 1;
#endif
        tcg_out_mov (s, TCG_TYPE_I32, ir++, l->addrhi_reg);
        tcg_out_mov (s, TCG_TYPE_I32, ir++, l->addrlo_reg);
    }

    datalo = l->datalo_reg;
    switch (opc & MO_SIZE) {
    case MO_8:
        tcg_out32(s, (RLWINM | RA (ir) | RS (datalo)
                      | SH (0) | MB (24) | ME (31)));
        break;
    case MO_16:
        tcg_out32(s, (RLWINM | RA (ir) | RS (datalo)
                      | SH (0) | MB (16) | ME (31)));
        break;
    default:
        tcg_out_mov(s, TCG_TYPE_I32, ir, datalo);
        break;
    case MO_64:
#ifdef TCG_TARGET_CALL_ALIGN_ARGS
        ir |= 1;
#endif
        tcg_out_mov(s, TCG_TYPE_I32, ir++, l->datahi_reg);
        tcg_out_mov(s, TCG_TYPE_I32, ir, datalo);
        break;
    }
    ir++;

    tcg_out_movi(s, TCG_TYPE_I32, ir++, l->mem_index);
    tcg_out32(s, MFSPR | RT(ir++) | LR);
    tcg_out_b(s, LK, st_trampolines[opc]);
    tcg_out_b(s, 0, l->raddr);
}
#endif

#ifdef CONFIG_SOFTMMU
static void emit_ldst_trampoline(TCGContext *s, void *ptr)
{
    tcg_out_mov(s, TCG_TYPE_PTR, TCG_REG_R3, TCG_AREG0);
    tcg_out_calli(s, ptr, 0);
}
#endif

static void tcg_target_qemu_prologue (TCGContext *s)
{
    int i, frame_size;

    frame_size = 0
        + LINKAGE_AREA_SIZE
        + TCG_STATIC_CALL_ARGS_SIZE
        + ARRAY_SIZE (tcg_target_callee_save_regs) * 4
        + CPU_TEMP_BUF_NLONGS * sizeof(long)
        ;
    frame_size = (frame_size + 15) & ~15;

    tcg_set_frame(s, TCG_REG_CALL_STACK, frame_size
                  - CPU_TEMP_BUF_NLONGS * sizeof(long),
                  CPU_TEMP_BUF_NLONGS * sizeof(long));

#ifdef _CALL_AIX
    {
        uintptr_t addr;

        /* First emit adhoc function descriptor */
        addr = (uintptr_t)s->code_ptr + 12;
        tcg_out32(s, addr);        /* entry point */
        tcg_out32(s, 0);           /* toc */
        tcg_out32(s, 0);           /* environment pointer */
    }
#endif
    tcg_out32 (s, MFSPR | RT (0) | LR);
    tcg_out32 (s, STWU | RS (1) | RA (1) | (-frame_size & 0xffff));
    for (i = 0; i < ARRAY_SIZE (tcg_target_callee_save_regs); ++i)
        tcg_out32 (s, (STW
                       | RS (tcg_target_callee_save_regs[i])
                       | RA (1)
                       | (i * 4 + LINKAGE_AREA_SIZE + TCG_STATIC_CALL_ARGS_SIZE)
                       )
            );
    tcg_out32 (s, STW | RS (0) | RA (1) | (frame_size + LR_OFFSET));

#ifdef CONFIG_USE_GUEST_BASE
    if (GUEST_BASE) {
        tcg_out_movi (s, TCG_TYPE_I32, TCG_GUEST_BASE_REG, GUEST_BASE);
        tcg_regset_set_reg(s->reserved_regs, TCG_GUEST_BASE_REG);
    }
#endif

    tcg_out_mov (s, TCG_TYPE_PTR, TCG_AREG0, tcg_target_call_iarg_regs[0]);
    tcg_out32 (s, MTSPR | RS (tcg_target_call_iarg_regs[1]) | CTR);
    tcg_out32 (s, BCCTR | BO_ALWAYS);
    tb_ret_addr = s->code_ptr;

    for (i = 0; i < ARRAY_SIZE (tcg_target_callee_save_regs); ++i)
        tcg_out32 (s, (LWZ
                       | RT (tcg_target_callee_save_regs[i])
                       | RA (1)
                       | (i * 4 + LINKAGE_AREA_SIZE + TCG_STATIC_CALL_ARGS_SIZE)
                       )
            );
    tcg_out32 (s, LWZ | RT (0) | RA (1) | (frame_size + LR_OFFSET));
    tcg_out32 (s, MTSPR | RS (0) | LR);
    tcg_out32 (s, ADDI | RT (1) | RA (1) | frame_size);
    tcg_out32 (s, BCLR | BO_ALWAYS);

#ifdef CONFIG_SOFTMMU
    for (i = 0; i < 16; ++i) {
        if (qemu_ld_helpers[i]) {
            ld_trampolines[i] = s->code_ptr;
            emit_ldst_trampoline(s, qemu_ld_helpers[i]);
        }
        if (qemu_st_helpers[i]) {
            st_trampolines[i] = s->code_ptr;
            emit_ldst_trampoline(s, qemu_st_helpers[i]);
        }
    }
#endif
}

static void tcg_out_ld(TCGContext *s, TCGType type, TCGReg ret, TCGReg arg1,
                       intptr_t arg2)
{
    tcg_out_ldst (s, ret, arg1, arg2, LWZ, LWZX);
}

static void tcg_out_st(TCGContext *s, TCGType type, TCGReg arg, TCGReg arg1,
                       intptr_t arg2)
{
    tcg_out_ldst (s, arg, arg1, arg2, STW, STWX);
}

static void ppc_addi (TCGContext *s, int rt, int ra, tcg_target_long si)
{
    if (!si && rt == ra)
        return;

    if (si == (int16_t) si)
        tcg_out32 (s, ADDI | RT (rt) | RA (ra) | (si & 0xffff));
    else {
        uint16_t h = ((si >> 16) & 0xffff) + ((uint16_t) si >> 15);
        tcg_out32 (s, ADDIS | RT (rt) | RA (ra) | h);
        tcg_out32 (s, ADDI | RT (rt) | RA (rt) | (si & 0xffff));
    }
}

static void tcg_out_cmp (TCGContext *s, int cond, TCGArg arg1, TCGArg arg2,
                         int const_arg2, int cr)
{
    int imm;
    uint32_t op;

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        if (const_arg2) {
            if ((int16_t) arg2 == arg2) {
                op = CMPI;
                imm = 1;
                break;
            }
            else if ((uint16_t) arg2 == arg2) {
                op = CMPLI;
                imm = 1;
                break;
            }
        }
        op = CMPL;
        imm = 0;
        break;

    case TCG_COND_LT:
    case TCG_COND_GE:
    case TCG_COND_LE:
    case TCG_COND_GT:
        if (const_arg2) {
            if ((int16_t) arg2 == arg2) {
                op = CMPI;
                imm = 1;
                break;
            }
        }
        op = CMP;
        imm = 0;
        break;

    case TCG_COND_LTU:
    case TCG_COND_GEU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
        if (const_arg2) {
            if ((uint16_t) arg2 == arg2) {
                op = CMPLI;
                imm = 1;
                break;
            }
        }
        op = CMPL;
        imm = 0;
        break;

    default:
        tcg_abort ();
    }
    op |= BF (cr);

    if (imm)
        tcg_out32 (s, op | RA (arg1) | (arg2 & 0xffff));
    else {
        if (const_arg2) {
            tcg_out_movi (s, TCG_TYPE_I32, 0, arg2);
            tcg_out32 (s, op | RA (arg1) | RB (0));
        }
        else
            tcg_out32 (s, op | RA (arg1) | RB (arg2));
    }

}

static void tcg_out_bc(TCGContext *s, int bc, int label_index)
{
    TCGLabel *l = &s->labels[label_index];

    if (l->has_value) {
        tcg_out32(s, bc | reloc_pc14_val(s->code_ptr, l->u.value_ptr));
    } else {
        /* Thanks to Andrzej Zaborowski */
        tcg_insn_unit retrans = *s->code_ptr & 0xfffc;
        tcg_out_reloc(s, s->code_ptr, R_PPC_REL14, label_index, 0);
        tcg_out32(s, bc | retrans);
    }
}

static void tcg_out_cr7eq_from_cond (TCGContext *s, const TCGArg *args,
                                     const int *const_args)
{
    TCGCond cond = args[4];
    int op;
    struct { int bit1; int bit2; int cond2; } bits[] = {
        [TCG_COND_LT ] = { CR_LT, CR_LT, TCG_COND_LT  },
        [TCG_COND_LE ] = { CR_LT, CR_GT, TCG_COND_LT  },
        [TCG_COND_GT ] = { CR_GT, CR_GT, TCG_COND_GT  },
        [TCG_COND_GE ] = { CR_GT, CR_LT, TCG_COND_GT  },
        [TCG_COND_LTU] = { CR_LT, CR_LT, TCG_COND_LTU },
        [TCG_COND_LEU] = { CR_LT, CR_GT, TCG_COND_LTU },
        [TCG_COND_GTU] = { CR_GT, CR_GT, TCG_COND_GTU },
        [TCG_COND_GEU] = { CR_GT, CR_LT, TCG_COND_GTU },
    }, *b = &bits[cond];

    switch (cond) {
    case TCG_COND_EQ:
    case TCG_COND_NE:
        op = (cond == TCG_COND_EQ) ? CRAND : CRNAND;
        tcg_out_cmp (s, cond, args[0], args[2], const_args[2], 6);
        tcg_out_cmp (s, cond, args[1], args[3], const_args[3], 7);
        tcg_out32 (s, op | BT (7, CR_EQ) | BA (6, CR_EQ) | BB (7, CR_EQ));
        break;
    case TCG_COND_LT:
    case TCG_COND_LE:
    case TCG_COND_GT:
    case TCG_COND_GE:
    case TCG_COND_LTU:
    case TCG_COND_LEU:
    case TCG_COND_GTU:
    case TCG_COND_GEU:
        op = (b->bit1 != b->bit2) ? CRANDC : CRAND;
        tcg_out_cmp (s, b->cond2, args[1], args[3], const_args[3], 5);
        tcg_out_cmp (s, tcg_unsigned_cond (cond), args[0], args[2],
                     const_args[2], 7);
        tcg_out32 (s, op | BT (7, CR_EQ) | BA (5, CR_EQ) | BB (7, b->bit2));
        tcg_out32 (s, CROR | BT (7, CR_EQ) | BA (5, b->bit1) | BB (7, CR_EQ));
        break;
    default:
        tcg_abort();
    }
}

static void tcg_out_setcond (TCGContext *s, TCGCond cond, TCGArg arg0,
                             TCGArg arg1, TCGArg arg2, int const_arg2)
{
    int crop, sh, arg;

    switch (cond) {
    case TCG_COND_EQ:
        if (const_arg2) {
            if (!arg2) {
                arg = arg1;
            }
            else {
                arg = 0;
                if ((uint16_t) arg2 == arg2) {
                    tcg_out32 (s, XORI | RS (arg1) | RA (0) | arg2);
                }
                else {
                    tcg_out_movi (s, TCG_TYPE_I32, 0, arg2);
                    tcg_out32 (s, XOR | SAB (arg1, 0, 0));
                }
            }
        }
        else {
            arg = 0;
            tcg_out32 (s, XOR | SAB (arg1, 0, arg2));
        }
        tcg_out32 (s, CNTLZW | RS (arg) | RA (0));
        tcg_out32 (s, (RLWINM
                       | RA (arg0)
                       | RS (0)
                       | SH (27)
                       | MB (5)
                       | ME (31)
                       )
            );
        break;

    case TCG_COND_NE:
        if (const_arg2) {
            if (!arg2) {
                arg = arg1;
            }
            else {
                arg = 0;
                if ((uint16_t) arg2 == arg2) {
                    tcg_out32 (s, XORI | RS (arg1) | RA (0) | arg2);
                }
                else {
                    tcg_out_movi (s, TCG_TYPE_I32, 0, arg2);
                    tcg_out32 (s, XOR | SAB (arg1, 0, 0));
                }
            }
        }
        else {
            arg = 0;
            tcg_out32 (s, XOR | SAB (arg1, 0, arg2));
        }

        if (arg == arg1 && arg1 == arg0) {
            tcg_out32 (s, ADDIC | RT (0) | RA (arg) | 0xffff);
            tcg_out32 (s, SUBFE | TAB (arg0, 0, arg));
        }
        else {
            tcg_out32 (s, ADDIC | RT (arg0) | RA (arg) | 0xffff);
            tcg_out32 (s, SUBFE | TAB (arg0, arg0, arg));
        }
        break;

    case TCG_COND_GT:
    case TCG_COND_GTU:
        sh = 30;
        crop = 0;
        goto crtest;

    case TCG_COND_LT:
    case TCG_COND_LTU:
        sh = 29;
        crop = 0;
        goto crtest;

    case TCG_COND_GE:
    case TCG_COND_GEU:
        sh = 31;
        crop = CRNOR | BT (7, CR_EQ) | BA (7, CR_LT) | BB (7, CR_LT);
        goto crtest;

    case TCG_COND_LE:
    case TCG_COND_LEU:
        sh = 31;
        crop = CRNOR | BT (7, CR_EQ) | BA (7, CR_GT) | BB (7, CR_GT);
    crtest:
        tcg_out_cmp (s, cond, arg1, arg2, const_arg2, 7);
        if (crop) tcg_out32 (s, crop);
        tcg_out32 (s, MFCR | RT (0));
        tcg_out32 (s, (RLWINM
                       | RA (arg0)
                       | RS (0)
                       | SH (sh)
                       | MB (31)
                       | ME (31)
                       )
            );
        break;

    default:
        tcg_abort ();
    }
}

static void tcg_out_setcond2 (TCGContext *s, const TCGArg *args,
                              const int *const_args)
{
    tcg_out_cr7eq_from_cond (s, args + 1, const_args + 1);
    tcg_out32 (s, MFCR | RT (0));
    tcg_out32 (s, (RLWINM
                   | RA (args[0])
                   | RS (0)
                   | SH (31)
                   | MB (31)
                   | ME (31)
                   )
        );
}

static void tcg_out_movcond (TCGContext *s, TCGCond cond,
                             TCGArg dest,
                             TCGArg c1, TCGArg c2,
                             TCGArg v1, TCGArg v2,
                             int const_c2)
{
    tcg_out_cmp (s, cond, c1, c2, const_c2, 7);

    if (1) {
        /* At least here on 7747A bit twiddling hacks are outperformed
           by jumpy code (the testing was not scientific) */
        if (dest == v2) {
            cond = tcg_invert_cond (cond);
            v2 = v1;
        }
        else {
            if (dest != v1) {
                tcg_out_mov (s, TCG_TYPE_I32, dest, v1);
            }
        }
        /* Branch forward over one insn */
        tcg_out32 (s, tcg_to_bc[cond] | 8);
        tcg_out_mov (s, TCG_TYPE_I32, dest, v2);
    }
    else {
        /* isel version, "if (1)" above should be replaced once a way
           to figure out availability of isel on the underlying
           hardware is found */
        int tab, bc;

        switch (cond) {
        case TCG_COND_EQ:
            tab = TAB (dest, v1, v2);
            bc = CR_EQ;
            break;
        case TCG_COND_NE:
            tab = TAB (dest, v2, v1);
            bc = CR_EQ;
            break;
        case TCG_COND_LTU:
        case TCG_COND_LT:
            tab = TAB (dest, v1, v2);
            bc = CR_LT;
            break;
        case TCG_COND_GEU:
        case TCG_COND_GE:
            tab = TAB (dest, v2, v1);
            bc = CR_LT;
            break;
        case TCG_COND_LEU:
        case TCG_COND_LE:
            tab = TAB (dest, v2, v1);
            bc = CR_GT;
            break;
        case TCG_COND_GTU:
        case TCG_COND_GT:
            tab = TAB (dest, v1, v2);
            bc = CR_GT;
            break;
        default:
            tcg_abort ();
        }
        tcg_out32 (s, ISEL | tab | ((bc + 28) << 6));
    }
}

static void tcg_out_brcond (TCGContext *s, TCGCond cond,
                            TCGArg arg1, TCGArg arg2, int const_arg2,
                            int label_index)
{
    tcg_out_cmp (s, cond, arg1, arg2, const_arg2, 7);
    tcg_out_bc (s, tcg_to_bc[cond], label_index);
}

/* XXX: we implement it at the target level to avoid having to
   handle cross basic blocks temporaries */
static void tcg_out_brcond2 (TCGContext *s, const TCGArg *args,
                             const int *const_args)
{
    tcg_out_cr7eq_from_cond (s, args, const_args);
    tcg_out_bc (s, (BC | BI (7, CR_EQ) | BO_COND_TRUE), args[5]);
}

void ppc_tb_set_jmp_target (unsigned long jmp_addr, unsigned long addr)
{
    uint32_t *ptr;
    long disp = addr - jmp_addr;
    unsigned long patch_size;

    ptr = (uint32_t *)jmp_addr;

    if ((disp << 6) >> 6 != disp) {
        ptr[0] = 0x3c000000 | (addr >> 16);    /* lis 0,addr@ha */
        ptr[1] = 0x60000000 | (addr & 0xffff); /* la  0,addr@l(0) */
        ptr[2] = 0x7c0903a6;                   /* mtctr 0 */
        ptr[3] = 0x4e800420;                   /* brctr */
        patch_size = 16;
    } else {
        /* patch the branch destination */
        if (disp != 16) {
            *ptr = 0x48000000 | (disp & 0x03fffffc); /* b disp */
            patch_size = 4;
        } else {
            ptr[0] = 0x60000000; /* nop */
            ptr[1] = 0x60000000;
            ptr[2] = 0x60000000;
            ptr[3] = 0x60000000;
            patch_size = 16;
        }
    }
    /* flush icache */
    flush_icache_range(jmp_addr, jmp_addr + patch_size);
}

static void tcg_out_op(TCGContext *s, TCGOpcode opc, const TCGArg *args,
                       const int *const_args)
{
    switch (opc) {
    case INDEX_op_exit_tb:
        tcg_out_movi(s, TCG_TYPE_I32, TCG_REG_R3, args[0]);
        tcg_out_b(s, 0, tb_ret_addr);
        break;
    case INDEX_op_goto_tb:
        if (s->tb_jmp_offset) {
            /* direct jump method */
            s->tb_jmp_offset[args[0]] = tcg_current_code_size(s);
            s->code_ptr += 4;
        } else {
            tcg_abort ();
        }
        s->tb_next_offset[args[0]] = tcg_current_code_size(s);
        break;
    case INDEX_op_br:
        {
            TCGLabel *l = &s->labels[args[0]];

            if (l->has_value) {
                tcg_out_b(s, 0, l->u.value_ptr);
            } else {
                /* Thanks to Andrzej Zaborowski */
                tcg_insn_unit retrans = *s->code_ptr & 0x3fffffc;
                tcg_out_reloc(s, s->code_ptr, R_PPC_REL24, args[0], 0);
                tcg_out32(s, B | retrans);
            }
        }
        break;
    case INDEX_op_call:
        if (const_args[0]) {
            tcg_out_calli(s, (void *)(uintptr_t)args[0], LK);
        } else {
            tcg_out_callr(s, args[0], LK);
        }
        break;
    case INDEX_op_movi_i32:
        tcg_out_movi(s, TCG_TYPE_I32, args[0], args[1]);
        break;
    case INDEX_op_ld8u_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], LBZ, LBZX);
        break;
    case INDEX_op_ld8s_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], LBZ, LBZX);
        tcg_out32 (s, EXTSB | RS (args[0]) | RA (args[0]));
        break;
    case INDEX_op_ld16u_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], LHZ, LHZX);
        break;
    case INDEX_op_ld16s_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], LHA, LHAX);
        break;
    case INDEX_op_ld_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], LWZ, LWZX);
        break;
    case INDEX_op_st8_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], STB, STBX);
        break;
    case INDEX_op_st16_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], STH, STHX);
        break;
    case INDEX_op_st_i32:
        tcg_out_ldst (s, args[0], args[1], args[2], STW, STWX);
        break;

    case INDEX_op_add_i32:
        if (const_args[2])
            ppc_addi (s, args[0], args[1], args[2]);
        else
            tcg_out32 (s, ADD | TAB (args[0], args[1], args[2]));
        break;
    case INDEX_op_sub_i32:
        if (const_args[2])
            ppc_addi (s, args[0], args[1], -args[2]);
        else
            tcg_out32 (s, SUBF | TAB (args[0], args[2], args[1]));
        break;

    case INDEX_op_and_i32:
        if (const_args[2]) {
            uint32_t c;

            c = args[2];

            if (!c) {
                tcg_out_movi (s, TCG_TYPE_I32, args[0], 0);
                break;
            }
#ifdef __PPU__
            uint32_t t, n;
            int mb, me;

            n = c ^ -(c & 1);
            t = n + (n & -n);

            if ((t & (t - 1)) == 0) {
                int lzc, tzc;

                if ((c & 0x80000001) == 0x80000001) {
                    lzc = clz32 (n);
                    tzc = ctz32 (n);

                    mb = 32 - tzc;
                    me = lzc - 1;
                }
                else {
                    lzc = clz32 (c);
                    tzc = ctz32 (c);

                    mb = lzc;
                    me = 31 - tzc;
                }

                tcg_out32 (s, (RLWINM
                               | RA (args[0])
                               | RS (args[1])
                               | SH (0)
                               | MB (mb)
                               | ME (me)
                               )
                    );
            }
            else
#endif /* !__PPU__ */
            {
                if ((c & 0xffff) == c)
                    tcg_out32 (s, ANDI | RS (args[1]) | RA (args[0]) | c);
                else if ((c & 0xffff0000) == c)
                    tcg_out32 (s, ANDIS | RS (args[1]) | RA (args[0])
                               | ((c >> 16) & 0xffff));
                else {
                    tcg_out_movi (s, TCG_TYPE_I32, 0, c);
                    tcg_out32 (s, AND | SAB (args[1], args[0], 0));
                }
            }
        }
        else
            tcg_out32 (s, AND | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_or_i32:
        if (const_args[2]) {
            if (args[2] & 0xffff) {
                tcg_out32 (s, ORI | RS (args[1])  | RA (args[0])
                           | (args[2] & 0xffff));
                if (args[2] >> 16)
                    tcg_out32 (s, ORIS | RS (args[0])  | RA (args[0])
                               | ((args[2] >> 16) & 0xffff));
            }
            else {
                tcg_out32 (s, ORIS | RS (args[1])  | RA (args[0])
                           | ((args[2] >> 16) & 0xffff));
            }
        }
        else
            tcg_out32 (s, OR | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_xor_i32:
        if (const_args[2]) {
            if ((args[2] & 0xffff) == args[2])
                tcg_out32 (s, XORI | RS (args[1])  | RA (args[0])
                           | (args[2] & 0xffff));
            else if ((args[2] & 0xffff0000) == args[2])
                tcg_out32 (s, XORIS | RS (args[1])  | RA (args[0])
                           | ((args[2] >> 16) & 0xffff));
            else {
                tcg_out_movi (s, TCG_TYPE_I32, 0, args[2]);
                tcg_out32 (s, XOR | SAB (args[1], args[0], 0));
            }
        }
        else
            tcg_out32 (s, XOR | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_andc_i32:
        tcg_out32 (s, ANDC | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_orc_i32:
        tcg_out32 (s, ORC | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_eqv_i32:
        tcg_out32 (s, EQV | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_nand_i32:
        tcg_out32 (s, NAND | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_nor_i32:
        tcg_out32 (s, NOR | SAB (args[1], args[0], args[2]));
        break;

    case INDEX_op_mul_i32:
        if (const_args[2]) {
            if (args[2] == (int16_t) args[2])
                tcg_out32 (s, MULLI | RT (args[0]) | RA (args[1])
                           | (args[2] & 0xffff));
            else {
                tcg_out_movi (s, TCG_TYPE_I32, 0, args[2]);
                tcg_out32 (s, MULLW | TAB (args[0], args[1], 0));
            }
        }
        else
            tcg_out32 (s, MULLW | TAB (args[0], args[1], args[2]));
        break;

    case INDEX_op_div_i32:
        tcg_out32 (s, DIVW | TAB (args[0], args[1], args[2]));
        break;

    case INDEX_op_divu_i32:
        tcg_out32 (s, DIVWU | TAB (args[0], args[1], args[2]));
        break;

    case INDEX_op_mulu2_i32:
        if (args[0] == args[2] || args[0] == args[3]) {
            tcg_out32 (s, MULLW | TAB (0, args[2], args[3]));
            tcg_out32 (s, MULHWU | TAB (args[1], args[2], args[3]));
            tcg_out_mov (s, TCG_TYPE_I32, args[0], 0);
        }
        else {
            tcg_out32 (s, MULLW | TAB (args[0], args[2], args[3]));
            tcg_out32 (s, MULHWU | TAB (args[1], args[2], args[3]));
        }
        break;

    case INDEX_op_shl_i32:
        if (const_args[2]) {
            tcg_out32 (s, (RLWINM
                           | RA (args[0])
                           | RS (args[1])
                           | SH (args[2])
                           | MB (0)
                           | ME (31 - args[2])
                           )
                );
        }
        else
            tcg_out32 (s, SLW | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_shr_i32:
        if (const_args[2]) {
            tcg_out32 (s, (RLWINM
                           | RA (args[0])
                           | RS (args[1])
                           | SH (32 - args[2])
                           | MB (args[2])
                           | ME (31)
                           )
                );
        }
        else
            tcg_out32 (s, SRW | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_sar_i32:
        if (const_args[2])
            tcg_out32 (s, SRAWI | RS (args[1]) | RA (args[0]) | SH (args[2]));
        else
            tcg_out32 (s, SRAW | SAB (args[1], args[0], args[2]));
        break;
    case INDEX_op_rotl_i32:
        {
            int op = 0
                | RA (args[0])
                | RS (args[1])
                | MB (0)
                | ME (31)
                | (const_args[2] ? RLWINM | SH (args[2])
                                 : RLWNM | RB (args[2]))
                ;
            tcg_out32 (s, op);
        }
        break;
    case INDEX_op_rotr_i32:
        if (const_args[2]) {
            if (!args[2]) {
                tcg_out_mov (s, TCG_TYPE_I32, args[0], args[1]);
            }
            else {
                tcg_out32 (s, RLWINM
                           | RA (args[0])
                           | RS (args[1])
                           | SH (32 - args[2])
                           | MB (0)
                           | ME (31)
                    );
            }
        }
        else {
            tcg_out32 (s, SUBFIC | RT (0) | RA (args[2]) | 32);
            tcg_out32 (s, RLWNM
                       | RA (args[0])
                       | RS (args[1])
                       | RB (0)
                       | MB (0)
                       | ME (31)
                );
        }
        break;

    case INDEX_op_add2_i32:
        if (args[0] == args[3] || args[0] == args[5]) {
            tcg_out32 (s, ADDC | TAB (0, args[2], args[4]));
            tcg_out32 (s, ADDE | TAB (args[1], args[3], args[5]));
            tcg_out_mov (s, TCG_TYPE_I32, args[0], 0);
        }
        else {
            tcg_out32 (s, ADDC | TAB (args[0], args[2], args[4]));
            tcg_out32 (s, ADDE | TAB (args[1], args[3], args[5]));
        }
        break;
    case INDEX_op_sub2_i32:
        if (args[0] == args[3] || args[0] == args[5]) {
            tcg_out32 (s, SUBFC | TAB (0, args[4], args[2]));
            tcg_out32 (s, SUBFE | TAB (args[1], args[5], args[3]));
            tcg_out_mov (s, TCG_TYPE_I32, args[0], 0);
        }
        else {
            tcg_out32 (s, SUBFC | TAB (args[0], args[4], args[2]));
            tcg_out32 (s, SUBFE | TAB (args[1], args[5], args[3]));
        }
        break;

    case INDEX_op_brcond_i32:
        /*
          args[0] = r0
          args[1] = r1
          args[2] = cond
          args[3] = r1 is const
          args[4] = label_index
        */
        tcg_out_brcond (s, args[2], args[0], args[1], const_args[1], args[3]);
        break;
    case INDEX_op_brcond2_i32:
        tcg_out_brcond2(s, args, const_args);
        break;

    case INDEX_op_neg_i32:
        tcg_out32 (s, NEG | RT (args[0]) | RA (args[1]));
        break;

    case INDEX_op_not_i32:
        tcg_out32 (s, NOR | SAB (args[1], args[0], args[1]));
        break;

    case INDEX_op_qemu_ld_i32:
        tcg_out_qemu_ld(s, args, 0);
        break;
    case INDEX_op_qemu_ld_i64:
        tcg_out_qemu_ld(s, args, 1);
        break;
    case INDEX_op_qemu_st_i32:
        tcg_out_qemu_st(s, args, 0);
        break;
    case INDEX_op_qemu_st_i64:
        tcg_out_qemu_st(s, args, 1);
        break;

    case INDEX_op_ext8s_i32:
        tcg_out32 (s, EXTSB | RS (args[1]) | RA (args[0]));
        break;
    case INDEX_op_ext8u_i32:
        tcg_out32 (s, RLWINM
                   | RA (args[0])
                   | RS (args[1])
                   | SH (0)
                   | MB (24)
                   | ME (31)
            );
        break;
    case INDEX_op_ext16s_i32:
        tcg_out32 (s, EXTSH | RS (args[1]) | RA (args[0]));
        break;
    case INDEX_op_ext16u_i32:
        tcg_out32 (s, RLWINM
                   | RA (args[0])
                   | RS (args[1])
                   | SH (0)
                   | MB (16)
                   | ME (31)
            );
        break;

    case INDEX_op_setcond_i32:
        tcg_out_setcond (s, args[3], args[0], args[1], args[2], const_args[2]);
        break;
    case INDEX_op_setcond2_i32:
        tcg_out_setcond2 (s, args, const_args);
        break;

    case INDEX_op_bswap16_i32:
        /* Stolen from gcc's builtin_bswap16 */

        /* a1 = abcd */

        /* r0 = (a1 << 8) & 0xff00 # 00d0 */
        tcg_out32 (s, RLWINM
                   | RA (0)
                   | RS (args[1])
                   | SH (8)
                   | MB (16)
                   | ME (23)
            );

        /* a0 = rotate_left (a1, 24) & 0xff # 000c */
        tcg_out32 (s, RLWINM
                   | RA (args[0])
                   | RS (args[1])
                   | SH (24)
                   | MB (24)
                   | ME (31)
            );

        /* a0 = a0 | r0 # 00dc */
        tcg_out32 (s, OR | SAB (0, args[0], args[0]));
        break;

    case INDEX_op_bswap32_i32:
        /* Stolen from gcc's builtin_bswap32 */
        {
            int a0 = args[0];

            /* a1 = args[1] # abcd */

            if (a0 == args[1]) {
                a0 = 0;
            }

            /* a0 = rotate_left (a1, 8) # bcda */
            tcg_out32 (s, RLWINM
                       | RA (a0)
                       | RS (args[1])
                       | SH (8)
                       | MB (0)
                       | ME (31)
                );

            /* a0 = (a0 & ~0xff000000) | ((a1 << 24) & 0xff000000) # dcda */
            tcg_out32 (s, RLWIMI
                       | RA (a0)
                       | RS (args[1])
                       | SH (24)
                       | MB (0)
                       | ME (7)
                );

            /* a0 = (a0 & ~0x0000ff00) | ((a1 << 24) & 0x0000ff00) # dcba */
            tcg_out32 (s, RLWIMI
                       | RA (a0)
                       | RS (args[1])
                       | SH (24)
                       | MB (16)
                       | ME (23)
                );

            if (!a0) {
                tcg_out_mov (s, TCG_TYPE_I32, args[0], a0);
            }
        }
        break;

    case INDEX_op_deposit_i32:
        tcg_out32 (s, RLWIMI
                   | RA (args[0])
                   | RS (args[2])
                   | SH (args[3])
                   | MB (32 - args[3] - args[4])
                   | ME (31 - args[3])
            );
        break;

    case INDEX_op_movcond_i32:
        tcg_out_movcond (s, args[5], args[0],
                         args[1], args[2],
                         args[3], args[4],
                         const_args[2]);
        break;

    default:
        tcg_dump_ops (s);
        tcg_abort ();
    }
}

static const TCGTargetOpDef ppc_op_defs[] = {
    { INDEX_op_exit_tb, { } },
    { INDEX_op_goto_tb, { } },
    { INDEX_op_call, { "ri" } },
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

    { INDEX_op_add_i32, { "r", "r", "ri" } },
    { INDEX_op_mul_i32, { "r", "r", "ri" } },
    { INDEX_op_div_i32, { "r", "r", "r" } },
    { INDEX_op_divu_i32, { "r", "r", "r" } },
    { INDEX_op_mulu2_i32, { "r", "r", "r", "r" } },
    { INDEX_op_sub_i32, { "r", "r", "ri" } },
    { INDEX_op_and_i32, { "r", "r", "ri" } },
    { INDEX_op_or_i32, { "r", "r", "ri" } },
    { INDEX_op_xor_i32, { "r", "r", "ri" } },

    { INDEX_op_shl_i32, { "r", "r", "ri" } },
    { INDEX_op_shr_i32, { "r", "r", "ri" } },
    { INDEX_op_sar_i32, { "r", "r", "ri" } },

    { INDEX_op_rotl_i32, { "r", "r", "ri" } },
    { INDEX_op_rotr_i32, { "r", "r", "ri" } },

    { INDEX_op_brcond_i32, { "r", "ri" } },

    { INDEX_op_add2_i32, { "r", "r", "r", "r", "r", "r" } },
    { INDEX_op_sub2_i32, { "r", "r", "r", "r", "r", "r" } },
    { INDEX_op_brcond2_i32, { "r", "r", "r", "r" } },

    { INDEX_op_neg_i32, { "r", "r" } },
    { INDEX_op_not_i32, { "r", "r" } },

    { INDEX_op_andc_i32, { "r", "r", "r" } },
    { INDEX_op_orc_i32, { "r", "r", "r" } },
    { INDEX_op_eqv_i32, { "r", "r", "r" } },
    { INDEX_op_nand_i32, { "r", "r", "r" } },
    { INDEX_op_nor_i32, { "r", "r", "r" } },

    { INDEX_op_setcond_i32, { "r", "r", "ri" } },
    { INDEX_op_setcond2_i32, { "r", "r", "r", "ri", "ri" } },

    { INDEX_op_bswap16_i32, { "r", "r" } },
    { INDEX_op_bswap32_i32, { "r", "r" } },

#if TARGET_LONG_BITS == 32
    { INDEX_op_qemu_ld_i32, { "r", "L" } },
    { INDEX_op_qemu_ld_i64, { "L", "L", "L" } },
    { INDEX_op_qemu_st_i32, { "K", "K" } },
    { INDEX_op_qemu_st_i64, { "M", "M", "M" } },
#else
    { INDEX_op_qemu_ld_i32, { "r", "L", "L" } },
    { INDEX_op_qemu_ld_i64, { "L", "L", "L", "L" } },
    { INDEX_op_qemu_st_i32, { "K", "K", "K" } },
    { INDEX_op_qemu_st_i64, { "M", "M", "M", "M" } },
#endif

    { INDEX_op_ext8s_i32, { "r", "r" } },
    { INDEX_op_ext8u_i32, { "r", "r" } },
    { INDEX_op_ext16s_i32, { "r", "r" } },
    { INDEX_op_ext16u_i32, { "r", "r" } },

    { INDEX_op_deposit_i32, { "r", "0", "r" } },
    { INDEX_op_movcond_i32, { "r", "r", "ri", "r", "r" } },

    { -1 },
};

static void tcg_target_init(TCGContext *s)
{
    tcg_regset_set32(tcg_target_available_regs[TCG_TYPE_I32], 0, 0xffffffff);
    tcg_regset_set32(tcg_target_call_clobber_regs, 0,
                     (1 << TCG_REG_R0) |
#ifdef TCG_TARGET_CALL_DARWIN
                     (1 << TCG_REG_R2) |
#endif
                     (1 << TCG_REG_R3) |
                     (1 << TCG_REG_R4) |
                     (1 << TCG_REG_R5) |
                     (1 << TCG_REG_R6) |
                     (1 << TCG_REG_R7) |
                     (1 << TCG_REG_R8) |
                     (1 << TCG_REG_R9) |
                     (1 << TCG_REG_R10) |
                     (1 << TCG_REG_R11) |
                     (1 << TCG_REG_R12)
        );

    tcg_regset_clear(s->reserved_regs);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R0);
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R1);
#ifndef TCG_TARGET_CALL_DARWIN
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R2);
#endif
#ifdef _CALL_SYSV
    tcg_regset_set_reg(s->reserved_regs, TCG_REG_R13);
#endif

    tcg_add_target_add_op_defs(ppc_op_defs);
}
