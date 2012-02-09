
default:
	@echo "nothing is build by default"
	@echo "available build targets:"
	@echo "  bios           -- update bios.bin (seabios)"

bios: config.seabios
	sh configure-seabios.sh $<
	make -C seabios out/bios.bin
	cp seabios/out/bios.bin ../pc-bios/bios.bin
