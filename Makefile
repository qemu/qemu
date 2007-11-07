# Makefile for QEMU.

include config-host.mak

.PHONY: all clean distclean dvi info install install-doc tar tarbin \
	speed test test2 html dvi info

BASE_CFLAGS=
BASE_LDFLAGS=

BASE_CFLAGS += $(OS_CFLAGS) $(ARCH_CFLAGS)
BASE_LDFLAGS += $(OS_LDFLAGS) $(ARCH_LDFLAGS)

CPPFLAGS += -I. -I$(SRC_PATH) -MMD -MP
CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
CPPFLAGS += -DQEMU_TOOL
LIBS=
ifdef CONFIG_STATIC
BASE_LDFLAGS += -static
endif
ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-tech.html qemu.1 qemu-img.1
else
DOCS=
endif

LIBS+=$(AIOLIBS)

all: libqemu_common.a $(TOOLS) $(DOCS) recurse-all 

subdir-%: dyngen$(EXESUF)
	$(MAKE) -C $(subst subdir-,,$@) all

recurse-all: $(patsubst %,subdir-%, $(TARGET_DIRS))

######################################################################
# libqemu_common.a: target indepedent part of system emulation. The
# long term path is to suppress *all* target specific code in case of
# system emulation, i.e. a single QEMU executable should support all
# CPUs and machines.

OBJS+=cutils.o readline.o console.o 
#OBJS+=block.o block-raw.o
OBJS+=block-cow.o block-qcow.o aes.o block-vmdk.o block-cloop.o block-dmg.o block-bochs.o block-vpc.o block-vvfat.o block-qcow2.o block-parallels.o

ifdef CONFIG_WIN32
OBJS+=tap-win32.o
endif

AUDIO_OBJS = audio.o noaudio.o wavaudio.o mixeng.o
ifdef CONFIG_SDL
AUDIO_OBJS += sdlaudio.o
endif
ifdef CONFIG_OSS
AUDIO_OBJS += ossaudio.o
endif
ifdef CONFIG_COREAUDIO
AUDIO_OBJS += coreaudio.o
endif
ifdef CONFIG_ALSA
AUDIO_OBJS += alsaaudio.o
endif
ifdef CONFIG_DSOUND
AUDIO_OBJS += dsoundaudio.o
endif
ifdef CONFIG_FMOD
AUDIO_OBJS += fmodaudio.o
audio/audio.o audio/fmodaudio.o: CPPFLAGS := -I$(CONFIG_FMOD_INC) $(CPPFLAGS)
endif
AUDIO_OBJS+= wavcapture.o
OBJS+=$(addprefix audio/, $(AUDIO_OBJS))

ifdef CONFIG_SDL
OBJS+=sdl.o x_keymap.o
endif
OBJS+=vnc.o d3des.o

ifdef CONFIG_COCOA
OBJS+=cocoa.o
endif

ifdef CONFIG_SLIRP
CPPFLAGS+=-I$(SRC_PATH)/slirp
SLIRP_OBJS=cksum.o if.o ip_icmp.o ip_input.o ip_output.o \
slirp.o mbuf.o misc.o sbuf.o socket.o tcp_input.o tcp_output.o \
tcp_subr.o tcp_timer.o udp.o bootp.o debug.o tftp.o
OBJS+=$(addprefix slirp/, $(SLIRP_OBJS))
endif

cocoa.o: cocoa.m
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) -c -o $@ $<

sdl.o: sdl.c keymaps.c sdl_keysym.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) $(BASE_CFLAGS) -c -o $@ $<

vnc.o: vnc.c keymaps.c sdl_keysym.h vnchextile.h d3des.c d3des.h
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) -c -o $@ $<

audio/sdlaudio.o: audio/sdlaudio.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(SDL_CFLAGS) $(BASE_CFLAGS) -c -o $@ $<

libqemu_common.a: $(OBJS)
	rm -f $@ 
	$(AR) rcs $@ $(OBJS)

######################################################################

qemu-img$(EXESUF): qemu-img.o block.o block-raw.o libqemu_common.a
	$(CC) $(LDFLAGS) $(BASE_LDFLAGS) -o $@ $^ -lz $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) -c -o $@ $<

# dyngen host tool
dyngen$(EXESUF): dyngen.c
	$(HOST_CC) $(CFLAGS) $(CPPFLAGS) $(BASE_CFLAGS) -o $@ $^

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f *.o *.d *.a $(TOOLS) dyngen$(EXESUF) TAGS cscope.* *.pod *~ */*~
	rm -f slirp/*.o slirp/*.d audio/*.o audio/*.d
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
ifneq ($(TOOLS),)
	$(INSTALL) -m 755 -s $(TOOLS) "$(DESTDIR)$(bindir)"
endif
	mkdir -p "$(DESTDIR)$(datadir)"
	for x in bios.bin vgabios.bin vgabios-cirrus.bin ppc_rom.bin \
		video.x openbios-sparc32 pxe-ne2k_pci.bin \
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
	( cd / ; tar zcvf ~/qemu-$(VERSION)-$(ARCH).tar.gz \
	$(bindir)/qemu \
	$(bindir)/qemu-system-ppc \
	$(bindir)/qemu-system-ppc64 \
	$(bindir)/qemu-system-ppcemb \
	$(bindir)/qemu-system-sparc \
	$(bindir)/qemu-system-x86_64 \
	$(bindir)/qemu-system-mips \
	$(bindir)/qemu-system-mipsel \
	$(bindir)/qemu-system-mips64 \
	$(bindir)/qemu-system-mips64el \
	$(bindir)/qemu-system-arm \
	$(bindir)/qemu-system-m68k \
	$(bindir)/qemu-system-sh4 \
	$(bindir)/qemu-i386 \
        $(bindir)/qemu-arm \
        $(bindir)/qemu-armeb \
        $(bindir)/qemu-sparc \
        $(bindir)/qemu-ppc \
        $(bindir)/qemu-ppc64 \
        $(bindir)/qemu-mips \
        $(bindir)/qemu-mipsel \
        $(bindir)/qemu-mipsn32 \
        $(bindir)/qemu-mipsn32el \
        $(bindir)/qemu-mips64 \
        $(bindir)/qemu-mips64el \
        $(bindir)/qemu-alpha \
        $(bindir)/qemu-m68k \
        $(bindir)/qemu-sh4 \
        $(bindir)/qemu-img \
	$(datadir)/bios.bin \
	$(datadir)/vgabios.bin \
	$(datadir)/vgabios-cirrus.bin \
	$(datadir)/ppc_rom.bin \
	$(datadir)/video.x \
	$(datadir)/openbios-sparc32 \
        $(datadir)/pxe-ne2k_pci.bin \
	$(datadir)/pxe-rtl8139.bin \
        $(datadir)/pxe-pcnet.bin \
	$(docdir)/qemu-doc.html \
	$(docdir)/qemu-tech.html \
	$(mandir)/man1/qemu.1 $(mandir)/man1/qemu-img.1 )

ifneq ($(wildcard .depend),)
include .depend
endif

# Include automatically generated dependency files
-include $(wildcard *.d audio/*.d slirp/*.d)
