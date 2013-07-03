all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/s390-ccw)

.PHONY : all clean build-all

OBJECTS=main.o bootmap.o sclp-ascii.o virtio.o start.o
CFLAGS += -fno-stack-protector
# XXX find a more clever to locate the bootloader
LDFLAGS += -Wl,-Ttext,0x7e00000,-Tbss,0x7f00000 -nostdlib

build-all: s390-ccw.img

s390-ccw.elf: $(OBJECTS)
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(OBJECTS),"  Building $(TARGET_DIR)$@")

s390-ccw.img: s390-ccw.elf
	$(call quiet-command,strip $< -o $@,"  Stripping $(TARGET_DIR)$@")

clean:
	rm -f *.o *.d *.img *.elf *~
