ARCH=i386
#ARCH=ppc
HOST_CC=gcc

ifeq ($(ARCH),i386)
CFLAGS=-Wall -O2 -g -fomit-frame-pointer
LDFLAGS=-g
LIBS=
CC=gcc
DEFINES=-DHAVE_BYTESWAP_H
OP_CFLAGS=$(CFLAGS) -malign-functions=0 -mpreferred-stack-boundary=2
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
OP_CFLAGS=$(CFLAGS)
endif

#########################################################

DEFINES+=-D_GNU_SOURCE
DEFINES+=-DCONFIG_PREFIX=\"/usr/local\"
LDSCRIPT=$(ARCH).ld
LIBS+=-ldl -lm

#DEFINES+= -DGEMU -DDOSEMU -DNO_TRACE_MSGS
#OBJS= i386/fp87.o i386/interp_main.o i386/interp_modrm.o i386/interp_16_32.o \
#      i386/interp_32_16.o i386/interp_32_32.o i386/emu-utils.o \
#      i386/dis8086.o i386/emu-ldt.o
OBJS+=translate-i386.o op-i386.o
OBJS+= elfload.o main.o thunk.o syscall.o
# NOTE: the disassembler code is only needed for debugging
OBJS+=i386-dis.o dis-buf.o
SRCS = $(OBJS:.o=.c)

all: gemu

gemu: $(OBJS)
	$(CC) -Wl,-T,$(LDSCRIPT) $(LDFLAGS) -o $@ $^ $(LIBS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

# old i386 emulator
i386/interp_32_32.o: i386/interp_32_32.c i386/interp_gen.h

i386/interp_gen.h: i386/gencode
	./i386/gencode > $@

i386/gencode: i386/gencode.c
	$(CC) -O2 -Wall -g $< -o $@

# new i386 emulator
dyngen: dyngen.c
	$(HOST_CC) -O2 -Wall -g $< -o $@

translate-i386.o: translate-i386.c op-i386.h cpu-i386.h

op-i386.h: op-i386.o dyngen
	./dyngen -o $@ $<

op-i386.o: op-i386.c opreg_template.h ops_template.h
	$(CC) $(OP_CFLAGS) $(DEFINES) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

clean:
	rm -f *.o *~ i386/*.o i386/*~ gemu TAGS

# various test targets
test speed: gemu
	make -C tests $@

TAGS: 
	etags *.[ch] i386/*.[ch]

ifneq ($(wildcard .depend),)
include .depend
endif
