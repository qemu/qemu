# Makefile for qemu target independent user files.

include ../config-host.mak
include $(SRC_PATH)/rules.mak

.PHONY: all

VPATH=$(SRC_PATH)

QEMU_CFLAGS+=-I..

obj-y =
obj-y += envlist.o path.o
obj-y += tcg-runtime.o host-utils.o
obj-y += cutils.o cache-utils.o

all: libuser.a
# Dummy command so that make thinks it has done something
	@true

libuser.a: $(obj-y)

clean:
	rm -f *.o *.d *.a *~

# Include automatically generated dependency files
-include $(wildcard *.d */*.d)
