-include ../../config-host.mak

CROSS = or32-linux-

SIM = qemu-or32

CC = $(CROSS)gcc

TESTCASES = test_add.tst
TESTCASES += test_sub.tst
TESTCASES += test_addc.tst
TESTCASES += test_addi.tst
TESTCASES += test_addic.tst
TESTCASES += test_and_or.tst
TESTCASES += test_bf.tst
TESTCASES += test_bnf.tst
TESTCASES += test_div.tst
TESTCASES += test_divu.tst
TESTCASES += test_extx.tst
TESTCASES += test_fx.tst
TESTCASES += test_jal.tst
TESTCASES += test_j.tst
TESTCASES += test_lf_div.tst
TESTCASES += test_lf_eqs.tst
TESTCASES += test_lf_ges.tst
TESTCASES += test_lf_gts.tst
TESTCASES += test_lf_les.tst
TESTCASES += test_lf_lts.tst
TESTCASES += test_lf_mul.tst
TESTCASES += test_lf_nes.tst
TESTCASES += test_lf_rem.tst
TESTCASES += test_lf_sub.tst
TESTCASES += test_lf_add.tst
TESTCASES += test_logic.tst
TESTCASES += test_lx.tst
TESTCASES += test_movhi.tst
TESTCASES += test_mul.tst
TESTCASES += test_mulu.tst
TESTCASES += test_muli.tst
TESTCASES += test_sfeq.tst
TESTCASES += test_sfeqi.tst
TESTCASES += test_sfges.tst
TESTCASES += test_sfgesi.tst
TESTCASES += test_sfgeu.tst
TESTCASES += test_sfgeui.tst
TESTCASES += test_sfgts.tst
TESTCASES += test_sfgtsi.tst
TESTCASES += test_sfgtu.tst
TESTCASES += test_sfgtui.tst
TESTCASES += test_sfles.tst
TESTCASES += test_sflesi.tst
TESTCASES += test_sfleu.tst
TESTCASES += test_sfleui.tst
TESTCASES += test_sflts.tst
TESTCASES += test_sfltsi.tst
TESTCASES += test_sfltu.tst
TESTCASES += test_sfltui.tst
TESTCASES += test_sfne.tst
TESTCASES += test_sfnei.tst

all: $(TESTCASES)

%.tst: %.c
	$(CC) -static $< -o $@


check: $(TESTCASES)
	@for case in $(TESTCASES); do $(SIM) $$case; echo $$case pass!; sleep 0.2; done

clean:
	$(RM) -rf $(TESTCASES)
