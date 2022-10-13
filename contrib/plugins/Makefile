# -*- Mode: makefile -*-
#
# This Makefile example is fairly independent from the main makefile
# so users can take and adapt it for their build. We only really
# include config-host.mak so we don't have to repeat probing for
# cflags that the main configure has already done for us.
#

BUILD_DIR := $(CURDIR)/../..

include $(BUILD_DIR)/config-host.mak

VPATH += $(SRC_PATH)/contrib/plugins

NAMES :=
NAMES += execlog
NAMES += hotblocks
NAMES += hotpages
NAMES += howvec
NAMES += lockstep
NAMES += hwprofile
NAMES += cache
NAMES += drcov
NAMES += qpp_srv
NAMES += qpp_client
NAMES += syscalls
NAMES += syscalls_logger
NAMES += sc_file_log
NAMES += stringsearch
NAMES += osi_linux
NAMES += osi

SONAMES := $(addsuffix .so,$(addprefix lib,$(NAMES)))

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

all: $(SONAMES)

libstringsearch.so: stringsearch.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

stringsearch.o: stringsearch.cpp
	g++  $(CPPFLAGS) -DCURRENT_PLUGIN=\"stringsearch\" -c -o stringsearch.o $<

libosi_linux.so: osi_linux.o default_profile.o kernelinfo_read.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

osi_linux.o: osi_linux/osi_linux.cpp osi_linux/osi_linux.h  osi_linux/osi_types.h 
	$(CC) $(CPPFLAGS) -DCURRENT_PLUGIN=\"osi_linux\" -c -o $@ $<

default_profile.o: osi_linux/default_profile.cpp  osi_linux/osi_linux.h  osi_linux/osi_types.h 
	$(CC) $(CPPFLAGS) -DCURRENT_PLUGIN=\"osi_linux\" -c -o $@ $<

kernelinfo_read.o: osi_linux/utils/kernelinfo/kernelinfo_read.c osi_linux/utils/kernelinfo/kernelinfo.h  osi_linux/osi_linux.h  osi_linux/osi_types.h 
	$(CC) $(CPPFLAGS) -DCURRENT_PLUGIN=\"osi_linux\" -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -DCURRENT_PLUGIN=\"$(basename $@)\" -c -o $@ $<

lib%.so: %.o
	$(CC) -shared -Wl,-soname,$@ -o $@ $^ $(LDLIBS)

clean:
	rm -f *.o *.so *.d
	rm -Rf .libs

.PHONY: all clean
