all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/optionrom)

.PHONY : all clean build-all

# Drop -fstack-protector and the like
QEMU_CFLAGS := $(filter -W%, $(QEMU_CFLAGS)) $(CFLAGS_NOPIE) -ffreestanding
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -m16)
ifeq ($(filter -m16, $(QEMU_CFLAGS)),)
# Attempt to work around compilers that lack -m16 (GCC <= 4.8, clang <= ??)
# On GCC we add -fno-toplevel-reorder to keep the order of asm blocks with
# respect to the rest of the code.  clang does not have -fno-toplevel-reorder,
# but it places all asm blocks at the beginning and we're relying on it for
# the option ROM header.  So just force clang not to use the integrated
# assembler, which doesn't support .code16gcc.
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -fno-toplevel-reorder)
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -no-integrated-as)
QEMU_CFLAGS += -m32 -include $(SRC_PATH)/pc-bios/optionrom/code16gcc.h
endif

# Drop gcov and glib flags
CFLAGS := $(filter -O% -g%, $(CFLAGS))
QEMU_INCLUDES += -I$(SRC_PATH)

Wa = -Wa,
ASFLAGS += -32
QEMU_CFLAGS += $(call cc-c-option, $(QEMU_CFLAGS), $(Wa)-32)

build-all: multiboot.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin

# suppress auto-removal of intermediate files
.SECONDARY:


%.o: %.S
	$(call quiet-command,$(CPP) $(QEMU_INCLUDES) $(QEMU_DGFLAGS) -c -o - $< | $(AS) $(ASFLAGS) -o $@,"  AS    $(TARGET_DIR)$@")

ifdef CONFIG_WIN32
LD_EMULATION = i386pe
else
ifdef CONFIG_BSD
LD_EMULATION = elf_i386_fbsd
else
LD_EMULATION = elf_i386
endif
endif

%.img: %.o
	$(call quiet-command,$(LD) $(LDFLAGS_NOPIE) -m $(LD_EMULATION) -Ttext 0 -e _start -s -o $@ $<,"  Building $(TARGET_DIR)$@")

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"  Building $(TARGET_DIR)$@")

%.bin: %.raw
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/signrom.py $< $@,"  Signing $(TARGET_DIR)$@")

clean:
	rm -f *.o *.d *.raw *.img *.bin *~
