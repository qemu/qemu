CURRENT_MAKEFILE := $(realpath $(word $(words $(MAKEFILE_LIST)),$(MAKEFILE_LIST)))
SRC_DIR := $(dir $(CURRENT_MAKEFILE))
TOPSRC_DIR := $(SRC_DIR)/../..
VPATH = $(SRC_DIR)

all: multiboot.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin pvh.bin
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
CFLAGS = -O2 -g

quiet-command = $(if $(V),$1,$(if $(2),@printf "  %-7s %s\n" $2 $3 && $1, @$1))
cc-option = $(if $(shell $(CC) $1 -c -o /dev/null -xc /dev/null >/dev/null 2>&1 && echo OK), $1, $2)

override CFLAGS += -march=i486 -Wall

# Flags for dependency generation
override CPPFLAGS += -MMD -MP -MT $@ -MF $(@D)/$(*F).d

override CFLAGS += $(filter -W%, $(QEMU_CFLAGS))
override CFLAGS += $(CFLAGS_NOPIE) -ffreestanding -I$(TOPSRC_DIR)/include
override CFLAGS += $(call cc-option, -fno-stack-protector)
override CFLAGS += $(call cc-option, -m16)

ifeq ($(filter -m16, $(CFLAGS)),)
# Attempt to work around compilers that lack -m16 (GCC <= 4.8, clang <= ??)
# On GCC we add -fno-toplevel-reorder to keep the order of asm blocks with
# respect to the rest of the code.  clang does not have -fno-toplevel-reorder,
# but it places all asm blocks at the beginning and we're relying on it for
# the option ROM header.  So just force clang not to use the integrated
# assembler, which doesn't support .code16gcc.
override CFLAGS += $(call cc-option, -fno-toplevel-reorder)
override CFLAGS += $(call cc-option, -no-integrated-as)
override CFLAGS += -m32 -include $(SRC_DIR)/code16gcc.h
endif

Wa = -Wa,
override ASFLAGS += -32
override CFLAGS += $(call cc-option, $(Wa)-32)

LD_I386_EMULATION ?= elf_i386
override LDFLAGS = -m $(LD_I386_EMULATION) -T $(SRC_DIR)/flat.lds

all: multiboot.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin pvh.bin

pvh.img: pvh.o pvh_main.o

%.o: %.S
	$(call quiet-command,$(CPP) $(CPPFLAGS) -c -o - $< | $(AS) $(ASFLAGS) -o $@,"AS","$@")

%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@,"CC","$@")

%.img: %.o
	$(call quiet-command,$(LD) $(LDFLAGS) -s -o $@ $^,"BUILD","$@")

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"BUILD","$@")

%.bin: %.raw
	$(call quiet-command,$(PYTHON) $(TOPSRC_DIR)/scripts/signrom.py $< $@,"SIGN","$@")

include $(wildcard *.d)

clean:
	rm -f *.o *.d *.raw *.img *.bin *~

# suppress auto-removal of intermediate files
.SECONDARY:

.PHONY: all clean
