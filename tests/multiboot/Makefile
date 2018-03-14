CC=gcc
CCFLAGS=-m32 -Wall -Wextra -Werror -fno-stack-protector -nostdinc -fno-builtin
ASFLAGS=-m32

LD=ld
LDFLAGS_ELF=-melf_i386 -T link.ld
LDFLAGS_BIN=-melf_i386 -T link.ld --oformat=binary
LIBS=$(shell $(CC) $(CCFLAGS) -print-libgcc-file-name)

AOUT_KLUDGE_BIN=$(foreach x,$(shell seq 1 9),aout_kludge_$x.bin)

all: mmap.elf modules.elf $(AOUT_KLUDGE_BIN)

mmap.elf: start.o mmap.o libc.o link.ld
	$(LD) $(LDFLAGS_ELF) -o $@ $^ $(LIBS)

modules.elf: start.o modules.o libc.o link.ld
	$(LD) $(LDFLAGS_ELF) -o $@ $^ $(LIBS)

aout_kludge_%.bin: aout_kludge_%.o link.ld
	$(LD) $(LDFLAGS_BIN) -o $@ $^ $(LIBS)

.PRECIOUS: aout_kludge_%.o
aout_kludge_%.o: aout_kludge.S
	$(CC) $(ASFLAGS) -DSCENARIO=$* -c -o $@ $^

%.o: %.c
	$(CC) $(CCFLAGS) -c -o $@ $^

%.o: %.S
	$(CC) $(ASFLAGS) -c -o $@ $^
