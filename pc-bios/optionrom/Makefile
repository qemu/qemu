all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

VPATH=$(SRC_PATH)/pc-bios/optionrom

CFLAGS = -Wall -Wstrict-prototypes -Werror -fomit-frame-pointer -fno-builtin
CFLAGS += -I$(SRC_PATH)
CFLAGS += $(call cc-option, -fno-stack-protector,"")
QEMU_CFLAGS = $(CFLAGS)

build-all: multiboot.bin

%.img: %.o
	$(call quiet-command,$(LD) -Ttext 0 -e _start -s -o $@ $<,"  Building $(TARGET_DIR)$@")

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"  Building $(TARGET_DIR)$@")

%.bin: %.raw
	$(call quiet-command,$(SHELL) $(SRC_PATH)/pc-bios/optionrom/signrom.sh $< $@,"  Signing $(TARGET_DIR)$@")

clean:
	$(RM) *.o *.d *.raw *.img *.bin *~
