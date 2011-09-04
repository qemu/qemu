# Makefile for QEMU.

GENERATED_HEADERS = config-host.h trace.h qemu-options.def
ifeq ($(TRACE_BACKEND),dtrace)
GENERATED_HEADERS += trace-dtrace.h
endif

ifneq ($(wildcard config-host.mak),)
# Put the all: rule here so that config-host.mak can contain dependencies.
all: build-all
include config-host.mak
include $(SRC_PATH)/rules.mak
config-host.mak: $(SRC_PATH)/configure
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
	pdf recurse-all speed tar tarbin test build-all

$(call set-vpath, $(SRC_PATH):$(SRC_PATH)/hw)

LIBS+=-lz $(LIBS_TOOLS)

ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-tech.html qemu.1 qemu-img.1 qemu-nbd.8 QMP/qmp-commands.txt
else
DOCS=
endif

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory)
SUBDIR_DEVICES_MAK=$(patsubst %, %/config-devices.mak, $(TARGET_DIRS))
SUBDIR_DEVICES_MAK_DEP=$(patsubst %, %/config-devices.mak.d, $(TARGET_DIRS))

config-all-devices.mak: $(SUBDIR_DEVICES_MAK)
	$(call quiet-command,cat $(SUBDIR_DEVICES_MAK) | grep =y | sort -u > $@,"  GEN   $@")

-include $(SUBDIR_DEVICES_MAK_DEP)

%/config-devices.mak: default-configs/%.mak
	$(call quiet-command,$(SHELL) $(SRC_PATH)/scripts/make_device_config.sh $@ $<, "  GEN   $@")
	@if test -f $@; then \
	  if cmp -s $@.old $@; then \
	    mv $@.tmp $@; \
	    cp -p $@ $@.old; \
	  else \
	    if test -f $@.old; then \
	      echo "WARNING: $@ (user modified) out of date.";\
	    else \
	      echo "WARNING: $@ out of date.";\
	    fi; \
	    echo "Run \"make defconfig\" to regenerate."; \
	    rm $@.tmp; \
	  fi; \
	 else \
	  mv $@.tmp $@; \
	  cp -p $@ $@.old; \
	 fi

defconfig:
	rm -f config-all-devices.mak $(SUBDIR_DEVICES_MAK)

-include config-all-devices.mak

build-all: $(DOCS) $(TOOLS) recurse-all

config-host.h: config-host.h-timestamp
config-host.h-timestamp: config-host.mak
qemu-options.def: $(SRC_PATH)/qemu-options.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"  GEN   $@")

SUBDIR_RULES=$(patsubst %,subdir-%, $(TARGET_DIRS))

subdir-%: $(GENERATED_HEADERS)
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $* V="$(V)" TARGET_DIR="$*/" all,)

ifneq ($(wildcard config-host.mak),)
include $(SRC_PATH)/Makefile.objs
endif

$(common-obj-y): $(GENERATED_HEADERS)
subdir-libcacard: $(oslib-obj-y) $(trace-obj-y) qemu-timer-common.o

$(filter %-softmmu,$(SUBDIR_RULES)): $(trace-obj-y) $(common-obj-y) subdir-libdis

$(filter %-user,$(SUBDIR_RULES)): $(GENERATED_HEADERS) $(trace-obj-y) subdir-libdis-user subdir-libuser

ROMSUBDIR_RULES=$(patsubst %,romsubdir-%, $(ROMS))
romsubdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C pc-bios/$* V="$(V)" TARGET_DIR="$*/",)

ALL_SUBDIRS=$(TARGET_DIRS) $(patsubst %,pc-bios/%, $(ROMS))

recurse-all: $(SUBDIR_RULES) $(ROMSUBDIR_RULES)

audio/audio.o audio/fmodaudio.o: QEMU_CFLAGS += $(FMOD_CFLAGS)

QEMU_CFLAGS+=$(CURL_CFLAGS)

QEMU_CFLAGS+=$(GLIB_CFLAGS)

ui/cocoa.o: ui/cocoa.m

ui/sdl.o audio/sdlaudio.o ui/sdl_zoom.o baum.o: QEMU_CFLAGS += $(SDL_CFLAGS)

ui/vnc.o: QEMU_CFLAGS += $(VNC_TLS_CFLAGS)

bt-host.o: QEMU_CFLAGS += $(BLUEZ_CFLAGS)

version.o: $(SRC_PATH)/version.rc config-host.h
	$(call quiet-command,$(WINDRES) -I. -o $@ $<,"  RC    $(TARGET_DIR)$@")

version-obj-$(CONFIG_WIN32) += version.o
######################################################################
# Support building shared library libcacard

.PHONY: libcacard.la install-libcacard
ifeq ($(LIBTOOL),)
libcacard.la:
	@echo "libtool is missing, please install and rerun configure"; exit 1

install-libcacard:
	@echo "libtool is missing, please install and rerun configure"; exit 1
else
libcacard.la: $(GENERATED_HEADERS) $(oslib-obj-y) qemu-timer-common.o $(addsuffix .lo, $(basename $(trace-obj-y)))
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C libcacard V="$(V)" TARGET_DIR="$*/" libcacard.la,)

install-libcacard: libcacard.la
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C libcacard V="$(V)" TARGET_DIR="$*/" install-libcacard,)
endif
######################################################################

qemu-img.o: qemu-img-cmds.h
qemu-img.o qemu-tool.o qemu-nbd.o qemu-io.o cmd.o qemu-ga.o: $(GENERATED_HEADERS)

qemu-img$(EXESUF): qemu-img.o qemu-tool.o qemu-error.o $(oslib-obj-y) $(trace-obj-y) $(block-obj-y) $(qobject-obj-y) $(version-obj-y) qemu-timer-common.o

qemu-nbd$(EXESUF): qemu-nbd.o qemu-tool.o qemu-error.o $(oslib-obj-y) $(trace-obj-y) $(block-obj-y) $(qobject-obj-y) $(version-obj-y) qemu-timer-common.o

qemu-io$(EXESUF): qemu-io.o cmd.o qemu-tool.o qemu-error.o $(oslib-obj-y) $(trace-obj-y) $(block-obj-y) $(qobject-obj-y) $(version-obj-y) qemu-timer-common.o

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"  GEN   $@")

check-qint.o check-qstring.o check-qdict.o check-qlist.o check-qfloat.o check-qjson.o test-coroutine.o: $(GENERATED_HEADERS)

CHECK_PROG_DEPS = $(oslib-obj-y) $(trace-obj-y) qemu-tool.o

check-qint: check-qint.o qint.o $(CHECK_PROG_DEPS)
check-qstring: check-qstring.o qstring.o $(CHECK_PROG_DEPS)
check-qdict: check-qdict.o qdict.o qfloat.o qint.o qstring.o qbool.o qlist.o $(CHECK_PROG_DEPS)
check-qlist: check-qlist.o qlist.o qint.o $(CHECK_PROG_DEPS)
check-qfloat: check-qfloat.o qfloat.o $(CHECK_PROG_DEPS)
check-qjson: check-qjson.o qfloat.o qint.o qdict.o qstring.o qlist.o qbool.o qjson.o json-streamer.o json-lexer.o json-parser.o error.o qerror.o qemu-error.o $(CHECK_PROG_DEPS)
test-coroutine: test-coroutine.o qemu-timer-common.o async.o $(coroutine-obj-y) $(CHECK_PROG_DEPS)

$(qapi-obj-y): $(GENERATED_HEADERS)
qapi-dir := qapi-generated
test-visitor.o test-qmp-commands.o qemu-ga$(EXESUF): QEMU_CFLAGS += -I $(qapi-dir)
qemu-ga$(EXESUF): LIBS = $(LIBS_QGA)

$(qapi-dir)/test-qapi-types.c: $(qapi-dir)/test-qapi-types.h
$(qapi-dir)/test-qapi-types.h: $(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-types.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py -o "$(qapi-dir)" -p "test-" < $<, "  GEN   $@")
$(qapi-dir)/test-qapi-visit.c: $(qapi-dir)/test-qapi-visit.h
$(qapi-dir)/test-qapi-visit.h: $(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-visit.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py -o "$(qapi-dir)" -p "test-" < $<, "  GEN   $@")
$(qapi-dir)/test-qmp-commands.h: $(qapi-dir)/test-qmp-marshal.c
$(qapi-dir)/test-qmp-marshal.c: $(SRC_PATH)/qapi-schema-test.json $(SRC_PATH)/scripts/qapi-commands.py
	    $(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py -o "$(qapi-dir)" -p "test-" < $<, "  GEN   $@")

$(qapi-dir)/qga-qapi-types.c: $(qapi-dir)/qga-qapi-types.h
$(qapi-dir)/qga-qapi-types.h: $(SRC_PATH)/qapi-schema-guest.json $(SRC_PATH)/scripts/qapi-types.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py -o "$(qapi-dir)" -p "qga-" < $<, "  GEN   $@")
$(qapi-dir)/qga-qapi-visit.c: $(qapi-dir)/qga-qapi-visit.h
$(qapi-dir)/qga-qapi-visit.h: $(SRC_PATH)/qapi-schema-guest.json $(SRC_PATH)/scripts/qapi-visit.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py -o "$(qapi-dir)" -p "qga-" < $<, "  GEN   $@")
$(qapi-dir)/qga-qmp-marshal.c: $(SRC_PATH)/qapi-schema-guest.json $(SRC_PATH)/scripts/qapi-commands.py
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py -o "$(qapi-dir)" -p "qga-" < $<, "  GEN   $@")

test-visitor.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h) $(qapi-obj-y)
test-visitor: test-visitor.o qfloat.o qint.o qdict.o qstring.o qlist.o qbool.o $(qapi-obj-y) error.o osdep.o $(oslib-obj-y) qjson.o json-streamer.o json-lexer.o json-parser.o qerror.o qemu-error.o qemu-tool.o $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o

test-qmp-commands.o: $(addprefix $(qapi-dir)/, test-qapi-types.c test-qapi-types.h test-qapi-visit.c test-qapi-visit.h test-qmp-marshal.c test-qmp-commands.h) $(qapi-obj-y)
test-qmp-commands: test-qmp-commands.o qfloat.o qint.o qdict.o qstring.o qlist.o qbool.o $(qapi-obj-y) error.o osdep.o $(oslib-obj-y) qjson.o json-streamer.o json-lexer.o json-parser.o qerror.o qemu-error.o qemu-tool.o $(qapi-dir)/test-qapi-visit.o $(qapi-dir)/test-qapi-types.o $(qapi-dir)/test-qmp-marshal.o module.o

QGALIB_GEN=$(addprefix $(qapi-dir)/, qga-qapi-types.c qga-qapi-types.h qga-qapi-visit.c qga-qmp-marshal.c)
$(QGALIB_GEN): $(GENERATED_HEADERS)
$(qga-obj-y) qemu-ga.o: $(QGALIB_GEN)

qemu-ga$(EXESUF): qemu-ga.o $(qga-obj-y) $(qapi-obj-y) $(trace-obj-y) $(qobject-obj-y) $(version-obj-y) $(addprefix $(qapi-dir)/, qga-qapi-visit.o qga-qapi-types.o qga-qmp-marshal.o)

QEMULIBS=libhw32 libhw64 libuser libdis libdis-user

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f qemu-options.def
	rm -f *.o *.d *.a *.lo $(TOOLS) qemu-ga TAGS cscope.* *.pod *~ */*~
	rm -Rf .libs
	rm -f slirp/*.o slirp/*.d audio/*.o audio/*.d block/*.o block/*.d net/*.o net/*.d fsdev/*.o fsdev/*.d ui/*.o ui/*.d qapi/*.o qapi/*.d qga/*.o qga/*.d
	rm -f qemu-img-cmds.h
	rm -f trace/*.o trace/*.d
	rm -f trace.c trace.h trace.c-timestamp trace.h-timestamp
	rm -f trace-dtrace.dtrace trace-dtrace.dtrace-timestamp
	rm -f trace-dtrace.h trace-dtrace.h-timestamp
	rm -rf $(qapi-dir)
	$(MAKE) -C tests clean
	for d in $(ALL_SUBDIRS) $(QEMULIBS) libcacard; do \
	if test -d $$d; then $(MAKE) -C $$d $@ || exit 1; fi; \
	rm -f $$d/qemu-options.def; \
        done

distclean: clean
	rm -f config-host.mak config-host.h* config-host.ld $(DOCS) qemu-options.texi qemu-img-cmds.texi qemu-monitor.texi
	rm -f config-all-devices.mak
	rm -f roms/seabios/config.mak roms/vgabios/config.mak
	rm -f qemu-doc.info qemu-doc.aux qemu-doc.cp qemu-doc.cps qemu-doc.dvi
	rm -f qemu-doc.fn qemu-doc.fns qemu-doc.info qemu-doc.ky qemu-doc.kys
	rm -f qemu-doc.log qemu-doc.pdf qemu-doc.pg qemu-doc.toc qemu-doc.tp
	rm -f qemu-doc.vr
	rm -f config.log
	rm -f qemu-tech.info qemu-tech.aux qemu-tech.cp qemu-tech.dvi qemu-tech.fn qemu-tech.info qemu-tech.ky qemu-tech.log qemu-tech.pdf qemu-tech.pg qemu-tech.toc qemu-tech.tp qemu-tech.vr
	for d in $(TARGET_DIRS) $(QEMULIBS); do \
	rm -rf $$d || exit 1 ; \
        done

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  modifiers  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
common  de-ch  es     fo  fr-ca  hu     ja  mk  nl-be      pt  sl     tr

ifdef INSTALL_BLOBS
BLOBS=bios.bin vgabios.bin vgabios-cirrus.bin \
vgabios-stdvga.bin vgabios-vmware.bin vgabios-qxl.bin \
ppc_rom.bin openbios-sparc32 openbios-sparc64 openbios-ppc \
pxe-e1000.rom pxe-eepro100.rom pxe-ne2k_pci.rom \
pxe-pcnet.rom pxe-rtl8139.rom pxe-virtio.rom \
bamboo.dtb petalogix-s3adsp1800.dtb petalogix-ml605.dtb \
mpc8544ds.dtb \
multiboot.bin linuxboot.bin \
s390-zipl.rom \
spapr-rtas.bin slof.bin
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

install-sysconfig:
	$(INSTALL_DIR) "$(DESTDIR)$(sysconfdir)/qemu"
	$(INSTALL_DATA) $(SRC_PATH)/sysconfigs/target/target-x86_64.conf "$(DESTDIR)$(sysconfdir)/qemu"

install: all $(if $(BUILD_DOCS),install-doc) install-sysconfig
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
	find "$(SRC_PATH)" -name "*.[chsS]" -print | sed 's,^\./,,' > ./cscope.files
	cscope -b

# documentation
MAKEINFO=makeinfo
MAKEINFOFLAGS=--no-headers --no-split --number-sections
TEXIFLAG=$(if $(V),,--quiet)
%.dvi: %.texi
	$(call quiet-command,texi2dvi $(TEXIFLAG) -I . $<,"  GEN   $@")

%.html: %.texi
	$(call quiet-command,$(MAKEINFO) $(MAKEINFOFLAGS) --html $< -o $@, \
	"  GEN   $@")

%.info: %.texi
	$(call quiet-command,$(MAKEINFO) $< -o $@,"  GEN   $@")

%.pdf: %.texi
	$(call quiet-command,texi2pdf $(TEXIFLAG) -I . $<,"  GEN   $@")

qemu-options.texi: $(SRC_PATH)/qemu-options.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"  GEN   $@")

qemu-monitor.texi: $(SRC_PATH)/hmp-commands.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"  GEN   $@")

QMP/qmp-commands.txt: $(SRC_PATH)/qmp-commands.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -q < $< > $@,"  GEN   $@")

qemu-img-cmds.texi: $(SRC_PATH)/qemu-img-cmds.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"  GEN   $@")

qemu.1: qemu-doc.texi qemu-options.texi qemu-monitor.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/scripts/texi2pod.pl $< qemu.pod && \
	  pod2man --section=1 --center=" " --release=" " qemu.pod > $@, \
	  "  GEN   $@")

qemu-img.1: qemu-img.texi qemu-img-cmds.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/scripts/texi2pod.pl $< qemu-img.pod && \
	  pod2man --section=1 --center=" " --release=" " qemu-img.pod > $@, \
	  "  GEN   $@")

qemu-nbd.8: qemu-nbd.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/scripts/texi2pod.pl $< qemu-nbd.pod && \
	  pod2man --section=8 --center=" " --release=" " qemu-nbd.pod > $@, \
	  "  GEN   $@")

dvi: qemu-doc.dvi qemu-tech.dvi
html: qemu-doc.html qemu-tech.html
info: qemu-doc.info qemu-tech.info
pdf: qemu-doc.pdf qemu-tech.pdf

qemu-doc.dvi qemu-doc.html qemu-doc.info qemu-doc.pdf: \
	qemu-img.texi qemu-nbd.texi qemu-options.texi \
	qemu-monitor.texi qemu-img-cmds.texi

VERSION ?= $(shell cat VERSION)
FILE = qemu-$(VERSION)

# tar release (use 'make -k tar' on a checkouted tree)
tar:
	rm -rf /tmp/$(FILE)
	cp -r . /tmp/$(FILE)
	cd /tmp && tar zcvf ~/$(FILE).tar.gz $(FILE) --exclude CVS --exclude .git --exclude .svn
	rm -rf /tmp/$(FILE)

SYSTEM_TARGETS=$(filter %-softmmu,$(TARGET_DIRS))
SYSTEM_PROGS=$(patsubst %-softmmu,qemu-system-%, \
             $(SYSTEM_TARGETS))

USER_TARGETS=$(filter %-user,$(TARGET_DIRS))
USER_PROGS=$(patsubst %-bsd-user,qemu-%, \
           $(patsubst %-darwin-user,qemu-%, \
           $(patsubst %-linux-user,qemu-%, \
           $(USER_TARGETS))))

# generate a binary distribution
tarbin:
	cd / && tar zcvf ~/qemu-$(VERSION)-$(ARCH).tar.gz \
	$(patsubst %,$(bindir)/%, $(SYSTEM_PROGS)) \
	$(patsubst %,$(bindir)/%, $(USER_PROGS)) \
	$(bindir)/qemu-img \
	$(bindir)/qemu-nbd \
	$(datadir)/bios.bin \
	$(datadir)/vgabios.bin \
	$(datadir)/vgabios-cirrus.bin \
	$(datadir)/ppc_rom.bin \
	$(datadir)/openbios-sparc32 \
	$(datadir)/openbios-sparc64 \
	$(datadir)/openbios-ppc \
	$(datadir)/pxe-e1000.rom \
	$(datadir)/pxe-eepro100.rom \
	$(datadir)/pxe-ne2k_pci.rom \
	$(datadir)/pxe-pcnet.rom \
	$(datadir)/pxe-rtl8139.rom \
	$(datadir)/pxe-virtio.rom \
	$(docdir)/qemu-doc.html \
	$(docdir)/qemu-tech.html \
	$(mandir)/man1/qemu.1 \
	$(mandir)/man1/qemu-img.1 \
	$(mandir)/man8/qemu-nbd.8

# Include automatically generated dependency files
-include $(wildcard *.d audio/*.d slirp/*.d block/*.d net/*.d ui/*.d qapi/*.d qga/*.d)
