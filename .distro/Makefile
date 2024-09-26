include Makefile.common

REDHAT:=$(shell pwd)
RPMBUILD:=$(REDHAT)/rpmbuild
SCRIPTS:=$(REDHAT)/scripts
KOJI_OPTIONS:=$(KOJI_FLAGS) --scratch
SOURCE_DIR:=$(shell git rev-parse --show-toplevel)
SOURCES_DIR=$(REDHAT)/sources
SOURCES_SHA512=$(REDHAT)/sources.sha512
SPECFILE?=$(shell ls *.spec)
NAME?=$(shell grep "^Name:" $(SPECFILE) | cut -d ':' -f 2 | xargs)
MARKER?=$(shell $(SCRIPTS)/guess-marker $(SPECFILE) -C $(SOURCE_DIR))

ifdef DIST
  DIST_DEF=--define "dist $(DIST)"
else
  # Better to have no dist suffix than a dist suffix set by the distro we're building on. The latter can lead to confusion.
  DIST_DEF=--undefine "dist"
endif

SRPM_NAME=$(shell rpmspec --query --srpm --target="src" $(DIST_DEF) $(RPMBUILD)/SPECS/$(SPECFILE) | sed -e 's/\(.*\)\..*/\1/').src.rpm

ifeq ($(USE_BINARY), 1)
  BINARY_FLAG=
else
  BINARY_FLAG=--no-binary
endif

# Options section

# Hide command calls without debug option
ifeq ($(DEBUG),1)
  DS=
else
  DS=@
endif

# Hide progress bar in scripts
ifeq ($(NOPROGRESS),1)
  KOJI_OPTIONS:=$(KOJI_OPTIONS) --noprogress
endif

# Do not wait for build finish
ifeq ($(NOWAIT),1)
  KOJI_OPTIONS:=$(KOJI_OPTIONS) --nowait
endif


# create an empty localversion file if you don't want a local buildid
ifneq ($(NOLOCALVERSION),1)
  ifeq ($(LOCALVERSION),)
    LOCALVERSION:=$(shell cat localversion 2>/dev/null)
  endif
  ifeq ($(LOCALVERSION),)
    LOCALVERSION:=$(shell id -u -n)$(shell date +"%Y%m%d%H%M")
  endif
else
  LOCALVERSION:=
endif


.PHONY: rh-clean-sources rh-prep rh-srpm rh-rhel-koji rh-centos-koji rh-help
all: rh-help

rh-clean-sources:
	$(DS)mkdir -p $(RPMBUILD)/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
	$(DS)rm -f $(RPMBUILD)/SOURCES/*

rh-format-patch: rh-clean-sources
	@echo Generating patch files...
	$(DS)git -C $(SOURCE_DIR) format-patch $(BINARY_FLAG) --no-cover-letter --no-numbered --no-signature -o "$(RPMBUILD)/SOURCES" "$(MARKER)" \
	-- ':(exclude).git*' ':(exclude)*.spec' ':(exclude).distro' ':(exclude)redhat'

rh-download-sources:
	@echo Downloading sources...
	$(DS)[ -n "$(TARURL)" ] && wget -nc "$(TARURL)" || true
	$(DS)[ -z "$(TARURL)" ] && grep -v "^#" $(SOURCES_SHA512) | sed -E "s@(\S+)\s+(\S+)@$(SOURCES_BASE_URL)/$(NAME)/\2/sha512/\1/\2@" | xargs -n 1 wget -nc || true
	@echo Checking sources checksum...
	$(DS)sha512sum --check "$(SOURCES_SHA512)"

rh-prep: rh-format-patch rh-download-sources
	@echo Copying sources to rpmbuild directory...
	$(DS)grep -v "^#" $(SOURCES_SHA512) | awk '{ print $$2 }' | xargs -I {} cp -v {} $(RPMBUILD)/SOURCES
	$(DS)[ -n "$(SOURCES_FILELIST)" ] && cp -v $(SOURCES_FILELIST) $(RPMBUILD)/SOURCES || true
	$(DS)[ -d "$(SOURCES_DIR)" ] && cp -v $(SOURCES_DIR)/* $(RPMBUILD)/SOURCES || true
	@echo Updating spec file...
	$(DS)$(SCRIPTS)/process_patches.sh "$(SPECFILE)" "$(LOCALVERSION)"

rh-srpm: rh-prep
	@echo Building SRPM...
	$(DS)rpmbuild \
	--define "_sourcedir $(RPMBUILD)/SOURCES" \
	--define "_builddir $(RPMBUILD)/BUILD" \
	--define "_srcrpmdir $(RPMBUILD)/SRPMS" \
	--define "_rpmdir $(RPMBUILD)/RPMS" \
	--define "_specdir $(RPMBUILD)/SPECS" \
	$(DIST_DEF) \
	--nodeps -bs $(RPMBUILD)/SPECS/$(SPECFILE)

rh-rhel-koji: rh-srpm
	@echo "Build $(SRPM_NAME) as $(BUILD_TARGET_RHEL)"
	$(DS)brew build $(KOJI_OPTIONS) $(BUILD_TARGET_RHEL) $(RPMBUILD)/SRPMS/$(SRPM_NAME)

rh-centos-koji: rh-srpm
	@echo "Build $(SRPM_NAME) as $(BUILD_TARGET_CENTOS)"
	$(DS)koji --profile=stream build $(KOJI_OPTIONS) $(BUILD_TARGET_CENTOS) $(RPMBUILD)/SRPMS/$(SRPM_NAME)

rh-help:
	@echo "Supported make targets:"
	@echo "  rh-srpm:        Create srpm"
	@echo "  rh-rhel-koji:   Build package using RHEL 9 koji"
	@echo "  rh-centos-koji: Build package using CentOS 9 Streams koji"
	@echo "  rh-help:        Print out help"
	@echo ""
	@echo "Supported variables:"
	@echo "  NOPROGRESS:     If 1, do not show progressbar when uploading srpm"
	@echo "  NOWAIT:         If 1, do not wait for build to finish"
	@echo "  NOLOCALVESION:  If 1, do append LOCALVERSION to release version"
	@echo "  LOCALVERSION:   Use value as release version suffix"
	@echo "  USE_BINARY:     If 1, generate binary patches"
	@echo ""
	@echo "LOCALVERSION can be stored in .distro/localversion file."
	@echo "If LOCALVERSION is not set and there's no (or empty) .distro/localversion file,"
	@echo "date +"%Y%m%d%H%M" is used instead."
