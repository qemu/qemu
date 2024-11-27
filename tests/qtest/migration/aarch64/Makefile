# To specify cross compiler prefix, use CROSS_PREFIX=
#   $ make CROSS_PREFIX=aarch64-linux-gnu-

.PHONY: all clean
all: a-b-kernel.h

a-b-kernel.h: aarch64.kernel
	echo "$$__note" > $@
	xxd -i $< | sed -e 's/.*int.*//' >> $@

aarch64.kernel: aarch64.elf
	$(CROSS_PREFIX)objcopy -O binary $< $@

aarch64.elf: a-b-kernel.S
	$(CROSS_PREFIX)gcc -o $@ -nostdlib -Wl,--build-id=none $<

clean:
	$(RM) *.kernel *.elf
