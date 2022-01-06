/*
 * QEMU RISC-V Disassembler
 *
 * Copyright (c) 2016-2017 Michael Clark <michaeljclark@mac.com>
 * Copyright (c) 2017-2018 SiFive, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "disas/dis-asm.h"


/* types */

typedef uint64_t rv_inst;
typedef uint16_t rv_opcode;

/* enums */

typedef enum {
    rv32,
    rv64,
    rv128
} rv_isa;

typedef enum {
    rv_rm_rne = 0,
    rv_rm_rtz = 1,
    rv_rm_rdn = 2,
    rv_rm_rup = 3,
    rv_rm_rmm = 4,
    rv_rm_dyn = 7,
} rv_rm;

typedef enum {
    rv_fence_i = 8,
    rv_fence_o = 4,
    rv_fence_r = 2,
    rv_fence_w = 1,
} rv_fence;

typedef enum {
    rv_ireg_zero,
    rv_ireg_ra,
    rv_ireg_sp,
    rv_ireg_gp,
    rv_ireg_tp,
    rv_ireg_t0,
    rv_ireg_t1,
    rv_ireg_t2,
    rv_ireg_s0,
    rv_ireg_s1,
    rv_ireg_a0,
    rv_ireg_a1,
    rv_ireg_a2,
    rv_ireg_a3,
    rv_ireg_a4,
    rv_ireg_a5,
    rv_ireg_a6,
    rv_ireg_a7,
    rv_ireg_s2,
    rv_ireg_s3,
    rv_ireg_s4,
    rv_ireg_s5,
    rv_ireg_s6,
    rv_ireg_s7,
    rv_ireg_s8,
    rv_ireg_s9,
    rv_ireg_s10,
    rv_ireg_s11,
    rv_ireg_t3,
    rv_ireg_t4,
    rv_ireg_t5,
    rv_ireg_t6,
} rv_ireg;

typedef enum {
    rvc_end,
    rvc_rd_eq_ra,
    rvc_rd_eq_x0,
    rvc_rs1_eq_x0,
    rvc_rs2_eq_x0,
    rvc_rs2_eq_rs1,
    rvc_rs1_eq_ra,
    rvc_imm_eq_zero,
    rvc_imm_eq_n1,
    rvc_imm_eq_p1,
    rvc_csr_eq_0x001,
    rvc_csr_eq_0x002,
    rvc_csr_eq_0x003,
    rvc_csr_eq_0xc00,
    rvc_csr_eq_0xc01,
    rvc_csr_eq_0xc02,
    rvc_csr_eq_0xc80,
    rvc_csr_eq_0xc81,
    rvc_csr_eq_0xc82,
} rvc_constraint;

typedef enum {
    rv_codec_illegal,
    rv_codec_none,
    rv_codec_u,
    rv_codec_uj,
    rv_codec_i,
    rv_codec_i_sh5,
    rv_codec_i_sh6,
    rv_codec_i_sh7,
    rv_codec_i_csr,
    rv_codec_s,
    rv_codec_sb,
    rv_codec_r,
    rv_codec_r_m,
    rv_codec_r4_m,
    rv_codec_r_a,
    rv_codec_r_l,
    rv_codec_r_f,
    rv_codec_cb,
    rv_codec_cb_imm,
    rv_codec_cb_sh5,
    rv_codec_cb_sh6,
    rv_codec_ci,
    rv_codec_ci_sh5,
    rv_codec_ci_sh6,
    rv_codec_ci_16sp,
    rv_codec_ci_lwsp,
    rv_codec_ci_ldsp,
    rv_codec_ci_lqsp,
    rv_codec_ci_li,
    rv_codec_ci_lui,
    rv_codec_ci_none,
    rv_codec_ciw_4spn,
    rv_codec_cj,
    rv_codec_cj_jal,
    rv_codec_cl_lw,
    rv_codec_cl_ld,
    rv_codec_cl_lq,
    rv_codec_cr,
    rv_codec_cr_mv,
    rv_codec_cr_jalr,
    rv_codec_cr_jr,
    rv_codec_cs,
    rv_codec_cs_sw,
    rv_codec_cs_sd,
    rv_codec_cs_sq,
    rv_codec_css_swsp,
    rv_codec_css_sdsp,
    rv_codec_css_sqsp,
} rv_codec;

typedef enum {
    rv_op_illegal = 0,
    rv_op_lui = 1,
    rv_op_auipc = 2,
    rv_op_jal = 3,
    rv_op_jalr = 4,
    rv_op_beq = 5,
    rv_op_bne = 6,
    rv_op_blt = 7,
    rv_op_bge = 8,
    rv_op_bltu = 9,
    rv_op_bgeu = 10,
    rv_op_lb = 11,
    rv_op_lh = 12,
    rv_op_lw = 13,
    rv_op_lbu = 14,
    rv_op_lhu = 15,
    rv_op_sb = 16,
    rv_op_sh = 17,
    rv_op_sw = 18,
    rv_op_addi = 19,
    rv_op_slti = 20,
    rv_op_sltiu = 21,
    rv_op_xori = 22,
    rv_op_ori = 23,
    rv_op_andi = 24,
    rv_op_slli = 25,
    rv_op_srli = 26,
    rv_op_srai = 27,
    rv_op_add = 28,
    rv_op_sub = 29,
    rv_op_sll = 30,
    rv_op_slt = 31,
    rv_op_sltu = 32,
    rv_op_xor = 33,
    rv_op_srl = 34,
    rv_op_sra = 35,
    rv_op_or = 36,
    rv_op_and = 37,
    rv_op_fence = 38,
    rv_op_fence_i = 39,
    rv_op_lwu = 40,
    rv_op_ld = 41,
    rv_op_sd = 42,
    rv_op_addiw = 43,
    rv_op_slliw = 44,
    rv_op_srliw = 45,
    rv_op_sraiw = 46,
    rv_op_addw = 47,
    rv_op_subw = 48,
    rv_op_sllw = 49,
    rv_op_srlw = 50,
    rv_op_sraw = 51,
    rv_op_ldu = 52,
    rv_op_lq = 53,
    rv_op_sq = 54,
    rv_op_addid = 55,
    rv_op_sllid = 56,
    rv_op_srlid = 57,
    rv_op_sraid = 58,
    rv_op_addd = 59,
    rv_op_subd = 60,
    rv_op_slld = 61,
    rv_op_srld = 62,
    rv_op_srad = 63,
    rv_op_mul = 64,
    rv_op_mulh = 65,
    rv_op_mulhsu = 66,
    rv_op_mulhu = 67,
    rv_op_div = 68,
    rv_op_divu = 69,
    rv_op_rem = 70,
    rv_op_remu = 71,
    rv_op_mulw = 72,
    rv_op_divw = 73,
    rv_op_divuw = 74,
    rv_op_remw = 75,
    rv_op_remuw = 76,
    rv_op_muld = 77,
    rv_op_divd = 78,
    rv_op_divud = 79,
    rv_op_remd = 80,
    rv_op_remud = 81,
    rv_op_lr_w = 82,
    rv_op_sc_w = 83,
    rv_op_amoswap_w = 84,
    rv_op_amoadd_w = 85,
    rv_op_amoxor_w = 86,
    rv_op_amoor_w = 87,
    rv_op_amoand_w = 88,
    rv_op_amomin_w = 89,
    rv_op_amomax_w = 90,
    rv_op_amominu_w = 91,
    rv_op_amomaxu_w = 92,
    rv_op_lr_d = 93,
    rv_op_sc_d = 94,
    rv_op_amoswap_d = 95,
    rv_op_amoadd_d = 96,
    rv_op_amoxor_d = 97,
    rv_op_amoor_d = 98,
    rv_op_amoand_d = 99,
    rv_op_amomin_d = 100,
    rv_op_amomax_d = 101,
    rv_op_amominu_d = 102,
    rv_op_amomaxu_d = 103,
    rv_op_lr_q = 104,
    rv_op_sc_q = 105,
    rv_op_amoswap_q = 106,
    rv_op_amoadd_q = 107,
    rv_op_amoxor_q = 108,
    rv_op_amoor_q = 109,
    rv_op_amoand_q = 110,
    rv_op_amomin_q = 111,
    rv_op_amomax_q = 112,
    rv_op_amominu_q = 113,
    rv_op_amomaxu_q = 114,
    rv_op_ecall = 115,
    rv_op_ebreak = 116,
    rv_op_uret = 117,
    rv_op_sret = 118,
    rv_op_hret = 119,
    rv_op_mret = 120,
    rv_op_dret = 121,
    rv_op_sfence_vm = 122,
    rv_op_sfence_vma = 123,
    rv_op_wfi = 124,
    rv_op_csrrw = 125,
    rv_op_csrrs = 126,
    rv_op_csrrc = 127,
    rv_op_csrrwi = 128,
    rv_op_csrrsi = 129,
    rv_op_csrrci = 130,
    rv_op_flw = 131,
    rv_op_fsw = 132,
    rv_op_fmadd_s = 133,
    rv_op_fmsub_s = 134,
    rv_op_fnmsub_s = 135,
    rv_op_fnmadd_s = 136,
    rv_op_fadd_s = 137,
    rv_op_fsub_s = 138,
    rv_op_fmul_s = 139,
    rv_op_fdiv_s = 140,
    rv_op_fsgnj_s = 141,
    rv_op_fsgnjn_s = 142,
    rv_op_fsgnjx_s = 143,
    rv_op_fmin_s = 144,
    rv_op_fmax_s = 145,
    rv_op_fsqrt_s = 146,
    rv_op_fle_s = 147,
    rv_op_flt_s = 148,
    rv_op_feq_s = 149,
    rv_op_fcvt_w_s = 150,
    rv_op_fcvt_wu_s = 151,
    rv_op_fcvt_s_w = 152,
    rv_op_fcvt_s_wu = 153,
    rv_op_fmv_x_s = 154,
    rv_op_fclass_s = 155,
    rv_op_fmv_s_x = 156,
    rv_op_fcvt_l_s = 157,
    rv_op_fcvt_lu_s = 158,
    rv_op_fcvt_s_l = 159,
    rv_op_fcvt_s_lu = 160,
    rv_op_fld = 161,
    rv_op_fsd = 162,
    rv_op_fmadd_d = 163,
    rv_op_fmsub_d = 164,
    rv_op_fnmsub_d = 165,
    rv_op_fnmadd_d = 166,
    rv_op_fadd_d = 167,
    rv_op_fsub_d = 168,
    rv_op_fmul_d = 169,
    rv_op_fdiv_d = 170,
    rv_op_fsgnj_d = 171,
    rv_op_fsgnjn_d = 172,
    rv_op_fsgnjx_d = 173,
    rv_op_fmin_d = 174,
    rv_op_fmax_d = 175,
    rv_op_fcvt_s_d = 176,
    rv_op_fcvt_d_s = 177,
    rv_op_fsqrt_d = 178,
    rv_op_fle_d = 179,
    rv_op_flt_d = 180,
    rv_op_feq_d = 181,
    rv_op_fcvt_w_d = 182,
    rv_op_fcvt_wu_d = 183,
    rv_op_fcvt_d_w = 184,
    rv_op_fcvt_d_wu = 185,
    rv_op_fclass_d = 186,
    rv_op_fcvt_l_d = 187,
    rv_op_fcvt_lu_d = 188,
    rv_op_fmv_x_d = 189,
    rv_op_fcvt_d_l = 190,
    rv_op_fcvt_d_lu = 191,
    rv_op_fmv_d_x = 192,
    rv_op_flq = 193,
    rv_op_fsq = 194,
    rv_op_fmadd_q = 195,
    rv_op_fmsub_q = 196,
    rv_op_fnmsub_q = 197,
    rv_op_fnmadd_q = 198,
    rv_op_fadd_q = 199,
    rv_op_fsub_q = 200,
    rv_op_fmul_q = 201,
    rv_op_fdiv_q = 202,
    rv_op_fsgnj_q = 203,
    rv_op_fsgnjn_q = 204,
    rv_op_fsgnjx_q = 205,
    rv_op_fmin_q = 206,
    rv_op_fmax_q = 207,
    rv_op_fcvt_s_q = 208,
    rv_op_fcvt_q_s = 209,
    rv_op_fcvt_d_q = 210,
    rv_op_fcvt_q_d = 211,
    rv_op_fsqrt_q = 212,
    rv_op_fle_q = 213,
    rv_op_flt_q = 214,
    rv_op_feq_q = 215,
    rv_op_fcvt_w_q = 216,
    rv_op_fcvt_wu_q = 217,
    rv_op_fcvt_q_w = 218,
    rv_op_fcvt_q_wu = 219,
    rv_op_fclass_q = 220,
    rv_op_fcvt_l_q = 221,
    rv_op_fcvt_lu_q = 222,
    rv_op_fcvt_q_l = 223,
    rv_op_fcvt_q_lu = 224,
    rv_op_fmv_x_q = 225,
    rv_op_fmv_q_x = 226,
    rv_op_c_addi4spn = 227,
    rv_op_c_fld = 228,
    rv_op_c_lw = 229,
    rv_op_c_flw = 230,
    rv_op_c_fsd = 231,
    rv_op_c_sw = 232,
    rv_op_c_fsw = 233,
    rv_op_c_nop = 234,
    rv_op_c_addi = 235,
    rv_op_c_jal = 236,
    rv_op_c_li = 237,
    rv_op_c_addi16sp = 238,
    rv_op_c_lui = 239,
    rv_op_c_srli = 240,
    rv_op_c_srai = 241,
    rv_op_c_andi = 242,
    rv_op_c_sub = 243,
    rv_op_c_xor = 244,
    rv_op_c_or = 245,
    rv_op_c_and = 246,
    rv_op_c_subw = 247,
    rv_op_c_addw = 248,
    rv_op_c_j = 249,
    rv_op_c_beqz = 250,
    rv_op_c_bnez = 251,
    rv_op_c_slli = 252,
    rv_op_c_fldsp = 253,
    rv_op_c_lwsp = 254,
    rv_op_c_flwsp = 255,
    rv_op_c_jr = 256,
    rv_op_c_mv = 257,
    rv_op_c_ebreak = 258,
    rv_op_c_jalr = 259,
    rv_op_c_add = 260,
    rv_op_c_fsdsp = 261,
    rv_op_c_swsp = 262,
    rv_op_c_fswsp = 263,
    rv_op_c_ld = 264,
    rv_op_c_sd = 265,
    rv_op_c_addiw = 266,
    rv_op_c_ldsp = 267,
    rv_op_c_sdsp = 268,
    rv_op_c_lq = 269,
    rv_op_c_sq = 270,
    rv_op_c_lqsp = 271,
    rv_op_c_sqsp = 272,
    rv_op_nop = 273,
    rv_op_mv = 274,
    rv_op_not = 275,
    rv_op_neg = 276,
    rv_op_negw = 277,
    rv_op_sext_w = 278,
    rv_op_seqz = 279,
    rv_op_snez = 280,
    rv_op_sltz = 281,
    rv_op_sgtz = 282,
    rv_op_fmv_s = 283,
    rv_op_fabs_s = 284,
    rv_op_fneg_s = 285,
    rv_op_fmv_d = 286,
    rv_op_fabs_d = 287,
    rv_op_fneg_d = 288,
    rv_op_fmv_q = 289,
    rv_op_fabs_q = 290,
    rv_op_fneg_q = 291,
    rv_op_beqz = 292,
    rv_op_bnez = 293,
    rv_op_blez = 294,
    rv_op_bgez = 295,
    rv_op_bltz = 296,
    rv_op_bgtz = 297,
    rv_op_ble = 298,
    rv_op_bleu = 299,
    rv_op_bgt = 300,
    rv_op_bgtu = 301,
    rv_op_j = 302,
    rv_op_ret = 303,
    rv_op_jr = 304,
    rv_op_rdcycle = 305,
    rv_op_rdtime = 306,
    rv_op_rdinstret = 307,
    rv_op_rdcycleh = 308,
    rv_op_rdtimeh = 309,
    rv_op_rdinstreth = 310,
    rv_op_frcsr = 311,
    rv_op_frrm = 312,
    rv_op_frflags = 313,
    rv_op_fscsr = 314,
    rv_op_fsrm = 315,
    rv_op_fsflags = 316,
    rv_op_fsrmi = 317,
    rv_op_fsflagsi = 318,
    rv_op_bseti = 319,
    rv_op_bclri = 320,
    rv_op_binvi = 321,
    rv_op_bexti = 322,
    rv_op_rori = 323,
    rv_op_clz = 324,
    rv_op_ctz = 325,
    rv_op_cpop = 326,
    rv_op_sext_h = 327,
    rv_op_sext_b = 328,
    rv_op_xnor = 329,
    rv_op_orn = 330,
    rv_op_andn = 331,
    rv_op_rol = 332,
    rv_op_ror = 333,
    rv_op_sh1add = 334,
    rv_op_sh2add = 335,
    rv_op_sh3add = 336,
    rv_op_sh1add_uw = 337,
    rv_op_sh2add_uw = 338,
    rv_op_sh3add_uw = 339,
    rv_op_clmul = 340,
    rv_op_clmulr = 341,
    rv_op_clmulh = 342,
    rv_op_min = 343,
    rv_op_minu = 344,
    rv_op_max = 345,
    rv_op_maxu = 346,
    rv_op_clzw = 347,
    rv_op_ctzw = 348,
    rv_op_cpopw = 349,
    rv_op_slli_uw = 350,
    rv_op_add_uw = 351,
    rv_op_rolw = 352,
    rv_op_rorw = 353,
    rv_op_rev8 = 354,
    rv_op_zext_h = 355,
    rv_op_roriw = 356,
    rv_op_orc_b = 357,
    rv_op_bset = 358,
    rv_op_bclr = 359,
    rv_op_binv = 360,
    rv_op_bext = 361,
} rv_op;

/* structures */

typedef struct {
    uint64_t  pc;
    uint64_t  inst;
    int32_t   imm;
    uint16_t  op;
    uint8_t   codec;
    uint8_t   rd;
    uint8_t   rs1;
    uint8_t   rs2;
    uint8_t   rs3;
    uint8_t   rm;
    uint8_t   pred;
    uint8_t   succ;
    uint8_t   aq;
    uint8_t   rl;
} rv_decode;

typedef struct {
    const int op;
    const rvc_constraint *constraints;
} rv_comp_data;

enum {
    rvcd_imm_nz = 0x1
};

typedef struct {
    const char * const name;
    const rv_codec codec;
    const char * const format;
    const rv_comp_data *pseudo;
    const short decomp_rv32;
    const short decomp_rv64;
    const short decomp_rv128;
    const short decomp_data;
} rv_opcode_data;

/* register names */

static const char rv_ireg_name_sym[32][5] = {
    "zero", "ra",   "sp",   "gp",   "tp",   "t0",   "t1",   "t2",
    "s0",   "s1",   "a0",   "a1",   "a2",   "a3",   "a4",   "a5",
    "a6",   "a7",   "s2",   "s3",   "s4",   "s5",   "s6",   "s7",
    "s8",   "s9",   "s10",  "s11",  "t3",   "t4",   "t5",   "t6",
};

static const char rv_freg_name_sym[32][5] = {
    "ft0",  "ft1",  "ft2",  "ft3",  "ft4",  "ft5",  "ft6",  "ft7",
    "fs0",  "fs1",  "fa0",  "fa1",  "fa2",  "fa3",  "fa4",  "fa5",
    "fa6",  "fa7",  "fs2",  "fs3",  "fs4",  "fs5",  "fs6",  "fs7",
    "fs8",  "fs9",  "fs10", "fs11", "ft8",  "ft9",  "ft10", "ft11",
};

/* instruction formats */

#define rv_fmt_none                   "O\t"
#define rv_fmt_rs1                    "O\t1"
#define rv_fmt_offset                 "O\to"
#define rv_fmt_pred_succ              "O\tp,s"
#define rv_fmt_rs1_rs2                "O\t1,2"
#define rv_fmt_rd_imm                 "O\t0,i"
#define rv_fmt_rd_offset              "O\t0,o"
#define rv_fmt_rd_rs1_rs2             "O\t0,1,2"
#define rv_fmt_frd_rs1                "O\t3,1"
#define rv_fmt_rd_frs1                "O\t0,4"
#define rv_fmt_rd_frs1_frs2           "O\t0,4,5"
#define rv_fmt_frd_frs1_frs2          "O\t3,4,5"
#define rv_fmt_rm_frd_frs1            "O\tr,3,4"
#define rv_fmt_rm_frd_rs1             "O\tr,3,1"
#define rv_fmt_rm_rd_frs1             "O\tr,0,4"
#define rv_fmt_rm_frd_frs1_frs2       "O\tr,3,4,5"
#define rv_fmt_rm_frd_frs1_frs2_frs3  "O\tr,3,4,5,6"
#define rv_fmt_rd_rs1_imm             "O\t0,1,i"
#define rv_fmt_rd_rs1_offset          "O\t0,1,i"
#define rv_fmt_rd_offset_rs1          "O\t0,i(1)"
#define rv_fmt_frd_offset_rs1         "O\t3,i(1)"
#define rv_fmt_rd_csr_rs1             "O\t0,c,1"
#define rv_fmt_rd_csr_zimm            "O\t0,c,7"
#define rv_fmt_rs2_offset_rs1         "O\t2,i(1)"
#define rv_fmt_frs2_offset_rs1        "O\t5,i(1)"
#define rv_fmt_rs1_rs2_offset         "O\t1,2,o"
#define rv_fmt_rs2_rs1_offset         "O\t2,1,o"
#define rv_fmt_aqrl_rd_rs2_rs1        "OAR\t0,2,(1)"
#define rv_fmt_aqrl_rd_rs1            "OAR\t0,(1)"
#define rv_fmt_rd                     "O\t0"
#define rv_fmt_rd_zimm                "O\t0,7"
#define rv_fmt_rd_rs1                 "O\t0,1"
#define rv_fmt_rd_rs2                 "O\t0,2"
#define rv_fmt_rs1_offset             "O\t1,o"
#define rv_fmt_rs2_offset             "O\t2,o"

/* pseudo-instruction constraints */

static const rvc_constraint rvcc_jal[] = { rvc_rd_eq_ra, rvc_end };
static const rvc_constraint rvcc_jalr[] = { rvc_rd_eq_ra, rvc_imm_eq_zero, rvc_end };
static const rvc_constraint rvcc_nop[] = { rvc_rd_eq_x0, rvc_rs1_eq_x0, rvc_imm_eq_zero, rvc_end };
static const rvc_constraint rvcc_mv[] = { rvc_imm_eq_zero, rvc_end };
static const rvc_constraint rvcc_not[] = { rvc_imm_eq_n1, rvc_end };
static const rvc_constraint rvcc_neg[] = { rvc_rs1_eq_x0, rvc_end };
static const rvc_constraint rvcc_negw[] = { rvc_rs1_eq_x0, rvc_end };
static const rvc_constraint rvcc_sext_w[] = { rvc_imm_eq_zero, rvc_end };
static const rvc_constraint rvcc_seqz[] = { rvc_imm_eq_p1, rvc_end };
static const rvc_constraint rvcc_snez[] = { rvc_rs1_eq_x0, rvc_end };
static const rvc_constraint rvcc_sltz[] = { rvc_rs2_eq_x0, rvc_end };
static const rvc_constraint rvcc_sgtz[] = { rvc_rs1_eq_x0, rvc_end };
static const rvc_constraint rvcc_fmv_s[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fabs_s[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fneg_s[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fmv_d[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fabs_d[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fneg_d[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fmv_q[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fabs_q[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_fneg_q[] = { rvc_rs2_eq_rs1, rvc_end };
static const rvc_constraint rvcc_beqz[] = { rvc_rs2_eq_x0, rvc_end };
static const rvc_constraint rvcc_bnez[] = { rvc_rs2_eq_x0, rvc_end };
static const rvc_constraint rvcc_blez[] = { rvc_rs1_eq_x0, rvc_end };
static const rvc_constraint rvcc_bgez[] = { rvc_rs2_eq_x0, rvc_end };
static const rvc_constraint rvcc_bltz[] = { rvc_rs2_eq_x0, rvc_end };
static const rvc_constraint rvcc_bgtz[] = { rvc_rs1_eq_x0, rvc_end };
static const rvc_constraint rvcc_ble[] = { rvc_end };
static const rvc_constraint rvcc_bleu[] = { rvc_end };
static const rvc_constraint rvcc_bgt[] = { rvc_end };
static const rvc_constraint rvcc_bgtu[] = { rvc_end };
static const rvc_constraint rvcc_j[] = { rvc_rd_eq_x0, rvc_end };
static const rvc_constraint rvcc_ret[] = { rvc_rd_eq_x0, rvc_rs1_eq_ra, rvc_end };
static const rvc_constraint rvcc_jr[] = { rvc_rd_eq_x0, rvc_imm_eq_zero, rvc_end };
static const rvc_constraint rvcc_rdcycle[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc00, rvc_end };
static const rvc_constraint rvcc_rdtime[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc01, rvc_end };
static const rvc_constraint rvcc_rdinstret[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc02, rvc_end };
static const rvc_constraint rvcc_rdcycleh[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc80, rvc_end };
static const rvc_constraint rvcc_rdtimeh[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc81, rvc_end };
static const rvc_constraint rvcc_rdinstreth[] = { rvc_rs1_eq_x0,
                                                  rvc_csr_eq_0xc82, rvc_end };
static const rvc_constraint rvcc_frcsr[] = { rvc_rs1_eq_x0, rvc_csr_eq_0x003, rvc_end };
static const rvc_constraint rvcc_frrm[] = { rvc_rs1_eq_x0, rvc_csr_eq_0x002, rvc_end };
static const rvc_constraint rvcc_frflags[] = { rvc_rs1_eq_x0, rvc_csr_eq_0x001, rvc_end };
static const rvc_constraint rvcc_fscsr[] = { rvc_csr_eq_0x003, rvc_end };
static const rvc_constraint rvcc_fsrm[] = { rvc_csr_eq_0x002, rvc_end };
static const rvc_constraint rvcc_fsflags[] = { rvc_csr_eq_0x001, rvc_end };
static const rvc_constraint rvcc_fsrmi[] = { rvc_csr_eq_0x002, rvc_end };
static const rvc_constraint rvcc_fsflagsi[] = { rvc_csr_eq_0x001, rvc_end };

/* pseudo-instruction metadata */

static const rv_comp_data rvcp_jal[] = {
    { rv_op_j, rvcc_j },
    { rv_op_jal, rvcc_jal },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_jalr[] = {
    { rv_op_ret, rvcc_ret },
    { rv_op_jr, rvcc_jr },
    { rv_op_jalr, rvcc_jalr },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_beq[] = {
    { rv_op_beqz, rvcc_beqz },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_bne[] = {
    { rv_op_bnez, rvcc_bnez },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_blt[] = {
    { rv_op_bltz, rvcc_bltz },
    { rv_op_bgtz, rvcc_bgtz },
    { rv_op_bgt, rvcc_bgt },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_bge[] = {
    { rv_op_blez, rvcc_blez },
    { rv_op_bgez, rvcc_bgez },
    { rv_op_ble, rvcc_ble },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_bltu[] = {
    { rv_op_bgtu, rvcc_bgtu },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_bgeu[] = {
    { rv_op_bleu, rvcc_bleu },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_addi[] = {
    { rv_op_nop, rvcc_nop },
    { rv_op_mv, rvcc_mv },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_sltiu[] = {
    { rv_op_seqz, rvcc_seqz },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_xori[] = {
    { rv_op_not, rvcc_not },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_sub[] = {
    { rv_op_neg, rvcc_neg },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_slt[] = {
    { rv_op_sltz, rvcc_sltz },
    { rv_op_sgtz, rvcc_sgtz },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_sltu[] = {
    { rv_op_snez, rvcc_snez },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_addiw[] = {
    { rv_op_sext_w, rvcc_sext_w },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_subw[] = {
    { rv_op_negw, rvcc_negw },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_csrrw[] = {
    { rv_op_fscsr, rvcc_fscsr },
    { rv_op_fsrm, rvcc_fsrm },
    { rv_op_fsflags, rvcc_fsflags },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_csrrs[] = {
    { rv_op_rdcycle, rvcc_rdcycle },
    { rv_op_rdtime, rvcc_rdtime },
    { rv_op_rdinstret, rvcc_rdinstret },
    { rv_op_rdcycleh, rvcc_rdcycleh },
    { rv_op_rdtimeh, rvcc_rdtimeh },
    { rv_op_rdinstreth, rvcc_rdinstreth },
    { rv_op_frcsr, rvcc_frcsr },
    { rv_op_frrm, rvcc_frrm },
    { rv_op_frflags, rvcc_frflags },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_csrrwi[] = {
    { rv_op_fsrmi, rvcc_fsrmi },
    { rv_op_fsflagsi, rvcc_fsflagsi },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnj_s[] = {
    { rv_op_fmv_s, rvcc_fmv_s },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnjn_s[] = {
    { rv_op_fneg_s, rvcc_fneg_s },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnjx_s[] = {
    { rv_op_fabs_s, rvcc_fabs_s },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnj_d[] = {
    { rv_op_fmv_d, rvcc_fmv_d },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnjn_d[] = {
    { rv_op_fneg_d, rvcc_fneg_d },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnjx_d[] = {
    { rv_op_fabs_d, rvcc_fabs_d },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnj_q[] = {
    { rv_op_fmv_q, rvcc_fmv_q },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnjn_q[] = {
    { rv_op_fneg_q, rvcc_fneg_q },
    { rv_op_illegal, NULL }
};

static const rv_comp_data rvcp_fsgnjx_q[] = {
    { rv_op_fabs_q, rvcc_fabs_q },
    { rv_op_illegal, NULL }
};

/* instruction metadata */

const rv_opcode_data opcode_data[] = {
    { "illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    { "lui", rv_codec_u, rv_fmt_rd_imm, NULL, 0, 0, 0 },
    { "auipc", rv_codec_u, rv_fmt_rd_offset, NULL, 0, 0, 0 },
    { "jal", rv_codec_uj, rv_fmt_rd_offset, rvcp_jal, 0, 0, 0 },
    { "jalr", rv_codec_i, rv_fmt_rd_rs1_offset, rvcp_jalr, 0, 0, 0 },
    { "beq", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_beq, 0, 0, 0 },
    { "bne", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_bne, 0, 0, 0 },
    { "blt", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_blt, 0, 0, 0 },
    { "bge", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_bge, 0, 0, 0 },
    { "bltu", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_bltu, 0, 0, 0 },
    { "bgeu", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_bgeu, 0, 0, 0 },
    { "lb", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "lh", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "lw", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "lbu", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "lhu", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "sb", rv_codec_s, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "sh", rv_codec_s, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "sw", rv_codec_s, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "addi", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_addi, 0, 0, 0 },
    { "slti", rv_codec_i, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sltiu", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_sltiu, 0, 0, 0 },
    { "xori", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_xori, 0, 0, 0 },
    { "ori", rv_codec_i, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "andi", rv_codec_i, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "slli", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srli", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srai", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "add", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sub", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_sub, 0, 0, 0 },
    { "sll", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "slt", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_slt, 0, 0, 0 },
    { "sltu", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_sltu, 0, 0, 0 },
    { "xor", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srl", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sra", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "or", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "and", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "fence", rv_codec_r_f, rv_fmt_pred_succ, NULL, 0, 0, 0 },
    { "fence.i", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "lwu", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "ld", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "sd", rv_codec_s, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "addiw", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_addiw, 0, 0, 0 },
    { "slliw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srliw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sraiw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "addw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "subw", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_subw, 0, 0, 0 },
    { "sllw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srlw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sraw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ldu", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "lq", rv_codec_i, rv_fmt_rd_offset_rs1, NULL, 0, 0, 0 },
    { "sq", rv_codec_s, rv_fmt_rs2_offset_rs1, NULL, 0, 0, 0 },
    { "addid", rv_codec_i, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sllid", rv_codec_i_sh6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "srlid", rv_codec_i_sh6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "sraid", rv_codec_i_sh6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "addd", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "subd", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "slld", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srld", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "srad", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mul", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mulh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mulhsu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mulhu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "div", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "divu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rem", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "remu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "mulw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "divw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "divuw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "remw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "remuw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "muld", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "divd", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "divud", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "remd", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "remud", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "lr.w", rv_codec_r_l, rv_fmt_aqrl_rd_rs1, NULL, 0, 0, 0 },
    { "sc.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoswap.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoadd.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoxor.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoor.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoand.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomin.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomax.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amominu.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomaxu.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "lr.d", rv_codec_r_l, rv_fmt_aqrl_rd_rs1, NULL, 0, 0, 0 },
    { "sc.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoswap.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoadd.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoxor.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoor.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoand.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomin.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomax.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amominu.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomaxu.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "lr.q", rv_codec_r_l, rv_fmt_aqrl_rd_rs1, NULL, 0, 0, 0 },
    { "sc.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoswap.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoadd.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoxor.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoor.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amoand.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomin.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomax.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amominu.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "amomaxu.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1, NULL, 0, 0, 0 },
    { "ecall", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "ebreak", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "uret", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "sret", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "hret", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "mret", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "dret", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "sfence.vm", rv_codec_r, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "sfence.vma", rv_codec_r, rv_fmt_rs1_rs2, NULL, 0, 0, 0 },
    { "wfi", rv_codec_none, rv_fmt_none, NULL, 0, 0, 0 },
    { "csrrw", rv_codec_i_csr, rv_fmt_rd_csr_rs1, rvcp_csrrw, 0, 0, 0 },
    { "csrrs", rv_codec_i_csr, rv_fmt_rd_csr_rs1, rvcp_csrrs, 0, 0, 0 },
    { "csrrc", rv_codec_i_csr, rv_fmt_rd_csr_rs1, NULL, 0, 0, 0 },
    { "csrrwi", rv_codec_i_csr, rv_fmt_rd_csr_zimm, rvcp_csrrwi, 0, 0, 0 },
    { "csrrsi", rv_codec_i_csr, rv_fmt_rd_csr_zimm, NULL, 0, 0, 0 },
    { "csrrci", rv_codec_i_csr, rv_fmt_rd_csr_zimm, NULL, 0, 0, 0 },
    { "flw", rv_codec_i, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "fsw", rv_codec_s, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "fmadd.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fmsub.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fnmsub.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fnmadd.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fadd.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsub.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmul.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fdiv.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsgnj.s", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnj_s, 0, 0, 0 },
    { "fsgnjn.s", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjn_s, 0, 0, 0 },
    { "fsgnjx.s", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjx_s, 0, 0, 0 },
    { "fmin.s", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmax.s", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsqrt.s", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fle.s", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "flt.s", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "feq.s", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fcvt.w.s", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.wu.s", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.s.w", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.s.wu", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fmv.x.s", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fclass.s", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fmv.s.x", rv_codec_r, rv_fmt_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.l.s", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.lu.s", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.s.l", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.s.lu", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fld", rv_codec_i, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "fsd", rv_codec_s, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "fmadd.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fmsub.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fnmsub.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fnmadd.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fadd.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsub.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmul.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fdiv.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsgnj.d", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnj_d, 0, 0, 0 },
    { "fsgnjn.d", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjn_d, 0, 0, 0 },
    { "fsgnjx.d", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjx_d, 0, 0, 0 },
    { "fmin.d", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmax.d", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fcvt.s.d", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fcvt.d.s", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fsqrt.d", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fle.d", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "flt.d", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "feq.d", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fcvt.w.d", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.wu.d", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.d.w", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.d.wu", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fclass.d", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.l.d", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.lu.d", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fmv.x.d", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.d.l", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.d.lu", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fmv.d.x", rv_codec_r, rv_fmt_frd_rs1, NULL, 0, 0, 0 },
    { "flq", rv_codec_i, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "fsq", rv_codec_s, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "fmadd.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fmsub.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fnmsub.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fnmadd.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3, NULL, 0, 0, 0 },
    { "fadd.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsub.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmul.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fdiv.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fsgnj.q", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnj_q, 0, 0, 0 },
    { "fsgnjn.q", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjn_q, 0, 0, 0 },
    { "fsgnjx.q", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjx_q, 0, 0, 0 },
    { "fmin.q", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmax.q", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fcvt.s.q", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fcvt.q.s", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fcvt.d.q", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fcvt.q.d", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fsqrt.q", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fle.q", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "flt.q", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "feq.q", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fcvt.w.q", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.wu.q", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.q.w", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.q.wu", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fclass.q", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.l.q", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.lu.q", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fcvt.q.l", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fcvt.q.lu", rv_codec_r_m, rv_fmt_rm_frd_rs1, NULL, 0, 0, 0 },
    { "fmv.x.q", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fmv.q.x", rv_codec_r, rv_fmt_frd_rs1, NULL, 0, 0, 0 },
    { "c.addi4spn", rv_codec_ciw_4spn, rv_fmt_rd_rs1_imm, NULL, rv_op_addi,
      rv_op_addi, rv_op_addi, rvcd_imm_nz },
    { "c.fld", rv_codec_cl_ld, rv_fmt_frd_offset_rs1, NULL, rv_op_fld, rv_op_fld, 0 },
    { "c.lw", rv_codec_cl_lw, rv_fmt_rd_offset_rs1, NULL, rv_op_lw, rv_op_lw, rv_op_lw },
    { "c.flw", rv_codec_cl_lw, rv_fmt_frd_offset_rs1, NULL, rv_op_flw, 0, 0 },
    { "c.fsd", rv_codec_cs_sd, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsd, rv_op_fsd, 0 },
    { "c.sw", rv_codec_cs_sw, rv_fmt_rs2_offset_rs1, NULL, rv_op_sw, rv_op_sw, rv_op_sw },
    { "c.fsw", rv_codec_cs_sw, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsw, 0, 0 },
    { "c.nop", rv_codec_ci_none, rv_fmt_none, NULL, rv_op_addi, rv_op_addi, rv_op_addi },
    { "c.addi", rv_codec_ci, rv_fmt_rd_rs1_imm, NULL, rv_op_addi, rv_op_addi,
      rv_op_addi, rvcd_imm_nz },
    { "c.jal", rv_codec_cj_jal, rv_fmt_rd_offset, NULL, rv_op_jal, 0, 0 },
    { "c.li", rv_codec_ci_li, rv_fmt_rd_rs1_imm, NULL, rv_op_addi, rv_op_addi, rv_op_addi },
    { "c.addi16sp", rv_codec_ci_16sp, rv_fmt_rd_rs1_imm, NULL, rv_op_addi,
      rv_op_addi, rv_op_addi, rvcd_imm_nz },
    { "c.lui", rv_codec_ci_lui, rv_fmt_rd_imm, NULL, rv_op_lui, rv_op_lui,
      rv_op_lui, rvcd_imm_nz },
    { "c.srli", rv_codec_cb_sh6, rv_fmt_rd_rs1_imm, NULL, rv_op_srli,
      rv_op_srli, rv_op_srli, rvcd_imm_nz },
    { "c.srai", rv_codec_cb_sh6, rv_fmt_rd_rs1_imm, NULL, rv_op_srai,
      rv_op_srai, rv_op_srai, rvcd_imm_nz },
    { "c.andi", rv_codec_cb_imm, rv_fmt_rd_rs1_imm, NULL, rv_op_andi,
      rv_op_andi, rv_op_andi },
    { "c.sub", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_sub, rv_op_sub, rv_op_sub },
    { "c.xor", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_xor, rv_op_xor, rv_op_xor },
    { "c.or", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_or, rv_op_or, rv_op_or },
    { "c.and", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_and, rv_op_and, rv_op_and },
    { "c.subw", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_subw, rv_op_subw, rv_op_subw },
    { "c.addw", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_addw, rv_op_addw, rv_op_addw },
    { "c.j", rv_codec_cj, rv_fmt_rd_offset, NULL, rv_op_jal, rv_op_jal, rv_op_jal },
    { "c.beqz", rv_codec_cb, rv_fmt_rs1_rs2_offset, NULL, rv_op_beq, rv_op_beq, rv_op_beq },
    { "c.bnez", rv_codec_cb, rv_fmt_rs1_rs2_offset, NULL, rv_op_bne, rv_op_bne, rv_op_bne },
    { "c.slli", rv_codec_ci_sh6, rv_fmt_rd_rs1_imm, NULL, rv_op_slli,
      rv_op_slli, rv_op_slli, rvcd_imm_nz },
    { "c.fldsp", rv_codec_ci_ldsp, rv_fmt_frd_offset_rs1, NULL, rv_op_fld, rv_op_fld, rv_op_fld },
    { "c.lwsp", rv_codec_ci_lwsp, rv_fmt_rd_offset_rs1, NULL, rv_op_lw, rv_op_lw, rv_op_lw },
    { "c.flwsp", rv_codec_ci_lwsp, rv_fmt_frd_offset_rs1, NULL, rv_op_flw, 0, 0 },
    { "c.jr", rv_codec_cr_jr, rv_fmt_rd_rs1_offset, NULL, rv_op_jalr, rv_op_jalr, rv_op_jalr },
    { "c.mv", rv_codec_cr_mv, rv_fmt_rd_rs1_rs2, NULL, rv_op_addi, rv_op_addi, rv_op_addi },
    { "c.ebreak", rv_codec_ci_none, rv_fmt_none, NULL, rv_op_ebreak, rv_op_ebreak, rv_op_ebreak },
    { "c.jalr", rv_codec_cr_jalr, rv_fmt_rd_rs1_offset, NULL, rv_op_jalr, rv_op_jalr, rv_op_jalr },
    { "c.add", rv_codec_cr, rv_fmt_rd_rs1_rs2, NULL, rv_op_add, rv_op_add, rv_op_add },
    { "c.fsdsp", rv_codec_css_sdsp, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsd, rv_op_fsd, rv_op_fsd },
    { "c.swsp", rv_codec_css_swsp, rv_fmt_rs2_offset_rs1, NULL, rv_op_sw, rv_op_sw, rv_op_sw },
    { "c.fswsp", rv_codec_css_swsp, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsw, 0, 0 },
    { "c.ld", rv_codec_cl_ld, rv_fmt_rd_offset_rs1, NULL, 0, rv_op_ld, rv_op_ld },
    { "c.sd", rv_codec_cs_sd, rv_fmt_rs2_offset_rs1, NULL, 0, rv_op_sd, rv_op_sd },
    { "c.addiw", rv_codec_ci, rv_fmt_rd_rs1_imm, NULL, 0, rv_op_addiw, rv_op_addiw },
    { "c.ldsp", rv_codec_ci_ldsp, rv_fmt_rd_offset_rs1, NULL, 0, rv_op_ld, rv_op_ld },
    { "c.sdsp", rv_codec_css_sdsp, rv_fmt_rs2_offset_rs1, NULL, 0, rv_op_sd, rv_op_sd },
    { "c.lq", rv_codec_cl_lq, rv_fmt_rd_offset_rs1, NULL, 0, 0, rv_op_lq },
    { "c.sq", rv_codec_cs_sq, rv_fmt_rs2_offset_rs1, NULL, 0, 0, rv_op_sq },
    { "c.lqsp", rv_codec_ci_lqsp, rv_fmt_rd_offset_rs1, NULL, 0, 0, rv_op_lq },
    { "c.sqsp", rv_codec_css_sqsp, rv_fmt_rs2_offset_rs1, NULL, 0, 0, rv_op_sq },
    { "nop", rv_codec_i, rv_fmt_none, NULL, 0, 0, 0 },
    { "mv", rv_codec_i, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "not", rv_codec_i, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "neg", rv_codec_r, rv_fmt_rd_rs2, NULL, 0, 0, 0 },
    { "negw", rv_codec_r, rv_fmt_rd_rs2, NULL, 0, 0, 0 },
    { "sext.w", rv_codec_i, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "seqz", rv_codec_i, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "snez", rv_codec_r, rv_fmt_rd_rs2, NULL, 0, 0, 0 },
    { "sltz", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sgtz", rv_codec_r, rv_fmt_rd_rs2, NULL, 0, 0, 0 },
    { "fmv.s", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fabs.s", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fneg.s", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fmv.d", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fabs.d", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fneg.d", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fmv.q", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fabs.q", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fneg.q", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "beqz", rv_codec_sb, rv_fmt_rs1_offset, NULL, 0, 0, 0 },
    { "bnez", rv_codec_sb, rv_fmt_rs1_offset, NULL, 0, 0, 0 },
    { "blez", rv_codec_sb, rv_fmt_rs2_offset, NULL, 0, 0, 0 },
    { "bgez", rv_codec_sb, rv_fmt_rs1_offset, NULL, 0, 0, 0 },
    { "bltz", rv_codec_sb, rv_fmt_rs1_offset, NULL, 0, 0, 0 },
    { "bgtz", rv_codec_sb, rv_fmt_rs2_offset, NULL, 0, 0, 0 },
    { "ble", rv_codec_sb, rv_fmt_rs2_rs1_offset, NULL, 0, 0, 0 },
    { "bleu", rv_codec_sb, rv_fmt_rs2_rs1_offset, NULL, 0, 0, 0 },
    { "bgt", rv_codec_sb, rv_fmt_rs2_rs1_offset, NULL, 0, 0, 0 },
    { "bgtu", rv_codec_sb, rv_fmt_rs2_rs1_offset, NULL, 0, 0, 0 },
    { "j", rv_codec_uj, rv_fmt_offset, NULL, 0, 0, 0 },
    { "ret", rv_codec_i, rv_fmt_none, NULL, 0, 0, 0 },
    { "jr", rv_codec_i, rv_fmt_rs1, NULL, 0, 0, 0 },
    { "rdcycle", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "rdtime", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "rdinstret", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "rdcycleh", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "rdtimeh", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "rdinstreth", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "frcsr", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "frrm", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "frflags", rv_codec_i_csr, rv_fmt_rd, NULL, 0, 0, 0 },
    { "fscsr", rv_codec_i_csr, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fsrm", rv_codec_i_csr, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fsflags", rv_codec_i_csr, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "fsrmi", rv_codec_i_csr, rv_fmt_rd_zimm, NULL, 0, 0, 0 },
    { "fsflagsi", rv_codec_i_csr, rv_fmt_rd_zimm, NULL, 0, 0, 0 },
    { "bseti", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "bclri", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "binvi", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "bexti", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "rori", rv_codec_i_sh7, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "clz", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "ctz", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "cpop", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sext.h", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "sext.b", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xnor", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "orn", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "andn", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "rol", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "ror", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sh1add", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sh2add", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sh3add", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sh1add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sh2add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sh3add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "clmul", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "clmulr", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "clmulh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "min", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "minu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "max", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "maxu", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "clzw", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "clzw", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "cpopw", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "slli.uw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rolw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rorw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "rev8", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zext.h", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "roriw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
    { "orc.b", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "bset", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "bclr", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "binv", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "bext", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
};

/* CSR names */

static const char *csr_name(int csrno)
{
    switch (csrno) {
    case 0x0000: return "ustatus";
    case 0x0001: return "fflags";
    case 0x0002: return "frm";
    case 0x0003: return "fcsr";
    case 0x0004: return "uie";
    case 0x0005: return "utvec";
    case 0x0040: return "uscratch";
    case 0x0041: return "uepc";
    case 0x0042: return "ucause";
    case 0x0043: return "utval";
    case 0x0044: return "uip";
    case 0x0100: return "sstatus";
    case 0x0102: return "sedeleg";
    case 0x0103: return "sideleg";
    case 0x0104: return "sie";
    case 0x0105: return "stvec";
    case 0x0106: return "scounteren";
    case 0x0140: return "sscratch";
    case 0x0141: return "sepc";
    case 0x0142: return "scause";
    case 0x0143: return "stval";
    case 0x0144: return "sip";
    case 0x0180: return "satp";
    case 0x0200: return "hstatus";
    case 0x0202: return "hedeleg";
    case 0x0203: return "hideleg";
    case 0x0204: return "hie";
    case 0x0205: return "htvec";
    case 0x0240: return "hscratch";
    case 0x0241: return "hepc";
    case 0x0242: return "hcause";
    case 0x0243: return "hbadaddr";
    case 0x0244: return "hip";
    case 0x0300: return "mstatus";
    case 0x0301: return "misa";
    case 0x0302: return "medeleg";
    case 0x0303: return "mideleg";
    case 0x0304: return "mie";
    case 0x0305: return "mtvec";
    case 0x0306: return "mcounteren";
    case 0x0320: return "mucounteren";
    case 0x0321: return "mscounteren";
    case 0x0322: return "mhcounteren";
    case 0x0323: return "mhpmevent3";
    case 0x0324: return "mhpmevent4";
    case 0x0325: return "mhpmevent5";
    case 0x0326: return "mhpmevent6";
    case 0x0327: return "mhpmevent7";
    case 0x0328: return "mhpmevent8";
    case 0x0329: return "mhpmevent9";
    case 0x032a: return "mhpmevent10";
    case 0x032b: return "mhpmevent11";
    case 0x032c: return "mhpmevent12";
    case 0x032d: return "mhpmevent13";
    case 0x032e: return "mhpmevent14";
    case 0x032f: return "mhpmevent15";
    case 0x0330: return "mhpmevent16";
    case 0x0331: return "mhpmevent17";
    case 0x0332: return "mhpmevent18";
    case 0x0333: return "mhpmevent19";
    case 0x0334: return "mhpmevent20";
    case 0x0335: return "mhpmevent21";
    case 0x0336: return "mhpmevent22";
    case 0x0337: return "mhpmevent23";
    case 0x0338: return "mhpmevent24";
    case 0x0339: return "mhpmevent25";
    case 0x033a: return "mhpmevent26";
    case 0x033b: return "mhpmevent27";
    case 0x033c: return "mhpmevent28";
    case 0x033d: return "mhpmevent29";
    case 0x033e: return "mhpmevent30";
    case 0x033f: return "mhpmevent31";
    case 0x0340: return "mscratch";
    case 0x0341: return "mepc";
    case 0x0342: return "mcause";
    case 0x0343: return "mtval";
    case 0x0344: return "mip";
    case 0x0380: return "mbase";
    case 0x0381: return "mbound";
    case 0x0382: return "mibase";
    case 0x0383: return "mibound";
    case 0x0384: return "mdbase";
    case 0x0385: return "mdbound";
    case 0x03a0: return "pmpcfg3";
    case 0x03b0: return "pmpaddr0";
    case 0x03b1: return "pmpaddr1";
    case 0x03b2: return "pmpaddr2";
    case 0x03b3: return "pmpaddr3";
    case 0x03b4: return "pmpaddr4";
    case 0x03b5: return "pmpaddr5";
    case 0x03b6: return "pmpaddr6";
    case 0x03b7: return "pmpaddr7";
    case 0x03b8: return "pmpaddr8";
    case 0x03b9: return "pmpaddr9";
    case 0x03ba: return "pmpaddr10";
    case 0x03bb: return "pmpaddr11";
    case 0x03bc: return "pmpaddr12";
    case 0x03bd: return "pmpaddr14";
    case 0x03be: return "pmpaddr13";
    case 0x03bf: return "pmpaddr15";
    case 0x0780: return "mtohost";
    case 0x0781: return "mfromhost";
    case 0x0782: return "mreset";
    case 0x0783: return "mipi";
    case 0x0784: return "miobase";
    case 0x07a0: return "tselect";
    case 0x07a1: return "tdata1";
    case 0x07a2: return "tdata2";
    case 0x07a3: return "tdata3";
    case 0x07b0: return "dcsr";
    case 0x07b1: return "dpc";
    case 0x07b2: return "dscratch";
    case 0x0b00: return "mcycle";
    case 0x0b01: return "mtime";
    case 0x0b02: return "minstret";
    case 0x0b03: return "mhpmcounter3";
    case 0x0b04: return "mhpmcounter4";
    case 0x0b05: return "mhpmcounter5";
    case 0x0b06: return "mhpmcounter6";
    case 0x0b07: return "mhpmcounter7";
    case 0x0b08: return "mhpmcounter8";
    case 0x0b09: return "mhpmcounter9";
    case 0x0b0a: return "mhpmcounter10";
    case 0x0b0b: return "mhpmcounter11";
    case 0x0b0c: return "mhpmcounter12";
    case 0x0b0d: return "mhpmcounter13";
    case 0x0b0e: return "mhpmcounter14";
    case 0x0b0f: return "mhpmcounter15";
    case 0x0b10: return "mhpmcounter16";
    case 0x0b11: return "mhpmcounter17";
    case 0x0b12: return "mhpmcounter18";
    case 0x0b13: return "mhpmcounter19";
    case 0x0b14: return "mhpmcounter20";
    case 0x0b15: return "mhpmcounter21";
    case 0x0b16: return "mhpmcounter22";
    case 0x0b17: return "mhpmcounter23";
    case 0x0b18: return "mhpmcounter24";
    case 0x0b19: return "mhpmcounter25";
    case 0x0b1a: return "mhpmcounter26";
    case 0x0b1b: return "mhpmcounter27";
    case 0x0b1c: return "mhpmcounter28";
    case 0x0b1d: return "mhpmcounter29";
    case 0x0b1e: return "mhpmcounter30";
    case 0x0b1f: return "mhpmcounter31";
    case 0x0b80: return "mcycleh";
    case 0x0b81: return "mtimeh";
    case 0x0b82: return "minstreth";
    case 0x0b83: return "mhpmcounter3h";
    case 0x0b84: return "mhpmcounter4h";
    case 0x0b85: return "mhpmcounter5h";
    case 0x0b86: return "mhpmcounter6h";
    case 0x0b87: return "mhpmcounter7h";
    case 0x0b88: return "mhpmcounter8h";
    case 0x0b89: return "mhpmcounter9h";
    case 0x0b8a: return "mhpmcounter10h";
    case 0x0b8b: return "mhpmcounter11h";
    case 0x0b8c: return "mhpmcounter12h";
    case 0x0b8d: return "mhpmcounter13h";
    case 0x0b8e: return "mhpmcounter14h";
    case 0x0b8f: return "mhpmcounter15h";
    case 0x0b90: return "mhpmcounter16h";
    case 0x0b91: return "mhpmcounter17h";
    case 0x0b92: return "mhpmcounter18h";
    case 0x0b93: return "mhpmcounter19h";
    case 0x0b94: return "mhpmcounter20h";
    case 0x0b95: return "mhpmcounter21h";
    case 0x0b96: return "mhpmcounter22h";
    case 0x0b97: return "mhpmcounter23h";
    case 0x0b98: return "mhpmcounter24h";
    case 0x0b99: return "mhpmcounter25h";
    case 0x0b9a: return "mhpmcounter26h";
    case 0x0b9b: return "mhpmcounter27h";
    case 0x0b9c: return "mhpmcounter28h";
    case 0x0b9d: return "mhpmcounter29h";
    case 0x0b9e: return "mhpmcounter30h";
    case 0x0b9f: return "mhpmcounter31h";
    case 0x0c00: return "cycle";
    case 0x0c01: return "time";
    case 0x0c02: return "instret";
    case 0x0c80: return "cycleh";
    case 0x0c81: return "timeh";
    case 0x0c82: return "instreth";
    case 0x0d00: return "scycle";
    case 0x0d01: return "stime";
    case 0x0d02: return "sinstret";
    case 0x0d80: return "scycleh";
    case 0x0d81: return "stimeh";
    case 0x0d82: return "sinstreth";
    case 0x0e00: return "hcycle";
    case 0x0e01: return "htime";
    case 0x0e02: return "hinstret";
    case 0x0e80: return "hcycleh";
    case 0x0e81: return "htimeh";
    case 0x0e82: return "hinstreth";
    case 0x0f11: return "mvendorid";
    case 0x0f12: return "marchid";
    case 0x0f13: return "mimpid";
    case 0x0f14: return "mhartid";
    default: return NULL;
    }
}

/* decode opcode */

static void decode_inst_opcode(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;
    switch (((inst >> 0) & 0b11)) {
    case 0:
        switch (((inst >> 13) & 0b111)) {
        case 0: op = rv_op_c_addi4spn; break;
        case 1:
            if (isa == rv128) {
                op = rv_op_c_lq;
            } else {
                op = rv_op_c_fld;
            }
            break;
        case 2: op = rv_op_c_lw; break;
        case 3:
            if (isa == rv32) {
                op = rv_op_c_flw;
            } else {
                op = rv_op_c_ld;
            }
            break;
        case 5:
            if (isa == rv128) {
                op = rv_op_c_sq;
            } else {
                op = rv_op_c_fsd;
            }
            break;
        case 6: op = rv_op_c_sw; break;
        case 7:
            if (isa == rv32) {
                op = rv_op_c_fsw;
            } else {
                op = rv_op_c_sd;
            }
            break;
        }
        break;
    case 1:
        switch (((inst >> 13) & 0b111)) {
        case 0:
            switch (((inst >> 2) & 0b11111111111)) {
            case 0: op = rv_op_c_nop; break;
            default: op = rv_op_c_addi; break;
            }
            break;
        case 1:
            if (isa == rv32) {
                op = rv_op_c_jal;
            } else {
                op = rv_op_c_addiw;
            }
            break;
        case 2: op = rv_op_c_li; break;
        case 3:
            switch (((inst >> 7) & 0b11111)) {
            case 2: op = rv_op_c_addi16sp; break;
            default: op = rv_op_c_lui; break;
            }
            break;
        case 4:
            switch (((inst >> 10) & 0b11)) {
            case 0:
                op = rv_op_c_srli;
                break;
            case 1:
                op = rv_op_c_srai;
                break;
            case 2: op = rv_op_c_andi; break;
            case 3:
                switch (((inst >> 10) & 0b100) | ((inst >> 5) & 0b011)) {
                case 0: op = rv_op_c_sub; break;
                case 1: op = rv_op_c_xor; break;
                case 2: op = rv_op_c_or; break;
                case 3: op = rv_op_c_and; break;
                case 4: op = rv_op_c_subw; break;
                case 5: op = rv_op_c_addw; break;
                }
                break;
            }
            break;
        case 5: op = rv_op_c_j; break;
        case 6: op = rv_op_c_beqz; break;
        case 7: op = rv_op_c_bnez; break;
        }
        break;
    case 2:
        switch (((inst >> 13) & 0b111)) {
        case 0:
            op = rv_op_c_slli;
            break;
        case 1:
            if (isa == rv128) {
                op = rv_op_c_lqsp;
            } else {
                op = rv_op_c_fldsp;
            }
            break;
        case 2: op = rv_op_c_lwsp; break;
        case 3:
            if (isa == rv32) {
                op = rv_op_c_flwsp;
            } else {
                op = rv_op_c_ldsp;
            }
            break;
        case 4:
            switch (((inst >> 12) & 0b1)) {
            case 0:
                switch (((inst >> 2) & 0b11111)) {
                case 0: op = rv_op_c_jr; break;
                default: op = rv_op_c_mv; break;
                }
                break;
            case 1:
                switch (((inst >> 2) & 0b11111)) {
                case 0:
                    switch (((inst >> 7) & 0b11111)) {
                    case 0: op = rv_op_c_ebreak; break;
                    default: op = rv_op_c_jalr; break;
                    }
                    break;
                default: op = rv_op_c_add; break;
                }
                break;
            }
            break;
        case 5:
            if (isa == rv128) {
                op = rv_op_c_sqsp;
            } else {
                op = rv_op_c_fsdsp;
            }
            break;
        case 6: op = rv_op_c_swsp; break;
        case 7:
            if (isa == rv32) {
                op = rv_op_c_fswsp;
            } else {
                op = rv_op_c_sdsp;
            }
            break;
        }
        break;
    case 3:
        switch (((inst >> 2) & 0b11111)) {
        case 0:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_lb; break;
            case 1: op = rv_op_lh; break;
            case 2: op = rv_op_lw; break;
            case 3: op = rv_op_ld; break;
            case 4: op = rv_op_lbu; break;
            case 5: op = rv_op_lhu; break;
            case 6: op = rv_op_lwu; break;
            case 7: op = rv_op_ldu; break;
            }
            break;
        case 1:
            switch (((inst >> 12) & 0b111)) {
            case 2: op = rv_op_flw; break;
            case 3: op = rv_op_fld; break;
            case 4: op = rv_op_flq; break;
            }
            break;
        case 3:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_fence; break;
            case 1: op = rv_op_fence_i; break;
            case 2: op = rv_op_lq; break;
            }
            break;
        case 4:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_addi; break;
            case 1:
                switch (((inst >> 27) & 0b11111)) {
                case 0b00000: op = rv_op_slli; break;
                case 0b00101: op = rv_op_bseti; break;
                case 0b01001: op = rv_op_bclri; break;
                case 0b01101: op = rv_op_binvi; break;
                case 0b01100:
                    switch (((inst >> 20) & 0b1111111)) {
                    case 0b0000000: op = rv_op_clz; break;
                    case 0b0000001: op = rv_op_ctz; break;
                    case 0b0000010: op = rv_op_cpop; break;
                      /* 0b0000011 */
                    case 0b0000100: op = rv_op_sext_b; break;
                    case 0b0000101: op = rv_op_sext_h; break;
                    }
                    break;
                }
                break;
            case 2: op = rv_op_slti; break;
            case 3: op = rv_op_sltiu; break;
            case 4: op = rv_op_xori; break;
            case 5:
                switch (((inst >> 27) & 0b11111)) {
                case 0b00000: op = rv_op_srli; break;
                case 0b00101: op = rv_op_orc_b; break;
                case 0b01000: op = rv_op_srai; break;
                case 0b01001: op = rv_op_bexti; break;
                case 0b01100: op = rv_op_rori; break;
                case 0b01101:
                    switch ((inst >> 20) & 0b1111111) {
                    case 0b0111000: op = rv_op_rev8; break;
                    }
                    break;
                }
                break;
            case 6: op = rv_op_ori; break;
            case 7: op = rv_op_andi; break;
            }
            break;
        case 5: op = rv_op_auipc; break;
        case 6:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_addiw; break;
            case 1:
                switch (((inst >> 25) & 0b1111111)) {
                case 0: op = rv_op_slliw; break;
                case 4: op = rv_op_slli_uw; break;
                case 48:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b00000: op = rv_op_clzw; break;
                    case 0b00001: op = rv_op_ctzw; break;
                    case 0b00010: op = rv_op_cpopw; break;
                    }
                    break;
                }
                break;
            case 5:
                switch (((inst >> 25) & 0b1111111)) {
                case 0: op = rv_op_srliw; break;
                case 32: op = rv_op_sraiw; break;
                case 48: op = rv_op_roriw; break;
                }
                break;
            }
            break;
        case 8:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_sb; break;
            case 1: op = rv_op_sh; break;
            case 2: op = rv_op_sw; break;
            case 3: op = rv_op_sd; break;
            case 4: op = rv_op_sq; break;
            }
            break;
        case 9:
            switch (((inst >> 12) & 0b111)) {
            case 2: op = rv_op_fsw; break;
            case 3: op = rv_op_fsd; break;
            case 4: op = rv_op_fsq; break;
            }
            break;
        case 11:
            switch (((inst >> 24) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
            case 2: op = rv_op_amoadd_w; break;
            case 3: op = rv_op_amoadd_d; break;
            case 4: op = rv_op_amoadd_q; break;
            case 10: op = rv_op_amoswap_w; break;
            case 11: op = rv_op_amoswap_d; break;
            case 12: op = rv_op_amoswap_q; break;
            case 18:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_lr_w; break;
                }
                break;
            case 19:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_lr_d; break;
                }
                break;
            case 20:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_lr_q; break;
                }
                break;
            case 26: op = rv_op_sc_w; break;
            case 27: op = rv_op_sc_d; break;
            case 28: op = rv_op_sc_q; break;
            case 34: op = rv_op_amoxor_w; break;
            case 35: op = rv_op_amoxor_d; break;
            case 36: op = rv_op_amoxor_q; break;
            case 66: op = rv_op_amoor_w; break;
            case 67: op = rv_op_amoor_d; break;
            case 68: op = rv_op_amoor_q; break;
            case 98: op = rv_op_amoand_w; break;
            case 99: op = rv_op_amoand_d; break;
            case 100: op = rv_op_amoand_q; break;
            case 130: op = rv_op_amomin_w; break;
            case 131: op = rv_op_amomin_d; break;
            case 132: op = rv_op_amomin_q; break;
            case 162: op = rv_op_amomax_w; break;
            case 163: op = rv_op_amomax_d; break;
            case 164: op = rv_op_amomax_q; break;
            case 194: op = rv_op_amominu_w; break;
            case 195: op = rv_op_amominu_d; break;
            case 196: op = rv_op_amominu_q; break;
            case 226: op = rv_op_amomaxu_w; break;
            case 227: op = rv_op_amomaxu_d; break;
            case 228: op = rv_op_amomaxu_q; break;
            }
            break;
        case 12:
            switch (((inst >> 22) & 0b1111111000) | ((inst >> 12) & 0b0000000111)) {
            case 0: op = rv_op_add; break;
            case 1: op = rv_op_sll; break;
            case 2: op = rv_op_slt; break;
            case 3: op = rv_op_sltu; break;
            case 4: op = rv_op_xor; break;
            case 5: op = rv_op_srl; break;
            case 6: op = rv_op_or; break;
            case 7: op = rv_op_and; break;
            case 8: op = rv_op_mul; break;
            case 9: op = rv_op_mulh; break;
            case 10: op = rv_op_mulhsu; break;
            case 11: op = rv_op_mulhu; break;
            case 12: op = rv_op_div; break;
            case 13: op = rv_op_divu; break;
            case 14: op = rv_op_rem; break;
            case 15: op = rv_op_remu; break;
            case 36:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_zext_h; break;
                }
                break;
            case 41: op = rv_op_clmul; break;
            case 42: op = rv_op_clmulr; break;
            case 43: op = rv_op_clmulh; break;
            case 44: op = rv_op_min; break;
            case 45: op = rv_op_minu; break;
            case 46: op = rv_op_max; break;
            case 47: op = rv_op_maxu; break;
            case 130: op = rv_op_sh1add; break;
            case 132: op = rv_op_sh2add; break;
            case 134: op = rv_op_sh3add; break;
            case 161: op = rv_op_bset; break;
            case 256: op = rv_op_sub; break;
            case 260: op = rv_op_xnor; break;
            case 261: op = rv_op_sra; break;
            case 262: op = rv_op_orn; break;
            case 263: op = rv_op_andn; break;
            case 289: op = rv_op_bclr; break;
            case 293: op = rv_op_bext; break;
            case 385: op = rv_op_rol; break;
            case 386: op = rv_op_ror; break;
            case 417: op = rv_op_binv; break;
            }
            break;
        case 13: op = rv_op_lui; break;
        case 14:
            switch (((inst >> 22) & 0b1111111000) | ((inst >> 12) & 0b0000000111)) {
            case 0: op = rv_op_addw; break;
            case 1: op = rv_op_sllw; break;
            case 5: op = rv_op_srlw; break;
            case 8: op = rv_op_mulw; break;
            case 12: op = rv_op_divw; break;
            case 13: op = rv_op_divuw; break;
            case 14: op = rv_op_remw; break;
            case 15: op = rv_op_remuw; break;
            case 32: op = rv_op_add_uw; break;
            case 36:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_zext_h; break;
                }
                break;
            case 130: op = rv_op_sh1add_uw; break;
            case 132: op = rv_op_sh2add_uw; break;
            case 134: op = rv_op_sh3add_uw; break;
            case 256: op = rv_op_subw; break;
            case 261: op = rv_op_sraw; break;
            case 385: op = rv_op_rolw; break;
            case 389: op = rv_op_rorw; break;
            }
            break;
        case 16:
            switch (((inst >> 25) & 0b11)) {
            case 0: op = rv_op_fmadd_s; break;
            case 1: op = rv_op_fmadd_d; break;
            case 3: op = rv_op_fmadd_q; break;
            }
            break;
        case 17:
            switch (((inst >> 25) & 0b11)) {
            case 0: op = rv_op_fmsub_s; break;
            case 1: op = rv_op_fmsub_d; break;
            case 3: op = rv_op_fmsub_q; break;
            }
            break;
        case 18:
            switch (((inst >> 25) & 0b11)) {
            case 0: op = rv_op_fnmsub_s; break;
            case 1: op = rv_op_fnmsub_d; break;
            case 3: op = rv_op_fnmsub_q; break;
            }
            break;
        case 19:
            switch (((inst >> 25) & 0b11)) {
            case 0: op = rv_op_fnmadd_s; break;
            case 1: op = rv_op_fnmadd_d; break;
            case 3: op = rv_op_fnmadd_q; break;
            }
            break;
        case 20:
            switch (((inst >> 25) & 0b1111111)) {
            case 0: op = rv_op_fadd_s; break;
            case 1: op = rv_op_fadd_d; break;
            case 3: op = rv_op_fadd_q; break;
            case 4: op = rv_op_fsub_s; break;
            case 5: op = rv_op_fsub_d; break;
            case 7: op = rv_op_fsub_q; break;
            case 8: op = rv_op_fmul_s; break;
            case 9: op = rv_op_fmul_d; break;
            case 11: op = rv_op_fmul_q; break;
            case 12: op = rv_op_fdiv_s; break;
            case 13: op = rv_op_fdiv_d; break;
            case 15: op = rv_op_fdiv_q; break;
            case 16:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fsgnj_s; break;
                case 1: op = rv_op_fsgnjn_s; break;
                case 2: op = rv_op_fsgnjx_s; break;
                }
                break;
            case 17:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fsgnj_d; break;
                case 1: op = rv_op_fsgnjn_d; break;
                case 2: op = rv_op_fsgnjx_d; break;
                }
                break;
            case 19:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fsgnj_q; break;
                case 1: op = rv_op_fsgnjn_q; break;
                case 2: op = rv_op_fsgnjx_q; break;
                }
                break;
            case 20:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fmin_s; break;
                case 1: op = rv_op_fmax_s; break;
                }
                break;
            case 21:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fmin_d; break;
                case 1: op = rv_op_fmax_d; break;
                }
                break;
            case 23:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fmin_q; break;
                case 1: op = rv_op_fmax_q; break;
                }
                break;
            case 32:
                switch (((inst >> 20) & 0b11111)) {
                case 1: op = rv_op_fcvt_s_d; break;
                case 3: op = rv_op_fcvt_s_q; break;
                }
                break;
            case 33:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_d_s; break;
                case 3: op = rv_op_fcvt_d_q; break;
                }
                break;
            case 35:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_q_s; break;
                case 1: op = rv_op_fcvt_q_d; break;
                }
                break;
            case 44:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fsqrt_s; break;
                }
                break;
            case 45:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fsqrt_d; break;
                }
                break;
            case 47:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fsqrt_q; break;
                }
                break;
            case 80:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fle_s; break;
                case 1: op = rv_op_flt_s; break;
                case 2: op = rv_op_feq_s; break;
                }
                break;
            case 81:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fle_d; break;
                case 1: op = rv_op_flt_d; break;
                case 2: op = rv_op_feq_d; break;
                }
                break;
            case 83:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fle_q; break;
                case 1: op = rv_op_flt_q; break;
                case 2: op = rv_op_feq_q; break;
                }
                break;
            case 96:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_w_s; break;
                case 1: op = rv_op_fcvt_wu_s; break;
                case 2: op = rv_op_fcvt_l_s; break;
                case 3: op = rv_op_fcvt_lu_s; break;
                }
                break;
            case 97:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_w_d; break;
                case 1: op = rv_op_fcvt_wu_d; break;
                case 2: op = rv_op_fcvt_l_d; break;
                case 3: op = rv_op_fcvt_lu_d; break;
                }
                break;
            case 99:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_w_q; break;
                case 1: op = rv_op_fcvt_wu_q; break;
                case 2: op = rv_op_fcvt_l_q; break;
                case 3: op = rv_op_fcvt_lu_q; break;
                }
                break;
            case 104:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_s_w; break;
                case 1: op = rv_op_fcvt_s_wu; break;
                case 2: op = rv_op_fcvt_s_l; break;
                case 3: op = rv_op_fcvt_s_lu; break;
                }
                break;
            case 105:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_d_w; break;
                case 1: op = rv_op_fcvt_d_wu; break;
                case 2: op = rv_op_fcvt_d_l; break;
                case 3: op = rv_op_fcvt_d_lu; break;
                }
                break;
            case 107:
                switch (((inst >> 20) & 0b11111)) {
                case 0: op = rv_op_fcvt_q_w; break;
                case 1: op = rv_op_fcvt_q_wu; break;
                case 2: op = rv_op_fcvt_q_l; break;
                case 3: op = rv_op_fcvt_q_lu; break;
                }
                break;
            case 112:
                switch (((inst >> 17) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_s; break;
                case 1: op = rv_op_fclass_s; break;
                }
                break;
            case 113:
                switch (((inst >> 17) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_d; break;
                case 1: op = rv_op_fclass_d; break;
                }
                break;
            case 115:
                switch (((inst >> 17) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_q; break;
                case 1: op = rv_op_fclass_q; break;
                }
                break;
            case 120:
                switch (((inst >> 17) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_s_x; break;
                }
                break;
            case 121:
                switch (((inst >> 17) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_d_x; break;
                }
                break;
            case 123:
                switch (((inst >> 17) & 0b11111000) | ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_q_x; break;
                }
                break;
            }
            break;
        case 22:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_addid; break;
            case 1:
                switch (((inst >> 26) & 0b111111)) {
                case 0: op = rv_op_sllid; break;
                }
                break;
            case 5:
                switch (((inst >> 26) & 0b111111)) {
                case 0: op = rv_op_srlid; break;
                case 16: op = rv_op_sraid; break;
                }
                break;
            }
            break;
        case 24:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_beq; break;
            case 1: op = rv_op_bne; break;
            case 4: op = rv_op_blt; break;
            case 5: op = rv_op_bge; break;
            case 6: op = rv_op_bltu; break;
            case 7: op = rv_op_bgeu; break;
            }
            break;
        case 25:
            switch (((inst >> 12) & 0b111)) {
            case 0: op = rv_op_jalr; break;
            }
            break;
        case 27: op = rv_op_jal; break;
        case 28:
            switch (((inst >> 12) & 0b111)) {
            case 0:
                switch (((inst >> 20) & 0b111111100000) | ((inst >> 7) & 0b000000011111)) {
                case 0:
                    switch (((inst >> 15) & 0b1111111111)) {
                    case 0: op = rv_op_ecall; break;
                    case 32: op = rv_op_ebreak; break;
                    case 64: op = rv_op_uret; break;
                    }
                    break;
                case 256:
                    switch (((inst >> 20) & 0b11111)) {
                    case 2:
                        switch (((inst >> 15) & 0b11111)) {
                        case 0: op = rv_op_sret; break;
                        }
                        break;
                    case 4: op = rv_op_sfence_vm; break;
                    case 5:
                        switch (((inst >> 15) & 0b11111)) {
                        case 0: op = rv_op_wfi; break;
                        }
                        break;
                    }
                    break;
                case 288: op = rv_op_sfence_vma; break;
                case 512:
                    switch (((inst >> 15) & 0b1111111111)) {
                    case 64: op = rv_op_hret; break;
                    }
                    break;
                case 768:
                    switch (((inst >> 15) & 0b1111111111)) {
                    case 64: op = rv_op_mret; break;
                    }
                    break;
                case 1952:
                    switch (((inst >> 15) & 0b1111111111)) {
                    case 576: op = rv_op_dret; break;
                    }
                    break;
                }
                break;
            case 1: op = rv_op_csrrw; break;
            case 2: op = rv_op_csrrs; break;
            case 3: op = rv_op_csrrc; break;
            case 5: op = rv_op_csrrwi; break;
            case 6: op = rv_op_csrrsi; break;
            case 7: op = rv_op_csrrci; break;
            }
            break;
        case 30:
            switch (((inst >> 22) & 0b1111111000) | ((inst >> 12) & 0b0000000111)) {
            case 0: op = rv_op_addd; break;
            case 1: op = rv_op_slld; break;
            case 5: op = rv_op_srld; break;
            case 8: op = rv_op_muld; break;
            case 12: op = rv_op_divd; break;
            case 13: op = rv_op_divud; break;
            case 14: op = rv_op_remd; break;
            case 15: op = rv_op_remud; break;
            case 256: op = rv_op_subd; break;
            case 261: op = rv_op_srad; break;
            }
            break;
        }
        break;
    }
    dec->op = op;
}

/* operand extractors */

static uint32_t operand_rd(rv_inst inst)
{
    return (inst << 52) >> 59;
}

static uint32_t operand_rs1(rv_inst inst)
{
    return (inst << 44) >> 59;
}

static uint32_t operand_rs2(rv_inst inst)
{
    return (inst << 39) >> 59;
}

static uint32_t operand_rs3(rv_inst inst)
{
    return (inst << 32) >> 59;
}

static uint32_t operand_aq(rv_inst inst)
{
    return (inst << 37) >> 63;
}

static uint32_t operand_rl(rv_inst inst)
{
    return (inst << 38) >> 63;
}

static uint32_t operand_pred(rv_inst inst)
{
    return (inst << 36) >> 60;
}

static uint32_t operand_succ(rv_inst inst)
{
    return (inst << 40) >> 60;
}

static uint32_t operand_rm(rv_inst inst)
{
    return (inst << 49) >> 61;
}

static uint32_t operand_shamt5(rv_inst inst)
{
    return (inst << 39) >> 59;
}

static uint32_t operand_shamt6(rv_inst inst)
{
    return (inst << 38) >> 58;
}

static uint32_t operand_shamt7(rv_inst inst)
{
    return (inst << 37) >> 57;
}

static uint32_t operand_crdq(rv_inst inst)
{
    return (inst << 59) >> 61;
}

static uint32_t operand_crs1q(rv_inst inst)
{
    return (inst << 54) >> 61;
}

static uint32_t operand_crs1rdq(rv_inst inst)
{
    return (inst << 54) >> 61;
}

static uint32_t operand_crs2q(rv_inst inst)
{
    return (inst << 59) >> 61;
}

static uint32_t operand_crd(rv_inst inst)
{
    return (inst << 52) >> 59;
}

static uint32_t operand_crs1(rv_inst inst)
{
    return (inst << 52) >> 59;
}

static uint32_t operand_crs1rd(rv_inst inst)
{
    return (inst << 52) >> 59;
}

static uint32_t operand_crs2(rv_inst inst)
{
    return (inst << 57) >> 59;
}

static uint32_t operand_cimmsh5(rv_inst inst)
{
    return (inst << 57) >> 59;
}

static uint32_t operand_csr12(rv_inst inst)
{
    return (inst << 32) >> 52;
}

static int32_t operand_imm12(rv_inst inst)
{
    return ((int64_t)inst << 32) >> 52;
}

static int32_t operand_imm20(rv_inst inst)
{
    return (((int64_t)inst << 32) >> 44) << 12;
}

static int32_t operand_jimm20(rv_inst inst)
{
    return (((int64_t)inst << 32) >> 63) << 20 |
        ((inst << 33) >> 54) << 1 |
        ((inst << 43) >> 63) << 11 |
        ((inst << 44) >> 56) << 12;
}

static int32_t operand_simm12(rv_inst inst)
{
    return (((int64_t)inst << 32) >> 57) << 5 |
        (inst << 52) >> 59;
}

static int32_t operand_sbimm12(rv_inst inst)
{
    return (((int64_t)inst << 32) >> 63) << 12 |
        ((inst << 33) >> 58) << 5 |
        ((inst << 52) >> 60) << 1 |
        ((inst << 56) >> 63) << 11;
}

static uint32_t operand_cimmsh6(rv_inst inst)
{
    return ((inst << 51) >> 63) << 5 |
        (inst << 57) >> 59;
}

static int32_t operand_cimmi(rv_inst inst)
{
    return (((int64_t)inst << 51) >> 63) << 5 |
        (inst << 57) >> 59;
}

static int32_t operand_cimmui(rv_inst inst)
{
    return (((int64_t)inst << 51) >> 63) << 17 |
        ((inst << 57) >> 59) << 12;
}

static uint32_t operand_cimmlwsp(rv_inst inst)
{
    return ((inst << 51) >> 63) << 5 |
        ((inst << 57) >> 61) << 2 |
        ((inst << 60) >> 62) << 6;
}

static uint32_t operand_cimmldsp(rv_inst inst)
{
    return ((inst << 51) >> 63) << 5 |
        ((inst << 57) >> 62) << 3 |
        ((inst << 59) >> 61) << 6;
}

static uint32_t operand_cimmlqsp(rv_inst inst)
{
    return ((inst << 51) >> 63) << 5 |
        ((inst << 57) >> 63) << 4 |
        ((inst << 58) >> 60) << 6;
}

static int32_t operand_cimm16sp(rv_inst inst)
{
    return (((int64_t)inst << 51) >> 63) << 9 |
        ((inst << 57) >> 63) << 4 |
        ((inst << 58) >> 63) << 6 |
        ((inst << 59) >> 62) << 7 |
        ((inst << 61) >> 63) << 5;
}

static int32_t operand_cimmj(rv_inst inst)
{
    return (((int64_t)inst << 51) >> 63) << 11 |
        ((inst << 52) >> 63) << 4 |
        ((inst << 53) >> 62) << 8 |
        ((inst << 55) >> 63) << 10 |
        ((inst << 56) >> 63) << 6 |
        ((inst << 57) >> 63) << 7 |
        ((inst << 58) >> 61) << 1 |
        ((inst << 61) >> 63) << 5;
}

static int32_t operand_cimmb(rv_inst inst)
{
    return (((int64_t)inst << 51) >> 63) << 8 |
        ((inst << 52) >> 62) << 3 |
        ((inst << 57) >> 62) << 6 |
        ((inst << 59) >> 62) << 1 |
        ((inst << 61) >> 63) << 5;
}

static uint32_t operand_cimmswsp(rv_inst inst)
{
    return ((inst << 51) >> 60) << 2 |
        ((inst << 55) >> 62) << 6;
}

static uint32_t operand_cimmsdsp(rv_inst inst)
{
    return ((inst << 51) >> 61) << 3 |
        ((inst << 54) >> 61) << 6;
}

static uint32_t operand_cimmsqsp(rv_inst inst)
{
    return ((inst << 51) >> 62) << 4 |
        ((inst << 53) >> 60) << 6;
}

static uint32_t operand_cimm4spn(rv_inst inst)
{
    return ((inst << 51) >> 62) << 4 |
        ((inst << 53) >> 60) << 6 |
        ((inst << 57) >> 63) << 2 |
        ((inst << 58) >> 63) << 3;
}

static uint32_t operand_cimmw(rv_inst inst)
{
    return ((inst << 51) >> 61) << 3 |
        ((inst << 57) >> 63) << 2 |
        ((inst << 58) >> 63) << 6;
}

static uint32_t operand_cimmd(rv_inst inst)
{
    return ((inst << 51) >> 61) << 3 |
        ((inst << 57) >> 62) << 6;
}

static uint32_t operand_cimmq(rv_inst inst)
{
    return ((inst << 51) >> 62) << 4 |
        ((inst << 53) >> 63) << 8 |
        ((inst << 57) >> 62) << 6;
}

/* decode operands */

static void decode_inst_operands(rv_decode *dec)
{
    rv_inst inst = dec->inst;
    dec->codec = opcode_data[dec->op].codec;
    switch (dec->codec) {
    case rv_codec_none:
        dec->rd = dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->imm = 0;
        break;
    case rv_codec_u:
        dec->rd = operand_rd(inst);
        dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->imm = operand_imm20(inst);
        break;
    case rv_codec_uj:
        dec->rd = operand_rd(inst);
        dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->imm = operand_jimm20(inst);
        break;
    case rv_codec_i:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_imm12(inst);
        break;
    case rv_codec_i_sh5:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_shamt5(inst);
        break;
    case rv_codec_i_sh6:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_shamt6(inst);
        break;
    case rv_codec_i_sh7:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_shamt7(inst);
        break;
    case rv_codec_i_csr:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_csr12(inst);
        break;
    case rv_codec_s:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = operand_simm12(inst);
        break;
    case rv_codec_sb:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = operand_sbimm12(inst);
        break;
    case rv_codec_r:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = 0;
        break;
    case rv_codec_r_m:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = 0;
        dec->rm = operand_rm(inst);
        break;
    case rv_codec_r4_m:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->rs3 = operand_rs3(inst);
        dec->imm = 0;
        dec->rm = operand_rm(inst);
        break;
    case rv_codec_r_a:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = 0;
        dec->aq = operand_aq(inst);
        dec->rl = operand_rl(inst);
        break;
    case rv_codec_r_l:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = 0;
        dec->aq = operand_aq(inst);
        dec->rl = operand_rl(inst);
        break;
    case rv_codec_r_f:
        dec->rd = dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->pred = operand_pred(inst);
        dec->succ = operand_succ(inst);
        dec->imm = 0;
        break;
    case rv_codec_cb:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmb(inst);
        break;
    case rv_codec_cb_imm:
        dec->rd = dec->rs1 = operand_crs1rdq(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmi(inst);
        break;
    case rv_codec_cb_sh5:
        dec->rd = dec->rs1 = operand_crs1rdq(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmsh5(inst);
        break;
    case rv_codec_cb_sh6:
        dec->rd = dec->rs1 = operand_crs1rdq(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmsh6(inst);
        break;
    case rv_codec_ci:
        dec->rd = dec->rs1 = operand_crs1rd(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmi(inst);
        break;
    case rv_codec_ci_sh5:
        dec->rd = dec->rs1 = operand_crs1rd(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmsh5(inst);
        break;
    case rv_codec_ci_sh6:
        dec->rd = dec->rs1 = operand_crs1rd(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmsh6(inst);
        break;
    case rv_codec_ci_16sp:
        dec->rd = rv_ireg_sp;
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimm16sp(inst);
        break;
    case rv_codec_ci_lwsp:
        dec->rd = operand_crd(inst);
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmlwsp(inst);
        break;
    case rv_codec_ci_ldsp:
        dec->rd = operand_crd(inst);
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmldsp(inst);
        break;
    case rv_codec_ci_lqsp:
        dec->rd = operand_crd(inst);
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmlqsp(inst);
        break;
    case rv_codec_ci_li:
        dec->rd = operand_crd(inst);
        dec->rs1 = rv_ireg_zero;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmi(inst);
        break;
    case rv_codec_ci_lui:
        dec->rd = operand_crd(inst);
        dec->rs1 = rv_ireg_zero;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmui(inst);
        break;
    case rv_codec_ci_none:
        dec->rd = dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->imm = 0;
        break;
    case rv_codec_ciw_4spn:
        dec->rd = operand_crdq(inst) + 8;
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimm4spn(inst);
        break;
    case rv_codec_cj:
        dec->rd = dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmj(inst);
        break;
    case rv_codec_cj_jal:
        dec->rd = rv_ireg_ra;
        dec->rs1 = dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmj(inst);
        break;
    case rv_codec_cl_lw:
        dec->rd = operand_crdq(inst) + 8;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmw(inst);
        break;
    case rv_codec_cl_ld:
        dec->rd = operand_crdq(inst) + 8;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmd(inst);
        break;
    case rv_codec_cl_lq:
        dec->rd = operand_crdq(inst) + 8;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = rv_ireg_zero;
        dec->imm = operand_cimmq(inst);
        break;
    case rv_codec_cr:
        dec->rd = dec->rs1 = operand_crs1rd(inst);
        dec->rs2 = operand_crs2(inst);
        dec->imm = 0;
        break;
    case rv_codec_cr_mv:
        dec->rd = operand_crd(inst);
        dec->rs1 = operand_crs2(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = 0;
        break;
    case rv_codec_cr_jalr:
        dec->rd = rv_ireg_ra;
        dec->rs1 = operand_crs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = 0;
        break;
    case rv_codec_cr_jr:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_crs1(inst);
        dec->rs2 = rv_ireg_zero;
        dec->imm = 0;
        break;
    case rv_codec_cs:
        dec->rd = dec->rs1 = operand_crs1rdq(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        dec->imm = 0;
        break;
    case rv_codec_cs_sw:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        dec->imm = operand_cimmw(inst);
        break;
    case rv_codec_cs_sd:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        dec->imm = operand_cimmd(inst);
        break;
    case rv_codec_cs_sq:
        dec->rd = rv_ireg_zero;
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        dec->imm = operand_cimmq(inst);
        break;
    case rv_codec_css_swsp:
        dec->rd = rv_ireg_zero;
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = operand_crs2(inst);
        dec->imm = operand_cimmswsp(inst);
        break;
    case rv_codec_css_sdsp:
        dec->rd = rv_ireg_zero;
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = operand_crs2(inst);
        dec->imm = operand_cimmsdsp(inst);
        break;
    case rv_codec_css_sqsp:
        dec->rd = rv_ireg_zero;
        dec->rs1 = rv_ireg_sp;
        dec->rs2 = operand_crs2(inst);
        dec->imm = operand_cimmsqsp(inst);
        break;
    };
}

/* check constraint */

static bool check_constraints(rv_decode *dec, const rvc_constraint *c)
{
    int32_t imm = dec->imm;
    uint8_t rd = dec->rd, rs1 = dec->rs1, rs2 = dec->rs2;
    while (*c != rvc_end) {
        switch (*c) {
        case rvc_rd_eq_ra:
            if (!(rd == 1)) {
                return false;
            }
            break;
        case rvc_rd_eq_x0:
            if (!(rd == 0)) {
                return false;
            }
            break;
        case rvc_rs1_eq_x0:
            if (!(rs1 == 0)) {
                return false;
            }
            break;
        case rvc_rs2_eq_x0:
            if (!(rs2 == 0)) {
                return false;
            }
            break;
        case rvc_rs2_eq_rs1:
            if (!(rs2 == rs1)) {
                return false;
            }
            break;
        case rvc_rs1_eq_ra:
            if (!(rs1 == 1)) {
                return false;
            }
            break;
        case rvc_imm_eq_zero:
            if (!(imm == 0)) {
                return false;
            }
            break;
        case rvc_imm_eq_n1:
            if (!(imm == -1)) {
                return false;
            }
            break;
        case rvc_imm_eq_p1:
            if (!(imm == 1)) {
                return false;
            }
            break;
        case rvc_csr_eq_0x001:
            if (!(imm == 0x001)) {
                return false;
            }
            break;
        case rvc_csr_eq_0x002:
            if (!(imm == 0x002)) {
                return false;
            }
            break;
        case rvc_csr_eq_0x003:
            if (!(imm == 0x003)) {
                return false;
            }
            break;
        case rvc_csr_eq_0xc00:
            if (!(imm == 0xc00)) {
                return false;
            }
            break;
        case rvc_csr_eq_0xc01:
            if (!(imm == 0xc01)) {
                return false;
            }
            break;
        case rvc_csr_eq_0xc02:
            if (!(imm == 0xc02)) {
                return false;
            }
            break;
        case rvc_csr_eq_0xc80:
            if (!(imm == 0xc80)) {
                return false;
            }
            break;
        case rvc_csr_eq_0xc81:
            if (!(imm == 0xc81)) {
                return false;
            }
            break;
        case rvc_csr_eq_0xc82:
            if (!(imm == 0xc82)) {
                return false;
            }
            break;
        default: break;
        }
        c++;
    }
    return true;
}

/* instruction length */

static size_t inst_length(rv_inst inst)
{
    /* NOTE: supports maximum instruction size of 64-bits */

    /* instruction length coding
     *
     *      aa - 16 bit aa != 11
     *   bbb11 - 32 bit bbb != 111
     *  011111 - 48 bit
     * 0111111 - 64 bit
     */

    return (inst &      0b11) != 0b11      ? 2
         : (inst &   0b11100) != 0b11100   ? 4
         : (inst &  0b111111) == 0b011111  ? 6
         : (inst & 0b1111111) == 0b0111111 ? 8
         : 0;
}

/* format instruction */

static void append(char *s1, const char *s2, size_t n)
{
    size_t l1 = strlen(s1);
    if (n - l1 - 1 > 0) {
        strncat(s1, s2, n - l1);
    }
}

static void format_inst(char *buf, size_t buflen, size_t tab, rv_decode *dec)
{
    char tmp[64];
    const char *fmt;

    fmt = opcode_data[dec->op].format;
    while (*fmt) {
        switch (*fmt) {
        case 'O':
            append(buf, opcode_data[dec->op].name, buflen);
            break;
        case '(':
            append(buf, "(", buflen);
            break;
        case ',':
            append(buf, ",", buflen);
            break;
        case ')':
            append(buf, ")", buflen);
            break;
        case '0':
            append(buf, rv_ireg_name_sym[dec->rd], buflen);
            break;
        case '1':
            append(buf, rv_ireg_name_sym[dec->rs1], buflen);
            break;
        case '2':
            append(buf, rv_ireg_name_sym[dec->rs2], buflen);
            break;
        case '3':
            append(buf, rv_freg_name_sym[dec->rd], buflen);
            break;
        case '4':
            append(buf, rv_freg_name_sym[dec->rs1], buflen);
            break;
        case '5':
            append(buf, rv_freg_name_sym[dec->rs2], buflen);
            break;
        case '6':
            append(buf, rv_freg_name_sym[dec->rs3], buflen);
            break;
        case '7':
            snprintf(tmp, sizeof(tmp), "%d", dec->rs1);
            append(buf, tmp, buflen);
            break;
        case 'i':
            snprintf(tmp, sizeof(tmp), "%d", dec->imm);
            append(buf, tmp, buflen);
            break;
        case 'o':
            snprintf(tmp, sizeof(tmp), "%d", dec->imm);
            append(buf, tmp, buflen);
            while (strlen(buf) < tab * 2) {
                append(buf, " ", buflen);
            }
            snprintf(tmp, sizeof(tmp), "# 0x%" PRIx64,
                dec->pc + dec->imm);
            append(buf, tmp, buflen);
            break;
        case 'c': {
            const char *name = csr_name(dec->imm & 0xfff);
            if (name) {
                append(buf, name, buflen);
            } else {
                snprintf(tmp, sizeof(tmp), "0x%03x", dec->imm & 0xfff);
                append(buf, tmp, buflen);
            }
            break;
        }
        case 'r':
            switch (dec->rm) {
            case rv_rm_rne:
                append(buf, "rne", buflen);
                break;
            case rv_rm_rtz:
                append(buf, "rtz", buflen);
                break;
            case rv_rm_rdn:
                append(buf, "rdn", buflen);
                break;
            case rv_rm_rup:
                append(buf, "rup", buflen);
                break;
            case rv_rm_rmm:
                append(buf, "rmm", buflen);
                break;
            case rv_rm_dyn:
                append(buf, "dyn", buflen);
                break;
            default:
                append(buf, "inv", buflen);
                break;
            }
            break;
        case 'p':
            if (dec->pred & rv_fence_i) {
                append(buf, "i", buflen);
            }
            if (dec->pred & rv_fence_o) {
                append(buf, "o", buflen);
            }
            if (dec->pred & rv_fence_r) {
                append(buf, "r", buflen);
            }
            if (dec->pred & rv_fence_w) {
                append(buf, "w", buflen);
            }
            break;
        case 's':
            if (dec->succ & rv_fence_i) {
                append(buf, "i", buflen);
            }
            if (dec->succ & rv_fence_o) {
                append(buf, "o", buflen);
            }
            if (dec->succ & rv_fence_r) {
                append(buf, "r", buflen);
            }
            if (dec->succ & rv_fence_w) {
                append(buf, "w", buflen);
            }
            break;
        case '\t':
            while (strlen(buf) < tab) {
                append(buf, " ", buflen);
            }
            break;
        case 'A':
            if (dec->aq) {
                append(buf, ".aq", buflen);
            }
            break;
        case 'R':
            if (dec->rl) {
                append(buf, ".rl", buflen);
            }
            break;
        default:
            break;
        }
        fmt++;
    }
}

/* lift instruction to pseudo-instruction */

static void decode_inst_lift_pseudo(rv_decode *dec)
{
    const rv_comp_data *comp_data = opcode_data[dec->op].pseudo;
    if (!comp_data) {
        return;
    }
    while (comp_data->constraints) {
        if (check_constraints(dec, comp_data->constraints)) {
            dec->op = comp_data->op;
            dec->codec = opcode_data[dec->op].codec;
            return;
        }
        comp_data++;
    }
}

/* decompress instruction */

static void decode_inst_decompress_rv32(rv_decode *dec)
{
    int decomp_op = opcode_data[dec->op].decomp_rv32;
    if (decomp_op != rv_op_illegal) {
        if ((opcode_data[dec->op].decomp_data & rvcd_imm_nz)
            && dec->imm == 0) {
            dec->op = rv_op_illegal;
        } else {
            dec->op = decomp_op;
            dec->codec = opcode_data[decomp_op].codec;
        }
    }
}

static void decode_inst_decompress_rv64(rv_decode *dec)
{
    int decomp_op = opcode_data[dec->op].decomp_rv64;
    if (decomp_op != rv_op_illegal) {
        if ((opcode_data[dec->op].decomp_data & rvcd_imm_nz)
            && dec->imm == 0) {
            dec->op = rv_op_illegal;
        } else {
            dec->op = decomp_op;
            dec->codec = opcode_data[decomp_op].codec;
        }
    }
}

static void decode_inst_decompress_rv128(rv_decode *dec)
{
    int decomp_op = opcode_data[dec->op].decomp_rv128;
    if (decomp_op != rv_op_illegal) {
        if ((opcode_data[dec->op].decomp_data & rvcd_imm_nz)
            && dec->imm == 0) {
            dec->op = rv_op_illegal;
        } else {
            dec->op = decomp_op;
            dec->codec = opcode_data[decomp_op].codec;
        }
    }
}

static void decode_inst_decompress(rv_decode *dec, rv_isa isa)
{
    switch (isa) {
    case rv32:
        decode_inst_decompress_rv32(dec);
        break;
    case rv64:
        decode_inst_decompress_rv64(dec);
        break;
    case rv128:
        decode_inst_decompress_rv128(dec);
        break;
    }
}

/* disassemble instruction */

static void
disasm_inst(char *buf, size_t buflen, rv_isa isa, uint64_t pc, rv_inst inst)
{
    rv_decode dec = { 0 };
    dec.pc = pc;
    dec.inst = inst;
    decode_inst_opcode(&dec, isa);
    decode_inst_operands(&dec);
    decode_inst_decompress(&dec, isa);
    decode_inst_lift_pseudo(&dec);
    format_inst(buf, buflen, 16, &dec);
}

#define INST_FMT_2 "%04" PRIx64 "              "
#define INST_FMT_4 "%08" PRIx64 "          "
#define INST_FMT_6 "%012" PRIx64 "      "
#define INST_FMT_8 "%016" PRIx64 "  "

static int
print_insn_riscv(bfd_vma memaddr, struct disassemble_info *info, rv_isa isa)
{
    char buf[128] = { 0 };
    bfd_byte packet[2];
    rv_inst inst = 0;
    size_t len = 2;
    bfd_vma n;
    int status;

    /* Instructions are made of 2-byte packets in little-endian order */
    for (n = 0; n < len; n += 2) {
        status = (*info->read_memory_func)(memaddr + n, packet, 2, info);
        if (status != 0) {
            /* Don't fail just because we fell off the end.  */
            if (n > 0) {
                break;
            }
            (*info->memory_error_func)(status, memaddr, info);
            return status;
        }
        inst |= ((rv_inst) bfd_getl16(packet)) << (8 * n);
        if (n == 0) {
            len = inst_length(inst);
        }
    }

    switch (len) {
    case 2:
        (*info->fprintf_func)(info->stream, INST_FMT_2, inst);
        break;
    case 4:
        (*info->fprintf_func)(info->stream, INST_FMT_4, inst);
        break;
    case 6:
        (*info->fprintf_func)(info->stream, INST_FMT_6, inst);
        break;
    default:
        (*info->fprintf_func)(info->stream, INST_FMT_8, inst);
        break;
    }

    disasm_inst(buf, sizeof(buf), isa, memaddr, inst);
    (*info->fprintf_func)(info->stream, "%s", buf);

    return len;
}

int print_insn_riscv32(bfd_vma memaddr, struct disassemble_info *info)
{
    return print_insn_riscv(memaddr, info, rv32);
}

int print_insn_riscv64(bfd_vma memaddr, struct disassemble_info *info)
{
    return print_insn_riscv(memaddr, info, rv64);
}

int print_insn_riscv128(bfd_vma memaddr, struct disassemble_info *info)
{
    return print_insn_riscv(memaddr, info, rv128);
}
