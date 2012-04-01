-include ../../../config-host.mak

CROSS=lm32-elf-

SIM = qemu-system-lm32
SIMFLAGS = -M lm32-evr -nographic -device lm32-sys -net none -kernel

CC      = $(CROSS)gcc
AS      = $(CROSS)as
AS      = $(CC) -x assembler
SIZE    = $(CROSS)size
LD      = $(CC)
OBJCOPY = $(CROSS)objcopy

TSRC_PATH = $(SRC_PATH)/tests/tcg/lm32

LDFLAGS = -T$(TSRC_PATH)/linker.ld
ASFLAGS += -Wa,-I,$(TSRC_PATH)/

CRT        = crt.o
TESTCASES += test_add.tst
TESTCASES += test_addi.tst
TESTCASES += test_and.tst
TESTCASES += test_andhi.tst
TESTCASES += test_andi.tst
TESTCASES += test_b.tst
TESTCASES += test_be.tst
TESTCASES += test_bg.tst
TESTCASES += test_bge.tst
TESTCASES += test_bgeu.tst
TESTCASES += test_bgu.tst
TESTCASES += test_bi.tst
TESTCASES += test_bne.tst
TESTCASES += test_break.tst
TESTCASES += test_bret.tst
TESTCASES += test_call.tst
TESTCASES += test_calli.tst
TESTCASES += test_cmpe.tst
TESTCASES += test_cmpei.tst
TESTCASES += test_cmpg.tst
TESTCASES += test_cmpgi.tst
TESTCASES += test_cmpge.tst
TESTCASES += test_cmpgei.tst
TESTCASES += test_cmpgeu.tst
TESTCASES += test_cmpgeui.tst
TESTCASES += test_cmpgu.tst
TESTCASES += test_cmpgui.tst
TESTCASES += test_cmpne.tst
TESTCASES += test_cmpnei.tst
TESTCASES += test_divu.tst
TESTCASES += test_eret.tst
TESTCASES += test_lb.tst
TESTCASES += test_lbu.tst
TESTCASES += test_lh.tst
TESTCASES += test_lhu.tst
TESTCASES += test_lw.tst
TESTCASES += test_modu.tst
TESTCASES += test_mul.tst
TESTCASES += test_muli.tst
TESTCASES += test_nor.tst
TESTCASES += test_nori.tst
TESTCASES += test_or.tst
TESTCASES += test_ori.tst
TESTCASES += test_orhi.tst
#TESTCASES += test_rcsr.tst
TESTCASES += test_ret.tst
TESTCASES += test_sb.tst
TESTCASES += test_scall.tst
TESTCASES += test_sextb.tst
TESTCASES += test_sexth.tst
TESTCASES += test_sh.tst
TESTCASES += test_sl.tst
TESTCASES += test_sli.tst
TESTCASES += test_sr.tst
TESTCASES += test_sri.tst
TESTCASES += test_sru.tst
TESTCASES += test_srui.tst
TESTCASES += test_sub.tst
TESTCASES += test_sw.tst
#TESTCASES += test_wcsr.tst
TESTCASES += test_xnor.tst
TESTCASES += test_xnori.tst
TESTCASES += test_xor.tst
TESTCASES += test_xori.tst

all: build

%.o: $(TSRC_PATH)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: $(TSRC_PATH)/%.S
	$(AS) $(ASFLAGS) -c $< -o $@

%.tst: %.o $(TSRC_PATH)/macros.inc $(CRT)
	$(LD) $(LDFLAGS) $(NOSTDFLAGS) $(CRT) $< -o $@

build: $(CRT) $(TESTCASES)

check: $(CRT) $(SYS) $(TESTCASES)
	@for case in $(TESTCASES); do \
		$(SIM) $(SIMFLAGS) ./$$case; \
	done

clean:
	$(RM) -fr $(TESTCASES) $(CRT)
