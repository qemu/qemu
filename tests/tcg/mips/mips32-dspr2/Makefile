-include ../../config-host.mak

CROSS=mips64el-unknown-linux-gnu-

SIM=qemu-mipsel
SIM_FLAGS=-cpu 74Kf

CC      = $(CROSS)gcc
CFLAGS  = -mabi=32 -march=mips32r2 -mgp32 -mdspr2 -static

TESTCASES = absq_s_qb.tst
TESTCASES += addqh_ph.tst
TESTCASES += addqh_r_ph.tst
TESTCASES += addqh_r_w.tst
TESTCASES += addqh_w.tst
TESTCASES += adduh_qb.tst
TESTCASES += adduh_r_qb.tst
TESTCASES += addu_ph.tst
TESTCASES += addu_s_ph.tst
TESTCASES += append.tst
TESTCASES += balign.tst
TESTCASES += cmpgdu_eq_qb.tst
TESTCASES += cmpgdu_le_qb.tst
TESTCASES += cmpgdu_lt_qb.tst
TESTCASES += dpaqx_sa_w_ph.tst
TESTCASES += dpa_w_ph.tst
TESTCASES += dpax_w_ph.tst
TESTCASES += dpaqx_s_w_ph.tst
TESTCASES += dpsqx_sa_w_ph.tst
TESTCASES += dpsqx_s_w_ph.tst
TESTCASES += dps_w_ph.tst
TESTCASES += dpsx_w_ph.tst
TESTCASES += mul_ph.tst
TESTCASES += mulq_rs_w.tst
TESTCASES += mulq_s_ph.tst
TESTCASES += mulq_s_w.tst
TESTCASES += mulsaq_s_w_ph.tst
TESTCASES += mulsa_w_ph.tst
TESTCASES += mul_s_ph.tst
TESTCASES += precr_qb_ph.tst
TESTCASES += precr_sra_ph_w.tst
TESTCASES += precr_sra_r_ph_w.tst
TESTCASES += prepend.tst
TESTCASES += shra_qb.tst
TESTCASES += shra_r_qb.tst
TESTCASES += shrav_qb.tst
TESTCASES += shrav_r_qb.tst
TESTCASES += shrl_ph.tst
TESTCASES += shrlv_ph.tst
TESTCASES += subqh_ph.tst
TESTCASES += subqh_r_ph.tst
TESTCASES += subqh_r_w.tst
TESTCASES += subqh_w.tst
TESTCASES += subuh_qb.tst
TESTCASES += subuh_r_qb.tst
TESTCASES += subu_ph.tst
TESTCASES += subu_s_ph.tst

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
