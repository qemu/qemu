# To specify cross compiler prefix, use CROSS_PREFIX=
#   $ make CROSS_PREFIX=s390x-linux-gnu-

.PHONY: all clean
all: a-b-bios.h
fwdir=../../../pc-bios/s390-ccw

CFLAGS+=-ffreestanding -fno-delete-null-pointer-checks -fPIE -Os \
	-msoft-float -march=z900 -fno-asynchronous-unwind-tables \
	-fno-stack-protector -Wl,-pie -Wl,--build-id=none -nostdlib

a-b-bios.h: s390x.elf
	echo "$$__note" > header.tmp
	xxd -i $< | sed -e 's/.*int.*//' >> header.tmp
	mv header.tmp $@

# We use common-page-size=16 to avoid big padding in the ELF file
s390x.elf: a-b-bios.c
	$(CROSS_PREFIX)gcc $(CFLAGS) -I$(fwdir) $(fwdir)/start.S \
		$(fwdir)/sclp.c -Wl,-zcommon-page-size=16 -o $@ $<
	$(CROSS_PREFIX)strip $@

clean:
	@rm -rf *.elf *.o
