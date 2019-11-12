# Makefile for QEMU.

ifneq ($(words $(subst :, ,$(CURDIR))), 1)
  $(error main directory cannot contain spaces nor colons)
endif

# Always point to the root of the build tree (needs GNU make).
BUILD_DIR=$(CURDIR)

# Before including a proper config-host.mak, assume we are in the source tree
SRC_PATH=.

UNCHECKED_GOALS := %clean TAGS cscope ctags dist \
    html info pdf txt \
    help check-help print-% \
    docker docker-% vm-help vm-test vm-build-%

print-%:
	@echo '$*=$($*)'

# All following code might depend on configuration variables
ifneq ($(wildcard config-host.mak),)
# Put the all: rule here so that config-host.mak can contain dependencies.
all:
include config-host.mak

git-submodule-update:

.PHONY: git-submodule-update

git_module_status := $(shell \
  cd '$(SRC_PATH)' && \
  GIT="$(GIT)" ./scripts/git-submodule.sh status $(GIT_SUBMODULES); \
  echo $$?; \
)

ifeq (1,$(git_module_status))
ifeq (no,$(GIT_UPDATE))
git-submodule-update:
	$(call quiet-command, \
            echo && \
            echo "GIT submodule checkout is out of date. Please run" && \
            echo "  scripts/git-submodule.sh update $(GIT_SUBMODULES)" && \
            echo "from the source directory checkout $(SRC_PATH)" && \
            echo && \
            exit 1)
else
git-submodule-update:
	$(call quiet-command, \
          (cd $(SRC_PATH) && GIT="$(GIT)" ./scripts/git-submodule.sh update $(GIT_SUBMODULES)), \
          "GIT","$(GIT_SUBMODULES)")
endif
endif

.git-submodule-status: git-submodule-update config-host.mak

# Check that we're not trying to do an out-of-tree build from
# a tree that's been used for an in-tree build.
ifneq ($(realpath $(SRC_PATH)),$(realpath .))
ifneq ($(wildcard $(SRC_PATH)/config-host.mak),)
$(error This is an out of tree build but your source tree ($(SRC_PATH)) \
seems to have been used for an in-tree build. You can fix this by running \
"$(MAKE) distclean && rm -rf *-linux-user *-softmmu" in your source tree)
endif
endif

CONFIG_SOFTMMU := $(if $(filter %-softmmu,$(TARGET_DIRS)),y)
CONFIG_USER_ONLY := $(if $(filter %-user,$(TARGET_DIRS)),y)
CONFIG_XEN := $(CONFIG_XEN_BACKEND)
CONFIG_ALL=y
-include config-all-devices.mak
-include config-all-disas.mak

config-host.mak: $(SRC_PATH)/configure $(SRC_PATH)/pc-bios $(SRC_PATH)/VERSION
	@echo $@ is out-of-date, running configure
	@./config.status

# Force configure to re-run if the API symbols are updated
ifeq ($(CONFIG_PLUGIN),y)
config-host.mak: $(SRC_PATH)/plugins/qemu-plugins.symbols
endif

else
config-host.mak:
ifneq ($(filter-out $(UNCHECKED_GOALS),$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
	@echo "Please call configure before running make!"
	@exit 1
endif
endif

include $(SRC_PATH)/rules.mak

# lor is defined in rules.mak
CONFIG_BLOCK := $(call lor,$(CONFIG_SOFTMMU),$(CONFIG_TOOLS))

# Create QEMU_PKGVERSION and FULL_VERSION strings
# If PKGVERSION is set, use that; otherwise get version and -dirty status from git
QEMU_PKGVERSION := $(if $(PKGVERSION),$(PKGVERSION),$(shell \
  cd $(SRC_PATH); \
  if test -e .git; then \
    git describe --match 'v*' 2>/dev/null | tr -d '\n'; \
    if ! git diff-index --quiet HEAD &>/dev/null; then \
      echo "-dirty"; \
    fi; \
  fi))

# Either "version (pkgversion)", or just "version" if pkgversion not set
FULL_VERSION := $(if $(QEMU_PKGVERSION),$(VERSION) ($(QEMU_PKGVERSION)),$(VERSION))

generated-files-y = qemu-version.h config-host.h qemu-options.def

GENERATED_QAPI_FILES = qapi/qapi-builtin-types.h qapi/qapi-builtin-types.c
GENERATED_QAPI_FILES += qapi/qapi-types.h qapi/qapi-types.c
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-types-%.h)
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-types-%.c)
GENERATED_QAPI_FILES += qapi/qapi-builtin-visit.h qapi/qapi-builtin-visit.c
GENERATED_QAPI_FILES += qapi/qapi-visit.h qapi/qapi-visit.c
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-visit-%.h)
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-visit-%.c)
GENERATED_QAPI_FILES += qapi/qapi-commands.h qapi/qapi-commands.c
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-commands-%.h)
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-commands-%.c)
GENERATED_QAPI_FILES += qapi/qapi-emit-events.h qapi/qapi-emit-events.c
GENERATED_QAPI_FILES += qapi/qapi-events.h qapi/qapi-events.c
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-events-%.h)
GENERATED_QAPI_FILES += $(QAPI_MODULES:%=qapi/qapi-events-%.c)
GENERATED_QAPI_FILES += qapi/qapi-introspect.c qapi/qapi-introspect.h
GENERATED_QAPI_FILES += qapi/qapi-doc.texi

generated-files-y += $(GENERATED_QAPI_FILES)

generated-files-y += trace/generated-tcg-tracers.h

generated-files-y += trace/generated-helpers-wrappers.h
generated-files-y += trace/generated-helpers.h
generated-files-y += trace/generated-helpers.c

generated-files-$(CONFIG_TRACE_UST) += trace-ust-all.h
generated-files-$(CONFIG_TRACE_UST) += trace-ust-all.c

generated-files-y += module_block.h

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

generated-files-y += $(TRACE_HEADERS)
generated-files-y += $(TRACE_SOURCES)
generated-files-y += $(BUILD_DIR)/trace-events-all
generated-files-y += .git-submodule-status

trace-group-name = $(shell dirname $1 | sed -e 's/[^a-zA-Z0-9]/_/g')

tracetool-y = $(SRC_PATH)/scripts/tracetool.py
tracetool-y += $(shell find $(SRC_PATH)/scripts/tracetool -name "*.py")

%/trace.h: %/trace.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace.h-timestamp: $(SRC_PATH)/%/trace-events $(tracetool-y) $(BUILD_DIR)/config-host.mak
	$(call quiet-command,$(TRACETOOL) \
		--group=$(call trace-group-name,$@) \
		--format=h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

%/trace.c: %/trace.c-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace.c-timestamp: $(SRC_PATH)/%/trace-events $(tracetool-y) $(BUILD_DIR)/config-host.mak
	$(call quiet-command,$(TRACETOOL) \
		--group=$(call trace-group-name,$@) \
		--format=c \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

%/trace-ust.h: %/trace-ust.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
%/trace-ust.h-timestamp: $(SRC_PATH)/%/trace-events $(tracetool-y) $(BUILD_DIR)/config-host.mak
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
trace-root.h-timestamp: $(SRC_PATH)/trace-events $(tracetool-y) $(BUILD_DIR)/config-host.mak
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-root.c: trace-root.c-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-root.c-timestamp: $(SRC_PATH)/trace-events $(tracetool-y) $(BUILD_DIR)/config-host.mak
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=c \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-ust-root.h: trace-ust-root.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-ust-root.h-timestamp: $(SRC_PATH)/trace-events $(tracetool-y) $(BUILD_DIR)/config-host.mak
	$(call quiet-command,$(TRACETOOL) \
		--group=root \
		--format=ust-events-h \
		--backends=$(TRACE_BACKENDS) \
		$< > $@,"GEN","$(@:%-timestamp=%)")

trace-ust-all.h: trace-ust-all.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-ust-all.h-timestamp: $(trace-events-files) $(tracetool-y) $(BUILD_DIR)/config-host.mak
	$(call quiet-command,$(TRACETOOL) \
		--group=all \
		--format=ust-events-h \
		--backends=$(TRACE_BACKENDS) \
		$(trace-events-files) > $@,"GEN","$(@:%-timestamp=%)")

trace-ust-all.c: trace-ust-all.c-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@
trace-ust-all.c-timestamp: $(trace-events-files) $(tracetool-y) $(BUILD_DIR)/config-host.mak
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

KEYCODEMAP_GEN = $(SRC_PATH)/ui/keycodemapdb/tools/keymap-gen
KEYCODEMAP_CSV = $(SRC_PATH)/ui/keycodemapdb/data/keymaps.csv

KEYCODEMAP_FILES = \
		 ui/input-keymap-atset1-to-qcode.c \
		 ui/input-keymap-linux-to-qcode.c \
		 ui/input-keymap-qcode-to-atset1.c \
		 ui/input-keymap-qcode-to-atset2.c \
		 ui/input-keymap-qcode-to-atset3.c \
		 ui/input-keymap-qcode-to-linux.c \
		 ui/input-keymap-qcode-to-qnum.c \
		 ui/input-keymap-qcode-to-sun.c \
		 ui/input-keymap-qnum-to-qcode.c \
		 ui/input-keymap-usb-to-qcode.c \
		 ui/input-keymap-win32-to-qcode.c \
		 ui/input-keymap-x11-to-qcode.c \
		 ui/input-keymap-xorgevdev-to-qcode.c \
		 ui/input-keymap-xorgkbd-to-qcode.c \
		 ui/input-keymap-xorgxquartz-to-qcode.c \
		 ui/input-keymap-xorgxwin-to-qcode.c \
		 ui/input-keymap-osx-to-qcode.c \
		 $(NULL)

generated-files-$(CONFIG_SOFTMMU) += $(KEYCODEMAP_FILES)

ui/input-keymap-%.c: $(KEYCODEMAP_GEN) $(KEYCODEMAP_CSV) $(SRC_PATH)/ui/Makefile.objs
	$(call quiet-command,\
	    stem=$* && src=$${stem%-to-*} dst=$${stem#*-to-} && \
	    test -e $(KEYCODEMAP_GEN) && \
	    $(PYTHON) $(KEYCODEMAP_GEN) \
	          --lang glib2 \
	          --varname qemu_input_map_$${src}_to_$${dst} \
	          code-map $(KEYCODEMAP_CSV) $${src} $${dst} \
	        > $@ || rm -f $@, "GEN", "$@")

$(KEYCODEMAP_GEN): .git-submodule-status
$(KEYCODEMAP_CSV): .git-submodule-status

edk2-decompressed = $(basename $(wildcard pc-bios/edk2-*.fd.bz2))
pc-bios/edk2-%.fd: pc-bios/edk2-%.fd.bz2
	$(call quiet-command,bzip2 -d -c $< > $@,"BUNZIP2",$<)

# Don't try to regenerate Makefile or configure
# We don't generate any of them
Makefile: ;
configure: ;

.PHONY: all clean cscope distclean html info install install-doc \
	pdf txt recurse-all dist msi FORCE

$(call set-vpath, $(SRC_PATH))

LIBS+=-lz $(LIBS_TOOLS)

vhost-user-json-y =
HELPERS-y =

HELPERS-$(call land,$(CONFIG_SOFTMMU),$(CONFIG_LINUX)) = qemu-bridge-helper$(EXESUF)

ifdef CONFIG_LINUX
ifdef CONFIG_VIRGL
ifdef CONFIG_GBM
HELPERS-y += vhost-user-gpu$(EXESUF)
vhost-user-json-y += contrib/vhost-user-gpu/50-qemu-gpu.json
endif
endif
endif

# Sphinx does not allow building manuals into the same directory as
# the source files, so if we're doing an in-tree QEMU build we must
# build the manuals into a subdirectory (and then install them from
# there for 'make install'). For an out-of-tree build we can just
# use the docs/ subdirectory in the build tree as normal.
ifeq ($(realpath $(SRC_PATH)),$(realpath .))
MANUAL_BUILDDIR := docs/built
else
MANUAL_BUILDDIR := docs
endif

ifdef BUILD_DOCS
DOCS=qemu-doc.html qemu-doc.txt qemu.1 qemu-img.1 qemu-nbd.8 $(MANUAL_BUILDDIR)/interop/qemu-ga.8
DOCS+=docs/interop/qemu-qmp-ref.html docs/interop/qemu-qmp-ref.txt docs/interop/qemu-qmp-ref.7
DOCS+=docs/interop/qemu-ga-ref.html docs/interop/qemu-ga-ref.txt docs/interop/qemu-ga-ref.7
DOCS+=docs/qemu-block-drivers.7
DOCS+=docs/qemu-cpu-models.7
ifdef CONFIG_VIRTFS
DOCS+=fsdev/virtfs-proxy-helper.1
endif
ifdef CONFIG_TRACE_SYSTEMTAP
DOCS+=scripts/qemu-trace-stap.1
endif
else
DOCS=
endif

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory --quiet) BUILD_DIR=$(BUILD_DIR)
SUBDIR_DEVICES_MAK=$(patsubst %, %/config-devices.mak, $(filter %-softmmu, $(TARGET_DIRS)))
SUBDIR_DEVICES_MAK_DEP=$(patsubst %, %.d, $(SUBDIR_DEVICES_MAK))

ifeq ($(SUBDIR_DEVICES_MAK),)
config-all-devices.mak: config-host.mak
	$(call quiet-command,echo '# no devices' > $@,"GEN","$@")
else
config-all-devices.mak: $(SUBDIR_DEVICES_MAK) config-host.mak
	$(call quiet-command, sed -n \
             's|^\([^=]*\)=\(.*\)$$|\1:=$$(findstring y,$$(\1)\2)|p' \
             $(SUBDIR_DEVICES_MAK) | sort -u > $@, \
             "GEN","$@")
endif

-include $(SUBDIR_DEVICES_MAK_DEP)

# This has to be kept in sync with Kconfig.host.
MINIKCONF_ARGS = \
    $(CONFIG_MINIKCONF_MODE) \
    $@ $*/config-devices.mak.d $< $(MINIKCONF_INPUTS) \
    CONFIG_KVM=$(CONFIG_KVM) \
    CONFIG_SPICE=$(CONFIG_SPICE) \
    CONFIG_IVSHMEM=$(CONFIG_IVSHMEM) \
    CONFIG_TPM=$(CONFIG_TPM) \
    CONFIG_XEN=$(CONFIG_XEN) \
    CONFIG_OPENGL=$(CONFIG_OPENGL) \
    CONFIG_X11=$(CONFIG_X11) \
    CONFIG_VHOST_USER=$(CONFIG_VHOST_USER) \
    CONFIG_VIRTFS=$(CONFIG_VIRTFS) \
    CONFIG_LINUX=$(CONFIG_LINUX) \
    CONFIG_PVRDMA=$(CONFIG_PVRDMA)

MINIKCONF_INPUTS = $(SRC_PATH)/Kconfig.host $(SRC_PATH)/hw/Kconfig \
                   $(wildcard $(SRC_PATH)/hw/*/Kconfig)
MINIKCONF = $(PYTHON) $(SRC_PATH)/scripts/minikconf.py \

$(SUBDIR_DEVICES_MAK): %/config-devices.mak: default-configs/%.mak $(MINIKCONF_INPUTS) $(BUILD_DIR)/config-host.mak
	$(call quiet-command, $(MINIKCONF) $(MINIKCONF_ARGS) > $@.tmp, "GEN", "$@.tmp")
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
	    echo "Run \"$(MAKE) defconfig\" to regenerate."; \
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
                authz-obj-y \
                chardev-obj-y \
                util-obj-y \
                qga-obj-y \
                elf2dmp-obj-y \
                ivshmem-client-obj-y \
                ivshmem-server-obj-y \
                rdmacm-mux-obj-y \
                libvhost-user-obj-y \
                vhost-user-scsi-obj-y \
                vhost-user-blk-obj-y \
                vhost-user-input-obj-y \
                vhost-user-gpu-obj-y \
                qga-vss-dll-obj-y \
                block-obj-y \
                block-obj-m \
                crypto-obj-y \
                crypto-user-obj-y \
                qom-obj-y \
                io-obj-y \
                common-obj-y \
                common-obj-m \
                ui-obj-y \
                ui-obj-m \
                audio-obj-y \
                audio-obj-m \
                trace-obj-y)

include $(SRC_PATH)/tests/Makefile.include

all: $(DOCS) $(if $(BUILD_DOCS),sphinxdocs) $(TOOLS) $(HELPERS-y) recurse-all modules $(vhost-user-json-y)

qemu-version.h: FORCE
	$(call quiet-command, \
                (printf '#define QEMU_PKGVERSION "$(QEMU_PKGVERSION)"\n'; \
		printf '#define QEMU_FULL_VERSION "$(FULL_VERSION)"\n'; \
		) > $@.tmp)
	$(call quiet-command, if ! cmp -s $@ $@.tmp; then \
	  mv $@.tmp $@; \
	 else \
	  rm $@.tmp; \
	 fi)

config-host.h: config-host.h-timestamp
config-host.h-timestamp: config-host.mak
qemu-options.def: $(SRC_PATH)/qemu-options.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$@")

TARGET_DIRS_RULES := $(foreach t, all clean install, $(addsuffix /$(t), $(TARGET_DIRS)))

SOFTMMU_ALL_RULES=$(filter %-softmmu/all, $(TARGET_DIRS_RULES))
$(SOFTMMU_ALL_RULES): $(authz-obj-y)
$(SOFTMMU_ALL_RULES): $(block-obj-y)
$(SOFTMMU_ALL_RULES): $(chardev-obj-y)
$(SOFTMMU_ALL_RULES): $(crypto-obj-y)
$(SOFTMMU_ALL_RULES): $(io-obj-y)
$(SOFTMMU_ALL_RULES): config-all-devices.mak
ifdef DECOMPRESS_EDK2_BLOBS
$(SOFTMMU_ALL_RULES): $(edk2-decompressed)
endif

.PHONY: $(TARGET_DIRS_RULES)
# The $(TARGET_DIRS_RULES) are of the form SUBDIR/GOAL, so that
# $(dir $@) yields the sub-directory, and $(notdir $@) yields the sub-goal
$(TARGET_DIRS_RULES):
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $(dir $@) V="$(V)" TARGET_DIR="$(dir $@)" $(notdir $@),)

DTC_MAKE_ARGS=-I$(SRC_PATH)/dtc VPATH=$(SRC_PATH)/dtc -C dtc V="$(V)" LIBFDT_srcdir=$(SRC_PATH)/dtc/libfdt
DTC_CFLAGS=$(CFLAGS) $(QEMU_CFLAGS)
DTC_CPPFLAGS=-I$(BUILD_DIR)/dtc -I$(SRC_PATH)/dtc -I$(SRC_PATH)/dtc/libfdt

.PHONY: dtc/all
dtc/all: .git-submodule-status dtc/libfdt dtc/tests
	$(call quiet-command,$(MAKE) $(DTC_MAKE_ARGS) CPPFLAGS="$(DTC_CPPFLAGS)" CFLAGS="$(DTC_CFLAGS)" LDFLAGS="$(LDFLAGS)" ARFLAGS="$(ARFLAGS)" CC="$(CC)" AR="$(AR)" LD="$(LD)" $(SUBDIR_MAKEFLAGS) libfdt/libfdt.a,)

dtc/%: .git-submodule-status
	@mkdir -p $@

# Overriding CFLAGS causes us to lose defines added in the sub-makefile.
# Not overriding CFLAGS leads to mis-matches between compilation modes.
# Therefore we replicate some of the logic in the sub-makefile.
# Remove all the extra -Warning flags that QEMU uses that Capstone doesn't;
# no need to annoy QEMU developers with such things.
CAP_CFLAGS = $(patsubst -W%,,$(CFLAGS) $(QEMU_CFLAGS))
CAP_CFLAGS += -DCAPSTONE_USE_SYS_DYN_MEM
CAP_CFLAGS += -DCAPSTONE_HAS_ARM
CAP_CFLAGS += -DCAPSTONE_HAS_ARM64
CAP_CFLAGS += -DCAPSTONE_HAS_POWERPC
CAP_CFLAGS += -DCAPSTONE_HAS_X86

.PHONY: capstone/all
capstone/all: .git-submodule-status
	$(call quiet-command,$(MAKE) -C $(SRC_PATH)/capstone CAPSTONE_SHARED=no BUILDDIR="$(BUILD_DIR)/capstone" CC="$(CC)" AR="$(AR)" LD="$(LD)" RANLIB="$(RANLIB)" CFLAGS="$(CAP_CFLAGS)" $(SUBDIR_MAKEFLAGS) $(BUILD_DIR)/capstone/$(LIBCAPSTONE))

.PHONY: slirp/all
slirp/all: .git-submodule-status
	$(call quiet-command,$(MAKE) -C $(SRC_PATH)/slirp		\
		BUILD_DIR="$(BUILD_DIR)/slirp" 			\
		PKG_CONFIG="$(PKG_CONFIG)" 				\
		CC="$(CC)" AR="$(AR)" 	LD="$(LD)" RANLIB="$(RANLIB)"	\
		CFLAGS="$(QEMU_CFLAGS) $(CFLAGS)" LDFLAGS="$(LDFLAGS)")

# Compatibility gunk to keep make working across the rename of targets
# for recursion, to be removed some time after 4.1.
subdir-dtc: dtc/all
subdir-capstone: capstone/all
subdir-slirp: slirp/all

$(filter %/all, $(TARGET_DIRS_RULES)): libqemuutil.a $(common-obj-y) \
	$(qom-obj-y) $(crypto-user-obj-$(CONFIG_USER_ONLY))

ROM_DIRS = $(addprefix pc-bios/, $(ROMS))
ROM_DIRS_RULES=$(foreach t, all clean, $(addsuffix /$(t), $(ROM_DIRS)))
# Only keep -O and -g cflags
.PHONY: $(ROM_DIRS_RULES)
$(ROM_DIRS_RULES):
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $(dir $@) V="$(V)" TARGET_DIR="$(dir $@)" CFLAGS="$(filter -O% -g%,$(CFLAGS))" $(notdir $@),)

.PHONY: recurse-all recurse-clean recurse-install
recurse-all: $(addsuffix /all, $(TARGET_DIRS) $(ROM_DIRS))
recurse-clean: $(addsuffix /clean, $(TARGET_DIRS) $(ROM_DIRS))
recurse-install: $(addsuffix /install, $(TARGET_DIRS))
$(addsuffix /install, $(TARGET_DIRS)): all

$(BUILD_DIR)/version.o: $(SRC_PATH)/version.rc config-host.h
	$(call quiet-command,$(WINDRES) -I$(BUILD_DIR) -o $@ $<,"RC","version.o")

Makefile: $(version-obj-y)

######################################################################
# Build libraries

libqemuutil.a: $(util-obj-y) $(trace-obj-y) $(stub-obj-y)
libvhost-user.a: $(libvhost-user-obj-y) $(util-obj-y) $(stub-obj-y)

######################################################################

COMMON_LDADDS = libqemuutil.a

qemu-img.o: qemu-img-cmds.h

qemu-img$(EXESUF): qemu-img.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-nbd$(EXESUF): qemu-nbd.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-io$(EXESUF): qemu-io.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)

qemu-bridge-helper$(EXESUF): qemu-bridge-helper.o $(COMMON_LDADDS)

qemu-keymap$(EXESUF): qemu-keymap.o ui/input-keymap.o $(COMMON_LDADDS)

qemu-edid$(EXESUF): qemu-edid.o hw/display/edid-generate.o $(COMMON_LDADDS)

fsdev/virtfs-proxy-helper$(EXESUF): fsdev/virtfs-proxy-helper.o fsdev/9p-marshal.o fsdev/9p-iov-marshal.o $(COMMON_LDADDS)
fsdev/virtfs-proxy-helper$(EXESUF): LIBS += -lcap

scsi/qemu-pr-helper$(EXESUF): scsi/qemu-pr-helper.o scsi/utils.o $(authz-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
ifdef CONFIG_MPATH
scsi/qemu-pr-helper$(EXESUF): LIBS += -ludev -lmultipath -lmpathpersist
endif

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$@")

qemu-ga$(EXESUF): LIBS = $(LIBS_QGA)
qemu-ga$(EXESUF): QEMU_CFLAGS += -I qga/qapi-generated

qemu-keymap$(EXESUF): LIBS += $(XKBCOMMON_LIBS)
qemu-keymap$(EXESUF): QEMU_CFLAGS += $(XKBCOMMON_CFLAGS)

qapi-py = $(SRC_PATH)/scripts/qapi/__init__.py \
$(SRC_PATH)/scripts/qapi/commands.py \
$(SRC_PATH)/scripts/qapi/common.py \
$(SRC_PATH)/scripts/qapi/doc.py \
$(SRC_PATH)/scripts/qapi/error.py \
$(SRC_PATH)/scripts/qapi/events.py \
$(SRC_PATH)/scripts/qapi/expr.py \
$(SRC_PATH)/scripts/qapi/gen.py \
$(SRC_PATH)/scripts/qapi/introspect.py \
$(SRC_PATH)/scripts/qapi/parser.py \
$(SRC_PATH)/scripts/qapi/schema.py \
$(SRC_PATH)/scripts/qapi/source.py \
$(SRC_PATH)/scripts/qapi/types.py \
$(SRC_PATH)/scripts/qapi/visit.py \
$(SRC_PATH)/scripts/qapi-gen.py

qga/qapi-generated/qga-qapi-types.c qga/qapi-generated/qga-qapi-types.h \
qga/qapi-generated/qga-qapi-visit.c qga/qapi-generated/qga-qapi-visit.h \
qga/qapi-generated/qga-qapi-commands.h qga/qapi-generated/qga-qapi-commands.c \
qga/qapi-generated/qga-qapi-doc.texi: \
qga/qapi-generated/qapi-gen-timestamp ;
qga/qapi-generated/qapi-gen-timestamp: $(SRC_PATH)/qga/qapi-schema.json $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-gen.py \
		-o qga/qapi-generated -p "qga-" $<, \
		"GEN","$(@:%-timestamp=%)")
	@>$@

qapi-modules = $(SRC_PATH)/qapi/qapi-schema.json \
               $(QAPI_MODULES:%=$(SRC_PATH)/qapi/%.json)

$(GENERATED_QAPI_FILES): qapi-gen-timestamp ;
qapi-gen-timestamp: $(qapi-modules) $(qapi-py)
	$(call quiet-command,$(PYTHON) $(SRC_PATH)/scripts/qapi-gen.py \
		-o "qapi" -b $<, \
		"GEN","$(@:%-timestamp=%)")
	@>$@

QGALIB_GEN=$(addprefix qga/qapi-generated/, qga-qapi-types.h qga-qapi-visit.h qga-qapi-commands.h)
$(qga-obj-y): $(QGALIB_GEN)

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

elf2dmp$(EXESUF): $(elf2dmp-obj-y)
	$(call LINK, $^)

ifdef CONFIG_IVSHMEM
ivshmem-client$(EXESUF): $(ivshmem-client-obj-y) $(COMMON_LDADDS)
	$(call LINK, $^)
ivshmem-server$(EXESUF): $(ivshmem-server-obj-y) $(COMMON_LDADDS)
	$(call LINK, $^)
endif
vhost-user-scsi$(EXESUF): $(vhost-user-scsi-obj-y) libvhost-user.a
	$(call LINK, $^)
vhost-user-blk$(EXESUF): $(vhost-user-blk-obj-y) libvhost-user.a
	$(call LINK, $^)

rdmacm-mux$(EXESUF): LIBS += "-libumad"
rdmacm-mux$(EXESUF): $(rdmacm-mux-obj-y) $(COMMON_LDADDS)
	$(call LINK, $^)

vhost-user-gpu$(EXESUF): $(vhost-user-gpu-obj-y) $(libvhost-user-obj-y) libqemuutil.a libqemustub.a
	$(call LINK, $^)

ifdef CONFIG_VHOST_USER_INPUT
ifdef CONFIG_LINUX
vhost-user-input$(EXESUF): $(vhost-user-input-obj-y) libvhost-user.a libqemuutil.a
	$(call LINK, $^)

# build by default, do not install
all: vhost-user-input$(EXESUF)
endif
endif

module_block.h: $(SRC_PATH)/scripts/modules/module_block.py config-host.mak
	$(call quiet-command,$(PYTHON) $< $@ \
	$(addprefix $(SRC_PATH)/,$(patsubst %.mo,%.c,$(block-obj-m))), \
	"GEN","$@")

ifdef CONFIG_GCOV
.PHONY: clean-coverage
clean-coverage:
	$(call quiet-command, \
		find . \( -name '*.gcda' -o -name '*.gcov' \) -type f -exec rm {} +, \
		"CLEAN", "coverage files")
endif

clean: recurse-clean
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f qemu-options.def
	rm -f *.msi
	find . \( -name '*.so' -o -name '*.dll' -o -name '*.mo' -o -name '*.[oda]' \) -type f \
		! -path ./roms/edk2/ArmPkg/Library/GccLto/liblto-aarch64.a \
		! -path ./roms/edk2/ArmPkg/Library/GccLto/liblto-arm.a \
		! -path ./roms/edk2/BaseTools/Source/Python/UPT/Dll/sqlite3.dll \
		-exec rm {} +
	rm -f $(edk2-decompressed)
	rm -f $(filter-out %.tlb,$(TOOLS)) $(HELPERS-y) TAGS cscope.* *.pod *~ */*~
	rm -f fsdev/*.pod scsi/*.pod
	rm -f qemu-img-cmds.h
	rm -f ui/shader/*-vert.h ui/shader/*-frag.h
	@# May not be present in generated-files-y
	rm -f trace/generated-tracers-dtrace.dtrace*
	rm -f trace/generated-tracers-dtrace.h*
	rm -f $(foreach f,$(generated-files-y),$(f) $(f)-timestamp)
	rm -f qapi-gen-timestamp
	rm -rf qga/qapi-generated
	rm -f config-all-devices.mak

VERSION ?= $(shell cat VERSION)

dist: qemu-$(VERSION).tar.bz2

qemu-%.tar.bz2:
	$(SRC_PATH)/scripts/make-release "$(SRC_PATH)" "$(patsubst qemu-%.tar.bz2,%,$@)"

define clean-manual =
rm -rf $(MANUAL_BUILDDIR)/$1/_static
rm -f $(MANUAL_BUILDDIR)/$1/objects.inv $(MANUAL_BUILDDIR)/$1/searchindex.js $(MANUAL_BUILDDIR)/$1/*.html
endef

distclean: clean
	rm -f config-host.mak config-host.h* config-host.ld $(DOCS) qemu-options.texi qemu-img-cmds.texi qemu-monitor.texi qemu-monitor-info.texi
	rm -f tests/tcg/config-*.mak
	rm -f config-all-devices.mak config-all-disas.mak config.status
	rm -f $(SUBDIR_DEVICES_MAK)
	rm -f po/*.mo tests/qemu-iotests/common.env
	rm -f roms/seabios/config.mak roms/vgabios/config.mak
	rm -f qemu-doc.info qemu-doc.aux qemu-doc.cp qemu-doc.cps
	rm -f qemu-doc.fn qemu-doc.fns qemu-doc.info qemu-doc.ky qemu-doc.kys
	rm -f qemu-doc.log qemu-doc.pdf qemu-doc.pg qemu-doc.toc qemu-doc.tp
	rm -f qemu-doc.vr qemu-doc.txt
	rm -f qemu-plugins-ld.symbols qemu-plugins-ld64.symbols
	rm -f config.log
	rm -f linux-headers/asm
	rm -f docs/version.texi
	rm -f docs/interop/qemu-ga-qapi.texi docs/interop/qemu-qmp-qapi.texi
	rm -f docs/interop/qemu-qmp-ref.7 docs/interop/qemu-ga-ref.7
	rm -f docs/interop/qemu-qmp-ref.txt docs/interop/qemu-ga-ref.txt
	rm -f docs/interop/qemu-qmp-ref.pdf docs/interop/qemu-ga-ref.pdf
	rm -f docs/interop/qemu-qmp-ref.html docs/interop/qemu-ga-ref.html
	rm -f docs/qemu-block-drivers.7
	rm -f docs/qemu-cpu-models.7
	rm -rf .doctrees
	$(call clean-manual,devel)
	$(call clean-manual,interop)
	$(call clean-manual,specs)
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done
	rm -Rf .sdk
	if test -f dtc/version_gen.h; then $(MAKE) $(DTC_MAKE_ARGS) clean; fi

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
de-ch  es     fo  fr-ca  hu     ja  mk  pt  sl     tr \
bepo    cz

ifdef INSTALL_BLOBS
BLOBS=bios.bin bios-256k.bin bios-microvm.bin sgabios.bin vgabios.bin vgabios-cirrus.bin \
vgabios-stdvga.bin vgabios-vmware.bin vgabios-qxl.bin vgabios-virtio.bin \
vgabios-ramfb.bin vgabios-bochs-display.bin vgabios-ati.bin \
ppc_rom.bin openbios-sparc32 openbios-sparc64 openbios-ppc QEMU,tcx.bin QEMU,cgthree.bin \
pxe-e1000.rom pxe-eepro100.rom pxe-ne2k_pci.rom \
pxe-pcnet.rom pxe-rtl8139.rom pxe-virtio.rom \
efi-e1000.rom efi-eepro100.rom efi-ne2k_pci.rom \
efi-pcnet.rom efi-rtl8139.rom efi-virtio.rom \
efi-e1000e.rom efi-vmxnet3.rom \
qemu-nsis.bmp \
bamboo.dtb canyonlands.dtb petalogix-s3adsp1800.dtb petalogix-ml605.dtb \
multiboot.bin linuxboot.bin linuxboot_dma.bin kvmvapic.bin pvh.bin \
s390-ccw.img s390-netboot.img \
slof.bin skiboot.lid \
palcode-clipper \
u-boot.e500 u-boot-sam460-20100605.bin \
qemu_vga.ndrv \
edk2-licenses.txt \
hppa-firmware.img \
opensbi-riscv32-virt-fw_jump.bin \
opensbi-riscv64-sifive_u-fw_jump.bin opensbi-riscv64-virt-fw_jump.bin


DESCS=50-edk2-i386-secure.json 50-edk2-x86_64-secure.json \
60-edk2-aarch64.json 60-edk2-arm.json 60-edk2-i386.json 60-edk2-x86_64.json
else
BLOBS=
DESCS=
endif

# Note that we manually filter-out the non-Sphinx documentation which
# is currently built into the docs/interop directory in the build tree,
# and also any sphinx-built manpages.
define install-manual =
for d in $$(cd $(MANUAL_BUILDDIR) && find $1 -type d); do $(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)/$$d"; done
for f in $$(cd $(MANUAL_BUILDDIR) && find $1 -type f -a '!' '(' -name '*.[0-9]' -o -name 'qemu-*-qapi.*' -o -name 'qemu-*-ref.*' ')' ); do $(INSTALL_DATA) "$(MANUAL_BUILDDIR)/$$f" "$(DESTDIR)$(qemu_docdir)/$$f"; done
endef

# Note that we deliberately do not install the "devel" manual: it is
# for QEMU developers, and not interesting to our users.
.PHONY: install-sphinxdocs
install-sphinxdocs: sphinxdocs
	$(call install-manual,interop)
	$(call install-manual,specs)

install-doc: $(DOCS) install-sphinxdocs
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) qemu-doc.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) qemu-doc.txt "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/interop/qemu-qmp-ref.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/interop/qemu-qmp-ref.txt "$(DESTDIR)$(qemu_docdir)"
ifdef CONFIG_POSIX
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) qemu.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) docs/interop/qemu-qmp-ref.7 "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) docs/qemu-block-drivers.7 "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) docs/qemu-cpu-models.7 "$(DESTDIR)$(mandir)/man7"
ifeq ($(CONFIG_TOOLS),y)
	$(INSTALL_DATA) qemu-img.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) qemu-nbd.8 "$(DESTDIR)$(mandir)/man8"
endif
ifdef CONFIG_TRACE_SYSTEMTAP
	$(INSTALL_DATA) scripts/qemu-trace-stap.1 "$(DESTDIR)$(mandir)/man1"
endif
ifneq (,$(findstring qemu-ga,$(TOOLS)))
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/interop/qemu-ga.8 "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) docs/interop/qemu-ga-ref.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/interop/qemu-ga-ref.txt "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) docs/interop/qemu-ga-ref.7 "$(DESTDIR)$(mandir)/man7"
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

ICON_SIZES=16x16 24x24 32x32 48x48 64x64 128x128 256x256 512x512

install-includedir:
	$(INSTALL_DIR) "$(DESTDIR)$(includedir)"

install: all $(if $(BUILD_DOCS),install-doc) \
	install-datadir install-localstatedir install-includedir \
	$(if $(INSTALL_BLOBS),$(edk2-decompressed)) \
	recurse-install
ifneq ($(TOOLS),)
	$(call install-prog,$(TOOLS),$(DESTDIR)$(bindir))
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
ifneq ($(vhost-user-json-y),)
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)/vhost-user/"
	for x in $(vhost-user-json-y); do \
		$(INSTALL_DATA) $$x "$(DESTDIR)$(qemu_datadir)/vhost-user/"; \
	done
endif
ifdef CONFIG_TRACE_SYSTEMTAP
	$(INSTALL_PROG) "scripts/qemu-trace-stap" $(DESTDIR)$(bindir)
endif
ifneq ($(BLOBS),)
	set -e; for x in $(BLOBS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/$$x "$(DESTDIR)$(qemu_datadir)"; \
	done
endif
ifdef INSTALL_BLOBS
	set -e; for x in $(edk2-decompressed); do \
		$(INSTALL_DATA) $$x "$(DESTDIR)$(qemu_datadir)"; \
	done
endif
ifneq ($(DESCS),)
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)/firmware"
	set -e; tmpf=$$(mktemp); trap 'rm -f -- "$$tmpf"' EXIT; \
	for x in $(DESCS); do \
		sed -e 's,@DATADIR@,$(qemu_datadir),' \
			"$(SRC_PATH)/pc-bios/descriptors/$$x" > "$$tmpf"; \
		$(INSTALL_DATA) "$$tmpf" \
			"$(DESTDIR)$(qemu_datadir)/firmware/$$x"; \
	done
endif
	for s in $(ICON_SIZES); do \
		mkdir -p "$(DESTDIR)$(qemu_icondir)/hicolor/$${s}/apps"; \
		$(INSTALL_DATA) $(SRC_PATH)/ui/icons/qemu_$${s}.png \
			"$(DESTDIR)$(qemu_icondir)/hicolor/$${s}/apps/qemu.png"; \
	done; \
	mkdir -p "$(DESTDIR)$(qemu_icondir)/hicolor/32x32/apps"; \
	$(INSTALL_DATA) $(SRC_PATH)/ui/icons/qemu_32x32.bmp \
		"$(DESTDIR)$(qemu_icondir)/hicolor/32x32/apps/qemu.bmp"; \
	mkdir -p "$(DESTDIR)$(qemu_icondir)/hicolor/scalable/apps"; \
	$(INSTALL_DATA) $(SRC_PATH)/ui/icons/qemu.svg \
		"$(DESTDIR)$(qemu_icondir)/hicolor/scalable/apps/qemu.svg"
	mkdir -p "$(DESTDIR)$(qemu_desktopdir)"
	$(INSTALL_DATA) $(SRC_PATH)/ui/qemu.desktop \
		"$(DESTDIR)$(qemu_desktopdir)/qemu.desktop"
ifdef CONFIG_GTK
	$(MAKE) -C po $@
endif
ifeq ($(CONFIG_PLUGIN),y)
	$(INSTALL_DATA) $(SRC_PATH)/include/qemu/qemu-plugin.h "$(DESTDIR)$(includedir)/qemu-plugin.h"
endif
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)/keymaps"
	set -e; for x in $(KEYMAPS); do \
		$(INSTALL_DATA) $(SRC_PATH)/pc-bios/keymaps/$$x "$(DESTDIR)$(qemu_datadir)/keymaps"; \
	done
	$(INSTALL_DATA) $(BUILD_DIR)/trace-events-all "$(DESTDIR)$(qemu_datadir)/trace-events-all"

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

ui/shader.o: $(SRC_PATH)/ui/shader.c \
	ui/shader/texture-blit-vert.h \
	ui/shader/texture-blit-flip-vert.h \
	ui/shader/texture-blit-frag.h

# documentation
MAKEINFO=makeinfo
MAKEINFOINCLUDES= -I docs -I $(<D) -I $(@D)
MAKEINFOFLAGS=--no-split --number-sections $(MAKEINFOINCLUDES)
TEXI2PODFLAGS=$(MAKEINFOINCLUDES) -DVERSION="$(VERSION)" -DCONFDIR="$(qemu_confdir)"
TEXI2PDFFLAGS=$(if $(V),,--quiet) -I $(SRC_PATH) $(MAKEINFOINCLUDES)

docs/version.texi: $(SRC_PATH)/VERSION config-host.mak
	$(call quiet-command,(\
		echo "@set VERSION $(VERSION)" && \
		echo "@set CONFDIR $(qemu_confdir)" \
	)> $@,"GEN","$@")

%.html: %.texi docs/version.texi
	$(call quiet-command,LC_ALL=C $(MAKEINFO) $(MAKEINFOFLAGS) --no-headers \
	--html $< -o $@,"GEN","$@")

%.info: %.texi docs/version.texi
	$(call quiet-command,$(MAKEINFO) $(MAKEINFOFLAGS) $< -o $@,"GEN","$@")

%.txt: %.texi docs/version.texi
	$(call quiet-command,LC_ALL=C $(MAKEINFO) $(MAKEINFOFLAGS) --no-headers \
	--plaintext $< -o $@,"GEN","$@")

%.pdf: %.texi docs/version.texi
	$(call quiet-command,texi2pdf $(TEXI2PDFFLAGS) $< -o $@,"GEN","$@")

# Sphinx builds all its documentation at once in one invocation
# and handles "don't rebuild things unless necessary" itself.
# The '.doctrees' files are cached information to speed this up.
.PHONY: sphinxdocs
sphinxdocs: $(MANUAL_BUILDDIR)/devel/index.html $(MANUAL_BUILDDIR)/interop/index.html $(MANUAL_BUILDDIR)/specs/index.html

# Canned command to build a single manual
# Arguments: $1 = manual name, $2 = Sphinx builder ('html' or 'man')
# Note the use of different doctree for each (manual, builder) tuple;
# this works around Sphinx not handling parallel invocation on
# a single doctree: https://github.com/sphinx-doc/sphinx/issues/2946
build-manual = $(call quiet-command,CONFDIR="$(qemu_confdir)" sphinx-build $(if $(V),,-q) -W -n -b $2 -D version=$(VERSION) -D release="$(FULL_VERSION)" -d .doctrees/$1-$2 $(SRC_PATH)/docs/$1 $(MANUAL_BUILDDIR)/$1 ,"SPHINX","$(MANUAL_BUILDDIR)/$1")
# We assume all RST files in the manual's directory are used in it
manual-deps = $(wildcard $(SRC_PATH)/docs/$1/*.rst) $(SRC_PATH)/docs/$1/conf.py $(SRC_PATH)/docs/conf.py

$(MANUAL_BUILDDIR)/devel/index.html: $(call manual-deps,devel)
	$(call build-manual,devel,html)

$(MANUAL_BUILDDIR)/interop/index.html: $(call manual-deps,interop)
	$(call build-manual,interop,html)

$(MANUAL_BUILDDIR)/specs/index.html: $(call manual-deps,specs)
	$(call build-manual,specs,html)

$(MANUAL_BUILDDIR)/interop/qemu-ga.8: $(call manual-deps,interop)
	$(call build-manual,interop,man)

qemu-options.texi: $(SRC_PATH)/qemu-options.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

qemu-monitor.texi: $(SRC_PATH)/hmp-commands.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

qemu-monitor-info.texi: $(SRC_PATH)/hmp-commands-info.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

qemu-img-cmds.texi: $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -t < $< > $@,"GEN","$@")

docs/interop/qemu-qmp-qapi.texi: qapi/qapi-doc.texi
	@cp -p $< $@

docs/interop/qemu-ga-qapi.texi: qga/qapi-generated/qga-qapi-doc.texi
	@cp -p $< $@

qemu.1: qemu-doc.texi qemu-options.texi qemu-monitor.texi qemu-monitor-info.texi
qemu.1: qemu-option-trace.texi
qemu-img.1: qemu-img.texi qemu-option-trace.texi qemu-img-cmds.texi
fsdev/virtfs-proxy-helper.1: fsdev/virtfs-proxy-helper.texi
qemu-nbd.8: qemu-nbd.texi qemu-option-trace.texi
docs/qemu-block-drivers.7: docs/qemu-block-drivers.texi
docs/qemu-cpu-models.7: docs/qemu-cpu-models.texi
scripts/qemu-trace-stap.1: scripts/qemu-trace-stap.texi

html: qemu-doc.html docs/interop/qemu-qmp-ref.html docs/interop/qemu-ga-ref.html sphinxdocs
info: qemu-doc.info docs/interop/qemu-qmp-ref.info docs/interop/qemu-ga-ref.info
pdf: qemu-doc.pdf docs/interop/qemu-qmp-ref.pdf docs/interop/qemu-ga-ref.pdf
txt: qemu-doc.txt docs/interop/qemu-qmp-ref.txt docs/interop/qemu-ga-ref.txt

qemu-doc.html qemu-doc.info qemu-doc.pdf qemu-doc.txt: \
	qemu-img.texi qemu-nbd.texi qemu-options.texi \
	qemu-tech.texi qemu-option-trace.texi \
	qemu-deprecated.texi qemu-monitor.texi qemu-img-cmds.texi \
	qemu-monitor-info.texi docs/qemu-block-drivers.texi \
	docs/qemu-cpu-models.texi docs/security.texi

docs/interop/qemu-ga-ref.dvi docs/interop/qemu-ga-ref.html \
    docs/interop/qemu-ga-ref.info docs/interop/qemu-ga-ref.pdf \
    docs/interop/qemu-ga-ref.txt docs/interop/qemu-ga-ref.7: \
	docs/interop/qemu-ga-ref.texi docs/interop/qemu-ga-qapi.texi

docs/interop/qemu-qmp-ref.dvi docs/interop/qemu-qmp-ref.html \
    docs/interop/qemu-qmp-ref.info docs/interop/qemu-qmp-ref.pdf \
    docs/interop/qemu-qmp-ref.txt docs/interop/qemu-qmp-ref.7: \
	docs/interop/qemu-qmp-ref.texi docs/interop/qemu-qmp-qapi.texi

$(filter %.1 %.7 %.8,$(DOCS)): scripts/texi2pod.pl

# Reports/Analysis

%/coverage-report.html:
	@mkdir -p $*
	$(call quiet-command,\
		gcovr -r $(SRC_PATH) \
		$(foreach t, $(TARGET_DIRS), --object-directory $(BUILD_DIR)/$(t)) \
		 --object-directory $(BUILD_DIR) \
		-p --html --html-details -o $@, \
		"GEN", "coverage-report.html")

.PHONY: coverage-report
coverage-report: $(CURDIR)/reports/coverage/coverage-report.html

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

$(INSTALLER): install-doc $(SRC_PATH)/qemu.nsi
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
ifneq ($(wildcard config-host.mak),)
ifneq ($(filter-out $(UNCHECKED_GOALS),$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
Makefile: $(generated-files-y)
endif
endif

.SECONDARY: $(TRACE_HEADERS) $(TRACE_HEADERS:%=%-timestamp) \
	$(TRACE_SOURCES) $(TRACE_SOURCES:%=%-timestamp) \
	$(TRACE_DTRACE) $(TRACE_DTRACE:%=%-timestamp)

# Include automatically generated dependency files
# Dependencies in Makefile.objs files come from our recursive subdir rules
-include $(wildcard *.d tests/*.d)

include $(SRC_PATH)/tests/docker/Makefile.include
include $(SRC_PATH)/tests/vm/Makefile.include

.PHONY: help
help:
	@echo  'Generic targets:'
	@echo  '  all             - Build all'
ifdef CONFIG_MODULES
	@echo  '  modules         - Build all modules'
endif
	@echo  '  dir/file.o      - Build specified target only'
	@echo  '  install         - Install QEMU, documentation and tools'
	@echo  '  ctags/TAGS      - Generate tags file for editors'
	@echo  '  cscope          - Generate cscope index'
	@echo  ''
	@$(if $(TARGET_DIRS), \
		echo 'Architecture specific targets:'; \
		$(foreach t, $(TARGET_DIRS), \
		printf "  %-30s - Build for %s\\n" $(t)/all $(t);) \
		echo '')
	@echo  'Cleaning targets:'
	@echo  '  clean           - Remove most generated files but keep the config'
ifdef CONFIG_GCOV
	@echo  '  clean-coverage  - Remove coverage files'
endif
	@echo  '  distclean       - Remove all generated files'
	@echo  '  dist            - Build a distributable tarball'
	@echo  ''
	@echo  'Test targets:'
	@echo  '  check           - Run all tests (check-help for details)'
	@echo  '  docker          - Help about targets running tests inside containers'
	@echo  '  vm-help         - Help about targets running tests inside VM'
	@echo  ''
	@echo  'Documentation targets:'
	@echo  '  html info pdf txt'
	@echo  '                  - Build documentation in specified format'
ifdef CONFIG_GCOV
	@echo  '  coverage-report - Create code coverage report'
endif
	@echo  ''
ifdef CONFIG_WIN32
	@echo  'Windows targets:'
	@echo  '  installer       - Build NSIS-based installer for QEMU'
ifdef QEMU_GA_MSI_ENABLED
	@echo  '  msi             - Build MSI-based installer for qemu-ga'
endif
	@echo  ''
endif
	@echo  '  $(MAKE) [targets]      (quiet build, default)'
	@echo  '  $(MAKE) V=1 [targets]  (verbose build)'
