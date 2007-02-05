# Makefile for QEMU.

include config-host.mak

.PHONY: all clean distclean dvi info install install-doc tar tarbin \
	speed test test2 html dvi info

BASE_CFLAGS=
BASE_LDFLAGS=

BASE_CFLAGS += $(OS_CFLAGS)
ifeq ($(ARCH),sparc)
BASE_CFLAGS += -mcpu=ultrasparc
endif
CPPFLAGS += -I. -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
LIBS=
TOOLS=qemu-img$(EXESUF)
ifdef CONFIG_STATIC
BASE_LDFLAGS += -static
endif
ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-tech.html qemu.1 qemu-img.1
else
DOCS=
endif

ifndef CONFIG_DARWIN
ifndef CONFIG_WIN32
ifndef CONFIG_SOLARIS
LIBS+=-lrt
endif
endif
endif

all: $(TOOLS) $(DOCS) recurse-all

subdir-%: dyngen$(EXESUF)
	$(MAKE) -C $(subst subdir-,,$@) all

recurse-all: $(patsubst %,subdir-%, $(TARGET_DIRS))

qemu-img$(EXESUF): qemu-img.c cutils.c block.c block-raw.c block-cow.c block-qcow.c aes.c block-vmdk.c block-cloop.c block-dmg.c block-bochs.c block-vpc.c block-vvfat.c block-qcow2.c
	$(CC) -DQEMU_TOOL $(CFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) $(LDFLAGS) $(BASE_LDFLAGS) -o $@ $^ -lz $(LIBS)

dyngen$(EXESUF): dyngen.c
	$(HOST_CC) $(CFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) -o $@ $^

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h 
	rm -f *.o *.a $(TOOLS) dyngen$(EXESUF) TAGS *.pod *~ */*~
	$(MAKE) -C tests clean
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h $(DOCS)
	rm -f qemu-{doc,tech}.{info,aux,cp,dvi,fn,info,ky,log,pg,toc,tp,vr}
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  modifiers  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
common  de-ch  es     fo  fr-ca  hu     ja  mk  nl-be      pt  sl     tr

install-doc: $(DOCS)
	mkdir -p "$(DESTDIR)$(docdir)"
	$(INSTALL) -m 644 qemu-doc.html  qemu-tech.html "$(DESTDIR)$(docdir)"
ifndef CONFIG_WIN32
	mkdir -p "$(DESTDIR)$(mandir)/man1"
	$(INSTALL) qemu.1 qemu-img.1 "$(DESTDIR)$(mandir)/man1"
endif

install: all $(if $(BUILD_DOCS),install-doc)
	mkdir -p "$(DESTDIR)$(bindir)"
	$(INSTALL) -m 755 -s $(TOOLS) "$(DESTDIR)$(bindir)"
	mkdir -p "$(DESTDIR)$(datadir)"
	for x in bios.bin vgabios.bin vgabios-cirrus.bin ppc_rom.bin \
		video.x openbios-sparc32 linux_boot.bin pxe-ne2k_pci.bin \
		pxe-rtl8139.bin pxe-pcnet.bin; do \
		$(INSTALL) -m 644 $(SRC_PATH)/pc-bios/$$x "$(DESTDIR)$(datadir)"; \
	done
ifndef CONFIG_WIN32
	mkdir -p "$(DESTDIR)$(datadir)/keymaps"
	for x in $(KEYMAPS); do \
		$(INSTALL) -m 644 $(SRC_PATH)/keymaps/$$x "$(DESTDIR)$(datadir)/keymaps"; \
	done
endif
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed test2: all
	$(MAKE) -C tests $@

TAGS: 
	etags *.[ch] tests/*.[ch]

cscope:
	rm -f ./cscope.*
	find . -name "*.[ch]" -print > ./cscope.files
	cscope -b

# documentation
%.html: %.texi
	texi2html -monolithic -number $<

%.info: %.texi
	makeinfo $< -o $@

%.dvi: %.texi
	texi2dvi $<

qemu.1: qemu-doc.texi
	$(SRC_PATH)/texi2pod.pl $< qemu.pod
	pod2man --section=1 --center=" " --release=" " qemu.pod > $@

qemu-img.1: qemu-img.texi
	$(SRC_PATH)/texi2pod.pl $< qemu-img.pod
	pod2man --section=1 --center=" " --release=" " qemu-img.pod > $@

info: qemu-doc.info qemu-tech.info

dvi: qemu-doc.dvi qemu-tech.dvi

html: qemu-doc.html qemu-tech.html

VERSION ?= $(shell cat VERSION)
FILE = qemu-$(VERSION)

# tar release (use 'make -k tar' on a checkouted tree)
tar:
	rm -rf /tmp/$(FILE)
	cp -r . /tmp/$(FILE)
	( cd /tmp ; tar zcvf ~/$(FILE).tar.gz $(FILE) --exclude CVS )
	rm -rf /tmp/$(FILE)

# generate a binary distribution
tarbin:
	( cd / ; tar zcvf ~/qemu-$(VERSION)-i386.tar.gz \
	$(bindir)/qemu \
	$(bindir)/qemu-system-ppc \
	$(bindir)/qemu-system-sparc \
	$(bindir)/qemu-system-x86_64 \
	$(bindir)/qemu-system-mips \
	$(bindir)/qemu-system-mipsel \
	$(bindir)/qemu-system-arm \
	$(bindir)/qemu-i386 \
        $(bindir)/qemu-arm \
        $(bindir)/qemu-armeb \
        $(bindir)/qemu-sparc \
        $(bindir)/qemu-ppc \
        $(bindir)/qemu-mips \
        $(bindir)/qemu-mipsel \
        $(bindir)/qemu-img \
	$(datadir)/bios.bin \
	$(datadir)/vgabios.bin \
	$(datadir)/vgabios-cirrus.bin \
	$(datadir)/ppc_rom.bin \
	$(datadir)/video.x \
	$(datadir)/openbios-sparc32 \
	$(datadir)/linux_boot.bin \
        $(datadir)/pxe-ne2k_pci.bin \
	$(datadir)/pxe-rtl8139.bin \
        $(datadir)/pxe-pcnet.bin \
	$(docdir)/qemu-doc.html \
	$(docdir)/qemu-tech.html \
	$(mandir)/man1/qemu.1 $(mandir)/man1/qemu-img.1 )

ifneq ($(wildcard .depend),)
include .depend
endif
