/*
   SPARC translation

   Copyright (C) 2003 Thomas M. Ogrisegg <tom@fnord.at>
   Copyright (C) 2003-2005 Fabrice Bellard

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public
   License as published by the Free Software Foundation; either
   version 2 of the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
   TODO-list:

   Rest of V9 instructions, VIS instructions
   NPC/PC static optimisations (use JUMP_TB when possible)
   Optimize synthetic instructions
*/

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "cpu.h"
#include "exec-all.h"
#include "disas.h"

#define DEBUG_DISAS

#define DYNAMIC_PC  1 /* dynamic pc value */
#define JUMP_PC     2 /* dynamic pc value which takes only two values
                         according to jump_pc[T2] */

typedef struct DisasContext {
    target_ulong pc;    /* current Program Counter: integer or DYNAMIC_PC */
    target_ulong npc;   /* next PC: integer or DYNAMIC_PC or JUMP_PC */
    target_ulong jump_pc[2]; /* used when JUMP_PC pc value is used */
    int is_br;
    int mem_idx;
    int fpu_enabled;
    struct TranslationBlock *tb;
} DisasContext;

typedef struct sparc_def_t sparc_def_t;

struct sparc_def_t {
    const unsigned char *name;
    target_ulong iu_version;
    uint32_t fpu_version;
    uint32_t mmu_version;
    uint32_t mmu_bm;
};

static const sparc_def_t *cpu_sparc_find_by_name(const unsigned char *name);

static uint16_t *gen_opc_ptr;
static uint32_t *gen_opparam_ptr;
extern FILE *logfile;
extern int loglevel;

enum {
#define DEF(s,n,copy_size) INDEX_op_ ## s,
#include "opc.h"
#undef DEF
    NB_OPS
};

#include "gen-op.h"

// This function uses non-native bit order
#define GET_FIELD(X, FROM, TO) \
  ((X) >> (31 - (TO)) & ((1 << ((TO) - (FROM) + 1)) - 1))

// This function uses the order in the manuals, i.e. bit 0 is 2^0
#define GET_FIELD_SP(X, FROM, TO) \
    GET_FIELD(X, 31 - (TO), 31 - (FROM))

#define GET_FIELDs(x,a,b) sign_extend (GET_FIELD(x,a,b), (b) - (a) + 1)
#define GET_FIELD_SPs(x,a,b) sign_extend (GET_FIELD_SP(x,a,b), ((b) - (a) + 1))

#ifdef TARGET_SPARC64
#define DFPREG(r) (((r & 1) << 5) | (r & 0x1e))
#define QFPREG(r) (((r & 1) << 5) | (r & 0x1c))
#else
#define DFPREG(r) (r & 0x1e)
#define QFPREG(r) (r & 0x1c)
#endif

#ifdef USE_DIRECT_JUMP
#define TBPARAM(x)
#else
#define TBPARAM(x) (long)(x)
#endif

static int sign_extend(int x, int len)
{
    len = 32 - len;
    return (x << len) >> len;
}

#define IS_IMM (insn & (1<<13))

static void disas_sparc_insn(DisasContext * dc);

static GenOpFunc * const gen_op_movl_TN_reg[2][32] = {
    {
     gen_op_movl_g0_T0,
     gen_op_movl_g1_T0,
     gen_op_movl_g2_T0,
     gen_op_movl_g3_T0,
     gen_op_movl_g4_T0,
     gen_op_movl_g5_T0,
     gen_op_movl_g6_T0,
     gen_op_movl_g7_T0,
     gen_op_movl_o0_T0,
     gen_op_movl_o1_T0,
     gen_op_movl_o2_T0,
     gen_op_movl_o3_T0,
     gen_op_movl_o4_T0,
     gen_op_movl_o5_T0,
     gen_op_movl_o6_T0,
     gen_op_movl_o7_T0,
     gen_op_movl_l0_T0,
     gen_op_movl_l1_T0,
     gen_op_movl_l2_T0,
     gen_op_movl_l3_T0,
     gen_op_movl_l4_T0,
     gen_op_movl_l5_T0,
     gen_op_movl_l6_T0,
     gen_op_movl_l7_T0,
     gen_op_movl_i0_T0,
     gen_op_movl_i1_T0,
     gen_op_movl_i2_T0,
     gen_op_movl_i3_T0,
     gen_op_movl_i4_T0,
     gen_op_movl_i5_T0,
     gen_op_movl_i6_T0,
     gen_op_movl_i7_T0,
     },
    {
     gen_op_movl_g0_T1,
     gen_op_movl_g1_T1,
     gen_op_movl_g2_T1,
     gen_op_movl_g3_T1,
     gen_op_movl_g4_T1,
     gen_op_movl_g5_T1,
     gen_op_movl_g6_T1,
     gen_op_movl_g7_T1,
     gen_op_movl_o0_T1,
     gen_op_movl_o1_T1,
     gen_op_movl_o2_T1,
     gen_op_movl_o3_T1,
     gen_op_movl_o4_T1,
     gen_op_movl_o5_T1,
     gen_op_movl_o6_T1,
     gen_op_movl_o7_T1,
     gen_op_movl_l0_T1,
     gen_op_movl_l1_T1,
     gen_op_movl_l2_T1,
     gen_op_movl_l3_T1,
     gen_op_movl_l4_T1,
     gen_op_movl_l5_T1,
     gen_op_movl_l6_T1,
     gen_op_movl_l7_T1,
     gen_op_movl_i0_T1,
     gen_op_movl_i1_T1,
     gen_op_movl_i2_T1,
     gen_op_movl_i3_T1,
     gen_op_movl_i4_T1,
     gen_op_movl_i5_T1,
     gen_op_movl_i6_T1,
     gen_op_movl_i7_T1,
     }
};

static GenOpFunc * const gen_op_movl_reg_TN[3][32] = {
    {
     gen_op_movl_T0_g0,
     gen_op_movl_T0_g1,
     gen_op_movl_T0_g2,
     gen_op_movl_T0_g3,
     gen_op_movl_T0_g4,
     gen_op_movl_T0_g5,
     gen_op_movl_T0_g6,
     gen_op_movl_T0_g7,
     gen_op_movl_T0_o0,
     gen_op_movl_T0_o1,
     gen_op_movl_T0_o2,
     gen_op_movl_T0_o3,
     gen_op_movl_T0_o4,
     gen_op_movl_T0_o5,
     gen_op_movl_T0_o6,
     gen_op_movl_T0_o7,
     gen_op_movl_T0_l0,
     gen_op_movl_T0_l1,
     gen_op_movl_T0_l2,
     gen_op_movl_T0_l3,
     gen_op_movl_T0_l4,
     gen_op_movl_T0_l5,
     gen_op_movl_T0_l6,
     gen_op_movl_T0_l7,
     gen_op_movl_T0_i0,
     gen_op_movl_T0_i1,
     gen_op_movl_T0_i2,
     gen_op_movl_T0_i3,
     gen_op_movl_T0_i4,
     gen_op_movl_T0_i5,
     gen_op_movl_T0_i6,
     gen_op_movl_T0_i7,
     },
    {
     gen_op_movl_T1_g0,
     gen_op_movl_T1_g1,
     gen_op_movl_T1_g2,
     gen_op_movl_T1_g3,
     gen_op_movl_T1_g4,
     gen_op_movl_T1_g5,
     gen_op_movl_T1_g6,
     gen_op_movl_T1_g7,
     gen_op_movl_T1_o0,
     gen_op_movl_T1_o1,
     gen_op_movl_T1_o2,
     gen_op_movl_T1_o3,
     gen_op_movl_T1_o4,
     gen_op_movl_T1_o5,
     gen_op_movl_T1_o6,
     gen_op_movl_T1_o7,
     gen_op_movl_T1_l0,
     gen_op_movl_T1_l1,
     gen_op_movl_T1_l2,
     gen_op_movl_T1_l3,
     gen_op_movl_T1_l4,
     gen_op_movl_T1_l5,
     gen_op_movl_T1_l6,
     gen_op_movl_T1_l7,
     gen_op_movl_T1_i0,
     gen_op_movl_T1_i1,
     gen_op_movl_T1_i2,
     gen_op_movl_T1_i3,
     gen_op_movl_T1_i4,
     gen_op_movl_T1_i5,
     gen_op_movl_T1_i6,
     gen_op_movl_T1_i7,
     },
    {
     gen_op_movl_T2_g0,
     gen_op_movl_T2_g1,
     gen_op_movl_T2_g2,
     gen_op_movl_T2_g3,
     gen_op_movl_T2_g4,
     gen_op_movl_T2_g5,
     gen_op_movl_T2_g6,
     gen_op_movl_T2_g7,
     gen_op_movl_T2_o0,
     gen_op_movl_T2_o1,
     gen_op_movl_T2_o2,
     gen_op_movl_T2_o3,
     gen_op_movl_T2_o4,
     gen_op_movl_T2_o5,
     gen_op_movl_T2_o6,
     gen_op_movl_T2_o7,
     gen_op_movl_T2_l0,
     gen_op_movl_T2_l1,
     gen_op_movl_T2_l2,
     gen_op_movl_T2_l3,
     gen_op_movl_T2_l4,
     gen_op_movl_T2_l5,
     gen_op_movl_T2_l6,
     gen_op_movl_T2_l7,
     gen_op_movl_T2_i0,
     gen_op_movl_T2_i1,
     gen_op_movl_T2_i2,
     gen_op_movl_T2_i3,
     gen_op_movl_T2_i4,
     gen_op_movl_T2_i5,
     gen_op_movl_T2_i6,
     gen_op_movl_T2_i7,
     }
};

static GenOpFunc1 * const gen_op_movl_TN_im[3] = {
    gen_op_movl_T0_im,
    gen_op_movl_T1_im,
    gen_op_movl_T2_im
};

// Sign extending version
static GenOpFunc1 * const gen_op_movl_TN_sim[3] = {
    gen_op_movl_T0_sim,
    gen_op_movl_T1_sim,
    gen_op_movl_T2_sim
};

#ifdef TARGET_SPARC64
#define GEN32(func, NAME) \
static GenOpFunc * const NAME ## _table [64] = {                              \
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,                                   \
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,                                   \
NAME ## 8, NAME ## 9, NAME ## 10, NAME ## 11,                                 \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,                               \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,                               \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,                               \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,                               \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,                               \
NAME ## 32, 0, NAME ## 34, 0, NAME ## 36, 0, NAME ## 38, 0,                   \
NAME ## 40, 0, NAME ## 42, 0, NAME ## 44, 0, NAME ## 46, 0,                   \
NAME ## 48, 0, NAME ## 50, 0, NAME ## 52, 0, NAME ## 54, 0,                   \
NAME ## 56, 0, NAME ## 58, 0, NAME ## 60, 0, NAME ## 62, 0,                   \
};                                                                            \
static inline void func(int n)                                                \
{                                                                             \
    NAME ## _table[n]();                                                      \
}
#else
#define GEN32(func, NAME) \
static GenOpFunc *const NAME ## _table [32] = {                               \
NAME ## 0, NAME ## 1, NAME ## 2, NAME ## 3,                                   \
NAME ## 4, NAME ## 5, NAME ## 6, NAME ## 7,                                   \
NAME ## 8, NAME ## 9, NAME ## 10, NAME ## 11,                                 \
NAME ## 12, NAME ## 13, NAME ## 14, NAME ## 15,                               \
NAME ## 16, NAME ## 17, NAME ## 18, NAME ## 19,                               \
NAME ## 20, NAME ## 21, NAME ## 22, NAME ## 23,                               \
NAME ## 24, NAME ## 25, NAME ## 26, NAME ## 27,                               \
NAME ## 28, NAME ## 29, NAME ## 30, NAME ## 31,                               \
};                                                                            \
static inline void func(int n)                                                \
{                                                                             \
    NAME ## _table[n]();                                                      \
}
#endif

/* floating point registers moves */
GEN32(gen_op_load_fpr_FT0, gen_op_load_fpr_FT0_fprf);
GEN32(gen_op_load_fpr_FT1, gen_op_load_fpr_FT1_fprf);
GEN32(gen_op_store_FT0_fpr, gen_op_store_FT0_fpr_fprf);
GEN32(gen_op_store_FT1_fpr, gen_op_store_FT1_fpr_fprf);

GEN32(gen_op_load_fpr_DT0, gen_op_load_fpr_DT0_fprf);
GEN32(gen_op_load_fpr_DT1, gen_op_load_fpr_DT1_fprf);
GEN32(gen_op_store_DT0_fpr, gen_op_store_DT0_fpr_fprf);
GEN32(gen_op_store_DT1_fpr, gen_op_store_DT1_fpr_fprf);

#if defined(CONFIG_USER_ONLY)
GEN32(gen_op_load_fpr_QT0, gen_op_load_fpr_QT0_fprf);
GEN32(gen_op_load_fpr_QT1, gen_op_load_fpr_QT1_fprf);
GEN32(gen_op_store_QT0_fpr, gen_op_store_QT0_fpr_fprf);
GEN32(gen_op_store_QT1_fpr, gen_op_store_QT1_fpr_fprf);
#endif

/* moves */
#ifdef CONFIG_USER_ONLY
#define supervisor(dc) 0
#ifdef TARGET_SPARC64
#define hypervisor(dc) 0
#endif
#define gen_op_ldst(name)        gen_op_##name##_raw()
#else
#define supervisor(dc) (dc->mem_idx >= 1)
#ifdef TARGET_SPARC64
#define hypervisor(dc) (dc->mem_idx == 2)
#define OP_LD_TABLE(width)                                              \
    static GenOpFunc * const gen_op_##width[] = {                       \
        &gen_op_##width##_user,                                         \
        &gen_op_##width##_kernel,                                       \
        &gen_op_##width##_hypv,                                         \
    };
#else
#define OP_LD_TABLE(width)                                              \
    static GenOpFunc * const gen_op_##width[] = {                       \
        &gen_op_##width##_user,                                         \
        &gen_op_##width##_kernel,                                       \
    };
#endif
#define gen_op_ldst(name)        (*gen_op_##name[dc->mem_idx])()
#endif

#ifndef CONFIG_USER_ONLY
OP_LD_TABLE(ld);
OP_LD_TABLE(st);
OP_LD_TABLE(ldub);
OP_LD_TABLE(lduh);
OP_LD_TABLE(ldsb);
OP_LD_TABLE(ldsh);
OP_LD_TABLE(stb);
OP_LD_TABLE(sth);
OP_LD_TABLE(std);
OP_LD_TABLE(ldstub);
OP_LD_TABLE(swap);
OP_LD_TABLE(ldd);
OP_LD_TABLE(stf);
OP_LD_TABLE(stdf);
OP_LD_TABLE(ldf);
OP_LD_TABLE(lddf);

#ifdef TARGET_SPARC64
OP_LD_TABLE(lduw);
OP_LD_TABLE(ldsw);
OP_LD_TABLE(ldx);
OP_LD_TABLE(stx);
#endif
#endif

/* asi moves */
#ifdef TARGET_SPARC64
static inline void gen_ld_asi(int insn, int size, int sign)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_ld_asi_reg(offset, size, sign);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_ld_asi(asi, size, sign);
    }
}

static inline void gen_st_asi(int insn, int size)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_st_asi_reg(offset, size);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_st_asi(asi, size);
    }
}

static inline void gen_ldf_asi(int insn, int size, int rd)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_ldf_asi_reg(offset, size, rd);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_ldf_asi(asi, size, rd);
    }
}

static inline void gen_stf_asi(int insn, int size, int rd)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_stf_asi_reg(offset, size, rd);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_stf_asi(asi, size, rd);
    }
}

static inline void gen_swap_asi(int insn)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_swap_asi_reg(offset);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_swap_asi(asi);
    }
}

static inline void gen_ldstub_asi(int insn)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_ldstub_asi_reg(offset);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_ldstub_asi(asi);
    }
}

static inline void gen_ldda_asi(int insn)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_ldda_asi_reg(offset);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_ldda_asi(asi);
    }
}

static inline void gen_stda_asi(int insn)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_stda_asi_reg(offset);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_stda_asi(asi);
    }
}

static inline void gen_cas_asi(int insn)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_cas_asi_reg(offset);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_cas_asi(asi);
    }
}

static inline void gen_casx_asi(int insn)
{
    int asi, offset;

    if (IS_IMM) {
        offset = GET_FIELD(insn, 25, 31);
        gen_op_casx_asi_reg(offset);
    } else {
        asi = GET_FIELD(insn, 19, 26);
        gen_op_casx_asi(asi);
    }
}

#elif !defined(CONFIG_USER_ONLY)

static inline void gen_ld_asi(int insn, int size, int sign)
{
    int asi;

    asi = GET_FIELD(insn, 19, 26);
    gen_op_ld_asi(asi, size, sign);
}

static inline void gen_st_asi(int insn, int size)
{
    int asi;

    asi = GET_FIELD(insn, 19, 26);
    gen_op_st_asi(asi, size);
}

static inline void gen_ldstub_asi(int insn)
{
    int asi;

    asi = GET_FIELD(insn, 19, 26);
    gen_op_ldstub_asi(asi);
}

static inline void gen_swap_asi(int insn)
{
    int asi;

    asi = GET_FIELD(insn, 19, 26);
    gen_op_swap_asi(asi);
}

static inline void gen_ldda_asi(int insn)
{
    int asi;

    asi = GET_FIELD(insn, 19, 26);
    gen_op_ld_asi(asi, 8, 0);
}

static inline void gen_stda_asi(int insn)
{
    int asi;

    asi = GET_FIELD(insn, 19, 26);
    gen_op_st_asi(asi, 8);
}
#endif

static inline void gen_movl_imm_TN(int reg, uint32_t imm)
{
    gen_op_movl_TN_im[reg](imm);
}

static inline void gen_movl_imm_T1(uint32_t val)
{
    gen_movl_imm_TN(1, val);
}

static inline void gen_movl_imm_T0(uint32_t val)
{
    gen_movl_imm_TN(0, val);
}

static inline void gen_movl_simm_TN(int reg, int32_t imm)
{
    gen_op_movl_TN_sim[reg](imm);
}

static inline void gen_movl_simm_T1(int32_t val)
{
    gen_movl_simm_TN(1, val);
}

static inline void gen_movl_simm_T0(int32_t val)
{
    gen_movl_simm_TN(0, val);
}

static inline void gen_movl_reg_TN(int reg, int t)
{
    if (reg)
        gen_op_movl_reg_TN[t][reg] ();
    else
        gen_movl_imm_TN(t, 0);
}

static inline void gen_movl_reg_T0(int reg)
{
    gen_movl_reg_TN(reg, 0);
}

static inline void gen_movl_reg_T1(int reg)
{
    gen_movl_reg_TN(reg, 1);
}

static inline void gen_movl_reg_T2(int reg)
{
    gen_movl_reg_TN(reg, 2);
}

static inline void gen_movl_TN_reg(int reg, int t)
{
    if (reg)
        gen_op_movl_TN_reg[t][reg] ();
}

static inline void gen_movl_T0_reg(int reg)
{
    gen_movl_TN_reg(reg, 0);
}

static inline void gen_movl_T1_reg(int reg)
{
    gen_movl_TN_reg(reg, 1);
}

static inline void gen_jmp_im(target_ulong pc)
{
#ifdef TARGET_SPARC64
    if (pc == (uint32_t)pc) {
        gen_op_jmp_im(pc);
    } else {
        gen_op_jmp_im64(pc >> 32, pc);
    }
#else
    gen_op_jmp_im(pc);
#endif
}

static inline void gen_movl_npc_im(target_ulong npc)
{
#ifdef TARGET_SPARC64
    if (npc == (uint32_t)npc) {
        gen_op_movl_npc_im(npc);
    } else {
        gen_op_movq_npc_im64(npc >> 32, npc);
    }
#else
    gen_op_movl_npc_im(npc);
#endif
}

static inline void gen_goto_tb(DisasContext *s, int tb_num,
                               target_ulong pc, target_ulong npc)
{
    TranslationBlock *tb;

    tb = s->tb;
    if ((pc & TARGET_PAGE_MASK) == (tb->pc & TARGET_PAGE_MASK) &&
        (npc & TARGET_PAGE_MASK) == (tb->pc & TARGET_PAGE_MASK))  {
        /* jump to same page: we can use a direct jump */
        if (tb_num == 0)
            gen_op_goto_tb0(TBPARAM(tb));
        else
            gen_op_goto_tb1(TBPARAM(tb));
        gen_jmp_im(pc);
        gen_movl_npc_im(npc);
        gen_op_movl_T0_im((long)tb + tb_num);
        gen_op_exit_tb();
    } else {
        /* jump to another page: currently not optimized */
        gen_jmp_im(pc);
        gen_movl_npc_im(npc);
        gen_op_movl_T0_0();
        gen_op_exit_tb();
    }
}

static inline void gen_branch2(DisasContext *dc, target_ulong pc1,
                               target_ulong pc2)
{
    int l1;

    l1 = gen_new_label();

    gen_op_jz_T2_label(l1);

    gen_goto_tb(dc, 0, pc1, pc1 + 4);

    gen_set_label(l1);
    gen_goto_tb(dc, 1, pc2, pc2 + 4);
}

static inline void gen_branch_a(DisasContext *dc, target_ulong pc1,
                                target_ulong pc2)
{
    int l1;

    l1 = gen_new_label();

    gen_op_jz_T2_label(l1);

    gen_goto_tb(dc, 0, pc2, pc1);

    gen_set_label(l1);
    gen_goto_tb(dc, 1, pc2 + 4, pc2 + 8);
}

static inline void gen_branch(DisasContext *dc, target_ulong pc,
                              target_ulong npc)
{
    gen_goto_tb(dc, 0, pc, npc);
}

static inline void gen_generic_branch(target_ulong npc1, target_ulong npc2)
{
    int l1, l2;

    l1 = gen_new_label();
    l2 = gen_new_label();
    gen_op_jz_T2_label(l1);

    gen_movl_npc_im(npc1);
    gen_op_jmp_label(l2);

    gen_set_label(l1);
    gen_movl_npc_im(npc2);
    gen_set_label(l2);
}

/* call this function before using T2 as it may have been set for a jump */
static inline void flush_T2(DisasContext * dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc->jump_pc[0], dc->jump_pc[1]);
        dc->npc = DYNAMIC_PC;
    }
}

static inline void save_npc(DisasContext * dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc->jump_pc[0], dc->jump_pc[1]);
        dc->npc = DYNAMIC_PC;
    } else if (dc->npc != DYNAMIC_PC) {
        gen_movl_npc_im(dc->npc);
    }
}

static inline void save_state(DisasContext * dc)
{
    gen_jmp_im(dc->pc);
    save_npc(dc);
}

static inline void gen_mov_pc_npc(DisasContext * dc)
{
    if (dc->npc == JUMP_PC) {
        gen_generic_branch(dc->jump_pc[0], dc->jump_pc[1]);
        gen_op_mov_pc_npc();
        dc->pc = DYNAMIC_PC;
    } else if (dc->npc == DYNAMIC_PC) {
        gen_op_mov_pc_npc();
        dc->pc = DYNAMIC_PC;
    } else {
        dc->pc = dc->npc;
    }
}

static GenOpFunc * const gen_cond[2][16] = {
    {
        gen_op_eval_bn,
        gen_op_eval_be,
        gen_op_eval_ble,
        gen_op_eval_bl,
        gen_op_eval_bleu,
        gen_op_eval_bcs,
        gen_op_eval_bneg,
        gen_op_eval_bvs,
        gen_op_eval_ba,
        gen_op_eval_bne,
        gen_op_eval_bg,
        gen_op_eval_bge,
        gen_op_eval_bgu,
        gen_op_eval_bcc,
        gen_op_eval_bpos,
        gen_op_eval_bvc,
    },
    {
#ifdef TARGET_SPARC64
        gen_op_eval_bn,
        gen_op_eval_xbe,
        gen_op_eval_xble,
        gen_op_eval_xbl,
        gen_op_eval_xbleu,
        gen_op_eval_xbcs,
        gen_op_eval_xbneg,
        gen_op_eval_xbvs,
        gen_op_eval_ba,
        gen_op_eval_xbne,
        gen_op_eval_xbg,
        gen_op_eval_xbge,
        gen_op_eval_xbgu,
        gen_op_eval_xbcc,
        gen_op_eval_xbpos,
        gen_op_eval_xbvc,
#endif
    },
};

static GenOpFunc * const gen_fcond[4][16] = {
    {
        gen_op_eval_bn,
        gen_op_eval_fbne,
        gen_op_eval_fblg,
        gen_op_eval_fbul,
        gen_op_eval_fbl,
        gen_op_eval_fbug,
        gen_op_eval_fbg,
        gen_op_eval_fbu,
        gen_op_eval_ba,
        gen_op_eval_fbe,
        gen_op_eval_fbue,
        gen_op_eval_fbge,
        gen_op_eval_fbuge,
        gen_op_eval_fble,
        gen_op_eval_fbule,
        gen_op_eval_fbo,
    },
#ifdef TARGET_SPARC64
    {
        gen_op_eval_bn,
        gen_op_eval_fbne_fcc1,
        gen_op_eval_fblg_fcc1,
        gen_op_eval_fbul_fcc1,
        gen_op_eval_fbl_fcc1,
        gen_op_eval_fbug_fcc1,
        gen_op_eval_fbg_fcc1,
        gen_op_eval_fbu_fcc1,
        gen_op_eval_ba,
        gen_op_eval_fbe_fcc1,
        gen_op_eval_fbue_fcc1,
        gen_op_eval_fbge_fcc1,
        gen_op_eval_fbuge_fcc1,
        gen_op_eval_fble_fcc1,
        gen_op_eval_fbule_fcc1,
        gen_op_eval_fbo_fcc1,
    },
    {
        gen_op_eval_bn,
        gen_op_eval_fbne_fcc2,
        gen_op_eval_fblg_fcc2,
        gen_op_eval_fbul_fcc2,
        gen_op_eval_fbl_fcc2,
        gen_op_eval_fbug_fcc2,
        gen_op_eval_fbg_fcc2,
        gen_op_eval_fbu_fcc2,
        gen_op_eval_ba,
        gen_op_eval_fbe_fcc2,
        gen_op_eval_fbue_fcc2,
        gen_op_eval_fbge_fcc2,
        gen_op_eval_fbuge_fcc2,
        gen_op_eval_fble_fcc2,
        gen_op_eval_fbule_fcc2,
        gen_op_eval_fbo_fcc2,
    },
    {
        gen_op_eval_bn,
        gen_op_eval_fbne_fcc3,
        gen_op_eval_fblg_fcc3,
        gen_op_eval_fbul_fcc3,
        gen_op_eval_fbl_fcc3,
        gen_op_eval_fbug_fcc3,
        gen_op_eval_fbg_fcc3,
        gen_op_eval_fbu_fcc3,
        gen_op_eval_ba,
        gen_op_eval_fbe_fcc3,
        gen_op_eval_fbue_fcc3,
        gen_op_eval_fbge_fcc3,
        gen_op_eval_fbuge_fcc3,
        gen_op_eval_fble_fcc3,
        gen_op_eval_fbule_fcc3,
        gen_op_eval_fbo_fcc3,
    },
#else
    {}, {}, {},
#endif
};

#ifdef TARGET_SPARC64
static void gen_cond_reg(int cond)
{
        switch (cond) {
        case 0x1:
            gen_op_eval_brz();
            break;
        case 0x2:
            gen_op_eval_brlez();
            break;
        case 0x3:
            gen_op_eval_brlz();
            break;
        case 0x5:
            gen_op_eval_brnz();
            break;
        case 0x6:
            gen_op_eval_brgz();
            break;
        default:
        case 0x7:
            gen_op_eval_brgez();
            break;
        }
}
#endif

/* XXX: potentially incorrect if dynamic npc */
static void do_branch(DisasContext * dc, int32_t offset, uint32_t insn, int cc)
{
    unsigned int cond = GET_FIELD(insn, 3, 6), a = (insn & (1 << 29));
    target_ulong target = dc->pc + offset;

    if (cond == 0x0) {
        /* unconditional not taken */
        if (a) {
            dc->pc = dc->npc + 4;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = dc->pc + 4;
        }
    } else if (cond == 0x8) {
        /* unconditional taken */
        if (a) {
            dc->pc = target;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = target;
        }
    } else {
        flush_T2(dc);
        gen_cond[cc][cond]();
        if (a) {
            gen_branch_a(dc, target, dc->npc);
            dc->is_br = 1;
        } else {
            dc->pc = dc->npc;
            dc->jump_pc[0] = target;
            dc->jump_pc[1] = dc->npc + 4;
            dc->npc = JUMP_PC;
        }
    }
}

/* XXX: potentially incorrect if dynamic npc */
static void do_fbranch(DisasContext * dc, int32_t offset, uint32_t insn, int cc)
{
    unsigned int cond = GET_FIELD(insn, 3, 6), a = (insn & (1 << 29));
    target_ulong target = dc->pc + offset;

    if (cond == 0x0) {
        /* unconditional not taken */
        if (a) {
            dc->pc = dc->npc + 4;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = dc->pc + 4;
        }
    } else if (cond == 0x8) {
        /* unconditional taken */
        if (a) {
            dc->pc = target;
            dc->npc = dc->pc + 4;
        } else {
            dc->pc = dc->npc;
            dc->npc = target;
        }
    } else {
        flush_T2(dc);
        gen_fcond[cc][cond]();
        if (a) {
            gen_branch_a(dc, target, dc->npc);
            dc->is_br = 1;
        } else {
            dc->pc = dc->npc;
            dc->jump_pc[0] = target;
            dc->jump_pc[1] = dc->npc + 4;
            dc->npc = JUMP_PC;
        }
    }
}

#ifdef TARGET_SPARC64
/* XXX: potentially incorrect if dynamic npc */
static void do_branch_reg(DisasContext * dc, int32_t offset, uint32_t insn)
{
    unsigned int cond = GET_FIELD_SP(insn, 25, 27), a = (insn & (1 << 29));
    target_ulong target = dc->pc + offset;

    flush_T2(dc);
    gen_cond_reg(cond);
    if (a) {
        gen_branch_a(dc, target, dc->npc);
        dc->is_br = 1;
    } else {
        dc->pc = dc->npc;
        dc->jump_pc[0] = target;
        dc->jump_pc[1] = dc->npc + 4;
        dc->npc = JUMP_PC;
    }
}

static GenOpFunc * const gen_fcmps[4] = {
    gen_op_fcmps,
    gen_op_fcmps_fcc1,
    gen_op_fcmps_fcc2,
    gen_op_fcmps_fcc3,
};

static GenOpFunc * const gen_fcmpd[4] = {
    gen_op_fcmpd,
    gen_op_fcmpd_fcc1,
    gen_op_fcmpd_fcc2,
    gen_op_fcmpd_fcc3,
};

#if defined(CONFIG_USER_ONLY)
static GenOpFunc * const gen_fcmpq[4] = {
    gen_op_fcmpq,
    gen_op_fcmpq_fcc1,
    gen_op_fcmpq_fcc2,
    gen_op_fcmpq_fcc3,
};
#endif

static GenOpFunc * const gen_fcmpes[4] = {
    gen_op_fcmpes,
    gen_op_fcmpes_fcc1,
    gen_op_fcmpes_fcc2,
    gen_op_fcmpes_fcc3,
};

static GenOpFunc * const gen_fcmped[4] = {
    gen_op_fcmped,
    gen_op_fcmped_fcc1,
    gen_op_fcmped_fcc2,
    gen_op_fcmped_fcc3,
};

#if defined(CONFIG_USER_ONLY)
static GenOpFunc * const gen_fcmpeq[4] = {
    gen_op_fcmpeq,
    gen_op_fcmpeq_fcc1,
    gen_op_fcmpeq_fcc2,
    gen_op_fcmpeq_fcc3,
};
#endif
#endif

static int gen_trap_ifnofpu(DisasContext * dc)
{
#if !defined(CONFIG_USER_ONLY)
    if (!dc->fpu_enabled) {
        save_state(dc);
        gen_op_exception(TT_NFPU_INSN);
        dc->is_br = 1;
        return 1;
    }
#endif
    return 0;
}

/* before an instruction, dc->pc must be static */
static void disas_sparc_insn(DisasContext * dc)
{
    unsigned int insn, opc, rs1, rs2, rd;

    insn = ldl_code(dc->pc);
    opc = GET_FIELD(insn, 0, 1);

    rd = GET_FIELD(insn, 2, 6);
    switch (opc) {
    case 0:                     /* branches/sethi */
        {
            unsigned int xop = GET_FIELD(insn, 7, 9);
            int32_t target;
            switch (xop) {
#ifdef TARGET_SPARC64
            case 0x1:           /* V9 BPcc */
                {
                    int cc;

                    target = GET_FIELD_SP(insn, 0, 18);
                    target = sign_extend(target, 18);
                    target <<= 2;
                    cc = GET_FIELD_SP(insn, 20, 21);
                    if (cc == 0)
                        do_branch(dc, target, insn, 0);
                    else if (cc == 2)
                        do_branch(dc, target, insn, 1);
                    else
                        goto illegal_insn;
                    goto jmp_insn;
                }
            case 0x3:           /* V9 BPr */
                {
                    target = GET_FIELD_SP(insn, 0, 13) |
                        (GET_FIELD_SP(insn, 20, 21) << 14);
                    target = sign_extend(target, 16);
                    target <<= 2;
                    rs1 = GET_FIELD(insn, 13, 17);
                    gen_movl_reg_T0(rs1);
                    do_branch_reg(dc, target, insn);
                    goto jmp_insn;
                }
            case 0x5:           /* V9 FBPcc */
                {
                    int cc = GET_FIELD_SP(insn, 20, 21);
                    if (gen_trap_ifnofpu(dc))
                        goto jmp_insn;
                    target = GET_FIELD_SP(insn, 0, 18);
                    target = sign_extend(target, 19);
                    target <<= 2;
                    do_fbranch(dc, target, insn, cc);
                    goto jmp_insn;
                }
#else
            case 0x7:           /* CBN+x */
                {
                    goto ncp_insn;
                }
#endif
            case 0x2:           /* BN+x */
                {
                    target = GET_FIELD(insn, 10, 31);
                    target = sign_extend(target, 22);
                    target <<= 2;
                    do_branch(dc, target, insn, 0);
                    goto jmp_insn;
                }
            case 0x6:           /* FBN+x */
                {
                    if (gen_trap_ifnofpu(dc))
                        goto jmp_insn;
                    target = GET_FIELD(insn, 10, 31);
                    target = sign_extend(target, 22);
                    target <<= 2;
                    do_fbranch(dc, target, insn, 0);
                    goto jmp_insn;
                }
            case 0x4:           /* SETHI */
#define OPTIM
#if defined(OPTIM)
                if (rd) { // nop
#endif
                    uint32_t value = GET_FIELD(insn, 10, 31);
                    gen_movl_imm_T0(value << 10);
                    gen_movl_T0_reg(rd);
#if defined(OPTIM)
                }
#endif
                break;
            case 0x0:           /* UNIMPL */
            default:
                goto illegal_insn;
            }
            break;
        }
        break;
    case 1:
        /*CALL*/ {
            target_long target = GET_FIELDs(insn, 2, 31) << 2;

#ifdef TARGET_SPARC64
            if (dc->pc == (uint32_t)dc->pc) {
                gen_op_movl_T0_im(dc->pc);
            } else {
                gen_op_movq_T0_im64(dc->pc >> 32, dc->pc);
            }
#else
            gen_op_movl_T0_im(dc->pc);
#endif
            gen_movl_T0_reg(15);
            target += dc->pc;
            gen_mov_pc_npc(dc);
            dc->npc = target;
        }
        goto jmp_insn;
    case 2:                     /* FPU & Logical Operations */
        {
            unsigned int xop = GET_FIELD(insn, 7, 12);
            if (xop == 0x3a) {  /* generate trap */
                int cond;

                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {
                    rs2 = GET_FIELD(insn, 25, 31);
#if defined(OPTIM)
                    if (rs2 != 0) {
#endif
                        gen_movl_simm_T1(rs2);
                        gen_op_add_T1_T0();
#if defined(OPTIM)
                    }
#endif
                } else {
                    rs2 = GET_FIELD(insn, 27, 31);
#if defined(OPTIM)
                    if (rs2 != 0) {
#endif
                        gen_movl_reg_T1(rs2);
                        gen_op_add_T1_T0();
#if defined(OPTIM)
                    }
#endif
                }
                cond = GET_FIELD(insn, 3, 6);
                if (cond == 0x8) {
                    save_state(dc);
                    gen_op_trap_T0();
                } else if (cond != 0) {
#ifdef TARGET_SPARC64
                    /* V9 icc/xcc */
                    int cc = GET_FIELD_SP(insn, 11, 12);
                    flush_T2(dc);
                    save_state(dc);
                    if (cc == 0)
                        gen_cond[0][cond]();
                    else if (cc == 2)
                        gen_cond[1][cond]();
                    else
                        goto illegal_insn;
#else
                    flush_T2(dc);
                    save_state(dc);
                    gen_cond[0][cond]();
#endif
                    gen_op_trapcc_T0();
                }
                gen_op_next_insn();
                gen_op_movl_T0_0();
                gen_op_exit_tb();
                dc->is_br = 1;
                goto jmp_insn;
            } else if (xop == 0x28) {
                rs1 = GET_FIELD(insn, 13, 17);
                switch(rs1) {
                case 0: /* rdy */
#ifndef TARGET_SPARC64
                case 0x01 ... 0x0e: /* undefined in the SPARCv8
                                       manual, rdy on the microSPARC
                                       II */
                case 0x0f:          /* stbar in the SPARCv8 manual,
                                       rdy on the microSPARC II */
                case 0x10 ... 0x1f: /* implementation-dependent in the
                                       SPARCv8 manual, rdy on the
                                       microSPARC II */
#endif
                    gen_op_movtl_T0_env(offsetof(CPUSPARCState, y));
                    gen_movl_T0_reg(rd);
                    break;
#ifdef TARGET_SPARC64
                case 0x2: /* V9 rdccr */
                    gen_op_rdccr();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x3: /* V9 rdasi */
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, asi));
                    gen_movl_T0_reg(rd);
                    break;
                case 0x4: /* V9 rdtick */
                    gen_op_rdtick();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x5: /* V9 rdpc */
                    if (dc->pc == (uint32_t)dc->pc) {
                        gen_op_movl_T0_im(dc->pc);
                    } else {
                        gen_op_movq_T0_im64(dc->pc >> 32, dc->pc);
                    }
                    gen_movl_T0_reg(rd);
                    break;
                case 0x6: /* V9 rdfprs */
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, fprs));
                    gen_movl_T0_reg(rd);
                    break;
                case 0xf: /* V9 membar */
                    break; /* no effect */
                case 0x13: /* Graphics Status */
                    if (gen_trap_ifnofpu(dc))
                        goto jmp_insn;
                    gen_op_movtl_T0_env(offsetof(CPUSPARCState, gsr));
                    gen_movl_T0_reg(rd);
                    break;
                case 0x17: /* Tick compare */
                    gen_op_movtl_T0_env(offsetof(CPUSPARCState, tick_cmpr));
                    gen_movl_T0_reg(rd);
                    break;
                case 0x18: /* System tick */
                    gen_op_rdstick();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x19: /* System tick compare */
                    gen_op_movtl_T0_env(offsetof(CPUSPARCState, stick_cmpr));
                    gen_movl_T0_reg(rd);
                    break;
                case 0x10: /* Performance Control */
                case 0x11: /* Performance Instrumentation Counter */
                case 0x12: /* Dispatch Control */
                case 0x14: /* Softint set, WO */
                case 0x15: /* Softint clear, WO */
                case 0x16: /* Softint write */
#endif
                default:
                    goto illegal_insn;
                }
#if !defined(CONFIG_USER_ONLY)
            } else if (xop == 0x29) { /* rdpsr / UA2005 rdhpr */
#ifndef TARGET_SPARC64
                if (!supervisor(dc))
                    goto priv_insn;
                gen_op_rdpsr();
#else
                if (!hypervisor(dc))
                    goto priv_insn;
                rs1 = GET_FIELD(insn, 13, 17);
                switch (rs1) {
                case 0: // hpstate
                    // gen_op_rdhpstate();
                    break;
                case 1: // htstate
                    // gen_op_rdhtstate();
                    break;
                case 3: // hintp
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, hintp));
                    break;
                case 5: // htba
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, htba));
                    break;
                case 6: // hver
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, hver));
                    break;
                case 31: // hstick_cmpr
                    gen_op_movl_env_T0(offsetof(CPUSPARCState, hstick_cmpr));
                    break;
                default:
                    goto illegal_insn;
                }
#endif
                gen_movl_T0_reg(rd);
                break;
            } else if (xop == 0x2a) { /* rdwim / V9 rdpr */
                if (!supervisor(dc))
                    goto priv_insn;
#ifdef TARGET_SPARC64
                rs1 = GET_FIELD(insn, 13, 17);
                switch (rs1) {
                case 0: // tpc
                    gen_op_rdtpc();
                    break;
                case 1: // tnpc
                    gen_op_rdtnpc();
                    break;
                case 2: // tstate
                    gen_op_rdtstate();
                    break;
                case 3: // tt
                    gen_op_rdtt();
                    break;
                case 4: // tick
                    gen_op_rdtick();
                    break;
                case 5: // tba
                    gen_op_movtl_T0_env(offsetof(CPUSPARCState, tbr));
                    break;
                case 6: // pstate
                    gen_op_rdpstate();
                    break;
                case 7: // tl
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, tl));
                    break;
                case 8: // pil
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, psrpil));
                    break;
                case 9: // cwp
                    gen_op_rdcwp();
                    break;
                case 10: // cansave
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, cansave));
                    break;
                case 11: // canrestore
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, canrestore));
                    break;
                case 12: // cleanwin
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, cleanwin));
                    break;
                case 13: // otherwin
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, otherwin));
                    break;
                case 14: // wstate
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, wstate));
                    break;
                case 16: // UA2005 gl
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, gl));
                    break;
                case 26: // UA2005 strand status
                    if (!hypervisor(dc))
                        goto priv_insn;
                    gen_op_movl_T0_env(offsetof(CPUSPARCState, ssr));
                    break;
                case 31: // ver
                    gen_op_movtl_T0_env(offsetof(CPUSPARCState, version));
                    break;
                case 15: // fq
                default:
                    goto illegal_insn;
                }
#else
                gen_op_movl_T0_env(offsetof(CPUSPARCState, wim));
#endif
                gen_movl_T0_reg(rd);
                break;
            } else if (xop == 0x2b) { /* rdtbr / V9 flushw */
#ifdef TARGET_SPARC64
                gen_op_flushw();
#else
                if (!supervisor(dc))
                    goto priv_insn;
                gen_op_movtl_T0_env(offsetof(CPUSPARCState, tbr));
                gen_movl_T0_reg(rd);
#endif
                break;
#endif
            } else if (xop == 0x34) {   /* FPU Operations */
                if (gen_trap_ifnofpu(dc))
                    goto jmp_insn;
                gen_op_clear_ieee_excp_and_FTT();
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                xop = GET_FIELD(insn, 18, 26);
                switch (xop) {
                    case 0x1: /* fmovs */
                        gen_op_load_fpr_FT0(rs2);
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x5: /* fnegs */
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fnegs();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x9: /* fabss */
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fabss();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x29: /* fsqrts */
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fsqrts();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x2a: /* fsqrtd */
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fsqrtd();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x2b: /* fsqrtq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fsqrtq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x41:
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fadds();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x42:
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_faddd();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x43: /* faddq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_faddq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x45:
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fsubs();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x46:
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fsubd();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x47: /* fsubq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fsubq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x49:
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fmuls();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x4a:
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fmuld();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x4b: /* fmulq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fmulq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x4d:
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fdivs();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x4e:
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fdivd();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x4f: /* fdivq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fdivq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x69:
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fsmuld();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x6e: /* fdmulq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fdmulq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xc4:
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fitos();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0xc6:
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fdtos();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0xc7: /* fqtos */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fqtos();
                        gen_op_store_FT0_fpr(rd);
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xc8:
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fitod();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0xc9:
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fstod();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0xcb: /* fqtod */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fqtod();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xcc: /* fitoq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fitoq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xcd: /* fstoq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fstoq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xce: /* fdtoq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fdtoq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xd1:
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fstoi();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0xd2:
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fdtoi();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0xd3: /* fqtoi */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fqtoi();
                        gen_op_store_FT0_fpr(rd);
                        break;
#else
                        goto nfpu_insn;
#endif
#ifdef TARGET_SPARC64
                    case 0x2: /* V9 fmovd */
                        gen_op_load_fpr_DT0(DFPREG(rs2));
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x3: /* V9 fmovq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs2));
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x6: /* V9 fnegd */
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fnegd();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x7: /* V9 fnegq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fnegq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0xa: /* V9 fabsd */
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fabsd();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0xb: /* V9 fabsq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fabsq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x81: /* V9 fstox */
                        gen_op_load_fpr_FT1(rs2);
                        gen_op_fstox();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x82: /* V9 fdtox */
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fdtox();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x83: /* V9 fqtox */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        gen_op_fqtox();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x84: /* V9 fxtos */
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fxtos();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x88: /* V9 fxtod */
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fxtod();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x8c: /* V9 fxtoq */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        gen_op_fxtoq();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
#endif
                    default:
                        goto illegal_insn;
                }
            } else if (xop == 0x35) {   /* FPU Operations */
#ifdef TARGET_SPARC64
                int cond;
#endif
                if (gen_trap_ifnofpu(dc))
                    goto jmp_insn;
                gen_op_clear_ieee_excp_and_FTT();
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                xop = GET_FIELD(insn, 18, 26);
#ifdef TARGET_SPARC64
                if ((xop & 0x11f) == 0x005) { // V9 fmovsr
                    cond = GET_FIELD_SP(insn, 14, 17);
                    gen_op_load_fpr_FT0(rd);
                    gen_op_load_fpr_FT1(rs2);
                    rs1 = GET_FIELD(insn, 13, 17);
                    gen_movl_reg_T0(rs1);
                    flush_T2(dc);
                    gen_cond_reg(cond);
                    gen_op_fmovs_cc();
                    gen_op_store_FT0_fpr(rd);
                    break;
                } else if ((xop & 0x11f) == 0x006) { // V9 fmovdr
                    cond = GET_FIELD_SP(insn, 14, 17);
                    gen_op_load_fpr_DT0(DFPREG(rd));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    flush_T2(dc);
                    rs1 = GET_FIELD(insn, 13, 17);
                    gen_movl_reg_T0(rs1);
                    gen_cond_reg(cond);
                    gen_op_fmovs_cc();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                } else if ((xop & 0x11f) == 0x007) { // V9 fmovqr
#if defined(CONFIG_USER_ONLY)
                    cond = GET_FIELD_SP(insn, 14, 17);
                    gen_op_load_fpr_QT0(QFPREG(rd));
                    gen_op_load_fpr_QT1(QFPREG(rs2));
                    flush_T2(dc);
                    rs1 = GET_FIELD(insn, 13, 17);
                    gen_movl_reg_T0(rs1);
                    gen_cond_reg(cond);
                    gen_op_fmovq_cc();
                    gen_op_store_QT0_fpr(QFPREG(rd));
                    break;
#else
                    goto nfpu_insn;
#endif
                }
#endif
                switch (xop) {
#ifdef TARGET_SPARC64
                    case 0x001: /* V9 fmovscc %fcc0 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_FT0(rd);
                        gen_op_load_fpr_FT1(rs2);
                        flush_T2(dc);
                        gen_fcond[0][cond]();
                        gen_op_fmovs_cc();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x002: /* V9 fmovdcc %fcc0 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_DT0(DFPREG(rd));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[0][cond]();
                        gen_op_fmovd_cc();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x003: /* V9 fmovqcc %fcc0 */
#if defined(CONFIG_USER_ONLY)
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_QT0(QFPREG(rd));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[0][cond]();
                        gen_op_fmovq_cc();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x041: /* V9 fmovscc %fcc1 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_FT0(rd);
                        gen_op_load_fpr_FT1(rs2);
                        flush_T2(dc);
                        gen_fcond[1][cond]();
                        gen_op_fmovs_cc();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x042: /* V9 fmovdcc %fcc1 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_DT0(DFPREG(rd));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[1][cond]();
                        gen_op_fmovd_cc();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x043: /* V9 fmovqcc %fcc1 */
#if defined(CONFIG_USER_ONLY)
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_QT0(QFPREG(rd));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[1][cond]();
                        gen_op_fmovq_cc();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x081: /* V9 fmovscc %fcc2 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_FT0(rd);
                        gen_op_load_fpr_FT1(rs2);
                        flush_T2(dc);
                        gen_fcond[2][cond]();
                        gen_op_fmovs_cc();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x082: /* V9 fmovdcc %fcc2 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_DT0(DFPREG(rd));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[2][cond]();
                        gen_op_fmovd_cc();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x083: /* V9 fmovqcc %fcc2 */
#if defined(CONFIG_USER_ONLY)
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_QT0(rd);
                        gen_op_load_fpr_QT1(rs2);
                        flush_T2(dc);
                        gen_fcond[2][cond]();
                        gen_op_fmovq_cc();
                        gen_op_store_QT0_fpr(rd);
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x0c1: /* V9 fmovscc %fcc3 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_FT0(rd);
                        gen_op_load_fpr_FT1(rs2);
                        flush_T2(dc);
                        gen_fcond[3][cond]();
                        gen_op_fmovs_cc();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x0c2: /* V9 fmovdcc %fcc3 */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_DT0(DFPREG(rd));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[3][cond]();
                        gen_op_fmovd_cc();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x0c3: /* V9 fmovqcc %fcc3 */
#if defined(CONFIG_USER_ONLY)
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_QT0(QFPREG(rd));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
                        flush_T2(dc);
                        gen_fcond[3][cond]();
                        gen_op_fmovq_cc();
                        gen_op_store_QT0_fpr(QFPREG(rd));
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x101: /* V9 fmovscc %icc */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_FT0(rd);
                        gen_op_load_fpr_FT1(rs2);
                        flush_T2(dc);
                        gen_cond[0][cond]();
                        gen_op_fmovs_cc();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x102: /* V9 fmovdcc %icc */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_DT0(DFPREG(rd));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        flush_T2(dc);
                        gen_cond[0][cond]();
                        gen_op_fmovd_cc();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x103: /* V9 fmovqcc %icc */
#if defined(CONFIG_USER_ONLY)
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_QT0(rd);
                        gen_op_load_fpr_QT1(rs2);
                        flush_T2(dc);
                        gen_cond[0][cond]();
                        gen_op_fmovq_cc();
                        gen_op_store_QT0_fpr(rd);
                        break;
#else
                        goto nfpu_insn;
#endif
                    case 0x181: /* V9 fmovscc %xcc */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_FT0(rd);
                        gen_op_load_fpr_FT1(rs2);
                        flush_T2(dc);
                        gen_cond[1][cond]();
                        gen_op_fmovs_cc();
                        gen_op_store_FT0_fpr(rd);
                        break;
                    case 0x182: /* V9 fmovdcc %xcc */
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_DT0(DFPREG(rd));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
                        flush_T2(dc);
                        gen_cond[1][cond]();
                        gen_op_fmovd_cc();
                        gen_op_store_DT0_fpr(DFPREG(rd));
                        break;
                    case 0x183: /* V9 fmovqcc %xcc */
#if defined(CONFIG_USER_ONLY)
                        cond = GET_FIELD_SP(insn, 14, 17);
                        gen_op_load_fpr_QT0(rd);
                        gen_op_load_fpr_QT1(rs2);
                        flush_T2(dc);
                        gen_cond[1][cond]();
                        gen_op_fmovq_cc();
                        gen_op_store_QT0_fpr(rd);
                        break;
#else
                        goto nfpu_insn;
#endif
#endif
                    case 0x51: /* fcmps, V9 %fcc */
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
#ifdef TARGET_SPARC64
                        gen_fcmps[rd & 3]();
#else
                        gen_op_fcmps();
#endif
                        break;
                    case 0x52: /* fcmpd, V9 %fcc */
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
#ifdef TARGET_SPARC64
                        gen_fcmpd[rd & 3]();
#else
                        gen_op_fcmpd();
#endif
                        break;
                    case 0x53: /* fcmpq, V9 %fcc */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
#ifdef TARGET_SPARC64
                        gen_fcmpq[rd & 3]();
#else
                        gen_op_fcmpq();
#endif
                        break;
#else /* !defined(CONFIG_USER_ONLY) */
                        goto nfpu_insn;
#endif
                    case 0x55: /* fcmpes, V9 %fcc */
                        gen_op_load_fpr_FT0(rs1);
                        gen_op_load_fpr_FT1(rs2);
#ifdef TARGET_SPARC64
                        gen_fcmpes[rd & 3]();
#else
                        gen_op_fcmpes();
#endif
                        break;
                    case 0x56: /* fcmped, V9 %fcc */
                        gen_op_load_fpr_DT0(DFPREG(rs1));
                        gen_op_load_fpr_DT1(DFPREG(rs2));
#ifdef TARGET_SPARC64
                        gen_fcmped[rd & 3]();
#else
                        gen_op_fcmped();
#endif
                        break;
                    case 0x57: /* fcmpeq, V9 %fcc */
#if defined(CONFIG_USER_ONLY)
                        gen_op_load_fpr_QT0(QFPREG(rs1));
                        gen_op_load_fpr_QT1(QFPREG(rs2));
#ifdef TARGET_SPARC64
                        gen_fcmpeq[rd & 3]();
#else
                        gen_op_fcmpeq();
#endif
                        break;
#else/* !defined(CONFIG_USER_ONLY) */
                        goto nfpu_insn;
#endif
                    default:
                        goto illegal_insn;
                }
#if defined(OPTIM)
            } else if (xop == 0x2) {
                // clr/mov shortcut

                rs1 = GET_FIELD(insn, 13, 17);
                if (rs1 == 0) {
                    // or %g0, x, y -> mov T1, x; mov y, T1
                    if (IS_IMM) {       /* immediate */
                        rs2 = GET_FIELDs(insn, 19, 31);
                        gen_movl_simm_T1(rs2);
                    } else {            /* register */
                        rs2 = GET_FIELD(insn, 27, 31);
                        gen_movl_reg_T1(rs2);
                    }
                    gen_movl_T1_reg(rd);
                } else {
                    gen_movl_reg_T0(rs1);
                    if (IS_IMM) {       /* immediate */
                        // or x, #0, y -> mov T1, x; mov y, T1
                        rs2 = GET_FIELDs(insn, 19, 31);
                        if (rs2 != 0) {
                            gen_movl_simm_T1(rs2);
                            gen_op_or_T1_T0();
                        }
                    } else {            /* register */
                        // or x, %g0, y -> mov T1, x; mov y, T1
                        rs2 = GET_FIELD(insn, 27, 31);
                        if (rs2 != 0) {
                            gen_movl_reg_T1(rs2);
                            gen_op_or_T1_T0();
                        }
                    }
                    gen_movl_T0_reg(rd);
                }
#endif
#ifdef TARGET_SPARC64
            } else if (xop == 0x25) { /* sll, V9 sllx */
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {   /* immediate */
                    rs2 = GET_FIELDs(insn, 20, 31);
                    gen_movl_simm_T1(rs2);
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    gen_movl_reg_T1(rs2);
                }
                if (insn & (1 << 12))
                    gen_op_sllx();
                else
                    gen_op_sll();
                gen_movl_T0_reg(rd);
            } else if (xop == 0x26) { /* srl, V9 srlx */
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {   /* immediate */
                    rs2 = GET_FIELDs(insn, 20, 31);
                    gen_movl_simm_T1(rs2);
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    gen_movl_reg_T1(rs2);
                }
                if (insn & (1 << 12))
                    gen_op_srlx();
                else
                    gen_op_srl();
                gen_movl_T0_reg(rd);
            } else if (xop == 0x27) { /* sra, V9 srax */
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {   /* immediate */
                    rs2 = GET_FIELDs(insn, 20, 31);
                    gen_movl_simm_T1(rs2);
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    gen_movl_reg_T1(rs2);
                }
                if (insn & (1 << 12))
                    gen_op_srax();
                else
                    gen_op_sra();
                gen_movl_T0_reg(rd);
#endif
            } else if (xop < 0x36) {
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {   /* immediate */
                    rs2 = GET_FIELDs(insn, 19, 31);
                    gen_movl_simm_T1(rs2);
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
                    gen_movl_reg_T1(rs2);
                }
                if (xop < 0x20) {
                    switch (xop & ~0x10) {
                    case 0x0:
                        if (xop & 0x10)
                            gen_op_add_T1_T0_cc();
                        else
                            gen_op_add_T1_T0();
                        break;
                    case 0x1:
                        gen_op_and_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x2:
                        gen_op_or_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x3:
                        gen_op_xor_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x4:
                        if (xop & 0x10)
                            gen_op_sub_T1_T0_cc();
                        else
                            gen_op_sub_T1_T0();
                        break;
                    case 0x5:
                        gen_op_andn_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x6:
                        gen_op_orn_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x7:
                        gen_op_xnor_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0x8:
                        if (xop & 0x10)
                            gen_op_addx_T1_T0_cc();
                        else
                            gen_op_addx_T1_T0();
                        break;
#ifdef TARGET_SPARC64
                    case 0x9: /* V9 mulx */
                        gen_op_mulx_T1_T0();
                        break;
#endif
                    case 0xa:
                        gen_op_umul_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0xb:
                        gen_op_smul_T1_T0();
                        if (xop & 0x10)
                            gen_op_logic_T0_cc();
                        break;
                    case 0xc:
                        if (xop & 0x10)
                            gen_op_subx_T1_T0_cc();
                        else
                            gen_op_subx_T1_T0();
                        break;
#ifdef TARGET_SPARC64
                    case 0xd: /* V9 udivx */
                        gen_op_udivx_T1_T0();
                        break;
#endif
                    case 0xe:
                        gen_op_udiv_T1_T0();
                        if (xop & 0x10)
                            gen_op_div_cc();
                        break;
                    case 0xf:
                        gen_op_sdiv_T1_T0();
                        if (xop & 0x10)
                            gen_op_div_cc();
                        break;
                    default:
                        goto illegal_insn;
                    }
                    gen_movl_T0_reg(rd);
                } else {
                    switch (xop) {
                    case 0x20: /* taddcc */
                        gen_op_tadd_T1_T0_cc();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x21: /* tsubcc */
                        gen_op_tsub_T1_T0_cc();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x22: /* taddcctv */
                        save_state(dc);
                        gen_op_tadd_T1_T0_ccTV();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x23: /* tsubcctv */
                        save_state(dc);
                        gen_op_tsub_T1_T0_ccTV();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x24: /* mulscc */
                        gen_op_mulscc_T1_T0();
                        gen_movl_T0_reg(rd);
                        break;
#ifndef TARGET_SPARC64
                    case 0x25:  /* sll */
                        gen_op_sll();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x26:  /* srl */
                        gen_op_srl();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x27:  /* sra */
                        gen_op_sra();
                        gen_movl_T0_reg(rd);
                        break;
#endif
                    case 0x30:
                        {
                            switch(rd) {
                            case 0: /* wry */
                                gen_op_xor_T1_T0();
                                gen_op_movtl_env_T0(offsetof(CPUSPARCState, y));
                                break;
#ifndef TARGET_SPARC64
                            case 0x01 ... 0x0f: /* undefined in the
                                                   SPARCv8 manual, nop
                                                   on the microSPARC
                                                   II */
                            case 0x10 ... 0x1f: /* implementation-dependent
                                                   in the SPARCv8
                                                   manual, nop on the
                                                   microSPARC II */
                                break;
#else
                            case 0x2: /* V9 wrccr */
                                gen_op_xor_T1_T0();
                                gen_op_wrccr();
                                break;
                            case 0x3: /* V9 wrasi */
                                gen_op_xor_T1_T0();
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, asi));
                                break;
                            case 0x6: /* V9 wrfprs */
                                gen_op_xor_T1_T0();
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, fprs));
                                save_state(dc);
                                gen_op_next_insn();
                                gen_op_movl_T0_0();
                                gen_op_exit_tb();
                                dc->is_br = 1;
                                break;
                            case 0xf: /* V9 sir, nop if user */
#if !defined(CONFIG_USER_ONLY)
                                if (supervisor(dc))
                                    gen_op_sir();
#endif
                                break;
                            case 0x13: /* Graphics Status */
                                if (gen_trap_ifnofpu(dc))
                                    goto jmp_insn;
                                gen_op_xor_T1_T0();
                                gen_op_movtl_env_T0(offsetof(CPUSPARCState, gsr));
                                break;
                            case 0x17: /* Tick compare */
#if !defined(CONFIG_USER_ONLY)
                                if (!supervisor(dc))
                                    goto illegal_insn;
#endif
                                gen_op_xor_T1_T0();
                                gen_op_movtl_env_T0(offsetof(CPUSPARCState, tick_cmpr));
                                gen_op_wrtick_cmpr();
                                break;
                            case 0x18: /* System tick */
#if !defined(CONFIG_USER_ONLY)
                                if (!supervisor(dc))
                                    goto illegal_insn;
#endif
                                gen_op_xor_T1_T0();
                                gen_op_wrstick();
                                break;
                            case 0x19: /* System tick compare */
#if !defined(CONFIG_USER_ONLY)
                                if (!supervisor(dc))
                                    goto illegal_insn;
#endif
                                gen_op_xor_T1_T0();
                                gen_op_movtl_env_T0(offsetof(CPUSPARCState, stick_cmpr));
                                gen_op_wrstick_cmpr();
                                break;

                            case 0x10: /* Performance Control */
                            case 0x11: /* Performance Instrumentation Counter */
                            case 0x12: /* Dispatch Control */
                            case 0x14: /* Softint set */
                            case 0x15: /* Softint clear */
                            case 0x16: /* Softint write */
#endif
                            default:
                                goto illegal_insn;
                            }
                        }
                        break;
#if !defined(CONFIG_USER_ONLY)
                    case 0x31: /* wrpsr, V9 saved, restored */
                        {
                            if (!supervisor(dc))
                                goto priv_insn;
#ifdef TARGET_SPARC64
                            switch (rd) {
                            case 0:
                                gen_op_saved();
                                break;
                            case 1:
                                gen_op_restored();
                                break;
                            case 2: /* UA2005 allclean */
                            case 3: /* UA2005 otherw */
                            case 4: /* UA2005 normalw */
                            case 5: /* UA2005 invalw */
                                // XXX
                            default:
                                goto illegal_insn;
                            }
#else
                            gen_op_xor_T1_T0();
                            gen_op_wrpsr();
                            save_state(dc);
                            gen_op_next_insn();
                            gen_op_movl_T0_0();
                            gen_op_exit_tb();
                            dc->is_br = 1;
#endif
                        }
                        break;
                    case 0x32: /* wrwim, V9 wrpr */
                        {
                            if (!supervisor(dc))
                                goto priv_insn;
                            gen_op_xor_T1_T0();
#ifdef TARGET_SPARC64
                            switch (rd) {
                            case 0: // tpc
                                gen_op_wrtpc();
                                break;
                            case 1: // tnpc
                                gen_op_wrtnpc();
                                break;
                            case 2: // tstate
                                gen_op_wrtstate();
                                break;
                            case 3: // tt
                                gen_op_wrtt();
                                break;
                            case 4: // tick
                                gen_op_wrtick();
                                break;
                            case 5: // tba
                                gen_op_movtl_env_T0(offsetof(CPUSPARCState, tbr));
                                break;
                            case 6: // pstate
                                gen_op_wrpstate();
                                save_state(dc);
                                gen_op_next_insn();
                                gen_op_movl_T0_0();
                                gen_op_exit_tb();
                                dc->is_br = 1;
                                break;
                            case 7: // tl
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, tl));
                                break;
                            case 8: // pil
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, psrpil));
                                break;
                            case 9: // cwp
                                gen_op_wrcwp();
                                break;
                            case 10: // cansave
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, cansave));
                                break;
                            case 11: // canrestore
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, canrestore));
                                break;
                            case 12: // cleanwin
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, cleanwin));
                                break;
                            case 13: // otherwin
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, otherwin));
                                break;
                            case 14: // wstate
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, wstate));
                                break;
                            case 16: // UA2005 gl
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, gl));
                                break;
                            case 26: // UA2005 strand status
                                if (!hypervisor(dc))
                                    goto priv_insn;
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, ssr));
                                break;
                            default:
                                goto illegal_insn;
                            }
#else
                            gen_op_wrwim();
#endif
                        }
                        break;
                    case 0x33: /* wrtbr, UA2005 wrhpr */
                        {
#ifndef TARGET_SPARC64
                            if (!supervisor(dc))
                                goto priv_insn;
                            gen_op_xor_T1_T0();
                            gen_op_movtl_env_T0(offsetof(CPUSPARCState, tbr));
#else
                            if (!hypervisor(dc))
                                goto priv_insn;
                            gen_op_xor_T1_T0();
                            switch (rd) {
                            case 0: // hpstate
                                // XXX gen_op_wrhpstate();
                                save_state(dc);
                                gen_op_next_insn();
                                gen_op_movl_T0_0();
                                gen_op_exit_tb();
                                dc->is_br = 1;
                                break;
                            case 1: // htstate
                                // XXX gen_op_wrhtstate();
                                break;
                            case 3: // hintp
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, hintp));
                                break;
                            case 5: // htba
                                gen_op_movl_env_T0(offsetof(CPUSPARCState, htba));
                                break;
                            case 31: // hstick_cmpr
                                gen_op_movtl_env_T0(offsetof(CPUSPARCState, hstick_cmpr));
                                gen_op_wrhstick_cmpr();
                                break;
                            case 6: // hver readonly
                            default:
                                goto illegal_insn;
                            }
#endif
                        }
                        break;
#endif
#ifdef TARGET_SPARC64
                    case 0x2c: /* V9 movcc */
                        {
                            int cc = GET_FIELD_SP(insn, 11, 12);
                            int cond = GET_FIELD_SP(insn, 14, 17);
                            if (IS_IMM) {       /* immediate */
                                rs2 = GET_FIELD_SPs(insn, 0, 10);
                                gen_movl_simm_T1(rs2);
                            }
                            else {
                                rs2 = GET_FIELD_SP(insn, 0, 4);
                                gen_movl_reg_T1(rs2);
                            }
                            gen_movl_reg_T0(rd);
                            flush_T2(dc);
                            if (insn & (1 << 18)) {
                                if (cc == 0)
                                    gen_cond[0][cond]();
                                else if (cc == 2)
                                    gen_cond[1][cond]();
                                else
                                    goto illegal_insn;
                            } else {
                                gen_fcond[cc][cond]();
                            }
                            gen_op_mov_cc();
                            gen_movl_T0_reg(rd);
                            break;
                        }
                    case 0x2d: /* V9 sdivx */
                        gen_op_sdivx_T1_T0();
                        gen_movl_T0_reg(rd);
                        break;
                    case 0x2e: /* V9 popc */
                        {
                            if (IS_IMM) {       /* immediate */
                                rs2 = GET_FIELD_SPs(insn, 0, 12);
                                gen_movl_simm_T1(rs2);
                                // XXX optimize: popc(constant)
                            }
                            else {
                                rs2 = GET_FIELD_SP(insn, 0, 4);
                                gen_movl_reg_T1(rs2);
                            }
                            gen_op_popc();
                            gen_movl_T0_reg(rd);
                        }
                    case 0x2f: /* V9 movr */
                        {
                            int cond = GET_FIELD_SP(insn, 10, 12);
                            rs1 = GET_FIELD(insn, 13, 17);
                            flush_T2(dc);
                            gen_movl_reg_T0(rs1);
                            gen_cond_reg(cond);
                            if (IS_IMM) {       /* immediate */
                                rs2 = GET_FIELD_SPs(insn, 0, 9);
                                gen_movl_simm_T1(rs2);
                            }
                            else {
                                rs2 = GET_FIELD_SP(insn, 0, 4);
                                gen_movl_reg_T1(rs2);
                            }
                            gen_movl_reg_T0(rd);
                            gen_op_mov_cc();
                            gen_movl_T0_reg(rd);
                            break;
                        }
#endif
                    default:
                        goto illegal_insn;
                    }
                }
            } else if (xop == 0x36) { /* UltraSparc shutdown, VIS, V8 CPop1 */
#ifdef TARGET_SPARC64
                int opf = GET_FIELD_SP(insn, 5, 13);
                rs1 = GET_FIELD(insn, 13, 17);
                rs2 = GET_FIELD(insn, 27, 31);
                if (gen_trap_ifnofpu(dc))
                    goto jmp_insn;

                switch (opf) {
                case 0x000: /* VIS I edge8cc */
                case 0x001: /* VIS II edge8n */
                case 0x002: /* VIS I edge8lcc */
                case 0x003: /* VIS II edge8ln */
                case 0x004: /* VIS I edge16cc */
                case 0x005: /* VIS II edge16n */
                case 0x006: /* VIS I edge16lcc */
                case 0x007: /* VIS II edge16ln */
                case 0x008: /* VIS I edge32cc */
                case 0x009: /* VIS II edge32n */
                case 0x00a: /* VIS I edge32lcc */
                case 0x00b: /* VIS II edge32ln */
                    // XXX
                    goto illegal_insn;
                case 0x010: /* VIS I array8 */
                    gen_movl_reg_T0(rs1);
                    gen_movl_reg_T1(rs2);
                    gen_op_array8();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x012: /* VIS I array16 */
                    gen_movl_reg_T0(rs1);
                    gen_movl_reg_T1(rs2);
                    gen_op_array16();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x014: /* VIS I array32 */
                    gen_movl_reg_T0(rs1);
                    gen_movl_reg_T1(rs2);
                    gen_op_array32();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x018: /* VIS I alignaddr */
                    gen_movl_reg_T0(rs1);
                    gen_movl_reg_T1(rs2);
                    gen_op_alignaddr();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x019: /* VIS II bmask */
                case 0x01a: /* VIS I alignaddrl */
                    // XXX
                    goto illegal_insn;
                case 0x020: /* VIS I fcmple16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmple16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x022: /* VIS I fcmpne16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmpne16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x024: /* VIS I fcmple32 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmple32();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x026: /* VIS I fcmpne32 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmpne32();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x028: /* VIS I fcmpgt16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmpgt16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x02a: /* VIS I fcmpeq16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmpeq16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x02c: /* VIS I fcmpgt32 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmpgt32();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x02e: /* VIS I fcmpeq32 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fcmpeq32();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x031: /* VIS I fmul8x16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmul8x16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x033: /* VIS I fmul8x16au */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmul8x16au();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x035: /* VIS I fmul8x16al */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmul8x16al();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x036: /* VIS I fmul8sux16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmul8sux16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x037: /* VIS I fmul8ulx16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmul8ulx16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x038: /* VIS I fmuld8sux16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmuld8sux16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x039: /* VIS I fmuld8ulx16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fmuld8ulx16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x03a: /* VIS I fpack32 */
                case 0x03b: /* VIS I fpack16 */
                case 0x03d: /* VIS I fpackfix */
                case 0x03e: /* VIS I pdist */
                    // XXX
                    goto illegal_insn;
                case 0x048: /* VIS I faligndata */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_faligndata();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x04b: /* VIS I fpmerge */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fpmerge();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x04c: /* VIS II bshuffle */
                    // XXX
                    goto illegal_insn;
                case 0x04d: /* VIS I fexpand */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fexpand();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x050: /* VIS I fpadd16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fpadd16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x051: /* VIS I fpadd16s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fpadd16s();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x052: /* VIS I fpadd32 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fpadd32();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x053: /* VIS I fpadd32s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fpadd32s();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x054: /* VIS I fpsub16 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fpsub16();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x055: /* VIS I fpsub16s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fpsub16s();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x056: /* VIS I fpsub32 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fpadd32();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x057: /* VIS I fpsub32s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fpsub32s();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x060: /* VIS I fzero */
                    gen_op_movl_DT0_0();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x061: /* VIS I fzeros */
                    gen_op_movl_FT0_0();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x062: /* VIS I fnor */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fnor();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x063: /* VIS I fnors */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fnors();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x064: /* VIS I fandnot2 */
                    gen_op_load_fpr_DT1(DFPREG(rs1));
                    gen_op_load_fpr_DT0(DFPREG(rs2));
                    gen_op_fandnot();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x065: /* VIS I fandnot2s */
                    gen_op_load_fpr_FT1(rs1);
                    gen_op_load_fpr_FT0(rs2);
                    gen_op_fandnots();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x066: /* VIS I fnot2 */
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fnot();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x067: /* VIS I fnot2s */
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fnot();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x068: /* VIS I fandnot1 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fandnot();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x069: /* VIS I fandnot1s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fandnots();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x06a: /* VIS I fnot1 */
                    gen_op_load_fpr_DT1(DFPREG(rs1));
                    gen_op_fnot();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x06b: /* VIS I fnot1s */
                    gen_op_load_fpr_FT1(rs1);
                    gen_op_fnot();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x06c: /* VIS I fxor */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fxor();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x06d: /* VIS I fxors */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fxors();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x06e: /* VIS I fnand */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fnand();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x06f: /* VIS I fnands */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fnands();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x070: /* VIS I fand */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fand();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x071: /* VIS I fands */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fands();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x072: /* VIS I fxnor */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fxnor();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x073: /* VIS I fxnors */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fxnors();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x074: /* VIS I fsrc1 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x075: /* VIS I fsrc1s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x076: /* VIS I fornot2 */
                    gen_op_load_fpr_DT1(DFPREG(rs1));
                    gen_op_load_fpr_DT0(DFPREG(rs2));
                    gen_op_fornot();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x077: /* VIS I fornot2s */
                    gen_op_load_fpr_FT1(rs1);
                    gen_op_load_fpr_FT0(rs2);
                    gen_op_fornots();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x078: /* VIS I fsrc2 */
                    gen_op_load_fpr_DT0(DFPREG(rs2));
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x079: /* VIS I fsrc2s */
                    gen_op_load_fpr_FT0(rs2);
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x07a: /* VIS I fornot1 */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_fornot();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x07b: /* VIS I fornot1s */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fornots();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x07c: /* VIS I for */
                    gen_op_load_fpr_DT0(DFPREG(rs1));
                    gen_op_load_fpr_DT1(DFPREG(rs2));
                    gen_op_for();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x07d: /* VIS I fors */
                    gen_op_load_fpr_FT0(rs1);
                    gen_op_load_fpr_FT1(rs2);
                    gen_op_fors();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x07e: /* VIS I fone */
                    gen_op_movl_DT0_1();
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                case 0x07f: /* VIS I fones */
                    gen_op_movl_FT0_1();
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x080: /* VIS I shutdown */
                case 0x081: /* VIS II siam */
                    // XXX
                    goto illegal_insn;
                default:
                    goto illegal_insn;
                }
#else
                goto ncp_insn;
#endif
            } else if (xop == 0x37) { /* V8 CPop2, V9 impdep2 */
#ifdef TARGET_SPARC64
                goto illegal_insn;
#else
                goto ncp_insn;
#endif
#ifdef TARGET_SPARC64
            } else if (xop == 0x39) { /* V9 return */
                rs1 = GET_FIELD(insn, 13, 17);
                save_state(dc);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {   /* immediate */
                    rs2 = GET_FIELDs(insn, 19, 31);
#if defined(OPTIM)
                    if (rs2) {
#endif
                        gen_movl_simm_T1(rs2);
                        gen_op_add_T1_T0();
#if defined(OPTIM)
                    }
#endif
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
#if defined(OPTIM)
                    if (rs2) {
#endif
                        gen_movl_reg_T1(rs2);
                        gen_op_add_T1_T0();
#if defined(OPTIM)
                    }
#endif
                }
                gen_op_restore();
                gen_mov_pc_npc(dc);
                gen_op_check_align_T0_3();
                gen_op_movl_npc_T0();
                dc->npc = DYNAMIC_PC;
                goto jmp_insn;
#endif
            } else {
                rs1 = GET_FIELD(insn, 13, 17);
                gen_movl_reg_T0(rs1);
                if (IS_IMM) {   /* immediate */
                    rs2 = GET_FIELDs(insn, 19, 31);
#if defined(OPTIM)
                    if (rs2) {
#endif
                        gen_movl_simm_T1(rs2);
                        gen_op_add_T1_T0();
#if defined(OPTIM)
                    }
#endif
                } else {                /* register */
                    rs2 = GET_FIELD(insn, 27, 31);
#if defined(OPTIM)
                    if (rs2) {
#endif
                        gen_movl_reg_T1(rs2);
                        gen_op_add_T1_T0();
#if defined(OPTIM)
                    }
#endif
                }
                switch (xop) {
                case 0x38:      /* jmpl */
                    {
                        if (rd != 0) {
#ifdef TARGET_SPARC64
                            if (dc->pc == (uint32_t)dc->pc) {
                                gen_op_movl_T1_im(dc->pc);
                            } else {
                                gen_op_movq_T1_im64(dc->pc >> 32, dc->pc);
                            }
#else
                            gen_op_movl_T1_im(dc->pc);
#endif
                            gen_movl_T1_reg(rd);
                        }
                        gen_mov_pc_npc(dc);
                        gen_op_check_align_T0_3();
                        gen_op_movl_npc_T0();
                        dc->npc = DYNAMIC_PC;
                    }
                    goto jmp_insn;
#if !defined(CONFIG_USER_ONLY) && !defined(TARGET_SPARC64)
                case 0x39:      /* rett, V9 return */
                    {
                        if (!supervisor(dc))
                            goto priv_insn;
                        gen_mov_pc_npc(dc);
                        gen_op_check_align_T0_3();
                        gen_op_movl_npc_T0();
                        dc->npc = DYNAMIC_PC;
                        gen_op_rett();
                    }
                    goto jmp_insn;
#endif
                case 0x3b: /* flush */
                    gen_op_flush_T0();
                    break;
                case 0x3c:      /* save */
                    save_state(dc);
                    gen_op_save();
                    gen_movl_T0_reg(rd);
                    break;
                case 0x3d:      /* restore */
                    save_state(dc);
                    gen_op_restore();
                    gen_movl_T0_reg(rd);
                    break;
#if !defined(CONFIG_USER_ONLY) && defined(TARGET_SPARC64)
                case 0x3e:      /* V9 done/retry */
                    {
                        switch (rd) {
                        case 0:
                            if (!supervisor(dc))
                                goto priv_insn;
                            dc->npc = DYNAMIC_PC;
                            dc->pc = DYNAMIC_PC;
                            gen_op_done();
                            goto jmp_insn;
                        case 1:
                            if (!supervisor(dc))
                                goto priv_insn;
                            dc->npc = DYNAMIC_PC;
                            dc->pc = DYNAMIC_PC;
                            gen_op_retry();
                            goto jmp_insn;
                        default:
                            goto illegal_insn;
                        }
                    }
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            }
            break;
        }
        break;
    case 3:                     /* load/store instructions */
        {
            unsigned int xop = GET_FIELD(insn, 7, 12);
            rs1 = GET_FIELD(insn, 13, 17);
            save_state(dc);
            gen_movl_reg_T0(rs1);
            if (xop == 0x3c || xop == 0x3e)
            {
                rs2 = GET_FIELD(insn, 27, 31);
                gen_movl_reg_T1(rs2);
            }
            else if (IS_IMM) {       /* immediate */
                rs2 = GET_FIELDs(insn, 19, 31);
#if defined(OPTIM)
                if (rs2 != 0) {
#endif
                    gen_movl_simm_T1(rs2);
                    gen_op_add_T1_T0();
#if defined(OPTIM)
                }
#endif
            } else {            /* register */
                rs2 = GET_FIELD(insn, 27, 31);
#if defined(OPTIM)
                if (rs2 != 0) {
#endif
                    gen_movl_reg_T1(rs2);
                    gen_op_add_T1_T0();
#if defined(OPTIM)
                }
#endif
            }
            if (xop < 4 || (xop > 7 && xop < 0x14 && xop != 0x0e) ||
                (xop > 0x17 && xop <= 0x1d ) ||
                (xop > 0x2c && xop <= 0x33) || xop == 0x1f || xop == 0x3d) {
                switch (xop) {
                case 0x0:       /* load word */
                    gen_op_check_align_T0_3();
#ifndef TARGET_SPARC64
                    gen_op_ldst(ld);
#else
                    gen_op_ldst(lduw);
#endif
                    break;
                case 0x1:       /* load unsigned byte */
                    gen_op_ldst(ldub);
                    break;
                case 0x2:       /* load unsigned halfword */
                    gen_op_check_align_T0_1();
                    gen_op_ldst(lduh);
                    break;
                case 0x3:       /* load double word */
                    if (rd & 1)
                        goto illegal_insn;
                    gen_op_check_align_T0_7();
                    gen_op_ldst(ldd);
                    gen_movl_T0_reg(rd + 1);
                    break;
                case 0x9:       /* load signed byte */
                    gen_op_ldst(ldsb);
                    break;
                case 0xa:       /* load signed halfword */
                    gen_op_check_align_T0_1();
                    gen_op_ldst(ldsh);
                    break;
                case 0xd:       /* ldstub -- XXX: should be atomically */
                    gen_op_ldst(ldstub);
                    break;
                case 0x0f:      /* swap register with memory. Also atomically */
                    gen_op_check_align_T0_3();
                    gen_movl_reg_T1(rd);
                    gen_op_ldst(swap);
                    break;
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x10:      /* load word alternate */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_op_check_align_T0_3();
                    gen_ld_asi(insn, 4, 0);
                    break;
                case 0x11:      /* load unsigned byte alternate */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_ld_asi(insn, 1, 0);
                    break;
                case 0x12:      /* load unsigned halfword alternate */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_op_check_align_T0_1();
                    gen_ld_asi(insn, 2, 0);
                    break;
                case 0x13:      /* load double word alternate */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    if (rd & 1)
                        goto illegal_insn;
                    gen_op_check_align_T0_7();
                    gen_ldda_asi(insn);
                    gen_movl_T0_reg(rd + 1);
                    break;
                case 0x19:      /* load signed byte alternate */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_ld_asi(insn, 1, 1);
                    break;
                case 0x1a:      /* load signed halfword alternate */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_op_check_align_T0_1();
                    gen_ld_asi(insn, 2, 1);
                    break;
                case 0x1d:      /* ldstuba -- XXX: should be atomically */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_ldstub_asi(insn);
                    break;
                case 0x1f:      /* swap reg with alt. memory. Also atomically */
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_op_check_align_T0_3();
                    gen_movl_reg_T1(rd);
                    gen_swap_asi(insn);
                    break;

#ifndef TARGET_SPARC64
                case 0x30: /* ldc */
                case 0x31: /* ldcsr */
                case 0x33: /* lddc */
                    goto ncp_insn;
#endif
#endif
#ifdef TARGET_SPARC64
                case 0x08: /* V9 ldsw */
                    gen_op_check_align_T0_3();
                    gen_op_ldst(ldsw);
                    break;
                case 0x0b: /* V9 ldx */
                    gen_op_check_align_T0_7();
                    gen_op_ldst(ldx);
                    break;
                case 0x18: /* V9 ldswa */
                    gen_op_check_align_T0_3();
                    gen_ld_asi(insn, 4, 1);
                    break;
                case 0x1b: /* V9 ldxa */
                    gen_op_check_align_T0_7();
                    gen_ld_asi(insn, 8, 0);
                    break;
                case 0x2d: /* V9 prefetch, no effect */
                    goto skip_move;
                case 0x30: /* V9 ldfa */
                    gen_op_check_align_T0_3();
                    gen_ldf_asi(insn, 4, rd);
                    goto skip_move;
                case 0x33: /* V9 lddfa */
                    gen_op_check_align_T0_3();
                    gen_ldf_asi(insn, 8, DFPREG(rd));
                    goto skip_move;
                case 0x3d: /* V9 prefetcha, no effect */
                    goto skip_move;
                case 0x32: /* V9 ldqfa */
#if defined(CONFIG_USER_ONLY)
                    gen_op_check_align_T0_3();
                    gen_ldf_asi(insn, 16, QFPREG(rd));
                    goto skip_move;
#else
                    goto nfpu_insn;
#endif
#endif
                default:
                    goto illegal_insn;
                }
                gen_movl_T1_reg(rd);
#ifdef TARGET_SPARC64
            skip_move: ;
#endif
            } else if (xop >= 0x20 && xop < 0x24) {
                if (gen_trap_ifnofpu(dc))
                    goto jmp_insn;
                switch (xop) {
                case 0x20:      /* load fpreg */
                    gen_op_check_align_T0_3();
                    gen_op_ldst(ldf);
                    gen_op_store_FT0_fpr(rd);
                    break;
                case 0x21:      /* load fsr */
                    gen_op_check_align_T0_3();
                    gen_op_ldst(ldf);
                    gen_op_ldfsr();
                    break;
                case 0x22:      /* load quad fpreg */
#if defined(CONFIG_USER_ONLY)
                    gen_op_check_align_T0_7();
                    gen_op_ldst(ldqf);
                    gen_op_store_QT0_fpr(QFPREG(rd));
                    break;
#else
                    goto nfpu_insn;
#endif
                case 0x23:      /* load double fpreg */
                    gen_op_check_align_T0_7();
                    gen_op_ldst(lddf);
                    gen_op_store_DT0_fpr(DFPREG(rd));
                    break;
                default:
                    goto illegal_insn;
                }
            } else if (xop < 8 || (xop >= 0x14 && xop < 0x18) || \
                       xop == 0xe || xop == 0x1e) {
                gen_movl_reg_T1(rd);
                switch (xop) {
                case 0x4:
                    gen_op_check_align_T0_3();
                    gen_op_ldst(st);
                    break;
                case 0x5:
                    gen_op_ldst(stb);
                    break;
                case 0x6:
                    gen_op_check_align_T0_1();
                    gen_op_ldst(sth);
                    break;
                case 0x7:
                    if (rd & 1)
                        goto illegal_insn;
                    gen_op_check_align_T0_7();
                    flush_T2(dc);
                    gen_movl_reg_T2(rd + 1);
                    gen_op_ldst(std);
                    break;
#if !defined(CONFIG_USER_ONLY) || defined(TARGET_SPARC64)
                case 0x14:
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_op_check_align_T0_3();
                    gen_st_asi(insn, 4);
                    break;
                case 0x15:
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_st_asi(insn, 1);
                    break;
                case 0x16:
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    gen_op_check_align_T0_1();
                    gen_st_asi(insn, 2);
                    break;
                case 0x17:
#ifndef TARGET_SPARC64
                    if (IS_IMM)
                        goto illegal_insn;
                    if (!supervisor(dc))
                        goto priv_insn;
#endif
                    if (rd & 1)
                        goto illegal_insn;
                    gen_op_check_align_T0_7();
                    flush_T2(dc);
                    gen_movl_reg_T2(rd + 1);
                    gen_stda_asi(insn);
                    break;
#endif
#ifdef TARGET_SPARC64
                case 0x0e: /* V9 stx */
                    gen_op_check_align_T0_7();
                    gen_op_ldst(stx);
                    break;
                case 0x1e: /* V9 stxa */
                    gen_op_check_align_T0_7();
                    gen_st_asi(insn, 8);
                    break;
#endif
                default:
                    goto illegal_insn;
                }
            } else if (xop > 0x23 && xop < 0x28) {
                if (gen_trap_ifnofpu(dc))
                    goto jmp_insn;
                switch (xop) {
                case 0x24:
                    gen_op_check_align_T0_3();
                    gen_op_load_fpr_FT0(rd);
                    gen_op_ldst(stf);
                    break;
                case 0x25: /* stfsr, V9 stxfsr */
#ifdef CONFIG_USER_ONLY
                    gen_op_check_align_T0_3();
#endif
                    gen_op_stfsr();
                    gen_op_ldst(stf);
                    break;
                case 0x26:
#ifdef TARGET_SPARC64
#if defined(CONFIG_USER_ONLY)
                    /* V9 stqf, store quad fpreg */
                    gen_op_check_align_T0_7();
                    gen_op_load_fpr_QT0(QFPREG(rd));
                    gen_op_ldst(stqf);
                    break;
#else
                    goto nfpu_insn;
#endif
#else /* !TARGET_SPARC64 */
                    /* stdfq, store floating point queue */
#if defined(CONFIG_USER_ONLY)
                    goto illegal_insn;
#else
                    if (!supervisor(dc))
                        goto priv_insn;
                    if (gen_trap_ifnofpu(dc))
                        goto jmp_insn;
                    goto nfq_insn;
#endif
#endif
                case 0x27:
                    gen_op_check_align_T0_7();
                    gen_op_load_fpr_DT0(DFPREG(rd));
                    gen_op_ldst(stdf);
                    break;
                default:
                    goto illegal_insn;
                }
            } else if (xop > 0x33 && xop < 0x3f) {
                switch (xop) {
#ifdef TARGET_SPARC64
                case 0x34: /* V9 stfa */
                    gen_op_check_align_T0_3();
                    gen_op_load_fpr_FT0(rd);
                    gen_stf_asi(insn, 4, rd);
                    break;
                case 0x36: /* V9 stqfa */
#if defined(CONFIG_USER_ONLY)
                    gen_op_check_align_T0_7();
                    gen_op_load_fpr_QT0(QFPREG(rd));
                    gen_stf_asi(insn, 16, QFPREG(rd));
                    break;
#else
                    goto nfpu_insn;
#endif
                case 0x37: /* V9 stdfa */
                    gen_op_check_align_T0_3();
                    gen_op_load_fpr_DT0(DFPREG(rd));
                    gen_stf_asi(insn, 8, DFPREG(rd));
                    break;
                case 0x3c: /* V9 casa */
                    gen_op_check_align_T0_3();
                    flush_T2(dc);
                    gen_movl_reg_T2(rd);
                    gen_cas_asi(insn);
                    gen_movl_T1_reg(rd);
                    break;
                case 0x3e: /* V9 casxa */
                    gen_op_check_align_T0_7();
                    flush_T2(dc);
                    gen_movl_reg_T2(rd);
                    gen_casx_asi(insn);
                    gen_movl_T1_reg(rd);
                    break;
#else
                case 0x34: /* stc */
                case 0x35: /* stcsr */
                case 0x36: /* stdcq */
                case 0x37: /* stdc */
                    goto ncp_insn;
#endif
                default:
                    goto illegal_insn;
                }
            }
            else
                goto illegal_insn;
        }
        break;
    }
    /* default case for non jump instructions */
    if (dc->npc == DYNAMIC_PC) {
        dc->pc = DYNAMIC_PC;
        gen_op_next_insn();
    } else if (dc->npc == JUMP_PC) {
        /* we can do a static jump */
        gen_branch2(dc, dc->jump_pc[0], dc->jump_pc[1]);
        dc->is_br = 1;
    } else {
        dc->pc = dc->npc;
        dc->npc = dc->npc + 4;
    }
 jmp_insn:
    return;
 illegal_insn:
    save_state(dc);
    gen_op_exception(TT_ILL_INSN);
    dc->is_br = 1;
    return;
#if !defined(CONFIG_USER_ONLY)
 priv_insn:
    save_state(dc);
    gen_op_exception(TT_PRIV_INSN);
    dc->is_br = 1;
    return;
 nfpu_insn:
    save_state(dc);
    gen_op_fpexception_im(FSR_FTT_UNIMPFPOP);
    dc->is_br = 1;
    return;
#ifndef TARGET_SPARC64
 nfq_insn:
    save_state(dc);
    gen_op_fpexception_im(FSR_FTT_SEQ_ERROR);
    dc->is_br = 1;
    return;
#endif
#endif
#ifndef TARGET_SPARC64
 ncp_insn:
    save_state(dc);
    gen_op_exception(TT_NCP_INSN);
    dc->is_br = 1;
    return;
#endif
}

static inline int gen_intermediate_code_internal(TranslationBlock * tb,
                                                 int spc, CPUSPARCState *env)
{
    target_ulong pc_start, last_pc;
    uint16_t *gen_opc_end;
    DisasContext dc1, *dc = &dc1;
    int j, lj = -1;

    memset(dc, 0, sizeof(DisasContext));
    dc->tb = tb;
    pc_start = tb->pc;
    dc->pc = pc_start;
    last_pc = dc->pc;
    dc->npc = (target_ulong) tb->cs_base;
    dc->mem_idx = cpu_mmu_index(env);
    dc->fpu_enabled = cpu_fpu_enabled(env);
    gen_opc_ptr = gen_opc_buf;
    gen_opc_end = gen_opc_buf + OPC_MAX_SIZE;
    gen_opparam_ptr = gen_opparam_buf;
    nb_gen_labels = 0;

    do {
        if (env->nb_breakpoints > 0) {
            for(j = 0; j < env->nb_breakpoints; j++) {
                if (env->breakpoints[j] == dc->pc) {
                    if (dc->pc != pc_start)
                        save_state(dc);
                    gen_op_debug();
                    gen_op_movl_T0_0();
                    gen_op_exit_tb();
                    dc->is_br = 1;
                    goto exit_gen_loop;
                }
            }
        }
        if (spc) {
            if (loglevel > 0)
                fprintf(logfile, "Search PC...\n");
            j = gen_opc_ptr - gen_opc_buf;
            if (lj < j) {
                lj++;
                while (lj < j)
                    gen_opc_instr_start[lj++] = 0;
                gen_opc_pc[lj] = dc->pc;
                gen_opc_npc[lj] = dc->npc;
                gen_opc_instr_start[lj] = 1;
            }
        }
        last_pc = dc->pc;
        disas_sparc_insn(dc);

        if (dc->is_br)
            break;
        /* if the next PC is different, we abort now */
        if (dc->pc != (last_pc + 4))
            break;
        /* if we reach a page boundary, we stop generation so that the
           PC of a TT_TFAULT exception is always in the right page */
        if ((dc->pc & (TARGET_PAGE_SIZE - 1)) == 0)
            break;
        /* if single step mode, we generate only one instruction and
           generate an exception */
        if (env->singlestep_enabled) {
            gen_jmp_im(dc->pc);
            gen_op_movl_T0_0();
            gen_op_exit_tb();
            break;
        }
    } while ((gen_opc_ptr < gen_opc_end) &&
             (dc->pc - pc_start) < (TARGET_PAGE_SIZE - 32));

 exit_gen_loop:
    if (!dc->is_br) {
        if (dc->pc != DYNAMIC_PC &&
            (dc->npc != DYNAMIC_PC && dc->npc != JUMP_PC)) {
            /* static PC and NPC: we can use direct chaining */
            gen_branch(dc, dc->pc, dc->npc);
        } else {
            if (dc->pc != DYNAMIC_PC)
                gen_jmp_im(dc->pc);
            save_npc(dc);
            gen_op_movl_T0_0();
            gen_op_exit_tb();
        }
    }
    *gen_opc_ptr = INDEX_op_end;
    if (spc) {
        j = gen_opc_ptr - gen_opc_buf;
        lj++;
        while (lj <= j)
            gen_opc_instr_start[lj++] = 0;
#if 0
        if (loglevel > 0) {
            page_dump(logfile);
        }
#endif
        gen_opc_jump_pc[0] = dc->jump_pc[0];
        gen_opc_jump_pc[1] = dc->jump_pc[1];
    } else {
        tb->size = last_pc + 4 - pc_start;
    }
#ifdef DEBUG_DISAS
    if (loglevel & CPU_LOG_TB_IN_ASM) {
        fprintf(logfile, "--------------\n");
        fprintf(logfile, "IN: %s\n", lookup_symbol(pc_start));
        target_disas(logfile, pc_start, last_pc + 4 - pc_start, 0);
        fprintf(logfile, "\n");
        if (loglevel & CPU_LOG_TB_OP) {
            fprintf(logfile, "OP:\n");
            dump_ops(gen_opc_buf, gen_opparam_buf);
            fprintf(logfile, "\n");
        }
    }
#endif
    return 0;
}

int gen_intermediate_code(CPUSPARCState * env, TranslationBlock * tb)
{
    return gen_intermediate_code_internal(tb, 0, env);
}

int gen_intermediate_code_pc(CPUSPARCState * env, TranslationBlock * tb)
{
    return gen_intermediate_code_internal(tb, 1, env);
}

extern int ram_size;

void cpu_reset(CPUSPARCState *env)
{
    tlb_flush(env, 1);
    env->cwp = 0;
    env->wim = 1;
    env->regwptr = env->regbase + (env->cwp * 16);
#if defined(CONFIG_USER_ONLY)
    env->user_mode_only = 1;
#ifdef TARGET_SPARC64
    env->cleanwin = NWINDOWS - 2;
    env->cansave = NWINDOWS - 2;
    env->pstate = PS_RMO | PS_PEF | PS_IE;
    env->asi = 0x82; // Primary no-fault
#endif
#else
    env->psret = 0;
    env->psrs = 1;
    env->psrps = 1;
#ifdef TARGET_SPARC64
    env->pstate = PS_PRIV;
    env->hpstate = HS_PRIV;
    env->pc = 0x1fff0000000ULL;
#else
    env->pc = 0;
    env->mmuregs[0] &= ~(MMU_E | MMU_NF);
    env->mmuregs[0] |= env->mmu_bm;
#endif
    env->npc = env->pc + 4;
#endif
}

CPUSPARCState *cpu_sparc_init(const char *cpu_model)
{
    CPUSPARCState *env;
    const sparc_def_t *def;

    def = cpu_sparc_find_by_name(cpu_model);
    if (!def)
        return NULL;

    env = qemu_mallocz(sizeof(CPUSPARCState));
    if (!env)
        return NULL;
    cpu_exec_init(env);
    env->cpu_model_str = cpu_model;
    env->version = def->iu_version;
    env->fsr = def->fpu_version;
#if !defined(TARGET_SPARC64)
    env->mmu_bm = def->mmu_bm;
    env->mmuregs[0] |= def->mmu_version;
    cpu_sparc_set_id(env, 0);
#endif
    cpu_reset(env);
    
    return env;
}

void cpu_sparc_set_id(CPUSPARCState *env, unsigned int cpu)
{
#if !defined(TARGET_SPARC64)
    env->mxccregs[7] = ((cpu + 8) & 0xf) << 24;
#endif
}

static const sparc_def_t sparc_defs[] = {
#ifdef TARGET_SPARC64
    {
        .name = "Fujitsu Sparc64",
        .iu_version = ((0x04ULL << 48) | (0x02ULL << 32) | (0ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Fujitsu Sparc64 III",
        .iu_version = ((0x04ULL << 48) | (0x03ULL << 32) | (0ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Fujitsu Sparc64 IV",
        .iu_version = ((0x04ULL << 48) | (0x04ULL << 32) | (0ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Fujitsu Sparc64 V",
        .iu_version = ((0x04ULL << 48) | (0x05ULL << 32) | (0x51ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "TI UltraSparc I",
        .iu_version = ((0x17ULL << 48) | (0x10ULL << 32) | (0x40ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "TI UltraSparc II",
        .iu_version = ((0x17ULL << 48) | (0x11ULL << 32) | (0x20ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "TI UltraSparc IIi",
        .iu_version = ((0x17ULL << 48) | (0x12ULL << 32) | (0x91ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "TI UltraSparc IIe",
        .iu_version = ((0x17ULL << 48) | (0x13ULL << 32) | (0x14ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Sun UltraSparc III",
        .iu_version = ((0x3eULL << 48) | (0x14ULL << 32) | (0x34ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Sun UltraSparc III Cu",
        .iu_version = ((0x3eULL << 48) | (0x15ULL << 32) | (0x41ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Sun UltraSparc IIIi",
        .iu_version = ((0x3eULL << 48) | (0x16ULL << 32) | (0x34ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Sun UltraSparc IV",
        .iu_version = ((0x3eULL << 48) | (0x18ULL << 32) | (0x31ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Sun UltraSparc IV+",
        .iu_version = ((0x3eULL << 48) | (0x19ULL << 32) | (0x22ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "Sun UltraSparc IIIi+",
        .iu_version = ((0x3eULL << 48) | (0x22ULL << 32) | (0ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
    {
        .name = "NEC UltraSparc I",
        .iu_version = ((0x22ULL << 48) | (0x10ULL << 32) | (0x40ULL << 24)
                       | (MAXTL << 8) | (NWINDOWS - 1)),
        .fpu_version = 0x00000000,
        .mmu_version = 0,
    },
#else
    {
        .name = "Fujitsu MB86900",
        .iu_version = 0x00 << 24, /* Impl 0, ver 0 */
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0x00 << 24, /* Impl 0, ver 0 */
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Fujitsu MB86904",
        .iu_version = 0x04 << 24, /* Impl 0, ver 4 */
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0x04 << 24, /* Impl 0, ver 4 */
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Fujitsu MB86907",
        .iu_version = 0x05 << 24, /* Impl 0, ver 5 */
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0x05 << 24, /* Impl 0, ver 5 */
        .mmu_bm = 0x00004000,
    },
    {
        .name = "LSI L64811",
        .iu_version = 0x10 << 24, /* Impl 1, ver 0 */
        .fpu_version = 1 << 17, /* FPU version 1 (LSI L64814) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Cypress CY7C601",
        .iu_version = 0x11 << 24, /* Impl 1, ver 1 */
        .fpu_version = 3 << 17, /* FPU version 3 (Cypress CY7C602) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Cypress CY7C611",
        .iu_version = 0x13 << 24, /* Impl 1, ver 3 */
        .fpu_version = 3 << 17, /* FPU version 3 (Cypress CY7C602) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "TI SuperSparc II",
        .iu_version = 0x40000000,
        .fpu_version = 0 << 17,
        .mmu_version = 0x04000000,
        .mmu_bm = 0x00002000,
    },
    {
        .name = "TI MicroSparc I",
        .iu_version = 0x41000000,
        .fpu_version = 4 << 17,
        .mmu_version = 0x41000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "TI MicroSparc II",
        .iu_version = 0x42000000,
        .fpu_version = 4 << 17,
        .mmu_version = 0x02000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "TI MicroSparc IIep",
        .iu_version = 0x42000000,
        .fpu_version = 4 << 17,
        .mmu_version = 0x04000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "TI SuperSparc 51",
        .iu_version = 0x43000000,
        .fpu_version = 0 << 17,
        .mmu_version = 0x04000000,
        .mmu_bm = 0x00002000,
    },
    {
        .name = "TI SuperSparc 61",
        .iu_version = 0x44000000,
        .fpu_version = 0 << 17,
        .mmu_version = 0x04000000,
        .mmu_bm = 0x00002000,
    },
    {
        .name = "Ross RT625",
        .iu_version = 0x1e000000,
        .fpu_version = 1 << 17,
        .mmu_version = 0x1e000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Ross RT620",
        .iu_version = 0x1f000000,
        .fpu_version = 1 << 17,
        .mmu_version = 0x1f000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "BIT B5010",
        .iu_version = 0x20000000,
        .fpu_version = 0 << 17, /* B5010/B5110/B5120/B5210 */
        .mmu_version = 0x20000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Matsushita MN10501",
        .iu_version = 0x50000000,
        .fpu_version = 0 << 17,
        .mmu_version = 0x50000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "Weitek W8601",
        .iu_version = 0x90 << 24, /* Impl 9, ver 0 */
        .fpu_version = 3 << 17, /* FPU version 3 (Weitek WTL3170/2) */
        .mmu_version = 0x10 << 24,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "LEON2",
        .iu_version = 0xf2000000,
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0xf2000000,
        .mmu_bm = 0x00004000,
    },
    {
        .name = "LEON3",
        .iu_version = 0xf3000000,
        .fpu_version = 4 << 17, /* FPU version 4 (Meiko) */
        .mmu_version = 0xf3000000,
        .mmu_bm = 0x00004000,
    },
#endif
};

static const sparc_def_t *cpu_sparc_find_by_name(const unsigned char *name)
{
    unsigned int i;

    for (i = 0; i < sizeof(sparc_defs) / sizeof(sparc_def_t); i++) {
        if (strcasecmp(name, sparc_defs[i].name) == 0) {
            return &sparc_defs[i];
        }
    }
    return NULL;
}

void sparc_cpu_list (FILE *f, int (*cpu_fprintf)(FILE *f, const char *fmt, ...))
{
    unsigned int i;

    for (i = 0; i < sizeof(sparc_defs) / sizeof(sparc_def_t); i++) {
        (*cpu_fprintf)(f, "Sparc %16s IU " TARGET_FMT_lx " FPU %08x MMU %08x\n",
                       sparc_defs[i].name,
                       sparc_defs[i].iu_version,
                       sparc_defs[i].fpu_version,
                       sparc_defs[i].mmu_version);
    }
}

#define GET_FLAG(a,b) ((env->psr & a)?b:'-')

void cpu_dump_state(CPUState *env, FILE *f,
                    int (*cpu_fprintf)(FILE *f, const char *fmt, ...),
                    int flags)
{
    int i, x;

    cpu_fprintf(f, "pc: " TARGET_FMT_lx "  npc: " TARGET_FMT_lx "\n", env->pc, env->npc);
    cpu_fprintf(f, "General Registers:\n");
    for (i = 0; i < 4; i++)
        cpu_fprintf(f, "%%g%c: " TARGET_FMT_lx "\t", i + '0', env->gregs[i]);
    cpu_fprintf(f, "\n");
    for (; i < 8; i++)
        cpu_fprintf(f, "%%g%c: " TARGET_FMT_lx "\t", i + '0', env->gregs[i]);
    cpu_fprintf(f, "\nCurrent Register Window:\n");
    for (x = 0; x < 3; x++) {
        for (i = 0; i < 4; i++)
            cpu_fprintf(f, "%%%c%d: " TARGET_FMT_lx "\t",
                    (x == 0 ? 'o' : (x == 1 ? 'l' : 'i')), i,
                    env->regwptr[i + x * 8]);
        cpu_fprintf(f, "\n");
        for (; i < 8; i++)
            cpu_fprintf(f, "%%%c%d: " TARGET_FMT_lx "\t",
                    (x == 0 ? 'o' : x == 1 ? 'l' : 'i'), i,
                    env->regwptr[i + x * 8]);
        cpu_fprintf(f, "\n");
    }
    cpu_fprintf(f, "\nFloating Point Registers:\n");
    for (i = 0; i < 32; i++) {
        if ((i & 3) == 0)
            cpu_fprintf(f, "%%f%02d:", i);
        cpu_fprintf(f, " %016lf", env->fpr[i]);
        if ((i & 3) == 3)
            cpu_fprintf(f, "\n");
    }
#ifdef TARGET_SPARC64
    cpu_fprintf(f, "pstate: 0x%08x ccr: 0x%02x asi: 0x%02x tl: %d fprs: %d\n",
                env->pstate, GET_CCR(env), env->asi, env->tl, env->fprs);
    cpu_fprintf(f, "cansave: %d canrestore: %d otherwin: %d wstate %d cleanwin %d cwp %d\n",
                env->cansave, env->canrestore, env->otherwin, env->wstate,
                env->cleanwin, NWINDOWS - 1 - env->cwp);
#else
    cpu_fprintf(f, "psr: 0x%08x -> %c%c%c%c %c%c%c wim: 0x%08x\n", GET_PSR(env),
            GET_FLAG(PSR_ZERO, 'Z'), GET_FLAG(PSR_OVF, 'V'),
            GET_FLAG(PSR_NEG, 'N'), GET_FLAG(PSR_CARRY, 'C'),
            env->psrs?'S':'-', env->psrps?'P':'-',
            env->psret?'E':'-', env->wim);
#endif
    cpu_fprintf(f, "fsr: 0x%08x\n", GET_FSR32(env));
}

#if defined(CONFIG_USER_ONLY)
target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    return addr;
}

#else
extern int get_physical_address (CPUState *env, target_phys_addr_t *physical, int *prot,
                                 int *access_index, target_ulong address, int rw,
                                 int mmu_idx);

target_phys_addr_t cpu_get_phys_page_debug(CPUState *env, target_ulong addr)
{
    target_phys_addr_t phys_addr;
    int prot, access_index;

    if (get_physical_address(env, &phys_addr, &prot, &access_index, addr, 2, 0) != 0)
        if (get_physical_address(env, &phys_addr, &prot, &access_index, addr, 0, 0) != 0)
            return -1;
    if (cpu_get_physical_page_desc(phys_addr) == IO_MEM_UNASSIGNED)
        return -1;
    return phys_addr;
}
#endif

void helper_flush(target_ulong addr)
{
    addr &= ~7;
    tb_invalidate_page_range(addr, addr + 8);
}
