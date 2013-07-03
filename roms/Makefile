
vgabios_variants := stdvga cirrus vmware qxl
pxerom_variants  := e1000 eepro100 ne2k_pci pcnet rtl8139 virtio

pxe-rom-e1000    efi-rom-e1000    : VID := 8086
pxe-rom-e1000    efi-rom-e1000    : DID := 100e
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
	@echo "  seavgabios     -- update vgabios binaries (seabios)"
	@echo "  lgplvgabios    -- update vgabios binaries (lgpl)"
	@echo "  pxerom         -- update nic roms (bios only)"
	@echo "  efirom         -- update nic roms (bios+efi, this needs"
	@echo "                    the EfiRom utility from edk2 / tianocore)"

bios: config.seabios
	sh configure-seabios.sh $<
	make -C seabios out/bios.bin
	cp seabios/out/bios.bin ../pc-bios/bios.bin
	cp seabios/out/*dsdt.aml ../pc-bios/

seavgabios: $(patsubst %,seavgabios-%,$(vgabios_variants))

seavgabios-%: config.vga.%
	sh configure-seabios.sh $<
	make -C seabios out/vgabios.bin
	cp seabios/out/vgabios.bin ../pc-bios/vgabios-$*.bin

lgplvgabios: $(patsubst %,lgplvgabios-%,$(vgabios_variants))

lgplvgabios-%:
	make -C vgabios vgabios-$*.bin
	cp vgabios/VGABIOS-lgpl-latest.$*.bin ../pc-bios/vgabios-$*.bin

pxerom: $(patsubst %,pxe-rom-%,$(pxerom_variants))

pxe-rom-%: ipxe/src/config/local/general.h
	make -C ipxe/src bin/$(VID)$(DID).rom
	cp ipxe/src/bin/$(VID)$(DID).rom ../pc-bios/pxe-$*.rom

efirom: $(patsubst %,efi-rom-%,$(pxerom_variants))

efi-rom-%: ipxe/src/config/local/general.h
	make -C ipxe/src bin/$(VID)$(DID).rom
	make -C ipxe/src bin-i386-efi/$(VID)$(DID).efidrv
	make -C ipxe/src bin-x86_64-efi/$(VID)$(DID).efidrv
	$(EFIROM) -f "0x$(VID)" -i "0x$(DID)" -l 0x02 \
		-b ipxe/src/bin/$(VID)$(DID).rom \
		-ec ipxe/src/bin-i386-efi/$(VID)$(DID).efidrv \
		-ec ipxe/src/bin-x86_64-efi/$(VID)$(DID).efidrv \
		-o ../pc-bios/efi-$*.rom

ipxe/src/config/local/%: config.ipxe.%
	cp $< $@
