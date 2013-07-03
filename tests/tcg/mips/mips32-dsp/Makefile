-include ../../config-host.mak

CROSS=mips64el-unknown-linux-gnu-

SIM=qemu-mipsel
SIM_FLAGS=-cpu 74Kf

CC      = $(CROSS)gcc
CFLAGS  = -mabi=32 -march=mips32r2 -mgp32 -mdsp -static

TESTCASES = absq_s_ph.tst
TESTCASES += absq_s_w.tst
TESTCASES += addq_ph.tst
TESTCASES += addq_s_ph.tst
TESTCASES += addq_s_w.tst
TESTCASES += addsc.tst
TESTCASES += addu_qb.tst
TESTCASES += addu_s_qb.tst
TESTCASES += addwc.tst
TESTCASES += bitrev.tst
TESTCASES += bposge32.tst
TESTCASES += cmp_eq_ph.tst
TESTCASES += cmpgu_eq_qb.tst
TESTCASES += cmpgu_le_qb.tst
TESTCASES += cmpgu_lt_qb.tst
TESTCASES += cmp_le_ph.tst
TESTCASES += cmp_lt_ph.tst
TESTCASES += cmpu_eq_qb.tst
TESTCASES += cmpu_le_qb.tst
TESTCASES += cmpu_lt_qb.tst
TESTCASES += dpaq_sa_l_w.tst
TESTCASES += dpaq_s_w_ph.tst
TESTCASES += dpau_h_qbl.tst
TESTCASES += dpau_h_qbr.tst
TESTCASES += dpsq_sa_l_w.tst
TESTCASES += dpsq_s_w_ph.tst
TESTCASES += dpsu_h_qbl.tst
TESTCASES += dpsu_h_qbr.tst
TESTCASES += extp.tst
TESTCASES += extpdp.tst
TESTCASES += extpdpv.tst
TESTCASES += extpv.tst
TESTCASES += extr_rs_w.tst
TESTCASES += extr_r_w.tst
TESTCASES += extr_s_h.tst
TESTCASES += extrv_rs_w.tst
TESTCASES += extrv_r_w.tst
TESTCASES += extrv_s_h.tst
TESTCASES += extrv_w.tst
TESTCASES += extr_w.tst
TESTCASES += insv.tst
TESTCASES += lbux.tst
TESTCASES += lhx.tst
TESTCASES += lwx.tst
TESTCASES += madd.tst
TESTCASES += maddu.tst
TESTCASES += maq_sa_w_phl.tst
TESTCASES += maq_sa_w_phr.tst
TESTCASES += maq_s_w_phl.tst
TESTCASES += maq_s_w_phr.tst
TESTCASES += mfhi.tst
TESTCASES += mflo.tst
TESTCASES += modsub.tst
TESTCASES += msub.tst
TESTCASES += msubu.tst
TESTCASES += mthi.tst
TESTCASES += mthlip.tst
TESTCASES += mtlo.tst
TESTCASES += muleq_s_w_phl.tst
TESTCASES += muleq_s_w_phr.tst
TESTCASES += muleu_s_ph_qbl.tst
TESTCASES += muleu_s_ph_qbr.tst
TESTCASES += mulq_rs_ph.tst
TESTCASES += mult.tst
TESTCASES += multu.tst
TESTCASES += packrl_ph.tst
TESTCASES += pick_ph.tst
TESTCASES += pick_qb.tst
TESTCASES += precequ_ph_qbla.tst
TESTCASES += precequ_ph_qbl.tst
TESTCASES += precequ_ph_qbra.tst
TESTCASES += precequ_ph_qbr.tst
TESTCASES += preceq_w_phl.tst
TESTCASES += preceq_w_phr.tst
TESTCASES += preceu_ph_qbla.tst
TESTCASES += preceu_ph_qbl.tst
TESTCASES += preceu_ph_qbra.tst
TESTCASES += preceu_ph_qbr.tst
TESTCASES += precrq_ph_w.tst
TESTCASES += precrq_qb_ph.tst
TESTCASES += precrq_rs_ph_w.tst
TESTCASES += precrqu_s_qb_ph.tst
TESTCASES += raddu_w_qb.tst
TESTCASES += rddsp.tst
TESTCASES += repl_ph.tst
TESTCASES += repl_qb.tst
TESTCASES += replv_ph.tst
TESTCASES += replv_qb.tst
TESTCASES += shilo.tst
TESTCASES += shilov.tst
TESTCASES += shll_ph.tst
TESTCASES += shll_qb.tst
TESTCASES += shll_s_ph.tst
TESTCASES += shll_s_w.tst
TESTCASES += shllv_ph.tst
TESTCASES += shllv_qb.tst
TESTCASES += shllv_s_ph.tst
TESTCASES += shllv_s_w.tst
TESTCASES += shra_ph.tst
TESTCASES += shra_r_ph.tst
TESTCASES += shra_r_w.tst
TESTCASES += shrav_ph.tst
TESTCASES += shrav_r_ph.tst
TESTCASES += shrav_r_w.tst
TESTCASES += shrl_qb.tst
TESTCASES += shrlv_qb.tst
TESTCASES += subq_ph.tst
TESTCASES += subq_s_ph.tst
TESTCASES += subq_s_w.tst
TESTCASES += subu_qb.tst
TESTCASES += subu_s_qb.tst
TESTCASES += wrdsp.tst

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
