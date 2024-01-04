#
# arm makefile
#
# Authors: Andrew Jones <drjones@redhat.com>
#
bits = 32
ldarch = elf32-littlearm
kernel_offset = 0x10000
machine = -marm

CFLAGS += $(machine)
CFLAGS += -mcpu=$(PROCESSOR)

cstart.o = $(TEST_DIR)/cstart.o
cflatobjs += lib/arm/spinlock.o
cflatobjs += lib/arm/processor.o

# arm specific tests
tests =

include config/config-arm-common.mak

arch_clean: arm_clean
