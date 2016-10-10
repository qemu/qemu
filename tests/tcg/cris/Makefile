-include ../../../config-host.mak

CROSS=crisv32-axis-linux-gnu-
SIM=../../../cris-linux-user/qemu-cris -L ./
SIMG=cris-axis-linux-gnu-run --sysroot=./

CC      = $(CROSS)gcc
#AS      = $(CROSS)as
AS	= $(CC) -x assembler-with-cpp
SIZE    = $(CROSS)size
LD      = $(CC)
OBJCOPY = $(CROSS)objcopy

# we rely on GCC inline:ing the stuff we tell it to in many places here.
CFLAGS  = -Winline -Wall -g -O2 -static
NOSTDFLAGS = -nostartfiles -nostdlib
ASFLAGS += -g -Wa,-I,$(SRC_PATH)/tests/tcg/cris/
LDLIBS  =
NOSTDLIBS = -lgcc

CRT        = crt.o
SYS        = sys.o
TESTCASES += check_abs.tst
TESTCASES += check_addc.tst
TESTCASES += check_addcm.tst
TESTCASES += check_addcv17.tst
TESTCASES += check_addo.tst
TESTCASES += check_addoq.tst
TESTCASES += check_addi.tst
TESTCASES += check_addiv32.tst
TESTCASES += check_addm.tst
TESTCASES += check_addr.tst
TESTCASES += check_addq.tst
TESTCASES += check_addxc.tst
TESTCASES += check_addxm.tst
TESTCASES += check_addxr.tst
TESTCASES += check_andc.tst
TESTCASES += check_andm.tst
TESTCASES += check_andr.tst
TESTCASES += check_andq.tst
TESTCASES += check_asr.tst
TESTCASES += check_ba.tst
TESTCASES += check_bas.tst
TESTCASES += check_bcc.tst
TESTCASES += check_bound.tst
TESTCASES += check_boundc.tst
TESTCASES += check_boundr.tst
TESTCASES += check_btst.tst
TESTCASES += check_clearfv32.tst
TESTCASES += check_cmpc.tst
TESTCASES += check_cmpr.tst
TESTCASES += check_cmpq.tst
TESTCASES += check_cmpm.tst
TESTCASES += check_cmpxc.tst
TESTCASES += check_cmpxm.tst
TESTCASES += check_cmp-2.tst
TESTCASES += check_clrjmp1.tst
TESTCASES += check_dstep.tst
TESTCASES += check_ftag.tst
TESTCASES += check_int64.tst
# check_jsr is broken.
#TESTCASES += check_jsr.tst
TESTCASES += check_mcp.tst
TESTCASES += check_movei.tst
TESTCASES += check_mover.tst
TESTCASES += check_moverm.tst
TESTCASES += check_moveq.tst
TESTCASES += check_movemr.tst
TESTCASES += check_movemrv32.tst
TESTCASES += check_movecr.tst
TESTCASES += check_movmp.tst
TESTCASES += check_movpr.tst
TESTCASES += check_movprv32.tst
TESTCASES += check_movdelsr1.tst
TESTCASES += check_movpmv32.tst
TESTCASES += check_movsr.tst
TESTCASES += check_movsm.tst
TESTCASES += check_movscr.tst
TESTCASES += check_movur.tst
TESTCASES += check_movum.tst
TESTCASES += check_movucr.tst
TESTCASES += check_mulx.tst
TESTCASES += check_mulv32.tst
TESTCASES += check_neg.tst
TESTCASES += check_not.tst
TESTCASES += check_lz.tst
TESTCASES += check_lapc.tst
TESTCASES += check_lsl.tst
TESTCASES += check_lsr.tst
TESTCASES += check_orc.tst
TESTCASES += check_orm.tst
TESTCASES += check_orr.tst
TESTCASES += check_orq.tst
TESTCASES += check_ret.tst
TESTCASES += check_swap.tst
TESTCASES += check_scc.tst
TESTCASES += check_subc.tst
TESTCASES += check_subq.tst
TESTCASES += check_subr.tst
TESTCASES += check_subm.tst
TESTCASES += check_glibc_kernelversion.tst
TESTCASES += check_xarith.tst

TESTCASES += check_hello.ctst
TESTCASES += check_stat1.ctst
TESTCASES += check_stat2.ctst
TESTCASES += check_stat3.ctst
TESTCASES += check_stat4.ctst
TESTCASES += check_openpf1.ctst
TESTCASES += check_openpf2.ctst
TESTCASES += check_openpf3.ctst
TESTCASES += check_openpf5.ctst
TESTCASES += check_mapbrk.ctst
TESTCASES += check_mmap1.ctst
TESTCASES += check_mmap2.ctst
TESTCASES += check_mmap3.ctst
TESTCASES += check_sigalrm.ctst
TESTCASES += check_time2.ctst
TESTCASES += check_settls1.ctst

TESTCASES += check_gcctorture_pr28634-1.ctst
#TESTCASES += check_gcctorture_pr28634.ctst

all: build

%.o: $(SRC_PATH)/tests/tcg/cris/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(SRC_PATH)/tests/tcg/cris/%.s
	$(AS) $(ASFLAGS) -c $< -o $@

%.tst: %.o
	$(CC) $(CFLAGS) $(NOSTDFLAGS) $(LDLIBS) $(NOSTDLIBS) $(CRT) $< $(SYS) -o $@

%.ctst: %.o
	$(CC) $(CFLAGS) $(LDLIBS) $< -o $@


sysv10.o: sys.c
	$(CC) $(CFLAGS) -mcpu=v10 -c $< -o $@

crtv10.o: crt.s
	$(AS) $(ASFLAGS) -mcpu=v10 -c $< -o $@

check_addcv17.tst: ASFLAGS += -mcpu=v10
check_addcv17.tst: CRT := crtv10.o
check_addcv17.tst: SYS := sysv10.o
check_addcv17.tst: crtv10.o sysv10.o

build: $(CRT) $(SYS) $(TESTCASES)

check: $(CRT) $(SYS) $(TESTCASES)
	@echo -e "\nQEMU simulator."
	for case in $(TESTCASES); do \
		echo -n "$$case "; \
		SIMARGS=; \
		case $$case in *v17*) SIMARGS="-cpu crisv17";; esac; \
		$(SIM) $$SIMARGS ./$$case; \
	done
check-g: $(CRT) $(SYS) $(TESTCASES)
	@echo -e "\nGDB simulator."
	@for case in $(TESTCASES); do \
		echo -n "$$case "; \
		$(SIMG) $$case; \
	done

clean:
	$(RM) -fr $(TESTCASES) *.o
