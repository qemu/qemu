PATH_TO_QEMU="../../../../../../mips64el-linux-user/qemu-mips64el"


#
# Bit Count
# ---------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nloc_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nloc_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nloc_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nloc_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nlzc_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nlzc_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nlzc_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nlzc_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pcnt_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pcnt_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pcnt_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pcnt_d

#
# Bit move
# --------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsl_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsl_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsl_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsl_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsr_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsr_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsr_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_binsr_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bmnz_v
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bmz_v
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bsel_v

#
# Bit Set
# -------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bclr_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bclr_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bclr_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bclr_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bneg_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bneg_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bneg_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bneg_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bset_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bset_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bset_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_bset_d

#
# Fixed Multiply
# --------------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mul_q_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mul_q_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mulr_q_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mulr_q_w

#
# Float Max Min
# -------------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmax_a_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmax_a_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmax_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmax_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmin_a_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmin_a_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmin_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_fmin_d

#
# Int Add
# -------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_add_a_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_add_a_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_add_a_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_add_a_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_a_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_a_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_a_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_a_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_adds_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_addv_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_addv_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_addv_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_addv_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hadd_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hadd_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hadd_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hadd_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hadd_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hadd_u_d

#
# Int Average
# -----------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ave_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_aver_u_d

#
# Int Compare
# -----------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ceq_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ceq_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ceq_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ceq_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_cle_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_clt_u_d

#
# Int Divide
# ----------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_div_u_d

#
# Int Dot Product
# ---------------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dotp_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dotp_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dotp_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dotp_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dotp_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dotp_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpadd_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpadd_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpadd_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpadd_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpadd_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpadd_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpsub_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpsub_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpsub_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpsub_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpsub_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_dpsub_u_d

#
# Int Max Min
# -----------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_a_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_a_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_a_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_a_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_max_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_a_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_a_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_a_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_a_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_min_u_d

#
# Int Modulo
# ----------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mod_u_d

#
# Int Multiply
# ------------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mulv_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mulv_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mulv_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_mulv_d

#
# Int Subtract
# ------------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_asub_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hsub_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hsub_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hsub_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hsub_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hsub_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_hsub_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subs_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsuu_s_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsuu_s_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsuu_s_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsuu_s_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsus_u_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsus_u_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsus_u_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subsus_u_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subv_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subv_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subv_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_subv_d

#
# Interleave
# ----------
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvev_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvev_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvev_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvev_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvod_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvod_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvod_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvod_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvl_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvl_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvl_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvl_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvr_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvr_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvr_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_ilvr_d

#
# Logic
# -----
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_and_v
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_nor_v
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_or_v
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_xor_v

#
# Move
# ----
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_move_v

#
# Pack
# ----
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckev_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckev_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckev_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckev_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckod_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckod_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckod_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_pckod_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_vshf_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_vshf_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_vshf_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_vshf_d

#
# Shift
# -----
#
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sll_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sll_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sll_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sll_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sra_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sra_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sra_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_sra_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srar_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srar_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srar_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srar_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srl_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srl_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srl_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srl_d
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srlr_b
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srlr_h
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srlr_w
$PATH_TO_QEMU -cpu I6400  /tmp/test_msa_srlr_d
