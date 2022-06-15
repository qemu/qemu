include config.mak
SRC_DIR := $(TOPSRC_DIR)/pc-bios/optionrom
VPATH = $(SRC_DIR)

all: multiboot.bin multiboot_dma.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin pvh.bin
# Dummy command so that make thinks it has done something
	@true

CFLAGS = -O2 -g

quiet-command = $(if $(V),$1,$(if $(2),@printf "  %-7s %s\n" $2 $3 && $1, @$1))
cc-option = $(if $(shell $(CC) $1 -c -o /dev/null -xc /dev/null >/dev/null 2>&1 && echo OK), $1, $2)

override CFLAGS += -march=i486 -Wall $(EXTRA_CFLAGS) -m16

# If -fcf-protection is enabled in flags or compiler defaults that will
# conflict with -march=i486
override CFLAGS += $(call cc-option, -fcf-protection=none)

# Flags for dependency generation
override CPPFLAGS += -MMD -MP -MT $@ -MF $(@D)/$(*F).d

override CFLAGS += $(filter -W%, $(QEMU_CFLAGS))
override CFLAGS += $(call cc-option, -fno-pie)
override CFLAGS += -ffreestanding -I$(TOPSRC_DIR)/include
override CFLAGS += $(call cc-option, -fno-stack-protector)
override CFLAGS += $(call cc-option, -Wno-array-bounds)

Wa = -Wa,
override ASFLAGS += -32
override CFLAGS += $(call cc-option, $(Wa)-32)

override LDFLAGS = -m $(LD_I386_EMULATION) -T $(SRC_DIR)/flat.lds

pvh.img: pvh.o pvh_main.o

%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) -E -o - $< | $(AS) $(ASFLAGS) -o $@,"AS","$@")

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
