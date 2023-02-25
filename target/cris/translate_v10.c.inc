/*
 *  CRISv10 emulation for qemu: main translation routines.
 *
 *  Copyright (c) 2010 AXIS Communications AB
 *  Written by Edgar E. Iglesias.
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
#include "crisv10-decode.h"

static const char * const regnames_v10[] =
{
    "$r0", "$r1", "$r2", "$r3",
    "$r4", "$r5", "$r6", "$r7",
    "$r8", "$r9", "$r10", "$r11",
    "$r12", "$r13", "$sp", "$pc",
};

static const char * const pregnames_v10[] =
{
    "$bz", "$vr", "$p2", "$p3",
    "$wz", "$ccr", "$p6-prefix", "$mof",
    "$dz", "$ibr", "$irp", "$srp",
    "$bar", "$dccr", "$brp", "$usp",
};

/* We need this table to handle preg-moves with implicit width.  */
static const int preg_sizes_v10[] = {
    1, /* bz.  */
    1, /* vr.  */
    1, /* pid. */
    1, /* srs. */
    2, /* wz.  */
    2, 2, 4,
    4, 4, 4, 4,
    4, 4, 4, 4,
};

static inline int dec10_size(unsigned int size)
{
    size++;
    if (size == 3)
        size++;
    return size;
}

static inline void cris_illegal_insn(DisasContext *dc)
{
    qemu_log_mask(LOG_GUEST_ERROR, "illegal insn at pc=%x\n", dc->pc);
    t_gen_raise_exception(EXCP_BREAK);
    dc->base.is_jmp = DISAS_NORETURN;
}

static void gen_store_v10_conditional(DisasContext *dc, TCGv addr, TCGv val,
                       unsigned int size, int mem_index)
{
    TCGLabel *l1 = gen_new_label();
    TCGv taddr = tcg_temp_new();
    TCGv tval = tcg_temp_new();
    TCGv t1 = tcg_temp_new();
    dc->postinc = 0;
    cris_evaluate_flags(dc);

    tcg_gen_mov_tl(taddr, addr);
    tcg_gen_mov_tl(tval, val);

    /* Store only if F flag isn't set */
    tcg_gen_andi_tl(t1, cpu_PR[PR_CCS], F_FLAG_V10);
    tcg_gen_brcondi_tl(TCG_COND_NE, t1, 0, l1);
    if (size == 1) {
        tcg_gen_qemu_st8(tval, taddr, mem_index);
    } else if (size == 2) {
        tcg_gen_qemu_st16(tval, taddr, mem_index);
    } else {
        tcg_gen_qemu_st32(tval, taddr, mem_index);
    }
    gen_set_label(l1);
    tcg_gen_shri_tl(t1, t1, 1);  /* shift F to P position */
    tcg_gen_or_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], t1); /*P=F*/
    tcg_temp_free(t1);
    tcg_temp_free(tval);
    tcg_temp_free(taddr);
}

static void gen_store_v10(DisasContext *dc, TCGv addr, TCGv val,
                       unsigned int size)
{
    int mem_index = cpu_mmu_index(&dc->cpu->env, false);

    /* If we get a fault on a delayslot we must keep the jmp state in
       the cpu-state to be able to re-execute the jmp.  */
    if (dc->delayed_branch == 1) {
        cris_store_direct_jmp(dc);
    }

    /* Conditional writes. */
    if (dc->flags_x) {
        gen_store_v10_conditional(dc, addr, val, size, mem_index);
        return;
    }

    if (size == 1) {
        tcg_gen_qemu_st8(val, addr, mem_index);
    } else if (size == 2) {
        tcg_gen_qemu_st16(val, addr, mem_index);
    } else {
        tcg_gen_qemu_st32(val, addr, mem_index);
    }
}


/* Prefix flag and register are used to handle the more complex
   addressing modes.  */
static void cris_set_prefix(DisasContext *dc)
{
    dc->clear_prefix = 0;
    dc->tb_flags |= PFIX_FLAG;
    tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], PFIX_FLAG);

    /* prefix insns don't clear the x flag.  */
    dc->clear_x = 0;
    cris_lock_irq(dc);
}

static void crisv10_prepare_memaddr(DisasContext *dc,
                                    TCGv addr, unsigned int size)
{
    if (dc->tb_flags & PFIX_FLAG) {
        tcg_gen_mov_tl(addr, cpu_PR[PR_PREFIX]);
    } else {
        tcg_gen_mov_tl(addr, cpu_R[dc->src]);
    }
}

static unsigned int crisv10_post_memaddr(DisasContext *dc, unsigned int size)
{
    unsigned int insn_len = 0;

    if (dc->tb_flags & PFIX_FLAG) {
        if (dc->mode == CRISV10_MODE_AUTOINC) {
            tcg_gen_mov_tl(cpu_R[dc->src], cpu_PR[PR_PREFIX]);
        }
    } else {
        if (dc->mode == CRISV10_MODE_AUTOINC) {
            if (dc->src == 15) {
                insn_len += size & ~1;
            } else {
                tcg_gen_addi_tl(cpu_R[dc->src], cpu_R[dc->src], size);
            }
        }
    }
    return insn_len;
}

static int dec10_prep_move_m(CPUCRISState *env, DisasContext *dc,
                             int s_ext, int memsize, TCGv dst)
{
    unsigned int rs;
    uint32_t imm;
    int is_imm;
    int insn_len = 0;

    rs = dc->src;
    is_imm = rs == 15 && !(dc->tb_flags & PFIX_FLAG);
    LOG_DIS("rs=%d rd=%d is_imm=%d mode=%d pfix=%d\n",
             rs, dc->dst, is_imm, dc->mode, dc->tb_flags & PFIX_FLAG);

    /* Load [$rs] onto T1.  */
    if (is_imm) {
        if (memsize != 4) {
            if (s_ext) {
                if (memsize == 1)
                    imm = cpu_ldsb_code(env, dc->pc + 2);
                else
                    imm = cpu_ldsw_code(env, dc->pc + 2);
            } else {
                if (memsize == 1)
                    imm = cpu_ldub_code(env, dc->pc + 2);
                else
                    imm = cpu_lduw_code(env, dc->pc + 2);
            }
        } else
            imm = cpu_ldl_code(env, dc->pc + 2);

        tcg_gen_movi_tl(dst, imm);

        if (dc->mode == CRISV10_MODE_AUTOINC) {
            insn_len += memsize;
            if (memsize == 1)
                insn_len++;
            tcg_gen_addi_tl(cpu_R[15], cpu_R[15], insn_len);
        }
    } else {
        TCGv addr;

        addr = tcg_temp_new();
        cris_flush_cc_state(dc);
        crisv10_prepare_memaddr(dc, addr, memsize);
        gen_load(dc, dst, addr, memsize, 0);
        if (s_ext)
            t_gen_sext(dst, dst, memsize);
        else
            t_gen_zext(dst, dst, memsize);
        insn_len += crisv10_post_memaddr(dc, memsize);
        tcg_temp_free(addr);
    }

    if (dc->mode == CRISV10_MODE_INDIRECT && (dc->tb_flags & PFIX_FLAG)) {
        dc->dst = dc->src;
    }
    return insn_len;
}

static unsigned int dec10_quick_imm(DisasContext *dc)
{
    int32_t imm, simm;
    int op;
    TCGv c;

    /* sign extend.  */
    imm = dc->ir & ((1 << 6) - 1);
    simm = (int8_t) (imm << 2);
    simm >>= 2;
    switch (dc->opcode) {
        case CRISV10_QIMM_BDAP_R0:
        case CRISV10_QIMM_BDAP_R1:
        case CRISV10_QIMM_BDAP_R2:
        case CRISV10_QIMM_BDAP_R3:
            simm = (int8_t)dc->ir;
            LOG_DIS("bdap %d $r%d\n", simm, dc->dst);
            LOG_DIS("pc=%x mode=%x quickimm %d r%d r%d\n",
                     dc->pc, dc->mode, dc->opcode, dc->src, dc->dst);
            cris_set_prefix(dc);
            if (dc->dst == 15) {
                tcg_gen_movi_tl(cpu_PR[PR_PREFIX], dc->pc + 2 + simm);
            } else {
                tcg_gen_addi_tl(cpu_PR[PR_PREFIX], cpu_R[dc->dst], simm);
            }
            break;

        case CRISV10_QIMM_MOVEQ:
            LOG_DIS("moveq %d, $r%d\n", simm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(simm);
            cris_alu(dc, CC_OP_MOVE, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_CMPQ:
            LOG_DIS("cmpq %d, $r%d\n", simm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(simm);
            cris_alu(dc, CC_OP_CMP, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_ADDQ:
            LOG_DIS("addq %d, $r%d\n", imm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(imm);
            cris_alu(dc, CC_OP_ADD, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_ANDQ:
            LOG_DIS("andq %d, $r%d\n", simm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(simm);
            cris_alu(dc, CC_OP_AND, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_ASHQ:
            LOG_DIS("ashq %d, $r%d\n", simm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            op = imm & (1 << 5);
            imm &= 0x1f;
            c = tcg_const_tl(imm);
            if (op) {
                cris_alu(dc, CC_OP_ASR, cpu_R[dc->dst],
                          cpu_R[dc->dst], c, 4);
            } else {
                /* BTST */
                cris_update_cc_op(dc, CC_OP_FLAGS, 4);
                gen_helper_btst(cpu_PR[PR_CCS], cpu_env, cpu_R[dc->dst],
                           c, cpu_PR[PR_CCS]);
            }
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_LSHQ:
            LOG_DIS("lshq %d, $r%d\n", simm, dc->dst);

            op = CC_OP_LSL;
            if (imm & (1 << 5)) {
                op = CC_OP_LSR; 
            }
            imm &= 0x1f;
            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(imm);
            cris_alu(dc, op, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_SUBQ:
            LOG_DIS("subq %d, $r%d\n", imm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(imm);
            cris_alu(dc, CC_OP_SUB, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;
        case CRISV10_QIMM_ORQ:
            LOG_DIS("andq %d, $r%d\n", simm, dc->dst);

            cris_cc_mask(dc, CC_MASK_NZVC);
            c = tcg_const_tl(simm);
            cris_alu(dc, CC_OP_OR, cpu_R[dc->dst],
                     cpu_R[dc->dst], c, 4);
            tcg_temp_free(c);
            break;

        case CRISV10_QIMM_BCC_R0:
        case CRISV10_QIMM_BCC_R1:
        case CRISV10_QIMM_BCC_R2:
        case CRISV10_QIMM_BCC_R3:
            imm = dc->ir & 0xff;
            /* bit 0 is a sign bit.  */
            if (imm & 1) {
                imm |= 0xffffff00;   /* sign extend.  */
                imm &= ~1;           /* get rid of the sign bit.  */
            }
            imm += 2;
            LOG_DIS("b%s %d\n", cc_name(dc->cond), imm);

            cris_cc_mask(dc, 0);
            cris_prepare_cc_branch(dc, imm, dc->cond); 
            break;

        default:
            LOG_DIS("pc=%x mode=%x quickimm %d r%d r%d\n",
                     dc->pc, dc->mode, dc->opcode, dc->src, dc->dst);
            cpu_abort(CPU(dc->cpu), "Unhandled quickimm\n");
            break;
    }
    return 2;
}

static unsigned int dec10_setclrf(DisasContext *dc)
{
    uint32_t flags;
    unsigned int set = ~dc->opcode & 1;

    flags = EXTRACT_FIELD(dc->ir, 0, 3)
            | (EXTRACT_FIELD(dc->ir, 12, 15) << 4);
    LOG_DIS("%s set=%d flags=%x\n", __func__, set, flags);


    if (flags & X_FLAG) {
        if (set)
            dc->flags_x = X_FLAG;
        else
            dc->flags_x = 0;
    }

    cris_evaluate_flags (dc);
    cris_update_cc_op(dc, CC_OP_FLAGS, 4);
    cris_update_cc_x(dc);
    tcg_gen_movi_tl(cc_op, dc->cc_op);

    if (set) {
        tcg_gen_ori_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], flags);
    } else {
        tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS],
                        ~(flags|F_FLAG_V10|P_FLAG_V10));
    }

    dc->flags_uptodate = 1;
    dc->clear_x = 0;
    cris_lock_irq(dc);
    return 2;
}

static inline void dec10_reg_prep_sext(DisasContext *dc, int size, int sext,
                                       TCGv dd, TCGv ds, TCGv sd, TCGv ss)
{
    if (sext) {
        t_gen_sext(dd, sd, size);
        t_gen_sext(ds, ss, size);
    } else {
        t_gen_zext(dd, sd, size);
        t_gen_zext(ds, ss, size);
    }
}

static void dec10_reg_alu(DisasContext *dc, int op, int size, int sext)
{
    TCGv t[2];

    t[0] = tcg_temp_new();
    t[1] = tcg_temp_new();
    dec10_reg_prep_sext(dc, size, sext,
                        t[0], t[1], cpu_R[dc->dst], cpu_R[dc->src]);

    if (op == CC_OP_LSL || op == CC_OP_LSR || op == CC_OP_ASR) {
        tcg_gen_andi_tl(t[1], t[1], 63);
    }

    assert(dc->dst != 15);
    cris_alu(dc, op, cpu_R[dc->dst], t[0], t[1], size);
    tcg_temp_free(t[0]);
    tcg_temp_free(t[1]);
}

static void dec10_reg_bound(DisasContext *dc, int size)
{
    TCGv t;

    t = tcg_temp_new();
    t_gen_zext(t, cpu_R[dc->src], size);
    cris_alu(dc, CC_OP_BOUND, cpu_R[dc->dst], cpu_R[dc->dst], t, 4);
    tcg_temp_free(t);
}

static void dec10_reg_mul(DisasContext *dc, int size, int sext)
{
    int op = sext ? CC_OP_MULS : CC_OP_MULU;
    TCGv t[2];

    t[0] = tcg_temp_new();
    t[1] = tcg_temp_new();
    dec10_reg_prep_sext(dc, size, sext,
                        t[0], t[1], cpu_R[dc->dst], cpu_R[dc->src]);

    cris_alu(dc, op, cpu_R[dc->dst], t[0], t[1], 4);

    tcg_temp_free(t[0]);
    tcg_temp_free(t[1]);
}


static void dec10_reg_movs(DisasContext *dc)
{
    int size = (dc->size & 1) + 1;
    TCGv t;

    LOG_DIS("movx.%d $r%d, $r%d\n", size, dc->src, dc->dst);
    cris_cc_mask(dc, CC_MASK_NZVC);

    t = tcg_temp_new();
    if (dc->ir & 32)
        t_gen_sext(t, cpu_R[dc->src], size);
    else
        t_gen_zext(t, cpu_R[dc->src], size);

    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->dst], cpu_R[dc->dst], t, 4);
    tcg_temp_free(t);
}

static void dec10_reg_alux(DisasContext *dc, int op)
{
    int size = (dc->size & 1) + 1;
    TCGv t;

    LOG_DIS("movx.%d $r%d, $r%d\n", size, dc->src, dc->dst);
    cris_cc_mask(dc, CC_MASK_NZVC);

    t = tcg_temp_new();
    if (dc->ir & 32)
        t_gen_sext(t, cpu_R[dc->src], size);
    else
        t_gen_zext(t, cpu_R[dc->src], size);

    cris_alu(dc, op, cpu_R[dc->dst], cpu_R[dc->dst], t, 4);
    tcg_temp_free(t);
}

static void dec10_reg_mov_pr(DisasContext *dc)
{
    LOG_DIS("move p%d r%d sz=%d\n", dc->dst, dc->src, preg_sizes_v10[dc->dst]);
    cris_lock_irq(dc);
    if (dc->src == 15) {
        tcg_gen_mov_tl(env_btarget, cpu_PR[dc->dst]);
        cris_prepare_jmp(dc, JMP_INDIRECT);
        return;
    }
    if (dc->dst == PR_CCS) {
        cris_evaluate_flags(dc); 
    }
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->src],
                 cpu_R[dc->src], cpu_PR[dc->dst], preg_sizes_v10[dc->dst]);
}

static void dec10_reg_abs(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("abs $r%u, $r%u\n", dc->src, dc->dst);

    assert(dc->dst != 15);
    t0 = tcg_temp_new();
    tcg_gen_sari_tl(t0, cpu_R[dc->src], 31);
    tcg_gen_xor_tl(cpu_R[dc->dst], cpu_R[dc->src], t0);
    tcg_gen_sub_tl(t0, cpu_R[dc->dst], t0);

    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->dst], cpu_R[dc->dst], t0, 4);
    tcg_temp_free(t0);
}

static void dec10_reg_swap(DisasContext *dc)
{
    TCGv t0;

    LOG_DIS("not $r%d, $r%d\n", dc->src, dc->dst);

    cris_cc_mask(dc, CC_MASK_NZVC);
    t0 = tcg_temp_new();
    tcg_gen_mov_tl(t0, cpu_R[dc->src]);
    if (dc->dst & 8)
        tcg_gen_not_tl(t0, t0);
    if (dc->dst & 4)
        t_gen_swapw(t0, t0);
    if (dc->dst & 2)
        t_gen_swapb(t0, t0);
    if (dc->dst & 1)
        t_gen_swapr(t0, t0);
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->src], cpu_R[dc->src], t0, 4);
    tcg_temp_free(t0);
}

static void dec10_reg_scc(DisasContext *dc)
{
    int cond = dc->dst;

    LOG_DIS("s%s $r%u\n", cc_name(cond), dc->src);

    gen_tst_cc(dc, cpu_R[dc->src], cond);
    tcg_gen_setcondi_tl(TCG_COND_NE, cpu_R[dc->src], cpu_R[dc->src], 0);

    cris_cc_mask(dc, 0);
}

static unsigned int dec10_reg(DisasContext *dc)
{
    TCGv t;
    unsigned int insn_len = 2;
    unsigned int size = dec10_size(dc->size);
    unsigned int tmp;

    if (dc->size != 3) {
        switch (dc->opcode) {
            case CRISV10_REG_MOVE_R:
                LOG_DIS("move.%d $r%d, $r%d\n", dc->size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_MOVE, size, 0);
                if (dc->dst == 15) {
                    tcg_gen_mov_tl(env_btarget, cpu_R[dc->dst]);
                    cris_prepare_jmp(dc, JMP_INDIRECT);
                    dc->delayed_branch = 1;
                }
                break;
            case CRISV10_REG_MOVX:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_movs(dc);
                break;
            case CRISV10_REG_ADDX:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alux(dc, CC_OP_ADD);
                break;
            case CRISV10_REG_SUBX:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alux(dc, CC_OP_SUB);
                break;
            case CRISV10_REG_ADD:
                LOG_DIS("add $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_ADD, size, 0);
                break;
            case CRISV10_REG_SUB:
                LOG_DIS("sub $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_SUB, size, 0);
                break;
            case CRISV10_REG_CMP:
                LOG_DIS("cmp $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_CMP, size, 0);
                break;
            case CRISV10_REG_BOUND:
                LOG_DIS("bound $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_bound(dc, size);
                break;
            case CRISV10_REG_AND:
                LOG_DIS("and $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_AND, size, 0);
                break;
            case CRISV10_REG_ADDI:
                if (dc->src == 15) {
                    /* nop.  */
                    return 2;
                }
                t = tcg_temp_new();
                LOG_DIS("addi r%d r%d size=%d\n", dc->src, dc->dst, dc->size);
                tcg_gen_shli_tl(t, cpu_R[dc->dst], dc->size & 3);
                tcg_gen_add_tl(cpu_R[dc->src], cpu_R[dc->src], t);
                tcg_temp_free(t);
                break;
            case CRISV10_REG_LSL:
                LOG_DIS("lsl $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_LSL, size, 0);
                break;
            case CRISV10_REG_LSR:
                LOG_DIS("lsr $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_LSR, size, 0);
                break;
            case CRISV10_REG_ASR:
                LOG_DIS("asr $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_ASR, size, 1);
                break;
            case CRISV10_REG_OR:
                LOG_DIS("or $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_OR, size, 0);
                break;
            case CRISV10_REG_NEG:
                LOG_DIS("neg $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_NEG, size, 0);
                break;
            case CRISV10_REG_BIAP:
                LOG_DIS("BIAP pc=%x reg %d r%d r%d size=%d\n", dc->pc,
                         dc->opcode, dc->src, dc->dst, size);
                switch (size) {
                    case 4: tmp = 2; break;
                    case 2: tmp = 1; break;
                    case 1: tmp = 0; break;
                    default:
                        cpu_abort(CPU(dc->cpu), "Unhandled BIAP");
                        break;
                }

                t = tcg_temp_new();
                tcg_gen_shli_tl(t, cpu_R[dc->dst], tmp);
                if (dc->src == 15) {
                    tcg_gen_addi_tl(cpu_PR[PR_PREFIX], t, ((dc->pc +2)| 1) + 1);
                } else {
                    tcg_gen_add_tl(cpu_PR[PR_PREFIX], cpu_R[dc->src], t);
                }
                tcg_temp_free(t);
                cris_set_prefix(dc);
                break;

            default:
                LOG_DIS("pc=%x reg %d r%d r%d\n", dc->pc,
                         dc->opcode, dc->src, dc->dst);
                cpu_abort(CPU(dc->cpu), "Unhandled opcode");
                break;
        }
    } else {
        switch (dc->opcode) {
            case CRISV10_REG_MOVX:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_movs(dc);
                break;
            case CRISV10_REG_ADDX:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alux(dc, CC_OP_ADD);
                break;
            case CRISV10_REG_SUBX:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alux(dc, CC_OP_SUB);
                break;
            case CRISV10_REG_MOVE_SPR_R:
                cris_evaluate_flags(dc);
                cris_cc_mask(dc, 0);
                dec10_reg_mov_pr(dc);
                break;
            case CRISV10_REG_MOVE_R_SPR:
                LOG_DIS("move r%d p%d\n", dc->src, dc->dst);
                cris_evaluate_flags(dc);
                if (dc->src != 11) /* fast for srp.  */
                    dc->cpustate_changed = 1;
                t_gen_mov_preg_TN(dc, dc->dst, cpu_R[dc->src]);
                break;
            case CRISV10_REG_SETF:
            case CRISV10_REG_CLEARF:
                dec10_setclrf(dc);
                break;
            case CRISV10_REG_SWAP:
                dec10_reg_swap(dc);
                break;
            case CRISV10_REG_ABS:
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_abs(dc);
                break;
            case CRISV10_REG_LZ:
                LOG_DIS("lz $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_LZ, 4, 0);
                break;
            case CRISV10_REG_XOR:
                LOG_DIS("xor $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_alu(dc, CC_OP_XOR, 4, 0);
                break;
            case CRISV10_REG_BTST:
                LOG_DIS("btst $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                cris_update_cc_op(dc, CC_OP_FLAGS, 4);
                gen_helper_btst(cpu_PR[PR_CCS], cpu_env, cpu_R[dc->dst],
                           cpu_R[dc->src], cpu_PR[PR_CCS]);
                break;
            case CRISV10_REG_DSTEP:
                LOG_DIS("dstep $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_cc_mask(dc, CC_MASK_NZVC);
                cris_alu(dc, CC_OP_DSTEP, cpu_R[dc->dst],
                            cpu_R[dc->dst], cpu_R[dc->src], 4);
                break;
            case CRISV10_REG_MSTEP:
                LOG_DIS("mstep $r%d, $r%d sz=%d\n", dc->src, dc->dst, size);
                cris_evaluate_flags(dc);
                cris_cc_mask(dc, CC_MASK_NZVC);
                cris_alu(dc, CC_OP_MSTEP, cpu_R[dc->dst],
                            cpu_R[dc->dst], cpu_R[dc->src], 4);
                break;
            case CRISV10_REG_SCC:
                dec10_reg_scc(dc);
                break;
            default:
                LOG_DIS("pc=%x reg %d r%d r%d\n", dc->pc,
                         dc->opcode, dc->src, dc->dst);
                cpu_abort(CPU(dc->cpu), "Unhandled opcode");
                break;
        }
    }
    return insn_len;
}

static unsigned int dec10_ind_move_m_r(CPUCRISState *env, DisasContext *dc,
                                       unsigned int size)
{
    unsigned int insn_len = 2;
    TCGv t;

    LOG_DIS("%s: move.%d [$r%d], $r%d\n", __func__,
             size, dc->src, dc->dst);

    cris_cc_mask(dc, CC_MASK_NZVC);
    t = tcg_temp_new();
    insn_len += dec10_prep_move_m(env, dc, 0, size, t);
    cris_alu(dc, CC_OP_MOVE, cpu_R[dc->dst], cpu_R[dc->dst], t, size);
    if (dc->dst == 15) {
        tcg_gen_mov_tl(env_btarget, cpu_R[dc->dst]);
        cris_prepare_jmp(dc, JMP_INDIRECT);
        dc->delayed_branch = 1;
    }

    tcg_temp_free(t);
    return insn_len;
}

static unsigned int dec10_ind_move_r_m(DisasContext *dc, unsigned int size)
{
    unsigned int insn_len = 2;
    TCGv addr;

    LOG_DIS("move.%d $r%d, [$r%d]\n", dc->size, dc->src, dc->dst);
    addr = tcg_temp_new();
    crisv10_prepare_memaddr(dc, addr, size);
    gen_store_v10(dc, addr, cpu_R[dc->dst], size);
    insn_len += crisv10_post_memaddr(dc, size);
    tcg_temp_free(addr);

    return insn_len;
}

static unsigned int dec10_ind_move_m_pr(CPUCRISState *env, DisasContext *dc)
{
    unsigned int insn_len = 2, rd = dc->dst;
    TCGv t, addr;

    LOG_DIS("move.%d $p%d, [$r%d]\n", dc->size, dc->dst, dc->src);
    cris_lock_irq(dc);

    addr = tcg_temp_new();
    t = tcg_temp_new();
    insn_len += dec10_prep_move_m(env, dc, 0, 4, t);
    if (rd == 15) {
        tcg_gen_mov_tl(env_btarget, t);
        cris_prepare_jmp(dc, JMP_INDIRECT);
        dc->delayed_branch = 1;
    } else {
        tcg_gen_mov_tl(cpu_PR[rd], t);
        dc->cpustate_changed = 1;
    }
    tcg_temp_free(addr);
    tcg_temp_free(t);
    return insn_len;
}

static unsigned int dec10_ind_move_pr_m(DisasContext *dc)
{
    unsigned int insn_len = 2, size = preg_sizes_v10[dc->dst];
    TCGv addr, t0;

    LOG_DIS("move.%d $p%d, [$r%d]\n", dc->size, dc->dst, dc->src);

    addr = tcg_temp_new();
    crisv10_prepare_memaddr(dc, addr, size);
    if (dc->dst == PR_CCS) {
        t0 = tcg_temp_new();
        cris_evaluate_flags(dc);
        tcg_gen_andi_tl(t0, cpu_PR[PR_CCS], ~PFIX_FLAG);
        gen_store_v10(dc, addr, t0, size);
        tcg_temp_free(t0);
    } else {
        gen_store_v10(dc, addr, cpu_PR[dc->dst], size);
    }
    insn_len += crisv10_post_memaddr(dc, size);
    tcg_temp_free(addr);
    cris_lock_irq(dc);

    return insn_len;
}

static void dec10_movem_r_m(DisasContext *dc)
{
    int i, pfix = dc->tb_flags & PFIX_FLAG;
    TCGv addr, t0;

    LOG_DIS("%s r%d, [r%d] pi=%d ir=%x\n", __func__,
              dc->dst, dc->src, dc->postinc, dc->ir);

    addr = tcg_temp_new();
    t0 = tcg_temp_new();
    crisv10_prepare_memaddr(dc, addr, 4);
    tcg_gen_mov_tl(t0, addr);
    for (i = dc->dst; i >= 0; i--) {
        if ((pfix && dc->mode == CRISV10_MODE_AUTOINC) && dc->src == i) {
            gen_store_v10(dc, addr, t0, 4);
        } else {
            gen_store_v10(dc, addr, cpu_R[i], 4);
        }
        tcg_gen_addi_tl(addr, addr, 4);
    }

    if (pfix && dc->mode == CRISV10_MODE_AUTOINC) {
        tcg_gen_mov_tl(cpu_R[dc->src], t0);
    }

    if (!pfix && dc->mode == CRISV10_MODE_AUTOINC) {
        tcg_gen_mov_tl(cpu_R[dc->src], addr);
    }
    tcg_temp_free(addr);
    tcg_temp_free(t0);
}

static void dec10_movem_m_r(DisasContext *dc)
{
    int i, pfix = dc->tb_flags & PFIX_FLAG;
    TCGv addr, t0;

    LOG_DIS("%s [r%d], r%d pi=%d ir=%x\n", __func__,
              dc->src, dc->dst, dc->postinc, dc->ir);

    addr = tcg_temp_new();
    t0 = tcg_temp_new();
    crisv10_prepare_memaddr(dc, addr, 4);
    tcg_gen_mov_tl(t0, addr);
    for (i = dc->dst; i >= 0; i--) {
        gen_load(dc, cpu_R[i], addr, 4, 0);
        tcg_gen_addi_tl(addr, addr, 4);
    }

    if (pfix && dc->mode == CRISV10_MODE_AUTOINC) {
        tcg_gen_mov_tl(cpu_R[dc->src], t0);
    }

    if (!pfix && dc->mode == CRISV10_MODE_AUTOINC) {
        tcg_gen_mov_tl(cpu_R[dc->src], addr);
    }
    tcg_temp_free(addr);
    tcg_temp_free(t0);
}

static int dec10_ind_alu(CPUCRISState *env, DisasContext *dc,
                         int op, unsigned int size)
{
    int insn_len = 0;
    int rd = dc->dst;
    TCGv t[2];

    cris_alu_m_alloc_temps(t);
    insn_len += dec10_prep_move_m(env, dc, 0, size, t[0]);
    cris_alu(dc, op, cpu_R[dc->dst], cpu_R[rd], t[0], size);
    if (dc->dst == 15) {
        tcg_gen_mov_tl(env_btarget, cpu_R[dc->dst]);
        cris_prepare_jmp(dc, JMP_INDIRECT);
        dc->delayed_branch = 1;
        return insn_len;
    }
    return insn_len;
}

static int dec10_ind_bound(CPUCRISState *env, DisasContext *dc,
                           unsigned int size)
{
    int insn_len = 0;
    int rd = dc->dst;
    TCGv t;

    t = tcg_temp_new();
    insn_len += dec10_prep_move_m(env, dc, 0, size, t);
    cris_alu(dc, CC_OP_BOUND, cpu_R[dc->dst], cpu_R[rd], t, 4);
    if (dc->dst == 15) {
        tcg_gen_mov_tl(env_btarget, cpu_R[dc->dst]);
        cris_prepare_jmp(dc, JMP_INDIRECT);
        dc->delayed_branch = 1;
    }

    tcg_temp_free(t);
    return insn_len;
}

static int dec10_alux_m(CPUCRISState *env, DisasContext *dc, int op)
{
    unsigned int size = (dc->size & 1) ? 2 : 1;
    unsigned int sx = !!(dc->size & 2);
    int insn_len = 2;
    int rd = dc->dst;
    TCGv t;

    LOG_DIS("addx size=%d sx=%d op=%d %d\n", size, sx, dc->src, dc->dst);

    t = tcg_temp_new();

    cris_cc_mask(dc, CC_MASK_NZVC);
    insn_len += dec10_prep_move_m(env, dc, sx, size, t);
    cris_alu(dc, op, cpu_R[dc->dst], cpu_R[rd], t, 4);
    if (dc->dst == 15) {
        tcg_gen_mov_tl(env_btarget, cpu_R[dc->dst]);
        cris_prepare_jmp(dc, JMP_INDIRECT);
        dc->delayed_branch = 1;
    }

    tcg_temp_free(t);
    return insn_len;
}

static int dec10_dip(CPUCRISState *env, DisasContext *dc)
{
    int insn_len = 2;
    uint32_t imm;

    LOG_DIS("dip pc=%x opcode=%d r%d r%d\n",
              dc->pc, dc->opcode, dc->src, dc->dst);
    if (dc->src == 15) {
        imm = cpu_ldl_code(env, dc->pc + 2);
        tcg_gen_movi_tl(cpu_PR[PR_PREFIX], imm);
        if (dc->postinc)
            insn_len += 4;
        tcg_gen_addi_tl(cpu_R[15], cpu_R[15], insn_len - 2);
    } else {
        gen_load(dc, cpu_PR[PR_PREFIX], cpu_R[dc->src], 4, 0);
        if (dc->postinc)
            tcg_gen_addi_tl(cpu_R[dc->src], cpu_R[dc->src], 4);
    }

    cris_set_prefix(dc);
    return insn_len;
}

static int dec10_bdap_m(CPUCRISState *env, DisasContext *dc, int size)
{
    int insn_len = 2;
    int rd = dc->dst;

    LOG_DIS("bdap_m pc=%x opcode=%d r%d r%d sz=%d\n",
              dc->pc, dc->opcode, dc->src, dc->dst, size);

    assert(dc->dst != 15);
#if 0
    /* 8bit embedded offset?  */
    if (!dc->postinc && (dc->ir & (1 << 11))) {
        int simm = dc->ir & 0xff;

        /* cpu_abort(CPU(dc->cpu), "Unhandled opcode"); */
        /* sign extended.  */
        simm = (int8_t)simm;

        tcg_gen_addi_tl(cpu_PR[PR_PREFIX], cpu_R[dc->dst], simm);

        cris_set_prefix(dc);
        return insn_len;
    }
#endif
    /* Now the rest of the modes are truly indirect.  */
    insn_len += dec10_prep_move_m(env, dc, 1, size, cpu_PR[PR_PREFIX]);
    tcg_gen_add_tl(cpu_PR[PR_PREFIX], cpu_PR[PR_PREFIX], cpu_R[rd]);
    cris_set_prefix(dc);
    return insn_len;
}

static unsigned int dec10_ind(CPUCRISState *env, DisasContext *dc)
{
    unsigned int insn_len = 2;
    unsigned int size = dec10_size(dc->size);
    uint32_t imm;
    int32_t simm;
    TCGv t[2], c;

    if (dc->size != 3) {
        switch (dc->opcode) {
            case CRISV10_IND_MOVE_M_R:
                return dec10_ind_move_m_r(env, dc, size);
            case CRISV10_IND_MOVE_R_M:
                return dec10_ind_move_r_m(dc, size);
            case CRISV10_IND_CMP:
                LOG_DIS("cmp size=%d op=%d %d\n",  size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                insn_len += dec10_ind_alu(env, dc, CC_OP_CMP, size);
                break;
            case CRISV10_IND_TEST:
                LOG_DIS("test size=%d op=%d %d\n",  size, dc->src, dc->dst);

                cris_evaluate_flags(dc);
                cris_cc_mask(dc, CC_MASK_NZVC);
                cris_alu_m_alloc_temps(t);
                insn_len += dec10_prep_move_m(env, dc, 0, size, t[0]);
                tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~3);
                c = tcg_const_tl(0);
                cris_alu(dc, CC_OP_CMP, cpu_R[dc->dst],
                         t[0], c, size);
                tcg_temp_free(c);
                break;
            case CRISV10_IND_ADD:
                LOG_DIS("add size=%d op=%d %d\n",  size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                insn_len += dec10_ind_alu(env, dc, CC_OP_ADD, size);
                break;
            case CRISV10_IND_SUB:
                LOG_DIS("sub size=%d op=%d %d\n",  size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                insn_len += dec10_ind_alu(env, dc, CC_OP_SUB, size);
                break;
            case CRISV10_IND_BOUND:
                LOG_DIS("bound size=%d op=%d %d\n",  size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                insn_len += dec10_ind_bound(env, dc, size);
                break;
            case CRISV10_IND_AND:
                LOG_DIS("and size=%d op=%d %d\n",  size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                insn_len += dec10_ind_alu(env, dc, CC_OP_AND, size);
                break;
            case CRISV10_IND_OR:
                LOG_DIS("or size=%d op=%d %d\n",  size, dc->src, dc->dst);
                cris_cc_mask(dc, CC_MASK_NZVC);
                insn_len += dec10_ind_alu(env, dc, CC_OP_OR, size);
                break;
            case CRISV10_IND_MOVX:
                insn_len = dec10_alux_m(env, dc, CC_OP_MOVE);
                break;
            case CRISV10_IND_ADDX:
                insn_len = dec10_alux_m(env, dc, CC_OP_ADD);
                break;
            case CRISV10_IND_SUBX:
                insn_len = dec10_alux_m(env, dc, CC_OP_SUB);
                break;
            case CRISV10_IND_CMPX:
                insn_len = dec10_alux_m(env, dc, CC_OP_CMP);
                break;
            case CRISV10_IND_MUL:
                /* This is a reg insn coded in the mem indir space.  */
                LOG_DIS("mul pc=%x opcode=%d\n", dc->pc, dc->opcode);
                cris_cc_mask(dc, CC_MASK_NZVC);
                dec10_reg_mul(dc, size, dc->ir & (1 << 10));
                break;
            case CRISV10_IND_BDAP_M:
                insn_len = dec10_bdap_m(env, dc, size);
                break;
            default:
            /*
             * ADDC for v17:
             *
             * Instruction format: ADDC [Rs],Rd
             *
             *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
             *  |Destination(Rd)| 1   0   0   1   1   0   1   0 |   Source(Rs)|
             *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+--+--+
             *
             * Instruction format: ADDC [Rs+],Rd
             *
             *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
             *  |Destination(Rd)| 1   1   0   1   1   0   1   0 |   Source(Rs)|
             *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+-+
             */
                if (dc->opcode == CRISV17_IND_ADDC && dc->size == 2 &&
                    env->pregs[PR_VR] == 17) {
                    LOG_DIS("addc op=%d %d\n",  dc->src, dc->dst);
                    cris_cc_mask(dc, CC_MASK_NZVC);
                    insn_len += dec10_ind_alu(env, dc, CC_OP_ADDC, size);
                    break;
                }

                LOG_DIS("pc=%x var-ind.%d %d r%d r%d\n",
                          dc->pc, size, dc->opcode, dc->src, dc->dst);
                cpu_abort(CPU(dc->cpu), "Unhandled opcode");
                break;
        }
        return insn_len;
    }

    switch (dc->opcode) {
        case CRISV10_IND_MOVE_M_SPR:
            insn_len = dec10_ind_move_m_pr(env, dc);
            break;
        case CRISV10_IND_MOVE_SPR_M:
            insn_len = dec10_ind_move_pr_m(dc);
            break;
        case CRISV10_IND_JUMP_M:
            if (dc->src == 15) {
                LOG_DIS("jump.%d %d r%d r%d direct\n", size,
                         dc->opcode, dc->src, dc->dst);
                imm = cpu_ldl_code(env, dc->pc + 2);
                if (dc->mode == CRISV10_MODE_AUTOINC)
                    insn_len += size;

                c = tcg_const_tl(dc->pc + insn_len);
                t_gen_mov_preg_TN(dc, dc->dst, c);
                tcg_temp_free(c);
                dc->jmp_pc = imm;
                cris_prepare_jmp(dc, JMP_DIRECT);
                dc->delayed_branch--; /* v10 has no dslot here.  */
            } else {
                if (dc->dst == 14) {
                    LOG_DIS("break %d\n", dc->src);
                    cris_evaluate_flags(dc);
                    tcg_gen_movi_tl(env_pc, dc->pc + 2);
                    c = tcg_const_tl(dc->src + 2);
                    t_gen_mov_env_TN(trap_vector, c);
                    tcg_temp_free(c);
                    t_gen_raise_exception(EXCP_BREAK);
                    dc->base.is_jmp = DISAS_NORETURN;
                    return insn_len;
                }
                LOG_DIS("%d: jump.%d %d r%d r%d\n", __LINE__, size,
                         dc->opcode, dc->src, dc->dst);
                t[0] = tcg_temp_new();
                c = tcg_const_tl(dc->pc + insn_len);
                t_gen_mov_preg_TN(dc, dc->dst, c);
                tcg_temp_free(c);
                crisv10_prepare_memaddr(dc, t[0], size);
                gen_load(dc, env_btarget, t[0], 4, 0);
                insn_len += crisv10_post_memaddr(dc, size);
                cris_prepare_jmp(dc, JMP_INDIRECT);
                dc->delayed_branch--; /* v10 has no dslot here.  */
                tcg_temp_free(t[0]);
            }
            break;

        case CRISV10_IND_MOVEM_R_M:
            LOG_DIS("movem_r_m pc=%x opcode=%d r%d r%d\n",
                        dc->pc, dc->opcode, dc->dst, dc->src);
            dec10_movem_r_m(dc);
            break;
        case CRISV10_IND_MOVEM_M_R:
            LOG_DIS("movem_m_r pc=%x opcode=%d\n", dc->pc, dc->opcode);
            dec10_movem_m_r(dc);
            break;
        case CRISV10_IND_JUMP_R:
            LOG_DIS("jmp pc=%x opcode=%d r%d r%d\n",
                        dc->pc, dc->opcode, dc->dst, dc->src);
            tcg_gen_mov_tl(env_btarget, cpu_R[dc->src]);
            c = tcg_const_tl(dc->pc + insn_len);
            t_gen_mov_preg_TN(dc, dc->dst, c);
            tcg_temp_free(c);
            cris_prepare_jmp(dc, JMP_INDIRECT);
            dc->delayed_branch--; /* v10 has no dslot here.  */
            break;
        case CRISV10_IND_MOVX:
            insn_len = dec10_alux_m(env, dc, CC_OP_MOVE);
            break;
        case CRISV10_IND_ADDX:
            insn_len = dec10_alux_m(env, dc, CC_OP_ADD);
            break;
        case CRISV10_IND_SUBX:
            insn_len = dec10_alux_m(env, dc, CC_OP_SUB);
            break;
        case CRISV10_IND_CMPX:
            insn_len = dec10_alux_m(env, dc, CC_OP_CMP);
            break;
        case CRISV10_IND_DIP:
            insn_len = dec10_dip(env, dc);
            break;
        case CRISV10_IND_BCC_M:

            cris_cc_mask(dc, 0);
            simm = cpu_ldsw_code(env, dc->pc + 2);
            simm += 4;

            LOG_DIS("bcc_m: b%s %x\n", cc_name(dc->cond), dc->pc + simm);
            cris_prepare_cc_branch(dc, simm, dc->cond);
            insn_len = 4;
            break;
        default:
            LOG_DIS("ERROR pc=%x opcode=%d\n", dc->pc, dc->opcode);
            cpu_abort(CPU(dc->cpu), "Unhandled opcode");
            break;
    }

    return insn_len;
}

static unsigned int crisv10_decoder(CPUCRISState *env, DisasContext *dc)
{
    unsigned int insn_len = 2;

    /* Load a halfword onto the instruction register.  */
    dc->ir = cpu_lduw_code(env, dc->pc);

    /* Now decode it.  */
    dc->opcode   = EXTRACT_FIELD(dc->ir, 6, 9);
    dc->mode     = EXTRACT_FIELD(dc->ir, 10, 11);
    dc->src      = EXTRACT_FIELD(dc->ir, 0, 3);
    dc->size     = EXTRACT_FIELD(dc->ir, 4, 5);
    dc->cond = dc->dst = EXTRACT_FIELD(dc->ir, 12, 15);
    dc->postinc  = EXTRACT_FIELD(dc->ir, 10, 10);

    dc->clear_prefix = 1;

    /* FIXME: What if this insn insn't 2 in length??  */
    if (dc->src == 15 || dc->dst == 15)
        tcg_gen_movi_tl(cpu_R[15], dc->pc + 2);

    switch (dc->mode) {
        case CRISV10_MODE_QIMMEDIATE:
            insn_len = dec10_quick_imm(dc);
            break;
        case CRISV10_MODE_REG:
            insn_len = dec10_reg(dc);
            break;
        case CRISV10_MODE_AUTOINC:
        case CRISV10_MODE_INDIRECT:
            insn_len = dec10_ind(env, dc);
            break;
    }

    if (dc->clear_prefix && dc->tb_flags & PFIX_FLAG) {
        dc->tb_flags &= ~PFIX_FLAG;
        tcg_gen_andi_tl(cpu_PR[PR_CCS], cpu_PR[PR_CCS], ~PFIX_FLAG);
        if (dc->tb_flags != dc->base.tb->flags) {
            dc->cpustate_changed = 1;
        }
    }

    /* CRISv10 locks out interrupts on dslots.  */
    if (dc->delayed_branch == 2) {
        cris_lock_irq(dc);
    }
    return insn_len;
}

void cris_initialize_crisv10_tcg(void)
{
    int i;

    cc_x = tcg_global_mem_new(cpu_env,
                              offsetof(CPUCRISState, cc_x), "cc_x");
    cc_src = tcg_global_mem_new(cpu_env,
                                offsetof(CPUCRISState, cc_src), "cc_src");
    cc_dest = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUCRISState, cc_dest),
                                 "cc_dest");
    cc_result = tcg_global_mem_new(cpu_env,
                                   offsetof(CPUCRISState, cc_result),
                                   "cc_result");
    cc_op = tcg_global_mem_new(cpu_env,
                               offsetof(CPUCRISState, cc_op), "cc_op");
    cc_size = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUCRISState, cc_size),
                                 "cc_size");
    cc_mask = tcg_global_mem_new(cpu_env,
                                 offsetof(CPUCRISState, cc_mask),
                                 "cc_mask");

    env_pc = tcg_global_mem_new(cpu_env,
                                offsetof(CPUCRISState, pc),
                                "pc");
    env_btarget = tcg_global_mem_new(cpu_env,
                                     offsetof(CPUCRISState, btarget),
                                     "btarget");
    env_btaken = tcg_global_mem_new(cpu_env,
                                    offsetof(CPUCRISState, btaken),
                                    "btaken");
    for (i = 0; i < 16; i++) {
        cpu_R[i] = tcg_global_mem_new(cpu_env,
                                      offsetof(CPUCRISState, regs[i]),
                                      regnames_v10[i]);
    }
    for (i = 0; i < 16; i++) {
        cpu_PR[i] = tcg_global_mem_new(cpu_env,
                                       offsetof(CPUCRISState, pregs[i]),
                                       pregnames_v10[i]);
    }
}
