
vgabios_variants := stdvga cirrus vmware qxl

default:
	@echo "nothing is build by default"
	@echo "available build targets:"
	@echo "  bios           -- update bios.bin (seabios)"
	@echo "  seavgabios     -- update vgabios binaries (seabios)"
	@echo "  lgplvgabios    -- update vgabios binaries (lgpl)"

bios: config.seabios
	sh configure-seabios.sh $<
	make -C seabios out/bios.bin
	cp seabios/out/bios.bin ../pc-bios/bios.bin

seavgabios: $(patsubst %,seavgabios-%,$(vgabios_variants))

seavgabios-%: config.vga.%
	sh configure-seabios.sh $<
	make -C seabios out/vgabios.bin
	cp seabios/out/vgabios.bin ../pc-bios/vgabios-$*.bin

lgplvgabios: $(patsubst %,lgplvgabios-%,$(vgabios_variants))

lgplvgabios-%:
	make -C vgabios vgabios-$*.bin
	cp vgabios/VGABIOS-lgpl-latest.$*.bin ../pc-bios/vgabios-$*.bin
