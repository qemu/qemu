all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
CFLAGS = -O2 -g

quiet-command = $(if $(V),$1,$(if $(2),@printf "  %-7s %s\n" $2 $3 && $1, @$1))
cc-option = $(if $(shell $(CC) $1 $2 -S -o /dev/null -xc /dev/null \
			 >/dev/null 2>&1 && echo OK),$2,$3)

VPATH_SUFFIXES = %.c %.h %.S %.m %.mak %.sh %.rc Kconfig% %.json.in
set-vpath = $(if $1,$(foreach PATTERN,$(VPATH_SUFFIXES),$(eval vpath $(PATTERN) $1)))
$(call set-vpath, $(SRC_PATH)/pc-bios/s390-ccw)

# Flags for dependency generation
QEMU_DGFLAGS = -MMD -MP -MT $@ -MF $(@D)/$(*F).d

%.o: %.c
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) \
	       -c -o $@ $<,"CC","$(TARGET_DIR)$@")

%.o: %.S
	$(call quiet-command,$(CCAS) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) \
	       -c -o $@ $<,"CCAS","$(TARGET_DIR)$@")

.PHONY : all clean build-all

OBJECTS = start.o main.o bootmap.o jump2ipl.o sclp.o menu.o \
	  virtio.o virtio-scsi.o virtio-blkdev.o libc.o cio.o dasd-ipl.o

QEMU_CFLAGS := -Wall $(filter -W%, $(QEMU_CFLAGS))
QEMU_CFLAGS += $(call cc-option,-Werror $(QEMU_CFLAGS),-Wno-stringop-overflow)
QEMU_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -fno-common -fPIE
QEMU_CFLAGS += -fwrapv -fno-strict-aliasing -fno-asynchronous-unwind-tables
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -fno-stack-protector)
QEMU_CFLAGS += -msoft-float
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS),-march=z900,-march=z10)
QEMU_CFLAGS += -std=gnu99
LDFLAGS += -Wl,-pie -nostdlib

build-all: s390-ccw.img s390-netboot.img

s390-ccw.elf: $(OBJECTS)
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(OBJECTS),"BUILD","$(TARGET_DIR)$@")

s390-ccw.img: s390-ccw.elf
	$(call quiet-command,$(STRIP) --strip-unneeded $< -o $@,"STRIP","$(TARGET_DIR)$@")

$(OBJECTS): Makefile

ifneq ($(wildcard $(SRC_PATH)/roms/SLOF/lib/libnet),)
include $(SRC_PATH)/pc-bios/s390-ccw/netboot.mak
else
s390-netboot.img:
	@echo "s390-netboot.img not built since roms/SLOF/ is not available."
endif

ALL_OBJS = $(sort $(OBJECTS) $(NETOBJS) $(LIBCOBJS) $(LIBNETOBJS))
-include $(ALL_OBJS:%.o=%.d)

clean:
	rm -f *.o *.d *.img *.elf *~ *.a
