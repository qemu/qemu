#
#  Test program for MIPS64R6 CRC32 instructions
#
#  Copyright (C) 2025  Aleksandar Rakic <aleksandar.rakic@htecgroup.com>
#
#  SPDX-License-Identifier: GPL-2.0-or-later
#

ifndef PREFIX
  $(error "PREFIX not set, please export GNU Toolchain install directory.")
endif

ifndef SYSROOT
  $(error "SYSROOT not set, please export GNU Toolchain system root directory.")
endif

SIM = ../../../../../../../build/qemu-mips64
SIM_FLAGS = -L $(SYSROOT)

CC      =  $(PREFIX)/bin/mips64-r6-linux-gnu-gcc

TESTCASES  = test_mips64r6_crc32b.tst
TESTCASES += test_mips64r6_crc32h.tst
TESTCASES += test_mips64r6_crc32w.tst
TESTCASES += test_mips64r6_crc32d.tst
TESTCASES += test_mips64r6_crc32cb.tst
TESTCASES += test_mips64r6_crc32ch.tst
TESTCASES += test_mips64r6_crc32cw.tst
TESTCASES += test_mips64r6_crc32cd.tst

all: $(TESTCASES)
	@for case in $(TESTCASES); do \
            echo $(SIM) $(SIM_FLAGS) ./$$case; \
            $(SIM) $(SIM_FLAGS) ./$$case; \
            echo $(RM) -rf ./$$case; \
            $(RM) -rf ./$$case; \
	done

%.tst: %.c
	$(CC) $< -o $@
