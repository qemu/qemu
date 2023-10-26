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

static const rvc_constraint rvcc_jal[] = { rvc_rd_eq_ra, rvc_end };
static const rvc_constraint rvcc_jalr[] = { rvc_rd_eq_ra, rvc_imm_eq_zero,
                                            rvc_end };
static const rvc_constraint rvcc_nop[] = { rvc_rd_eq_x0, rvc_rs1_eq_x0,
                                           rvc_imm_eq_zero, rvc_end };
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
static const rvc_constraint rvcc_ret[] = { rvc_rd_eq_x0, rvc_rs1_eq_ra,
                                           rvc_end };
static const rvc_constraint rvcc_jr[] = { rvc_rd_eq_x0, rvc_imm_eq_zero,
                                          rvc_end };
static const rvc_constraint rvcc_rdcycle[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc00,
                                               rvc_end };
static const rvc_constraint rvcc_rdtime[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc01,
                                              rvc_end };
static const rvc_constraint rvcc_rdinstret[] = { rvc_rs1_eq_x0,
                                                 rvc_csr_eq_0xc02, rvc_end };
static const rvc_constraint rvcc_rdcycleh[] = { rvc_rs1_eq_x0,
                                                rvc_csr_eq_0xc80, rvc_end };
static const rvc_constraint rvcc_rdtimeh[] = { rvc_rs1_eq_x0, rvc_csr_eq_0xc81,
                                               rvc_end };
static const rvc_constraint rvcc_rdinstreth[] = { rvc_rs1_eq_x0,
                                                  rvc_csr_eq_0xc82, rvc_end };
static const rvc_constraint rvcc_frcsr[] = { rvc_rs1_eq_x0, rvc_csr_eq_0x003,
                                             rvc_end };
static const rvc_constraint rvcc_frrm[] = { rvc_rs1_eq_x0, rvc_csr_eq_0x002,
                                            rvc_end };
static const rvc_constraint rvcc_frflags[] = { rvc_rs1_eq_x0, rvc_csr_eq_0x001,
                                               rvc_end };
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

const rv_opcode_data rvi_opcode_data[] = {
    { "illegal", rv_codec_illegal, rv_fmt_none, NULL, 0, 0, 0 },
    { "lui", rv_codec_u, rv_fmt_rd_uimm, NULL, 0, 0, 0 },
    { "auipc", rv_codec_u, rv_fmt_rd_uoffset, NULL, 0, 0, 0 },
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
    { "c.fld", rv_codec_cl_ld, rv_fmt_frd_offset_rs1, NULL, rv_op_fld,
      rv_op_fld, 0 },
    { "c.lw", rv_codec_cl_lw, rv_fmt_rd_offset_rs1, NULL, rv_op_lw, rv_op_lw,
      rv_op_lw },
    { "c.flw", rv_codec_cl_lw, rv_fmt_frd_offset_rs1, NULL, rv_op_flw, 0, 0 },
    { "c.fsd", rv_codec_cs_sd, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsd,
      rv_op_fsd, 0 },
    { "c.sw", rv_codec_cs_sw, rv_fmt_rs2_offset_rs1, NULL, rv_op_sw, rv_op_sw,
      rv_op_sw },
    { "c.fsw", rv_codec_cs_sw, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsw, 0, 0 },
    { "c.nop", rv_codec_ci_none, rv_fmt_none, NULL, rv_op_addi, rv_op_addi,
      rv_op_addi },
    { "c.addi", rv_codec_ci, rv_fmt_rd_rs1_imm, NULL, rv_op_addi, rv_op_addi,
      rv_op_addi, rvcd_imm_nz },
    { "c.jal", rv_codec_cj_jal, rv_fmt_rd_offset, NULL, rv_op_jal, 0, 0 },
    { "c.li", rv_codec_ci_li, rv_fmt_rd_rs1_imm, NULL, rv_op_addi, rv_op_addi,
      rv_op_addi },
    { "c.addi16sp", rv_codec_ci_16sp, rv_fmt_rd_rs1_imm, NULL, rv_op_addi,
      rv_op_addi, rv_op_addi, rvcd_imm_nz },
    { "c.lui", rv_codec_ci_lui, rv_fmt_rd_uimm, NULL, rv_op_lui, rv_op_lui,
      rv_op_lui, rvcd_imm_nz },
    { "c.srli", rv_codec_cb_sh6, rv_fmt_rd_rs1_imm, NULL, rv_op_srli,
      rv_op_srli, rv_op_srli, rvcd_imm_nz },
    { "c.srai", rv_codec_cb_sh6, rv_fmt_rd_rs1_imm, NULL, rv_op_srai,
      rv_op_srai, rv_op_srai, rvcd_imm_nz },
    { "c.andi", rv_codec_cb_imm, rv_fmt_rd_rs1_imm, NULL, rv_op_andi,
      rv_op_andi, rv_op_andi },
    { "c.sub", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_sub, rv_op_sub,
      rv_op_sub },
    { "c.xor", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_xor, rv_op_xor,
      rv_op_xor },
    { "c.or", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_or, rv_op_or,
      rv_op_or },
    { "c.and", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_and, rv_op_and,
      rv_op_and },
    { "c.subw", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_subw, rv_op_subw,
      rv_op_subw },
    { "c.addw", rv_codec_cs, rv_fmt_rd_rs1_rs2, NULL, rv_op_addw, rv_op_addw,
      rv_op_addw },
    { "c.j", rv_codec_cj, rv_fmt_rd_offset, NULL, rv_op_jal, rv_op_jal,
      rv_op_jal },
    { "c.beqz", rv_codec_cb, rv_fmt_rs1_rs2_offset, NULL, rv_op_beq, rv_op_beq,
      rv_op_beq },
    { "c.bnez", rv_codec_cb, rv_fmt_rs1_rs2_offset, NULL, rv_op_bne, rv_op_bne,
      rv_op_bne },
    { "c.slli", rv_codec_ci_sh6, rv_fmt_rd_rs1_imm, NULL, rv_op_slli,
      rv_op_slli, rv_op_slli, rvcd_imm_nz },
    { "c.fldsp", rv_codec_ci_ldsp, rv_fmt_frd_offset_rs1, NULL, rv_op_fld,
      rv_op_fld, rv_op_fld },
    { "c.lwsp", rv_codec_ci_lwsp, rv_fmt_rd_offset_rs1, NULL, rv_op_lw,
      rv_op_lw, rv_op_lw },
    { "c.flwsp", rv_codec_ci_lwsp, rv_fmt_frd_offset_rs1, NULL, rv_op_flw, 0,
      0 },
    { "c.jr", rv_codec_cr_jr, rv_fmt_rd_rs1_offset, NULL, rv_op_jalr,
      rv_op_jalr, rv_op_jalr },
    { "c.mv", rv_codec_cr_mv, rv_fmt_rd_rs1_rs2, NULL, rv_op_addi, rv_op_addi,
      rv_op_addi },
    { "c.ebreak", rv_codec_ci_none, rv_fmt_none, NULL, rv_op_ebreak,
      rv_op_ebreak, rv_op_ebreak },
    { "c.jalr", rv_codec_cr_jalr, rv_fmt_rd_rs1_offset, NULL, rv_op_jalr,
      rv_op_jalr, rv_op_jalr },
    { "c.add", rv_codec_cr, rv_fmt_rd_rs1_rs2, NULL, rv_op_add, rv_op_add,
      rv_op_add },
    { "c.fsdsp", rv_codec_css_sdsp, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsd,
      rv_op_fsd, rv_op_fsd },
    { "c.swsp", rv_codec_css_swsp, rv_fmt_rs2_offset_rs1, NULL, rv_op_sw,
      rv_op_sw, rv_op_sw },
    { "c.fswsp", rv_codec_css_swsp, rv_fmt_frs2_offset_rs1, NULL, rv_op_fsw, 0,
      0 },
    { "c.ld", rv_codec_cl_ld, rv_fmt_rd_offset_rs1, NULL, 0, rv_op_ld,
      rv_op_ld },
    { "c.sd", rv_codec_cs_sd, rv_fmt_rs2_offset_rs1, NULL, 0, rv_op_sd,
      rv_op_sd },
    { "c.addiw", rv_codec_ci, rv_fmt_rd_rs1_imm, NULL, 0, rv_op_addiw,
      rv_op_addiw },
    { "c.ldsp", rv_codec_ci_ldsp, rv_fmt_rd_offset_rs1, NULL, 0, rv_op_ld,
      rv_op_ld },
    { "c.sdsp", rv_codec_css_sdsp, rv_fmt_rs2_offset_rs1, NULL, 0, rv_op_sd,
      rv_op_sd },
    { "c.lq", rv_codec_cl_lq, rv_fmt_rd_offset_rs1, NULL, 0, 0, rv_op_lq },
    { "c.sq", rv_codec_cs_sq, rv_fmt_rs2_offset_rs1, NULL, 0, 0, rv_op_sq },
    { "c.lqsp", rv_codec_ci_lqsp, rv_fmt_rd_offset_rs1, NULL, 0, 0, rv_op_lq },
    { "c.sqsp", rv_codec_css_sqsp, rv_fmt_rs2_offset_rs1, NULL, 0, 0,
      rv_op_sq },
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
    { "fmv.s", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fabs.s", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fneg.s", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fmv.d", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fabs.d", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fneg.d", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fmv.q", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fabs.q", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
    { "fneg.q", rv_codec_r, rv_fmt_frd_frs1, NULL, 0, 0, 0 },
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
    { "xnor", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "orn", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "andn", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
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
    { "ctzw", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "cpopw", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "slli.uw", rv_codec_i_sh6, rv_fmt_rd_rs1_imm, NULL, 0, 0, 0 },
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
    { "aes32esmi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs, NULL, 0, 0, 0 },
    { "aes32esi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs, NULL, 0, 0, 0 },
    { "aes32dsmi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs, NULL, 0, 0, 0 },
    { "aes32dsi", rv_codec_k_bs, rv_fmt_rs1_rs2_bs, NULL, 0, 0, 0 },
    { "aes64ks1i", rv_codec_k_rnum,  rv_fmt_rd_rs1_rnum, NULL, 0, 0, 0 },
    { "aes64ks2", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "aes64im", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "aes64esm", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "aes64es", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "aes64dsm", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "aes64ds", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha256sig0", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "sha256sig1", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "sha256sum0", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "sha256sum1", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "sha512sig0", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sig1", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sum0", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sum1", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sum0r", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sum1r", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sig0l", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sig0h", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sig1l", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sha512sig1h", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "sm3p0", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "sm3p1", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0 },
    { "sm4ed", rv_codec_k_bs, rv_fmt_rs1_rs2_bs, NULL, 0, 0, 0 },
    { "sm4ks", rv_codec_k_bs, rv_fmt_rs1_rs2_bs, NULL, 0, 0, 0 },
    { "brev8", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "pack", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "packh", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "packw", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "unzip", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "zip", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "xperm4", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "xperm8", rv_codec_r, rv_fmt_rd_rs1, NULL, 0, 0, 0 },
    { "vle8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vle16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vle32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vle64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vse8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vse16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vse32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vse64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vlm.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vsm.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vlse8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vlse16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vlse32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vlse64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vsse8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vsse16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vsse32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vsse64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_rs2_vm, NULL, 0, 0, 0 },
    { "vluxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vluxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vluxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vluxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vloxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vloxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vloxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vloxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsuxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsuxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsuxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsuxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsoxei8.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsoxei16.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsoxei32.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vsoxei64.v", rv_codec_v_r, rv_fmt_ldst_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vle8ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vle16ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vle32ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vle64ff.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl1re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl1re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl1re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl1re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl2re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl2re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl2re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl2re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl4re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl4re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl4re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl4re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl8re8.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl8re16.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl8re32.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vl8re64.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vs1r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vs2r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vs4r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vs8r.v", rv_codec_v_ldst, rv_fmt_ldst_vd_rs1_vm, NULL, 0, 0, 0 },
    { "vadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vadd.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vrsub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vrsub.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vwaddu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwaddu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwsubu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwsubu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwsub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwaddu.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwaddu.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwadd.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwadd.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwsubu.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwsubu.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwsub.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwsub.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vadc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl, NULL, 0, 0, 0 },
    { "vadc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl, NULL, 0, 0, 0 },
    { "vadc.vim", rv_codec_v_i, rv_fmt_vd_vs2_imm_vl, NULL, 0, 0, 0 },
    { "vmadc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl, NULL, 0, 0, 0 },
    { "vmadc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl, NULL, 0, 0, 0 },
    { "vmadc.vim", rv_codec_v_i, rv_fmt_vd_vs2_imm_vl, NULL, 0, 0, 0 },
    { "vsbc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl, NULL, 0, 0, 0 },
    { "vsbc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl, NULL, 0, 0, 0 },
    { "vmsbc.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl, NULL, 0, 0, 0 },
    { "vmsbc.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl, NULL, 0, 0, 0 },
    { "vand.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vand.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vand.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vor.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vor.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vor.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vxor.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vxor.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vxor.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vsll.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsll.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vsll.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vsrl.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsrl.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vsrl.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vsra.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsra.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vsra.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vnsrl.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vnsrl.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vnsrl.wi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vnsra.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vnsra.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vnsra.wi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vmseq.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmseq.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmseq.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vmsne.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmsne.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmsne.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vmsltu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmsltu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmslt.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmslt.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmsleu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmsleu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmsleu.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vmsle.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmsle.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmsle.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vmsgtu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmsgtu.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vmsgt.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmsgt.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vminu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vminu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmin.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmin.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmaxu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmaxu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmax.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmax.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmulh.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmulh.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmulhu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmulhu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmulhsu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmulhsu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vdivu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vdivu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vdiv.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vdiv.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vremu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vremu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vrem.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vrem.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwmulu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwmulu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwmulsu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwmulsu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vwmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vmacc.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vnmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vnmsac.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vmadd.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vmadd.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vnmsub.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vnmsub.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmaccu.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmaccu.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmacc.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmaccsu.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmaccsu.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vwmaccus.vx", rv_codec_v_r, rv_fmt_vd_rs1_vs2_vm, NULL, 0, 0, 0 },
    { "vmv.v.v", rv_codec_v_r, rv_fmt_vd_vs1, NULL, 0, 0, 0 },
    { "vmv.v.x", rv_codec_v_r, rv_fmt_vd_rs1, NULL, 0, 0, 0 },
    { "vmv.v.i", rv_codec_v_i, rv_fmt_vd_imm, NULL, 0, 0, 0 },
    { "vmerge.vvm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vl, NULL, 0, 0, 0 },
    { "vmerge.vxm", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vl, NULL, 0, 0, 0 },
    { "vmerge.vim", rv_codec_v_i, rv_fmt_vd_vs2_imm_vl, NULL, 0, 0, 0 },
    { "vsaddu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsaddu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vsaddu.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vsadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vsadd.vi", rv_codec_v_i, rv_fmt_vd_vs2_imm_vm, NULL, 0, 0, 0 },
    { "vssubu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vssubu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vssub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vssub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vaadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vaadd.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vaaddu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vaaddu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vasub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vasub.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vasubu.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vasubu.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vsmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vsmul.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vssrl.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vssrl.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vssrl.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vssra.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vssra.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vssra.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vnclipu.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vnclipu.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vnclipu.wi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vnclip.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vnclip.wx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vnclip.wi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vfadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfadd.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfsub.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfrsub.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfwadd.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwadd.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfwadd.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwadd.wf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfwsub.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwsub.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfwsub.wv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwsub.wf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfmul.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfdiv.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfdiv.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfrdiv.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfwmul.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwmul.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmadd.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmadd.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmadd.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmadd.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmsub.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfmsub.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmsub.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfnmsub.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwnmacc.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwnmacc.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwnmsac.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwnmsac.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfsqrt.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vfrsqrt7.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vfrec7.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vfmin.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfmin.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfmax.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfmax.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfsgnj.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfsgnj.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfsgnjn.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfsgnjn.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfsgnjx.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfsgnjx.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfslide1up.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfslide1down.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vmfeq.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmfeq.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vmfne.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmfne.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vmflt.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmflt.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vmfle.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmfle.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vmfgt.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vmfge.vf", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vm, NULL, 0, 0, 0 },
    { "vfclass.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfmerge.vfm", rv_codec_v_r, rv_fmt_vd_vs2_fs1_vl, NULL, 0, 0, 0 },
    { "vfmv.v.f", rv_codec_v_r, rv_fmt_vd_fs1, NULL, 0, 0, 0 },
    { "vfcvt.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfcvt.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfcvt.f.xu.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfcvt.f.x.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfcvt.rtz.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfcvt.rtz.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.f.xu.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.f.x.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.f.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.rtz.xu.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvt.rtz.x.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.xu.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.x.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.f.xu.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.f.x.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.f.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.rod.f.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.rtz.xu.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfncvt.rtz.x.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vredsum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredand.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredor.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredxor.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredminu.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredmin.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredmaxu.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vredmax.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwredsumu.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vwredsum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfredusum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfredosum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfredmin.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfredmax.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwredusum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vfwredosum.vs", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmand.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmnand.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmandn.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmxor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmnor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmorn.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vmxnor.mm", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vcpop.m", rv_codec_v_r, rv_fmt_rd_vs2_vm, NULL, 0, 0, 0 },
    { "vfirst.m", rv_codec_v_r, rv_fmt_rd_vs2_vm, NULL, 0, 0, 0 },
    { "vmsbf.m", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vmsif.m", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vmsof.m", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "viota.m", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vid.v", rv_codec_v_r, rv_fmt_vd_vm, NULL, 0, 0, 0 },
    { "vmv.x.s", rv_codec_v_r, rv_fmt_rd_vs2, NULL, 0, 0, 0 },
    { "vmv.s.x", rv_codec_v_r, rv_fmt_vd_rs1, NULL, 0, 0, 0 },
    { "vfmv.f.s", rv_codec_v_r, rv_fmt_fd_vs2, NULL, 0, 0, 0 },
    { "vfmv.s.f", rv_codec_v_r, rv_fmt_vd_fs1, NULL, 0, 0, 0 },
    { "vslideup.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vslideup.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vslide1up.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vslidedown.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vslidedown.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vslide1down.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vrgather.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vrgatherei16.vv", rv_codec_v_r, rv_fmt_vd_vs2_vs1_vm, NULL, 0, 0, 0 },
    { "vrgather.vx", rv_codec_v_r, rv_fmt_vd_vs2_rs1_vm, NULL, 0, 0, 0 },
    { "vrgather.vi", rv_codec_v_i, rv_fmt_vd_vs2_uimm_vm, NULL, 0, 0, 0 },
    { "vcompress.vm", rv_codec_v_r, rv_fmt_vd_vs2_vs1, NULL, 0, 0, 0 },
    { "vmv1r.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vmv2r.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vmv4r.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vmv8r.v", rv_codec_v_r, rv_fmt_vd_vs2, NULL, 0, 0, 0 },
    { "vzext.vf2", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vzext.vf4", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vzext.vf8", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vsext.vf2", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vsext.vf4", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vsext.vf8", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vsetvli", rv_codec_vsetvli, rv_fmt_vsetvli, NULL, 0, 0, 0 },
    { "vsetivli", rv_codec_vsetivli, rv_fmt_vsetivli, NULL, 0, 0, 0 },
    { "vsetvl", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "c.zext.b", rv_codec_zcb_ext, rv_fmt_rd, NULL, 0 },
    { "c.sext.b", rv_codec_zcb_ext, rv_fmt_rd, NULL, 0 },
    { "c.zext.h", rv_codec_zcb_ext, rv_fmt_rd, NULL, 0 },
    { "c.sext.h", rv_codec_zcb_ext, rv_fmt_rd, NULL, 0 },
    { "c.zext.w", rv_codec_zcb_ext, rv_fmt_rd, NULL, 0 },
    { "c.not", rv_codec_zcb_ext, rv_fmt_rd, NULL, 0 },
    { "c.mul", rv_codec_zcb_mul, rv_fmt_rd_rs2, NULL, 0, 0 },
    { "c.lbu", rv_codec_zcb_lb, rv_fmt_rs1_rs2_zce_ldst, NULL, 0, 0, 0 },
    { "c.lhu", rv_codec_zcb_lh, rv_fmt_rs1_rs2_zce_ldst, NULL, 0, 0, 0 },
    { "c.lh", rv_codec_zcb_lh, rv_fmt_rs1_rs2_zce_ldst, NULL, 0, 0, 0 },
    { "c.sb", rv_codec_zcb_lb, rv_fmt_rs1_rs2_zce_ldst, NULL, 0, 0, 0 },
    { "c.sh", rv_codec_zcb_lh, rv_fmt_rs1_rs2_zce_ldst, NULL, 0, 0, 0 },
    { "cm.push", rv_codec_zcmp_cm_pushpop, rv_fmt_push_rlist, NULL, 0, 0 },
    { "cm.pop", rv_codec_zcmp_cm_pushpop, rv_fmt_pop_rlist, NULL, 0, 0 },
    { "cm.popret", rv_codec_zcmp_cm_pushpop, rv_fmt_pop_rlist, NULL, 0, 0, 0 },
    { "cm.popretz", rv_codec_zcmp_cm_pushpop, rv_fmt_pop_rlist, NULL, 0, 0 },
    { "cm.mva01s", rv_codec_zcmp_cm_mv, rv_fmt_rd_rs2, NULL, 0, 0, 0 },
    { "cm.mvsa01", rv_codec_zcmp_cm_mv, rv_fmt_rd_rs2, NULL, 0, 0, 0 },
    { "cm.jt", rv_codec_zcmt_jt, rv_fmt_zcmt_index, NULL, 0 },
    { "cm.jalt", rv_codec_zcmt_jt, rv_fmt_zcmt_index, NULL, 0 },
    { "czero.eqz", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "czero.nez", rv_codec_r, rv_fmt_rd_rs1_rs2, NULL, 0, 0, 0 },
    { "fcvt.bf16.s", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fcvt.s.bf16", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "vfncvtbf16.f.f.w", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwcvtbf16.f.f.v", rv_codec_v_r, rv_fmt_vd_vs2_vm, NULL, 0, 0, 0 },
    { "vfwmaccbf16.vv", rv_codec_v_r, rv_fmt_vd_vs1_vs2_vm, NULL, 0, 0, 0 },
    { "vfwmaccbf16.vf", rv_codec_v_r, rv_fmt_vd_fs1_vs2_vm, NULL, 0, 0, 0 },
    { "flh", rv_codec_i, rv_fmt_frd_offset_rs1, NULL, 0, 0, 0 },
    { "fsh", rv_codec_s, rv_fmt_frs2_offset_rs1, NULL, 0, 0, 0 },
    { "fmv.h.x", rv_codec_r, rv_fmt_frd_rs1, NULL, 0, 0, 0 },
    { "fmv.x.h", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fli.s", rv_codec_fli, rv_fmt_fli, NULL, 0, 0, 0 },
    { "fli.d", rv_codec_fli, rv_fmt_fli, NULL, 0, 0, 0 },
    { "fli.q", rv_codec_fli, rv_fmt_fli, NULL, 0, 0, 0 },
    { "fli.h", rv_codec_fli, rv_fmt_fli, NULL, 0, 0, 0 },
    { "fminm.s", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmaxm.s", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fminm.d", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmaxm.d", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fminm.q", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmaxm.q", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fminm.h", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fmaxm.h", rv_codec_r, rv_fmt_frd_frs1_frs2, NULL, 0, 0, 0 },
    { "fround.s", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "froundnx.s", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fround.d", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "froundnx.d", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fround.q", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "froundnx.q", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fround.h", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "froundnx.h", rv_codec_r_m, rv_fmt_rm_frd_frs1, NULL, 0, 0, 0 },
    { "fcvtmod.w.d", rv_codec_r_m, rv_fmt_rm_rd_frs1, NULL, 0, 0, 0 },
    { "fmvh.x.d", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fmvp.d.x", rv_codec_r, rv_fmt_frd_rs1_rs2, NULL, 0, 0, 0 },
    { "fmvh.x.q", rv_codec_r, rv_fmt_rd_frs1, NULL, 0, 0, 0 },
    { "fmvp.q.x", rv_codec_r, rv_fmt_frd_rs1_rs2, NULL, 0, 0, 0 },
    { "fleq.s", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fltq.s", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fleq.d", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fltq.d", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fleq.q", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fltq.q", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fleq.h", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
    { "fltq.h", rv_codec_r, rv_fmt_rd_frs1_frs2, NULL, 0, 0, 0 },
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
    case 0x03bd: return "pmpaddr13";
    case 0x03be: return "pmpaddr14";
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

static void decode_inst_opcode(rv_decode *dec, rv_isa isa)
{
    rv_inst inst = dec->inst;
    rv_opcode op = rv_op_illegal;
    switch ((inst >> 0) & 0b11) {
    case 0:
        switch ((inst >> 13) & 0b111) {
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
            switch ((inst >> 2) & 0b11111111111) {
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
            switch ((inst >> 7) & 0b11111) {
            case 2: op = rv_op_c_addi16sp; break;
            default: op = rv_op_c_lui; break;
            }
            break;
        case 4:
            switch ((inst >> 10) & 0b11) {
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
                if (dec->cfg->ext_zcmp && ((inst >> 12) & 0b01)) {
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
                        if (!dec->cfg->ext_zcmt) {
                            break;
                        }
                        if (((inst >> 2) & 0xFF) >= 32) {
                            op = rv_op_cm_jalt;
                        } else {
                            op = rv_op_cm_jt;
                        }
                        break;
                    case 3:
                        if (!dec->cfg->ext_zcmp) {
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
            case 2: op = rv_op_lq; break;
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
        case 5: op = rv_op_auipc; break;
        case 6:
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
            case 2: op = rv_op_amoadd_w; break;
            case 3: op = rv_op_amoadd_d; break;
            case 4: op = rv_op_amoadd_q; break;
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
                switch ((inst >> 20) & 0b11111) {
                case 0: op = rv_op_zext_h; break;
                default: op = rv_op_pack; break;
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
                }
                break;
            case 4:
                switch ((inst >> 26) & 0b111111) {
                case 0: op = rv_op_vadd_vx; break;
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
                case 1952:
                    switch ((inst >> 15) & 0b1111111111) {
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

static uint32_t calculate_xreg(uint32_t sreg)
{
    return sreg < 2 ? sreg + 8 : sreg + 16;
}

static uint32_t operand_sreg1(rv_inst inst)
{
    return calculate_xreg((inst << 54) >> 61);
}

static uint32_t operand_sreg2(rv_inst inst)
{
    return calculate_xreg((inst << 59) >> 61);
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

static uint32_t operand_cimmshl6(rv_inst inst, rv_isa isa)
{
    int imm = ((inst << 51) >> 63) << 5 |
        (inst << 57) >> 59;
    if (isa == rv128) {
        imm = imm ? imm : 64;
    }
    return imm;
}

static uint32_t operand_cimmshr6(rv_inst inst, rv_isa isa)
{
    int imm = ((inst << 51) >> 63) << 5 |
        (inst << 57) >> 59;
    if (isa == rv128) {
        imm = imm | (imm & 32) << 1;
        imm = imm ? imm : 64;
    }
    return imm;
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

static uint32_t operand_vimm(rv_inst inst)
{
    return (int64_t)(inst << 44) >> 59;
}

static uint32_t operand_vzimm11(rv_inst inst)
{
    return (inst << 33) >> 53;
}

static uint32_t operand_vzimm10(rv_inst inst)
{
    return (inst << 34) >> 54;
}

static uint32_t operand_vzimm6(rv_inst inst)
{
    return ((inst << 37) >> 63) << 5 |
        ((inst << 44) >> 59);
}

static uint32_t operand_bs(rv_inst inst)
{
    return (inst << 32) >> 62;
}

static uint32_t operand_rnum(rv_inst inst)
{
    return (inst << 40) >> 60;
}

static uint32_t operand_vm(rv_inst inst)
{
    return (inst << 38) >> 63;
}

static uint32_t operand_uimm_c_lb(rv_inst inst)
{
    return (((inst << 58) >> 63) << 1) |
        ((inst << 57) >> 63);
}

static uint32_t operand_uimm_c_lh(rv_inst inst)
{
    return (((inst << 58) >> 63) << 1);
}

static uint32_t operand_zcmp_spimm(rv_inst inst)
{
    return ((inst << 60) >> 62) << 4;
}

static uint32_t operand_zcmp_rlist(rv_inst inst)
{
    return ((inst << 56) >> 60);
}

static uint32_t operand_imm6(rv_inst inst)
{
    return (inst << 38) >> 60;
}

static uint32_t operand_imm2(rv_inst inst)
{
    return (inst << 37) >> 62;
}

static uint32_t operand_immh(rv_inst inst)
{
    return (inst << 32) >> 58;
}

static uint32_t operand_imml(rv_inst inst)
{
    return (inst << 38) >> 58;
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
    return ((inst << 54) >> 56);
}

/* decode operands */

static void decode_inst_operands(rv_decode *dec, rv_isa isa)
{
    const rv_opcode_data *opcode_data = dec->opcode_data;
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
        dec->imm = operand_vimm(inst);
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

    /*
     * instruction length coding
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
    const rv_opcode_data *opcode_data = dec->opcode_data;
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
        case '-':
            append(buf, "-", buflen);
            break;
        case 'b':
            snprintf(tmp, sizeof(tmp), "%d", dec->bs);
            append(buf, tmp, buflen);
            break;
        case 'n':
            snprintf(tmp, sizeof(tmp), "%d", dec->rnum);
            append(buf, tmp, buflen);
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
            append(buf, dec->cfg->ext_zfinx ? rv_ireg_name_sym[dec->rd] :
                                              rv_freg_name_sym[dec->rd],
                   buflen);
            break;
        case '4':
            append(buf, dec->cfg->ext_zfinx ? rv_ireg_name_sym[dec->rs1] :
                                              rv_freg_name_sym[dec->rs1],
                   buflen);
            break;
        case '5':
            append(buf, dec->cfg->ext_zfinx ? rv_ireg_name_sym[dec->rs2] :
                                              rv_freg_name_sym[dec->rs2],
                   buflen);
            break;
        case '6':
            append(buf, dec->cfg->ext_zfinx ? rv_ireg_name_sym[dec->rs3] :
                                              rv_freg_name_sym[dec->rs3],
                   buflen);
            break;
        case '7':
            snprintf(tmp, sizeof(tmp), "%d", dec->rs1);
            append(buf, tmp, buflen);
            break;
        case 'i':
            snprintf(tmp, sizeof(tmp), "%d", dec->imm);
            append(buf, tmp, buflen);
            break;
        case 'u':
            snprintf(tmp, sizeof(tmp), "%u", ((uint32_t)dec->imm & 0b111111));
            append(buf, tmp, buflen);
            break;
        case 'j':
            snprintf(tmp, sizeof(tmp), "%d", dec->imm1);
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
        case 'U':
            fmt++;
            snprintf(tmp, sizeof(tmp), "%d", dec->imm >> 12);
            append(buf, tmp, buflen);
            if (*fmt == 'o') {
                while (strlen(buf) < tab * 2) {
                    append(buf, " ", buflen);
                }
                snprintf(tmp, sizeof(tmp), "# 0x%" PRIx64,
                    dec->pc + dec->imm);
                append(buf, tmp, buflen);
            }
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
        case 'l':
            append(buf, ",v0", buflen);
            break;
        case 'm':
            if (dec->vm == 0) {
                append(buf, ",v0.t", buflen);
            }
            break;
        case 'D':
            append(buf, rv_vreg_name_sym[dec->rd], buflen);
            break;
        case 'E':
            append(buf, rv_vreg_name_sym[dec->rs1], buflen);
            break;
        case 'F':
            append(buf, rv_vreg_name_sym[dec->rs2], buflen);
            break;
        case 'G':
            append(buf, rv_vreg_name_sym[dec->rs3], buflen);
            break;
        case 'v': {
            char nbuf[32] = {0};
            const int sew = 1 << (((dec->vzimm >> 3) & 0b111) + 3);
            sprintf(nbuf, "%d", sew);
            const int lmul = dec->vzimm & 0b11;
            const int flmul = (dec->vzimm >> 2) & 1;
            const char *vta = (dec->vzimm >> 6) & 1 ? "ta" : "tu";
            const char *vma = (dec->vzimm >> 7) & 1 ? "ma" : "mu";
            append(buf, "e", buflen);
            append(buf, nbuf, buflen);
            append(buf, ",m", buflen);
            if (flmul) {
                switch (lmul) {
                case 3:
                    sprintf(nbuf, "f2");
                    break;
                case 2:
                    sprintf(nbuf, "f4");
                    break;
                case 1:
                    sprintf(nbuf, "f8");
                break;
                }
                append(buf, nbuf, buflen);
            } else {
                sprintf(nbuf, "%d", 1 << lmul);
                append(buf, nbuf, buflen);
            }
            append(buf, ",", buflen);
            append(buf, vta, buflen);
            append(buf, ",", buflen);
            append(buf, vma, buflen);
            break;
        }
        case 'x': {
            switch (dec->rlist) {
            case 4:
                snprintf(tmp, sizeof(tmp), "{ra}");
                break;
            case 5:
                snprintf(tmp, sizeof(tmp), "{ra, s0}");
                break;
            case 15:
                snprintf(tmp, sizeof(tmp), "{ra, s0-s11}");
                break;
            default:
                snprintf(tmp, sizeof(tmp), "{ra, s0-s%d}", dec->rlist - 5);
                break;
            }
            append(buf, tmp, buflen);
            break;
        }
        case 'h':
            append(buf, rv_fli_name_const[dec->imm], buflen);
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
    const rv_opcode_data *opcode_data = dec->opcode_data;
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
    const rv_opcode_data *opcode_data = dec->opcode_data;
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
    const rv_opcode_data *opcode_data = dec->opcode_data;
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
    const rv_opcode_data *opcode_data = dec->opcode_data;
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
disasm_inst(char *buf, size_t buflen, rv_isa isa, uint64_t pc, rv_inst inst,
            RISCVCPUConfig *cfg)
{
    rv_decode dec = { 0 };
    dec.pc = pc;
    dec.inst = inst;
    dec.cfg = cfg;

    static const struct {
        bool (*guard_func)(const RISCVCPUConfig *);
        const rv_opcode_data *opcode_data;
        void (*decode_func)(rv_decode *, rv_isa);
    } decoders[] = {
        { always_true_p, rvi_opcode_data, decode_inst_opcode },
        { has_xtheadba_p, xthead_opcode_data, decode_xtheadba },
        { has_xtheadbb_p, xthead_opcode_data, decode_xtheadbb },
        { has_xtheadbs_p, xthead_opcode_data, decode_xtheadbs },
        { has_xtheadcmo_p, xthead_opcode_data, decode_xtheadcmo },
        { has_xtheadcondmov_p, xthead_opcode_data, decode_xtheadcondmov },
        { has_xtheadfmemidx_p, xthead_opcode_data, decode_xtheadfmemidx },
        { has_xtheadfmv_p, xthead_opcode_data, decode_xtheadfmv },
        { has_xtheadmac_p, xthead_opcode_data, decode_xtheadmac },
        { has_xtheadmemidx_p, xthead_opcode_data, decode_xtheadmemidx },
        { has_xtheadmempair_p, xthead_opcode_data, decode_xtheadmempair },
        { has_xtheadsync_p, xthead_opcode_data, decode_xtheadsync },
        { has_XVentanaCondOps_p, ventana_opcode_data, decode_xventanacondops },
    };

    for (size_t i = 0; i < ARRAY_SIZE(decoders); i++) {
        bool (*guard_func)(const RISCVCPUConfig *) = decoders[i].guard_func;
        const rv_opcode_data *opcode_data = decoders[i].opcode_data;
        void (*decode_func)(rv_decode *, rv_isa) = decoders[i].decode_func;

        if (guard_func(cfg)) {
            dec.opcode_data = opcode_data;
            decode_func(&dec, isa);
            if (dec.op != rv_op_illegal)
                break;
        }
    }

    if (dec.op == rv_op_illegal) {
        dec.opcode_data = rvi_opcode_data;
    }

    decode_inst_operands(&dec, isa);
    decode_inst_decompress(&dec, isa);
    decode_inst_lift_pseudo(&dec);
    format_inst(buf, buflen, 24, &dec);
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

    disasm_inst(buf, sizeof(buf), isa, memaddr, inst,
                (RISCVCPUConfig *)info->target_info);
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
