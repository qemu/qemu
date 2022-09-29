include config.mak
SRC_DIR := $(TOPSRC_DIR)/pc-bios/optionrom
VPATH = $(SRC_DIR)

all: multiboot.bin multiboot_dma.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin pvh.bin
# Dummy command so that make thinks it has done something
	@true

CFLAGS = -O2 -g

NULL :=
SPACE := $(NULL) #
TARGET_PREFIX := $(patsubst %/,%:$(SPACE),$(TARGET_DIR))

quiet-@ = $(if $(V),,@$(if $1,printf "%s\n" "$(TARGET_PREFIX)$1" && ))
quiet-command = $(call quiet-@,$2 $@)$1

# Flags for dependency generation
override CPPFLAGS += -MMD -MP -MT $@ -MF $(@D)/$(*F).d

override CFLAGS += -march=i486 -Wall $(EXTRA_CFLAGS) -m16
override CFLAGS += -ffreestanding -I$(TOPSRC_DIR)/include

cc-test = $(CC) -Werror $1 -c -o /dev/null -xc /dev/null >/dev/null 2>/dev/null
cc-option = if $(call cc-test, $1); then \
    echo "$(TARGET_PREFIX)$1 detected" && echo "override CFLAGS += $1" >&3; else \
    echo "$(TARGET_PREFIX)$1 not detected" $(if $2,&& echo "override CFLAGS += $2" >&3); fi

# If -fcf-protection is enabled in flags or compiler defaults that will
# conflict with -march=i486
config-cc.mak: Makefile
	$(quiet-@)($(call cc-option,-fcf-protection=none); \
	    $(call cc-option,-fno-pie); \
	    $(call cc-option,-no-pie); \
	    $(call cc-option,-fno-stack-protector); \
	    $(call cc-option,-Wno-array-bounds)) 3> config-cc.mak
-include config-cc.mak

override LDFLAGS = -nostdlib -Wl,-T,$(SRC_DIR)/flat.lds

pvh.img: pvh.o pvh_main.o

%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<,Assembling)

%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@,Compiling)

%.img: %.o
	$(call quiet-command,$(CC) $(CFLAGS) $(LDFLAGS) -s -o $@ $^,Linking)

%.raw: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,Extracting raw object)

%.bin: %.raw
	$(call quiet-command,$(PYTHON) $(TOPSRC_DIR)/scripts/signrom.py $< $@,Computing checksum into)

include $(wildcard *.d)

clean:
	rm -f *.o *.d *.raw *.img *.bin *~

distclean:
	rm -f config-cc.mak

# suppress auto-removal of intermediate files
.SECONDARY:

.PHONY: all clean distclean
