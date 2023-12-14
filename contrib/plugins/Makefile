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

# The lockstep example communicates using unix sockets,
# and can't be easily made to work on windows.
ifneq ($(CONFIG_WIN32),y)
NAMES += lockstep
endif

NAMES += hwprofile
NAMES += cache
NAMES += drcov

ifeq ($(CONFIG_WIN32),y)
SO_SUFFIX := .dll
LDLIBS += $(shell $(PKG_CONFIG) --libs glib-2.0)
else
SO_SUFFIX := .so
endif

SONAMES := $(addsuffix $(SO_SUFFIX),$(addprefix lib,$(NAMES)))

# The main QEMU uses Glib extensively so it's perfectly fine to use it
# in plugins (which many example do).
PLUGIN_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0)
PLUGIN_CFLAGS += -fPIC -Wall
PLUGIN_CFLAGS += -I$(TOP_SRC_PATH)/include/qemu

all: $(SONAMES)

%.o: %.c
	$(CC) $(CFLAGS) $(PLUGIN_CFLAGS) -c -o $@ $<

ifeq ($(CONFIG_WIN32),y)
lib%$(SO_SUFFIX): %.o win32_linker.o ../../plugins/libqemu_plugin_api.a
	$(CC) -shared -o $@ $^ $(LDLIBS)
else ifeq ($(CONFIG_DARWIN),y)
lib%$(SO_SUFFIX): %.o
	$(CC) -bundle -Wl,-undefined,dynamic_lookup -o $@ $^ $(LDLIBS)
else
lib%$(SO_SUFFIX): %.o
	$(CC) -shared -o $@ $^ $(LDLIBS)
endif


clean:
	rm -f *.o *$(SO_SUFFIX) *.d
	rm -Rf .libs

.PHONY: all clean
.SECONDARY:
