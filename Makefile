# Makefile for QEMU.

# Always point to the root of the build tree (needs GNU make).
BUILD_DIR=$(CURDIR)

# All following code might depend on configuration variables
ifneq ($(wildcard config-host.mak),)
# Put the all: rule here so that config-host.mak can contain dependencies.
all:
include config-host.mak

# Check that we're not trying to do an out-of-tree build from
# a tree that's been used for an in-tree build.
ifneq ($(realpath $(SRC_PATH)),$(realpath .))
ifneq ($(wildcard $(SRC_PATH)/config-host.mak),)
$(error This is an out of tree build but your source tree ($(SRC_PATH)) \
seems to have been used for an in-tree build. You can fix this by running \
"make distclean && rm -rf *-linux-user *-softmmu" in your source tree)
endif
endif

include $(SRC_PATH)/rules.mak
config-host.mak: $(SRC_PATH)/configure
	@echo $@ is out-of-date, running configure
	@sed -n "/.*Configured with/s/[^:]*: //p" $@ | sh
else
config-host.mak:
ifneq ($(filter-out %clean,$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
	@echo "Please call configure before running make!"
	@exit 1
endif
endif

GENERATED_HEADERS = config-host.h trace.h qemu-options.def
ifeq ($(TRACE_BACKEND),dtrace)
GENERATED_HEADERS += trace-dtrace.h
endif
GENERATED_HEADERS += qmp-commands.h qapi-types.h qapi-visit.h
GENERATED_SOURCES += qmp-marshal.c qapi-types.c qapi-visit.c trace.c

# Don't try to regenerate Makefile or configure
# We don't generate any of them
Makefile: ;
configure: ;

.PHONY: all clean cscope distclean dvi html info install install-doc \
	pdf recurse-all speed test dist

$(call set-vpath, $(SRC_PATH))

LIBS+=-lz $(LIBS_TOOLS)

HELPERS-$(CONFIG_LINUX) = qemu-bridge-helper$(EXESUF)

ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-tech.html qemu.1 qemu-img.1 qemu-nbd.8 QMP/qmp-commands.txt
ifdef CONFIG_VIRTFS
DOCS+=fsdev/virtfs-proxy-helper.1
endif
else
DOCS=
endif

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory) BUILD_DIR=$(BUILD_DIR)
SUBDIR_DEVICES_MAK=$(patsubst %, %/config-devices.mak, $(TARGET_DIRS))
SUBDIR_DEVICES_MAK_DEP=$(patsubst %, %/config-devices.mak.d, $(TARGET_DIRS))

ifeq ($(SUBDIR_DEVICES_MAK),)
config-all-devices.mak:
	$(call quiet-command,echo '# no devices' > $@,"  GEN   $@")
else
config-all-devices.mak: $(SUBDIR_DEVICES_MAK)
	$(call quiet-command,cat $(SUBDIR_DEVICES_MAK) | grep =y | sort -u > $@,"  GEN   $@")
endif

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

all: $(DOCS) $(TOOLS) $(HELPERS-y) recurse-all

config-host.h: config-host.h-timestamp
config-host.h-timestamp: config-host.mak
qemu-options.def: $(SRC_PATH)/qemu-options.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"  GEN   $@")

SUBDIR_RULES=$(patsubst %,subdir-%, $(TARGET_DIRS))

subdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $* V="$(V)" TARGET_DIR="$*/" all,)

ifneq ($(wildcard config-host.mak),)
include $(SRC_PATH)/Makefile.objs
endif

subdir-libcacard: $(oslib-obj-y) $(trace-obj-y) qemu-timer-common.o

subdir-pixman: pixman/Makefile
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C pixman V="$(V)" all,)

pixman/Makefile: $(SRC_PATH)/pixman/configure
	(cd pixman; CFLAGS="$(CFLAGS) -fPIC" $(SRC_PATH)/pixman/configure $(AUTOCONF_HOST) --disable-gtk --disable-shared --enable-static)

$(SRC_PATH)/pixman/configure:
	(cd $(SRC_PATH)/pixman; autoreconf -v --install)

$(SUBDIR_RULES): libqemustub.a

$(filter %-softmmu,$(SUBDIR_RULES)): $(universal-obj-y) $(trace-obj-y) $(common-obj-y) $(extra-obj-y) subdir-libdis

$(filter %-user,$(SUBDIR_RULES)): $(universal-obj-y) $(trace-obj-y) subdir-libdis-user subdir-libuser

ROMSUBDIR_RULES=$(patsubst %,romsubdir-%, $(ROMS))
romsubdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C pc-bios/$* V="$(V)" TARGET_DIR="$*/",)

ALL_SUBDIRS=$(TARGET_DIRS) $(patsubst %,pc-bios/%, $(ROMS))

recurse-all: $(SUBDIR_RULES) $(ROMSUBDIR_RULES)

audio/audio.o audio/fmodaudio.o: QEMU_CFLAGS += $(FMOD_CFLAGS)

QEMU_CFLAGS+=$(CURL_CFLAGS)

QEMU_CFLAGS += -I$(SRC_PATH)/include

ui/cocoa.o: ui/cocoa.m

ui/sdl.o audio/sdlaudio.o ui/sdl_zoom.o hw/baum.o: QEMU_CFLAGS += $(SDL_CFLAGS)

ui/vnc.o: QEMU_CFLAGS += $(VNC_TLS_CFLAGS)

bt-host.o: QEMU_CFLAGS += $(BLUEZ_CFLAGS)

version.o: $(SRC_PATH)/version.rc config-host.h
	$(call quiet-command,$(WINDRES) -I. -o $@ $<,"  RC    $(TARGET_DIR)$@")

version-obj-$(CONFIG_WIN32) += version.o

######################################################################
# Build library with stubs

libqemustub.a: $(stub-obj-y)

######################################################################
# Support building shared library libcacard

.PHONY: libcacard.la install-libcacard
ifeq ($(LIBTOOL),)
libcacard.la:
	@echo "libtool is missing, please install and rerun configure"; exit 1

install-libcacard:
	@echo "libtool is missing, please install and rerun configure"; exit 1
else
libcacard.la: $(oslib-obj-y) qemu-timer-common.o $(addsuffix .lo, $(basename $(trace-obj-y)))
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C libcacard V="$(V)" TARGET_DIR="$*/" libcacard.la,)

install-libcacard: libcacard.la
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C libcacard V="$(V)" TARGET_DIR="$*/" install-libcacard,)
endif

######################################################################

qemu-img.o: qemu-img-cmds.h

tools-obj-y = $(oslib-obj-y) $(trace-obj-y) qemu-tool.o qemu-timer.o \
	main-loop.o iohandler.o error.o
tools-obj-$(CONFIG_POSIX) += compatfd.o

qemu-img$(EXESUF): qemu-img.o $(tools-obj-y) $(block-obj-y) libqemustub.a
qemu-nbd$(EXESUF): qemu-nbd.o $(tools-obj-y) $(block-obj-y) libqemustub.a
qemu-io$(EXESUF): qemu-io.o cmd.o $(tools-obj-y) $(block-obj-y) libqemustub.a

qemu-bridge-helper$(EXESUF): qemu-bridge-helper.o

vscclient$(EXESUF): $(libcacard-y) $(oslib-obj-y) $(trace-obj-y) libcacard/vscclient.o libqemustub.a
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $^ $(libcacard_libs) $(LIBS),"  LINK  $@")

fsdev/virtfs-proxy-helper$(EXESUF): fsdev/virtfs-proxy-helper.o fsdev/virtio-9p-marshal.o oslib-posix.o $(trace-obj-y)
fsdev/virtfs-proxy-helper$(EXESUF): LIBS += -lcap

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"  GEN   $@")

qemu-ga$(EXESUF): LIBS = $(LIBS_QGA)
qemu-ga$(EXESUF): QEMU_CFLAGS += -I qga/qapi-generated

gen-out-type = $(subst .,-,$(suffix $@))

ifneq ($(wildcard config-host.mak),)
include $(SRC_PATH)/tests/Makefile
endif

qapi-py = $(SRC_PATH)/scripts/qapi.py $(SRC_PATH)/scripts/ordereddict.py

qga/qapi-generated/qga-qapi-types.c qga/qapi-generated/qga-qapi-types.h :\
$(SRC_PATH)/qapi-schema-guest.json $(SRC_PATH)/scripts/qapi-types.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py $(gen-out-type) -o qga/qapi-generated -p "qga-" < $<, "  GEN   $@")
qga/qapi-generated/qga-qapi-visit.c qga/qapi-generated/qga-qapi-visit.h :\
$(SRC_PATH)/qapi-schema-guest.json $(SRC_PATH)/scripts/qapi-visit.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py $(gen-out-type) -o qga/qapi-generated -p "qga-" < $<, "  GEN   $@")
qga/qapi-generated/qga-qmp-commands.h qga/qapi-generated/qga-qmp-marshal.c :\
$(SRC_PATH)/qapi-schema-guest.json $(SRC_PATH)/scripts/qapi-commands.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py $(gen-out-type) -o qga/qapi-generated -p "qga-" < $<, "  GEN   $@")

qapi-types.c qapi-types.h :\
$(SRC_PATH)/qapi-schema.json $(SRC_PATH)/scripts/qapi-types.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py $(gen-out-type) -o "." < $<, "  GEN   $@")
qapi-visit.c qapi-visit.h :\
$(SRC_PATH)/qapi-schema.json $(SRC_PATH)/scripts/qapi-visit.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py $(gen-out-type) -o "."  < $<, "  GEN   $@")
qmp-commands.h qmp-marshal.c :\
$(SRC_PATH)/qapi-schema.json $(SRC_PATH)/scripts/qapi-commands.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py $(gen-out-type) -m -o "." < $<, "  GEN   $@")

QGALIB_GEN=$(addprefix qga/qapi-generated/, qga-qapi-types.h qga-qapi-visit.h qga-qmp-commands.h)
$(qga-obj-y) qemu-ga.o: $(QGALIB_GEN)

qemu-ga$(EXESUF): qemu-ga.o $(qga-obj-y) $(oslib-obj-y) $(trace-obj-y) $(qapi-obj-y) $(qobject-obj-y) $(version-obj-y) libqemustub.a

QEMULIBS=libuser libdis libdis-user

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f qemu-options.def
	find . -name '*.[od]' -exec rm -f {} +
	rm -f *.a *.lo $(TOOLS) $(HELPERS-y) qemu-ga TAGS cscope.* *.pod *~ */*~
	rm -Rf .libs
	rm -f qemu-img-cmds.h
	rm -f trace-dtrace.dtrace trace-dtrace.dtrace-timestamp
	@# May not be present in GENERATED_HEADERS
	rm -f trace-dtrace.h trace-dtrace.h-timestamp
	rm -f $(foreach f,$(GENERATED_HEADERS),$(f) $(f)-timestamp)
	rm -f $(foreach f,$(GENERATED_SOURCES),$(f) $(f)-timestamp)
	rm -rf qapi-generated
	rm -rf qga/qapi-generated
	$(MAKE) -C tests/tcg clean
	for d in $(ALL_SUBDIRS) $(QEMULIBS) libcacard; do \
	if test -d $$d; then $(MAKE) -C $$d $@ || exit 1; fi; \
	rm -f $$d/qemu-options.def; \
        done

VERSION ?= $(shell cat VERSION)

dist: qemu-$(VERSION).tar.bz2

qemu-%.tar.bz2:
	$(SRC_PATH)/scripts/make-release "$(SRC_PATH)" "$(patsubst qemu-%.tar.bz2,%,$@)"

distclean: clean
	rm -f config-host.mak config-host.h* config-host.ld $(DOCS) qemu-options.texi qemu-img-cmds.texi qemu-monitor.texi
	rm -f config-all-devices.mak
	rm -f roms/seabios/config.mak roms/vgabios/config.mak
	rm -f qemu-doc.info qemu-doc.aux qemu-doc.cp qemu-doc.cps qemu-doc.dvi
	rm -f qemu-doc.fn qemu-doc.fns qemu-doc.info qemu-doc.ky qemu-doc.kys
	rm -f qemu-doc.log qemu-doc.pdf qemu-doc.pg qemu-doc.toc qemu-doc.tp
	rm -f qemu-doc.vr
	rm -f config.log
	rm -f linux-headers/asm
	rm -f qemu-tech.info qemu-tech.aux qemu-tech.cp qemu-tech.dvi qemu-tech.fn qemu-tech.info qemu-tech.ky qemu-tech.log qemu-tech.pdf qemu-tech.pg qemu-tech.toc qemu-tech.tp qemu-tech.vr
	for d in $(TARGET_DIRS) $(QEMULIBS); do \
	rm -rf $$d || exit 1 ; \
        done
	test -f pixman/config.log && make -C pixman distclean

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  modifiers  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
common  de-ch  es     fo  fr-ca  hu     ja  mk  nl-be      pt  sl     tr \
bepo

ifdef INSTALL_BLOBS
BLOBS=bios.bin sgabios.bin vgabios.bin vgabios-cirrus.bin \
vgabios-stdvga.bin vgabios-vmware.bin vgabios-qxl.bin \
ppc_rom.bin openbios-sparc32 openbios-sparc64 openbios-ppc \
pxe-e1000.rom pxe-eepro100.rom pxe-ne2k_pci.rom \
pxe-pcnet.rom pxe-rtl8139.rom pxe-virtio.rom \
qemu-icon.bmp \
bamboo.dtb petalogix-s3adsp1800.dtb petalogix-ml605.dtb \
multiboot.bin linuxboot.bin kvmvapic.bin \
s390-zipl.rom \
spapr-rtas.bin slof.bin \
palcode-clipper
else
BLOBS=
endif

install-doc: $(DOCS)
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) qemu-doc.html  qemu-tech.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) QMP/qmp-commands.txt "$(DESTDIR)$(qemu_docdir)"
ifdef CONFIG_POSIX
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) qemu.1 qemu-img.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) qemu-nbd.8 "$(DESTDIR)$(mandir)/man8"
endif
ifdef CONFIG_VIRTFS
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) fsdev/virtfs-proxy-helper.1 "$(DESTDIR)$(mandir)/man1"
endif

install-datadir:
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)"

install-confdir:
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_confdir)"

install-sysconfig: install-datadir install-confdir
	$(INSTALL_DATA) $(SRC_PATH)/sysconfigs/target/target-x86_64.conf "$(DESTDIR)$(qemu_confdir)"

install: all $(if $(BUILD_DOCS),install-doc) install-sysconfig install-datadir
	$(INSTALL_DIR) "$(DESTDIR)$(bindir)"
ifneq ($(TOOLS),)
	$(INSTALL_PROG) $(STRIP_OPT) $(TOOLS) "$(DESTDIR)$(bindir)"
endif
ifneq ($(HELPERS-y),)
	$(INSTALL_DIR) "$(DESTDIR)$(libexecdir)"
	$(INSTALL_PROG) $(STRIP_OPT) $(HELPERS-y) "$(DESTDIR)$(libexecdir)"
endif
ifneq ($(BLOBS),)
	set -e; for x in $(BLOBS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/$$x "$(DESTDIR)$(qemu_datadir)"; \
	done
endif
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)/keymaps"
	set -e; for x in $(KEYMAPS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/keymaps/$$x "$(DESTDIR)$(qemu_datadir)/keymaps"; \
	done
	for d in $(TARGET_DIRS); do \
	$(MAKE) -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed: all
	$(MAKE) -C tests/tcg $@

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
	$(call quiet-command,LC_ALL=C $(MAKEINFO) $(MAKEINFOFLAGS) --html $< -o $@, \
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
	  $(POD2MAN) --section=1 --center=" " --release=" " qemu.pod > $@, \
	  "  GEN   $@")

qemu-img.1: qemu-img.texi qemu-img-cmds.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/scripts/texi2pod.pl $< qemu-img.pod && \
	  $(POD2MAN) --section=1 --center=" " --release=" " qemu-img.pod > $@, \
	  "  GEN   $@")

fsdev/virtfs-proxy-helper.1: fsdev/virtfs-proxy-helper.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/scripts/texi2pod.pl $< fsdev/virtfs-proxy-helper.pod && \
	  $(POD2MAN) --section=1 --center=" " --release=" " fsdev/virtfs-proxy-helper.pod > $@, \
	  "  GEN   $@")

qemu-nbd.8: qemu-nbd.texi
	$(call quiet-command, \
	  perl -Ww -- $(SRC_PATH)/scripts/texi2pod.pl $< qemu-nbd.pod && \
	  $(POD2MAN) --section=8 --center=" " --release=" " qemu-nbd.pod > $@, \
	  "  GEN   $@")

dvi: qemu-doc.dvi qemu-tech.dvi
html: qemu-doc.html qemu-tech.html
info: qemu-doc.info qemu-tech.info
pdf: qemu-doc.pdf qemu-tech.pdf

qemu-doc.dvi qemu-doc.html qemu-doc.info qemu-doc.pdf: \
	qemu-img.texi qemu-nbd.texi qemu-options.texi \
	qemu-monitor.texi qemu-img-cmds.texi

# Add a dependency on the generated files, so that they are always
# rebuilt before other object files
ifneq ($(filter-out %clean,$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
Makefile: $(GENERATED_HEADERS)
endif

# Include automatically generated dependency files
# Dependencies in Makefile.objs files come from our recursive subdir rules
-include $(wildcard *.d tests/*.d)
