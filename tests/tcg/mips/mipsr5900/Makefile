-include ../../config-host.mak

CROSS=mipsr5900el-unknown-linux-gnu-

SIM=qemu-mipsel
SIM_FLAGS=-cpu R5900

CC      = $(CROSS)gcc
CFLAGS  = -Wall -mabi=32 -march=r5900 -static

TESTCASES = div1.tst
TESTCASES += divu1.tst
TESTCASES += mflohi1.tst
TESTCASES += mtlohi1.tst
TESTCASES += mult.tst
TESTCASES += multu.tst

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
