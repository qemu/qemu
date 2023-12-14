# Makefile for the test helper UEFI applications that run in guests.
#
# Copyright (C) 2019, Red Hat, Inc.
#
# This program and the accompanying materials are licensed and made available
# under the terms and conditions of the BSD License that accompanies this
# distribution. The full text of the license may be found at
# <http://opensource.org/licenses/bsd-license.php>.
#
# THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS, WITHOUT
# WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

edk2_dir              := ../../roms/edk2
images_dir            := ../data/uefi-boot-images
emulation_targets     := arm aarch64 i386 x86_64
uefi_binaries         := bios-tables-test
intermediate_suffixes := .efi .fat .iso.raw

images: $(foreach binary,$(uefi_binaries), \
		$(foreach target,$(emulation_targets), \
			$(images_dir)/$(binary).$(target).iso.qcow2))

# Preserve all intermediate targets if the build succeeds.
# - Intermediate targets help with development & debugging.
# - Preserving intermediate targets also keeps spurious changes out of the
#   final build products, in case the user re-runs "make" without any changes
#   to the UEFI source code. Normally, the intermediate files would have been
#   removed by the last "make" invocation, hence the re-run would rebuild them
#   from the unchanged UEFI sources. Unfortunately, the "mkdosfs" and
#   "genisoimage" utilities embed timestamp-based information in their outputs,
#   which causes git to report differences for the tracked qcow2 ISO images.
.SECONDARY: $(foreach binary,$(uefi_binaries), \
		$(foreach target,$(emulation_targets), \
			$(foreach suffix,$(intermediate_suffixes), \
				Build/$(binary).$(target)$(suffix))))

# In the pattern rules below, the stem (%, $*) stands for
# "$(binary).$(target)".

# Convert the raw ISO image to a qcow2 one, enabling compression, and using a
# small cluster size. This allows for small binary files under git control,
# hence for small binary patches.
$(images_dir)/%.iso.qcow2: Build/%.iso.raw
	mkdir -p -- $(images_dir)
	$${QTEST_QEMU_IMG:-qemu-img} convert -f raw -O qcow2 -c \
		-o cluster_size=512 -- $< $@

# Embed the "UEFI system partition" into an ISO9660 file system as an ElTorito
# boot image.
Build/%.iso.raw: Build/%.fat
	genisoimage -input-charset ASCII -efi-boot $(notdir $<) -no-emul-boot \
		-quiet -o $@ -- $<

# Define chained macros in order to map QEMU system emulation targets to
# *short* UEFI architecture identifiers. Periods are allowed in, and ultimately
# stripped from, the argument.
map_arm_to_uefi     = $(subst arm,ARM,$(1))
map_aarch64_to_uefi = $(subst aarch64,AA64,$(call map_arm_to_uefi,$(1)))
map_i386_to_uefi    = $(subst i386,IA32,$(call map_aarch64_to_uefi,$(1)))
map_x86_64_to_uefi  = $(subst x86_64,X64,$(call map_i386_to_uefi,$(1)))
map_to_uefi         = $(subst .,,$(call map_x86_64_to_uefi,$(1)))

# Format a "UEFI system partition", using the UEFI binary as the default boot
# loader. Add 10% size for filesystem metadata, round up to the next KB, and
# make sure the size is large enough for a FAT filesystem. Name the filesystem
# after the UEFI binary. (Excess characters are automatically dropped from the
# filesystem label.)
Build/%.fat: Build/%.efi
	rm -f -- $@
	uefi_bin_b=$$(stat --format=%s -- $<) && \
		uefi_fat_kb=$$(( (uefi_bin_b * 11 / 10 + 1023) / 1024 )) && \
		uefi_fat_kb=$$(( uefi_fat_kb >= 64 ? uefi_fat_kb : 64 )) && \
		mkdosfs -C $@ -n $(basename $(@F)) -- $$uefi_fat_kb
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::EFI
	MTOOLS_SKIP_CHECK=1 mmd -i $@ ::EFI/BOOT
	MTOOLS_SKIP_CHECK=1 mcopy -i $@ -- $< \
		::EFI/BOOT/BOOT$(call map_to_uefi,$(suffix $*)).EFI

# In the pattern rules below, the stem (%, $*) stands for "$(target)" only. The
# association between the UEFI binary (such as "bios-tables-test") and the
# component name from the edk2 platform DSC file (such as "BiosTablesTest") is
# explicit in each rule.

# "build.sh" invokes the "build" utility of edk2 BaseTools. In any given edk2
# workspace, at most one "build" instance may be operating at a time. Therefore
# we must serialize the rebuilding of targets in this Makefile.
.NOTPARALLEL:

# In turn, the "build" utility of edk2 BaseTools invokes another "make".
# Although the outer "make" process advertises its job server to all child
# processes via MAKEFLAGS in the environment, the outer "make" closes the job
# server file descriptors (exposed in MAKEFLAGS) before executing a recipe --
# unless the recipe is recognized as a recursive "make" recipe. Recipes that
# call $(MAKE) are classified automatically as recursive; for "build.sh" below,
# we must mark the recipe manually as recursive, by using the "+" indicator.
# This way, when the inner "make" starts a parallel build of the target edk2
# module, it can communicate with the outer "make"'s job server.
Build/bios-tables-test.%.efi: build-edk2-tools
	+./build.sh $(edk2_dir) BiosTablesTest $* $@

build-edk2-tools:
	cd $(edk2_dir)/BaseTools && git submodule update --init --force
	$(MAKE) -C $(edk2_dir)/BaseTools \
		PYTHON_COMMAND=$${EDK2_PYTHON_COMMAND:-python3} \
		EXTRA_OPTFLAGS='$(EDK2_BASETOOLS_OPTFLAGS)' \
		EXTRA_LDFLAGS='$(EDK2_BASETOOLS_LDFLAGS)'

clean:
	rm -rf Build Conf log
	$(MAKE) -C $(edk2_dir)/BaseTools clean
