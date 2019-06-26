
#
# Bit Count
# ---------
#
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nloc_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nloc_b
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nloc_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nloc_h
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nloc_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nloc_w
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nloc_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nloc_d
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nlzc_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nlzc_b
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nlzc_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nlzc_h
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nlzc_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nlzc_w
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_nlzc_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nlzc_d
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_pcnt_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pcnt_b
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_pcnt_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pcnt_h
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_pcnt_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pcnt_w
/opt/img/bin/mips-img-linux-gnu-gcc       bit-count/test_msa_pcnt_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pcnt_d

#
# Bit move
# --------
#
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsl_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsl_b
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsl_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsl_h
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsl_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsl_w
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsl_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsl_d
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsr_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsr_b
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsr_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsr_h
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsr_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsr_w
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_binsr_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_binsr_d
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_bmnz_v.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_bmnz_v
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_bmz_v.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_bmz_v
/opt/img/bin/mips-img-linux-gnu-gcc         bit-move/test_msa_bsel_v.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_bsel_v

#
# Bit Set
# -------
#
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bclr_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bclr_b
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bclr_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bclr_h
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bclr_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bclr_w
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bclr_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bclr_d
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bneg_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bneg_b
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bneg_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bneg_h
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bneg_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bneg_w
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bneg_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bneg_d
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bset_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bset_b
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bset_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bset_h
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bset_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bset_w
/opt/img/bin/mips-img-linux-gnu-gcc         bit-set/test_msa_bset_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_bset_d

#
# Fixed Multiply
# --------------
#
/opt/img/bin/mips-img-linux-gnu-gcc    fixed-multiply/test_msa_mul_q_h.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mul_q_h
/opt/img/bin/mips-img-linux-gnu-gcc    fixed-multiply/test_msa_mul_q_w.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mul_q_w
/opt/img/bin/mips-img-linux-gnu-gcc    fixed-multiply/test_msa_mulr_q_h.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mulr_q_h
/opt/img/bin/mips-img-linux-gnu-gcc    fixed-multiply/test_msa_mulr_q_w.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mulr_q_w

#
# Float Max Min
# -------------
#
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmax_a_w.c  \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmax_a_w
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmax_a_d.c  \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmax_a_d
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmax_w.c    \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmax_w
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmax_d.c    \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmax_d
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmin_a_w.c  \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmin_a_w
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmin_a_d.c  \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmin_a_d
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmin_w.c    \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmin_w
/opt/img/bin/mips-img-linux-gnu-gcc         float-max-min/test_msa_fmin_d.c    \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o        /tmp/test_msa_fmin_d

#
# Int Add
# -------
#
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_add_a_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_add_a_b
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_add_a_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_add_a_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_add_a_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_add_a_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_add_a_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_add_a_d
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_a_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_a_b
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_a_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_a_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_a_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_a_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_a_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_a_d
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_s_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_s_b
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_s_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_s_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_s_d
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_u_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_u_b
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_u_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_u_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_adds_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_adds_u_d
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_addv_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_addv_b
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_addv_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_addv_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_addv_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_addv_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_addv_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_addv_d
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_hadd_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hadd_s_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_hadd_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hadd_s_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_hadd_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hadd_s_d
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_hadd_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hadd_u_h
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_hadd_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hadd_u_w
/opt/img/bin/mips-img-linux-gnu-gcc         int-add/test_msa_hadd_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hadd_u_d

#
# Int Average
# -----------
#
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_s_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_s_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_s_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_s_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_u_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_u_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_u_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_ave_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ave_u_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_s_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_s_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_s_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_s_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_s_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_u_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_u_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_u_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_u_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-average/test_msa_aver_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_aver_u_d

#
# Int Compare
# -----------
#
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_ceq_b.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ceq_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_ceq_h.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ceq_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_ceq_w.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ceq_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_ceq_d.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ceq_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_s_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_s_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_s_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_s_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_u_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_u_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_u_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_cle_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_cle_u_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_s_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_s_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_s_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_s_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_u_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_u_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_u_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-compare/test_msa_clt_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_clt_u_d

#
# Int Divide
# ----------
#
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_s_b
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_s_h
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_s_w
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_s_d
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_u_b
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_u_h
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_u_w
/opt/img/bin/mips-img-linux-gnu-gcc      int-divide/test_msa_div_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_div_u_d

#
# Int Dot Product
# ---------------
#
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dotp_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dotp_s_h
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dotp_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dotp_s_w
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dotp_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dotp_s_d
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dotp_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dotp_u_h
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dotp_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dotp_u_w
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dotp_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dotp_u_d
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpadd_s_h.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpadd_s_h
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpadd_s_w.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpadd_s_w
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpadd_s_d.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpadd_s_d
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpadd_u_h.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpadd_u_h
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpadd_u_w.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpadd_u_w
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpadd_u_d.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpadd_u_d
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpsub_s_h.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpsub_s_h
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpsub_s_w.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpsub_s_w
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpsub_s_d.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpsub_s_d
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpsub_u_h.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpsub_u_h
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpsub_u_w.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpsub_u_w
/opt/img/bin/mips-img-linux-gnu-gcc int-dot-product/test_msa_dpsub_u_d.c       \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_dpsub_u_d

#
# Int Max Min
# -----------
#
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_a_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_a_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_a_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_a_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_a_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_a_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_a_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_a_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_s_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_s_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_s_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_s_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_u_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_u_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_u_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_max_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_max_u_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_a_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_a_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_a_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_a_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_a_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_a_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_a_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_a_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_s_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_s_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_s_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_s_d
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_u_b
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_u_h
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_u_w
/opt/img/bin/mips-img-linux-gnu-gcc     int-max-min/test_msa_min_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_min_u_d

#
# Int Modulo
# ----------
#
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_s_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_s_b
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_s_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_s_h
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_s_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_s_w
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_s_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_s_d
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_u_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_u_b
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_u_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_u_h
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_u_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_u_w
/opt/img/bin/mips-img-linux-gnu-gcc      int-modulo/test_msa_mod_u_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mod_u_d

#
# Int Multiply
# ------------
#
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_maddv_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_maddv_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_maddv_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_maddv_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_maddv_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_maddv_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_maddv_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_maddv_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_msubv_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_msubv_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_msubv_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_msubv_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_msubv_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_msubv_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_msubv_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_msubv_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_mulv_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mulv_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_mulv_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mulv_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_mulv_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mulv_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-multiply/test_msa_mulv_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_mulv_d

#
# Int Subtract
# ------------
#
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_s_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_s_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_s_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_s_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_s_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_u_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_u_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_u_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_u_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_asub_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_asub_u_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_hsub_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hsub_s_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_hsub_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hsub_s_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_hsub_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hsub_s_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_hsub_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hsub_u_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_hsub_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hsub_u_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_hsub_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_hsub_u_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_s_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_s_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_s_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_s_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_s_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_s_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_s_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_s_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_u_b.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_u_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_u_h.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_u_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_u_w.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_u_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subs_u_d.c        \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subs_u_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsuu_s_b.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsuu_s_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsuu_s_h.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsuu_s_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsuu_s_w.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsuu_s_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsuu_s_d.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsuu_s_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsus_u_b.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsus_u_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsus_u_h.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsus_u_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsus_u_w.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsus_u_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subsus_u_d.c      \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subsus_u_d
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subv_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subv_b
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subv_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subv_h
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subv_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subv_w
/opt/img/bin/mips-img-linux-gnu-gcc    int-subtract/test_msa_subv_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_subv_d

#
# Interleave
# ----------
#
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvev_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvev_b
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvev_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvev_h
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvev_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvev_w
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvev_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvev_d
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvod_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvod_b
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvod_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvod_h
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvod_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvod_w
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvod_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvod_d
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvl_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvl_b
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvl_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvl_h
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvl_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvl_w
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvl_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvl_d
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvr_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvr_b
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvr_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvr_h
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvr_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvr_w
/opt/img/bin/mips-img-linux-gnu-gcc      interleave/test_msa_ilvr_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_ilvr_d

#
# Logic
# -----
#
/opt/img/bin/mips-img-linux-gnu-gcc           logic/test_msa_and_v.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_and_v
/opt/img/bin/mips-img-linux-gnu-gcc           logic/test_msa_nor_v.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_nor_v
/opt/img/bin/mips-img-linux-gnu-gcc           logic/test_msa_or_v.c            \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_or_v
/opt/img/bin/mips-img-linux-gnu-gcc           logic/test_msa_xor_v.c           \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_xor_v

#
# Move
# ----
#
/opt/img/bin/mips-img-linux-gnu-gcc            move/test_msa_move_v.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_move_v

#
# Pack
# ----
#
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckev_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckev_b
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckev_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckev_h
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckev_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckev_w
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckev_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckev_d
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckod_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckod_b
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckod_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckod_h
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckod_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckod_w
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_pckod_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_pckod_d
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_vshf_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_vshf_b
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_vshf_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_vshf_h
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_vshf_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_vshf_w
/opt/img/bin/mips-img-linux-gnu-gcc            pack/test_msa_vshf_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o  /tmp/test_msa_vshf_d

#
# Shift
# -----
#
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sll_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sll_b
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sll_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sll_h
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sll_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sll_w
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sll_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sll_d
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sra_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sra_b
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sra_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sra_h
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sra_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sra_w
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_sra_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_sra_d
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srar_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srar_b
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srar_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srar_h
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srar_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srar_w
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srar_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srar_d
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srl_b.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srl_b
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srl_h.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srl_h
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srl_w.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srl_w
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srl_d.c          \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srl_d
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srlr_b.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srlr_b
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srlr_h.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srlr_h
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srlr_w.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srlr_w
/opt/img/bin/mips-img-linux-gnu-gcc            shift/test_msa_srlr_d.c         \
-EL -static -mabi=64 -march=mips64r6 -mmsa -o   /tmp/test_msa_srlr_d
