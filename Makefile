include config.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=-g
LIBS=
DEFINES=-DHAVE_BYTESWAP_H

ifeq ($(ARCH),i386)
CFLAGS+=-fomit-frame-pointer
OP_CFLAGS=$(CFLAGS) -mpreferred-stack-boundary=2
ifeq ($(GCC_MAJOR),3)
OP_CFLAGS+= -falign-functions=0
else
OP_CFLAGS+= -malign-functions=0
endif
# WARNING: this LDFLAGS is _very_ tricky : qemu is an ELF shared object
# that the kernel ELF loader considers as an executable. I think this
# is the simplest way to make it self virtualizable!
LDFLAGS+=-Wl,-shared
endif

ifeq ($(ARCH),ppc)
OP_CFLAGS=$(CFLAGS)
LDFLAGS+=-Wl,-T,ppc.ld
endif

ifeq ($(ARCH),s390)
OP_CFLAGS=$(CFLAGS)
LDFLAGS+=-Wl,-T,s390.ld
endif

ifeq ($(GCC_MAJOR),3)
# very important to generate a return at the end of every operation
OP_CFLAGS+=-fno-reorder-blocks -fno-optimize-sibling-calls
endif

#########################################################

DEFINES+=-D_GNU_SOURCE
LIBS+=-lm

# profiling code
ifdef TARGET_GPROF
LDFLAGS+=-p
main.o: CFLAGS+=-p
endif

OBJS= elfload.o main.o syscall.o signal.o
SRCS:= $(OBJS:.o=.c)
OBJS+= libqemu.a

LIBOBJS+=thunk.o translate-i386.o op-i386.o exec-i386.o
# NOTE: the disassembler code is only needed for debugging
LIBOBJS+=i386-dis.o dis-buf.o

all: qemu qemu-doc.html

qemu: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^  $(LIBS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

# libqemu 

libqemu.a: $(LIBOBJS)
	rm -f $@
	$(AR) rcs $@ $(LIBOBJS)

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
	rm -f *.o  *.a *~ qemu dyngen TAGS

distclean: clean
	rm -f config.mak config.h

install: qemu
	install -m 755 -s qemu $(prefix)/bin

# various test targets
test speed: qemu
	make -C tests $@

TAGS: 
	etags *.[ch] i386/*.[ch]

# documentation
qemu-doc.html: qemu-doc.texi
	texi2html -monolithic -number $<

FILES= \
README README.distrib COPYING COPYING.LIB TODO Changelog VERSION \
dyngen.c ioctls.h ops_template.h op_string.h  syscall_types.h\
Makefile     elf.h       thunk.c\
elfload.c   main.c            signal.c        thunk.h\
cpu-i386.h qemu.h op-i386.c opc-i386.h syscall-i386.h  translate-i386.c\
dis-asm.h    gen-i386.h  syscall.c\
dis-buf.c    i386-dis.c  opreg_template.h  syscall_defs.h\
ppc.ld s390.ld exec-i386.h exec-i386.c configure \
tests/Makefile\
tests/test-i386.c tests/test-i386-shift.h tests/test-i386.h\
tests/test-i386-muldiv.h tests/test-i386-code16.S\
tests/hello.c tests/hello tests/sha1.c \
tests/testsig.c tests/testclone.c tests/testthread.c \
tests/runcom.c tests/pi_10.com \
qemu-doc.texi qemu-doc.html

FILE=qemu-$(VERSION)

tar:
	rm -rf /tmp/$(FILE)
	mkdir -p /tmp/$(FILE)
	cp -P $(FILES) /tmp/$(FILE)
	( cd /tmp ; tar zcvf ~/$(FILE).tar.gz $(FILE) )
	rm -rf /tmp/$(FILE)

# generate a binary distribution including the test binary environnment 
BINPATH=/usr/local/qemu-i386

tarbin:
	tar zcvf /tmp/qemu-i386-glibc21.tar.gz \
                 $(BINPATH)/etc $(BINPATH)/lib $(BINPATH)/bin
	tar zcvf /tmp/qemu-i386-wine.tar.gz \
                 $(BINPATH)/X11R6 $(BINPATH)/wine

ifneq ($(wildcard .depend),)
include .depend
endif
