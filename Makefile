# Makefile for QEMU.

ifneq ($(wildcard config-host.mak),)
# Put the all: rule here so that config-host.mak can contain dependencies.
all: build-all
include config-host.mak
include $(SRC_PATH)/rules.mak
config-host.mak: configure
	@echo $@ is out-of-date, running configure
	@sed -n "/.*Configured with/s/[^:]*: //p" $@ | sh
else
config-host.mak:
	@echo "Please call configure before running make!"
	@exit 1
endif

# Don't try to regenerate Makefile or configure
# We don't generate any of them
Makefile: ;
configure: ;

.PHONY: all clean cscope distclean dvi html info install install-doc \
	recurse-all speed tar tarbin test build-all

VPATH=$(SRC_PATH):$(SRC_PATH)/hw

LIBS+=-lz $(LIBS_TOOLS)

ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-tech.html qemu.1 qemu-img.1 qemu-nbd.8
else
DOCS=
endif

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory)
SUBDIR_DEVICES_MAK=$(patsubst %, %/config-devices.mak, $(TARGET_DIRS))

config-all-devices.mak: $(SUBDIR_DEVICES_MAK)
	$(call quiet-command,cat $(SUBDIR_DEVICES_MAK) | grep "=y$$" | sort -u > $@,"  GEN  $@")

-include config-all-devices.mak

build-all: config-host.h config-all-devices.h
	$(call quiet-command, $(MAKE) $(SUBDIR_MAKEFLAGS) $(TOOLS) $(DOCS) recurse-all,)

config-host.h: config-host.h-timestamp
config-host.h-timestamp: config-host.mak

config-all-devices.h: config-all-devices.h-timestamp
config-all-devices.h-timestamp: config-all-devices.mak

SUBDIR_RULES=$(patsubst %,subdir-%, $(TARGET_DIRS))

subdir-%: config-host.h config-all-devices.h
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $* V="$(V)" TARGET_DIR="$*/" all,)

$(filter %-softmmu,$(SUBDIR_RULES)): libqemu_common.a

$(filter %-user,$(SUBDIR_RULES)): libuser.a

libuser.a:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C libuser V="$(V)" TARGET_DIR="$*/" all,)

ROMSUBDIR_RULES=$(patsubst %,romsubdir-%, $(ROMS))
romsubdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C pc-bios/$* V="$(V)" TARGET_DIR="$*/",)

ALL_SUBDIRS=$(TARGET_DIRS) $(patsubst %,pc-bios/%, $(ROMS))

recurse-all: $(SUBDIR_RULES) $(ROMSUBDIR_RULES)

#######################################################################
# block-obj-y is code used by both qemu system emulation and qemu-img

block-obj-y = cutils.o cache-utils.o qemu-malloc.o qemu-option.o module.o
block-obj-y += nbd.o block.o aio.o aes.o osdep.o
block-obj-$(CONFIG_POSIX) += posix-aio-compat.o
block-obj-$(CONFIG_LINUX_AIO) += linux-aio.o

block-nested-y += cow.o qcow.o vdi.o vmdk.o cloop.o dmg.o bochs.o vpc.o vvfat.o
block-nested-y += qcow2.o qcow2-refcount.o qcow2-cluster.o qcow2-snapshot.o
block-nested-y += parallels.o nbd.o
block-nested-$(CONFIG_WIN32) += raw-win32.o
block-nested-$(CONFIG_POSIX) += raw-posix.o
block-nested-$(CONFIG_CURL) += curl.o

block-obj-y +=  $(addprefix block/, $(block-nested-y))

######################################################################
# libqemu_common.a: Target independent part of system emulation. The
# long term path is to suppress *all* target specific code in case of
# system emulation, i.e. a single QEMU executable should support all
# CPUs and machines.

obj-y = $(block-obj-y)
obj-y += readline.o console.o

obj-y += tcg-runtime.o host-utils.o
obj-y += irq.o ioport.o
obj-$(CONFIG_PTIMER) += ptimer.o
obj-y += i2c.o smbus.o smbus_eeprom.o max7310.o max111x.o wm8750.o
obj-y += ssd0303.o ssd0323.o ads7846.o stellaris_input.o twl92230.o
obj-y += tmp105.o lm832x.o eeprom93xx.o tsc2005.o
obj-y += scsi-disk.o cdrom.o
obj-y += scsi-generic.o scsi-bus.o
obj-y += usb.o usb-hub.o usb-$(HOST_USB).o usb-hid.o usb-msd.o usb-wacom.o
obj-y += usb-serial.o usb-net.o usb-bus.o
obj-y += sd.o ssi-sd.o
obj-y += bt.o bt-host.o bt-vhci.o bt-l2cap.o bt-sdp.o bt-hci.o bt-hid.o usb-bt.o
obj-y += bt-hci-csr.o
obj-y += buffered_file.o migration.o migration-tcp.o net.o qemu-sockets.o
obj-y += qemu-char.o aio.o net-checksum.o savevm.o
obj-y += msmouse.o ps2.o
obj-y += qdev.o qdev-properties.o ssi.o
obj-y += qint.o qstring.o qdict.o qemu-config.o

obj-$(CONFIG_BRLAPI) += baum.o
obj-$(CONFIG_WIN32) += tap-win32.o
obj-$(CONFIG_POSIX) += migration-exec.o migration-unix.o migration-fd.o

audio/audio.o audio/fmodaudio.o: QEMU_CFLAGS += $(FMOD_CFLAGS)

audio-obj-y = audio.o noaudio.o wavaudio.o mixeng.o
audio-obj-$(CONFIG_SDL) += sdlaudio.o
audio-obj-$(CONFIG_OSS) += ossaudio.o
audio-obj-$(CONFIG_COREAUDIO) += coreaudio.o
audio-obj-$(CONFIG_ALSA) += alsaaudio.o
audio-obj-$(CONFIG_DSOUND) += dsoundaudio.o
audio-obj-$(CONFIG_FMOD) += fmodaudio.o
audio-obj-$(CONFIG_ESD) += esdaudio.o
audio-obj-$(CONFIG_PA) += paaudio.o
audio-obj-$(CONFIG_AUDIO_PT_INT) += audio_pt_int.o
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

slirp-obj-y = cksum.o if.o ip_icmp.o ip_input.o ip_output.o
slirp-obj-y += slirp.o mbuf.o misc.o sbuf.o socket.o tcp_input.o tcp_output.o
slirp-obj-y += tcp_subr.o tcp_timer.o udp.o bootp.o tftp.o
obj-$(CONFIG_SLIRP) += $(addprefix slirp/, $(slirp-obj-y))

# xen backend driver support
obj-$(CONFIG_XEN) += xen_backend.o xen_devconfig.o
obj-$(CONFIG_XEN) += xen_console.o xenfb.o xen_disk.o xen_nic.o

QEMU_CFLAGS+=$(CURL_CFLAGS)

cocoa.o: cocoa.m

keymaps.o: keymaps.c keymaps.h

sdl_zoom.o: sdl_zoom.c sdl_zoom.h sdl_zoom_template.h

sdl.o: sdl.c keymaps.h sdl_keysym.h sdl_zoom.h

sdl.o audio/sdlaudio.o sdl_zoom.o baum.o: QEMU_CFLAGS += $(SDL_CFLAGS)

acl.o: acl.h acl.c

vnc.h: vnc-tls.h vnc-auth-vencrypt.h vnc-auth-sasl.h keymaps.h

vnc.o: vnc.c vnc.h vnc_keysym.h vnchextile.h d3des.c d3des.h acl.h

vnc.o: QEMU_CFLAGS += $(VNC_TLS_CFLAGS)

vnc-tls.o: vnc-tls.c vnc.h

vnc-auth-vencrypt.o: vnc-auth-vencrypt.c vnc.h

vnc-auth-sasl.o: vnc-auth-sasl.c vnc.h

curses.o: curses.c keymaps.h curses_keys.h

bt-host.o: QEMU_CFLAGS += $(BLUEZ_CFLAGS)

libqemu_common.a: $(obj-y)

######################################################################

qemu-img.o: qemu-img-cmds.h

qemu-img$(EXESUF): qemu-img.o qemu-tool.o $(block-obj-y)

qemu-nbd$(EXESUF):  qemu-nbd.o qemu-tool.o $(block-obj-y)

qemu-io$(EXESUF):  qemu-io.o qemu-tool.o cmd.o $(block-obj-y)

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx
	$(call quiet-command,sh $(SRC_PATH)/hxtool -h < $< > $@,"  GEN   $@")

check-qint: check-qint.o qint.o qemu-malloc.o
check-qstring: check-qstring.o qstring.o qemu-malloc.o
check-qdict: check-qdict.o qdict.o qint.o qstring.o qemu-malloc.o

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f *.o *.d *.a $(TOOLS) TAGS cscope.* *.pod *~ */*~
	rm -f slirp/*.o slirp/*.d audio/*.o audio/*.d block/*.o block/*.d
	rm -f qemu-img-cmds.h
	$(MAKE) -C tests clean
	for d in $(ALL_SUBDIRS) libhw32 libhw64 libuser; do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

distclean: clean
	rm -f config-host.mak config-host.h* config-host.ld $(DOCS) qemu-options.texi qemu-img-cmds.texi
	rm -f config-all-devices.mak config-all-devices.h*
	rm -f qemu-{doc,tech}.{info,aux,cp,dvi,fn,info,ky,log,pg,toc,tp,vr}
	for d in $(TARGET_DIRS) libhw32 libhw64 libuser; do \
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

install-doc: $(DOCS)
	$(INSTALL_DIR) "$(DESTDIR)$(docdir)"
	$(INSTALL_DATA) qemu-doc.html  qemu-tech.html "$(DESTDIR)$(docdir)"
ifdef CONFIG_POSIX
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

.PHONY: TAGS
TAGS:
	find "$(SRC_PATH)" -name '*.[hc]' -print0 | xargs -0 etags

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
	$(bindir)/qemu-system-microblaze \
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
	$(bindir)/qemu-microblaze \
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
