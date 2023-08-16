# -*- Mode: makefile -*-
#
# This Makefile example is fairly independent from the main makefile
# so users can take and adapt it for their build. We only really
# include config-host.mak so we don't have to repeat probing for
# cflags that the main configure has already done for us.
#

BUILD_DIR := $(CURDIR)/../..

include $(BUILD_DIR)/config-host.mak

VPATH += $(SRC_PATH)/contrib/simpoint

NAMES :=
NAMES += bbv
NAMES += trace

SONAMES := $(addsuffix .so,$(addprefix lib,$(NAMES)))

# The main QEMU uses Glib extensively so it's perfectly fine to use it
# in plugins (which many example do).
CFLAGS = `pkg-config --cflags glib-2.0`
CFLAGS += -fPIC -Wall -fvisibility=hidden -static
# CFLAGS += $(if $(CONFIG_DEBUG_TCG), -ggdb -O0, -O3)
CFLAGS += -O3
CFLAGS += -I$(SRC_PATH)/contrib/simpoint/include

all: $(SONAMES)

%.o: %.cc
	$(CXX) $(CFLAGS) -c -o $@ $<

lib%.so: %.o
	$(CXX) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o *.so *.d
	rm -Rf .libs

.PHONY: all clean
