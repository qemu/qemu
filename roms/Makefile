
vgabios_variants := stdvga cirrus vmware qxl isavga virtio bochs-display ramfb
vgabios_targets  := $(subst -isavga,,$(patsubst %,vgabios-%.bin,$(vgabios_variants)))
pxerom_variants  := e1000 e1000e eepro100 ne2k_pci pcnet rtl8139 virtio vmxnet3
pxerom_targets   := 8086100e 808610d3 80861209 10500940 10222000 10ec8139 1af41000 15ad07b0

pxe-rom-e1000    efi-rom-e1000    : VID := 8086
pxe-rom-e1000    efi-rom-e1000    : DID := 100e
pxe-rom-e1000e   efi-rom-e1000e   : VID := 8086
pxe-rom-e1000e   efi-rom-e1000e   : DID := 10d3
pxe-rom-eepro100 efi-rom-eepro100 : VID := 8086
pxe-rom-eepro100 efi-rom-eepro100 : DID := 1209
pxe-rom-ne2k_pci efi-rom-ne2k_pci : VID := 1050
pxe-rom-ne2k_pci efi-rom-ne2k_pci : DID := 0940
pxe-rom-pcnet    efi-rom-pcnet    : VID := 1022
pxe-rom-pcnet    efi-rom-pcnet    : DID := 2000
pxe-rom-rtl8139  efi-rom-rtl8139  : VID := 10ec
pxe-rom-rtl8139  efi-rom-rtl8139  : DID := 8139
pxe-rom-virtio   efi-rom-virtio   : VID := 1af4
pxe-rom-virtio   efi-rom-virtio   : DID := 1000
pxe-rom-vmxnet3  efi-rom-vmxnet3  : VID := 15ad
pxe-rom-vmxnet3  efi-rom-vmxnet3  : DID := 07b0

#
# cross compiler auto detection
#
path := $(subst :, ,$(PATH))
system := $(shell uname -s | tr "A-Z" "a-z")

# first find cross binutils in path
find-cross-ld = $(firstword $(wildcard $(patsubst %,%/$(1)-*$(system)*-ld,$(path))))
# then check we have cross gcc too
find-cross-gcc = $(firstword $(wildcard $(patsubst %ld,%gcc,$(call find-cross-ld,$(1)))))
# finally strip off path + toolname so we get the prefix
find-cross-prefix = $(subst gcc,,$(notdir $(call find-cross-gcc,$(1))))

powerpc64_cross_prefix := $(call find-cross-prefix,powerpc64)
powerpc_cross_prefix := $(call find-cross-prefix,powerpc)
x86_64_cross_prefix := $(call find-cross-prefix,x86_64)

# tag our seabios builds
SEABIOS_EXTRAVERSION="-prebuilt.qemu.org"

#
# EfiRom utility is shipped with edk2 / tianocore, in BaseTools/
#
# We need that to combine multiple images (legacy bios,
# efi ia32, efi x64) into a single rom binary.
#
# We try to find it in the path.  You can also pass the location on
# the command line, i.e. "make EFIROM=/path/to/EfiRom efirom"
#
EFIROM ?= $(shell which EfiRom 2>/dev/null)

default:
	@echo "nothing is build by default"
	@echo "available build targets:"
	@echo "  bios           -- update bios.bin (seabios)"
	@echo "  vgabios        -- update vgabios binaries (seabios)"
	@echo "  sgabios        -- update sgabios binaries"
	@echo "  pxerom         -- update nic roms (bios only)"
	@echo "  efirom         -- update nic roms (bios+efi, this needs"
	@echo "                    the EfiRom utility from edk2 / tianocore)"
	@echo "  slof           -- update slof.bin"
	@echo "  skiboot        -- update skiboot.lid"
	@echo "  u-boot.e500    -- update u-boot.e500"
	@echo "  u-boot.sam460  -- update u-boot.sam460"

bios: build-seabios-config-seabios-128k build-seabios-config-seabios-256k
	cp seabios/builds/seabios-128k/bios.bin ../pc-bios/bios.bin
	cp seabios/builds/seabios-256k/bios.bin ../pc-bios/bios-256k.bin

vgabios seavgabios: $(patsubst %,seavgabios-%,$(vgabios_variants))

seavgabios-isavga: build-seabios-config-vga-isavga
	cp seabios/builds/vga-isavga/vgabios.bin ../pc-bios/vgabios.bin

seavgabios-%: build-seabios-config-vga-%
	cp seabios/builds/vga-$*/vgabios.bin ../pc-bios/vgabios-$*.bin

build-seabios-config-%: config.%
	mkdir -p seabios/builds/$*
	cp $< seabios/builds/$*/.config
	$(MAKE) -C seabios \
		EXTRAVERSION=$(SEABIOS_EXTRAVERSION) \
		CROSS_COMPILE=$(x86_64_cross_prefix) \
		KCONFIG_CONFIG=$(CURDIR)/seabios/builds/$*/.config \
		OUT=$(CURDIR)/seabios/builds/$*/ oldnoconfig
	$(MAKE) -C seabios \
		EXTRAVERSION=$(SEABIOS_EXTRAVERSION) \
		CROSS_COMPILE=$(x86_64_cross_prefix) \
		KCONFIG_CONFIG=$(CURDIR)/seabios/builds/$*/.config \
		OUT=$(CURDIR)/seabios/builds/$*/ all


.PHONY: sgabios skiboot
sgabios:
	$(MAKE) -C sgabios
	cp sgabios/sgabios.bin ../pc-bios


pxerom: $(patsubst %,pxe-rom-%,$(pxerom_variants))

pxe-rom-%: build-pxe-roms
	cp ipxe/src/bin/$(VID)$(DID).rom ../pc-bios/pxe-$*.rom

efirom: $(patsubst %,efi-rom-%,$(pxerom_variants))

efi-rom-%: build-pxe-roms build-efi-roms
	$(EFIROM) -f "0x$(VID)" -i "0x$(DID)" -l 0x02 \
		-b ipxe/src/bin/$(VID)$(DID).rom \
		-ec ipxe/src/bin-i386-efi/$(VID)$(DID).efidrv \
		-ec ipxe/src/bin-x86_64-efi/$(VID)$(DID).efidrv \
		-o ../pc-bios/efi-$*.rom

build-pxe-roms:
	$(MAKE) -C ipxe/src CONFIG=qemu \
		CROSS_COMPILE=$(x86_64_cross_prefix) \
		$(patsubst %,bin/%.rom,$(pxerom_targets))

build-efi-roms: build-pxe-roms
	$(MAKE) -C ipxe/src CONFIG=qemu \
		CROSS_COMPILE=$(x86_64_cross_prefix) \
		$(patsubst %,bin-i386-efi/%.efidrv,$(pxerom_targets)) \
		$(patsubst %,bin-x86_64-efi/%.efidrv,$(pxerom_targets))


slof:
	$(MAKE) -C SLOF CROSS=$(powerpc64_cross_prefix) qemu
	cp SLOF/boot_rom.bin ../pc-bios/slof.bin

u-boot.e500:
	$(MAKE) -C u-boot O=build.e500 qemu-ppce500_config
	$(MAKE) -C u-boot CROSS_COMPILE=$(powerpc_cross_prefix) \
		O=build.e500
	$(powerpc_cross_prefix)strip u-boot/build.e500/u-boot -o \
		../pc-bios/u-boot.e500

u-boot.sam460:
	$(MAKE) -C u-boot-sam460ex Sam460ex_config
	$(MAKE) -C u-boot-sam460ex CROSS_COMPILE=$(powerpc_cross_prefix)
	cp u-boot-sam460ex/u-boot.bin ../pc-bios/u-boot-sam460-20100605.bin

skiboot:
	$(MAKE) -C skiboot CROSS=$(powerpc64_cross_prefix)
	cp skiboot/skiboot.lid ../pc-bios/skiboot.lid

clean:
	rm -rf seabios/.config seabios/out seabios/builds
	$(MAKE) -C sgabios clean
	rm -f sgabios/.depend
	$(MAKE) -C ipxe/src veryclean
	$(MAKE) -C SLOF clean
	rm -rf u-boot/build.e500
	$(MAKE) -C u-boot-sam460ex distclean
	$(MAKE) -C skiboot clean
