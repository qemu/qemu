# -*- Mode: makefile -*-
#
# This Makefile example is fairly independent from the main makefile
# so users can take and adapt it for their build. We only really
# include config-host.mak so we don't have to repeat probing for
# programs that the main configure has already done for us.
#

include config-host.mak

TOP_SRC_PATH = $(SRC_PATH)/../..

VPATH += $(SRC_PATH)

NAMES :=
NAMES += execlog
NAMES += hotblocks
NAMES += hotpages
NAMES += howvec
NAMES += lockstep
NAMES += hwprofile
NAMES += cache
NAMES += drcov

SONAMES := $(addsuffix .so,$(addprefix lib,$(NAMES)))

# The main QEMU uses Glib extensively so it's perfectly fine to use it
# in plugins (which many example do).
PLUGIN_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0)
PLUGIN_CFLAGS += -fPIC -Wall
PLUGIN_CFLAGS += -I$(TOP_SRC_PATH)/include/qemu

all: $(SONAMES)

%.o: %.c
	$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) -c -o $@ $<

lib%.so: %.o
ifeq ($(CONFIG_DARWIN),y)
	$(CC) -bundle -Wl,-undefined,dynamic_lookup -o $@ $^ $(LDLIBS)
else
	$(CC) -shared -o $@ $^ $(LDLIBS)
endif

clean:
	rm -f *.o *.so *.d
	rm -Rf .libs

.PHONY: all clean
.SECONDARY:
