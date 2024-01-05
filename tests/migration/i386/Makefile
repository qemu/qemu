# To specify cross compiler prefix, use CROSS_PREFIX=
#   $ make CROSS_PREFIX=x86_64-linux-gnu-

.PHONY: all clean
all: a-b-bootblock.h

a-b-bootblock.h: x86.bootsect x86.o
	echo "$$__note" > header.tmp
	xxd -i $< | sed -e 's/.*int.*//' >> header.tmp
	nm x86.o | awk '{print "#define SYM_"$$3" 0x"$$1}' >> header.tmp
	mv header.tmp $@

x86.bootsect: x86.boot
	dd if=$< of=$@ bs=256 count=2 skip=124

x86.boot: x86.o
	$(CROSS_PREFIX)objcopy -O binary $< $@

x86.o: a-b-bootblock.S
	$(CROSS_PREFIX)gcc -I.. -m32 -march=i486 -c $< -o $@

clean:
	@rm -rf *.boot *.o *.bootsect
