-include config-host.mak

CFLAGS=-Wall -O2 -g -fno-strict-aliasing 
ifdef CONFIG_DARWIN
CFLAGS+= -mdynamic-no-pic
endif
ifdef CONFIG_WIN32
CFLAGS+=-fpack-struct 
endif
LDFLAGS=-g
LIBS=
DEFINES+=-D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
TOOLS=qemu-img
ifdef CONFIG_STATIC
LDFLAGS+=-static
endif
DOCS=qemu-doc.html qemu-tech.html qemu.1

all: dyngen$(EXESUF) $(TOOLS) $(DOCS)
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

qemu-img: qemu-img.c block.c block-cow.c block-qcow.c aes.c block-vmdk.c block-cloop.c
	$(CC) -DQEMU_TOOL $(CFLAGS) $(LDFLAGS) $(DEFINES) -o $@ $^ -lz $(LIBS)

dyngen$(EXESUF): dyngen.c
	$(HOST_CC) $(CFLAGS) $(DEFINES) -o $@ $^

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h 
	rm -f *.o *.a $(TOOLS) dyngen$(EXESUF) TAGS qemu.pod *~ */*~
	$(MAKE) -C tests clean
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done

install: all 
	mkdir -p "$(bindir)"
ifndef CONFIG_WIN32
	install -m 755 -s $(TOOLS) "$(bindir)"
endif
	mkdir -p "$(datadir)"
	install -m 644 pc-bios/bios.bin pc-bios/vgabios.bin \
                       pc-bios/vgabios-cirrus.bin \
                       pc-bios/ppc_rom.bin \
                       pc-bios/proll.bin \
                       pc-bios/linux_boot.bin "$(datadir)"
	mkdir -p "$(docdir)"
	install -m 644 qemu-doc.html  qemu-tech.html "$(docdir)"
ifndef CONFIG_WIN32
	mkdir -p "$(mandir)/man1"
	install qemu.1 qemu-mkcow.1 "$(mandir)/man1"
endif
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed test2: all
	$(MAKE) -C tests $@

TAGS: 
	etags *.[ch] tests/*.[ch]

# documentation
%.html: %.texi
	texi2html -monolithic -number $<

qemu.1: qemu-doc.texi
	./texi2pod.pl $< qemu.pod
	pod2man --section=1 --center=" " --release=" " qemu.pod > $@

FILE=qemu-$(shell cat VERSION)

# tar release (use 'make -k tar' on a checkouted tree)
tar:
	rm -rf /tmp/$(FILE)
	cp -r . /tmp/$(FILE)
	( cd /tmp ; tar zcvf ~/$(FILE).tar.gz $(FILE) --exclude CVS )
	rm -rf /tmp/$(FILE)

# generate a binary distribution
tarbin:
	( cd / ; tar zcvf ~/qemu-$(VERSION)-i386.tar.gz \
	$(bindir)/qemu $(bindir)/qemu-fast \
	$(bindir)/qemu-system-ppc \
	$(bindir)/qemu-i386 \
        $(bindir)/qemu-arm \
        $(bindir)/qemu-sparc \
        $(bindir)/qemu-ppc \
        $(bindir)/qemu-img \
	$(datadir)/bios.bin \
	$(datadir)/vgabios.bin \
	$(datadir)/vgabios-cirrus.bin \
	$(datadir)/ppc_rom.bin \
	$(datadir)/proll.bin \
	$(datadir)/linux_boot.bin \
	$(docdir)/qemu-doc.html \
	$(docdir)/qemu-tech.html \
	$(mandir)/man1/qemu.1 $(mandir)/man1/qemu-mkcow.1 )

ifneq ($(wildcard .depend),)
include .depend
endif
