include config.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=-g
LIBS=
DEFINES=-DHAVE_BYTESWAP_H

ifeq ($(ARCH),i386)
CFLAGS+=-fomit-frame-pointer
OP_CFLAGS=$(CFLAGS) -malign-functions=0 -mpreferred-stack-boundary=2
endif

ifeq ($(ARCH),ppc)
OP_CFLAGS=$(CFLAGS)
endif

#########################################################

DEFINES+=-D_GNU_SOURCE
LDSCRIPT=$(ARCH).ld
LIBS+=-ldl -lm

# profiling code
ifdef TARGET_GPROF
LDFLAGS+=-p
CFLAGS+=-p
endif

OBJS= elfload.o main.o thunk.o syscall.o
OBJS+=translate-i386.o op-i386.o exec-i386.o
# NOTE: the disassembler code is only needed for debugging
OBJS+=i386-dis.o dis-buf.o
SRCS = $(OBJS:.o=.c)

all: gemu

gemu: $(OBJS)
	$(CC) -Wl,-T,$(LDSCRIPT) $(LDFLAGS) -o $@ $^ $(LIBS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

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
	$(MAKE) -C tests clean
	rm -f *.o *~ gemu dyngen TAGS

distclean: clean
	rm -f config.mak config.h

# various test targets
test speed: gemu
	make -C tests $@

TAGS: 
	etags *.[ch] i386/*.[ch]

FILES= \
COPYING.LIB  dyngen.c    ioctls.h          ops_template.h  syscall_types.h\
Makefile     elf.h       linux_bin.h       segment.h       thunk.c\
TODO         elfload.c   main.c            signal.c        thunk.h\
cpu-i386.h   gemu.h      op-i386.c         syscall-i386.h  translate-i386.c\
dis-asm.h    gen-i386.h  op-i386.h         syscall.c\
dis-buf.c    i386-dis.c  opreg_template.h  syscall_defs.h\
i386.ld ppc.ld exec-i386.h exec-i386.c configure VERSION \
tests/Makefile\
tests/test-i386.c tests/test-i386-shift.h tests/test-i386.h\
tests/test-i386-muldiv.h\
tests/test2.c tests/hello.c tests/sha1.c

FILE=gemu-$(VERSION)

tar:
	rm -rf /tmp/$(FILE)
	mkdir -p /tmp/$(FILE)
	cp -P $(FILES) /tmp/$(FILE)
	( cd /tmp ; tar zcvf ~/$(FILE).tar.gz $(FILE) )
	rm -rf /tmp/$(FILE)

ifneq ($(wildcard .depend),)
include .depend
endif
