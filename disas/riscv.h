/*
 * QEMU disassembler -- RISC-V specific header.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef DISAS_RISCV_H
#define DISAS_RISCV_H

#include "target/riscv/cpu_cfg.h"

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
    rv_codec_k_bs,
    rv_codec_k_rnum,
    rv_codec_v_r,
    rv_codec_v_ldst,
    rv_codec_v_i,
    rv_codec_vsetvli,
    rv_codec_vsetivli,
    rv_codec_vror_vi,
    rv_codec_zcb_ext,
    rv_codec_zcb_mul,
    rv_codec_zcb_lb,
    rv_codec_zcb_lh,
    rv_codec_zcmp_cm_pushpop,
    rv_codec_zcmp_cm_mv,
    rv_codec_zcmt_jt,
    rv_codec_r2_imm5,
    rv_codec_r2,
    rv_codec_r2_imm6,
    rv_codec_r_imm2,
    rv_codec_r2_immhl,
    rv_codec_r2_imm2_imm5,
    rv_codec_fli,
} rv_codec;

/* structures */

typedef struct {
    const int op;
    const rvc_constraint *constraints;
} rv_comp_data;

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

typedef struct {
    RISCVCPUConfig *cfg;
    uint64_t  pc;
    uint64_t  inst;
    const rv_opcode_data *opcode_data;
    int32_t   imm;
    int32_t   imm1;
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
    uint8_t   bs;
    uint8_t   rnum;
    uint8_t   vm;
    uint32_t  vzimm;
    uint8_t   rlist;
} rv_decode;

enum {
    rv_op_illegal = 0
};

enum {
    rvcd_imm_nz = 0x1
};

/* instruction formats */

#define rv_fmt_none                   "O\t"
#define rv_fmt_rs1                    "O\t1"
#define rv_fmt_offset                 "O\to"
#define rv_fmt_pred_succ              "O\tp,s"
#define rv_fmt_rs1_rs2                "O\t1,2"
#define rv_fmt_rd_imm                 "O\t0,i"
#define rv_fmt_rd_uimm                "O\t0,Ui"
#define rv_fmt_rd_offset              "O\t0,o"
#define rv_fmt_rd_uoffset             "O\t0,Uo"
#define rv_fmt_rd_rs1_rs2             "O\t0,1,2"
#define rv_fmt_frd_rs1                "O\t3,1"
#define rv_fmt_frd_rs1_rs2            "O\t3,1,2"
#define rv_fmt_frd_frs1               "O\t3,4"
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
#define rv_fmt_rs1_rs2_bs             "O\t1,2,b"
#define rv_fmt_rd_rs1_rnum            "O\t0,1,n"
#define rv_fmt_ldst_vd_rs1_vm         "O\tD,(1)m"
#define rv_fmt_ldst_vd_rs1_rs2_vm     "O\tD,(1),2m"
#define rv_fmt_ldst_vd_rs1_vs2_vm     "O\tD,(1),Fm"
#define rv_fmt_vd_vs2_vs1             "O\tD,F,E"
#define rv_fmt_vd_vs2_vs1_vl          "O\tD,F,El"
#define rv_fmt_vd_vs2_vs1_vm          "O\tD,F,Em"
#define rv_fmt_vd_vs2_rs1_vl          "O\tD,F,1l"
#define rv_fmt_vd_vs2_fs1_vl          "O\tD,F,4l"
#define rv_fmt_vd_vs2_rs1_vm          "O\tD,F,1m"
#define rv_fmt_vd_vs2_fs1_vm          "O\tD,F,4m"
#define rv_fmt_vd_vs2_imm_vl          "O\tD,F,il"
#define rv_fmt_vd_vs2_imm_vm          "O\tD,F,im"
#define rv_fmt_vd_vs2_uimm            "O\tD,F,u"
#define rv_fmt_vd_vs2_uimm_vm         "O\tD,F,um"
#define rv_fmt_vd_vs1_vs2_vm          "O\tD,E,Fm"
#define rv_fmt_vd_rs1_vs2_vm          "O\tD,1,Fm"
#define rv_fmt_vd_fs1_vs2_vm          "O\tD,4,Fm"
#define rv_fmt_vd_vs1                 "O\tD,E"
#define rv_fmt_vd_rs1                 "O\tD,1"
#define rv_fmt_vd_fs1                 "O\tD,4"
#define rv_fmt_vd_imm                 "O\tD,i"
#define rv_fmt_vd_vs2                 "O\tD,F"
#define rv_fmt_vd_vs2_vm              "O\tD,Fm"
#define rv_fmt_rd_vs2_vm              "O\t0,Fm"
#define rv_fmt_rd_vs2                 "O\t0,F"
#define rv_fmt_fd_vs2                 "O\t3,F"
#define rv_fmt_vd_vm                  "O\tDm"
#define rv_fmt_vsetvli                "O\t0,1,v"
#define rv_fmt_vsetivli               "O\t0,u,v"
#define rv_fmt_rs1_rs2_zce_ldst       "O\t2,i(1)"
#define rv_fmt_push_rlist             "O\tx,-i"
#define rv_fmt_pop_rlist              "O\tx,i"
#define rv_fmt_zcmt_index             "O\ti"
#define rv_fmt_rd_rs1_rs2_imm         "O\t0,1,2,i"
#define rv_fmt_frd_rs1_rs2_imm        "O\t3,1,2,i"
#define rv_fmt_rd_rs1_immh_imml       "O\t0,1,i,j"
#define rv_fmt_rd_rs1_immh_imml_addr  "O\t0,(1),i,j"
#define rv_fmt_rd2_imm                "O\t0,2,(1),i"
#define rv_fmt_fli                    "O\t3,h"

#endif /* DISAS_RISCV_H */
