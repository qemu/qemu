# Makefile for QEMU.

ifneq ($(wildcard config-host.mak),)
include config-host.mak
include $(SRC_PATH)/rules.mak
else
config-host.mak:
	@echo "Please call configure before running make!"
	@exit 1
endif

.PHONY: all clean cscope distclean dvi html info install install-doc \
	recurse-all speed tar tarbin test

VPATH=$(SRC_PATH):$(SRC_PATH)/hw


CFLAGS += $(OS_CFLAGS) $(ARCH_CFLAGS)
LDFLAGS += $(OS_LDFLAGS) $(ARCH_LDFLAGS)

CPPFLAGS += -I. -I$(SRC_PATH) -MMD -MP -MT $@
CPPFLAGS += -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
LIBS=
ifdef CONFIG_STATIC
LDFLAGS += -static
endif
ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-tech.html qemu.1 qemu-img.1 qemu-nbd.8
else
DOCS=
endif

LIBS+=$(PTHREADLIBS)
LIBS+=$(CLOCKLIBS)

ifdef CONFIG_SOLARIS
LIBS+=-lsocket -lnsl -lresolv
endif

ifdef CONFIG_WIN32
LIBS+=-lwinmm -lws2_32 -liphlpapi
endif

all: $(TOOLS) $(DOCS) recurse-all

config-host.mak: configure
ifneq ($(wildcard config-host.mak),)
	@echo $@ is out-of-date, running configure
	@sed -n "/.*Configured with/s/[^:]*: //p" $@ | sh
endif

SUBDIR_RULES=$(patsubst %,subdir-%, $(TARGET_DIRS))

subdir-%:
	$(call quiet-command,$(MAKE) -C $* V="$(V)" TARGET_DIR="$*/" all,)

$(filter %-softmmu,$(SUBDIR_RULES)): libqemu_common.a
$(filter %-user,$(SUBDIR_RULES)): libqemu_user.a

recurse-all: $(SUBDIR_RULES)

#######################################################################
# BLOCK_OBJS is code used by both qemu system emulation and qemu-img

BLOCK_OBJS=cutils.o qemu-malloc.o
BLOCK_OBJS+=block-cow.o block-qcow.o aes.o block-vmdk.o block-cloop.o
BLOCK_OBJS+=block-dmg.o block-bochs.o block-vpc.o block-vvfat.o
BLOCK_OBJS+=block-qcow2.o block-parallels.o block-nbd.o
BLOCK_OBJS+=nbd.o block.o aio.o

ifdef CONFIG_WIN32
BLOCK_OBJS += block-raw-win32.o
else
ifdef CONFIG_AIO
BLOCK_OBJS += posix-aio-compat.o
endif
BLOCK_OBJS += block-raw-posix.o
endif

######################################################################
# libqemu_common.a: Target independent part of system emulation. The
# long term path is to suppress *all* target specific code in case of
# system emulation, i.e. a single QEMU executable should support all
# CPUs and machines.

OBJS=$(BLOCK_OBJS)
OBJS+=readline.o console.o

OBJS+=irq.o ptimer.o
OBJS+=i2c.o smbus.o smbus_eeprom.o max7310.o max111x.o wm8750.o
OBJS+=ssd0303.o ssd0323.o ads7846.o stellaris_input.o twl92230.o
OBJS+=tmp105.o lm832x.o eeprom93xx.o tsc2005.o
OBJS+=scsi-disk.o cdrom.o
OBJS+=scsi-generic.o
OBJS+=usb.o usb-hub.o usb-$(HOST_USB).o usb-hid.o usb-msd.o usb-wacom.o
OBJS+=usb-serial.o usb-net.o
OBJS+=sd.o ssi-sd.o
OBJS+=bt.o bt-host.o bt-vhci.o bt-l2cap.o bt-sdp.o bt-hci.o bt-hid.o usb-bt.o
OBJS+=bt-hci-csr.o
OBJS+=buffered_file.o migration.o migration-tcp.o net.o qemu-sockets.o
OBJS+=qemu-char.o aio.o net-checksum.o savevm.o cache-utils.o
OBJS+=msmouse.o ps2.o

ifdef CONFIG_BRLAPI
OBJS+= baum.o
LIBS+=-lbrlapi
endif

ifdef CONFIG_WIN32
OBJS+=tap-win32.o
else
OBJS+=migration-exec.o
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
AUDIO_PT = yes
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
ifdef CONFIG_ESD
AUDIO_PT = yes
AUDIO_PT_INT = yes
AUDIO_OBJS += esdaudio.o
endif
ifdef CONFIG_PA
AUDIO_PT = yes
AUDIO_PT_INT = yes
AUDIO_OBJS += paaudio.o
endif
ifdef AUDIO_PT
LDFLAGS += -pthread
endif
ifdef AUDIO_PT_INT
AUDIO_OBJS += audio_pt_int.o
endif
AUDIO_OBJS+= wavcapture.o
OBJS+=$(addprefix audio/, $(AUDIO_OBJS))

OBJS+=keymaps.o
ifdef CONFIG_SDL
OBJS+=sdl.o x_keymap.o
endif
ifdef CONFIG_CURSES
OBJS+=curses.o
endif
OBJS+=vnc.o acl.o d3des.o
ifdef CONFIG_VNC_TLS
OBJS+=vnc-tls.o vnc-auth-vencrypt.o
endif
ifdef CONFIG_VNC_SASL
OBJS+=vnc-auth-sasl.o
endif

ifdef CONFIG_COCOA
OBJS+=cocoa.o
endif

ifdef CONFIG_IOTHREAD
OBJS+=qemu-thread.o
endif

ifdef CONFIG_SLIRP
CPPFLAGS+=-I$(SRC_PATH)/slirp
SLIRP_OBJS=cksum.o if.o ip_icmp.o ip_input.o ip_output.o \
slirp.o mbuf.o misc.o sbuf.o socket.o tcp_input.o tcp_output.o \
tcp_subr.o tcp_timer.o udp.o bootp.o debug.o tftp.o
OBJS+=$(addprefix slirp/, $(SLIRP_OBJS))
endif

LIBS+=$(VDE_LIBS)

cocoa.o: cocoa.m

keymaps.o: keymaps.c keymaps.h

sdl.o: sdl.c keymaps.h sdl_keysym.h

sdl.o audio/sdlaudio.o: CFLAGS += $(SDL_CFLAGS)

acl.o: acl.h acl.c

vnc.h: vnc-tls.h vnc-auth-vencrypt.h vnc-auth-sasl.h keymaps.h

vnc.o: vnc.c vnc.h vnc_keysym.h vnchextile.h d3des.c d3des.h acl.h

vnc.o: CFLAGS += $(CONFIG_VNC_TLS_CFLAGS)

vnc-tls.o: vnc-tls.c vnc.h

vnc-auth-vencrypt.o: vnc-auth-vencrypt.c vnc.h

vnc-auth-sasl.o: vnc-auth-sasl.c vnc.h

curses.o: curses.c keymaps.h curses_keys.h

bt-host.o: CFLAGS += $(CONFIG_BLUEZ_CFLAGS)

libqemu_common.a: $(OBJS)

#######################################################################
# USER_OBJS is code used by qemu userspace emulation
USER_OBJS=cutils.o  cache-utils.o

libqemu_user.a: $(USER_OBJS)

######################################################################

qemu-img$(EXESUF): qemu-img.o qemu-tool.o osdep.o $(BLOCK_OBJS)

qemu-nbd$(EXESUF):  qemu-nbd.o qemu-tool.o osdep.o $(BLOCK_OBJS)

qemu-io$(EXESUF):  qemu-io.o qemu-tool.o osdep.o cmd.o $(BLOCK_OBJS)

qemu-img$(EXESUF) qemu-nbd$(EXESUF) qemu-io$(EXESUF): LIBS += -lz

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f *.o *.d *.a $(TOOLS) TAGS cscope.* *.pod *~ */*~
	rm -f slirp/*.o slirp/*.d audio/*.o audio/*.d
	$(MAKE) -C tests clean
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h $(DOCS) qemu-options.texi
	rm -f qemu-{doc,tech}.{info,aux,cp,dvi,fn,info,ky,log,pg,toc,tp,vr}
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  modifiers  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
common  de-ch  es     fo  fr-ca  hu     ja  mk  nl-be      pt  sl     tr

ifdef INSTALL_BLOBS
BLOBS=bios.bin vgabios.bin vgabios-cirrus.bin ppc_rom.bin \
video.x openbios-sparc32 openbios-sparc64 openbios-ppc \
pxe-ne2k_pci.bin pxe-rtl8139.bin pxe-pcnet.bin pxe-e1000.bin \
bamboo.dtb
else
BLOBS=
endif

install-doc: $(DOCS)
	$(INSTALL_DIR) "$(DESTDIR)$(docdir)"
	$(INSTALL_DATA) qemu-doc.html  qemu-tech.html "$(DESTDIR)$(docdir)"
ifndef CONFIG_WIN32
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) qemu.1 qemu-img.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) qemu-nbd.8 "$(DESTDIR)$(mandir)/man8"
endif

install: all $(if $(BUILD_DOCS),install-doc)
	$(INSTALL_DIR) "$(DESTDIR)$(bindir)"
ifneq ($(TOOLS),)
	$(INSTALL_PROG) $(STRIP_OPT) $(TOOLS) "$(DESTDIR)$(bindir)"
endif
ifneq ($(BLOBS),)
	$(INSTALL_DIR) "$(DESTDIR)$(datadir)"
	set -e; for x in $(BLOBS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/$$x "$(DESTDIR)$(datadir)"; \
	done
endif
ifndef CONFIG_WIN32
	$(INSTALL_DIR) "$(DESTDIR)$(datadir)/keymaps"
	set -e; for x in $(KEYMAPS); do \
		$(INSTALL_DATA) $(SRC_PATH)/keymaps/$$x "$(DESTDIR)$(datadir)/keymaps"; \
	done
endif
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed: all
	$(MAKE) -C tests $@

TAGS:
	etags *.[ch] tests/*.[ch]

cscope:
	rm -f ./cscope.*
	find . -name "*.[ch]" -print | sed 's,^\./,,' > ./cscope.files
	cscope -b

# documentation
%.html: %.texi
	$(call quiet-command,texi2html -I=. -monolithic -number $<,"  GEN   $@")

%.info: %.texi
	$(call quiet-command,makeinfo -I . $< -o $@,"  GEN   $@")

%.dvi: %.texi
	$(call quiet-command,texi2dvi -I . $<,"  GEN   $@")

qemu-options.texi: $(SRC_PATH)/qemu-options.hx
	$(call quiet-command,sh $(SRC_PATH)/hxtool -t < $< > $@,"  GEN   $@")

qemu.1: qemu-doc.texi qemu-options.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/texi2pod.pl $< qemu.pod && \
	  pod2man --section=1 --center=" " --release=" " qemu.pod > $@, \
	  "  GEN   $@")

qemu-img.1: qemu-img.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/texi2pod.pl $< qemu-img.pod && \
	  pod2man --section=1 --center=" " --release=" " qemu-img.pod > $@, \
	  "  GEN   $@")

qemu-nbd.8: qemu-nbd.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/texi2pod.pl $< qemu-nbd.pod && \
	  pod2man --section=8 --center=" " --release=" " qemu-nbd.pod > $@, \
	  "  GEN   $@")

info: qemu-doc.info qemu-tech.info

dvi: qemu-doc.dvi qemu-tech.dvi

html: qemu-doc.html qemu-tech.html

qemu-doc.dvi qemu-doc.html qemu-doc.info: qemu-img.texi qemu-nbd.texi qemu-options.texi

VERSION ?= $(shell cat VERSION)
FILE = qemu-$(VERSION)

# tar release (use 'make -k tar' on a checkouted tree)
tar:
	rm -rf /tmp/$(FILE)
	cp -r . /tmp/$(FILE)
	cd /tmp && tar zcvf ~/$(FILE).tar.gz $(FILE) --exclude CVS --exclude .git --exclude .svn
	rm -rf /tmp/$(FILE)

# generate a binary distribution
tarbin:
	cd / && tar zcvf ~/qemu-$(VERSION)-$(ARCH).tar.gz \
	$(bindir)/qemu \
	$(bindir)/qemu-system-x86_64 \
	$(bindir)/qemu-system-arm \
	$(bindir)/qemu-system-cris \
	$(bindir)/qemu-system-m68k \
	$(bindir)/qemu-system-mips \
	$(bindir)/qemu-system-mipsel \
	$(bindir)/qemu-system-mips64 \
	$(bindir)/qemu-system-mips64el \
	$(bindir)/qemu-system-ppc \
	$(bindir)/qemu-system-ppcemb \
	$(bindir)/qemu-system-ppc64 \
	$(bindir)/qemu-system-sh4 \
	$(bindir)/qemu-system-sh4eb \
	$(bindir)/qemu-system-sparc \
	$(bindir)/qemu-i386 \
	$(bindir)/qemu-x86_64 \
	$(bindir)/qemu-alpha \
	$(bindir)/qemu-arm \
	$(bindir)/qemu-armeb \
	$(bindir)/qemu-cris \
	$(bindir)/qemu-m68k \
	$(bindir)/qemu-mips \
	$(bindir)/qemu-mipsel \
	$(bindir)/qemu-ppc \
	$(bindir)/qemu-ppc64 \
	$(bindir)/qemu-ppc64abi32 \
	$(bindir)/qemu-sh4 \
	$(bindir)/qemu-sh4eb \
	$(bindir)/qemu-sparc \
	$(bindir)/qemu-sparc64 \
	$(bindir)/qemu-sparc32plus \
	$(bindir)/qemu-img \
	$(bindir)/qemu-nbd \
	$(datadir)/bios.bin \
	$(datadir)/vgabios.bin \
	$(datadir)/vgabios-cirrus.bin \
	$(datadir)/ppc_rom.bin \
	$(datadir)/video.x \
	$(datadir)/openbios-sparc32 \
	$(datadir)/openbios-sparc64 \
	$(datadir)/openbios-ppc \
	$(datadir)/pxe-ne2k_pci.bin \
	$(datadir)/pxe-rtl8139.bin \
	$(datadir)/pxe-pcnet.bin \
	$(datadir)/pxe-e1000.bin \
	$(docdir)/qemu-doc.html \
	$(docdir)/qemu-tech.html \
	$(mandir)/man1/qemu.1 \
	$(mandir)/man1/qemu-img.1 \
	$(mandir)/man8/qemu-nbd.8

# Include automatically generated dependency files
-include $(wildcard *.d audio/*.d slirp/*.d)
