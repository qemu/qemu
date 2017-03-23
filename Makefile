# Makefile for QEMU.

# Always point to the root of the build tree (needs GNU make).
BUILD_DIR=$(CURDIR)

# Before including a proper config-host.mak, assume we are in the source tree
SRC_PATH=.

UNCHECKED_GOALS := %clean TAGS cscope ctags docker docker-%

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

CONFIG_SOFTMMU := $(if $(filter %-softmmu,$(TARGET_DIRS)),y)
CONFIG_USER_ONLY := $(if $(filter %-user,$(TARGET_DIRS)),y)
CONFIG_XEN := $(CONFIG_XEN_BACKEND)
CONFIG_ALL=y
-include config-all-devices.mak
-include config-all-disas.mak

config-host.mak: $(SRC_PATH)/configure $(SRC_PATH)/pc-bios
	@echo $@ is out-of-date, running configure
	@# TODO: The next lines include code which supports a smooth
	@# transition from old configurations without config.status.
	@# This code can be removed after QEMU 1.7.
	@if test -x config.status; then \
	    ./config.status; \
        else \
	    sed -n "/.*Configured with/s/[^:]*: //p" $@ | sh; \
	fi
else
config-host.mak:
ifneq ($(filter-out $(UNCHECKED_GOALS),$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
	@echo "Please call configure before running make!"
	@exit 1
endif
endif

include $(SRC_PATH)/rules.mak

GENERATED_FILES = qemu-version.h config-host.h qemu-options.def
GENERATED_FILES += qmp-commands.h qapi-types.h qapi-visit.h qapi-event.h
GENERATED_FILES += qmp-marshal.c qapi-types.c qapi-visit.c qapi-event.c
GENERATED_FILES += qmp-introspect.h
GENERATED_FILES += qmp-introspect.c

GENERATED_FILES += trace/generated-tcg-tracers.h

GENERATED_FILES += trace/generated-helpers-wrappers.h
GENERATED_FILES += trace/generated-helpers.h
GENERATED_FILES += trace/generated-helpers.c

ifdef CONFIG_TRACE_UST
GENERATED_FILES += trace-ust-all.h
GENERATED_FILES += trace-ust-all.c
endif

GENERATED_FILES += module_block.h

TRACE_HEADERS = trace-root.h $(trace-events-subdirs:%=%/trace.h)
TRACE_SOURCES = trace-root.c $(trace-events-subdirs:%=%/trace.c)
TRACE_DTRACE =
ifdef CONFIG_TRACE_DTRACE
TRACE_HEADERS += trace-dtrace-root.h $(trace-events-subdirs:%=%/trace-dtrace.h)
TRACE_DTRACE += trace-dtrace-root.dtrace $(trace-events-subdirs:%=%/trace-dtrace.dtrace)
endif
ifdef CONFIG_TRACE_UST
TRACE_HEADERS += trace-ust-root.h $(trace-events-subdirs:%=%/trace-ust.h)
endif

GENERATED_FILES += $(TRACE_HEADERS)
GENERATED_FILES += $(TRACE_SOURCES)
GENERATED_FILES += $(BUILD_DIR)/trace-events-all

trace-group-name = $(shell dirname $1 | sed -e 's/[^a-zA-Z0-9]/_/g')

tracetool-y = $(SRC_PATH)/scripts/tracetool.py
tracetool-y += $(shell find $(SRC_PATH)/scripts/tracetool -name "*.py")

%/trace.h: %/trace.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace.h-timestamp: $(SRC_PATH)/%/trace-events $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=$(call trace-group-name,$@) \
		--format=h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

%/trace.c: %/trace.c-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace.c-timestamp: $(SRC_PATH)/%/trace-events $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=$(call trace-group-name,$@) \
		--format=c \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

%/trace-ust.h: %/trace-ust.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace-ust.h-timestamp: $(SRC_PATH)/%/trace-events $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=$(call trace-group-name,$@) \
		--format=ust-events-h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

%/trace-dtrace.dtrace: %/trace-dtrace.dtrace-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace-dtrace.dtrace-timestamp: $(SRC_PATH)/%/trace-events $(BUILD_DIR)/config-host.mak $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=$(call trace-group-name,$@) \
		--format=d \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

%/trace-dtrace.h: %/trace-dtrace.dtrace $(tracetool-y)
	$(call quiet-command,dtrace -o $@ -h -s $<, "GEN","$@")

%/trace-dtrace.o: %/trace-dtrace.dtrace $(tracetool-y)


trace-root.h: trace-root.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-root.h-timestamp: $(SRC_PATH)/trace-events $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-root.c: trace-root.c-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-root.c-timestamp: $(SRC_PATH)/trace-events $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=c \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-ust-root.h: trace-ust-root.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-ust-root.h-timestamp: $(SRC_PATH)/trace-events $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=ust-events-h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-ust-all.h: trace-ust-all.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-ust-all.h-timestamp: $(trace-events-files) $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=all \
		--format=ust-events-h \
		--backends=$(TRACE_BACKENDS) \
		$(trace-events-files) > $@,"GEN","$(@:%-timestamp=%)")

trace-ust-all.c: trace-ust-all.c-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-ust-all.c-timestamp: $(trace-events-files) $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=all \
		--format=ust-events-c \
		--backends=$(TRACE_BACKENDS) \
		$(trace-events-files) > $@,"GEN","$(@:%-timestamp=%)")

trace-dtrace-root.dtrace: trace-dtrace-root.dtrace-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-dtrace-root.dtrace-timestamp: $(SRC_PATH)/trace-events $(BUILD_DIR)/config-host.mak $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=d \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-dtrace-root.h: trace-dtrace-root.dtrace
	$(call quiet-command,dtrace -o $@ -h -s $<, "GEN","$@")

trace-dtrace-root.o: trace-dtrace-root.dtrace

# Don't try to regenerate Makefile or configure
# We don't generate any of them
Makefile: ;
configure: ;

.PHONY: all clean cscope distclean html info install install-doc \
	pdf txt recurse-all speed test dist msi FORCE

$(call set-vpath, $(SRC_PATH))

LIBS+=-lz $(LIBS_TOOLS)

HELPERS-$(CONFIG_LINUX) = qemu-bridge-helper$(EXESUF)

ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-doc.txt qemu.1 qemu-img.1 qemu-nbd.8 qemu-ga.8
DOCS+=docs/qemu-qmp-ref.html docs/qemu-qmp-ref.txt docs/qemu-qmp-ref.7
DOCS+=docs/qemu-ga-ref.html docs/qemu-ga-ref.txt docs/qemu-ga-ref.7
ifdef CONFIG_VIRTFS
DOCS+=fsdev/virtfs-proxy-helper.1
endif
else
DOCS=
endif

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory) BUILD_DIR=$(BUILD_DIR)
SUBDIR_DEVICES_MAK=$(patsubst %, %/config-devices.mak, $(TARGET_DIRS))
SUBDIR_DEVICES_MAK_DEP=$(patsubst %, %-config-devices.mak.d, $(TARGET_DIRS))

ifeq ($(SUBDIR_DEVICES_MAK),)
config-all-devices.mak:
	$(call quiet-command,echo '# no devices' > $@,"GEN","$@")
else
config-all-devices.mak: $(SUBDIR_DEVICES_MAK)
	$(call quiet-command, sed -n \
             's|^\([^=]*\)=\(.*\)$$|\1:=$$(findstring y,$$(\1)\2)|p' \
             $(SUBDIR_DEVICES_MAK) | sort -u > $@, \
             "GEN","$@")
endif

-include $(SUBDIR_DEVICES_MAK_DEP)

%/config-devices.mak: default-configs/%.mak $(SRC_PATH)/scripts/make_device_config.sh
	$(call quiet-command, \
            $(SHELL) $(SRC_PATH)/scripts/make_device_config.sh $< $*-config-devices.mak.d $@ > $@.tmp,"GEN","$@.tmp")
	$(call quiet-command, if test -f $@; then \
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
	 fi,"GEN","$@");

defconfig:
	rm -f config-all-devices.mak $(SUBDIR_DEVICES_MAK)

ifneq ($(wildcard config-host.mak),)
include $(SRC_PATH)/Makefile.objs
endif

dummy := $(call unnest-vars,, \
                stub-obj-y \
                chardev-obj-y \
                util-obj-y \
                qga-obj-y \
                ivshmem-client-obj-y \
                ivshmem-server-obj-y \
                libvhost-user-obj-y \
                qga-vss-dll-obj-y \
                block-obj-y \
                block-obj-m \
                crypto-obj-y \
                crypto-aes-obj-y \
                qom-obj-y \
                io-obj-y \
                common-obj-y \
                common-obj-m \
                trace-obj-y)

ifneq ($(wildcard config-host.mak),)
include $(SRC_PATH)/tests/Makefile.include
endif

all: $(DOCS) $(TOOLS) $(HELPERS-y) recurse-all modules

qemu-version.h: FORCE
	$(call quiet-command, \
		(cd $(SRC_PATH); \
		printf '#define QEMU_PKGVERSION '; \
		if test -n "$(PKGVERSION)"; then \
			printf '"$(PKGVERSION)"\n'; \
		else \
			if test -d .git; then \
				printf '" ('; \
				git describe --match 'v*' 2>/dev/null | tr -d '\n'; \
				if ! git diff-index --quiet HEAD &>/dev/null; then \
					printf -- '-dirty'; \
				fi; \
				printf ')"\n'; \
			else \
				printf '""\n'; \
			fi; \
		fi) > $@.tmp)
	$(call quiet-command, if ! cmp -s $@ $@.tmp; then \
	  mv $@.tmp $@; \
	 else \
	  rm $@.tmp; \
	 fi)

config-host.h: config-host.h-timestamp
config-host.h-timestamp: config-host.mak
qemu-options.def: $(SRC_PATH)/qemu-options.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$@")

SUBDIR_RULES=$(patsubst %,subdir-%, $(TARGET_DIRS))
SOFTMMU_SUBDIR_RULES=$(filter %-softmmu,$(SUBDIR_RULES))

$(SOFTMMU_SUBDIR_RULES): $(block-obj-y)
$(SOFTMMU_SUBDIR_RULES): $(crypto-obj-y)
$(SOFTMMU_SUBDIR_RULES): $(io-obj-y)
$(SOFTMMU_SUBDIR_RULES): config-all-devices.mak

subdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $* V="$(V)" TARGET_DIR="$*/" all,)

subdir-pixman: pixman/Makefile
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C pixman V="$(V)" all,)

pixman/Makefile: $(SRC_PATH)/pixman/configure
	(cd pixman; CFLAGS="$(CFLAGS) -fPIC $(extra_cflags) $(extra_ldflags)" $(SRC_PATH)/pixman/configure $(AUTOCONF_HOST) --disable-gtk --disable-shared --enable-static)

$(SRC_PATH)/pixman/configure:
	(cd $(SRC_PATH)/pixman; autoreconf -v --install)

DTC_MAKE_ARGS=-I$(SRC_PATH)/dtc VPATH=$(SRC_PATH)/dtc -C dtc V="$(V)" LIBFDT_srcdir=$(SRC_PATH)/dtc/libfdt
DTC_CFLAGS=$(CFLAGS) $(QEMU_CFLAGS)
DTC_CPPFLAGS=-I$(BUILD_DIR)/dtc -I$(SRC_PATH)/dtc -I$(SRC_PATH)/dtc/libfdt

subdir-dtc:dtc/libfdt dtc/tests
	$(call quiet-command,$(MAKE) $(DTC_MAKE_ARGS) CPPFLAGS="$(DTC_CPPFLAGS)" CFLAGS="$(DTC_CFLAGS)" LDFLAGS="$(LDFLAGS)" ARFLAGS="$(ARFLAGS)" CC="$(CC)" AR="$(AR)" LD="$(LD)" $(SUBDIR_MAKEFLAGS) libfdt/libfdt.a,)

dtc/%:
	mkdir -p $@

$(SUBDIR_RULES): libqemuutil.a libqemustub.a $(common-obj-y) $(chardev-obj-y) \
	$(qom-obj-y) $(crypto-aes-obj-$(CONFIG_USER_ONLY)) $(trace-obj-y)

ROMSUBDIR_RULES=$(patsubst %,romsubdir-%, $(ROMS))
# Only keep -O and -g cflags
romsubdir-%:
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C pc-bios/$* V="$(V)" TARGET_DIR="$*/" CFLAGS="$(filter -O% -g%,$(CFLAGS))",)

ALL_SUBDIRS=$(TARGET_DIRS) $(patsubst %,pc-bios/%, $(ROMS))

recurse-all: $(SUBDIR_RULES) $(ROMSUBDIR_RULES)

$(BUILD_DIR)/version.o: $(SRC_PATH)/version.rc config-host.h
	$(call quiet-command,$(WINDRES) -I$(BUILD_DIR) -o $@ $<,"RC","version.o")

Makefile: $(version-obj-y)

######################################################################
# Build libraries

libqemustub.a: $(stub-obj-y)
libqemuutil.a: $(util-obj-y)

######################################################################

COMMON_LDADDS = $(trace-obj-y) libqemuutil.a libqemustub.a

qemu-img.o: qemu-img-cmds.h

qemu-img$(EXESUF): qemu-img.o $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-nbd$(EXESUF): qemu-nbd.o $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-io$(EXESUF): qemu-io.o $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)

qemu-bridge-helper$(EXESUF): qemu-bridge-helper.o $(COMMON_LDADDS)

fsdev/virtfs-proxy-helper$(EXESUF): fsdev/virtfs-proxy-helper.o fsdev/9p-marshal.o fsdev/9p-iov-marshal.o $(COMMON_LDADDS)
fsdev/virtfs-proxy-helper$(EXESUF): LIBS += -lcap

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$@")

qemu-ga$(EXESUF): LIBS = $(LIBS_QGA)
qemu-ga$(EXESUF): QEMU_CFLAGS += -I qga/qapi-generated

gen-out-type = $(subst .,-,$(suffix $@))

qapi-py = $(SRC_PATH)/scripts/qapi.py $(SRC_PATH)/scripts/ordereddict.py

qga/qapi-generated/qga-qapi-types.c qga/qapi-generated/qga-qapi-types.h :\
$(SRC_PATH)/qga/qapi-schema.json $(SRC_PATH)/scripts/qapi-types.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py \
		$(gen-out-type) -o qga/qapi-generated -p "qga-" $<, \
		"GEN","$@")
qga/qapi-generated/qga-qapi-visit.c qga/qapi-generated/qga-qapi-visit.h :\
$(SRC_PATH)/qga/qapi-schema.json $(SRC_PATH)/scripts/qapi-visit.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py \
		$(gen-out-type) -o qga/qapi-generated -p "qga-" $<, \
		"GEN","$@")
qga/qapi-generated/qga-qmp-commands.h qga/qapi-generated/qga-qmp-marshal.c :\
$(SRC_PATH)/qga/qapi-schema.json $(SRC_PATH)/scripts/qapi-commands.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py \
		$(gen-out-type) -o qga/qapi-generated -p "qga-" $<, \
		"GEN","$@")

qapi-modules = $(SRC_PATH)/qapi-schema.json $(SRC_PATH)/qapi/common.json \
               $(SRC_PATH)/qapi/block.json $(SRC_PATH)/qapi/block-core.json \
               $(SRC_PATH)/qapi/event.json $(SRC_PATH)/qapi/introspect.json \
               $(SRC_PATH)/qapi/crypto.json $(SRC_PATH)/qapi/rocker.json \
               $(SRC_PATH)/qapi/trace.json

qapi-types.c qapi-types.h :\
$(qapi-modules) $(SRC_PATH)/scripts/qapi-types.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-types.py \
		$(gen-out-type) -o "." -b $<, \
		"GEN","$@")
qapi-visit.c qapi-visit.h :\
$(qapi-modules) $(SRC_PATH)/scripts/qapi-visit.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-visit.py \
		$(gen-out-type) -o "." -b $<, \
		"GEN","$@")
qapi-event.c qapi-event.h :\
$(qapi-modules) $(SRC_PATH)/scripts/qapi-event.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-event.py \
		$(gen-out-type) -o "." $<, \
		"GEN","$@")
qmp-commands.h qmp-marshal.c :\
$(qapi-modules) $(SRC_PATH)/scripts/qapi-commands.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-commands.py \
		$(gen-out-type) -o "." $<, \
		"GEN","$@")
qmp-introspect.h qmp-introspect.c :\
$(qapi-modules) $(SRC_PATH)/scripts/qapi-introspect.py $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-introspect.py \
		$(gen-out-type) -o "." $<, \
		"GEN","$@")

QGALIB_GEN=$(addprefix qga/qapi-generated/, qga-qapi-types.h qga-qapi-visit.h qga-qmp-commands.h)
$(qga-obj-y) qemu-ga.o: $(QGALIB_GEN)

qemu-ga$(EXESUF): $(qga-obj-y) $(COMMON_LDADDS)
	$(call LINK, $^)

ifdef QEMU_GA_MSI_ENABLED
QEMU_GA_MSI=qemu-ga-$(ARCH).msi

msi: $(QEMU_GA_MSI)

$(QEMU_GA_MSI): qemu-ga.exe $(QGA_VSS_PROVIDER)

$(QEMU_GA_MSI): config-host.mak

$(QEMU_GA_MSI):  $(SRC_PATH)/qga/installer/qemu-ga.wxs
	$(call quiet-command,QEMU_GA_VERSION="$(QEMU_GA_VERSION)" QEMU_GA_MANUFACTURER="$(QEMU_GA_MANUFACTURER)" QEMU_GA_DISTRO="$(QEMU_GA_DISTRO)" BUILD_DIR="$(BUILD_DIR)" \
	wixl -o $@ $(QEMU_GA_MSI_ARCH) $(QEMU_GA_MSI_WITH_VSS) $(QEMU_GA_MSI_MINGW_DLL_PATH) $<,"WIXL","$@")
else
msi:
	@echo "MSI build not configured or dependency resolution failed (reconfigure with --enable-guest-agent-msi option)"
endif

ifneq ($(EXESUF),)
.PHONY: qemu-ga
qemu-ga: qemu-ga$(EXESUF) $(QGA_VSS_PROVIDER) $(QEMU_GA_MSI)
endif

ivshmem-client$(EXESUF): $(ivshmem-client-obj-y) $(COMMON_LDADDS)
	$(call LINK, $^)
ivshmem-server$(EXESUF): $(ivshmem-server-obj-y) $(COMMON_LDADDS)
	$(call LINK, $^)

module_block.h: $(SRC_PATH)/scripts/modules/module_block.py config-host.mak
	$(call quiet-command,$(PYTHON) $< $@ \
	$(addprefix $(SRC_PATH)/,$(patsubst %.mo,%.c,$(block-obj-m))), \
	"GEN","$@")

clean:
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f qemu-options.def
	rm -f *.msi
	find . \( -name '*.so' -o -name '*.dll' -o -name '*.mo' -o -name '*.[oda]' \) -type f -exec rm {} +
	rm -f $(filter-out %.tlb,$(TOOLS)) $(HELPERS-y) qemu-ga TAGS cscope.* *.pod *~ */*~
	rm -f fsdev/*.pod
	rm -f qemu-img-cmds.h
	rm -f ui/shader/*-vert.h ui/shader/*-frag.h
	@# May not be present in GENERATED_FILES
	rm -f trace/generated-tracers-dtrace.dtrace*
	rm -f trace/generated-tracers-dtrace.h*
	rm -f $(foreach f,$(GENERATED_FILES),$(f) $(f)-timestamp)
	rm -rf qapi-generated
	rm -rf qga/qapi-generated
	for d in $(ALL_SUBDIRS); do \
	if test -d $$d; then $(MAKE) -C $$d $@ || exit 1; fi; \
	rm -f $$d/qemu-options.def; \
        done
	rm -f $(SUBDIR_DEVICES_MAK) config-all-devices.mak

VERSION ?= $(shell cat VERSION)

dist: qemu-$(VERSION).tar.bz2

qemu-%.tar.bz2:
	$(SRC_PATH)/scripts/make-release "$(SRC_PATH)" "$(patsubst qemu-%.tar.bz2,%,$@)"

distclean: clean
	rm -f config-host.mak config-host.h* config-host.ld $(DOCS) qemu-options.texi qemu-img-cmds.texi qemu-monitor.texi qemu-monitor-info.texi
	rm -f config-all-devices.mak config-all-disas.mak config.status
	rm -f po/*.mo tests/qemu-iotests/common.env
	rm -f roms/seabios/config.mak roms/vgabios/config.mak
	rm -f qemu-doc.info qemu-doc.aux qemu-doc.cp qemu-doc.cps
	rm -f qemu-doc.fn qemu-doc.fns qemu-doc.info qemu-doc.ky qemu-doc.kys
	rm -f qemu-doc.log qemu-doc.pdf qemu-doc.pg qemu-doc.toc qemu-doc.tp
	rm -f qemu-doc.vr qemu-doc.txt
	rm -f config.log
	rm -f linux-headers/asm
	rm -f docs/qemu-ga-qapi.texi docs/qemu-qmp-qapi.texi docs/version.texi
	rm -f docs/qemu-qmp-ref.7 docs/qemu-ga-ref.7
	rm -f docs/qemu-qmp-ref.txt docs/qemu-ga-ref.txt
	rm -f docs/qemu-qmp-ref.pdf docs/qemu-ga-ref.pdf
	rm -f docs/qemu-qmp-ref.html docs/qemu-ga-ref.html
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done
	rm -Rf .sdk
	if test -f pixman/config.log; then $(MAKE) -C pixman distclean; fi
	if test -f dtc/version_gen.h; then $(MAKE) $(DTC_MAKE_ARGS) clean; fi

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  modifiers  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
common  de-ch  es     fo  fr-ca  hu     ja  mk  nl-be      pt  sl     tr \
bepo    cz

ifdef INSTALL_BLOBS
BLOBS=bios.bin bios-256k.bin sgabios.bin vgabios.bin vgabios-cirrus.bin \
vgabios-stdvga.bin vgabios-vmware.bin vgabios-qxl.bin vgabios-virtio.bin \
acpi-dsdt.aml \
ppc_rom.bin openbios-sparc32 openbios-sparc64 openbios-ppc QEMU,tcx.bin QEMU,cgthree.bin \
pxe-e1000.rom pxe-eepro100.rom pxe-ne2k_pci.rom \
pxe-pcnet.rom pxe-rtl8139.rom pxe-virtio.rom \
efi-e1000.rom efi-eepro100.rom efi-ne2k_pci.rom \
efi-pcnet.rom efi-rtl8139.rom efi-virtio.rom \
efi-e1000e.rom efi-vmxnet3.rom \
qemu-icon.bmp qemu_logo_no_text.svg \
bamboo.dtb petalogix-s3adsp1800.dtb petalogix-ml605.dtb \
multiboot.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin \
s390-ccw.img \
spapr-rtas.bin slof.bin skiboot.lid \
palcode-clipper \
u-boot.e500
else
BLOBS=
endif

install-doc: $(DOCS)
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) qemu-doc.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) qemu-doc.txt "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/qemu-qmp-ref.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/qemu-qmp-ref.txt "$(DESTDIR)$(qemu_docdir)"
ifdef CONFIG_POSIX
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) qemu.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) docs/qemu-qmp-ref.7 "$(DESTDIR)$(mandir)/man7"
ifneq ($(TOOLS),)
	$(INSTALL_DATA) qemu-img.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) qemu-nbd.8 "$(DESTDIR)$(mandir)/man8"
endif
ifneq (,$(findstring qemu-ga,$(TOOLS)))
	$(INSTALL_DATA) qemu-ga.8 "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) docs/qemu-ga-ref.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/qemu-ga-ref.txt "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/qemu-ga-ref.7 "$(DESTDIR)$(mandir)/man7"
endif
endif
ifdef CONFIG_VIRTFS
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) fsdev/virtfs-proxy-helper.1 "$(DESTDIR)$(mandir)/man1"
endif

install-datadir:
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)"

install-localstatedir:
ifdef CONFIG_POSIX
ifneq (,$(findstring qemu-ga,$(TOOLS)))
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_localstatedir)"/run
endif
endif


install: all $(if $(BUILD_DOCS),install-doc) install-datadir install-localstatedir
ifneq ($(TOOLS),)
	$(call install-prog,$(subst qemu-ga,qemu-ga$(EXESUF),$(TOOLS)),$(DESTDIR)$(bindir))
endif
ifneq ($(CONFIG_MODULES),)
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_moddir)"
	for s in $(modules-m:.mo=$(DSOSUF)); do \
		t="$(DESTDIR)$(qemu_moddir)/$$(echo $$s | tr / -)"; \
		$(INSTALL_LIB) $$s "$$t"; \
		test -z "$(STRIP)" || $(STRIP) "$$t"; \
	done
endif
ifneq ($(HELPERS-y),)
	$(call install-prog,$(HELPERS-y),$(DESTDIR)$(libexecdir))
endif
ifneq ($(BLOBS),)
	set -e; for x in $(BLOBS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/$$x "$(DESTDIR)$(qemu_datadir)"; \
	done
endif
ifeq ($(CONFIG_GTK),y)
	$(MAKE) -C po $@
endif
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)/keymaps"
	set -e; for x in $(KEYMAPS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/keymaps/$$x "$(DESTDIR)$(qemu_datadir)/keymaps"; \
	done
	$(INSTALL_DATA) $(BUILD_DIR)/trace-events-all "$(DESTDIR)$(qemu_datadir)/trace-events-all"
	for d in $(TARGET_DIRS); do \
	$(MAKE) $(SUBDIR_MAKEFLAGS) TARGET_DIR=$$d/ -C $$d $@ || exit 1 ; \
        done

# various test targets
test speed: all
	$(MAKE) -C tests/tcg $@

.PHONY: ctags
ctags:
	rm -f tags
	find "$(SRC_PATH)" -name '*.[hc]' -exec ctags --append {} +

.PHONY: TAGS
TAGS:
	rm -f TAGS
	find "$(SRC_PATH)" -name '*.[hc]' -exec etags --append {} +

cscope:
	rm -f "$(SRC_PATH)"/cscope.*
	find "$(SRC_PATH)/" -name "*.[chsS]" -print | sed 's,^\./,,' > "$(SRC_PATH)/cscope.files"
	cscope -b -i"$(SRC_PATH)/cscope.files"

# opengl shader programs
ui/shader/%-vert.h: $(SRC_PATH)/ui/shader/%.vert $(SRC_PATH)/scripts/shaderinclude.pl
	@mkdir -p $(dir $@)
	$(call quiet-command,\
		perl $(SRC_PATH)/scripts/shaderinclude.pl $< > $@,\
		"VERT","$@")

ui/shader/%-frag.h: $(SRC_PATH)/ui/shader/%.frag $(SRC_PATH)/scripts/shaderinclude.pl
	@mkdir -p $(dir $@)
	$(call quiet-command,\
		perl $(SRC_PATH)/scripts/shaderinclude.pl $< > $@,\
		"FRAG","$@")

ui/console-gl.o: $(SRC_PATH)/ui/console-gl.c \
	ui/shader/texture-blit-vert.h ui/shader/texture-blit-frag.h

# documentation
MAKEINFO=makeinfo
MAKEINFOFLAGS=--no-split --number-sections -I docs
TEXIFLAG=$(if $(V),,--quiet)

docs/version.texi: $(SRC_PATH)/VERSION
	$(call quiet-command,echo "@set VERSION $(VERSION)" > $@,"GEN","$@")

%.html: %.texi
	$(call quiet-command,LC_ALL=C $(MAKEINFO) $(MAKEINFOFLAGS) --no-headers \
	--html $< -o $@,"GEN","$@")

%.info: %.texi
	$(call quiet-command,$(MAKEINFO) $(MAKEINFOFLAGS) $< -o $@,"GEN","$@")

%.txt: %.texi
	$(call quiet-command,LC_ALL=C $(MAKEINFO) $(MAKEINFOFLAGS) --no-headers \
	--plaintext $< -o $@,"GEN","$@")

%.pdf: %.texi
	$(call quiet-command,texi2pdf $(TEXIFLAG) -I $(SRC_PATH) -I docs $< -o $@,"GEN","$@")

docs/qemu-ga-ref.html docs/qemu-ga-ref.info docs/qemu-ga-ref.txt docs/qemu-ga-ref.pdf docs/qemu-ga-ref.7.pod: docs/version.texi
docs/qemu-qmp-ref.html docs/qemu-qmp-ref.info docs/qemu-qmp-ref.txt docs/qemu-qmp-ref.pdf docs/qemu-qmp-ref.pod: docs/version.texi

qemu-options.texi: $(SRC_PATH)/qemu-options.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

qemu-monitor.texi: $(SRC_PATH)/hmp-commands.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

qemu-monitor-info.texi: $(SRC_PATH)/hmp-commands-info.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

qemu-img-cmds.texi: $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

docs/qemu-qmp-qapi.texi docs/qemu-ga-qapi.texi: $(SRC_PATH)/scripts/qapi2texi.py $(qapi-py)

docs/qemu-qmp-qapi.texi: $(qapi-modules)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi2texi.py $< > $@,"GEN","$@")

docs/qemu-ga-qapi.texi: $(SRC_PATH)/qga/qapi-schema.json
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi2texi.py $< > $@,"GEN","$@")

qemu.1: qemu-doc.texi qemu-options.texi qemu-monitor.texi qemu-monitor-info.texi
qemu.1: qemu-option-trace.texi
qemu-img.1: qemu-img.texi qemu-option-trace.texi qemu-img-cmds.texi
fsdev/virtfs-proxy-helper.1: fsdev/virtfs-proxy-helper.texi
qemu-nbd.8: qemu-nbd.texi qemu-option-trace.texi
qemu-ga.8: qemu-ga.texi

html: qemu-doc.html docs/qemu-qmp-ref.html docs/qemu-ga-ref.html
info: qemu-doc.info docs/qemu-qmp-ref.info docs/qemu-ga-ref.info
pdf: qemu-doc.pdf docs/qemu-qmp-ref.pdf docs/qemu-ga-ref.pdf
txt: qemu-doc.txt docs/qemu-qmp-ref.txt docs/qemu-ga-ref.txt

qemu-doc.html qemu-doc.info qemu-doc.pdf qemu-doc.txt: \
	qemu-img.texi qemu-nbd.texi qemu-options.texi qemu-option-trace.texi \
	qemu-monitor.texi qemu-img-cmds.texi qemu-ga.texi \
	qemu-monitor-info.texi

docs/qemu-ga-ref.dvi docs/qemu-ga-ref.html docs/qemu-ga-ref.info docs/qemu-ga-ref.pdf docs/qemu-ga-ref.txt docs/qemu-ga-ref.7: \
docs/qemu-ga-ref.texi docs/qemu-ga-qapi.texi

docs/qemu-qmp-ref.dvi docs/qemu-qmp-ref.html docs/qemu-qmp-ref.info docs/qemu-qmp-ref.pdf docs/qemu-qmp-ref.txt docs/qemu-qmp-ref.7: \
docs/qemu-qmp-ref.texi docs/qemu-qmp-qapi.texi


ifdef CONFIG_WIN32

INSTALLER = qemu-setup-$(VERSION)$(EXESUF)

nsisflags = -V2 -NOCD

ifneq ($(wildcard $(SRC_PATH)/dll),)
ifeq ($(ARCH),x86_64)
# 64 bit executables
DLL_PATH = $(SRC_PATH)/dll/w64
nsisflags += -DW64
else
# 32 bit executables
DLL_PATH = $(SRC_PATH)/dll/w32
endif
endif

.PHONY: installer
installer: $(INSTALLER)

INSTDIR=/tmp/qemu-nsis

$(INSTALLER): $(SRC_PATH)/qemu.nsi
	$(MAKE) install prefix=${INSTDIR}
ifdef SIGNCODE
	(cd ${INSTDIR}; \
         for i in *.exe; do \
           $(SIGNCODE) $${i}; \
         done \
        )
endif # SIGNCODE
	(cd ${INSTDIR}; \
         for i in qemu-system-*.exe; do \
           arch=$${i%.exe}; \
           arch=$${arch#qemu-system-}; \
           echo Section \"$$arch\" Section_$$arch; \
           echo SetOutPath \"\$$INSTDIR\"; \
           echo File \"\$${BINDIR}\\$$i\"; \
           echo SectionEnd; \
         done \
        ) >${INSTDIR}/system-emulations.nsh
	makensis $(nsisflags) \
                $(if $(BUILD_DOCS),-DCONFIG_DOCUMENTATION="y") \
                $(if $(CONFIG_GTK),-DCONFIG_GTK="y") \
                -DBINDIR="${INSTDIR}" \
                $(if $(DLL_PATH),-DDLLDIR="$(DLL_PATH)") \
                -DSRCDIR="$(SRC_PATH)" \
                -DOUTFILE="$(INSTALLER)" \
                -DDISPLAYVERSION="$(VERSION)" \
                $(SRC_PATH)/qemu.nsi
	rm -r ${INSTDIR}
ifdef SIGNCODE
	$(SIGNCODE) $(INSTALLER)
endif # SIGNCODE
endif # CONFIG_WIN

# Add a dependency on the generated files, so that they are always
# rebuilt before other object files
ifneq ($(filter-out $(UNCHECKED_GOALS),$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
Makefile: $(GENERATED_FILES)
endif

.SECONDARY: $(TRACE_HEADERS) $(TRACE_HEADERS:%=%-timestamp) \
	$(TRACE_SOURCES) $(TRACE_SOURCES:%=%-timestamp) \
	$(TRACE_DTRACE) $(TRACE_DTRACE:%=%-timestamp)

# Include automatically generated dependency files
# Dependencies in Makefile.objs files come from our recursive subdir rules
-include $(wildcard *.d tests/*.d)

include $(SRC_PATH)/tests/docker/Makefile.include

.PHONY: help
help:
	@echo  'Generic targets:'
	@echo  '  all             - Build all'
	@echo  '  dir/file.o      - Build specified target only'
	@echo  '  install         - Install QEMU, documentation and tools'
	@echo  '  ctags/TAGS      - Generate tags file for editors'
	@echo  '  cscope          - Generate cscope index'
	@echo  ''
	@$(if $(TARGET_DIRS), \
		echo 'Architecture specific targets:'; \
		$(foreach t, $(TARGET_DIRS), \
		printf "  %-30s - Build for %s\\n" $(patsubst %,subdir-%,$(t)) $(t);) \
		echo '')
	@echo  'Cleaning targets:'
	@echo  '  clean           - Remove most generated files but keep the config'
	@echo  '  distclean       - Remove all generated files'
	@echo  '  dist            - Build a distributable tarball'
	@echo  ''
	@echo  'Test targets:'
	@echo  '  check           - Run all tests (check-help for details)'
	@echo  '  docker          - Help about targets running tests inside Docker containers'
	@echo  ''
	@echo  'Documentation targets:'
	@echo  '  html info pdf txt'
	@echo  '                  - Build documentation in specified format'
	@echo  ''
ifdef CONFIG_WIN32
	@echo  'Windows targets:'
	@echo  '  installer       - Build NSIS-based installer for QEMU'
ifdef QEMU_GA_MSI_ENABLED
	@echo  '  msi             - Build MSI-based installer for qemu-ga'
endif
	@echo  ''
endif
	@echo  '  make V=0|1 [targets] 0 => quiet build (default), 1 => verbose build'
