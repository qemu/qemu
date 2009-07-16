# Makefile for QEMU.

ifneq ($(wildcard config-host.mak),)
# Put the all: rule here so that config-host.mak can contain dependencies.
all: build-all
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
CPPFLAGS += -U_FORTIFY_SOURCE
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

build-all: $(TOOLS) $(DOCS) roms recurse-all

config-host.mak: configure
ifneq ($(wildcard config-host.mak),)
	@echo $@ is out-of-date, running configure
	@sed -n "/.*Configured with/s/[^:]*: //p" $@ | sh
endif

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory)
SUBDIR_RULES=$(patsubst %,subdir-%, $(TARGET_DIRS))

subdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $* V="$(V)" TARGET_DIR="$*/" all,)

$(filter %-softmmu,$(SUBDIR_RULES)): libqemu_common.a
$(filter %-user,$(SUBDIR_RULES)): libqemu_user.a

recurse-all: $(SUBDIR_RULES)

#######################################################################
# block-obj-y is code used by both qemu system emulation and qemu-img

block-obj-y = cutils.o cache-utils.o qemu-malloc.o qemu-option.o module.o
block-obj-y += nbd.o block.o aio.o aes.o

block-nested-y += cow.o qcow.o vmdk.o cloop.o dmg.o bochs.o vpc.o vvfat.o
block-nested-y += qcow2.o qcow2-refcount.o qcow2-cluster.o qcow2-snapshot.o
block-nested-y += parallels.o nbd.o


ifdef CONFIG_WIN32
block-nested-y += raw-win32.o
else
ifdef CONFIG_AIO
block-obj-y += posix-aio-compat.o
endif
block-nested-y += raw-posix.o
endif

block-nested-$(CONFIG_CURL) += curl.o

block-obj-y +=  $(addprefix block/, $(block-nested-y))

######################################################################
# libqemu_common.a: Target independent part of system emulation. The
# long term path is to suppress *all* target specific code in case of
# system emulation, i.e. a single QEMU executable should support all
# CPUs and machines.

obj-y = $(block-obj-y)
obj-y += readline.o console.o

obj-y += irq.o ptimer.o
obj-y += i2c.o smbus.o smbus_eeprom.o max7310.o max111x.o wm8750.o
obj-y += ssd0303.o ssd0323.o ads7846.o stellaris_input.o twl92230.o
obj-y += tmp105.o lm832x.o eeprom93xx.o tsc2005.o
obj-y += scsi-disk.o cdrom.o
obj-y += scsi-generic.o
obj-y += usb.o usb-hub.o usb-$(HOST_USB).o usb-hid.o usb-msd.o usb-wacom.o
obj-y += usb-serial.o usb-net.o
obj-y += sd.o ssi-sd.o
obj-y += bt.o bt-host.o bt-vhci.o bt-l2cap.o bt-sdp.o bt-hci.o bt-hid.o usb-bt.o
obj-y += bt-hci-csr.o
obj-y += buffered_file.o migration.o migration-tcp.o net.o qemu-sockets.o
obj-y += qemu-char.o aio.o net-checksum.o savevm.o
obj-y += msmouse.o ps2.o
obj-y += qdev.o qdev-properties.o ssi.o

obj-$(CONFIG_BRLAPI) += baum.o

ifdef CONFIG_BRLAPI
LIBS+=-lbrlapi
endif

ifdef CONFIG_WIN32
obj-y += tap-win32.o
else
obj-y += migration-exec.o
endif

ifdef CONFIG_COREAUDIO
AUDIO_PT = y
endif
ifdef CONFIG_FMOD
audio/audio.o audio/fmodaudio.o: CPPFLAGS := -I$(CONFIG_FMOD_INC) $(CPPFLAGS)
endif
ifdef CONFIG_ESD
AUDIO_PT = y
AUDIO_PT_INT = y
endif
ifdef CONFIG_PA
AUDIO_PT = y
AUDIO_PT_INT = y
endif
ifdef AUDIO_PT
LDFLAGS += -pthread
endif

audio-obj-y = audio.o noaudio.o wavaudio.o mixeng.o
audio-obj-$(CONFIG_SDL) += sdlaudio.o
audio-obj-$(CONFIG_OSS) += ossaudio.o
audio-obj-$(CONFIG_COREAUDIO) += coreaudio.o
audio-obj-$(CONFIG_ALSA) += alsaaudio.o
audio-obj-$(CONFIG_DSOUND) += dsoundaudio.o
audio-obj-$(CONFIG_FMOD) += fmodaudio.o
audio-obj-$(CONFIG_ESD) += esdaudio.o
audio-obj-$(CONFIG_PA) += paaudio.o
audio-obj-$(AUDIO_PT_INT) += audio_pt_int.o
audio-obj-y += wavcapture.o
obj-y += $(addprefix audio/, $(audio-obj-y))

obj-y += keymaps.o
obj-$(CONFIG_SDL) += sdl.o sdl_zoom.o x_keymap.o
obj-$(CONFIG_CURSES) += curses.o
obj-y += vnc.o acl.o d3des.o
obj-$(CONFIG_VNC_TLS) += vnc-tls.o vnc-auth-vencrypt.o
obj-$(CONFIG_VNC_SASL) += vnc-auth-sasl.o
obj-$(CONFIG_COCOA) += cocoa.o
obj-$(CONFIG_IOTHREAD) += qemu-thread.o

ifdef CONFIG_SLIRP
CPPFLAGS+=-I$(SRC_PATH)/slirp
endif

slirp-obj-y = cksum.o if.o ip_icmp.o ip_input.o ip_output.o
slirp-obj-y += slirp.o mbuf.o misc.o sbuf.o socket.o tcp_input.o tcp_output.o
slirp-obj-y += tcp_subr.o tcp_timer.o udp.o bootp.o tftp.o
obj-$(CONFIG_SLIRP) += $(addprefix slirp/, $(slirp-obj-y))

LIBS+=$(VDE_LIBS)

# xen backend driver support
obj-$(CONFIG_XEN) += xen_backend.o xen_devconfig.o
obj-$(CONFIG_XEN) += xen_console.o xenfb.o xen_disk.o xen_nic.o

LIBS+=$(CURL_LIBS)

cocoa.o: cocoa.m

keymaps.o: keymaps.c keymaps.h

sdl_zoom.o: sdl_zoom.c sdl_zoom.h sdl_zoom_template.h

sdl.o: sdl.c keymaps.h sdl_keysym.h sdl_zoom.h

sdl.o audio/sdlaudio.o sdl_zoom.o baum.o: CFLAGS += $(SDL_CFLAGS)

acl.o: acl.h acl.c

vnc.h: vnc-tls.h vnc-auth-vencrypt.h vnc-auth-sasl.h keymaps.h

vnc.o: vnc.c vnc.h vnc_keysym.h vnchextile.h d3des.c d3des.h acl.h

vnc.o: CFLAGS += $(CONFIG_VNC_TLS_CFLAGS)

vnc-tls.o: vnc-tls.c vnc.h

vnc-auth-vencrypt.o: vnc-auth-vencrypt.c vnc.h

vnc-auth-sasl.o: vnc-auth-sasl.c vnc.h

curses.o: curses.c keymaps.h curses_keys.h

bt-host.o: CFLAGS += $(CONFIG_BLUEZ_CFLAGS)

libqemu_common.a: $(obj-y)

#######################################################################
# user-obj-y is code used by qemu userspace emulation
user-obj-y = cutils.o cache-utils.o

libqemu_user.a: $(user-obj-y)

######################################################################

qemu-img.o: qemu-img-cmds.h

qemu-img$(EXESUF): qemu-img.o qemu-tool.o tool-osdep.o $(block-obj-y)

qemu-nbd$(EXESUF):  qemu-nbd.o qemu-tool.o tool-osdep.o $(block-obj-y)

qemu-io$(EXESUF):  qemu-io.o qemu-tool.o tool-osdep.o cmd.o $(block-obj-y)

qemu-img$(EXESUF) qemu-nbd$(EXESUF) qemu-io$(EXESUF): LIBS += -lz

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx
	$(call quiet-command,sh $(SRC_PATH)/hxtool -h < $< > $@,"  GEN   $@")

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak config.h op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f *.o *.d *.a $(TOOLS) TAGS cscope.* *.pod *~ */*~
	rm -f slirp/*.o slirp/*.d audio/*.o audio/*.d block/*.o block/*.d
	rm -f qemu-img-cmds.h
	$(MAKE) -C tests clean
	for d in $(TARGET_DIRS) $(ROMS) libhw32 libhw64; do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h $(DOCS) qemu-options.texi qemu-img-cmds.texi
	rm -f qemu-{doc,tech}.{info,aux,cp,dvi,fn,info,ky,log,pg,toc,tp,vr}
	for d in $(TARGET_DIRS) libhw32 libhw64; do \
	rm -rf $$d || exit 1 ; \
        done

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  modifiers  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
common  de-ch  es     fo  fr-ca  hu     ja  mk  nl-be      pt  sl     tr

ifdef INSTALL_BLOBS
BLOBS=bios.bin vgabios.bin vgabios-cirrus.bin ppc_rom.bin \
video.x openbios-sparc32 openbios-sparc64 openbios-ppc \
pxe-ne2k_pci.bin pxe-rtl8139.bin pxe-pcnet.bin pxe-e1000.bin \
bamboo.dtb petalogix-s3adsp1800.dtb \
multiboot.bin
else
BLOBS=
endif

roms:
	for d in $(ROMS); do \
	$(MAKE) -C $$d || exit 1 ; \
        done

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
	$(INSTALL_DIR) "$(DESTDIR)$(datadir)/keymaps"
	set -e; for x in $(KEYMAPS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/keymaps/$$x "$(DESTDIR)$(datadir)/keymaps"; \
	done
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed: all
	$(MAKE) -C tests $@

TAGS:
	etags *.[ch] tests/*.[ch] block/*.[ch] hw/*.[ch]

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

qemu-monitor.texi: $(SRC_PATH)/qemu-monitor.hx
	$(call quiet-command,sh $(SRC_PATH)/hxtool -t < $< > $@,"  GEN   $@")

qemu-img-cmds.texi: $(SRC_PATH)/qemu-img-cmds.hx
	$(call quiet-command,sh $(SRC_PATH)/hxtool -t < $< > $@,"  GEN   $@")

qemu.1: qemu-doc.texi qemu-options.texi qemu-monitor.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/texi2pod.pl $< qemu.pod && \
	  pod2man --section=1 --center=" " --release=" " qemu.pod > $@, \
	  "  GEN   $@")

qemu-img.1: qemu-img.texi qemu-img-cmds.texi
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

qemu-doc.dvi qemu-doc.html qemu-doc.info: qemu-img.texi qemu-nbd.texi qemu-options.texi qemu-monitor.texi qemu-img-cmds.texi

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
-include $(wildcard *.d audio/*.d slirp/*.d block/*.d)
