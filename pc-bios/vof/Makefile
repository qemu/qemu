all: build-all

build-all: vof.bin

CROSS ?=
CC = $(CROSS)gcc
LD = $(CROSS)ld
OBJCOPY = $(CROSS)objcopy

%.o: %.S
	$(CC) -m32 -mbig-endian -mcpu=power4 -c -o $@ $<

%.o: %.c
	$(CC) -m32 -mbig-endian -mcpu=power4 -c -fno-stack-protector -o $@ $<

vof.elf: entry.o main.o ci.o bootmem.o libc.o
	$(LD) -nostdlib -e_start -Tvof.lds -EB -o $@ $^

%.bin: %.elf
	$(OBJCOPY) -O binary -j .text -j .data -j .toc -j .got2 $^ $@

clean:
	rm -f *.o vof.bin vof.elf *~
