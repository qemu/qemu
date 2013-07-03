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
          -pipe -march=mips64r2 -mgp64 -mdspr2 -static -Wa,--trap -msym32 \
          -DKBUILD_64BIT_SYM32 -I./

LDFLAGS = -T./mips_boot.lds -L./
FLAGS = -nostdlib -mabi=64 -march=mips64r2 -mgp64 -mdspr2

TESTCASES = absq_s_qb.tst
TESTCASES += addqh_ph.tst
TESTCASES += addqh_r_ph.tst
TESTCASES += addqh_r_w.tst
TESTCASES += addqh_w.tst
#TESTCASES += adduh_ob.tst
TESTCASES += adduh_qb.tst
#TESTCASES += adduh_r_ob.tst
TESTCASES += adduh_r_qb.tst
TESTCASES += addu_ph.tst
#TESTCASES += addu_qh.tst
TESTCASES += addu_s_ph.tst
#TESTCASES += addu_s_qh.tst
TESTCASES += append.tst
TESTCASES += balign.tst
#TESTCASES += cmpgdu_eq_ob.tst
TESTCASES += cmpgdu_eq_qb.tst
#TESTCASES += cmpgdu_le_ob.tst
TESTCASES += cmpgdu_le_qb.tst
#TESTCASES += cmpgdu_lt_ob.tst
TESTCASES += cmpgdu_lt_qb.tst
#TESTCASES += dbalign.tst
TESTCASES += dpaqx_sa_w_ph.tst
TESTCASES += dpaqx_s_w_ph.tst
TESTCASES += dpa_w_ph.tst
#TESTCASES += dpa_w_qh.tst
TESTCASES += dpax_w_ph.tst
TESTCASES += dpsqx_sa_w_ph.tst
TESTCASES += dpsqx_s_w_ph.tst
TESTCASES += dps_w_ph.tst
#TESTCASES += dps_w_qh.tst
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
#TESTCASES += shrav_ob.tst
TESTCASES += shrav_qb.tst
#TESTCASES += shrav_r_ob.tst
TESTCASES += shrav_r_qb.tst
TESTCASES += shrl_ph.tst
TESTCASES += shrlv_ph.tst
TESTCASES += subqh_ph.tst
TESTCASES += subqh_r_ph.tst
TESTCASES += subqh_r_w.tst
TESTCASES += subqh_w.tst
#TESTCASES += subuh_ob.tst
TESTCASES += subuh_qb.tst
#TESTCASES += subuh_r_ob.tst
TESTCASES += subuh_r_qb.tst
TESTCASES += subu_ph.tst
#TESTCASES += subu_qh.tst
TESTCASES += subu_s_ph.tst
#TESTCASES += subu_s_qh.tst

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
