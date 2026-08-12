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
#include "qemu/bitops.h"
#include "disas/dis-asm.h"
#include "target/riscv/cpu_cfg.h"
#include "disas/riscv.h"

/* Vendor extensions */
#include "disas/riscv-xthead.h"
#include "disas/riscv-xventana.h"
#include "disas/riscv-xlrbr.h"

typedef enum {
    /* 0 is reserved for rv_op_illegal. */
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
    rv_op_jal_ra = 298,
    rv_op_jalr_ra = 299,
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
    rv_op_aes32esmi = 362,
    rv_op_aes32esi = 363,
    rv_op_aes32dsmi = 364,
    rv_op_aes32dsi = 365,
    rv_op_aes64ks1i = 366,
    rv_op_aes64ks2 = 367,
    rv_op_aes64im = 368,
    rv_op_aes64esm = 369,
    rv_op_aes64es = 370,
    rv_op_aes64dsm = 371,
    rv_op_aes64ds = 372,
    rv_op_sha256sig0 = 373,
    rv_op_sha256sig1 = 374,
    rv_op_sha256sum0 = 375,
    rv_op_sha256sum1 = 376,
    rv_op_sha512sig0 = 377,
    rv_op_sha512sig1 = 378,
    rv_op_sha512sum0 = 379,
    rv_op_sha512sum1 = 380,
    rv_op_sha512sum0r = 381,
    rv_op_sha512sum1r = 382,
    rv_op_sha512sig0l = 383,
    rv_op_sha512sig0h = 384,
    rv_op_sha512sig1l = 385,
    rv_op_sha512sig1h = 386,
    rv_op_sm3p0 = 387,
    rv_op_sm3p1 = 388,
    rv_op_sm4ed = 389,
    rv_op_sm4ks = 390,
    rv_op_brev8 = 391,
    rv_op_pack = 392,
    rv_op_packh = 393,
    rv_op_packw = 394,
    rv_op_unzip = 395,
    rv_op_zip = 396,
    rv_op_xperm4 = 397,
    rv_op_xperm8 = 398,
    rv_op_vle8_v = 399,
    rv_op_vle16_v = 400,
    rv_op_vle32_v = 401,
    rv_op_vle64_v = 402,
    rv_op_vse8_v = 403,
    rv_op_vse16_v = 404,
    rv_op_vse32_v = 405,
    rv_op_vse64_v = 406,
    rv_op_vlm_v = 407,
    rv_op_vsm_v = 408,
    rv_op_vlse8_v = 409,
    rv_op_vlse16_v = 410,
    rv_op_vlse32_v = 411,
    rv_op_vlse64_v = 412,
    rv_op_vsse8_v = 413,
    rv_op_vsse16_v = 414,
    rv_op_vsse32_v = 415,
    rv_op_vsse64_v = 416,
    rv_op_vluxei8_v = 417,
    rv_op_vluxei16_v = 418,
    rv_op_vluxei32_v = 419,
    rv_op_vluxei64_v = 420,
    rv_op_vloxei8_v = 421,
    rv_op_vloxei16_v = 422,
    rv_op_vloxei32_v = 423,
    rv_op_vloxei64_v = 424,
    rv_op_vsuxei8_v = 425,
    rv_op_vsuxei16_v = 426,
    rv_op_vsuxei32_v = 427,
    rv_op_vsuxei64_v = 428,
    rv_op_vsoxei8_v = 429,
    rv_op_vsoxei16_v = 430,
    rv_op_vsoxei32_v = 431,
    rv_op_vsoxei64_v = 432,
    rv_op_vle8ff_v = 433,
    rv_op_vle16ff_v = 434,
    rv_op_vle32ff_v = 435,
    rv_op_vle64ff_v = 436,
    rv_op_vl1re8_v = 437,
    rv_op_vl1re16_v = 438,
    rv_op_vl1re32_v = 439,
    rv_op_vl1re64_v = 440,
    rv_op_vl2re8_v = 441,
    rv_op_vl2re16_v = 442,
    rv_op_vl2re32_v = 443,
    rv_op_vl2re64_v = 444,
    rv_op_vl4re8_v = 445,
    rv_op_vl4re16_v = 446,
    rv_op_vl4re32_v = 447,
    rv_op_vl4re64_v = 448,
    rv_op_vl8re8_v = 449,
    rv_op_vl8re16_v = 450,
    rv_op_vl8re32_v = 451,
    rv_op_vl8re64_v = 452,
    rv_op_vs1r_v = 453,
    rv_op_vs2r_v = 454,
    rv_op_vs4r_v = 455,
    rv_op_vs8r_v = 456,
    rv_op_vadd_vv = 457,
    rv_op_vadd_vx = 458,
    rv_op_vadd_vi = 459,
    rv_op_vsub_vv = 460,
    rv_op_vsub_vx = 461,
    rv_op_vrsub_vx = 462,
    rv_op_vrsub_vi = 463,
    rv_op_vwaddu_vv = 464,
    rv_op_vwaddu_vx = 465,
    rv_op_vwadd_vv = 466,
    rv_op_vwadd_vx = 467,
    rv_op_vwsubu_vv = 468,
    rv_op_vwsubu_vx = 469,
    rv_op_vwsub_vv = 470,
    rv_op_vwsub_vx = 471,
    rv_op_vwaddu_wv = 472,
    rv_op_vwaddu_wx = 473,
    rv_op_vwadd_wv = 474,
    rv_op_vwadd_wx = 475,
    rv_op_vwsubu_wv = 476,
    rv_op_vwsubu_wx = 477,
    rv_op_vwsub_wv = 478,
    rv_op_vwsub_wx = 479,
    rv_op_vadc_vvm = 480,
    rv_op_vadc_vxm = 481,
    rv_op_vadc_vim = 482,
    rv_op_vmadc_vvm = 483,
    rv_op_vmadc_vxm = 484,
    rv_op_vmadc_vim = 485,
    rv_op_vsbc_vvm = 486,
    rv_op_vsbc_vxm = 487,
    rv_op_vmsbc_vvm = 488,
    rv_op_vmsbc_vxm = 489,
    rv_op_vand_vv = 490,
    rv_op_vand_vx = 491,
    rv_op_vand_vi = 492,
    rv_op_vor_vv = 493,
    rv_op_vor_vx = 494,
    rv_op_vor_vi = 495,
    rv_op_vxor_vv = 496,
    rv_op_vxor_vx = 497,
    rv_op_vxor_vi = 498,
    rv_op_vsll_vv = 499,
    rv_op_vsll_vx = 500,
    rv_op_vsll_vi = 501,
    rv_op_vsrl_vv = 502,
    rv_op_vsrl_vx = 503,
    rv_op_vsrl_vi = 504,
    rv_op_vsra_vv = 505,
    rv_op_vsra_vx = 506,
    rv_op_vsra_vi = 507,
    rv_op_vnsrl_wv = 508,
    rv_op_vnsrl_wx = 509,
    rv_op_vnsrl_wi = 510,
    rv_op_vnsra_wv = 511,
    rv_op_vnsra_wx = 512,
    rv_op_vnsra_wi = 513,
    rv_op_vmseq_vv = 514,
    rv_op_vmseq_vx = 515,
    rv_op_vmseq_vi = 516,
    rv_op_vmsne_vv = 517,
    rv_op_vmsne_vx = 518,
    rv_op_vmsne_vi = 519,
    rv_op_vmsltu_vv = 520,
    rv_op_vmsltu_vx = 521,
    rv_op_vmslt_vv = 522,
    rv_op_vmslt_vx = 523,
    rv_op_vmsleu_vv = 524,
    rv_op_vmsleu_vx = 525,
    rv_op_vmsleu_vi = 526,
    rv_op_vmsle_vv = 527,
    rv_op_vmsle_vx = 528,
    rv_op_vmsle_vi = 529,
    rv_op_vmsgtu_vx = 530,
    rv_op_vmsgtu_vi = 531,
    rv_op_vmsgt_vx = 532,
    rv_op_vmsgt_vi = 533,
    rv_op_vminu_vv = 534,
    rv_op_vminu_vx = 535,
    rv_op_vmin_vv = 536,
    rv_op_vmin_vx = 537,
    rv_op_vmaxu_vv = 538,
    rv_op_vmaxu_vx = 539,
    rv_op_vmax_vv = 540,
    rv_op_vmax_vx = 541,
    rv_op_vmul_vv = 542,
    rv_op_vmul_vx = 543,
    rv_op_vmulh_vv = 544,
    rv_op_vmulh_vx = 545,
    rv_op_vmulhu_vv = 546,
    rv_op_vmulhu_vx = 547,
    rv_op_vmulhsu_vv = 548,
    rv_op_vmulhsu_vx = 549,
    rv_op_vdivu_vv = 550,
    rv_op_vdivu_vx = 551,
    rv_op_vdiv_vv = 552,
    rv_op_vdiv_vx = 553,
    rv_op_vremu_vv = 554,
    rv_op_vremu_vx = 555,
    rv_op_vrem_vv = 556,
    rv_op_vrem_vx = 557,
    rv_op_vwmulu_vv = 558,
    rv_op_vwmulu_vx = 559,
    rv_op_vwmulsu_vv = 560,
    rv_op_vwmulsu_vx = 561,
    rv_op_vwmul_vv = 562,
    rv_op_vwmul_vx = 563,
    rv_op_vmacc_vv = 564,
    rv_op_vmacc_vx = 565,
    rv_op_vnmsac_vv = 566,
    rv_op_vnmsac_vx = 567,
    rv_op_vmadd_vv = 568,
    rv_op_vmadd_vx = 569,
    rv_op_vnmsub_vv = 570,
    rv_op_vnmsub_vx = 571,
    rv_op_vwmaccu_vv = 572,
    rv_op_vwmaccu_vx = 573,
    rv_op_vwmacc_vv = 574,
    rv_op_vwmacc_vx = 575,
    rv_op_vwmaccsu_vv = 576,
    rv_op_vwmaccsu_vx = 577,
    rv_op_vwmaccus_vx = 578,
    rv_op_vmv_v_v = 579,
    rv_op_vmv_v_x = 580,
    rv_op_vmv_v_i = 581,
    rv_op_vmerge_vvm = 582,
    rv_op_vmerge_vxm = 583,
    rv_op_vmerge_vim = 584,
    rv_op_vsaddu_vv = 585,
    rv_op_vsaddu_vx = 586,
    rv_op_vsaddu_vi = 587,
    rv_op_vsadd_vv = 588,
    rv_op_vsadd_vx = 589,
    rv_op_vsadd_vi = 590,
    rv_op_vssubu_vv = 591,
    rv_op_vssubu_vx = 592,
    rv_op_vssub_vv = 593,
    rv_op_vssub_vx = 594,
    rv_op_vaadd_vv = 595,
    rv_op_vaadd_vx = 596,
    rv_op_vaaddu_vv = 597,
    rv_op_vaaddu_vx = 598,
    rv_op_vasub_vv = 599,
    rv_op_vasub_vx = 600,
    rv_op_vasubu_vv = 601,
    rv_op_vasubu_vx = 602,
    rv_op_vsmul_vv = 603,
    rv_op_vsmul_vx = 604,
    rv_op_vssrl_vv = 605,
    rv_op_vssrl_vx = 606,
    rv_op_vssrl_vi = 607,
    rv_op_vssra_vv = 608,
    rv_op_vssra_vx = 609,
    rv_op_vssra_vi = 610,
    rv_op_vnclipu_wv = 611,
    rv_op_vnclipu_wx = 612,
    rv_op_vnclipu_wi = 613,
    rv_op_vnclip_wv = 614,
    rv_op_vnclip_wx = 615,
    rv_op_vnclip_wi = 616,
    rv_op_vfadd_vv = 617,
    rv_op_vfadd_vf = 618,
    rv_op_vfsub_vv = 619,
    rv_op_vfsub_vf = 620,
    rv_op_vfrsub_vf = 621,
    rv_op_vfwadd_vv = 622,
    rv_op_vfwadd_vf = 623,
    rv_op_vfwadd_wv = 624,
    rv_op_vfwadd_wf = 625,
    rv_op_vfwsub_vv = 626,
    rv_op_vfwsub_vf = 627,
    rv_op_vfwsub_wv = 628,
    rv_op_vfwsub_wf = 629,
    rv_op_vfmul_vv = 630,
    rv_op_vfmul_vf = 631,
    rv_op_vfdiv_vv = 632,
    rv_op_vfdiv_vf = 633,
    rv_op_vfrdiv_vf = 634,
    rv_op_vfwmul_vv = 635,
    rv_op_vfwmul_vf = 636,
    rv_op_vfmacc_vv = 637,
    rv_op_vfmacc_vf = 638,
    rv_op_vfnmacc_vv = 639,
    rv_op_vfnmacc_vf = 640,
    rv_op_vfmsac_vv = 641,
    rv_op_vfmsac_vf = 642,
    rv_op_vfnmsac_vv = 643,
    rv_op_vfnmsac_vf = 644,
    rv_op_vfmadd_vv = 645,
    rv_op_vfmadd_vf = 646,
    rv_op_vfnmadd_vv = 647,
    rv_op_vfnmadd_vf = 648,
    rv_op_vfmsub_vv = 649,
    rv_op_vfmsub_vf = 650,
    rv_op_vfnmsub_vv = 651,
    rv_op_vfnmsub_vf = 652,
    rv_op_vfwmacc_vv = 653,
    rv_op_vfwmacc_vf = 654,
    rv_op_vfwnmacc_vv = 655,
    rv_op_vfwnmacc_vf = 656,
    rv_op_vfwmsac_vv = 657,
    rv_op_vfwmsac_vf = 658,
    rv_op_vfwnmsac_vv = 659,
    rv_op_vfwnmsac_vf = 660,
    rv_op_vfsqrt_v = 661,
    rv_op_vfrsqrt7_v = 662,
    rv_op_vfrec7_v = 663,
    rv_op_vfmin_vv = 664,
    rv_op_vfmin_vf = 665,
    rv_op_vfmax_vv = 666,
    rv_op_vfmax_vf = 667,
    rv_op_vfsgnj_vv = 668,
    rv_op_vfsgnj_vf = 669,
    rv_op_vfsgnjn_vv = 670,
    rv_op_vfsgnjn_vf = 671,
    rv_op_vfsgnjx_vv = 672,
    rv_op_vfsgnjx_vf = 673,
    rv_op_vfslide1up_vf = 674,
    rv_op_vfslide1down_vf = 675,
    rv_op_vmfeq_vv = 676,
    rv_op_vmfeq_vf = 677,
    rv_op_vmfne_vv = 678,
    rv_op_vmfne_vf = 679,
    rv_op_vmflt_vv = 680,
    rv_op_vmflt_vf = 681,
    rv_op_vmfle_vv = 682,
    rv_op_vmfle_vf = 683,
    rv_op_vmfgt_vf = 684,
    rv_op_vmfge_vf = 685,
    rv_op_vfclass_v = 686,
    rv_op_vfmerge_vfm = 687,
    rv_op_vfmv_v_f = 688,
    rv_op_vfcvt_xu_f_v = 689,
    rv_op_vfcvt_x_f_v = 690,
    rv_op_vfcvt_f_xu_v = 691,
    rv_op_vfcvt_f_x_v = 692,
    rv_op_vfcvt_rtz_xu_f_v = 693,
    rv_op_vfcvt_rtz_x_f_v = 694,
    rv_op_vfwcvt_xu_f_v = 695,
    rv_op_vfwcvt_x_f_v = 696,
    rv_op_vfwcvt_f_xu_v = 697,
    rv_op_vfwcvt_f_x_v = 698,
    rv_op_vfwcvt_f_f_v = 699,
    rv_op_vfwcvt_rtz_xu_f_v = 700,
    rv_op_vfwcvt_rtz_x_f_v = 701,
    rv_op_vfncvt_xu_f_w = 702,
    rv_op_vfncvt_x_f_w = 703,
    rv_op_vfncvt_f_xu_w = 704,
    rv_op_vfncvt_f_x_w = 705,
    rv_op_vfncvt_f_f_w = 706,
    rv_op_vfncvt_rod_f_f_w = 707,
    rv_op_vfncvt_rtz_xu_f_w = 708,
    rv_op_vfncvt_rtz_x_f_w = 709,
    rv_op_vredsum_vs = 710,
    rv_op_vredand_vs = 711,
    rv_op_vredor_vs = 712,
    rv_op_vredxor_vs = 713,
    rv_op_vredminu_vs = 714,
    rv_op_vredmin_vs = 715,
    rv_op_vredmaxu_vs = 716,
    rv_op_vredmax_vs = 717,
    rv_op_vwredsumu_vs = 718,
    rv_op_vwredsum_vs = 719,
    rv_op_vfredusum_vs = 720,
    rv_op_vfredosum_vs = 721,
    rv_op_vfredmin_vs = 722,
    rv_op_vfredmax_vs = 723,
    rv_op_vfwredusum_vs = 724,
    rv_op_vfwredosum_vs = 725,
    rv_op_vmand_mm = 726,
    rv_op_vmnand_mm = 727,
    rv_op_vmandn_mm = 728,
    rv_op_vmxor_mm = 729,
    rv_op_vmor_mm = 730,
    rv_op_vmnor_mm = 731,
    rv_op_vmorn_mm = 732,
    rv_op_vmxnor_mm = 733,
    rv_op_vcpop_m = 734,
    rv_op_vfirst_m = 735,
    rv_op_vmsbf_m = 736,
    rv_op_vmsif_m = 737,
    rv_op_vmsof_m = 738,
    rv_op_viota_m = 739,
    rv_op_vid_v = 740,
    rv_op_vmv_x_s = 741,
    rv_op_vmv_s_x = 742,
    rv_op_vfmv_f_s = 743,
    rv_op_vfmv_s_f = 744,
    rv_op_vslideup_vx = 745,
    rv_op_vslideup_vi = 746,
    rv_op_vslide1up_vx = 747,
    rv_op_vslidedown_vx = 748,
    rv_op_vslidedown_vi = 749,
    rv_op_vslide1down_vx = 750,
    rv_op_vrgather_vv = 751,
    rv_op_vrgatherei16_vv = 752,
    rv_op_vrgather_vx = 753,
    rv_op_vrgather_vi = 754,
    rv_op_vcompress_vm = 755,
    rv_op_vmv1r_v = 756,
    rv_op_vmv2r_v = 757,
    rv_op_vmv4r_v = 758,
    rv_op_vmv8r_v = 759,
    rv_op_vzext_vf2 = 760,
    rv_op_vzext_vf4 = 761,
    rv_op_vzext_vf8 = 762,
    rv_op_vsext_vf2 = 763,
    rv_op_vsext_vf4 = 764,
    rv_op_vsext_vf8 = 765,
    rv_op_vsetvli = 766,
    rv_op_vsetivli = 767,
    rv_op_vsetvl = 768,
    rv_op_c_zext_b = 769,
    rv_op_c_sext_b = 770,
    rv_op_c_zext_h = 771,
    rv_op_c_sext_h = 772,
    rv_op_c_zext_w = 773,
    rv_op_c_not = 774,
    rv_op_c_mul = 775,
    rv_op_c_lbu = 776,
    rv_op_c_lhu = 777,
    rv_op_c_lh = 778,
    rv_op_c_sb = 779,
    rv_op_c_sh = 780,
    rv_op_cm_push = 781,
    rv_op_cm_pop = 782,
    rv_op_cm_popret = 783,
    rv_op_cm_popretz = 784,
    rv_op_cm_mva01s = 785,
    rv_op_cm_mvsa01 = 786,
    rv_op_cm_jt = 787,
    rv_op_cm_jalt = 788,
    rv_op_czero_eqz = 789,
    rv_op_czero_nez = 790,
    rv_op_fcvt_bf16_s = 791,
    rv_op_fcvt_s_bf16 = 792,
    rv_op_vfncvtbf16_f_f_w = 793,
    rv_op_vfwcvtbf16_f_f_v = 794,
    rv_op_vfwmaccbf16_vv = 795,
    rv_op_vfwmaccbf16_vf = 796,
    rv_op_flh = 797,
    rv_op_fsh = 798,
    rv_op_fmv_h_x = 799,
    rv_op_fmv_x_h = 800,
    rv_op_fli_s = 801,
    rv_op_fli_d = 802,
    rv_op_fli_q = 803,
    rv_op_fli_h = 804,
    rv_op_fminm_s = 805,
    rv_op_fmaxm_s = 806,
    rv_op_fminm_d = 807,
    rv_op_fmaxm_d = 808,
    rv_op_fminm_q = 809,
    rv_op_fmaxm_q = 810,
    rv_op_fminm_h = 811,
    rv_op_fmaxm_h = 812,
    rv_op_fround_s = 813,
    rv_op_froundnx_s = 814,
    rv_op_fround_d = 815,
    rv_op_froundnx_d = 816,
    rv_op_fround_q = 817,
    rv_op_froundnx_q = 818,
    rv_op_fround_h = 819,
    rv_op_froundnx_h = 820,
    rv_op_fcvtmod_w_d = 821,
    rv_op_fmvh_x_d = 822,
    rv_op_fmvp_d_x = 823,
    rv_op_fmvh_x_q = 824,
    rv_op_fmvp_q_x = 825,
    rv_op_fleq_s = 826,
    rv_op_fltq_s = 827,
    rv_op_fleq_d = 828,
    rv_op_fltq_d = 829,
    rv_op_fleq_q = 830,
    rv_op_fltq_q = 831,
    rv_op_fleq_h = 832,
    rv_op_fltq_h = 833,
    rv_op_vaesdf_vv = 834,
    rv_op_vaesdf_vs = 835,
    rv_op_vaesdm_vv = 836,
    rv_op_vaesdm_vs = 837,
    rv_op_vaesef_vv = 838,
    rv_op_vaesef_vs = 839,
    rv_op_vaesem_vv = 840,
    rv_op_vaesem_vs = 841,
    rv_op_vaeskf1_vi = 842,
    rv_op_vaeskf2_vi = 843,
    rv_op_vaesz_vs = 844,
    rv_op_vandn_vv = 845,
    rv_op_vandn_vx = 846,
    rv_op_vbrev_v = 847,
    rv_op_vbrev8_v = 848,
    rv_op_vclmul_vv = 849,
    rv_op_vclmul_vx = 850,
    rv_op_vclmulh_vv = 851,
    rv_op_vclmulh_vx = 852,
    rv_op_vclz_v = 853,
    rv_op_vcpop_v = 854,
    rv_op_vctz_v = 855,
    rv_op_vghsh_vv = 856,
    rv_op_vgmul_vv = 857,
    rv_op_vrev8_v = 858,
    rv_op_vrol_vv = 859,
    rv_op_vrol_vx = 860,
    rv_op_vror_vv = 861,
    rv_op_vror_vx = 862,
    rv_op_vror_vi = 863,
    rv_op_vsha2ch_vv = 864,
    rv_op_vsha2cl_vv = 865,
    rv_op_vsha2ms_vv = 866,
    rv_op_vsm3c_vi = 867,
    rv_op_vsm3me_vv = 868,
    rv_op_vsm4k_vi = 869,
    rv_op_vsm4r_vv = 870,
    rv_op_vsm4r_vs = 871,
    rv_op_vwsll_vv = 872,
    rv_op_vwsll_vx = 873,
    rv_op_vwsll_vi = 874,
    rv_op_amocas_w = 875,
    rv_op_amocas_d = 876,
    rv_op_amocas_q = 877,
    rv_mop_r_0     = 878,
    rv_mop_r_1     = 879,
    rv_mop_r_2     = 880,
    rv_mop_r_3     = 881,
    rv_mop_r_4     = 882,
    rv_mop_r_5     = 883,
    rv_mop_r_6     = 884,
    rv_mop_r_7     = 885,
    rv_mop_r_8     = 886,
    rv_mop_r_9     = 887,
    rv_mop_r_10    = 888,
    rv_mop_r_11    = 889,
    rv_mop_r_12    = 890,
    rv_mop_r_13    = 891,
    rv_mop_r_14    = 892,
    rv_mop_r_15    = 893,
    rv_mop_r_16    = 894,
    rv_mop_r_17    = 895,
    rv_mop_r_18    = 896,
    rv_mop_r_19    = 897,
    rv_mop_r_20    = 898,
    rv_mop_r_21    = 899,
    rv_mop_r_22    = 900,
    rv_mop_r_23    = 901,
    rv_mop_r_24    = 902,
    rv_mop_r_25    = 903,
    rv_mop_r_26    = 904,
    rv_mop_r_27    = 905,
    rv_mop_r_28    = 906,
    rv_mop_r_29    = 907,
    rv_mop_r_30    = 908,
    rv_mop_r_31    = 909,
    rv_mop_rr_0    = 910,
    rv_mop_rr_1    = 911,
    rv_mop_rr_2    = 912,
    rv_mop_rr_3    = 913,
    rv_mop_rr_4    = 914,
    rv_mop_rr_5    = 915,
    rv_mop_rr_6    = 916,
    rv_mop_rr_7    = 917,
    rv_c_mop_1     = 918,
    rv_c_mop_3     = 919,
    rv_c_mop_5     = 920,
    rv_c_mop_7     = 921,
    rv_c_mop_9     = 922,
    rv_c_mop_11    = 923,
    rv_c_mop_13    = 924,
    rv_c_mop_15    = 925,
    rv_op_amoswap_b = 926,
    rv_op_amoadd_b  = 927,
    rv_op_amoxor_b  = 928,
    rv_op_amoor_b   = 929,
    rv_op_amoand_b  = 930,
    rv_op_amomin_b  = 931,
    rv_op_amomax_b  = 932,
    rv_op_amominu_b = 933,
    rv_op_amomaxu_b = 934,
    rv_op_amoswap_h = 935,
    rv_op_amoadd_h  = 936,
    rv_op_amoxor_h  = 937,
    rv_op_amoor_h   = 938,
    rv_op_amoand_h  = 939,
    rv_op_amomin_h  = 940,
    rv_op_amomax_h  = 941,
    rv_op_amominu_h = 942,
    rv_op_amomaxu_h = 943,
    rv_op_amocas_b  = 944,
    rv_op_amocas_h  = 945,
    rv_op_wrs_sto = 946,
    rv_op_wrs_nto = 947,
    rv_op_lpad = 948,
    rv_op_sspush = 949,
    rv_op_sspopchk = 950,
    rv_op_ssrdp = 951,
    rv_op_ssamoswap_w = 952,
    rv_op_ssamoswap_d = 953,
    rv_op_c_sspush = 954,
    rv_op_c_sspopchk = 955,
    rv_op_cbo_inval = 956,
    rv_op_cbo_clean = 957,
    rv_op_cbo_flush = 958,
    rv_op_cbo_zero = 959,
    rv_op_mnret = 960,
} rv_op;

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

static const char rv_vreg_name_sym[32][4] = {
    "v0",  "v1",  "v2",  "v3",  "v4",  "v5",  "v6",  "v7",
    "v8",  "v9",  "v10", "v11", "v12", "v13", "v14", "v15",
    "v16", "v17", "v18", "v19", "v20", "v21", "v22", "v23",
    "v24", "v25", "v26", "v27", "v28", "v29", "v30", "v31"
};

/* The FLI.[HSDQ] numeric constants (0.0 for symbolic constants).
 * The constants use the hex floating-point literal representation
 * that is printed when using the printf %a format specifier,
 * which matches the output that is generated by the disassembler.
 */
static const char rv_fli_name_const[32][9] =
{
    "0x1p+0", "min", "0x1p-16", "0x1p-15",
    "0x1p-8", "0x1p-7", "0x1p-4", "0x1p-3",
    "0x1p-2", "0x1.4p-2", "0x1.8p-2", "0x1.cp-2",
    "0x1p-1", "0x1.4p-1", "0x1.8p-1", "0x1.cp-1",
    "0x1p+0", "0x1.4p+0", "0x1.8p+0", "0x1.cp+0",
    "0x1p+1", "0x1.4p+1", "0x1.8p+1", "0x1p+2",
    "0x1p+3", "0x1p+4", "0x1p+7", "0x1p+8",
    "0x1p+15", "0x1p+16", "inf", "nan"
};

/* pseudo-instruction constraints */

static const rvc_constraint rvcc_jal_ra[] = { rvc_rd_eq_ra, rvc_end };
static const rvc_constraint rvcc_jalr_ra[] = { rvc_rd_eq_ra, rvc_imm_eq_zero,
                                               rvc_end };
static const rvc_constraint rvcc_nop[] = { rvc_rd_eq_x0, rvc_rs1_eq_x0,
                                           rvc_end };
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
static const rvc_constraint rvcc_j[] = { rvc_rd_eq_x0, rvc_end };
static const rvc_constraint rvcc_ret[] = { rvc_rs1_eq_ra, rvc_end };
static const rvc_constraint rvcc_jr[] = { rvc_rd_eq_x0, rvc_imm_eq_zero,
                                          rvc_end };
static const rvc_constraint rvcc_true[] = { rvc_end };

/* pseudo-instruction metadata */

static const rv_opcode_data rvi_opcode_data[];

static const rv_comp_data rvcp_jal[] = {
    { &rvi_opcode_data[rv_op_j], rvcc_j },
    { &rvi_opcode_data[rv_op_jal_ra], rvcc_jal_ra },
    { },
};

static const rv_comp_data rvcp_jalr[] = {
    { &rvi_opcode_data[rv_op_jr], rvcc_jr },
    { &rvi_opcode_data[rv_op_jalr_ra], rvcc_jalr_ra },
    { },
};

static const rv_comp_data rvcp_jr[] = {
    { &rvi_opcode_data[rv_op_ret], rvcc_ret },
    { },
};

static const rv_comp_data rvcp_beq[] = {
    { &rvi_opcode_data[rv_op_beqz], rvcc_beqz },
    { },
};

static const rv_comp_data rvcp_bne[] = {
    { &rvi_opcode_data[rv_op_bnez], rvcc_bnez },
    { },
};

static const rv_comp_data rvcp_blt[] = {
    { &rvi_opcode_data[rv_op_bltz], rvcc_bltz },
    { &rvi_opcode_data[rv_op_bgtz], rvcc_bgtz },
    { },
};

static const rv_comp_data rvcp_bge[] = {
    { &rvi_opcode_data[rv_op_blez], rvcc_blez },
    { &rvi_opcode_data[rv_op_bgez], rvcc_bgez },
    { },
};

static const rv_comp_data rvcp_addi[] = {
    { &rvi_opcode_data[rv_op_mv], rvcc_mv },
    { },
};

static const rv_comp_data rvcp_mv[] = {
    { &rvi_opcode_data[rv_op_nop], rvcc_nop },
    { },
};

static const rv_comp_data rvcp_sltiu[] = {
    { &rvi_opcode_data[rv_op_seqz], rvcc_seqz },
    { },
};

static const rv_comp_data rvcp_xori[] = {
    { &rvi_opcode_data[rv_op_not], rvcc_not },
    { },
};

static const rv_comp_data rvcp_sub[] = {
    { &rvi_opcode_data[rv_op_neg], rvcc_neg },
    { },
};

static const rv_comp_data rvcp_slt[] = {
    { &rvi_opcode_data[rv_op_sltz], rvcc_sltz },
    { &rvi_opcode_data[rv_op_sgtz], rvcc_sgtz },
    { },
};

static const rv_comp_data rvcp_sltu[] = {
    { &rvi_opcode_data[rv_op_snez], rvcc_snez },
    { },
};

static const rv_comp_data rvcp_addiw[] = {
    { &rvi_opcode_data[rv_op_sext_w], rvcc_sext_w },
    { },
};

static const rv_comp_data rvcp_subw[] = {
    { &rvi_opcode_data[rv_op_negw], rvcc_negw },
    { },
};

static const rv_comp_data rvcp_fsgnj_s[] = {
    { &rvi_opcode_data[rv_op_fmv_s], rvcc_fmv_s },
    { },
};

static const rv_comp_data rvcp_fsgnjn_s[] = {
    { &rvi_opcode_data[rv_op_fneg_s], rvcc_fneg_s },
    { },
};

static const rv_comp_data rvcp_fsgnjx_s[] = {
    { &rvi_opcode_data[rv_op_fabs_s], rvcc_fabs_s },
    { },
};

static const rv_comp_data rvcp_fsgnj_d[] = {
    { &rvi_opcode_data[rv_op_fmv_d], rvcc_fmv_d },
    { },
};

static const rv_comp_data rvcp_fsgnjn_d[] = {
    { &rvi_opcode_data[rv_op_fneg_d], rvcc_fneg_d },
    { },
};

static const rv_comp_data rvcp_fsgnjx_d[] = {
    { &rvi_opcode_data[rv_op_fabs_d], rvcc_fabs_d },
    { },
};

static const rv_comp_data rvcp_fsgnj_q[] = {
    { &rvi_opcode_data[rv_op_fmv_q], rvcc_fmv_q },
    { },
};

static const rv_comp_data rvcp_fsgnjn_q[] = {
    { &rvi_opcode_data[rv_op_fneg_q], rvcc_fneg_q },
    { },
};

static const rv_comp_data rvcp_fsgnjx_q[] = {
    { &rvi_opcode_data[rv_op_fabs_q], rvcc_fabs_q },
    { },
};

/* Convert compressed insns into normal insns via pseudo expansion. */
#define DECOMP(X)  &(const rv_comp_data){ &rvi_opcode_data[X], rvcc_true }

/* operand extractors */

static uint32_t operand_rd(rv_inst inst)
{
    return extract32(inst, 7, 5);
}

static uint32_t operand_rs1(rv_inst inst)
{
    return extract32(inst, 15, 5);
}

static uint32_t operand_rs2(rv_inst inst)
{
    return extract32(inst, 20, 5);
}

static uint32_t operand_rs3(rv_inst inst)
{
    return extract32(inst, 27, 5);
}

static uint32_t operand_aq(rv_inst inst)
{
    return extract32(inst, 26, 1);
}

static uint32_t operand_rl(rv_inst inst)
{
    return extract32(inst, 25, 1);
}

static uint32_t operand_pred(rv_inst inst)
{
    return extract32(inst, 24, 4);
}

static uint32_t operand_succ(rv_inst inst)
{
    return extract32(inst, 20, 4);
}

static uint32_t operand_rm(rv_inst inst)
{
    return extract32(inst, 12, 3);
}

static uint32_t operand_shamt5(rv_inst inst)
{
    return extract32(inst, 20, 5);
}

static uint32_t operand_shamt6(rv_inst inst)
{
    return extract32(inst, 20, 6);
}

static uint32_t operand_shamt7(rv_inst inst)
{
    return extract32(inst, 20, 7);
}

static uint32_t operand_crdq(rv_inst inst)
{
    return extract32(inst, 2, 3);
}

static uint32_t operand_crs1q(rv_inst inst)
{
    return extract32(inst, 7, 3);
}

static uint32_t operand_crs1rdq(rv_inst inst)
{
    return extract32(inst, 7, 3);
}

static uint32_t operand_crs2q(rv_inst inst)
{
    return extract32(inst, 2, 3);
}

static uint32_t calculate_xreg(uint32_t sreg)
{
    return sreg < 2 ? sreg + 8 : sreg + 16;
}

static uint32_t operand_sreg1(rv_inst inst)
{
    return calculate_xreg(extract32(inst, 7, 3));
}

static uint32_t operand_sreg2(rv_inst inst)
{
    return calculate_xreg(extract32(inst, 2, 3));
}

static uint32_t operand_crd(rv_inst inst)
{
    return extract32(inst, 7, 5);
}

static uint32_t operand_crs1(rv_inst inst)
{
    return extract32(inst, 7, 5);
}

static uint32_t operand_crs1rd(rv_inst inst)
{
    return extract32(inst, 7, 5);
}

static uint32_t operand_crs2(rv_inst inst)
{
    return extract32(inst, 2, 5);
}

static uint32_t operand_cimmsh5(rv_inst inst)
{
    return extract32(inst, 2, 5);
}

static uint32_t operand_csr12(rv_inst inst)
{
    return extract32(inst, 20, 12);
}

static int32_t operand_imm12(rv_inst inst)
{
    return sextract32(inst, 20, 12);
}

static int32_t operand_imm20(rv_inst inst)
{
    return sextract32(inst, 12, 20) << 12;
}

static int32_t operand_jimm20(rv_inst inst)
{
    return sextract32(inst, 31, 1) << 20 |
        extract32(inst, 21, 10) << 1 |
        extract32(inst, 20, 1) << 11 |
        extract32(inst, 12, 8) << 12;
}

static int32_t operand_simm12(rv_inst inst)
{
    return sextract32(inst, 25, 7) << 5 |
        extract32(inst, 7, 5);
}

static int32_t operand_sbimm12(rv_inst inst)
{
    return sextract32(inst, 31, 1) << 12 |
        extract32(inst, 25, 6) << 5 |
        extract32(inst, 8, 4) << 1 |
        extract32(inst, 7, 1) << 11;
}

static uint32_t operand_cimmshl6(rv_inst inst, rv_isa isa)
{
    int imm = extract32(inst, 12, 1) << 5 |
        extract32(inst, 2, 5);
    if (isa == rv128) {
        imm = imm ? imm : 64;
    }
    return imm;
}

static uint32_t operand_cimmshr6(rv_inst inst, rv_isa isa)
{
    int imm = extract32(inst, 12, 1) << 5 |
        extract32(inst, 2, 5);
    if (isa == rv128) {
        imm = imm | (imm & 32) << 1;
        imm = imm ? imm : 64;
    }
    return imm;
}

static int32_t operand_cimmi(rv_inst inst)
{
    return sextract32(inst, 12, 1) << 5 |
        extract32(inst, 2, 5);
}

static int32_t operand_cimmui(rv_inst inst)
{
    return sextract32(inst, 12, 1) << 17 |
        extract32(inst, 2, 5) << 12;
}

static uint32_t operand_cimmlwsp(rv_inst inst)
{
    return extract32(inst, 12, 1) << 5 |
        extract32(inst, 4, 3) << 2 |
        extract32(inst, 2, 2) << 6;
}

static uint32_t operand_cimmldsp(rv_inst inst)
{
    return extract32(inst, 12, 1) << 5 |
        extract32(inst, 5, 2) << 3 |
        extract32(inst, 2, 3) << 6;
}

static uint32_t operand_cimmlqsp(rv_inst inst)
{
    return extract32(inst, 12, 1) << 5 |
        extract32(inst, 6, 1) << 4 |
        extract32(inst, 2, 4) << 6;
}

static int32_t operand_cimm16sp(rv_inst inst)
{
    return sextract32(inst, 12, 1) << 9 |
        extract32(inst, 6, 1) << 4 |
        extract32(inst, 5, 1) << 6 |
        extract32(inst, 3, 2) << 7 |
        extract32(inst, 2, 1) << 5;
}

static int32_t operand_cimmj(rv_inst inst)
{
    return sextract32(inst, 12, 1) << 11 |
        extract32(inst, 11, 1) << 4 |
        extract32(inst, 9, 2) << 8 |
        extract32(inst, 8, 1) << 10 |
        extract32(inst, 7, 1) << 6 |
        extract32(inst, 6, 1) << 7 |
        extract32(inst, 3, 3) << 1 |
        extract32(inst, 2, 1) << 5;
}

static int32_t operand_cimmb(rv_inst inst)
{
    return sextract32(inst, 12, 1) << 8 |
        extract32(inst, 10, 2) << 3 |
        extract32(inst, 5, 2) << 6 |
        extract32(inst, 3, 2) << 1 |
        extract32(inst, 2, 1) << 5;
}

static uint32_t operand_cimmswsp(rv_inst inst)
{
    return extract32(inst, 9, 4) << 2 |
        extract32(inst, 7, 2) << 6;
}

static uint32_t operand_cimmsdsp(rv_inst inst)
{
    return extract32(inst, 10, 3) << 3 |
        extract32(inst, 7, 3) << 6;
}

static uint32_t operand_cimmsqsp(rv_inst inst)
{
    return extract32(inst, 11, 2) << 4 |
        extract32(inst, 7, 4) << 6;
}

static uint32_t operand_cimm4spn(rv_inst inst)
{
    return extract32(inst, 11, 2) << 4 |
        extract32(inst, 7, 4) << 6 |
        extract32(inst, 6, 1) << 2 |
        extract32(inst, 5, 1) << 3;
}

static uint32_t operand_cimmw(rv_inst inst)
{
    return extract32(inst, 10, 3) << 3 |
        extract32(inst, 6, 1) << 2 |
        extract32(inst, 5, 1) << 6;
}

static uint32_t operand_cimmd(rv_inst inst)
{
    return extract32(inst, 10, 3) << 3 |
        extract32(inst, 5, 2) << 6;
}

static uint32_t operand_cimmq(rv_inst inst)
{
    return extract32(inst, 11, 2) << 4 |
        extract32(inst, 10, 1) << 8 |
        extract32(inst, 5, 2) << 6;
}

static int32_t operand_vimm(rv_inst inst)
{
    return sextract32(inst, 15, 5);
}

static uint32_t operand_vuimm(rv_inst inst)
{
    return extract32(inst, 15, 5);
}

static uint32_t operand_vzimm11(rv_inst inst)
{
    return extract32(inst, 20, 11);
}

static uint32_t operand_vzimm10(rv_inst inst)
{
    return extract32(inst, 20, 10);
}

static uint32_t operand_vzimm6(rv_inst inst)
{
    return extract32(inst, 26, 1) << 5 |
        extract32(inst, 15, 5);
}

static uint32_t operand_bs(rv_inst inst)
{
    return extract32(inst, 30, 2);
}

static uint32_t operand_rnum(rv_inst inst)
{
    return extract32(inst, 20, 4);
}

static uint32_t operand_vm(rv_inst inst)
{
    return extract32(inst, 25, 1);
}

static uint32_t operand_uimm_c_lb(rv_inst inst)
{
    return extract32(inst, 5, 1) << 1 |
        extract32(inst, 6, 1);
}

static uint32_t operand_uimm_c_lh(rv_inst inst)
{
    return extract32(inst, 5, 1) << 1;
}

static uint32_t operand_zcmp_spimm(rv_inst inst)
{
    return extract32(inst, 2, 2) << 4;
}

static uint32_t operand_zcmp_rlist(rv_inst inst)
{
    return extract32(inst, 4, 4);
}

static uint32_t operand_imm6(rv_inst inst)
{
    return extract32(inst, 20, 6);
}

static uint32_t operand_imm2(rv_inst inst)
{
    return extract32(inst, 25, 2);
}

static uint32_t operand_immh(rv_inst inst)
{
    return extract32(inst, 26, 6);
}

static uint32_t operand_imml(rv_inst inst)
{
    return extract32(inst, 20, 6);
}

static uint32_t calculate_stack_adj(rv_isa isa, uint32_t rlist, uint32_t spimm)
{
    int xlen_bytes_log2 = isa == rv64 ? 3 : 2;
    int regs = rlist == 15 ? 13 : rlist - 3;
    uint32_t stack_adj_base = ROUND_UP(regs << xlen_bytes_log2, 16);
    return stack_adj_base + spimm;
}

static uint32_t operand_zcmp_stack_adj(rv_inst inst, rv_isa isa)
{
    return calculate_stack_adj(isa, operand_zcmp_rlist(inst),
                               operand_zcmp_spimm(inst));
}

static uint32_t operand_tbl_index(rv_inst inst)
{
    return extract32(inst, 2, 8);
}

static uint32_t operand_lpl(rv_inst inst)
{
    return extract32(inst, 12, 20);
}

/* instruction metadata */

static const rv_opcode_data rvi_opcode_data[] = {
    { "illegal", rv_codec_none, rv_fmt_none },
    { "lui", rv_codec_u, rv_fmt_rd_uimm },
    { "auipc", rv_codec_u, rv_fmt_rd_uoffset },
    { "jal", rv_codec_uj, rv_fmt_rd_offset, rvcp_jal },
    { "jalr", rv_codec_i, rv_fmt_rd_rs1_offset, rvcp_jalr },
    { "beq", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_beq },
    { "bne", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_bne },
    { "blt", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_blt },
    { "bge", rv_codec_sb, rv_fmt_rs1_rs2_offset, rvcp_bge },
    { "bltu", rv_codec_sb, rv_fmt_rs1_rs2_offset },
    { "bgeu", rv_codec_sb, rv_fmt_rs1_rs2_offset },
    { "lb", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "lh", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "lw", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "lbu", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "lhu", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "sb", rv_codec_s, rv_fmt_rs2_offset_rs1 },
    { "sh", rv_codec_s, rv_fmt_rs2_offset_rs1 },
    { "sw", rv_codec_s, rv_fmt_rs2_offset_rs1 },
    { "addi", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_addi },
    { "slti", rv_codec_i, rv_fmt_rd_rs1_imm },
    { "sltiu", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_sltiu },
    { "xori", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_xori },
    { "ori", rv_codec_i, rv_fmt_rd_rs1_imm },
    { "andi", rv_codec_i, rv_fmt_rd_rs1_imm },
    { "slli", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "srli", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "srai", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "add", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sub", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_sub },
    { "sll", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "slt", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_slt },
    { "sltu", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_sltu },
    { "xor", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "srl", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sra", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "or", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "and", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "fence", rv_codec_r_f, rv_fmt_pred_succ },
    { "fence.i", rv_codec_none, rv_fmt_none },
    { "lwu", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "ld", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "sd", rv_codec_s, rv_fmt_rs2_offset_rs1 },
    { "addiw", rv_codec_i, rv_fmt_rd_rs1_imm, rvcp_addiw },
    { "slliw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm },
    { "srliw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm },
    { "sraiw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm },
    { "addw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "subw", rv_codec_r, rv_fmt_rd_rs1_rs2, rvcp_subw },
    { "sllw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "srlw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sraw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "ldu", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "lq", rv_codec_i, rv_fmt_rd_offset_rs1 },
    { "sq", rv_codec_s, rv_fmt_rs2_offset_rs1 },
    { "addid", rv_codec_i, rv_fmt_rd_rs1_imm },
    { "sllid", rv_codec_i_sh6, rv_fmt_rd_rs1_imm },
    { "srlid", rv_codec_i_sh6, rv_fmt_rd_rs1_imm },
    { "sraid", rv_codec_i_sh6, rv_fmt_rd_rs1_imm },
    { "addd", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "subd", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "slld", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "srld", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "srad", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mul", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mulh", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mulhsu", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mulhu", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "div", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "divu", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "rem", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "remu", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mulw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "divw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "divuw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "remw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "remuw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "muld", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "divd", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "divud", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "remd", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "remud", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "lr.w", rv_codec_r_l, rv_fmt_aqrl_rd_rs1 },
    { "sc.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoswap.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoadd.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoxor.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoor.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoand.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomin.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomax.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amominu.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomaxu.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "lr.d", rv_codec_r_l, rv_fmt_aqrl_rd_rs1 },
    { "sc.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoswap.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoadd.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoxor.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoor.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoand.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomin.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomax.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amominu.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomaxu.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "lr.q", rv_codec_r_l, rv_fmt_aqrl_rd_rs1 },
    { "sc.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoswap.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoadd.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoxor.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoor.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoand.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomin.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomax.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amominu.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomaxu.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "ecall", rv_codec_none, rv_fmt_none },
    { "ebreak", rv_codec_none, rv_fmt_none },
    { "uret", rv_codec_none, rv_fmt_none },
    { "sret", rv_codec_none, rv_fmt_none },
    { "hret", rv_codec_none, rv_fmt_none },
    { "mret", rv_codec_none, rv_fmt_none },
    { "dret", rv_codec_none, rv_fmt_none },
    { "sfence.vm", rv_codec_r, rv_fmt_rs1 },
    { "sfence.vma", rv_codec_r, rv_fmt_rs1_rs2 },
    { "wfi", rv_codec_none, rv_fmt_none },
    { "csrrw", rv_codec_i_csr, rv_fmt_rd_csr_rs1 },
    { "csrrs", rv_codec_i_csr, rv_fmt_rd_csr_rs1 },
    { "csrrc", rv_codec_i_csr, rv_fmt_rd_csr_rs1 },
    { "csrrwi", rv_codec_i_csr, rv_fmt_rd_csr_zimm },
    { "csrrsi", rv_codec_i_csr, rv_fmt_rd_csr_zimm },
    { "csrrci", rv_codec_i_csr, rv_fmt_rd_csr_zimm },
    { "flw", rv_codec_i, rv_fmt_frd_offset_rs1 },
    { "fsw", rv_codec_s, rv_fmt_frs2_offset_rs1 },
    { "fmadd.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fmsub.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fnmsub.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fnmadd.s", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fadd.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fsub.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fmul.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fdiv.s", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fsgnj.s", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnj_s },
    { "fsgnjn.s", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjn_s },
    { "fsgnjx.s", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjx_s },
    { "fmin.s", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmax.s", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fsqrt.s", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fle.s", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "flt.s", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "feq.s", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fcvt.w.s", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.wu.s", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.s.w", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fcvt.s.wu", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fmv.x.s", rv_codec_r, rv_fmt_rd_frs1 },
    { "fclass.s", rv_codec_r, rv_fmt_rd_frs1 },
    { "fmv.s.x", rv_codec_r, rv_fmt_frd_rs1 },
    { "fcvt.l.s", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.lu.s", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.s.l", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fcvt.s.lu", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fld", rv_codec_i, rv_fmt_frd_offset_rs1 },
    { "fsd", rv_codec_s, rv_fmt_frs2_offset_rs1 },
    { "fmadd.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fmsub.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fnmsub.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fnmadd.d", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fadd.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fsub.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fmul.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fdiv.d", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fsgnj.d", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnj_d },
    { "fsgnjn.d", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjn_d },
    { "fsgnjx.d", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjx_d },
    { "fmin.d", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmax.d", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fcvt.s.d", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fcvt.d.s", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fsqrt.d", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fle.d", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "flt.d", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "feq.d", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fcvt.w.d", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.wu.d", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.d.w", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fcvt.d.wu", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fclass.d", rv_codec_r, rv_fmt_rd_frs1 },
    { "fcvt.l.d", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.lu.d", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fmv.x.d", rv_codec_r, rv_fmt_rd_frs1 },
    { "fcvt.d.l", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fcvt.d.lu", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fmv.d.x", rv_codec_r, rv_fmt_frd_rs1 },
    { "flq", rv_codec_i, rv_fmt_frd_offset_rs1 },
    { "fsq", rv_codec_s, rv_fmt_frs2_offset_rs1 },
    { "fmadd.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fmsub.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fnmsub.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fnmadd.q", rv_codec_r4_m, rv_fmt_rm_frd_frs1_frs2_frs3 },
    { "fadd.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fsub.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fmul.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fdiv.q", rv_codec_r_m, rv_fmt_rm_frd_frs1_frs2 },
    { "fsgnj.q", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnj_q },
    { "fsgnjn.q", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjn_q },
    { "fsgnjx.q", rv_codec_r, rv_fmt_frd_frs1_frs2, rvcp_fsgnjx_q },
    { "fmin.q", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmax.q", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fcvt.s.q", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fcvt.q.s", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fcvt.d.q", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fcvt.q.d", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fsqrt.q", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fle.q", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "flt.q", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "feq.q", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fcvt.w.q", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.wu.q", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.q.w", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fcvt.q.wu", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fclass.q", rv_codec_r, rv_fmt_rd_frs1 },
    { "fcvt.l.q", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.lu.q", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fcvt.q.l", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fcvt.q.lu", rv_codec_r_m, rv_fmt_rm_frd_rs1 },
    { "fmv.x.q", rv_codec_r, rv_fmt_rd_frs1 },
    { "fmv.q.x", rv_codec_r, rv_fmt_frd_rs1 },
    { "c.addi4spn", rv_codec_ciw_4spn, NULL, DECOMP(rv_op_addi) },
    { "c.fld", rv_codec_cl_ld, NULL, DECOMP(rv_op_fld) },
    { "c.lw", rv_codec_cl_lw, NULL, DECOMP(rv_op_lw) },
    { "c.flw", rv_codec_cl_lw, NULL, DECOMP(rv_op_flw) },
    { "c.fsd", rv_codec_cs_sd, NULL, DECOMP(rv_op_fsd) },
    { "c.sw", rv_codec_cs_sw, NULL, DECOMP(rv_op_sw) },
    { "c.fsw", rv_codec_cs_sw, NULL, DECOMP(rv_op_fsw) },
    { },
    { "c.addi", rv_codec_ci, NULL, DECOMP(rv_op_addi) },
    { "c.jal", rv_codec_cj_jal, NULL, DECOMP(rv_op_jal) },
    { "c.li", rv_codec_ci_li, NULL, DECOMP(rv_op_addi) },
    { "c.addi16sp", rv_codec_ci_16sp, NULL, DECOMP(rv_op_addi) },
    { "c.lui", rv_codec_ci_lui, NULL, DECOMP(rv_op_lui) },
    { "c.srli", rv_codec_cb_sh6, NULL, DECOMP(rv_op_srli) },
    { "c.srai", rv_codec_cb_sh6, NULL, DECOMP(rv_op_srai) },
    { "c.andi", rv_codec_cb_imm, NULL, DECOMP(rv_op_andi) },
    { "c.sub", rv_codec_cs, NULL, DECOMP(rv_op_sub) },
    { "c.xor", rv_codec_cs, NULL, DECOMP(rv_op_xor) },
    { "c.or", rv_codec_cs, NULL, DECOMP(rv_op_or) },
    { "c.and", rv_codec_cs, NULL, DECOMP(rv_op_and) },
    { "c.subw", rv_codec_cs, NULL, DECOMP(rv_op_subw) },
    { "c.addw", rv_codec_cs, NULL, DECOMP(rv_op_addw) },
    { "c.j", rv_codec_cj, NULL, DECOMP(rv_op_j) },
    { "c.beqz", rv_codec_cb, NULL, DECOMP(rv_op_beqz) },
    { "c.bnez", rv_codec_cb, NULL, DECOMP(rv_op_bnez) },
    { "c.slli", rv_codec_ci_sh6, NULL, DECOMP(rv_op_slli) },
    { "c.fldsp", rv_codec_ci_ldsp, NULL, DECOMP(rv_op_fld) },
    { "c.lwsp", rv_codec_ci_lwsp, NULL, DECOMP(rv_op_lw) },
    { "c.flwsp", rv_codec_ci_lwsp, NULL, DECOMP(rv_op_flw) },
    { "c.jr", rv_codec_cr_jr, NULL, DECOMP(rv_op_jr) },
    { "c.mv", rv_codec_cr_mv, NULL, DECOMP(rv_op_mv) },
    { "c.ebreak", rv_codec_ci_none, NULL, DECOMP(rv_op_ebreak) },
    { "c.jalr", rv_codec_cr_jalr, NULL, DECOMP(rv_op_jalr) },
    { "c.add", rv_codec_cr, NULL, DECOMP(rv_op_add) },
    { "c.fsdsp", rv_codec_css_sdsp, NULL, DECOMP(rv_op_fsd) },
    { "c.swsp", rv_codec_css_swsp, NULL, DECOMP(rv_op_sw) },
    { "c.fswsp", rv_codec_css_swsp, NULL, DECOMP(rv_op_fsw) },
    { "c.ld", rv_codec_cl_ld, NULL, DECOMP(rv_op_ld) },
    { "c.sd", rv_codec_cs_sd, NULL, DECOMP(rv_op_sd) },
    { "c.addiw", rv_codec_ci, NULL, DECOMP(rv_op_addiw) },
    { "c.ldsp", rv_codec_ci_ldsp, NULL, DECOMP(rv_op_ld) },
    { "c.sdsp", rv_codec_css_sdsp, NULL, DECOMP(rv_op_sd) },
    { "c.lq", rv_codec_cl_lq, NULL, DECOMP(rv_op_lq) },
    { "c.sq", rv_codec_cs_sq, NULL, DECOMP(rv_op_sq) },
    { "c.lqsp", rv_codec_ci_lqsp, NULL, DECOMP(rv_op_lq) },
    { "c.sqsp", rv_codec_css_sqsp, NULL, DECOMP(rv_op_sq) },
    { "nop", rv_codec_illegal, rv_fmt_none },
    { "mv", rv_codec_illegal, rv_fmt_rd_rs1, rvcp_mv },
    { "not", rv_codec_illegal, rv_fmt_rd_rs1 },
    { "neg", rv_codec_illegal, rv_fmt_rd_rs2 },
    { "negw", rv_codec_illegal, rv_fmt_rd_rs2 },
    { "sext.w", rv_codec_illegal, rv_fmt_rd_rs1 },
    { "seqz", rv_codec_illegal, rv_fmt_rd_rs1 },
    { "snez", rv_codec_illegal, rv_fmt_rd_rs2 },
    { "sltz", rv_codec_illegal, rv_fmt_rd_rs1 },
    { "sgtz", rv_codec_illegal, rv_fmt_rd_rs2 },
    { "fmv.s", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fabs.s", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fneg.s", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fmv.d", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fabs.d", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fneg.d", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fmv.q", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fabs.q", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "fneg.q", rv_codec_illegal, rv_fmt_frd_frs1 },
    { "beqz", rv_codec_illegal, rv_fmt_rs1_offset },
    { "bnez", rv_codec_illegal, rv_fmt_rs1_offset },
    { "blez", rv_codec_illegal, rv_fmt_rs2_offset },
    { "bgez", rv_codec_illegal, rv_fmt_rs1_offset },
    { "bltz", rv_codec_illegal, rv_fmt_rs1_offset },
    { "bgtz", rv_codec_illegal, rv_fmt_rs2_offset },
    { "jal", rv_codec_illegal, rv_fmt_offset }, /* rv_op_jal_ra */
    { "jalr", rv_codec_illegal, rv_fmt_rs1 }, /* rv_op_jalr_ra */
    { },
    { },
    { "j", rv_codec_illegal, rv_fmt_offset },
    { "ret", rv_codec_illegal, rv_fmt_none },
    { "jr", rv_codec_illegal, rv_fmt_rs1, rvcp_jr },
    { "rdcycle", rv_codec_i_csr, rv_fmt_rd },
    { "rdtime", rv_codec_i_csr, rv_fmt_rd },
    { "rdinstret", rv_codec_i_csr, rv_fmt_rd },
    { "rdcycleh", rv_codec_i_csr, rv_fmt_rd },
    { "rdtimeh", rv_codec_i_csr, rv_fmt_rd },
    { "rdinstreth", rv_codec_i_csr, rv_fmt_rd },
    { "frcsr", rv_codec_i_csr, rv_fmt_rd },
    { "frrm", rv_codec_i_csr, rv_fmt_rd },
    { "frflags", rv_codec_i_csr, rv_fmt_rd },
    { "fscsr", rv_codec_i_csr, rv_fmt_rd_rs1 },
    { "fsrm", rv_codec_i_csr, rv_fmt_rd_rs1 },
    { "fsflags", rv_codec_i_csr, rv_fmt_rd_rs1 },
    { "fsrmi", rv_codec_i_csr, rv_fmt_rd_zimm },
    { "fsflagsi", rv_codec_i_csr, rv_fmt_rd_zimm },
    { "bseti", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "bclri", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "binvi", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "bexti", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "rori", rv_codec_i_sh7, rv_fmt_rd_rs1_imm },
    { "clz", rv_codec_r, rv_fmt_rd_rs1 },
    { "ctz", rv_codec_r, rv_fmt_rd_rs1 },
    { "cpop", rv_codec_r, rv_fmt_rd_rs1 },
    { "sext.h", rv_codec_r, rv_fmt_rd_rs1 },
    { "sext.b", rv_codec_r, rv_fmt_rd_rs1 },
    { "xnor", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "orn", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "andn", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "rol", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "ror", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sh1add", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sh2add", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sh3add", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sh1add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sh2add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sh3add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "clmul", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "clmulr", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "clmulh", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "min", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "minu", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "max", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "maxu", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "clzw", rv_codec_r, rv_fmt_rd_rs1 },
    { "ctzw", rv_codec_r, rv_fmt_rd_rs1 },
    { "cpopw", rv_codec_r, rv_fmt_rd_rs1 },
    { "slli.uw", rv_codec_i_sh6, rv_fmt_rd_rs1_imm },
    { "add.uw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "rolw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "rorw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "rev8", rv_codec_r, rv_fmt_rd_rs1 },
    { "zext.h", rv_codec_r, rv_fmt_rd_rs1 },
    { "roriw", rv_codec_i_sh5, rv_fmt_rd_rs1_imm },
    { "orc.b", rv_codec_r, rv_fmt_rd_rs1 },
    { "bset", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "bclr", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "binv", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "bext", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "aes32esmi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs },
    { "aes32esi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs },
    { "aes32dsmi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs },
    { "aes32dsi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs },
    { "aes64ks1i", rv_codec_k_rnum, rv_fmt_rd_rs1_rnum },
    { "aes64ks2", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "aes64im", rv_codec_r, rv_fmt_rd_rs1 },
    { "aes64esm", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "aes64es", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "aes64dsm", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "aes64ds", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha256sig0", rv_codec_r, rv_fmt_rd_rs1 },
    { "sha256sig1", rv_codec_r, rv_fmt_rd_rs1 },
    { "sha256sum0", rv_codec_r, rv_fmt_rd_rs1 },
    { "sha256sum1", rv_codec_r, rv_fmt_rd_rs1 },
    { "sha512sig0", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sig1", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sum0", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sum1", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sum0r", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sum1r", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sig0l", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sig0h", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sig1l", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sha512sig1h", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "sm3p0", rv_codec_r, rv_fmt_rd_rs1 },
    { "sm3p1", rv_codec_r, rv_fmt_rd_rs1 },
    { "sm4ed", rv_codec_k_bs, rv_fmt_rs1_rs2_bs },
    { "sm4ks", rv_codec_k_bs, rv_fmt_rs1_rs2_bs },
    { "brev8", rv_codec_r, rv_fmt_rd_rs1 },
    { "pack", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "packh", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "packw", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "unzip", rv_codec_r, rv_fmt_rd_rs1 },
    { "zip", rv_codec_r, rv_fmt_rd_rs1 },
    { "xperm4", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "xperm8", rv_codec_r, rv_fmt_rd_rs1 },
    { "vle8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vle16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vle32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vle64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vse8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vse16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vse32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vse64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vlm.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vsm.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vlse8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vlse16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vlse32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vlse64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vsse8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vsse16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vsse32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vsse64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm },
    { "vluxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vluxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vluxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vluxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vloxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vloxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vloxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vloxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsuxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsuxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsuxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsuxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsoxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsoxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsoxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vsoxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm },
    { "vle8ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vle16ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vle32ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vle64ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl1re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl1re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl1re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl1re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl2re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl2re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl2re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl2re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl4re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl4re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl4re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl4re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl8re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl8re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl8re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vl8re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vs1r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vs2r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vs4r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vs8r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm },
    { "vadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vadd.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vrsub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vrsub.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vwaddu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwaddu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwsubu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwsubu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwsub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwaddu.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwaddu.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwadd.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwadd.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwsubu.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwsubu.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwsub.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwsub.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vadc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl },
    { "vadc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl },
    { "vadc.vim", rv_codec_v_i, rv_fmt_vd_vs2_imm_vl },
    { "vmadc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl },
    { "vmadc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl },
    { "vmadc.vim", rv_codec_v_i, rv_fmt_vd_vs2_imm_vl },
    { "vsbc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl },
    { "vsbc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl },
    { "vmsbc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl },
    { "vmsbc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl },
    { "vand.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vand.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vand.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vor.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vor.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vor.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vxor.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vxor.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vxor.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vsll.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsll.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vsll.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vsrl.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsrl.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vsrl.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vsra.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsra.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vsra.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vnsrl.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vnsrl.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vnsrl.wi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vnsra.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vnsra.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vnsra.wi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vmseq.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmseq.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmseq.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vmsne.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmsne.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmsne.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vmsltu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmsltu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmslt.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmslt.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmsleu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmsleu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmsleu.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vmsle.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmsle.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmsle.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vmsgtu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmsgtu.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vmsgt.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmsgt.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vminu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vminu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmin.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmin.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmaxu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmaxu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmax.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmax.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmulh.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmulh.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmulhu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmulhu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmulhsu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmulhsu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vdivu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vdivu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vdiv.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vdiv.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vremu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vremu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vrem.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vrem.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwmulu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwmulu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwmulsu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwmulsu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vmacc.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vnmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vnmsac.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vmadd.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vmadd.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vnmsub.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vnmsub.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vwmaccu.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vwmaccu.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vwmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vwmacc.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vwmaccsu.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vwmaccsu.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vwmaccus.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm },
    { "vmv.v.v", rv_codec_v_r, rv_fmt_vd_vs1 },
    { "vmv.v.x", rv_codec_v_r, rv_fmt_vd_rs1 },
    { "vmv.v.i", rv_codec_v_i, rv_fmt_vd_imm },
    { "vmerge.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl },
    { "vmerge.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl },
    { "vmerge.vim", rv_codec_v_i, rv_fmt_vd_vs2_imm_vl },
    { "vsaddu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsaddu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vsaddu.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vsadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vsadd.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm },
    { "vssubu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vssubu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vssub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vssub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vaadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vaadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vaaddu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vaaddu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vasub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vasub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vasubu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vasubu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vsmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vsmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vssrl.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vssrl.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vssrl.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vssra.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vssra.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vssra.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vnclipu.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vnclipu.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vnclipu.wi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vnclip.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vnclip.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vnclip.wi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vfadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfadd.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfsub.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfrsub.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfwadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwadd.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfwadd.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwadd.wf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfwsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwsub.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfwsub.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwsub.wf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfmul.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfdiv.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfdiv.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfrdiv.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfwmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwmul.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfnmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfnmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfnmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfnmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfmadd.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfmadd.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfnmadd.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfnmadd.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfmsub.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfmsub.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfnmsub.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfnmsub.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfwmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfwmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfwnmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfwnmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfwmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfwmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfwnmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfwnmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "vfsqrt.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vfrsqrt7.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vfrec7.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vfmin.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfmin.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfmax.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfmax.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfsgnj.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfsgnj.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfsgnjn.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfsgnjn.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfsgnjx.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfsgnjx.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfslide1up.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfslide1down.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vmfeq.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmfeq.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vmfne.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmfne.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vmflt.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmflt.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vmfle.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmfle.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vmfgt.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vmfge.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm },
    { "vfclass.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfmerge.vfm", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vl },
    { "vfmv.v.f", rv_codec_v_r, rv_fmt_vd_fs1 },
    { "vfcvt.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfcvt.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfcvt.f.xu.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfcvt.f.x.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfcvt.rtz.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfcvt.rtz.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.f.xu.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.f.x.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.f.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.rtz.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvt.rtz.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.xu.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.x.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.f.xu.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.f.x.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.f.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.rod.f.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.rtz.xu.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfncvt.rtz.x.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vredsum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredand.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredor.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredxor.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredminu.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredmin.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredmaxu.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vredmax.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwredsumu.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwredsum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfredusum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfredosum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfredmin.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfredmax.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwredusum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vfwredosum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmand.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmnand.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmandn.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmxor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmnor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmorn.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vmxnor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vcpop.m", rv_codec_v_r, rv_fmt_rd_vs2_vm },
    { "vfirst.m", rv_codec_v_r, rv_fmt_rd_vs2_vm },
    { "vmsbf.m", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vmsif.m", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vmsof.m", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "viota.m", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vid.v", rv_codec_v_r, rv_fmt_vd_vm },
    { "vmv.x.s", rv_codec_v_r, rv_fmt_rd_vs2 },
    { "vmv.s.x", rv_codec_v_r, rv_fmt_vd_rs1 },
    { "vfmv.f.s", rv_codec_v_r, rv_fmt_fd_vs2 },
    { "vfmv.s.f", rv_codec_v_r, rv_fmt_vd_fs1 },
    { "vslideup.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vslideup.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vslide1up.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vslidedown.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vslidedown.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vslide1down.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vrgather.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vrgatherei16.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vrgather.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vrgather.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "vcompress.vm", rv_codec_v_r, rv_fmt_vd_vs2_vs1 },
    { "vmv1r.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vmv2r.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vmv4r.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vmv8r.v", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vzext.vf2", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vzext.vf4", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vzext.vf8", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vsext.vf2", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vsext.vf4", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vsext.vf8", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vsetvli", rv_codec_vsetvli, rv_fmt_vsetvli },
    { "vsetivli", rv_codec_vsetivli, rv_fmt_vsetivli },
    { "vsetvl", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "c.zext.b", rv_codec_zcb_ext, rv_fmt_rd },
    { "c.sext.b", rv_codec_zcb_ext, rv_fmt_rd },
    { "c.zext.h", rv_codec_zcb_ext, rv_fmt_rd },
    { "c.sext.h", rv_codec_zcb_ext, rv_fmt_rd },
    { "c.zext.w", rv_codec_zcb_ext, rv_fmt_rd },
    { "c.not", rv_codec_zcb_ext, rv_fmt_rd },
    { "c.mul", rv_codec_zcb_mul, rv_fmt_rd_rs2 },
    { "c.lbu", rv_codec_zcb_lb, rv_fmt_rs1_rs2_zce_ldst },
    { "c.lhu", rv_codec_zcb_lh, rv_fmt_rs1_rs2_zce_ldst },
    { "c.lh", rv_codec_zcb_lh, rv_fmt_rs1_rs2_zce_ldst },
    { "c.sb", rv_codec_zcb_lb, rv_fmt_rs1_rs2_zce_ldst },
    { "c.sh", rv_codec_zcb_lh, rv_fmt_rs1_rs2_zce_ldst },
    { "cm.push", rv_codec_zcmp_cm_pushpop, rv_fmt_push_rlist },
    { "cm.pop", rv_codec_zcmp_cm_pushpop, rv_fmt_pop_rlist },
    { "cm.popret", rv_codec_zcmp_cm_pushpop, rv_fmt_pop_rlist },
    { "cm.popretz", rv_codec_zcmp_cm_pushpop, rv_fmt_pop_rlist },
    { "cm.mva01s", rv_codec_zcmp_cm_mv, rv_fmt_rd_rs2 },
    { "cm.mvsa01", rv_codec_zcmp_cm_mv, rv_fmt_rd_rs2 },
    { "cm.jt", rv_codec_zcmt_jt, rv_fmt_zcmt_index },
    { "cm.jalt", rv_codec_zcmt_jt, rv_fmt_zcmt_index },
    { "czero.eqz", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "czero.nez", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "fcvt.bf16.s", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fcvt.s.bf16", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "vfncvtbf16.f.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwcvtbf16.f.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vfwmaccbf16.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm },
    { "vfwmaccbf16.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm },
    { "flh", rv_codec_i, rv_fmt_frd_offset_rs1 },
    { "fsh", rv_codec_s, rv_fmt_frs2_offset_rs1 },
    { "fmv.h.x", rv_codec_r, rv_fmt_frd_rs1 },
    { "fmv.x.h", rv_codec_r, rv_fmt_rd_frs1 },
    { "fli.s", rv_codec_fli, rv_fmt_fli },
    { "fli.d", rv_codec_fli, rv_fmt_fli },
    { "fli.q", rv_codec_fli, rv_fmt_fli },
    { "fli.h", rv_codec_fli, rv_fmt_fli },
    { "fminm.s", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmaxm.s", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fminm.d", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmaxm.d", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fminm.q", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmaxm.q", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fminm.h", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fmaxm.h", rv_codec_r, rv_fmt_frd_frs1_frs2 },
    { "fround.s", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "froundnx.s", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fround.d", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "froundnx.d", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fround.q", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "froundnx.q", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fround.h", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "froundnx.h", rv_codec_r_m, rv_fmt_rm_frd_frs1 },
    { "fcvtmod.w.d", rv_codec_r_m, rv_fmt_rm_rd_frs1 },
    { "fmvh.x.d", rv_codec_r, rv_fmt_rd_frs1 },
    { "fmvp.d.x", rv_codec_r, rv_fmt_frd_rs1_rs2 },
    { "fmvh.x.q", rv_codec_r, rv_fmt_rd_frs1 },
    { "fmvp.q.x", rv_codec_r, rv_fmt_frd_rs1_rs2 },
    { "fleq.s", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fltq.s", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fleq.d", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fltq.d", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fleq.q", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fltq.q", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fleq.h", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "fltq.h", rv_codec_r, rv_fmt_rd_frs1_frs2 },
    { "vaesdf.vv", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesdf.vs", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesdm.vv", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesdm.vs", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesef.vv", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesef.vs", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesem.vv", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaesem.vs", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vaeskf1.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm },
    { "vaeskf2.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm },
    { "vaesz.vs", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vandn.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vandn.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vbrev.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vbrev8.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vclmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vclmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vclmulh.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vclmulh.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vclz.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vcpop.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vctz.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vghsh.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1 },
    { "vgmul.vv", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vrev8.v", rv_codec_v_r, rv_fmt_vd_vs2_vm },
    { "vrol.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vrol.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vror.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vror.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vror.vi", rv_codec_vror_vi, rv_fmt_vd_vs2_uimm_vm },
    { "vsha2ch.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1 },
    { "vsha2cl.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1 },
    { "vsha2ms.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1 },
    { "vsm3c.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm },
    { "vsm3me.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1 },
    { "vsm4k.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm },
    { "vsm4r.vv", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vsm4r.vs", rv_codec_v_r, rv_fmt_vd_vs2 },
    { "vwsll.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm },
    { "vwsll.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm },
    { "vwsll.vi", rv_codec_v_i_u, rv_fmt_vd_vs2_uimm_vm },
    { "amocas.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amocas.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amocas.q", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "mop.r.0", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.1", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.2", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.3", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.4", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.5", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.6", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.7", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.8", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.9", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.10", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.11", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.12", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.13", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.14", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.15", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.16", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.17", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.18", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.19", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.20", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.21", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.22", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.23", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.24", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.25", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.26", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.27", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.28", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.29", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.30", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.r.31", rv_codec_r, rv_fmt_rd_rs1 },
    { "mop.rr.0", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.1", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.2", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.3", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.4", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.5", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.6", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "mop.rr.7", rv_codec_r, rv_fmt_rd_rs1_rs2 },
    { "c.mop.1", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.3", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.5", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.7", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.9", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.11", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.13", rv_codec_ci_none, rv_fmt_none },
    { "c.mop.15", rv_codec_ci_none, rv_fmt_none },
    { "amoswap.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoadd.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoxor.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoor.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoand.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomin.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomax.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amominu.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomaxu.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoswap.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoadd.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoxor.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoor.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amoand.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomin.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomax.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amominu.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amomaxu.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amocas.b", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "amocas.h", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "wrs.sto", rv_codec_none, rv_fmt_none },
    { "wrs.nto", rv_codec_none, rv_fmt_none },
    { "lpad", rv_codec_lp, rv_fmt_imm },
    { "sspush", rv_codec_r, rv_fmt_rs2 },
    { "sspopchk", rv_codec_r, rv_fmt_rs1 },
    { "ssrdp", rv_codec_r, rv_fmt_rd },
    { "ssamoswap.w", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "ssamoswap.d", rv_codec_r_a, rv_fmt_aqrl_rd_rs2_rs1 },
    { "c.sspush", rv_codec_cmop_ss, NULL, DECOMP(rv_op_sspush) },
    { "c.sspopchk", rv_codec_cmop_ss, NULL, DECOMP(rv_op_sspopchk) },
   { "cbo.inval", rv_codec_r, rv_fmt_rs1 },
   { "cbo.clean", rv_codec_r, rv_fmt_rs1 },
   { "cbo.flush", rv_codec_r, rv_fmt_rs1 },
   { "cbo.zero", rv_codec_r, rv_fmt_rs1 },
   { "mnret", rv_codec_none, rv_fmt_none },
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
    case 0x0008: return "vstart";
    case 0x0009: return "vxsat";
    case 0x000a: return "vxrm";
    case 0x000f: return "vcsr";
    case 0x0011: return "ssp";
    case 0x0015: return "seed";
    case 0x0017: return "jvt";
    case 0x0040: return "uscratch";
    case 0x0041: return "uepc";
    case 0x0042: return "ucause";
    case 0x0043: return "utval";
    case 0x0044: return "uip";
    case 0x0100: return "sstatus";
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
    case 0x03a0: return "pmpcfg0";
    case 0x03a1: return "pmpcfg1";
    case 0x03a2: return "pmpcfg2";
    case 0x03a3: return "pmpcfg3";
    case 0x03a4: return "pmpcfg4";
    case 0x03a5: return "pmpcfg5";
    case 0x03a6: return "pmpcfg6";
    case 0x03a7: return "pmpcfg7";
    case 0x03a8: return "pmpcfg8";
    case 0x03a9: return "pmpcfg9";
    case 0x03aa: return "pmpcfg10";
    case 0x03ab: return "pmpcfg11";
    case 0x03ac: return "pmpcfg12";
    case 0x03ad: return "pmpcfg13";
    case 0x03ae: return "pmpcfg14";
    case 0x03af: return "pmpcfg15";
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
    case 0x03bd: return "pmpaddr13";
    case 0x03be: return "pmpaddr14";
    case 0x03bf: return "pmpaddr15";
    case 0x03c0: return "pmpaddr16";
    case 0x03c1: return "pmpaddr17";
    case 0x03c2: return "pmpaddr18";
    case 0x03c3: return "pmpaddr19";
    case 0x03c4: return "pmpaddr20";
    case 0x03c5: return "pmpaddr21";
    case 0x03c6: return "pmpaddr22";
    case 0x03c7: return "pmpaddr23";
    case 0x03c8: return "pmpaddr24";
    case 0x03c9: return "pmpaddr25";
    case 0x03ca: return "pmpaddr26";
    case 0x03cb: return "pmpaddr27";
    case 0x03cc: return "pmpaddr28";
    case 0x03cd: return "pmpaddr29";
    case 0x03ce: return "pmpaddr30";
    case 0x03cf: return "pmpaddr31";
    case 0x03d0: return "pmpaddr32";
    case 0x03d1: return "pmpaddr33";
    case 0x03d2: return "pmpaddr34";
    case 0x03d3: return "pmpaddr35";
    case 0x03d4: return "pmpaddr36";
    case 0x03d5: return "pmpaddr37";
    case 0x03d6: return "pmpaddr38";
    case 0x03d7: return "pmpaddr39";
    case 0x03d8: return "pmpaddr40";
    case 0x03d9: return "pmpaddr41";
    case 0x03da: return "pmpaddr42";
    case 0x03db: return "pmpaddr43";
    case 0x03dc: return "pmpaddr44";
    case 0x03dd: return "pmpaddr45";
    case 0x03de: return "pmpaddr46";
    case 0x03df: return "pmpaddr47";
    case 0x03e0: return "pmpaddr48";
    case 0x03e1: return "pmpaddr49";
    case 0x03e2: return "pmpaddr50";
    case 0x03e3: return "pmpaddr51";
    case 0x03e4: return "pmpaddr52";
    case 0x03e5: return "pmpaddr53";
    case 0x03e6: return "pmpaddr54";
    case 0x03e7: return "pmpaddr55";
    case 0x03e8: return "pmpaddr56";
    case 0x03e9: return "pmpaddr57";
    case 0x03ea: return "pmpaddr58";
    case 0x03eb: return "pmpaddr59";
    case 0x03ec: return "pmpaddr60";
    case 0x03ed: return "pmpaddr61";
    case 0x03ee: return "pmpaddr62";
    case 0x03ef: return "pmpaddr63";
    case 0x0780: return "mtohost";
    case 0x0781: return "mfromhost";
    case 0x0782: return "mreset";
    case 0x0783: return "mipi";
    case 0x0784: return "miobase";
    case 0x07a0: return "tselect";
    case 0x07a1: return "tdata1";
    case 0x07a2: return "tdata2";
    case 0x07a3: return "tdata3";
    case 0x07a4: return "tinfo";
    case 0x07b0: return "dcsr";
    case 0x07b1: return "dpc";
    case 0x07b2: return "dscratch0";
    case 0x07b3: return "dscratch1";
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
    case 0x0c20: return "vl";
    case 0x0c21: return "vtype";
    case 0x0c22: return "vlenb";
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

static const rv_opcode_data *decode_inst_opcode(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;

    switch ((inst >> 0) & 0b11) {
    case 0:
        switch ((inst >> 13) & 0b111) {
        case 0:
            if ((inst >> 5) & 0xff) {
                op = rv_op_c_addi4spn;
            }
            break;
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
        case 4:
            switch ((inst >> 10) & 0b111) {
            case 0: op = rv_op_c_lbu; break;
            case 1:
                if (((inst >> 6) & 1) == 0) {
                    op = rv_op_c_lhu;
                } else {
                    op = rv_op_c_lh;
                }
                break;
            case 2: op = rv_op_c_sb; break;
            case 3:
                if (((inst >> 6) & 1) == 0) {
                    op = rv_op_c_sh;
                }
                break;
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
        switch ((inst >> 13) & 0b111) {
        case 0:
            op = rv_op_c_addi; /* or unspecified HINT */
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
            if (dec->cfg && dec->cfg->ext_zcmop) {
                if ((((inst >> 2) & 0b111111) == 0b100000) &&
                    (((inst >> 11) & 0b11) == 0b0)) {
                    unsigned int cmop_code = 0;
                    cmop_code = ((inst >> 8) & 0b111);
                    op = rv_c_mop_1 + cmop_code;
                    if (dec->cfg->ext_zicfiss) {
                        op = (cmop_code == 0) ? rv_op_c_sspush : op;
                        op = (cmop_code == 2) ? rv_op_c_sspopchk : op;
                    }
                    break;
                }
            }
            if (inst & ((1 << 12) | (0x1f << 2))) {
                switch ((inst >> 7) & 0b11111) {
                case 2: op = rv_op_c_addi16sp; break;
                default: op = rv_op_c_lui; break;
                }
            }
            break;
        case 4:
            switch ((inst >> 10) & 0b11) {
            case 0:
                /* For rv32, shamt[5]=1 is designated for custom extensions. */
                if (isa != rv32 || (inst & 0x1000) == 0) {
                    op = rv_op_c_srli; /* or unspecified HINT */
                }
                break;
            case 1:
                /* For rv32, shamt[5]=1 is designated for custom extensions. */
                if (isa != rv32 || (inst & 0x1000) == 0) {
                    op = rv_op_c_srai; /* or unspecified HINT */
                }
                break;
            case 2: op = rv_op_c_andi; break;
            case 3:
                switch (((inst >> 10) & 0b100) | ((inst >> 5) & 0b011)) {
                case 0: op = rv_op_c_sub; break;
                case 1: op = rv_op_c_xor; break;
                case 2: op = rv_op_c_or; break;
                case 3: op = rv_op_c_and; break;
                case 4:
                    if (isa != rv32) {
                        op = rv_op_c_subw;
                    }
                    break;
                case 5:
                    if (isa != rv32) {
                        op = rv_op_c_addw;
                    }
                    break;
                case 6: op = rv_op_c_mul; break;
                case 7:
                    switch ((inst >> 2) & 0b111) {
                    case 0: op = rv_op_c_zext_b; break;
                    case 1: op = rv_op_c_sext_b; break;
                    case 2: op = rv_op_c_zext_h; break;
                    case 3: op = rv_op_c_sext_h; break;
                    case 4: op = rv_op_c_zext_w; break;
                    case 5: op = rv_op_c_not; break;
                    }
                    break;
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
        switch ((inst >> 13) & 0b111) {
        case 0:
            if (isa != rv32 || (inst & 0x1000) == 0) {
                op = rv_op_c_slli; /* or unspecified HINT */
            }
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
            switch ((inst >> 12) & 0b1) {
            case 0:
                switch ((inst >> 2) & 0b11111) {
                case 0: op = rv_op_c_jr; break;
                default: op = rv_op_c_mv; break;
                }
                break;
            case 1:
                switch ((inst >> 2) & 0b11111) {
                case 0:
                    switch ((inst >> 7) & 0b11111) {
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
                if (dec->cfg && dec->cfg->ext_zcmp && ((inst >> 12) & 0b01)) {
                    switch ((inst >> 8) & 0b01111) {
                    case 8:
                        if (((inst >> 4) & 0b01111) >= 4) {
                            op = rv_op_cm_push;
                        }
                        break;
                    case 10:
                        if (((inst >> 4) & 0b01111) >= 4) {
                            op = rv_op_cm_pop;
                        }
                        break;
                    case 12:
                        if (((inst >> 4) & 0b01111) >= 4) {
                            op = rv_op_cm_popretz;
                        }
                        break;
                    case 14:
                        if (((inst >> 4) & 0b01111) >= 4) {
                            op = rv_op_cm_popret;
                        }
                        break;
                    }
                } else {
                    switch ((inst >> 10) & 0b011) {
                    case 0:
                        if (dec->cfg && !dec->cfg->ext_zcmt) {
                            break;
                        }
                        if (((inst >> 2) & 0xFF) >= 32) {
                            op = rv_op_cm_jalt;
                        } else {
                            op = rv_op_cm_jt;
                        }
                        break;
                    case 3:
                        if (dec->cfg && !dec->cfg->ext_zcmp) {
                            break;
                        }
                        switch ((inst >> 5) & 0b011) {
                        case 1: op = rv_op_cm_mvsa01; break;
                        case 3: op = rv_op_cm_mva01s; break;
                        }
                        break;
                    }
                }
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
        switch ((inst >> 2) & 0b11111) {
        case 0:
            switch ((inst >> 12) & 0b111) {
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
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch ((inst >> 20) & 0b111111111111) {
                case 40: op = rv_op_vl1re8_v; break;
                case 552: op = rv_op_vl2re8_v; break;
                case 1576: op = rv_op_vl4re8_v; break;
                case 3624: op = rv_op_vl8re8_v; break;
                }
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vle8_v; break;
                    case 11: op = rv_op_vlm_v; break;
                    case 16: op = rv_op_vle8ff_v; break;
                    }
                    break;
                case 1: op = rv_op_vluxei8_v; break;
                case 2: op = rv_op_vlse8_v; break;
                case 3: op = rv_op_vloxei8_v; break;
                }
                break;
            case 1: op = rv_op_flh; break;
            case 2: op = rv_op_flw; break;
            case 3: op = rv_op_fld; break;
            case 4: op = rv_op_flq; break;
            case 5:
                switch ((inst >> 20) & 0b111111111111) {
                case 40: op = rv_op_vl1re16_v; break;
                case 552: op = rv_op_vl2re16_v; break;
                case 1576: op = rv_op_vl4re16_v; break;
                case 3624: op = rv_op_vl8re16_v; break;
                }
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vle16_v; break;
                    case 16: op = rv_op_vle16ff_v; break;
                    }
                    break;
                case 1: op = rv_op_vluxei16_v; break;
                case 2: op = rv_op_vlse16_v; break;
                case 3: op = rv_op_vloxei16_v; break;
                }
                break;
            case 6:
                switch ((inst >> 20) & 0b111111111111) {
                case 40: op = rv_op_vl1re32_v; break;
                case 552: op = rv_op_vl2re32_v; break;
                case 1576: op = rv_op_vl4re32_v; break;
                case 3624: op = rv_op_vl8re32_v; break;
                }
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vle32_v; break;
                    case 16: op = rv_op_vle32ff_v; break;
                    }
                    break;
                case 1: op = rv_op_vluxei32_v; break;
                case 2: op = rv_op_vlse32_v; break;
                case 3: op = rv_op_vloxei32_v; break;
                }
                break;
            case 7:
                switch ((inst >> 20) & 0b111111111111) {
                case 40: op = rv_op_vl1re64_v; break;
                case 552: op = rv_op_vl2re64_v; break;
                case 1576: op = rv_op_vl4re64_v; break;
                case 3624: op = rv_op_vl8re64_v; break;
                }
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vle64_v; break;
                    case 16: op = rv_op_vle64ff_v; break;
                    }
                    break;
                case 1: op = rv_op_vluxei64_v; break;
                case 2: op = rv_op_vlse64_v; break;
                case 3: op = rv_op_vloxei64_v; break;
                }
                break;
            }
            break;
        case 3:
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_fence; break;
            case 1: op = rv_op_fence_i; break;
            case 2:
               /*
                * 'lq' shares the "(...) 010 ..... 0001111" opcode space
                * with 'cbo' insns.  Check the next 5 bits to select
                * what we want:
                *
                * cbo_inval  0000000 00000 ..... 010 00000 0001111
                * cbo_clean  0000000 00001 ..... 010 00000 0001111
                * cbo_flush  0000000 00010 ..... 010 00000 0001111
                * cbo_zero   0000000 00100 ..... 010 00000 0001111
                *
                * Anything that doesn't match these will default to 'lq'.
                */
               switch ((inst >> 17) & 0b11111) {
               case 0: op = rv_op_cbo_inval; break;
               case 1: op = rv_op_cbo_clean; break;
               case 2: op = rv_op_cbo_flush; break;
               case 4: op = rv_op_cbo_zero; break;
               default: op = rv_op_lq; break;
               }
            }
            break;
        case 4:
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_addi; break;
            case 1:
                switch ((inst >> 27) & 0b11111) {
                case 0b00000: op = rv_op_slli; break;
                case 0b00001:
                    switch ((inst >> 20) & 0b1111111) {
                    case 0b0001111: op = rv_op_zip; break;
                    }
                    break;
                case 0b00010:
                    switch ((inst >> 20) & 0b1111111) {
                    case 0b0000000: op = rv_op_sha256sum0; break;
                    case 0b0000001: op = rv_op_sha256sum1; break;
                    case 0b0000010: op = rv_op_sha256sig0; break;
                    case 0b0000011: op = rv_op_sha256sig1; break;
                    case 0b0000100: op = rv_op_sha512sum0; break;
                    case 0b0000101: op = rv_op_sha512sum1; break;
                    case 0b0000110: op = rv_op_sha512sig0; break;
                    case 0b0000111: op = rv_op_sha512sig1; break;
                    case 0b0001000: op = rv_op_sm3p0; break;
                    case 0b0001001: op = rv_op_sm3p1; break;
                    }
                    break;
                case 0b00101: op = rv_op_bseti; break;
                case 0b00110:
                    switch ((inst >> 20) & 0b1111111) {
                    case 0b0000000: op = rv_op_aes64im; break;
                    default:
                        if (((inst >> 24) & 0b0111) == 0b001) {
                            op = rv_op_aes64ks1i;
                        }
                        break;
                     }
                     break;
                case 0b01001: op = rv_op_bclri; break;
                case 0b01101: op = rv_op_binvi; break;
                case 0b01100:
                    switch ((inst >> 20) & 0b1111111) {
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
                switch ((inst >> 27) & 0b11111) {
                case 0b00000: op = rv_op_srli; break;
                case 0b00001:
                    switch ((inst >> 20) & 0b1111111) {
                    case 0b0001111: op = rv_op_unzip; break;
                    }
                    break;
                case 0b00101: op = rv_op_orc_b; break;
                case 0b01000: op = rv_op_srai; break;
                case 0b01001: op = rv_op_bexti; break;
                case 0b01100: op = rv_op_rori; break;
                case 0b01101:
                    switch ((inst >> 20) & 0b1111111) {
                    case 0b0011000: op = rv_op_rev8; break;
                    case 0b0111000: op = rv_op_rev8; break;
                    case 0b0000111: op = rv_op_brev8; break;
                    }
                    break;
                }
                break;
            case 6: op = rv_op_ori; break;
            case 7: op = rv_op_andi; break;
            }
            break;
        case 5:
            op = rv_op_auipc;
            if (dec->cfg && dec->cfg->ext_zicfilp &&
                (((inst >> 7) & 0b11111) == 0b00000)) {
                op = rv_op_lpad;
            }
            break;
        case 6:
            /* OP-IMM-32 */
            if (isa == rv32) {
                break;
            }
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_addiw; break;
            case 1:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_slliw; break;
                case 2: op = rv_op_slli_uw; break;
                case 24:
                    switch ((inst >> 20) & 0b11111) {
                    case 0b00000: op = rv_op_clzw; break;
                    case 0b00001: op = rv_op_ctzw; break;
                    case 0b00010: op = rv_op_cpopw; break;
                    }
                    break;
                }
                break;
            case 5:
                switch ((inst >> 25) & 0b1111111) {
                case 0: op = rv_op_srliw; break;
                case 32: op = rv_op_sraiw; break;
                case 48: op = rv_op_roriw; break;
                }
                break;
            }
            break;
        case 8:
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_sb; break;
            case 1: op = rv_op_sh; break;
            case 2: op = rv_op_sw; break;
            case 3: op = rv_op_sd; break;
            case 4: op = rv_op_sq; break;
            }
            break;
        case 9:
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch ((inst >> 20) & 0b111111111111) {
                case 40: op = rv_op_vs1r_v; break;
                case 552: op = rv_op_vs2r_v; break;
                case 1576: op = rv_op_vs4r_v; break;
                case 3624: op = rv_op_vs8r_v; break;
                }
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vse8_v; break;
                    case 11: op = rv_op_vsm_v; break;
                    }
                    break;
                case 1: op = rv_op_vsuxei8_v; break;
                case 2: op = rv_op_vsse8_v; break;
                case 3: op = rv_op_vsoxei8_v; break;
                }
                break;
            case 1: op = rv_op_fsh; break;
            case 2: op = rv_op_fsw; break;
            case 3: op = rv_op_fsd; break;
            case 4: op = rv_op_fsq; break;
            case 5:
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vse16_v; break;
                    }
                    break;
                case 1: op = rv_op_vsuxei16_v; break;
                case 2: op = rv_op_vsse16_v; break;
                case 3: op = rv_op_vsoxei16_v; break;
                }
                break;
            case 6:
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vse32_v; break;
                    }
                    break;
                case 1: op = rv_op_vsuxei32_v; break;
                case 2: op = rv_op_vsse32_v; break;
                case 3: op = rv_op_vsoxei32_v; break;
                }
                break;
            case 7:
                switch ((inst >> 26) & 0b111) {
                case 0:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: op = rv_op_vse64_v; break;
                    }
                    break;
                case 1: op = rv_op_vsuxei64_v; break;
                case 2: op = rv_op_vsse64_v; break;
                case 3: op = rv_op_vsoxei64_v; break;
                }
                break;
            }
            break;
        case 11:
            switch (((inst >> 24) & 0b11111000) |
                    ((inst >> 12) & 0b00000111)) {
            case 0: op = rv_op_amoadd_b; break;
            case 1: op = rv_op_amoadd_h; break;
            case 2: op = rv_op_amoadd_w; break;
            case 3: op = rv_op_amoadd_d; break;
            case 4: op = rv_op_amoadd_q; break;
            case 8: op = rv_op_amoswap_b; break;
            case 9: op = rv_op_amoswap_h; break;
            case 10: op = rv_op_amoswap_w; break;
            case 11: op = rv_op_amoswap_d; break;
            case 12: op = rv_op_amoswap_q; break;
            case 18:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_lr_w; break;
                }
                break;
            case 19:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_lr_d; break;
                }
                break;
            case 20:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_lr_q; break;
                }
                break;
            case 26: op = rv_op_sc_w; break;
            case 27: op = rv_op_sc_d; break;
            case 28: op = rv_op_sc_q; break;
            case 32: op = rv_op_amoxor_b; break;
            case 33: op = rv_op_amoxor_h; break;
            case 34: op = rv_op_amoxor_w; break;
            case 35: op = rv_op_amoxor_d; break;
            case 36: op = rv_op_amoxor_q; break;
            case 40: op = rv_op_amocas_b; break;
            case 41: op = rv_op_amocas_h; break;
            case 42: op = rv_op_amocas_w; break;
            case 43: op = rv_op_amocas_d; break;
            case 44: op = rv_op_amocas_q; break;
            case 64: op = rv_op_amoor_b; break;
            case 65: op = rv_op_amoor_h; break;
            case 66: op = rv_op_amoor_w; break;
            case 67: op = rv_op_amoor_d; break;
            case 68: op = rv_op_amoor_q; break;
            case 74: op = rv_op_ssamoswap_w; break;
            case 75: op = rv_op_ssamoswap_d; break;
            case 96: op = rv_op_amoand_b; break;
            case 97: op = rv_op_amoand_h; break;
            case 98: op = rv_op_amoand_w; break;
            case 99: op = rv_op_amoand_d; break;
            case 100: op = rv_op_amoand_q; break;
            case 128: op = rv_op_amomin_b; break;
            case 129: op = rv_op_amomin_h; break;
            case 130: op = rv_op_amomin_w; break;
            case 131: op = rv_op_amomin_d; break;
            case 132: op = rv_op_amomin_q; break;
            case 160: op = rv_op_amomax_b; break;
            case 161: op = rv_op_amomax_h; break;
            case 162: op = rv_op_amomax_w; break;
            case 163: op = rv_op_amomax_d; break;
            case 164: op = rv_op_amomax_q; break;
            case 192: op = rv_op_amominu_b; break;
            case 193: op = rv_op_amominu_h; break;
            case 194: op = rv_op_amominu_w; break;
            case 195: op = rv_op_amominu_d; break;
            case 196: op = rv_op_amominu_q; break;
            case 224: op = rv_op_amomaxu_b; break;
            case 225: op = rv_op_amomaxu_h; break;
            case 226: op = rv_op_amomaxu_w; break;
            case 227: op = rv_op_amomaxu_d; break;
            case 228: op = rv_op_amomaxu_q; break;
            }
            break;
        case 12:
            switch (((inst >> 22) & 0b1111111000) |
                    ((inst >> 12) & 0b0000000111)) {
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
                if (isa == rv32 && !((inst >> 20) & 0b11111)) {
                    op = rv_op_zext_h;
                } else {
                    op = rv_op_pack;
                }
                break;
            case 39: op = rv_op_packh; break;

            case 41: op = rv_op_clmul; break;
            case 42: op = rv_op_clmulr; break;
            case 43: op = rv_op_clmulh; break;
            case 44: op = rv_op_min; break;
            case 45: op = rv_op_minu; break;
            case 46: op = rv_op_max; break;
            case 47: op = rv_op_maxu; break;
            case 075: op = rv_op_czero_eqz; break;
            case 077: op = rv_op_czero_nez; break;
            case 130: op = rv_op_sh1add; break;
            case 132: op = rv_op_sh2add; break;
            case 134: op = rv_op_sh3add; break;
            case 161: op = rv_op_bset; break;
            case 162: op = rv_op_xperm4; break;
            case 164: op = rv_op_xperm8; break;
            case 200: op = rv_op_aes64es; break;
            case 216: op = rv_op_aes64esm; break;
            case 232: op = rv_op_aes64ds; break;
            case 248: op = rv_op_aes64dsm; break;
            case 256: op = rv_op_sub; break;
            case 260: op = rv_op_xnor; break;
            case 261: op = rv_op_sra; break;
            case 262: op = rv_op_orn; break;
            case 263: op = rv_op_andn; break;
            case 289: op = rv_op_bclr; break;
            case 293: op = rv_op_bext; break;
            case 320: op = rv_op_sha512sum0r; break;
            case 328: op = rv_op_sha512sum1r; break;
            case 336: op = rv_op_sha512sig0l; break;
            case 344: op = rv_op_sha512sig1l; break;
            case 368: op = rv_op_sha512sig0h; break;
            case 376: op = rv_op_sha512sig1h; break;
            case 385: op = rv_op_rol; break;
            case 389: op = rv_op_ror; break;
            case 417: op = rv_op_binv; break;
            case 504: op = rv_op_aes64ks2; break;
            }
            switch ((inst >> 25) & 0b0011111) {
            case 17: op = rv_op_aes32esi; break;
            case 19: op = rv_op_aes32esmi; break;
            case 21: op = rv_op_aes32dsi; break;
            case 23: op = rv_op_aes32dsmi; break;
            case 24: op = rv_op_sm4ed; break;
            case 26: op = rv_op_sm4ks; break;
            }
            break;
        case 13: op = rv_op_lui; break;
        case 14:
            /* OP-32 */
            if (isa == rv32) {
                break;
            }
            switch (((inst >> 22) & 0b1111111000) |
                    ((inst >> 12) & 0b0000000111)) {
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
                default: op = rv_op_packw; break;
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
            switch ((inst >> 25) & 0b11) {
            case 0: op = rv_op_fmadd_s; break;
            case 1: op = rv_op_fmadd_d; break;
            case 3: op = rv_op_fmadd_q; break;
            }
            break;
        case 17:
            switch ((inst >> 25) & 0b11) {
            case 0: op = rv_op_fmsub_s; break;
            case 1: op = rv_op_fmsub_d; break;
            case 3: op = rv_op_fmsub_q; break;
            }
            break;
        case 18:
            switch ((inst >> 25) & 0b11) {
            case 0: op = rv_op_fnmsub_s; break;
            case 1: op = rv_op_fnmsub_d; break;
            case 3: op = rv_op_fnmsub_q; break;
            }
            break;
        case 19:
            switch ((inst >> 25) & 0b11) {
            case 0: op = rv_op_fnmadd_s; break;
            case 1: op = rv_op_fnmadd_d; break;
            case 3: op = rv_op_fnmadd_q; break;
            }
            break;
        case 20:
            switch ((inst >> 25) & 0b1111111) {
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
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fsgnj_s; break;
                case 1: op = rv_op_fsgnjn_s; break;
                case 2: op = rv_op_fsgnjx_s; break;
                }
                break;
            case 17:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fsgnj_d; break;
                case 1: op = rv_op_fsgnjn_d; break;
                case 2: op = rv_op_fsgnjx_d; break;
                }
                break;
            case 19:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fsgnj_q; break;
                case 1: op = rv_op_fsgnjn_q; break;
                case 2: op = rv_op_fsgnjx_q; break;
                }
                break;
            case 20:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fmin_s; break;
                case 1: op = rv_op_fmax_s; break;
                case 2: op = rv_op_fminm_s; break;
                case 3: op = rv_op_fmaxm_s; break;
                }
                break;
            case 21:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fmin_d; break;
                case 1: op = rv_op_fmax_d; break;
                case 2: op = rv_op_fminm_d; break;
                case 3: op = rv_op_fmaxm_d; break;
                }
                break;
            case 22:
                switch (((inst >> 12) & 0b111)) {
                case 2: op = rv_op_fminm_h; break;
                case 3: op = rv_op_fmaxm_h; break;
                }
                break;
            case 23:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fmin_q; break;
                case 1: op = rv_op_fmax_q; break;
                case 2: op = rv_op_fminm_q; break;
                case 3: op = rv_op_fmaxm_q; break;
                }
                break;
            case 32:
                switch ((inst >> 20) & 0b11111) {
                case 1: op = rv_op_fcvt_s_d; break;
                case 3: op = rv_op_fcvt_s_q; break;
                case 4: op = rv_op_fround_s; break;
                case 5: op = rv_op_froundnx_s; break;
                case 6: op = rv_op_fcvt_s_bf16; break;
                }
                break;
            case 33:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_d_s; break;
                case 3: op = rv_op_fcvt_d_q; break;
                case 4: op = rv_op_fround_d; break;
                case 5: op = rv_op_froundnx_d; break;
                }
                break;
            case 34:
                switch (((inst >> 20) & 0b11111)) {
                case 4: op = rv_op_fround_h; break;
                case 5: op = rv_op_froundnx_h; break;
                case 8: op = rv_op_fcvt_bf16_s; break;
                }
                break;
            case 35:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_q_s; break;
                case 1: op = rv_op_fcvt_q_d; break;
                case 4: op = rv_op_fround_q; break;
                case 5: op = rv_op_froundnx_q; break;
                }
                break;
            case 44:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fsqrt_s; break;
                }
                break;
            case 45:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fsqrt_d; break;
                }
                break;
            case 47:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fsqrt_q; break;
                }
                break;
            case 80:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fle_s; break;
                case 1: op = rv_op_flt_s; break;
                case 2: op = rv_op_feq_s; break;
                case 4: op = rv_op_fleq_s; break;
                case 5: op = rv_op_fltq_s; break;
                }
                break;
            case 81:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fle_d; break;
                case 1: op = rv_op_flt_d; break;
                case 2: op = rv_op_feq_d; break;
                case 4: op = rv_op_fleq_d; break;
                case 5: op = rv_op_fltq_d; break;
                }
                break;
            case 82:
                switch (((inst >> 12) & 0b111)) {
                case 4: op = rv_op_fleq_h; break;
                case 5: op = rv_op_fltq_h; break;
                }
                break;
            case 83:
                switch ((inst >> 12) & 0b111) {
                case 0: op = rv_op_fle_q; break;
                case 1: op = rv_op_flt_q; break;
                case 2: op = rv_op_feq_q; break;
                case 4: op = rv_op_fleq_q; break;
                case 5: op = rv_op_fltq_q; break;
                }
                break;
            case 89:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fmvp_d_x; break;
                }
                break;
            case 91:
                switch (((inst >> 12) & 0b111)) {
                case 0: op = rv_op_fmvp_q_x; break;
                }
                break;
            case 96:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_w_s; break;
                case 1: op = rv_op_fcvt_wu_s; break;
                case 2: op = rv_op_fcvt_l_s; break;
                case 3: op = rv_op_fcvt_lu_s; break;
                }
                break;
            case 97:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_w_d; break;
                case 1: op = rv_op_fcvt_wu_d; break;
                case 2: op = rv_op_fcvt_l_d; break;
                case 3: op = rv_op_fcvt_lu_d; break;
                case 8: op = rv_op_fcvtmod_w_d; break;
                }
                break;
            case 99:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_w_q; break;
                case 1: op = rv_op_fcvt_wu_q; break;
                case 2: op = rv_op_fcvt_l_q; break;
                case 3: op = rv_op_fcvt_lu_q; break;
                }
                break;
            case 104:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_s_w; break;
                case 1: op = rv_op_fcvt_s_wu; break;
                case 2: op = rv_op_fcvt_s_l; break;
                case 3: op = rv_op_fcvt_s_lu; break;
                }
                break;
            case 105:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_d_w; break;
                case 1: op = rv_op_fcvt_d_wu; break;
                case 2: op = rv_op_fcvt_d_l; break;
                case 3: op = rv_op_fcvt_d_lu; break;
                }
                break;
            case 107:
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_fcvt_q_w; break;
                case 1: op = rv_op_fcvt_q_wu; break;
                case 2: op = rv_op_fcvt_q_l; break;
                case 3: op = rv_op_fcvt_q_lu; break;
                }
                break;
            case 112:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_s; break;
                case 1: op = rv_op_fclass_s; break;
                }
                break;
            case 113:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_d; break;
                case 1: op = rv_op_fclass_d; break;
                case 8: op = rv_op_fmvh_x_d; break;
                }
                break;
            case 114:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_h; break;
                }
                break;
            case 115:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_x_q; break;
                case 1: op = rv_op_fclass_q; break;
                case 8: op = rv_op_fmvh_x_q; break;
                }
                break;
            case 120:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_s_x; break;
                case 8: op = rv_op_fli_s; break;
                }
                break;
            case 121:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_d_x; break;
                case 8: op = rv_op_fli_d; break;
                }
                break;
            case 122:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_h_x; break;
                case 8: op = rv_op_fli_h; break;
                }
                break;
            case 123:
                switch (((inst >> 17) & 0b11111000) |
                        ((inst >> 12) & 0b00000111)) {
                case 0: op = rv_op_fmv_q_x; break;
                case 8: op = rv_op_fli_q; break;
                }
                break;
            }
            break;
        case 21:
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vadd_vv; break;
                case 1: op = rv_op_vandn_vv; break;
                case 2: op = rv_op_vsub_vv; break;
                case 4: op = rv_op_vminu_vv; break;
                case 5: op = rv_op_vmin_vv; break;
                case 6: op = rv_op_vmaxu_vv; break;
                case 7: op = rv_op_vmax_vv; break;
                case 9: op = rv_op_vand_vv; break;
                case 10: op = rv_op_vor_vv; break;
                case 11: op = rv_op_vxor_vv; break;
                case 12: op = rv_op_vrgather_vv; break;
                case 14: op = rv_op_vrgatherei16_vv; break;
                case 16:
                    if (((inst >> 25) & 1) == 0) {
                        op = rv_op_vadc_vvm;
                    }
                    break;
                case 17: op = rv_op_vmadc_vvm; break;
                case 18:
                    if (((inst >> 25) & 1) == 0) {
                        op = rv_op_vsbc_vvm;
                    }
                    break;
                case 19: op = rv_op_vmsbc_vvm; break;
                case 20: op = rv_op_vror_vv; break;
                case 21: op = rv_op_vrol_vv; break;
                case 23:
                    if (((inst >> 20) & 0b111111) == 32)
                        op = rv_op_vmv_v_v;
                    else if (((inst >> 25) & 1) == 0)
                        op = rv_op_vmerge_vvm;
                    break;
                case 24: op = rv_op_vmseq_vv; break;
                case 25: op = rv_op_vmsne_vv; break;
                case 26: op = rv_op_vmsltu_vv; break;
                case 27: op = rv_op_vmslt_vv; break;
                case 28: op = rv_op_vmsleu_vv; break;
                case 29: op = rv_op_vmsle_vv; break;
                case 32: op = rv_op_vsaddu_vv; break;
                case 33: op = rv_op_vsadd_vv; break;
                case 34: op = rv_op_vssubu_vv; break;
                case 35: op = rv_op_vssub_vv; break;
                case 37: op = rv_op_vsll_vv; break;
                case 39: op = rv_op_vsmul_vv; break;
                case 40: op = rv_op_vsrl_vv; break;
                case 41: op = rv_op_vsra_vv; break;
                case 42: op = rv_op_vssrl_vv; break;
                case 43: op = rv_op_vssra_vv; break;
                case 44: op = rv_op_vnsrl_wv; break;
                case 45: op = rv_op_vnsra_wv; break;
                case 46: op = rv_op_vnclipu_wv; break;
                case 47: op = rv_op_vnclip_wv; break;
                case 48: op = rv_op_vwredsumu_vs; break;
                case 49: op = rv_op_vwredsum_vs; break;
                case 53: op = rv_op_vwsll_vv; break;
                }
                break;
            case 1:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vfadd_vv; break;
                case 1: op = rv_op_vfredusum_vs; break;
                case 2: op = rv_op_vfsub_vv; break;
                case 3: op = rv_op_vfredosum_vs; break;
                case 4: op = rv_op_vfmin_vv; break;
                case 5: op = rv_op_vfredmin_vs; break;
                case 6: op = rv_op_vfmax_vv; break;
                case 7: op = rv_op_vfredmax_vs; break;
                case 8: op = rv_op_vfsgnj_vv; break;
                case 9: op = rv_op_vfsgnjn_vv; break;
                case 10: op = rv_op_vfsgnjx_vv; break;
                case 16:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: if ((inst >> 25) & 1) op = rv_op_vfmv_f_s; break;
                    }
                    break;
                case 18:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: op = rv_op_vfcvt_xu_f_v; break;
                    case 1: op = rv_op_vfcvt_x_f_v; break;
                    case 2: op = rv_op_vfcvt_f_xu_v; break;
                    case 3: op = rv_op_vfcvt_f_x_v; break;
                    case 6: op = rv_op_vfcvt_rtz_xu_f_v; break;
                    case 7: op = rv_op_vfcvt_rtz_x_f_v; break;
                    case 8: op = rv_op_vfwcvt_xu_f_v; break;
                    case 9: op = rv_op_vfwcvt_x_f_v; break;
                    case 10: op = rv_op_vfwcvt_f_xu_v; break;
                    case 11: op = rv_op_vfwcvt_f_x_v; break;
                    case 12: op = rv_op_vfwcvt_f_f_v; break;
                    case 13: op = rv_op_vfwcvtbf16_f_f_v; break;
                    case 14: op = rv_op_vfwcvt_rtz_xu_f_v; break;
                    case 15: op = rv_op_vfwcvt_rtz_x_f_v; break;
                    case 16: op = rv_op_vfncvt_xu_f_w; break;
                    case 17: op = rv_op_vfncvt_x_f_w; break;
                    case 18: op = rv_op_vfncvt_f_xu_w; break;
                    case 19: op = rv_op_vfncvt_f_x_w; break;
                    case 20: op = rv_op_vfncvt_f_f_w; break;
                    case 21: op = rv_op_vfncvt_rod_f_f_w; break;
                    case 22: op = rv_op_vfncvt_rtz_xu_f_w; break;
                    case 23: op = rv_op_vfncvt_rtz_x_f_w; break;
                    case 29: op = rv_op_vfncvtbf16_f_f_w; break;
                    }
                    break;
                case 19:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: op = rv_op_vfsqrt_v; break;
                    case 4: op = rv_op_vfrsqrt7_v; break;
                    case 5: op = rv_op_vfrec7_v; break;
                    case 16: op = rv_op_vfclass_v; break;
                    }
                    break;
                case 24: op = rv_op_vmfeq_vv; break;
                case 25: op = rv_op_vmfle_vv; break;
                case 27: op = rv_op_vmflt_vv; break;
                case 28: op = rv_op_vmfne_vv; break;
                case 32: op = rv_op_vfdiv_vv; break;
                case 36: op = rv_op_vfmul_vv; break;
                case 40: op = rv_op_vfmadd_vv; break;
                case 41: op = rv_op_vfnmadd_vv; break;
                case 42: op = rv_op_vfmsub_vv; break;
                case 43: op = rv_op_vfnmsub_vv; break;
                case 44: op = rv_op_vfmacc_vv; break;
                case 45: op = rv_op_vfnmacc_vv; break;
                case 46: op = rv_op_vfmsac_vv; break;
                case 47: op = rv_op_vfnmsac_vv; break;
                case 48: op = rv_op_vfwadd_vv; break;
                case 49: op = rv_op_vfwredusum_vs; break;
                case 50: op = rv_op_vfwsub_vv; break;
                case 51: op = rv_op_vfwredosum_vs; break;
                case 52: op = rv_op_vfwadd_wv; break;
                case 54: op = rv_op_vfwsub_wv; break;
                case 56: op = rv_op_vfwmul_vv; break;
                case 59: op = rv_op_vfwmaccbf16_vv; break;
                case 60: op = rv_op_vfwmacc_vv; break;
                case 61: op = rv_op_vfwnmacc_vv; break;
                case 62: op = rv_op_vfwmsac_vv; break;
                case 63: op = rv_op_vfwnmsac_vv; break;
                }
                break;
            case 2:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vredsum_vs; break;
                case 1: op = rv_op_vredand_vs; break;
                case 2: op = rv_op_vredor_vs; break;
                case 3: op = rv_op_vredxor_vs; break;
                case 4: op = rv_op_vredminu_vs; break;
                case 5: op = rv_op_vredmin_vs; break;
                case 6: op = rv_op_vredmaxu_vs; break;
                case 7: op = rv_op_vredmax_vs; break;
                case 8: op = rv_op_vaaddu_vv; break;
                case 9: op = rv_op_vaadd_vv; break;
                case 10: op = rv_op_vasubu_vv; break;
                case 11: op = rv_op_vasub_vv; break;
                case 12: op = rv_op_vclmul_vv; break;
                case 13: op = rv_op_vclmulh_vv; break;
                case 16:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: if ((inst >> 25) & 1) op = rv_op_vmv_x_s; break;
                    case 16: op = rv_op_vcpop_m; break;
                    case 17: op = rv_op_vfirst_m; break;
                    }
                    break;
                case 18:
                    switch ((inst >> 15) & 0b11111) {
                    case 2: op = rv_op_vzext_vf8; break;
                    case 3: op = rv_op_vsext_vf8; break;
                    case 4: op = rv_op_vzext_vf4; break;
                    case 5: op = rv_op_vsext_vf4; break;
                    case 6: op = rv_op_vzext_vf2; break;
                    case 7: op = rv_op_vsext_vf2; break;
                    case 8: op = rv_op_vbrev8_v; break;
                    case 9: op = rv_op_vrev8_v; break;
                    case 10: op = rv_op_vbrev_v; break;
                    case 12: op = rv_op_vclz_v; break;
                    case 13: op = rv_op_vctz_v; break;
                    case 14: op = rv_op_vcpop_v; break;
                    }
                    break;
                case 20:
                    switch ((inst >> 15) & 0b11111) {
                    case 1: op = rv_op_vmsbf_m;  break;
                    case 2: op = rv_op_vmsof_m; break;
                    case 3: op = rv_op_vmsif_m; break;
                    case 16: op = rv_op_viota_m; break;
                    case 17:
                        if (((inst >> 20) & 0b11111) == 0) {
                            op = rv_op_vid_v;
                        }
                        break;
                    }
                    break;
                case 23: if ((inst >> 25) & 1) op = rv_op_vcompress_vm; break;
                case 24: if ((inst >> 25) & 1) op = rv_op_vmandn_mm; break;
                case 25: if ((inst >> 25) & 1) op = rv_op_vmand_mm; break;
                case 26: if ((inst >> 25) & 1) op = rv_op_vmor_mm; break;
                case 27: if ((inst >> 25) & 1) op = rv_op_vmxor_mm; break;
                case 28: if ((inst >> 25) & 1) op = rv_op_vmorn_mm; break;
                case 29: if ((inst >> 25) & 1) op = rv_op_vmnand_mm; break;
                case 30: if ((inst >> 25) & 1) op = rv_op_vmnor_mm; break;
                case 31: if ((inst >> 25) & 1) op = rv_op_vmxnor_mm; break;
                case 32: op = rv_op_vdivu_vv; break;
                case 33: op = rv_op_vdiv_vv; break;
                case 34: op = rv_op_vremu_vv; break;
                case 35: op = rv_op_vrem_vv; break;
                case 36: op = rv_op_vmulhu_vv; break;
                case 37: op = rv_op_vmul_vv; break;
                case 38: op = rv_op_vmulhsu_vv; break;
                case 39: op = rv_op_vmulh_vv; break;
                case 41: op = rv_op_vmadd_vv; break;
                case 43: op = rv_op_vnmsub_vv; break;
                case 45: op = rv_op_vmacc_vv; break;
                case 47: op = rv_op_vnmsac_vv; break;
                case 48: op = rv_op_vwaddu_vv; break;
                case 49: op = rv_op_vwadd_vv; break;
                case 50: op = rv_op_vwsubu_vv; break;
                case 51: op = rv_op_vwsub_vv; break;
                case 52: op = rv_op_vwaddu_wv; break;
                case 53: op = rv_op_vwadd_wv; break;
                case 54: op = rv_op_vwsubu_wv; break;
                case 55: op = rv_op_vwsub_wv; break;
                case 56: op = rv_op_vwmulu_vv; break;
                case 58: op = rv_op_vwmulsu_vv; break;
                case 59: op = rv_op_vwmul_vv; break;
                case 60: op = rv_op_vwmaccu_vv; break;
                case 61: op = rv_op_vwmacc_vv; break;
                case 63: op = rv_op_vwmaccsu_vv; break;
                }
                break;
            case 3:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vadd_vi; break;
                case 3: op = rv_op_vrsub_vi; break;
                case 9: op = rv_op_vand_vi; break;
                case 10: op = rv_op_vor_vi; break;
                case 11: op = rv_op_vxor_vi; break;
                case 12: op = rv_op_vrgather_vi; break;
                case 14: op = rv_op_vslideup_vi; break;
                case 15: op = rv_op_vslidedown_vi; break;
                case 16:
                    if (((inst >> 25) & 1) == 0) {
                        op = rv_op_vadc_vim;
                    }
                    break;
                case 17: op = rv_op_vmadc_vim; break;
                case 20: case 21: op = rv_op_vror_vi; break;
                case 23:
                    if (((inst >> 20) & 0b111111) == 32)
                        op = rv_op_vmv_v_i;
                    else if (((inst >> 25) & 1) == 0)
                        op = rv_op_vmerge_vim;
                    break;
                case 24: op = rv_op_vmseq_vi; break;
                case 25: op = rv_op_vmsne_vi; break;
                case 28: op = rv_op_vmsleu_vi; break;
                case 29: op = rv_op_vmsle_vi; break;
                case 30: op = rv_op_vmsgtu_vi; break;
                case 31: op = rv_op_vmsgt_vi; break;
                case 32: op = rv_op_vsaddu_vi; break;
                case 33: op = rv_op_vsadd_vi; break;
                case 37: op = rv_op_vsll_vi; break;
                case 39:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: op = rv_op_vmv1r_v; break;
                    case 1: op = rv_op_vmv2r_v; break;
                    case 3: op = rv_op_vmv4r_v; break;
                    case 7: op = rv_op_vmv8r_v; break;
                    }
                    break;
                case 40: op = rv_op_vsrl_vi; break;
                case 41: op = rv_op_vsra_vi; break;
                case 42: op = rv_op_vssrl_vi; break;
                case 43: op = rv_op_vssra_vi; break;
                case 44: op = rv_op_vnsrl_wi; break;
                case 45: op = rv_op_vnsra_wi; break;
                case 46: op = rv_op_vnclipu_wi; break;
                case 47: op = rv_op_vnclip_wi; break;
                case 53: op = rv_op_vwsll_vi; break;
                }
                break;
            case 4:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vadd_vx; break;
                case 1: op = rv_op_vandn_vx; break;
                case 2: op = rv_op_vsub_vx; break;
                case 3: op = rv_op_vrsub_vx; break;
                case 4: op = rv_op_vminu_vx; break;
                case 5: op = rv_op_vmin_vx; break;
                case 6: op = rv_op_vmaxu_vx; break;
                case 7: op = rv_op_vmax_vx; break;
                case 9: op = rv_op_vand_vx; break;
                case 10: op = rv_op_vor_vx; break;
                case 11: op = rv_op_vxor_vx; break;
                case 12: op = rv_op_vrgather_vx; break;
                case 14: op = rv_op_vslideup_vx; break;
                case 15: op = rv_op_vslidedown_vx; break;
                case 16:
                    if (((inst >> 25) & 1) == 0) {
                        op = rv_op_vadc_vxm;
                    }
                    break;
                case 17: op = rv_op_vmadc_vxm; break;
                case 18:
                    if (((inst >> 25) & 1) == 0) {
                        op = rv_op_vsbc_vxm;
                    }
                    break;
                case 19: op = rv_op_vmsbc_vxm; break;
                case 20: op = rv_op_vror_vx; break;
                case 21: op = rv_op_vrol_vx; break;
                case 23:
                    if (((inst >> 20) & 0b111111) == 32)
                        op = rv_op_vmv_v_x;
                    else if (((inst >> 25) & 1) == 0)
                        op = rv_op_vmerge_vxm;
                    break;
                case 24: op = rv_op_vmseq_vx; break;
                case 25: op = rv_op_vmsne_vx; break;
                case 26: op = rv_op_vmsltu_vx; break;
                case 27: op = rv_op_vmslt_vx; break;
                case 28: op = rv_op_vmsleu_vx; break;
                case 29: op = rv_op_vmsle_vx; break;
                case 30: op = rv_op_vmsgtu_vx; break;
                case 31: op = rv_op_vmsgt_vx; break;
                case 32: op = rv_op_vsaddu_vx; break;
                case 33: op = rv_op_vsadd_vx; break;
                case 34: op = rv_op_vssubu_vx; break;
                case 35: op = rv_op_vssub_vx; break;
                case 37: op = rv_op_vsll_vx; break;
                case 39: op = rv_op_vsmul_vx; break;
                case 40: op = rv_op_vsrl_vx; break;
                case 41: op = rv_op_vsra_vx; break;
                case 42: op = rv_op_vssrl_vx; break;
                case 43: op = rv_op_vssra_vx; break;
                case 44: op = rv_op_vnsrl_wx; break;
                case 45: op = rv_op_vnsra_wx; break;
                case 46: op = rv_op_vnclipu_wx; break;
                case 47: op = rv_op_vnclip_wx; break;
                case 53: op = rv_op_vwsll_vx; break;
                }
                break;
            case 5:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vfadd_vf; break;
                case 2: op = rv_op_vfsub_vf; break;
                case 4: op = rv_op_vfmin_vf; break;
                case 6: op = rv_op_vfmax_vf; break;
                case 8: op = rv_op_vfsgnj_vf; break;
                case 9: op = rv_op_vfsgnjn_vf; break;
                case 10: op = rv_op_vfsgnjx_vf; break;
                case 14: op = rv_op_vfslide1up_vf; break;
                case 15: op = rv_op_vfslide1down_vf; break;
                case 16:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: if ((inst >> 25) & 1) op = rv_op_vfmv_s_f; break;
                    }
                    break;
                case 23:
                    if (((inst >> 25) & 1) == 0)
                        op = rv_op_vfmerge_vfm;
                    else if (((inst >> 20) & 0b111111) == 32)
                        op = rv_op_vfmv_v_f;
                    break;
                case 24: op = rv_op_vmfeq_vf; break;
                case 25: op = rv_op_vmfle_vf; break;
                case 27: op = rv_op_vmflt_vf; break;
                case 28: op = rv_op_vmfne_vf; break;
                case 29: op = rv_op_vmfgt_vf; break;
                case 31: op = rv_op_vmfge_vf; break;
                case 32: op = rv_op_vfdiv_vf; break;
                case 33: op = rv_op_vfrdiv_vf; break;
                case 36: op = rv_op_vfmul_vf; break;
                case 39: op = rv_op_vfrsub_vf; break;
                case 40: op = rv_op_vfmadd_vf; break;
                case 41: op = rv_op_vfnmadd_vf; break;
                case 42: op = rv_op_vfmsub_vf; break;
                case 43: op = rv_op_vfnmsub_vf; break;
                case 44: op = rv_op_vfmacc_vf; break;
                case 45: op = rv_op_vfnmacc_vf; break;
                case 46: op = rv_op_vfmsac_vf; break;
                case 47: op = rv_op_vfnmsac_vf; break;
                case 48: op = rv_op_vfwadd_vf; break;
                case 50: op = rv_op_vfwsub_vf; break;
                case 52: op = rv_op_vfwadd_wf; break;
                case 54: op = rv_op_vfwsub_wf; break;
                case 56: op = rv_op_vfwmul_vf; break;
                case 59: op = rv_op_vfwmaccbf16_vf; break;
                case 60: op = rv_op_vfwmacc_vf; break;
                case 61: op = rv_op_vfwnmacc_vf; break;
                case 62: op = rv_op_vfwmsac_vf; break;
                case 63: op = rv_op_vfwnmsac_vf; break;
                }
                break;
            case 6:
                switch ((inst >> 26) & 0b111111) {
                case 8: op = rv_op_vaaddu_vx; break;
                case 9: op = rv_op_vaadd_vx; break;
                case 10: op = rv_op_vasubu_vx; break;
                case 11: op = rv_op_vasub_vx; break;
                case 12: op = rv_op_vclmul_vx; break;
                case 13: op = rv_op_vclmulh_vx; break;
                case 14: op = rv_op_vslide1up_vx; break;
                case 15: op = rv_op_vslide1down_vx; break;
                case 16:
                    switch ((inst >> 20) & 0b11111) {
                    case 0: if ((inst >> 25) & 1) op = rv_op_vmv_s_x; break;
                    }
                    break;
                case 32: op = rv_op_vdivu_vx; break;
                case 33: op = rv_op_vdiv_vx; break;
                case 34: op = rv_op_vremu_vx; break;
                case 35: op = rv_op_vrem_vx; break;
                case 36: op = rv_op_vmulhu_vx; break;
                case 37: op = rv_op_vmul_vx; break;
                case 38: op = rv_op_vmulhsu_vx; break;
                case 39: op = rv_op_vmulh_vx; break;
                case 41: op = rv_op_vmadd_vx; break;
                case 43: op = rv_op_vnmsub_vx; break;
                case 45: op = rv_op_vmacc_vx; break;
                case 47: op = rv_op_vnmsac_vx; break;
                case 48: op = rv_op_vwaddu_vx; break;
                case 49: op = rv_op_vwadd_vx; break;
                case 50: op = rv_op_vwsubu_vx; break;
                case 51: op = rv_op_vwsub_vx; break;
                case 52: op = rv_op_vwaddu_wx; break;
                case 53: op = rv_op_vwadd_wx; break;
                case 54: op = rv_op_vwsubu_wx; break;
                case 55: op = rv_op_vwsub_wx; break;
                case 56: op = rv_op_vwmulu_vx; break;
                case 58: op = rv_op_vwmulsu_vx; break;
                case 59: op = rv_op_vwmul_vx; break;
                case 60: op = rv_op_vwmaccu_vx; break;
                case 61: op = rv_op_vwmacc_vx; break;
                case 62: op = rv_op_vwmaccus_vx; break;
                case 63: op = rv_op_vwmaccsu_vx; break;
                }
                break;
            case 7:
                if (((inst >> 31) & 1) == 0) {
                    op = rv_op_vsetvli;
                } else if ((inst >> 30) & 1) {
                    op = rv_op_vsetivli;
                } else if (((inst >> 25) & 0b11111) == 0) {
                    op = rv_op_vsetvl;
                }
                break;
            }
            break;
        case 22:
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_addid; break;
            case 1:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_sllid; break;
                }
                break;
            case 5:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_srlid; break;
                case 16: op = rv_op_sraid; break;
                }
                break;
            }
            break;
        case 24:
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_beq; break;
            case 1: op = rv_op_bne; break;
            case 4: op = rv_op_blt; break;
            case 5: op = rv_op_bge; break;
            case 6: op = rv_op_bltu; break;
            case 7: op = rv_op_bgeu; break;
            }
            break;
        case 25:
            switch ((inst >> 12) & 0b111) {
            case 0: op = rv_op_jalr; break;
            }
            break;
        case 27: op = rv_op_jal; break;
        case 28:
            switch ((inst >> 12) & 0b111) {
            case 0:
                switch (((inst >> 20) & 0b111111100000) |
                        ((inst >> 7) & 0b000000011111)) {
                case 0:
                    switch ((inst >> 15) & 0b1111111111) {
                    case 0: op = rv_op_ecall; break;
                    case 32: op = rv_op_ebreak; break;
                    case 64: op = rv_op_uret; break;
                    case 416: op = rv_op_wrs_nto; break;
                    case 928: op = rv_op_wrs_sto; break;
                    }
                    break;
                case 256:
                    switch ((inst >> 20) & 0b11111) {
                    case 2:
                        switch ((inst >> 15) & 0b11111) {
                        case 0: op = rv_op_sret; break;
                        }
                        break;
                    case 4: op = rv_op_sfence_vm; break;
                    case 5:
                        switch ((inst >> 15) & 0b11111) {
                        case 0: op = rv_op_wfi; break;
                        }
                        break;
                    }
                    break;
                case 288: op = rv_op_sfence_vma; break;
                case 512:
                    switch ((inst >> 15) & 0b1111111111) {
                    case 64: op = rv_op_hret; break;
                    }
                    break;
                case 768:
                    switch ((inst >> 15) & 0b1111111111) {
                    case 64: op = rv_op_mret; break;
                    }
                    break;
                case 1792:
                    switch ((inst >> 15) & 0b1111111111) {
                    case 64: op = rv_op_mnret; break;
                    }
                    break;
                case 1952:
                    switch ((inst >> 15) & 0b1111111111) {
                    case 576: op = rv_op_dret; break;
                    }
                    break;
                }
                break;
            case 1:
                switch (operand_csr12(inst)) {
                case 1: op = rv_op_fsflags; break;
                case 2: op = rv_op_fsrm; break;
                case 3: op = rv_op_fscsr; break;
                default: op = rv_op_csrrw; break;
                }
                break;
            case 2:
                op = rv_op_csrrs;
                if (operand_rs1(inst) == 0) {
                    switch (operand_csr12(inst)) {
                    case 0x001: op = rv_op_frflags; break;
                    case 0x002: op = rv_op_frrm; break;
                    case 0x003: op = rv_op_frcsr; break;
                    case 0xc00: op = rv_op_rdcycle; break;
                    case 0xc01: op = rv_op_rdtime; break;
                    case 0xc02: op = rv_op_rdinstret; break;
                    case 0xc80: op = rv_op_rdcycleh; break;
                    case 0xc81: op = rv_op_rdtimeh; break;
                    case 0xc82: op = rv_op_rdinstreth; break;
                    }
                }
                break;
            case 3: op = rv_op_csrrc; break;
            case 4:
                if (dec->cfg && dec->cfg->ext_zimop) {
                    int imm_mop5, imm_mop3, reg_num;
                    if ((extract32(inst, 22, 10) & 0b1011001111)
                        == 0b1000000111) {
                        imm_mop5 = deposit32(deposit32(extract32(inst, 20, 2),
                                                       2, 2,
                                                       extract32(inst, 26, 2)),
                                             4, 1, extract32(inst, 30, 1));
                        op = rv_mop_r_0 + imm_mop5;
                        /* if zicfiss enabled and mop5 is shadow stack */
                        if (dec->cfg->ext_zicfiss &&
                            ((imm_mop5 & 0b11100) == 0b11100)) {
                                /* rs1=0 means ssrdp */
                                if ((inst & (0b011111 << 15)) == 0) {
                                    op = rv_op_ssrdp;
                                }
                                /* rd=0 means sspopchk */
                                reg_num = (inst >> 15) & 0b011111;
                                if (((inst & (0b011111 << 7)) == 0) &&
                                    ((reg_num == 1) || (reg_num == 5))) {
                                    op = rv_op_sspopchk;
                                }
                        }
                    } else if ((extract32(inst, 25, 7) & 0b1011001)
                               == 0b1000001) {
                        imm_mop3 = deposit32(extract32(inst, 26, 2),
                                             2, 1, extract32(inst, 30, 1));
                        op = rv_mop_rr_0 + imm_mop3;
                        /* if zicfiss enabled and mop3 is shadow stack */
                        if (dec->cfg->ext_zicfiss &&
                            ((imm_mop3 & 0b111) == 0b111)) {
                                /* rs1=0 and rd=0 means sspush */
                                reg_num = (inst >> 20) & 0b011111;
                                if (((inst & (0b011111 << 15)) == 0) &&
                                    ((inst & (0b011111 << 7)) == 0) &&
                                    ((reg_num == 1) || (reg_num == 5))) {
                                    op = rv_op_sspush;
                                }
                        }
                    }
                }
                break;
            case 5:
                switch (operand_csr12(inst)) {
                case 1: op = rv_op_fsflagsi; break;
                case 2: op = rv_op_fsrmi; break;
                default: op = rv_op_csrrwi; break;
                }
                break;
            case 6: op = rv_op_csrrsi; break;
            case 7: op = rv_op_csrrci; break;
            }
            break;
        case 29:
            if (((inst >> 25) & 1) == 1 && ((inst >> 12) & 0b111) == 2) {
                switch ((inst >> 26) & 0b111111) {
                case 32: op = rv_op_vsm3me_vv; break;
                case 33: op = rv_op_vsm4k_vi; break;
                case 34: op = rv_op_vaeskf1_vi; break;
                case 40:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: op = rv_op_vaesdm_vv; break;
                    case 1: op = rv_op_vaesdf_vv; break;
                    case 2: op = rv_op_vaesem_vv; break;
                    case 3: op = rv_op_vaesef_vv; break;
                    case 16: op = rv_op_vsm4r_vv; break;
                    case 17: op = rv_op_vgmul_vv; break;
                    }
                    break;
                case 41:
                    switch ((inst >> 15) & 0b11111) {
                    case 0: op = rv_op_vaesdm_vs; break;
                    case 1: op = rv_op_vaesdf_vs; break;
                    case 2: op = rv_op_vaesem_vs; break;
                    case 3: op = rv_op_vaesef_vs; break;
                    case 7: op = rv_op_vaesz_vs; break;
                    case 16: op = rv_op_vsm4r_vs; break;
                    }
                    break;
                case 42: op = rv_op_vaeskf2_vi; break;
                case 43: op = rv_op_vsm3c_vi; break;
                case 44: op = rv_op_vghsh_vv; break;
                case 45: op = rv_op_vsha2ms_vv; break;
                case 46: op = rv_op_vsha2ch_vv; break;
                case 47: op = rv_op_vsha2cl_vv; break;
                }
            }
            break;
        case 30:
            switch (((inst >> 22) & 0b1111111000) |
                    ((inst >> 12) & 0b0000000111)) {
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

    return op == rv_op_illegal ? NULL : &rvi_opcode_data[op];
}

/* decode operands */

static void decode_inst_operands(rv_decode *dec, rv_isa isa,
                                 const rv_opcode_data *op)
{
    rv_inst inst = dec->inst;

    switch (op->codec) {
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
        dec->imm = operand_cimmshr6(inst, isa);
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
        dec->imm = operand_cimmshl6(inst, isa);
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
    case rv_codec_k_bs:
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->bs = operand_bs(inst);
        break;
    case rv_codec_k_rnum:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rnum = operand_rnum(inst);
        break;
    case rv_codec_v_r:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->vm = operand_vm(inst);
        break;
    case rv_codec_v_ldst:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->vm = operand_vm(inst);
        break;
    case rv_codec_v_i:
        dec->rd = operand_rd(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = operand_vimm(inst);
        dec->vm = operand_vm(inst);
        break;
    case rv_codec_v_i_u:
        dec->rd = operand_rd(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = operand_vuimm(inst);
        dec->vm = operand_vm(inst);
        break;
    case rv_codec_vror_vi:
        dec->rd = operand_rd(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = operand_vzimm6(inst);
        dec->vm = operand_vm(inst);
        break;
    case rv_codec_vsetvli:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->vzimm = operand_vzimm11(inst);
        break;
    case rv_codec_vsetivli:
        dec->rd = operand_rd(inst);
        dec->imm = extract32(inst, 15, 5);
        dec->vzimm = operand_vzimm10(inst);
        break;
    case rv_codec_zcb_lb:
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        dec->imm = operand_uimm_c_lb(inst);
        break;
    case rv_codec_zcb_lh:
        dec->rs1 = operand_crs1q(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        dec->imm = operand_uimm_c_lh(inst);
        break;
    case rv_codec_zcb_ext:
        dec->rd = operand_crs1q(inst) + 8;
        break;
    case rv_codec_zcb_mul:
        dec->rd = operand_crs1rdq(inst) + 8;
        dec->rs2 = operand_crs2q(inst) + 8;
        break;
    case rv_codec_zcmp_cm_pushpop:
        dec->imm = operand_zcmp_stack_adj(inst, isa);
        dec->rlist = operand_zcmp_rlist(inst);
        break;
    case rv_codec_zcmp_cm_mv:
        dec->rd = operand_sreg1(inst);
        dec->rs2 = operand_sreg2(inst);
        break;
    case rv_codec_zcmt_jt:
        dec->imm = operand_tbl_index(inst);
        break;
    case rv_codec_fli:
        dec->rd = operand_rd(inst);
        dec->imm = operand_rs1(inst);
        break;
    case rv_codec_r2_imm5:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->imm = operand_rs2(inst);
        break;
    case rv_codec_r2:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        break;
    case rv_codec_r2_imm6:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->imm = operand_imm6(inst);
        break;
    case rv_codec_r_imm2:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->rs2 = operand_rs2(inst);
        dec->imm = operand_imm2(inst);
        break;
    case rv_codec_r2_immhl:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->imm = operand_immh(inst);
        dec->imm1 = operand_imml(inst);
        break;
    case rv_codec_r2_imm2_imm5:
        dec->rd = operand_rd(inst);
        dec->rs1 = operand_rs1(inst);
        dec->imm = sextract32(operand_rs2(inst), 0, 5);
        dec->imm1 = operand_imm2(inst);
        break;
    case rv_codec_lp:
        dec->imm = operand_lpl(inst);
        break;
    case rv_codec_cmop_ss:
        dec->rd = rv_ireg_zero;
        dec->rs1 = dec->rs2 = operand_crs1(inst);
        dec->imm = 0;
        break;
    default:
        g_assert_not_reached();
    }
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
        default: break;
        }
        c++;
    }
    return true;
}

/* Same as insn_len() from target/riscv/internals.h */
static size_t inst_length(rv_inst inst)
{
    return (inst & 3) == 3 ? 4 : 2;
}

/* format instruction */

static GString *format_inst(size_t tab, rv_decode *dec,
                            const rv_opcode_data *op)
{
    GString *buf = g_string_sized_new(64);
    const char *fmt = op->format;

    while (*fmt) {
        switch (*fmt) {
        case 'O':
            g_string_append(buf, op->name);
            break;
        case '(':
        case ',':
        case ')':
        case '-':
            g_string_append_c(buf, *fmt);
            break;
        case 'b':
            g_string_append_printf(buf, "%d", dec->bs);
            break;
        case 'n':
            g_string_append_printf(buf, "%d", dec->rnum);
            break;
        case '0':
            g_string_append(buf, rv_ireg_name_sym[dec->rd]);
            break;
        case '1':
            g_string_append(buf, rv_ireg_name_sym[dec->rs1]);
            break;
        case '2':
            g_string_append(buf, rv_ireg_name_sym[dec->rs2]);
            break;
        case '3':
            if (dec->cfg && dec->cfg->ext_zfinx) {
                g_string_append(buf, rv_ireg_name_sym[dec->rd]);
            } else {
                g_string_append(buf, rv_freg_name_sym[dec->rd]);
            }
            break;
        case '4':
            if (dec->cfg && dec->cfg->ext_zfinx) {
                g_string_append(buf, rv_ireg_name_sym[dec->rs1]);
            } else {
                g_string_append(buf, rv_freg_name_sym[dec->rs1]);
            }
            break;
        case '5':
            if (dec->cfg && dec->cfg->ext_zfinx) {
                g_string_append(buf, rv_ireg_name_sym[dec->rs2]);
            } else {
                g_string_append(buf, rv_freg_name_sym[dec->rs2]);
            }
            break;
        case '6':
            if (dec->cfg && dec->cfg->ext_zfinx) {
                g_string_append(buf, rv_ireg_name_sym[dec->rs3]);
            } else {
                g_string_append(buf, rv_freg_name_sym[dec->rs3]);
            }
            break;
        case '7':
            g_string_append_printf(buf, "%d", dec->rs1);
            break;
        case 'i':
            g_string_append_printf(buf, "%d", dec->imm);
            break;
        case 'u':
            g_string_append_printf(buf, "%u", ((uint32_t)dec->imm & 0b111111));
            break;
        case 'j':
            g_string_append_printf(buf, "%d", dec->imm1);
            break;
        case 'o':
            g_string_append_printf(buf, "%d", dec->imm);
            while (buf->len < tab * 2) {
                g_string_append_c(buf, ' ');
            }
            g_string_append_printf(buf, "# 0x%" PRIx64, dec->pc + dec->imm);
            break;
        case 'U':
            fmt++;
            g_string_append_printf(buf, "%d", dec->imm >> 12);
            if (*fmt == 'o') {
                while (buf->len < tab * 2) {
                    g_string_append_c(buf, ' ');
                }
                g_string_append_printf(buf, "# 0x%" PRIx64, dec->pc + dec->imm);
            }
            break;
        case 'c': {
            const char *name = csr_name(dec->imm & 0xfff);
            if (name) {
                g_string_append(buf, name);
            } else {
                g_string_append_printf(buf, "0x%03x", dec->imm & 0xfff);
            }
            break;
        }
        case 'r':
            switch (dec->rm) {
            case rv_rm_rne:
                g_string_append(buf, "rne");
                break;
            case rv_rm_rtz:
                g_string_append(buf, "rtz");
                break;
            case rv_rm_rdn:
                g_string_append(buf, "rdn");
                break;
            case rv_rm_rup:
                g_string_append(buf, "rup");
                break;
            case rv_rm_rmm:
                g_string_append(buf, "rmm");
                break;
            case rv_rm_dyn:
                g_string_append(buf, "dyn");
                break;
            default:
                g_string_append(buf, "inv");
                break;
            }
            break;
        case 'p':
            if (dec->pred & rv_fence_i) {
                g_string_append_c(buf, 'i');
            }
            if (dec->pred & rv_fence_o) {
                g_string_append_c(buf, 'o');
            }
            if (dec->pred & rv_fence_r) {
                g_string_append_c(buf, 'r');
            }
            if (dec->pred & rv_fence_w) {
                g_string_append_c(buf, 'w');
            }
            break;
        case 's':
            if (dec->succ & rv_fence_i) {
                g_string_append_c(buf, 'i');
            }
            if (dec->succ & rv_fence_o) {
                g_string_append_c(buf, 'o');
            }
            if (dec->succ & rv_fence_r) {
                g_string_append_c(buf, 'r');
            }
            if (dec->succ & rv_fence_w) {
                g_string_append_c(buf, 'w');
            }
            break;
        case '\t':
            while (buf->len < tab) {
                g_string_append_c(buf, ' ');
            }
            break;
        case 'A':
            if (dec->aq) {
                g_string_append(buf, ".aq");
            }
            break;
        case 'R':
            if (dec->rl) {
                g_string_append(buf, ".rl");
            }
            break;
        case 'l':
            g_string_append(buf, ",v0");
            break;
        case 'm':
            if (dec->vm == 0) {
                g_string_append(buf, ",v0.t");
            }
            break;
        case 'D':
            g_string_append(buf, rv_vreg_name_sym[dec->rd]);
            break;
        case 'E':
            g_string_append(buf, rv_vreg_name_sym[dec->rs1]);
            break;
        case 'F':
            g_string_append(buf, rv_vreg_name_sym[dec->rs2]);
            break;
        case 'G':
            g_string_append(buf, rv_vreg_name_sym[dec->rs3]);
            break;
        case 'v': {
            const int sew = 1 << (((dec->vzimm >> 3) & 0b111) + 3);
            const int lmul = dec->vzimm & 0b11;
            const int flmul = (dec->vzimm >> 2) & 1;
            const char *vta = (dec->vzimm >> 6) & 1 ? "ta" : "tu";
            const char *vma = (dec->vzimm >> 7) & 1 ? "ma" : "mu";

            g_string_append_printf(buf, "e%d,m", sew);
            if (flmul) {
                switch (lmul) {
                case 3:
                    g_string_append(buf, "f2");
                    break;
                case 2:
                    g_string_append(buf, "f4");
                    break;
                case 1:
                    g_string_append(buf, "f8");
                    break;
                }
            } else {
                g_string_append_printf(buf, "%d", 1 << lmul);
            }
            g_string_append_c(buf, ',');
            g_string_append(buf, vta);
            g_string_append_c(buf, ',');
            g_string_append(buf, vma);
            break;
        }
        case 'x': {
            switch (dec->rlist) {
            case 4:
                g_string_append(buf, "{ra}");
                break;
            case 5:
                g_string_append(buf, "{ra, s0}");
                break;
            case 15:
                g_string_append(buf, "{ra, s0-s11}");
                break;
            default:
                g_string_append_printf(buf, "{ra, s0-s%d}", dec->rlist - 5);
                break;
            }
            break;
        }
        case 'h':
            g_string_append(buf, rv_fli_name_const[dec->imm]);
            break;
        default:
            break;
        }
        fmt++;
    }

    return buf;
}

/* lift instruction to pseudo-instruction */

static const rv_opcode_data *decode_inst_lift_pseudo(rv_decode *dec,
                                                     const rv_opcode_data *op)
{
    const rv_comp_data *comp_data = op->pseudo;
    if (comp_data) {
        while (comp_data->constraints) {
            if (check_constraints(dec, comp_data->constraints)) {
                assert(op != comp_data->op);
                return decode_inst_lift_pseudo(dec, comp_data->op);
            }
            comp_data++;
        }
    }
    return op;
}

/* disassemble instruction */

static GString *disasm_inst(rv_isa isa, uint64_t pc, rv_inst inst,
                            const RISCVCPUConfig *cfg)
{
    rv_decode dec = {
        .pc = pc,
        .inst = inst,
        .cfg = cfg,
    };
    const rv_opcode_data *op = decode_inst_opcode(&dec, isa);

    if (!op && cfg) {
        static const struct {
            bool (*guard_func)(const RISCVCPUConfig *);
            const rv_opcode_data *(*decode_func)(rv_decode *, rv_isa);
        } decoders[] = {
            { has_xtheadba_p, decode_xtheadba },
            { has_xtheadbb_p, decode_xtheadbb },
            { has_xtheadbs_p, decode_xtheadbs },
            { has_xtheadcmo_p, decode_xtheadcmo },
            { has_xtheadcondmov_p, decode_xtheadcondmov },
            { has_xtheadfmemidx_p, decode_xtheadfmemidx },
            { has_xtheadfmv_p, decode_xtheadfmv },
            { has_xtheadmac_p, decode_xtheadmac },
            { has_xtheadmemidx_p, decode_xtheadmemidx },
            { has_xtheadmempair_p, decode_xtheadmempair },
            { has_xtheadsync_p, decode_xtheadsync },
            { has_XVentanaCondOps_p, decode_xventanacondops },
            { has_xlrbr_p, decode_xlrbr },
        };

        for (size_t i = 0; i < ARRAY_SIZE(decoders); i++) {
            if (decoders[i].guard_func(cfg)) {
                op = decoders[i].decode_func(&dec, isa);
                if (op) {
                    break;
                }
            }
        }
    }

    if (op) {
        decode_inst_operands(&dec, isa, op);
        op = decode_inst_lift_pseudo(&dec, op);
    } else {
        op = &rvi_opcode_data[rv_op_illegal];
    }

    return format_inst(24, &dec, op);
}

#define INST_FMT_2 "%04x              "
#define INST_FMT_4 "%08x          "

static int
print_insn_riscv(bfd_vma memaddr, struct disassemble_info *info, rv_isa isa)
{
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

    if (info->show_opcodes) {
        switch (len) {
        case 2:
            (*info->fprintf_func)(info->stream, INST_FMT_2, inst);
            break;
        case 4:
            (*info->fprintf_func)(info->stream, INST_FMT_4, inst);
            break;
        default:
            g_assert_not_reached();
        }
    }

    g_autoptr(GString) str =
        disasm_inst(isa, memaddr, inst,
                    (const RISCVCPUConfig *)info->target_info);
    (*info->fprintf_func)(info->stream, "%s", str->str);

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
