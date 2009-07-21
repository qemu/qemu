all: build-all

include ../../config-host.mak
include ../../rules.mak

VPATH=$(SRC_PATH)/pc-bios/optionrom

# from kernel sources - scripts/Kbuild.include
# try-run
# Usage: option = $(call try-run, $(CC)...-o "$$TMP",option-ok,otherwise)
# Exit code chooses option. "$$TMP" is can be used as temporary file and
# is automatically cleaned up.
try-run = $(shell set -e;		\
	TMP="$(TMPOUT).$$$$.tmp";	\
	if ($(1)) >/dev/null 2>&1;	\
	then echo "$(2)";		\
	else echo "$(3)";		\
	fi;				\
	rm -f "$$TMP")

# cc-option-yn
# Usage: flag := $(call cc-option-yn,-march=winchip-c6)
cc-option-yn = $(call try-run,\
	$(CC) $(KBUILD_CFLAGS) $(1) -S -xc /dev/null -o "$$TMP",y,n)

CPPFLAGS = -Wall -Wstrict-prototypes -Werror -fomit-frame-pointer -fno-builtin
CPPFLAGS += -I$(SRC_PATH)
ifeq ($(call cc-option-yn,-fno-stack-protector),y)
CPPFLAGS += -fno-stack-protector
endif

build-all: multiboot.bin

%.img: %.o
	$(call quiet-command,$(LD) -Ttext 0 -e _start -s -o $@ $<,"  Building $(TARGET_DIR)$@")

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"  Building $(TARGET_DIR)$@")

%.bin: %.raw
	$(call quiet-command,$(SRC_PATH)/pc-bios/optionrom/signrom.sh $< $@,"  Signing $(TARGET_DIR)$@")

clean:
	$(RM) *.o *.img *.bin *~
