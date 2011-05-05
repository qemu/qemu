all: build-all
# Dummy command so that make thinks it has done something
	@true

include ../../config-host.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/pc-bios/spapr-rtas)

.PHONY : all clean build-all

#CFLAGS += -I$(SRC_PATH)
#QEMU_CFLAGS = $(CFLAGS)

build-all: spapr-rtas.bin

%.img: %.o
	$(call quiet-command,$(CC) -nostdlib -o $@ $<,"  Building $(TARGET_DIR)$@")

%.bin: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"  Building $(TARGET_DIR)$@")

clean:
	rm -f *.o *.d *.img *.bin *~
