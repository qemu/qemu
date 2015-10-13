all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/s390-ccw)

.PHONY : all clean build-all

OBJECTS = start.o main.o bootmap.o sclp-ascii.o virtio.o
CFLAGS += -fPIE -fno-stack-protector -ffreestanding -march=z900
CFLAGS += -fno-delete-null-pointer-checks -msoft-float
LDFLAGS += -Wl,-pie -nostdlib

build-all: s390-ccw.img

s390-ccw.elf: $(OBJECTS)
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(OBJECTS),"  Building $(TARGET_DIR)$@")

s390-ccw.img: s390-ccw.elf
	$(call quiet-command,strip --strip-unneeded $< -o $@,"  Stripping $(TARGET_DIR)$@")

$(OBJECTS): Makefile

clean:
	rm -f *.o *.d *.img *.elf *~
