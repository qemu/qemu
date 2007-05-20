#
# NOTE: only compilable with x86 cross compile tools
#
include ../config-host.mak

DEFINES=

TARGETS=

all: $(TARGETS)

%.o: %.S
	$(CC) $(DEFINES) -c -o $@ $<

clean:
	rm -f $(TARGETS) *.o *~

mips_bios.bin: mips_bios.o
	mipsel-linux-uclibc-ld --oformat binary -Ttext 0 -o $@ $<
	chmod a-x $@

mips_bios.o: mips_bios.c
	mipsel-linux-uclibc-gcc -c -O2 -Wall -o $@ $<
