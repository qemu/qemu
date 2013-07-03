
CROSS_COMPILE	?= mips64el-unknown-linux-gnu-

SIM = qemu-system-mips64el
SIMFLAGS = -nographic -cpu mips64dspr2 -kernel

AS      = $(CROSS_COMPILE)as
LD      = $(CROSS_COMPILE)ld
CC      = $(CROSS_COMPILE)gcc
AR      = $(CROSS_COMPILE)ar
NM      = $(CROSS_COMPILE)nm
STRIP       = $(CROSS_COMPILE)strip
RANLIB      = $(CROSS_COMPILE)ranlib
OBJCOPY     = $(CROSS_COMPILE)objcopy
OBJDUMP     = $(CROSS_COMPILE)objdump

VECTORS_OBJ ?= ./head.o ./printf.o

HEAD_FLAGS ?= -nostdinc -mabi=64 -G 0 -mno-abicalls -fno-pic -pipe \
              -msoft-float -march=mips64 -Wa,-mips64 -Wa,--trap \
              -msym32 -DKBUILD_64BIT_SYM32 -I./

CFLAGS ?= -nostdinc -mabi=64 -G 0 -mno-abicalls -fno-pic -fno-builtin  \
          -pipe -march=mips64r2 -mgp64 -mdsp -static -Wa,--trap -msym32 \
          -DKBUILD_64BIT_SYM32 -I./

LDFLAGS = -T./mips_boot.lds -L./
FLAGS = -nostdlib -mabi=64 -march=mips64r2 -mgp64 -mdsp


#TESTCASES = absq_s_ob.tst
TESTCASES = absq_s_ph.tst
TESTCASES += absq_s_pw.tst
TESTCASES += absq_s_qh.tst
TESTCASES += absq_s_w.tst
TESTCASES += addq_ph.tst
TESTCASES += addq_pw.tst
TESTCASES += addq_qh.tst
TESTCASES += addq_s_ph.tst
TESTCASES += addq_s_pw.tst
TESTCASES += addq_s_qh.tst
TESTCASES += addq_s_w.tst
TESTCASES += addsc.tst
TESTCASES += addu_ob.tst
TESTCASES += addu_qb.tst
TESTCASES += addu_s_ob.tst
TESTCASES += addu_s_qb.tst
TESTCASES += addwc.tst
TESTCASES += bitrev.tst
TESTCASES += bposge32.tst
TESTCASES += bposge64.tst
TESTCASES += cmp_eq_ph.tst
TESTCASES += cmp_eq_pw.tst
TESTCASES += cmp_eq_qh.tst
TESTCASES += cmpgu_eq_ob.tst
TESTCASES += cmpgu_eq_qb.tst
TESTCASES += cmpgu_le_ob.tst
TESTCASES += cmpgu_le_qb.tst
TESTCASES += cmpgu_lt_ob.tst
TESTCASES += cmpgu_lt_qb.tst
TESTCASES += cmp_le_ph.tst
TESTCASES += cmp_le_pw.tst
TESTCASES += cmp_le_qh.tst
TESTCASES += cmp_lt_ph.tst
TESTCASES += cmp_lt_pw.tst
TESTCASES += cmp_lt_qh.tst
TESTCASES += cmpu_eq_ob.tst
TESTCASES += cmpu_eq_qb.tst
TESTCASES += cmpu_le_ob.tst
TESTCASES += cmpu_le_qb.tst
TESTCASES += cmpu_lt_ob.tst
TESTCASES += cmpu_lt_qb.tst
#TESTCASES += dappend.tst
TESTCASES += dextp.tst
TESTCASES += dextpdp.tst
TESTCASES += dextpdpv.tst
TESTCASES += dextpv.tst
TESTCASES += dextr_l.tst
TESTCASES += dextr_r_l.tst
TESTCASES += dextr_rs_l.tst
TESTCASES += dextr_rs_w.tst
TESTCASES += dextr_r_w.tst
TESTCASES += dextr_s_h.tst
TESTCASES += dextrv_l.tst
TESTCASES += dextrv_r_l.tst
TESTCASES += dextrv_rs_l.tst
TESTCASES += dextrv_rs_w.tst
TESTCASES += dextrv_r_w.tst
TESTCASES += dextrv_s_h.tst
TESTCASES += dextrv_w.tst
TESTCASES += dextr_w.tst
TESTCASES += dinsv.tst
TESTCASES += dmadd.tst
TESTCASES += dmaddu.tst
TESTCASES += dmsub.tst
TESTCASES += dmsubu.tst
TESTCASES += dmthlip.tst
TESTCASES += dpaq_sa_l_pw.tst
TESTCASES += dpaq_sa_l_w.tst
TESTCASES += dpaq_s_w_ph.tst
TESTCASES += dpaq_s_w_qh.tst
TESTCASES += dpau_h_obl.tst
TESTCASES += dpau_h_obr.tst
TESTCASES += dpau_h_qbl.tst
TESTCASES += dpau_h_qbr.tst
TESTCASES += dpsq_sa_l_pw.tst
TESTCASES += dpsq_sa_l_w.tst
TESTCASES += dpsq_s_w_ph.tst
TESTCASES += dpsq_s_w_qh.tst
TESTCASES += dpsu_h_obl.tst
TESTCASES += dpsu_h_obr.tst
TESTCASES += dpsu_h_qbl.tst
TESTCASES += dpsu_h_qbr.tst
TESTCASES += dshilo.tst
TESTCASES += dshilov.tst
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
TESTCASES += ldx.tst
TESTCASES += madd.tst
TESTCASES += maddu.tst
TESTCASES += maq_sa_w_phl.tst
TESTCASES += maq_sa_w_phr.tst
TESTCASES += maq_sa_w_qhll.tst
TESTCASES += maq_sa_w_qhlr.tst
TESTCASES += maq_sa_w_qhrl.tst
TESTCASES += maq_sa_w_qhrr.tst
TESTCASES += maq_s_l_pwl.tst
TESTCASES += maq_s_l_pwr.tst
TESTCASES += maq_s_w_phl.tst
TESTCASES += maq_s_w_phr.tst
TESTCASES += maq_s_w_qhll.tst
TESTCASES += maq_s_w_qhlr.tst
TESTCASES += maq_s_w_qhrl.tst
TESTCASES += maq_s_w_qhrr.tst
TESTCASES += mfhi.tst
TESTCASES += mflo.tst
TESTCASES += modsub.tst
TESTCASES += msub.tst
TESTCASES += msubu.tst
TESTCASES += mthi.tst
TESTCASES += mthlip.tst
TESTCASES += mtlo.tst
TESTCASES += muleq_s_pw_qhl.tst
TESTCASES += muleq_s_pw_qhr.tst
TESTCASES += muleq_s_w_phl.tst
TESTCASES += muleq_s_w_phr.tst
TESTCASES += muleu_s_ph_qbl.tst
TESTCASES += muleu_s_ph_qbr.tst
TESTCASES += muleu_s_qh_obl.tst
TESTCASES += muleu_s_qh_obr.tst
TESTCASES += mulq_rs_ph.tst
TESTCASES += mulq_rs_qh.tst
TESTCASES += mulsaq_s_l_pw.tst
TESTCASES += mulsaq_s_w_qh.tst
TESTCASES += mult.tst
TESTCASES += multu.tst
TESTCASES += packrl_ph.tst
TESTCASES += packrl_pw.tst
TESTCASES += pick_ob.tst
TESTCASES += pick_ph.tst
TESTCASES += pick_pw.tst
TESTCASES += pick_qb.tst
TESTCASES += pick_qh.tst
#TESTCASES += preceq_l_pwl.tst
#TESTCASES += preceq_l_pwr.tst
TESTCASES += preceq_pw_qhla.tst
TESTCASES += preceq_pw_qhl.tst
TESTCASES += preceq_pw_qhra.tst
TESTCASES += preceq_pw_qhr.tst
TESTCASES += precequ_ph_qbla.tst
TESTCASES += precequ_ph_qbl.tst
TESTCASES += precequ_ph_qbra.tst
TESTCASES += precequ_ph_qbr.tst
#TESTCASES += precequ_qh_obla.tst
#TESTCASES += precequ_qh_obl.tst
#TESTCASES += precequ_qh_obra.tst
#TESTCASES += precequ_qh_obr.tst
TESTCASES += preceq_w_phl.tst
TESTCASES += preceq_w_phr.tst
TESTCASES += preceu_ph_qbla.tst
TESTCASES += preceu_ph_qbl.tst
TESTCASES += preceu_ph_qbra.tst
TESTCASES += preceu_ph_qbr.tst
TESTCASES += preceu_qh_obla.tst
TESTCASES += preceu_qh_obl.tst
TESTCASES += preceu_qh_obra.tst
TESTCASES += preceu_qh_obr.tst
#TESTCASES += precr_ob_qh.tst
TESTCASES += precrq_ob_qh.tst
TESTCASES += precrq_ph_w.tst
TESTCASES += precrq_pw_l.tst
TESTCASES += precrq_qb_ph.tst
TESTCASES += precrq_qh_pw.tst
TESTCASES += precrq_rs_ph_w.tst
TESTCASES += precrq_rs_qh_pw.tst
TESTCASES += precrqu_s_ob_qh.tst
TESTCASES += precrqu_s_qb_ph.tst
#TESTCASES += precr_sra_qh_pw.tst
#TESTCASES += precr_sra_r_qh_pw.tst
#TESTCASES += prependd.tst
#TESTCASES += prependw.tst
#TESTCASES += raddu_l_ob.tst
TESTCASES += raddu_w_qb.tst
TESTCASES += rddsp.tst
TESTCASES += repl_ob.tst
TESTCASES += repl_ph.tst
TESTCASES += repl_pw.tst
TESTCASES += repl_qb.tst
TESTCASES += repl_qh.tst
TESTCASES += replv_ob.tst
TESTCASES += replv_ph.tst
TESTCASES += replv_pw.tst
TESTCASES += replv_qb.tst
TESTCASES += shilo.tst
TESTCASES += shilov.tst
TESTCASES += shll_ob.tst
TESTCASES += shll_ph.tst
TESTCASES += shll_pw.tst
TESTCASES += shll_qb.tst
TESTCASES += shll_qh.tst
TESTCASES += shll_s_ph.tst
TESTCASES += shll_s_pw.tst
TESTCASES += shll_s_qh.tst
TESTCASES += shll_s_w.tst
TESTCASES += shllv_ob.tst
TESTCASES += shllv_ph.tst
TESTCASES += shllv_pw.tst
TESTCASES += shllv_qb.tst
TESTCASES += shllv_qh.tst
TESTCASES += shllv_s_ph.tst
TESTCASES += shllv_s_pw.tst
TESTCASES += shllv_s_qh.tst
TESTCASES += shllv_s_w.tst
#TESTCASES += shra_ob.tst
TESTCASES += shra_ph.tst
TESTCASES += shra_pw.tst
TESTCASES += shra_qh.tst
#TESTCASES += shra_r_ob.tst
TESTCASES += shra_r_ph.tst
TESTCASES += shra_r_pw.tst
TESTCASES += shra_r_qh.tst
TESTCASES += shra_r_w.tst
TESTCASES += shrav_ph.tst
TESTCASES += shrav_pw.tst
TESTCASES += shrav_qh.tst
TESTCASES += shrav_r_ph.tst
TESTCASES += shrav_r_pw.tst
TESTCASES += shrav_r_qh.tst
TESTCASES += shrav_r_w.tst
TESTCASES += shrl_ob.tst
TESTCASES += shrl_qb.tst
#TESTCASES += shrl_qh.tst
TESTCASES += shrlv_ob.tst
TESTCASES += shrlv_qb.tst
#TESTCASES += shrlv_qh.tst
TESTCASES += subq_ph.tst
TESTCASES += subq_pw.tst
TESTCASES += subq_qh.tst
TESTCASES += subq_s_ph.tst
TESTCASES += subq_s_pw.tst
TESTCASES += subq_s_qh.tst
TESTCASES += subq_s_w.tst
TESTCASES += subu_ob.tst
TESTCASES += subu_qb.tst
TESTCASES += subu_s_ob.tst
TESTCASES += subu_s_qb.tst
TESTCASES += wrdsp.tst

all: build

head.o : head.S
	$(Q)$(CC) $(HEAD_FLAGS) -D"STACK_TOP=0xffffffff80200000" -c $< -o $@

%.o  : %.S
	$(CC) $(CFLAGS) -c $< -o $@

%.o  : %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.tst: %.o $(VECTORS_OBJ)
	$(CC) $(VECTORS_OBJ) $(FLAGS) $(LDFLAGS) $< -o $@

build: $(VECTORS_OBJ) $(MIPSSOC_LIB) $(TESTCASES)

check:  $(VECTORS_OBJ) $(MIPSSOC_LIB) $(TESTCASES)
	@for case in $(TESTCASES); do \
		echo $(SIM) $(SIMFLAGS) ./$$case; \
		$(SIM) $(SIMFLAGS) ./$$case & (sleep 1; killall $(SIM)); \
	done

clean:
	$(Q)rm -f *.o *.tst *.a
