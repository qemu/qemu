# Makefile for qemu target independent user files.

include ../config-host.mak
include $(SRC_PATH)/rules.mak
-include config.mak

.PHONY: all

$(call set-vpath, $(SRC_PATH))

QEMU_CFLAGS+=-I..

include $(SRC_PATH)/Makefile.objs

all: $(user-obj-y)
# Dummy command so that make thinks it has done something
	@true

clean:
	rm -f *.o *.d *.a *~

# Include automatically generated dependency files
-include $(wildcard *.d */*.d)
