all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/s390-ccw)

.PHONY : all clean build-all

OBJECTS = start.o main.o bootmap.o sclp-ascii.o virtio.o virtio-scsi.o
QEMU_CFLAGS := $(filter -W%, $(QEMU_CFLAGS))
QEMU_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -msoft-float
QEMU_CFLAGS += -march=z900 -fPIE -fno-strict-aliasing
QEMU_CFLAGS += $(call cc-option, $(QEMU_CFLAGS), -fno-stack-protector)
LDFLAGS += -Wl,-pie -nostdlib

build-all: s390-ccw.img

s390-ccw.elf: $(OBJECTS)
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(OBJECTS),"BUILD","$(TARGET_DIR)$@")

s390-ccw.img: s390-ccw.elf
	$(call quiet-command,strip --strip-unneeded $< -o $@,"STRIP","$(TARGET_DIR)$@")

$(OBJECTS): Makefile

clean:
	rm -f *.o *.d *.img *.elf *~
