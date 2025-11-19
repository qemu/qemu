CROSS   = aarch64-linux-gnu
CC      = $(CROSS)-gcc
OBJCOPY = $(CROSS)-objcopy

CFLAGS = -nostdlib -O2 -Wall -Wextra -ffreestanding
LDFLAGS = -T linker.ld -nostdlib

all: euart_test.elf euart_test.bin

euart_test.elf: main.o linker.ld
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ main.o

main.o: main.c
	$(CC) $(CFLAGS) -c main.c

euart_test.bin: euart_test.elf
	$(OBJCOPY) -O binary euart_test.elf euart_test.bin

clean:
	rm -f *.o *.elf *.bin

