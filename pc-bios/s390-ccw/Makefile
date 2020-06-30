all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/s390-ccw)

.PHONY : all clean build-all

OBJECTS = start.o main.o bootmap.o jump2ipl.o sclp.o menu.o \
	  virtio.o virtio-scsi.o virtio-blkdev.o libc.o cio.o dasd-ipl.o

QEMU_CFLAGS := $(filter -W%, $(QEMU_CFLAGS))
QEMU_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -msoft-float
QEMU_CFLAGS += -march=z900 -fPIE -fno-strict-aliasing
QEMU_CFLAGS += -fno-asynchronous-unwind-tables
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -fno-stack-protector)
LDFLAGS += -Wl,-pie -nostdlib

build-all: s390-ccw.img s390-netboot.img

s390-ccw.elf: $(OBJECTS)
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(OBJECTS),"BUILD","$(TARGET_DIR)$@")

STRIP ?= strip

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
