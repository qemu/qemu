ARCH=i386
#ARCH=ppc

ifeq ($(ARCH),i386)
CFLAGS=-Wall -O2 -g
LDFLAGS=-g
LIBS=
CC=gcc
DEFINES=-DHAVE_BYTESWAP_H
endif

ifeq ($(ARCH),ppc)
GCC_LIBS_DIR=/usr/netgem/tools/lib/gcc-lib/powerpc-linux/2.95.2
DIST=/home/fbe/nsv/dist/hw/n6-dtt
CC=powerpc-linux-gcc -msoft-float 
CFLAGS=-Wall -pipe -O2 -mcpu=405 -mbig -nostdinc -g -I$(GCC_LIBS_DIR)/include -I$(DIST)/include
LIBS_DIR=$(DIST)/lib
CRT1=$(LIBS_DIR)/crt1.o
CRTI=$(LIBS_DIR)/crti.o
CRTN=$(LIBS_DIR)/crtn.o
CRTBEGIN=$(GCC_LIBS_DIR)/crtbegin.o
CRTEND=$(GCC_LIBS_DIR)/crtend.o
LDFLAGS=-static -g -nostdlib $(CRT1) $(CRTI) $(CRTBEGIN) 
LIBS=-L$(LIBS_DIR) -ltinyc -lgcc $(CRTEND) $(CRTN)
DEFINES=-Dsocklen_t=int
endif

#########################################################

DEFINES+=-D_GNU_SOURCE -DGEMU -DDOSEMU #-DNO_TRACE_MSGS
LDSCRIPT=$(ARCH).ld

OBJS= i386/fp87.o i386/interp_main.o i386/interp_modrm.o i386/interp_16_32.o \
      i386/interp_32_16.o i386/interp_32_32.o i386/emu-utils.o \
      i386/dis8086.o i386/emu-ldt.o
OBJS+= elfload.o main.o thunk.o syscall.o

SRCS = $(OBJS:.o=.c)

all: gemu

gemu: $(OBJS)
	$(CC) -Wl,-T,$(LDSCRIPT) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

clean:
	rm -f *.o *~ i386/*.o i386/*~ gemu hello test1 test2 TAGS

hello: hello.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<

test1: test1.c
	$(CC) $(CFLAGS) -static $(LDFLAGS) -o $@ $<

test2: test2.c
	$(CC) $(CFLAGS) -static $(LDFLAGS) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif
