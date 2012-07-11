# Makefile for qemu target independent user files.

include ../config-host.mak
include $(SRC_PATH)/rules.mak
-include config.mak

.PHONY: all

$(call set-vpath, $(SRC_PATH))

QEMU_CFLAGS+=-I..
QEMU_CFLAGS += -I$(SRC_PATH)/include
QEMU_CFLAGS += -DCONFIG_USER_ONLY

include $(SRC_PATH)/Makefile.objs

all: $(user-obj-y)
# Dummy command so that make thinks it has done something
	@true

clean:
	for d in . trace; do \
	rm -f $$d/*.o $$d/*.d $$d/*.a $$d/*~; \
	done
