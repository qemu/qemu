include config-host.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=-g
LIBS=
DEFINES+=-D_GNU_SOURCE
TOOLS=qemu-mkcow

all: dyngen $(TOOLS) qemu-doc.html qemu.1
	for d in $(TARGET_DIRS); do \
	make -C $$d $@ || exit 1 ; \
        done

qemu-mkcow: qemu-mkcow.o
	$(HOST_CC) -o $@ $^  $(LIBS)

dyngen: dyngen.o
	$(HOST_CC) -o $@ $^  $(LIBS)

%.o: %.c
	$(HOST_CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h 
	rm -f *.o *.a $(TOOLS) dyngen TAGS qemu.pod
	make -C tests clean
	for d in $(TARGET_DIRS); do \
	make -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done

install: all 
	mkdir -p $(prefix)/bin
	install -m 755 -s $(TOOLS) $(prefix)/bin
	mkdir -p $(sharedir)
	install -m 644 pc-bios/bios.bin pc-bios/vgabios.bin \
                       pc-bios/linux_boot.bin $(sharedir)
	mkdir -p $(mandir)/man1
	install qemu.1 qemu-mkcow.1 $(mandir)/man1
	for d in $(TARGET_DIRS); do \
	make -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed test2: all
	make -C tests $@

TAGS: 
	etags *.[ch] tests/*.[ch]

# documentation
qemu-doc.html: qemu-doc.texi
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
	$(prefix)/bin/qemu $(prefix)/bin/qemu-fast \
	$(prefix)/bin/qemu-i386 \
        $(prefix)/bin/qemu-arm \
        $(prefix)/bin/qemu-sparc \
        $(prefix)/bin/qemu-ppc \
	$(sharedir)/bios.bin \
	$(sharedir)/vgabios.bin \
	$(mandir)/man1/qemu.1 )

ifneq ($(wildcard .depend),)
include .depend
endif
