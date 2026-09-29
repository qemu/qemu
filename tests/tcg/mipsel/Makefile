-include ../../../../config-host.mak

CROSS=mips64el-unknown-linux-gnu-

SIM=qemu-mipsel
SIM_FLAGS=-cpu 74Kf

CC      = $(CROSS)gcc
CFLAGS  = -EL -mabi=32 -march=mips32r2 -mgp32 -mdsp -mdspr2 -static

TESTCASES  = test_dsp_r1_absq_s_ph.tst
TESTCASES += test_dsp_r1_absq_s_w.tst
TESTCASES += test_dsp_r1_addq_ph.tst
TESTCASES += test_dsp_r1_addq_s_ph.tst
TESTCASES += test_dsp_r1_addq_s_w.tst
TESTCASES += test_dsp_r1_addsc.tst
TESTCASES += test_dsp_r1_addu_qb.tst
TESTCASES += test_dsp_r1_addu_s_qb.tst
TESTCASES += test_dsp_r1_addwc.tst
TESTCASES += test_dsp_r1_bitrev.tst
TESTCASES += test_dsp_r1_bposge32.tst
TESTCASES += test_dsp_r1_cmp_eq_ph.tst
TESTCASES += test_dsp_r1_cmpgu_eq_qb.tst
TESTCASES += test_dsp_r1_cmpgu_le_qb.tst
TESTCASES += test_dsp_r1_cmpgu_lt_qb.tst
TESTCASES += test_dsp_r1_cmp_le_ph.tst
TESTCASES += test_dsp_r1_cmp_lt_ph.tst
TESTCASES += test_dsp_r1_cmpu_eq_qb.tst
TESTCASES += test_dsp_r1_cmpu_le_qb.tst
TESTCASES += test_dsp_r1_cmpu_lt_qb.tst
TESTCASES += test_dsp_r1_dpaq_sa_l_w.tst
TESTCASES += test_dsp_r1_dpaq_s_w_ph.tst
TESTCASES += test_dsp_r1_dpau_h_qbl.tst
TESTCASES += test_dsp_r1_dpau_h_qbr.tst
TESTCASES += test_dsp_r1_dpsq_sa_l_w.tst
TESTCASES += test_dsp_r1_dpsq_s_w_ph.tst
TESTCASES += test_dsp_r1_dpsu_h_qbl.tst
TESTCASES += test_dsp_r1_dpsu_h_qbr.tst
TESTCASES += test_dsp_r1_extp.tst
TESTCASES += test_dsp_r1_extpdp.tst
TESTCASES += test_dsp_r1_extpdpv.tst
TESTCASES += test_dsp_r1_extpv.tst
TESTCASES += test_dsp_r1_extr_rs_w.tst
TESTCASES += test_dsp_r1_extr_r_w.tst
TESTCASES += test_dsp_r1_extr_s_h.tst
TESTCASES += test_dsp_r1_extrv_rs_w.tst
TESTCASES += test_dsp_r1_extrv_r_w.tst
TESTCASES += test_dsp_r1_extrv_s_h.tst
TESTCASES += test_dsp_r1_extrv_w.tst
TESTCASES += test_dsp_r1_extr_w.tst
TESTCASES += test_dsp_r1_insv.tst
TESTCASES += test_dsp_r1_lbux.tst
TESTCASES += test_dsp_r1_lhx.tst
TESTCASES += test_dsp_r1_lwx.tst
TESTCASES += test_dsp_r1_madd.tst
TESTCASES += test_dsp_r1_maddu.tst
TESTCASES += test_dsp_r1_maq_sa_w_phl.tst
TESTCASES += test_dsp_r1_maq_sa_w_phr.tst
TESTCASES += test_dsp_r1_maq_s_w_phl.tst
TESTCASES += test_dsp_r1_maq_s_w_phr.tst
TESTCASES += test_dsp_r1_mfhi.tst
TESTCASES += test_dsp_r1_mflo.tst
TESTCASES += test_dsp_r1_modsub.tst
TESTCASES += test_dsp_r1_msub.tst
TESTCASES += test_dsp_r1_msubu.tst
TESTCASES += test_dsp_r1_mthi.tst
TESTCASES += test_dsp_r1_mthlip.tst
TESTCASES += test_dsp_r1_mtlo.tst
TESTCASES += test_dsp_r1_muleq_s_w_phl.tst
TESTCASES += test_dsp_r1_muleq_s_w_phr.tst
TESTCASES += test_dsp_r1_muleu_s_ph_qbl.tst
TESTCASES += test_dsp_r1_muleu_s_ph_qbr.tst
TESTCASES += test_dsp_r1_mulq_rs_ph.tst
TESTCASES += test_dsp_r1_mult.tst
TESTCASES += test_dsp_r1_multu.tst
TESTCASES += test_dsp_r1_packrl_ph.tst
TESTCASES += test_dsp_r1_pick_ph.tst
TESTCASES += test_dsp_r1_pick_qb.tst
TESTCASES += test_dsp_r1_precequ_ph_qbla.tst
TESTCASES += test_dsp_r1_precequ_ph_qbl.tst
TESTCASES += test_dsp_r1_precequ_ph_qbra.tst
TESTCASES += test_dsp_r1_precequ_ph_qbr.tst
TESTCASES += test_dsp_r1_preceq_w_phl.tst
TESTCASES += test_dsp_r1_preceq_w_phr.tst
TESTCASES += test_dsp_r1_preceu_ph_qbla.tst
TESTCASES += test_dsp_r1_preceu_ph_qbl.tst
TESTCASES += test_dsp_r1_preceu_ph_qbra.tst
TESTCASES += test_dsp_r1_preceu_ph_qbr.tst
TESTCASES += test_dsp_r1_precrq_ph_w.tst
TESTCASES += test_dsp_r1_precrq_qb_ph.tst
TESTCASES += test_dsp_r1_precrq_rs_ph_w.tst
TESTCASES += test_dsp_r1_precrqu_s_qb_ph.tst
TESTCASES += test_dsp_r1_raddu_w_qb.tst
TESTCASES += test_dsp_r1_rddsp.tst
TESTCASES += test_dsp_r1_repl_ph.tst
TESTCASES += test_dsp_r1_repl_qb.tst
TESTCASES += test_dsp_r1_replv_ph.tst
TESTCASES += test_dsp_r1_replv_qb.tst
TESTCASES += test_dsp_r1_shilo.tst
TESTCASES += test_dsp_r1_shilov.tst
TESTCASES += test_dsp_r1_shll_ph.tst
TESTCASES += test_dsp_r1_shll_qb.tst
TESTCASES += test_dsp_r1_shll_s_ph.tst
TESTCASES += test_dsp_r1_shll_s_w.tst
TESTCASES += test_dsp_r1_shllv_ph.tst
TESTCASES += test_dsp_r1_shllv_qb.tst
TESTCASES += test_dsp_r1_shllv_s_ph.tst
TESTCASES += test_dsp_r1_shllv_s_w.tst
TESTCASES += test_dsp_r1_shra_ph.tst
TESTCASES += test_dsp_r1_shra_r_ph.tst
TESTCASES += test_dsp_r1_shra_r_w.tst
TESTCASES += test_dsp_r1_shrav_ph.tst
TESTCASES += test_dsp_r1_shrav_r_ph.tst
TESTCASES += test_dsp_r1_shrav_r_w.tst
TESTCASES += test_dsp_r1_shrl_qb.tst
TESTCASES += test_dsp_r1_shrlv_qb.tst
TESTCASES += test_dsp_r1_subq_ph.tst
TESTCASES += test_dsp_r1_subq_s_ph.tst
TESTCASES += test_dsp_r1_subq_s_w.tst
TESTCASES += test_dsp_r1_subu_qb.tst
TESTCASES += test_dsp_r1_subu_s_qb.tst
TESTCASES += test_dsp_r1_wrdsp.tst
TESTCASES += test_dsp_r2_absq_s_qb.tst
TESTCASES += test_dsp_r2_addqh_ph.tst
TESTCASES += test_dsp_r2_addqh_r_ph.tst
TESTCASES += test_dsp_r2_addqh_r_w.tst
TESTCASES += test_dsp_r2_addqh_w.tst
TESTCASES += test_dsp_r2_adduh_qb.tst
TESTCASES += test_dsp_r2_adduh_r_qb.tst
TESTCASES += test_dsp_r2_addu_ph.tst
TESTCASES += test_dsp_r2_addu_s_ph.tst
TESTCASES += test_dsp_r2_append.tst
TESTCASES += test_dsp_r2_balign.tst
TESTCASES += test_dsp_r2_cmpgdu_eq_qb.tst
TESTCASES += test_dsp_r2_cmpgdu_le_qb.tst
TESTCASES += test_dsp_r2_cmpgdu_lt_qb.tst
TESTCASES += test_dsp_r2_dpaqx_sa_w_ph.tst
TESTCASES += test_dsp_r2_dpa_w_ph.tst
TESTCASES += test_dsp_r2_dpax_w_ph.tst
TESTCASES += test_dsp_r2_dpaqx_s_w_ph.tst
TESTCASES += test_dsp_r2_dpsqx_sa_w_ph.tst
TESTCASES += test_dsp_r2_dpsqx_s_w_ph.tst
TESTCASES += test_dsp_r2_dps_w_ph.tst
TESTCASES += test_dsp_r2_dpsx_w_ph.tst
TESTCASES += test_dsp_r2_mul_ph.tst
TESTCASES += test_dsp_r2_mulq_rs_w.tst
TESTCASES += test_dsp_r2_mulq_s_ph.tst
TESTCASES += test_dsp_r2_mulq_s_w.tst
TESTCASES += test_dsp_r2_mulsaq_s_w_ph.tst
TESTCASES += test_dsp_r2_mulsa_w_ph.tst
TESTCASES += test_dsp_r2_mul_s_ph.tst
TESTCASES += test_dsp_r2_precr_qb_ph.tst
TESTCASES += test_dsp_r2_precr_sra_ph_w.tst
TESTCASES += test_dsp_r2_precr_sra_r_ph_w.tst
TESTCASES += test_dsp_r2_prepend.tst
TESTCASES += test_dsp_r2_shra_qb.tst
TESTCASES += test_dsp_r2_shra_r_qb.tst
TESTCASES += test_dsp_r2_shrav_qb.tst
TESTCASES += test_dsp_r2_shrav_r_qb.tst
TESTCASES += test_dsp_r2_shrl_ph.tst
TESTCASES += test_dsp_r2_shrlv_ph.tst
TESTCASES += test_dsp_r2_subqh_ph.tst
TESTCASES += test_dsp_r2_subqh_r_ph.tst
TESTCASES += test_dsp_r2_subqh_r_w.tst
TESTCASES += test_dsp_r2_subqh_w.tst
TESTCASES += test_dsp_r2_subuh_qb.tst
TESTCASES += test_dsp_r2_subuh_r_qb.tst
TESTCASES += test_dsp_r2_subu_ph.tst
TESTCASES += test_dsp_r2_subu_s_ph.tst


all: $(TESTCASES)

%.tst: %.c
	$(CC) $(CFLAGS) $< -o $@

check: $(TESTCASES)
	@for case in $(TESTCASES); do \
        echo $(SIM) $(SIM_FLAGS) ./$$case;\
        $(SIM) $(SIM_FLAGS) ./$$case; \
	done

clean:
	$(RM) -rf $(TESTCASES)
