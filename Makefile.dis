# Makefile for disassemblers.

include ../config-host.mak
include config.mak
include $(SRC_PATH)/rules.mak

.PHONY: all

$(call set-vpath, $(SRC_PATH))

QEMU_CFLAGS+=-I..

include $(SRC_PATH)/Makefile.objs

all: $(libdis-y)
# Dummy command so that make thinks it has done something
	@true

clean:
	rm -f *.o *.d *.a *~

# Include automatically generated dependency files
-include $(wildcard *.d */*.d)
