include config-host.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=-g
LIBS=
DEFINES+=-D_GNU_SOURCE
TOOLS=vlmkcow

all: dyngen $(TOOLS) qemu-doc.html
	for d in $(TARGET_DIRS); do \
	make -C $$d $@ || exit 1 ; \
        done

vlmkcow: vlmkcow.o
	$(HOST_CC) -o $@ $^  $(LIBS)

dyngen: dyngen.o
	$(HOST_CC) -o $@ $^  $(LIBS)

%.o: %.c
	$(HOST_CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h 
	rm -f *.o *.a $(TOOLS) dyngen TAGS
	for d in $(TARGET_DIRS); do \
	make -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h
	for d in $(TARGET_DIRS); do \
	rm -f $$d/config.h $$d/config.mak || exit 1 ; \
        done

install: all 
	mkdir -p $(prefix)/bin
	install -m 755 -s $(TOOLS) $(prefix)/bin
	for d in $(TARGET_DIRS); do \
	make -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed: all
	make -C tests $@

TAGS: 
	etags *.[ch] tests/*.[ch]

# documentation
qemu-doc.html: qemu-doc.texi
	texi2html -monolithic -number $<

FILES= \
README README.distrib COPYING COPYING.LIB TODO Changelog VERSION \
configure Makefile Makefile.target \
dyngen.c dyngen.h dyngen-exec.h ioctls.h syscall_types.h \
elf.h elfload.c main.c signal.c qemu.h \
syscall.c syscall_defs.h vm86.c path.c mmap.c \
i386.ld ppc.ld alpha.ld s390.ld sparc.ld arm.ld m68k.ld \
vl.c i386-vl.ld vl.h block.c vlmkcow.c vga.c vga_template.h sdl.c \
thunk.c cpu-exec.c translate.c cpu-all.h cpu-defs.h thunk.h exec.h\
exec.c cpu-exec.c gdbstub.c bswap.h \
cpu-i386.h op-i386.c helper-i386.c helper2-i386.c syscall-i386.h translate-i386.c \
exec-i386.h ops_template.h ops_template_mem.h op_string.h opreg_template.h \
ops_mem.h softmmu_template.h softmmu_header.h \
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
