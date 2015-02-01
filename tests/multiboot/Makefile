CC=gcc
CCFLAGS=-m32 -Wall -Wextra -Werror -fno-stack-protector -nostdinc -fno-builtin
ASFLAGS=-m32

LD=ld
LDFLAGS=-melf_i386 -T link.ld
LIBS=$(shell $(CC) $(CCFLAGS) -print-libgcc-file-name)

all: mmap.elf modules.elf

mmap.elf: start.o mmap.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

modules.elf: start.o modules.o libc.o
	$(LD) $(LDFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CCFLAGS) -c -o $@ $^

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $^
