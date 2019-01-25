-include ../../../../config-host.mak

CROSS=mipsr5900el-unknown-linux-gnu-

SIM=qemu-mipsel
SIM_FLAGS=-cpu R5900

CC      = $(CROSS)gcc
CFLAGS  = -Wall -mabi=32 -march=r5900 -static

TESTCASES  = test_r5900_div1.tst
TESTCASES += test_r5900_divu1.tst
TESTCASES += test_r5900_madd.tst
TESTCASES += test_r5900_maddu.tst
TESTCASES += test_r5900_mflohi1.tst
TESTCASES += test_r5900_mtlohi1.tst
TESTCASES += test_r5900_mult.tst
TESTCASES += test_r5900_multu.tst

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
