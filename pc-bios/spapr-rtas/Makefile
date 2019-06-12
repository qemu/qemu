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

%.o: %.S
	$(call quiet-command,$(CCAS) -mbig -c -o $@ $<,"CCAS","$(TARGET_DIR)$@")

%.img: %.o
	$(call quiet-command,$(CC) -nostdlib -mbig -o $@ $<,"Building","$(TARGET_DIR)$@")

%.bin: %.img
	$(call quiet-command,$(OBJCOPY) -O binary -j .text $< $@,"Building","$(TARGET_DIR)$@")

clean:
	rm -f *.o *.d *.img *.bin *~
