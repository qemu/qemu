include config.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=-g
LIBS=
DEFINES=-DHAVE_BYTESWAP_H
HELPER_CFLAGS=$(CFLAGS)
PROGS=qemu

ifdef CONFIG_STATIC
LDFLAGS+=-static
endif

ifeq ($(ARCH),i386)
CFLAGS+=-fomit-frame-pointer
OP_CFLAGS=$(CFLAGS) -mpreferred-stack-boundary=2
ifeq ($(HAVE_GCC3_OPTIONS),yes)
OP_CFLAGS+= -falign-functions=0
else
OP_CFLAGS+= -malign-functions=0
endif
ifdef TARGET_GPROF
LDFLAGS+=-Wl,-T,i386.ld
else
# WARNING: this LDFLAGS is _very_ tricky : qemu is an ELF shared object
# that the kernel ELF loader considers as an executable. I think this
# is the simplest way to make it self virtualizable!
LDFLAGS+=-Wl,-shared
endif
ifeq ($(TARGET_ARCH), i386)
PROGS+=vl vlmkcow
endif
endif

ifeq ($(ARCH),ppc)
OP_CFLAGS=$(CFLAGS)
LDFLAGS+=-Wl,-T,ppc.ld
endif

ifeq ($(ARCH),s390)
OP_CFLAGS=$(CFLAGS)
LDFLAGS+=-Wl,-T,s390.ld
endif

ifeq ($(ARCH),sparc)
CFLAGS+=-m32 -ffixed-g1 -ffixed-g2 -ffixed-g3 -ffixed-g6
LDFLAGS+=-m32
OP_CFLAGS=$(CFLAGS) -fno-delayed-branch -ffixed-i0
HELPER_CFLAGS=$(CFLAGS) -ffixed-i0 -mflat
# -static is used to avoid g1/g3 usage by the dynamic linker
LDFLAGS+=-Wl,-T,sparc.ld -static
endif

ifeq ($(ARCH),sparc64)
CFLAGS+=-m64 -ffixed-g1 -ffixed-g2 -ffixed-g3 -ffixed-g6
LDFLAGS+=-m64
OP_CFLAGS=$(CFLAGS) -fno-delayed-branch -ffixed-i0
endif

ifeq ($(ARCH),alpha)
# -msmall-data is not used because we want two-instruction relocations
# for the constant constructions
OP_CFLAGS=-Wall -O2 -g
# Ensure there's only a single GP
CFLAGS += -msmall-data
LDFLAGS+=-Wl,-T,alpha.ld
endif

ifeq ($(ARCH),ia64)
OP_CFLAGS=$(CFLAGS)
endif

ifeq ($(ARCH),arm)
OP_CFLAGS=$(CFLAGS) -mno-sched-prolog
LDFLAGS+=-Wl,-T,arm.ld
endif

ifeq ($(HAVE_GCC3_OPTIONS),yes)
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

OBJS= elfload.o main.o syscall.o mmap.o signal.o path.o
ifeq ($(TARGET_ARCH), i386)
OBJS+= vm86.o
endif
SRCS:= $(OBJS:.o=.c)
OBJS+= libqemu.a

# cpu emulator library
LIBOBJS=thunk.o exec.o translate.o cpu-exec.o gdbstub.o

ifeq ($(TARGET_ARCH), i386)
LIBOBJS+=translate-i386.o op-i386.o helper-i386.o
endif
ifeq ($(TARGET_ARCH), arm)
LIBOBJS+=translate-arm.o op-arm.o
endif

# NOTE: the disassembler code is only needed for debugging
LIBOBJS+=disas.o 
ifeq ($(findstring i386, $(TARGET_ARCH) $(ARCH)),i386)
LIBOBJS+=i386-dis.o
endif
ifeq ($(findstring alpha, $(TARGET_ARCH) $(ARCH)),alpha)
LIBOBJS+=alpha-dis.o
endif
ifeq ($(findstring ppc, $(TARGET_ARCH) $(ARCH)),ppc)
LIBOBJS+=ppc-dis.o
endif
ifeq ($(findstring sparc, $(TARGET_ARCH) $(ARCH)),sparc)
LIBOBJS+=sparc-dis.o
endif
ifeq ($(findstring arm, $(TARGET_ARCH) $(ARCH)),arm)
LIBOBJS+=arm-dis.o
endif

ifeq ($(ARCH),ia64)
OBJS += ia64-syscall.o
endif

all: $(PROGS) qemu-doc.html

qemu: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^  $(LIBS)
ifeq ($(ARCH),alpha)
# Mark as 32 bit binary, i. e. it will be mapped into the low 31 bit of
# the address space (31 bit so sign extending doesn't matter)
	echo -ne '\001\000\000\000' | dd of=qemu bs=1 seek=48 count=4 conv=notrunc
endif

# must use static linking to avoid leaving stuff in virtual address space
vl: vl.o block.o libqemu.a
	$(CC) -static -Wl,-T,i386-vl.ld -o $@ $^  $(LIBS)

vlmkcow: vlmkcow.o
	$(CC) -o $@ $^  $(LIBS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

# libqemu 

libqemu.a: $(LIBOBJS)
	rm -f $@
	$(AR) rcs $@ $(LIBOBJS)

dyngen: dyngen.c
	$(HOST_CC) -O2 -Wall -g $< -o $@

translate-$(TARGET_ARCH).o: translate-$(TARGET_ARCH).c gen-op-$(TARGET_ARCH).h opc-$(TARGET_ARCH).h cpu-$(TARGET_ARCH).h

translate.o: translate.c op-$(TARGET_ARCH).h opc-$(TARGET_ARCH).h cpu-$(TARGET_ARCH).h

op-$(TARGET_ARCH).h: op-$(TARGET_ARCH).o dyngen
	./dyngen -o $@ $<

opc-$(TARGET_ARCH).h: op-$(TARGET_ARCH).o dyngen
	./dyngen -c -o $@ $<

gen-op-$(TARGET_ARCH).h: op-$(TARGET_ARCH).o dyngen
	./dyngen -g -o $@ $<

op-$(TARGET_ARCH).o: op-$(TARGET_ARCH).c
	$(CC) $(OP_CFLAGS) $(DEFINES) -c -o $@ $<

helper-$(TARGET_ARCH).o: helper-$(TARGET_ARCH).c
	$(CC) $(HELPER_CFLAGS) $(DEFINES) -c -o $@ $<

op-i386.o: op-i386.c opreg_template.h ops_template.h ops_template_mem.h

op-arm.o: op-arm.c op-arm-template.h

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

clean:
	$(MAKE) -C tests clean
	rm -f *.o  *.a *~ qemu dyngen TAGS

distclean: clean
	rm -f config.mak config.h

install: $(PROGS)
	mkdir -p $(prefix)/bin
	install -m 755 -s $(PROGS) $(prefix)/bin

# various test targets
test speed: qemu
	make -C tests $@

TAGS: 
	etags *.[ch] tests/*.[ch]

# documentation
qemu-doc.html: qemu-doc.texi
	texi2html -monolithic -number $<

FILES= \
README README.distrib COPYING COPYING.LIB TODO Changelog VERSION \
configure \
dyngen.c dyngen.h dyngen-exec.h ioctls.h syscall_types.h \
Makefile elf.h elfload.c main.c signal.c qemu.h \
syscall.c syscall_defs.h vm86.c path.c mmap.c \
i386.ld ppc.ld alpha.ld s390.ld sparc.ld arm.ld\
vl.c i386-vl.ld vl.h block.c vlmkcow.c\
thunk.c cpu-exec.c translate.c cpu-all.h thunk.h exec.h\
exec.c cpu-exec.c gdbstub.c\
cpu-i386.h op-i386.c helper-i386.c syscall-i386.h translate-i386.c \
exec-i386.h ops_template.h ops_template_mem.h op_string.h opreg_template.h \
cpu-arm.h syscall-arm.h exec-arm.h op-arm.c translate-arm.c op-arm-template.h \
dis-asm.h disas.c disas.h alpha-dis.c ppc-dis.c i386-dis.c sparc-dis.c \
arm-dis.c \
tests/Makefile \
tests/test-i386.c tests/test-i386-shift.h tests/test-i386.h \
tests/test-i386-muldiv.h tests/test-i386-code16.S tests/test-i386-vm86.S \
tests/hello-i386.c tests/hello-i386 \
tests/hello-arm.c tests/hello-arm \
tests/sha1.c \
tests/testsig.c tests/testclone.c tests/testthread.c \
tests/runcom.c tests/pi_10.com \
tests/test_path.c \
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
	tar zcvf /tmp/qemu-$(VERSION)-i386-glibc21.tar.gz \
                 $(BINPATH)/etc $(BINPATH)/lib $(BINPATH)/bin $(BINPATH)/usr
	tar zcvf /tmp/qemu-$(VERSION)-i386-wine.tar.gz \
                 $(BINPATH)/wine

ifneq ($(wildcard .depend),)
include .depend
endif
