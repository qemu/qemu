all: build-all

include ../../config-host.mak

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

CFLAGS = -Wall -Wstrict-prototypes -Werror -fomit-frame-pointer -fno-builtin
CFLAGS += -I$(SRC_PATH)
ifeq ($(call cc-option-yn,-fno-stack-protector),y)
CFLAGS += -fno-stack-protector
endif

build-all: multiboot.bin

%.o: %.S
	$(CC) $(CFLAGS) -o $@ -c $<

%.img: %.o
	$(LD) -Ttext 0 -e _start -s -o $@ $<

%.raw: %.img
	$(OBJCOPY) -O binary -j .text $< $@

%.bin: %.raw
	$(SRC_PATH)/pc-bios/optionrom/signrom.sh $< $@

clean:
	$(RM) *.o *.img *.bin *~
