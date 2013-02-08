
vgabios_variants := stdvga cirrus vmware qxl
pxerom_variants  := e1000 eepro100 ne2k_pci pcnet rtl8139 virtio

pxe-rom-e1000    : VID := 8086
pxe-rom-e1000    : DID := 100e
pxe-rom-eepro100 : VID := 8086
pxe-rom-eepro100 : DID := 1209
pxe-rom-ne2k_pci : VID := 1050
pxe-rom-ne2k_pci : DID := 0940
pxe-rom-pcnet    : VID := 1022
pxe-rom-pcnet    : DID := 2000
pxe-rom-rtl8139  : VID := 10ec
pxe-rom-rtl8139  : DID := 8139
pxe-rom-virtio   : VID := 1af4
pxe-rom-virtio   : DID := 1000

default:
	@echo "nothing is build by default"
	@echo "available build targets:"
	@echo "  bios           -- update bios.bin (seabios)"
	@echo "  seavgabios     -- update vgabios binaries (seabios)"
	@echo "  lgplvgabios    -- update vgabios binaries (lgpl)"
	@echo "  pxerom         -- update nic roms (bios only)"

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

pxe-rom-%:
	make -C ipxe/src bin/$(VID)$(DID).rom
	cp ipxe/src/bin/$(VID)$(DID).rom ../pc-bios/pxe-$*.rom
