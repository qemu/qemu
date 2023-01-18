# Makefile for QEMU.

ifneq ($(words $(subst :, ,$(CURDIR))), 1)
  $(error main directory cannot contain spaces nor colons)
endif

# Always point to the root of the build tree (needs GNU make).
BUILD_DIR=$(CURDIR)

# Before including a proper config-host.mak, assume we are in the source tree
SRC_PATH=.

# Don't use implicit rules or variables
# we have explicit rules for everything
MAKEFLAGS += -rR

SHELL = bash -o pipefail

# Usage: $(call quiet-command,command and args,"NAME","args to print")
# This will run "command and args", and either:
#  if V=1 just print the whole command and args
#  otherwise print the 'quiet' output in the format "  NAME     args to print"
# NAME should be a short name of the command, 7 letters or fewer.
# If called with only a single argument, will print nothing in quiet mode.
quiet-command-run = $(if $(V),,$(if $2,printf "  %-7s %s\n" $2 $3 && ))$1
quiet-@ = $(if $(V),,@)
quiet-command = $(quiet-@)$(call quiet-command-run,$1,$2,$3)

UNCHECKED_GOALS := %clean TAGS cscope ctags dist \
    help check-help print-% \
    docker docker-% vm-help vm-test vm-build-%

all:
.PHONY: all clean distclean recurse-all dist msi FORCE

# Don't try to regenerate Makefile or configure
# We don't generate any of them
Makefile: ;
configure: ;

# All following code might depend on configuration variables
ifneq ($(wildcard config-host.mak),)
include config-host.mak

include Makefile.prereqs
Makefile.prereqs: config-host.mak

git-submodule-update:
.git-submodule-status: git-submodule-update config-host.mak
Makefile: .git-submodule-status

.PHONY: git-submodule-update
git-submodule-update:
ifneq ($(GIT_SUBMODULES_ACTION),ignore)
	$(call quiet-command, \
		(GIT="$(GIT)" "$(SRC_PATH)/scripts/git-submodule.sh" $(GIT_SUBMODULES_ACTION) $(GIT_SUBMODULES)), \
		"GIT","$(GIT_SUBMODULES)")
endif

# 0. ensure the build tree is okay

# Check that we're not trying to do an out-of-tree build from
# a tree that's been used for an in-tree build.
ifneq ($(realpath $(SRC_PATH)),$(realpath .))
ifneq ($(wildcard $(SRC_PATH)/config-host.mak),)
$(error This is an out of tree build but your source tree ($(SRC_PATH)) \
seems to have been used for an in-tree build. You can fix this by running \
"$(MAKE) distclean && rm -rf *-linux-user *-softmmu" in your source tree)
endif
endif

# force a rerun of configure if config-host.mak is too old or corrupted
ifeq ($(MESON),)
.PHONY: config-host.mak
x := $(shell rm -rf meson-private meson-info meson-logs)
endif
ifeq ($(NINJA),)
.PHONY: config-host.mak
x := $(shell rm -rf meson-private meson-info meson-logs)
else
export NINJA
endif
ifeq ($(wildcard build.ninja),)
.PHONY: config-host.mak
x := $(shell rm -rf meson-private meson-info meson-logs)
endif
ifeq ($(origin prefix),file)
.PHONY: config-host.mak
x := $(shell rm -rf meson-private meson-info meson-logs)
endif

# 1. ensure config-host.mak is up-to-date
config-host.mak: $(SRC_PATH)/configure $(SRC_PATH)/scripts/meson-buildoptions.sh $(SRC_PATH)/VERSION
	@echo config-host.mak is out-of-date, running configure
	@if test -f meson-private/coredata.dat; then \
	  ./config.status --skip-meson; \
	else \
	  ./config.status && touch build.ninja.stamp; \
	fi

# 2. meson.stamp exists if meson has run at least once (so ninja reconfigure
# works), but otherwise never needs to be updated
meson-private/coredata.dat: meson.stamp
meson.stamp: config-host.mak
	@touch meson.stamp

# 3. ensure generated build files are up-to-date

ifneq ($(NINJA),)
Makefile.ninja: build.ninja
	$(quiet-@){ \
	  echo 'ninja-targets = \'; \
	  $(NINJA) -t targets all | sed 's/:.*//; $$!s/$$/ \\/'; \
	  echo 'build-files = \'; \
	  $(NINJA) -t query build.ninja | sed -n '1,/^  input:/d; /^  outputs:/q; s/$$/ \\/p'; \
	} > $@.tmp && mv $@.tmp $@
-include Makefile.ninja

# A separate rule is needed for Makefile dependencies to avoid -n
build.ninja: build.ninja.stamp
$(build-files):
build.ninja.stamp: meson.stamp $(build-files)
	$(NINJA) $(if $V,-v,) build.ninja && touch $@
endif

ifneq ($(MESON),)
Makefile.mtest: build.ninja scripts/mtest2make.py
	$(MESON) introspect --targets --tests --benchmarks | $(PYTHON) scripts/mtest2make.py > $@
-include Makefile.mtest

.PHONY: update-buildoptions
all update-buildoptions: $(SRC_PATH)/scripts/meson-buildoptions.sh
$(SRC_PATH)/scripts/meson-buildoptions.sh: $(SRC_PATH)/meson_options.txt
	$(MESON) introspect --buildoptions $(SRC_PATH)/meson.build | $(PYTHON) \
	  scripts/meson-buildoptions.py > $@.tmp && mv $@.tmp $@
endif

# 4. Rules to bridge to other makefiles

ifneq ($(NINJA),)
# Filter out long options to avoid flags like --no-print-directory which
# may result in false positive match for MAKE.n
MAKE.n = $(findstring n,$(firstword $(filter-out --%,$(MAKEFLAGS))))
MAKE.k = $(findstring k,$(firstword $(filter-out --%,$(MAKEFLAGS))))
MAKE.q = $(findstring q,$(firstword $(filter-out --%,$(MAKEFLAGS))))
MAKE.nq = $(if $(word 2, $(MAKE.n) $(MAKE.q)),nq)
NINJAFLAGS = $(if $V,-v) $(if $(MAKE.n), -n) $(if $(MAKE.k), -k0) \
        $(filter-out -j, $(lastword -j1 $(filter -l% -j%, $(MAKEFLAGS)))) \
        -d keepdepfile
ninja-cmd-goals = $(or $(MAKECMDGOALS), all)
ninja-cmd-goals += $(foreach g, $(MAKECMDGOALS), $(.ninja-goals.$g))

makefile-targets := build.ninja ctags TAGS cscope dist clean
# "ninja -t targets" also lists all prerequisites.  If build system
# files are marked as PHONY, however, Make will always try to execute
# "ninja build.ninja".
ninja-targets := $(filter-out $(build-files) $(makefile-targets), $(ninja-targets))
.PHONY: $(ninja-targets) run-ninja
$(ninja-targets): run-ninja

# Use "| cat" to give Ninja a more "make-y" output.  Use "+" to bypass the
# --output-sync line.
run-ninja: config-host.mak
ifneq ($(filter $(ninja-targets), $(ninja-cmd-goals)),)
	+$(if $(MAKE.nq),@:,$(quiet-@)$(NINJA) $(NINJAFLAGS) \
	   $(sort $(filter $(ninja-targets), $(ninja-cmd-goals))) | cat)
endif
endif

ifeq ($(CONFIG_PLUGIN),y)
.PHONY: plugins
plugins:
	$(call quiet-command,\
		$(MAKE) $(SUBDIR_MAKEFLAGS) -C contrib/plugins V="$(V)", \
		"BUILD", "example plugins")
endif # $(CONFIG_PLUGIN)

else # config-host.mak does not exist
config-host.mak:
ifneq ($(filter-out $(UNCHECKED_GOALS),$(MAKECMDGOALS)),$(if $(MAKECMDGOALS),,fail))
	@echo "Please call configure before running make!"
	@exit 1
endif
endif # config-host.mak does not exist

SUBDIR_MAKEFLAGS=$(if $(V),,--no-print-directory --quiet)

include $(SRC_PATH)/tests/Makefile.include

all: recurse-all

ROMS_RULES=$(foreach t, all clean distclean, $(addsuffix /$(t), $(ROMS)))
.PHONY: $(ROMS_RULES)
$(ROMS_RULES):
	$(call quiet-command,$(MAKE) $(SUBDIR_MAKEFLAGS) -C $(dir $@) V="$(V)" TARGET_DIR="$(dir $@)" $(notdir $@),)

.PHONY: recurse-all recurse-clean
recurse-all: $(addsuffix /all, $(ROMS))
recurse-clean: $(addsuffix /clean, $(ROMS))
recurse-distclean: $(addsuffix /distclean, $(ROMS))

######################################################################

clean: recurse-clean
	-$(quiet-@)test -f build.ninja && $(NINJA) $(NINJAFLAGS) -t clean || :
	-$(quiet-@)test -f build.ninja && $(NINJA) $(NINJAFLAGS) clean-ctlist || :
	find . \( -name '*.so' -o -name '*.dll' -o \
		  -name '*.[oda]' -o -name '*.gcno' \) -type f \
		! -path ./roms/edk2/ArmPkg/Library/GccLto/liblto-aarch64.a \
		! -path ./roms/edk2/ArmPkg/Library/GccLto/liblto-arm.a \
		-exec rm {} +
	rm -f TAGS cscope.* *~ */*~

VERSION = $(shell cat $(SRC_PATH)/VERSION)

dist: qemu-$(VERSION).tar.bz2

qemu-%.tar.bz2:
	$(SRC_PATH)/scripts/make-release "$(SRC_PATH)" "$(patsubst qemu-%.tar.bz2,%,$@)"

distclean: clean recurse-distclean
	-$(quiet-@)test -f build.ninja && $(NINJA) $(NINJAFLAGS) -t clean -g || :
	rm -f config-host.mak Makefile.prereqs qemu-bundle
	rm -f tests/tcg/*/config-target.mak tests/tcg/config-host.mak
	rm -f config.status
	rm -f roms/seabios/config.mak
	rm -f qemu-plugins-ld.symbols qemu-plugins-ld64.symbols
	rm -f *-config-target.h *-config-devices.mak *-config-devices.h
	rm -rf meson-private meson-logs meson-info compile_commands.json
	rm -f Makefile.ninja Makefile.mtest build.ninja.stamp meson.stamp
	rm -f config.log
	rm -f linux-headers/asm
	rm -Rf .sdk

find-src-path = find "$(SRC_PATH)" -path "$(SRC_PATH)/meson" -prune -o \
	-type l -prune -o \( -name "*.[chsS]" -o -name "*.[ch].inc" \)

.PHONY: ctags
ctags:
	$(call quiet-command, 			\
		rm -f "$(SRC_PATH)/"tags, 	\
		"CTAGS", "Remove old tags")
	$(call quiet-command, \
		$(find-src-path) -exec ctags 		\
		-f "$(SRC_PATH)/"tags --append {} +,	\
		"CTAGS", "Re-index $(SRC_PATH)")

.PHONY: gtags
gtags:
	$(call quiet-command, 			\
		rm -f "$(SRC_PATH)/"GTAGS; 	\
		rm -f "$(SRC_PATH)/"GRTAGS; 	\
		rm -f "$(SRC_PATH)/"GPATH, 	\
		"GTAGS", "Remove old $@ files")
	$(call quiet-command, 				\
	        (cd $(SRC_PATH) && 			\
		 $(find-src-path) -print | gtags -f -),	\
		"GTAGS", "Re-index $(SRC_PATH)")

.PHONY: TAGS
TAGS:
	$(call quiet-command, 			\
		rm -f "$(SRC_PATH)/"TAGS,	\
		"TAGS", "Remove old $@")
	$(call quiet-command, 				\
		$(find-src-path) -exec etags 		\
		-f "$(SRC_PATH)/"TAGS --append {} +, 	\
		"TAGS", "Re-index $(SRC_PATH)")

.PHONY: cscope
cscope:
	$(call quiet-command,			\
		rm -f "$(SRC_PATH)/"cscope.* ,	\
		"cscope", "Remove old $@ files")
	$(call quiet-command, 					\
		($(find-src-path) -print | sed -e 's,^\./,,'    \
		> "$(SRC_PATH)/cscope.files"), 			\
		"cscope", "Create file list")
	$(call quiet-command, 				\
		cscope -b -i"$(SRC_PATH)/cscope.files" 	\
		-f"$(SRC_PATH)"/cscope.out, 		\
		"cscope", "Re-index $(SRC_PATH)")

# Needed by "meson install"
export DESTDIR

include $(SRC_PATH)/tests/lcitool/Makefile.include
include $(SRC_PATH)/tests/docker/Makefile.include
include $(SRC_PATH)/tests/vm/Makefile.include

print-help-run = printf "  %-30s - %s\\n" "$1" "$2"
print-help = @$(call print-help-run,$1,$2)

.PHONY: help
help:
	@echo  'Generic targets:'
	$(call print-help,all,Build all)
	$(call print-help,dir/file.o,Build specified target only)
	$(call print-help,install,Install QEMU, documentation and tools)
	$(call print-help,ctags/gtags/TAGS,Generate tags file for editors)
	$(call print-help,cscope,Generate cscope index)
	$(call print-help,sparse,Run sparse on the QEMU source)
	@echo  ''
ifeq ($(CONFIG_PLUGIN),y)
	@echo  'Plugin targets:'
	$(call print-help,plugins,Build the example TCG plugins)
	@echo  ''
endif
	@echo  'Cleaning targets:'
	$(call print-help,clean,Remove most generated files but keep the config)
	$(call print-help,distclean,Remove all generated files)
	$(call print-help,dist,Build a distributable tarball)
	@echo  ''
	@echo  'Test targets:'
	$(call print-help,check,Run all tests (check-help for details))
	$(call print-help,bench,Run all benchmarks)
	$(call print-help,lcitool-help,Help about targets for managing build environment manifests)
	$(call print-help,docker-help,Help about targets running tests inside containers)
	$(call print-help,vm-help,Help about targets running tests inside VM)
	@echo  ''
	@echo  'Documentation targets:'
	$(call print-help,html man,Build documentation in specified format)
	@echo  ''
ifdef CONFIG_WIN32
	@echo  'Windows targets:'
	$(call print-help,installer,Build NSIS-based installer for QEMU)
	$(call print-help,msi,Build MSI-based installer for qemu-ga)
	@echo  ''
endif
	$(call print-help,$(MAKE) [targets],(quiet build, default))
	$(call print-help,$(MAKE) V=1 [targets],(verbose build))

# will delete the target of a rule if commands exit with a nonzero exit status
.DELETE_ON_ERROR:

print-%:
	@echo '$*=$($*)'
