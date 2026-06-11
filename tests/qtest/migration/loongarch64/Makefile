# SPDX-License-Identifier: GPL-2.0-or-later

# To specify cross compiler prefix, use CROSS_PREFIX=
#   $ make CROSS_PREFIX=loongarch64-linux-gnu-

.PHONY: all clean
all: a-b-kernel.h

a-b-kernel.h: loongarch64.kernel
	echo "$$__note" > $@
	xxd -i $< | sed -e 's/.*int.*//' >> $@

loongarch64.kernel: loongarch64.elf
	$(CROSS_PREFIX)objcopy -j .text -O binary $< $@

loongarch64.elf: a-b-kernel.S
	$(CROSS_PREFIX)gcc -o $@ -nostdlib -Wl,--build-id=none $<

clean:
	$(RM) *.kernel *.elf
