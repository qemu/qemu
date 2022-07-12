include config.mak
VPATH=$(SRC_DIR)
all: vof.bin

EXTRA_CFLAGS += -mcpu=power4

%.o: %.S
	$(CC) $(EXTRA_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(EXTRA_CFLAGS) -c -fno-stack-protector -o $@ $<

vof.elf: entry.o main.o ci.o bootmem.o libc.o
	$(LD) -nostdlib -e_start -T$(SRC_DIR)/vof.lds -EB -o $@ $^

%.bin: %.elf
	$(OBJCOPY) -O binary -j .text -j .data -j .toc -j .got2 $^ $@

clean:
	rm -f *.o vof.bin vof.elf *~

.PHONY: all clean
