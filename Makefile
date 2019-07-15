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

export NINJA=./ninjatool

# Running meson regenerates both build.ninja and ninjatool, and that is
# enough to prime the rest of the build.
ninjatool: build.ninja

# Only needed in case Makefile.ninja does not exist.
.PHONY: ninja-clean ninja-distclean clean-ctlist
clean-ctlist:
ninja-clean::
ninja-distclean::
build.ninja: config-host.mak

Makefile.ninja: build.ninja ninjatool
	./ninjatool -t ninja2make --omit clean dist uninstall < $< > $@
-include Makefile.ninja

${ninja-targets-c_COMPILER} ${ninja-targets-cpp_COMPILER}: .var.command += -MP

# If MESON is empty, the rule will be re-evaluated after Makefiles are
# reread (and MESON won't be empty anymore).
ifneq ($(MESON),)
Makefile.mtest: build.ninja scripts/mtest2make.py
	$(MESON) introspect --tests | $(PYTHON) scripts/mtest2make.py > $@
-include Makefile.mtest
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
	@if test -f meson-private/coredata.dat; then \
	  ./config.status --skip-meson; \
	else \
	  ./config.status; \
	fi

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

generated-files-y += module_block.h

generated-files-y += .git-submodule-status

KEYCODEMAP_GEN = $(SRC_PATH)/ui/keycodemapdb/tools/keymap-gen
KEYCODEMAP_CSV = $(SRC_PATH)/ui/keycodemapdb/data/keymaps.csv

KEYCODEMAP_FILES = \
		 ui/input-keymap-atset1-to-qcode.c.inc \
		 ui/input-keymap-linux-to-qcode.c.inc \
		 ui/input-keymap-qcode-to-atset1.c.inc \
		 ui/input-keymap-qcode-to-atset2.c.inc \
		 ui/input-keymap-qcode-to-atset3.c.inc \
		 ui/input-keymap-qcode-to-linux.c.inc \
		 ui/input-keymap-qcode-to-qnum.c.inc \
		 ui/input-keymap-qcode-to-sun.c.inc \
		 ui/input-keymap-qnum-to-qcode.c.inc \
		 ui/input-keymap-usb-to-qcode.c.inc \
		 ui/input-keymap-win32-to-qcode.c.inc \
		 ui/input-keymap-x11-to-qcode.c.inc \
		 ui/input-keymap-xorgevdev-to-qcode.c.inc \
		 ui/input-keymap-xorgkbd-to-qcode.c.inc \
		 ui/input-keymap-xorgxquartz-to-qcode.c.inc \
		 ui/input-keymap-xorgxwin-to-qcode.c.inc \
		 ui/input-keymap-osx-to-qcode.c.inc \
		 $(NULL)

generated-files-$(CONFIG_SOFTMMU) += $(KEYCODEMAP_FILES)

ui/input-keymap-%.c.inc: $(KEYCODEMAP_GEN) $(KEYCODEMAP_CSV) $(SRC_PATH)/ui/Makefile.objs
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

HELPERS-y = $(HELPERS)

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
DOCS+=$(MANUAL_BUILDDIR)/system/qemu.1
DOCS+=$(MANUAL_BUILDDIR)/tools/qemu-img.1
DOCS+=$(MANUAL_BUILDDIR)/tools/qemu-nbd.8
DOCS+=$(MANUAL_BUILDDIR)/interop/qemu-ga.8
ifeq ($(CONFIG_LINUX)$(CONFIG_SECCOMP)$(CONFIG_LIBCAP_NG),yyy)
DOCS+=$(MANUAL_BUILDDIR)/tools/virtiofsd.1
endif
DOCS+=$(MANUAL_BUILDDIR)/system/qemu-block-drivers.7
DOCS+=docs/interop/qemu-qmp-ref.html docs/interop/qemu-qmp-ref.txt docs/interop/qemu-qmp-ref.7
DOCS+=docs/interop/qemu-ga-ref.html docs/interop/qemu-ga-ref.txt docs/interop/qemu-ga-ref.7
DOCS+=$(MANUAL_BUILDDIR)/system/qemu-cpu-models.7
DOCS+=$(MANUAL_BUILDDIR)/index.html
ifdef CONFIG_VIRTFS
DOCS+=$(MANUAL_BUILDDIR)/tools/virtfs-proxy-helper.1
endif
ifdef CONFIG_TRACE_SYSTEMTAP
DOCS+=$(MANUAL_BUILDDIR)/tools/qemu-trace-stap.1
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
    $@ $*/config-devices.mak.d $< $(SRC_PATH)/Kconfig \
    CONFIG_TCG=$(CONFIG_TCG) \
    CONFIG_KVM=$(CONFIG_KVM) \
    CONFIG_SPICE=$(CONFIG_SPICE) \
    CONFIG_IVSHMEM=$(CONFIG_IVSHMEM) \
    CONFIG_TPM=$(CONFIG_TPM) \
    CONFIG_XEN=$(CONFIG_XEN) \
    CONFIG_OPENGL=$(CONFIG_OPENGL) \
    CONFIG_X11=$(CONFIG_X11) \
    CONFIG_VHOST_USER=$(CONFIG_VHOST_USER) \
    CONFIG_VHOST_KERNEL=$(CONFIG_VHOST_KERNEL) \
    CONFIG_VIRTFS=$(CONFIG_VIRTFS) \
    CONFIG_LINUX=$(CONFIG_LINUX) \
    CONFIG_PVRDMA=$(CONFIG_PVRDMA)

MINIKCONF = $(PYTHON) $(SRC_PATH)/scripts/minikconf.py

$(SUBDIR_DEVICES_MAK): %/config-devices.mak: default-configs/%.mak $(SRC_PATH)/Kconfig $(BUILD_DIR)/config-host.mak
	$(call quiet-command, $(MINIKCONF) $(MINIKCONF_ARGS) \
		> $@.tmp, "GEN", "$@.tmp")
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
                authz-obj-y \
                chardev-obj-y \
                block-obj-y \
                block-obj-m \
                storage-daemon-obj-y \
                storage-daemon-obj-m \
                crypto-obj-y \
                qom-obj-y \
                io-obj-y \
                common-obj-y \
                common-obj-m)

include $(SRC_PATH)/tests/Makefile.include

all: $(DOCS) $(if $(BUILD_DOCS),sphinxdocs) $(TOOLS) $(HELPERS-y) recurse-all modules

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

TARGET_DIRS_RULES := $(foreach t, all fuzz clean install, $(addsuffix /$(t), $(TARGET_DIRS)))

SOFTMMU_ALL_RULES=$(filter %-softmmu/all, $(TARGET_DIRS_RULES))
$(SOFTMMU_ALL_RULES): $(authz-obj-y)
$(SOFTMMU_ALL_RULES): $(block-obj-y)
$(SOFTMMU_ALL_RULES): $(storage-daemon-obj-y)
$(SOFTMMU_ALL_RULES): $(chardev-obj-y)
$(SOFTMMU_ALL_RULES): $(crypto-obj-y)
$(SOFTMMU_ALL_RULES): $(io-obj-y)
$(SOFTMMU_ALL_RULES): config-all-devices.mak
ifdef DECOMPRESS_EDK2_BLOBS
$(SOFTMMU_ALL_RULES): $(edk2-decompressed)
endif

SOFTMMU_FUZZ_RULES=$(filter %-softmmu/fuzz, $(TARGET_DIRS_RULES))
$(SOFTMMU_FUZZ_RULES): $(authz-obj-y)
$(SOFTMMU_FUZZ_RULES): $(block-obj-y)
$(SOFTMMU_FUZZ_RULES): $(chardev-obj-y)
$(SOFTMMU_FUZZ_RULES): $(crypto-obj-y)
$(SOFTMMU_FUZZ_RULES): $(io-obj-y)
$(SOFTMMU_FUZZ_RULES): config-all-devices.mak
$(SOFTMMU_FUZZ_RULES): $(edk2-decompressed)

.PHONY: $(TARGET_DIRS_RULES)
# The $(TARGET_DIRS_RULES) are of the form SUBDIR/GOAL, so that
# $(dir $@) yields the sub-directory, and $(notdir $@) yields the sub-goal
$(TARGET_DIRS_RULES):
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $(dir $@) V="$(V)" TARGET_DIR="$(dir $@)" $(notdir $@),)

# LIBFDT_lib="": avoid breaking existing trees with objects requiring -fPIC
DTC_MAKE_ARGS=-I$(SRC_PATH)/dtc VPATH=$(SRC_PATH)/dtc -C dtc V="$(V)" LIBFDT_lib=""
DTC_CFLAGS=$(CFLAGS) $(QEMU_CFLAGS)
DTC_CPPFLAGS=-I$(SRC_PATH)/dtc/libfdt

.PHONY: dtc/all
dtc/all: .git-submodule-status dtc/libfdt
	$(call quiet-command,$(MAKE) $(DTC_MAKE_ARGS) CPPFLAGS="$(DTC_CPPFLAGS)" CFLAGS="$(DTC_CFLAGS)" LDFLAGS="$(QEMU_LDFLAGS)" ARFLAGS="$(ARFLAGS)" CC="$(CC)" AR="$(AR)" LD="$(LD)" $(SUBDIR_MAKEFLAGS) libfdt,)

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
		CFLAGS="$(QEMU_CFLAGS) $(CFLAGS)" LDFLAGS="$(QEMU_LDFLAGS)")

$(filter %/all, $(TARGET_DIRS_RULES)): libqemuutil.a $(common-obj-y) \
	$(qom-obj-y)

$(filter %/fuzz, $(TARGET_DIRS_RULES)): libqemuutil.a $(common-obj-y) \
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

COMMON_LDADDS = libqemuutil.a

qemu-img.o: qemu-img-cmds.h

qemu-img$(EXESUF): qemu-img.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-nbd$(EXESUF): qemu-nbd.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-io$(EXESUF): qemu-io.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
qemu-storage-daemon$(EXESUF): qemu-storage-daemon.o $(authz-obj-y) $(block-obj-y) $(crypto-obj-y) $(chardev-obj-y) $(io-obj-y) $(qom-obj-y) $(storage-daemon-obj-y) $(COMMON_LDADDS)

fsdev/virtfs-proxy-helper$(EXESUF): fsdev/virtfs-proxy-helper.o fsdev/9p-marshal.o fsdev/9p-iov-marshal.o $(COMMON_LDADDS)

scsi/qemu-pr-helper$(EXESUF): scsi/qemu-pr-helper.o scsi/utils.o $(authz-obj-y) $(crypto-obj-y) $(io-obj-y) $(qom-obj-y) $(COMMON_LDADDS)
ifdef CONFIG_MPATH
scsi/qemu-pr-helper$(EXESUF): LIBS += -ludev -lmultipath -lmpathpersist
endif

qemu-img-cmds.h: $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$@")

module_block.h: $(SRC_PATH)/scripts/modules/module_block.py config-host.mak
	$(call quiet-command,$(PYTHON) $< $@ \
	$(addprefix $(SRC_PATH)/,$(patsubst %.mo,%.c,$(block-obj-m))), \
	"GEN","$@")

clean: recurse-clean ninja-clean clean-ctlist
	-test -f ninjatool && ./ninjatool $(if $(V),-v,) -t clean
# avoid old build problems by removing potentially incorrect old files
	rm -f config.mak op-i386.h opc-i386.h gen-op-i386.h op-arm.h opc-arm.h gen-op-arm.h
	rm -f qemu-options.def
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
	rm -f $(foreach f,$(generated-files-y),$(f) $(f)-timestamp)
	rm -f config-all-devices.mak
	rm -f $(SUBDIR_DEVICES_MAK)

VERSION ?= $(shell cat VERSION)

dist: qemu-$(VERSION).tar.bz2

qemu-%.tar.bz2:
	$(SRC_PATH)/scripts/make-release "$(SRC_PATH)" "$(patsubst qemu-%.tar.bz2,%,$@)"

define clean-manual =
rm -rf $(MANUAL_BUILDDIR)/$1/_static
rm -f $(MANUAL_BUILDDIR)/$1/objects.inv $(MANUAL_BUILDDIR)/$1/searchindex.js $(MANUAL_BUILDDIR)/$1/*.html
endef

distclean: clean ninja-distclean
	-test -f ninjatool && ./ninjatool $(if $(V),-v,) -t clean -g
	rm -f config-host.mak config-host.h* $(DOCS)
	rm -f tests/tcg/config-*.mak
	rm -f config-all-devices.mak config-all-disas.mak config.status
	rm -f $(SUBDIR_DEVICES_MAK)
	rm -f po/*.mo tests/qemu-iotests/common.env
	rm -f roms/seabios/config.mak roms/vgabios/config.mak
	rm -f qemu-plugins-ld.symbols qemu-plugins-ld64.symbols
	rm -rf meson-private meson-logs meson-info compile_commands.json
	rm -f Makefile.ninja ninjatool ninjatool.stamp Makefile.mtest
	rm -f config.log
	rm -f linux-headers/asm
	rm -f docs/version.texi
	rm -f docs/interop/qemu-ga-qapi.texi docs/interop/qemu-qmp-qapi.texi
	rm -f docs/interop/qemu-qmp-ref.7 docs/interop/qemu-ga-ref.7
	rm -f docs/interop/qemu-qmp-ref.txt docs/interop/qemu-ga-ref.txt
	rm -f docs/interop/qemu-qmp-ref.pdf docs/interop/qemu-ga-ref.pdf
	rm -f docs/interop/qemu-qmp-ref.html docs/interop/qemu-ga-ref.html
	rm -rf .doctrees
	$(call clean-manual,devel)
	$(call clean-manual,interop)
	$(call clean-manual,specs)
	$(call clean-manual,system)
	$(call clean-manual,tools)
	$(call clean-manual,user)
	for d in $(TARGET_DIRS); do \
	rm -rf $$d || exit 1 ; \
        done
	rm -Rf .sdk

KEYMAPS=da     en-gb  et  fr     fr-ch  is  lt  no  pt-br  sv \
ar      de     en-us  fi  fr-be  hr     it  lv  nl         pl  ru     th \
de-ch  es     fo  fr-ca  hu     ja  mk  pt  sl     tr \
bepo    cz

ifdef INSTALL_BLOBS
BLOBS=bios.bin bios-256k.bin bios-microvm.bin sgabios.bin vgabios.bin vgabios-cirrus.bin \
vgabios-stdvga.bin vgabios-vmware.bin vgabios-qxl.bin vgabios-virtio.bin \
vgabios-ramfb.bin vgabios-bochs-display.bin vgabios-ati.bin \
openbios-sparc32 openbios-sparc64 openbios-ppc QEMU,tcx.bin QEMU,cgthree.bin \
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
opensbi-riscv32-sifive_u-fw_jump.bin opensbi-riscv32-virt-fw_jump.bin \
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
	$(call install-manual,system)
	$(call install-manual,tools)
	$(call install-manual,user)

install-doc: $(DOCS) install-sphinxdocs
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/index.html "$(DESTDIR)$(qemu_docdir)"
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)/interop"
	$(INSTALL_DATA) docs/interop/qemu-qmp-ref.html "$(DESTDIR)$(qemu_docdir)/interop"
	$(INSTALL_DATA) docs/interop/qemu-qmp-ref.txt "$(DESTDIR)$(qemu_docdir)/interop"
ifdef CONFIG_POSIX
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/system/qemu.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) docs/interop/qemu-qmp-ref.7 "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/system/qemu-block-drivers.7 "$(DESTDIR)$(mandir)/man7"
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/system/qemu-cpu-models.7 "$(DESTDIR)$(mandir)/man7"
ifeq ($(CONFIG_TOOLS),y)
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/tools/qemu-img.1 "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/tools/qemu-nbd.8 "$(DESTDIR)$(mandir)/man8"
endif
ifdef CONFIG_TRACE_SYSTEMTAP
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/tools/qemu-trace-stap.1 "$(DESTDIR)$(mandir)/man1"
endif
ifeq ($(CONFIG_GUEST_AGENT),y)
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/interop/qemu-ga.8 "$(DESTDIR)$(mandir)/man8"
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_docdir)/interop"
	$(INSTALL_DATA) docs/interop/qemu-ga-ref.html "$(DESTDIR)$(qemu_docdir)/interop"
	$(INSTALL_DATA) docs/interop/qemu-ga-ref.txt "$(DESTDIR)$(qemu_docdir)/interop"
	$(INSTALL_DATA) docs/interop/qemu-ga-ref.7 "$(DESTDIR)$(mandir)/man7"
endif
endif
ifdef CONFIG_VIRTFS
	$(INSTALL_DIR) "$(DESTDIR)$(mandir)/man1"
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/tools/virtfs-proxy-helper.1 "$(DESTDIR)$(mandir)/man1"
endif
ifeq ($(CONFIG_LINUX)$(CONFIG_SECCOMP)$(CONFIG_LIBCAP_NG),yyy)
	$(INSTALL_DATA) $(MANUAL_BUILDDIR)/tools/virtiofsd.1 "$(DESTDIR)$(mandir)/man1"
endif

install-datadir:
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)"

install-localstatedir:
ifdef CONFIG_POSIX
ifeq ($(CONFIG_GUEST_AGENT),y)
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_localstatedir)"/run
endif
endif

ICON_SIZES=16x16 24x24 32x32 48x48 64x64 128x128 256x256 512x512

install-includedir:
	$(INSTALL_DIR) "$(DESTDIR)$(includedir)"

# Needed by "meson install"
export DESTDIR
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
	for d in $(TARGET_DIRS); do \
	$(MAKE) $(SUBDIR_MAKEFLAGS) TARGET_DIR=$$d/ -C $$d $@ || exit 1 ; \
        done

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
sphinxdocs: $(MANUAL_BUILDDIR)/devel/index.html \
            $(MANUAL_BUILDDIR)/interop/index.html \
            $(MANUAL_BUILDDIR)/specs/index.html \
            $(MANUAL_BUILDDIR)/system/index.html \
            $(MANUAL_BUILDDIR)/tools/index.html \
            $(MANUAL_BUILDDIR)/user/index.html

# Canned command to build a single manual
# Arguments: $1 = manual name, $2 = Sphinx builder ('html' or 'man')
# Note the use of different doctree for each (manual, builder) tuple;
# this works around Sphinx not handling parallel invocation on
# a single doctree: https://github.com/sphinx-doc/sphinx/issues/2946
build-manual = $(call quiet-command,CONFDIR="$(qemu_confdir)" $(SPHINX_BUILD) $(if $(V),,-q) $(SPHINX_WERROR) -b $2 -D version=$(VERSION) -D release="$(FULL_VERSION)" -d .doctrees/$1-$2 $(SRC_PATH)/docs/$1 $(MANUAL_BUILDDIR)/$1 ,"SPHINX","$(MANUAL_BUILDDIR)/$1")
# We assume all RST files in the manual's directory are used in it
manual-deps = $(wildcard $(SRC_PATH)/docs/$1/*.rst $(SRC_PATH)/docs/$1/*/*.rst) \
              $(SRC_PATH)/docs/defs.rst.inc \
              $(SRC_PATH)/docs/$1/conf.py $(SRC_PATH)/docs/conf.py \
              $(SRC_PATH)/docs/sphinx/*.py
# Macro to write out the rule and dependencies for building manpages
# Usage: $(call define-manpage-rule,manualname,manpage1 manpage2...[,extradeps])
# 'extradeps' is optional, and specifies extra files (eg .hx files) that
# the manual page depends on.
define define-manpage-rule
$(call atomic,$(foreach manpage,$2,$(MANUAL_BUILDDIR)/$1/$(manpage)),$(call manual-deps,$1) $3)
	$(call build-manual,$1,man)
endef

$(MANUAL_BUILDDIR)/devel/index.html: $(call manual-deps,devel)
	$(call build-manual,devel,html)

$(MANUAL_BUILDDIR)/interop/index.html: $(call manual-deps,interop)
	$(call build-manual,interop,html)

$(MANUAL_BUILDDIR)/specs/index.html: $(call manual-deps,specs)
	$(call build-manual,specs,html)

$(MANUAL_BUILDDIR)/system/index.html: $(call manual-deps,system) $(SRC_PATH)/hmp-commands.hx $(SRC_PATH)/hmp-commands-info.hx $(SRC_PATH)/qemu-options.hx
	$(call build-manual,system,html)

$(MANUAL_BUILDDIR)/tools/index.html: $(call manual-deps,tools) $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/docs/qemu-option-trace.rst.inc
	$(call build-manual,tools,html)

$(MANUAL_BUILDDIR)/user/index.html: $(call manual-deps,user)
	$(call build-manual,user,html)

$(call define-manpage-rule,interop,qemu-ga.8)

$(call define-manpage-rule,system,qemu.1 qemu-block-drivers.7 qemu-cpu-models.7)

$(call define-manpage-rule,tools,\
       qemu-img.1 qemu-nbd.8 qemu-trace-stap.1\
       virtiofsd.1 virtfs-proxy-helper.1,\
       $(SRC_PATH)/qemu-img-cmds.hx $(SRC_PATH)/docs/qemu-option-trace.rst.inc)

$(MANUAL_BUILDDIR)/index.html: $(SRC_PATH)/docs/index.html.in qemu-version.h
	@mkdir -p "$(MANUAL_BUILDDIR)"
	$(call quiet-command, sed "s|@@VERSION@@|${VERSION}|g" $< >$@, \
             "GEN","$@")

docs/interop/qemu-qmp-qapi.texi: qapi/qapi-doc.texi
	@cp -p $< $@

docs/interop/qemu-ga-qapi.texi: qga/qga-qapi-doc.texi
	@cp -p $< $@

html: docs/interop/qemu-qmp-ref.html docs/interop/qemu-ga-ref.html sphinxdocs
info: docs/interop/qemu-qmp-ref.info docs/interop/qemu-ga-ref.info
pdf: docs/interop/qemu-qmp-ref.pdf docs/interop/qemu-ga-ref.pdf
txt: docs/interop/qemu-qmp-ref.txt docs/interop/qemu-ga-ref.txt

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

$(INSTALLER): $(SRC_PATH)/qemu.nsi
	$(MAKE) install DESTDIR=${INSTDIR}
ifdef SIGNCODE
	(cd ${INSTDIR}/${bindir}; \
         for i in *.exe; do \
           $(SIGNCODE) $${i}; \
         done \
        )
endif # SIGNCODE
	(cd ${INSTDIR}/${bindir}; \
         for i in qemu-system-*.exe; do \
           arch=$${i%.exe}; \
           arch=$${arch#qemu-system-}; \
           echo Section \"$$arch\" Section_$$arch; \
           echo SetOutPath \"\$$INSTDIR\"; \
           echo File \"\$${BINDIR}\\$$i\"; \
           echo SectionEnd; \
         done \
        ) >${INSTDIR}/${bindir}/system-emulations.nsh
	makensis $(nsisflags) \
                $(if $(BUILD_DOCS),-DCONFIG_DOCUMENTATION="y") \
                $(if $(CONFIG_GTK),-DCONFIG_GTK="y") \
                -DBINDIR="${INSTDIR}/${bindir}" \
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

# Include automatically generated dependency files
# Dependencies in Makefile.objs files come from our recursive subdir rules
-include $(wildcard *.d tests/*.d)

include $(SRC_PATH)/tests/docker/Makefile.include
include $(SRC_PATH)/tests/vm/Makefile.include

print-help-run = printf "  %-30s - %s\\n" "$1" "$2"
print-help = $(quiet-@)$(call print-help-run,$1,$2)

.PHONY: help
help:
	@echo  'Generic targets:'
	$(call print-help,all,Build all)
ifdef CONFIG_MODULES
	$(call print-help,modules,Build all modules)
endif
	$(call print-help,dir/file.o,Build specified target only)
	$(call print-help,install,Install QEMU, documentation and tools)
	$(call print-help,ctags/TAGS,Generate tags file for editors)
	$(call print-help,cscope,Generate cscope index)
	$(call print-help,sparse,Run sparse on the QEMU source)
	@echo  ''
	@$(if $(TARGET_DIRS), \
		echo 'Architecture specific targets:'; \
		$(foreach t, $(TARGET_DIRS), \
		$(call print-help-run,$(t)/all,Build for $(t)); \
		$(if $(CONFIG_FUZZ), \
			$(if $(findstring softmmu,$(t)), \
				$(call print-help-run,$(t)/fuzz,Build fuzzer for $(t)); \
		))) \
		echo '')
	@$(if $(HELPERS-y), \
		echo 'Helper targets:'; \
		$(foreach t, $(HELPERS-y), \
		$(call print-help-run,$(t),Build $(shell basename $(t)));) \
		echo '')
	@$(if $(TOOLS), \
		echo 'Tools targets:'; \
		$(foreach t, $(TOOLS), \
		$(call print-help-run,$(t),Build $(shell basename $(t)) tool);) \
		echo '')
	@echo  'Cleaning targets:'
	$(call print-help,clean,Remove most generated files but keep the config)
	$(call print-help,distclean,Remove all generated files)
	$(call print-help,dist,Build a distributable tarball)
	@echo  ''
	@echo  'Test targets:'
	$(call print-help,check,Run all tests (check-help for details))
	$(call print-help,docker,Help about targets running tests inside containers)
	$(call print-help,vm-help,Help about targets running tests inside VM)
	@echo  ''
	@echo  'Documentation targets:'
	$(call print-help,html info pdf txt,Build documentation in specified format)
	@echo  ''
ifdef CONFIG_WIN32
	@echo  'Windows targets:'
	$(call print-help,installer,Build NSIS-based installer for QEMU)
ifdef QEMU_GA_MSI_ENABLED
	$(call print-help,msi,Build MSI-based installer for qemu-ga)
endif
	@echo  ''
endif
	$(call print-help,$(MAKE) [targets],(quiet build, default))
	$(call print-help,$(MAKE) V=1 [targets],(verbose build))
