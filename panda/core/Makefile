# -*- Mode: makefile -*-
#
# This Makefile example is fairly independent from the main makefile
# so users can take and adapt it for their build. We only really
# include config-host.mak so we don't have to repeat probing for
# cflags that the main configure has already done for us.
#

BUILD_DIR := $(CURDIR)/../..

include $(BUILD_DIR)/config-host.mak

VPATH += $(SRC_PATH)/panda/core

NAMES :=
NAMES += pandacore

SONAMES := $(addsuffix .so,$(addprefix lib,$(NAMES)))

# The main QEMU uses Glib extensively so it's perfectly fine to use it
# in plugins (which many example do).
CFLAGS = $(GLIB_CFLAGS)
CFLAGS += -fPIC -Wall $(filter -W%, $(QEMU_CFLAGS))
CFLAGS += $(if $(findstring no-psabi,$(QEMU_CFLAGS)),-Wpsabi)
CFLAGS += -I$(SRC_PATH)/include/qemu

# The main QEMU uses Glib extensively so it's perfectly fine to use it
# in plugins (which many example do).
CFLAGS = $(GLIB_CFLAGS)
CFLAGS += -fPIC -Wall $(filter -W%, $(QEMU_CFLAGS))
CFLAGS += $(if $(findstring no-psabi,$(QEMU_CFLAGS)),-Wpsabi)
CFLAGS += -I$(SRC_PATH)/include/qemu

CPPFLAGS = $(GLIB_CFLAGS)
CPPFLAGS += -fPIC -Wall
CPPFLAGS += $(if $(findstring no-psabi,$(QEMU_CFLAGS)),-Wpsabi)
CPPFLAGS += -idirafter $(SRC_PATH)/include/qemu
CPPFLAGS += -I$(SRC_PATH)/include
CPPFLAGS += -I$(BUILD_DIR)

all: $(SONAMES)

libpandacore.so: pandacore.o pandacore.cpp Makefile
	g++ -shared -Wl,-soname,$@ -o $@ pandacore.o $(LDLIBS)

pandacore.o: pandacore.cpp
	g++  $(CPPFLAGS) -DCURRENT_PLUGIN=\"pandacore\" -c -o pandacore.o $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

lib%.so: %.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o *.so *.d
	rm -Rf .libs

.PHONY: all clean
