CC=gcc
CFLAGS=-Wall -O2 -g
LDFLAGS=

TESTS=hello test2 sha1 test-i386
TESTS+=op-i386.o #op-i386.o op-ppc.o op-arm.o op-mips.o op-sparc.o

GEMU=../gemu

all: $(TESTS)

hello: hello.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<

test2: test2.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

# i386 emulation test (dump various opcodes) */
test-i386: test-i386.c test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ $< -lm

test: test-i386
	./test-i386 > test-i386.ref
	$(GEMU) test-i386 > test-i386.out
	@if diff -u test-i386.ref test-i386.out ; then echo "Auto Test OK"; fi

# dyngen tests
op-i386.o: op.c
	gcc $(CFLAGS) -c -o $@ $<

op-ppc.o: op.c
	powerpc-linux-gcc $(CFLAGS) -c -o $@ $<

op-arm.o: op.c
	arm-linux-gcc $(CFLAGS) -c -o $@ $<

op-mips.o: op.c
	mips-linux-gcc $(CFLAGS) -mno-abicalls -c -o $@ $<

op-sparc.o: op.c
	sparc-linux-gcc $(CFLAGS) -mflat -c -o $@ $<

# speed test
sha1: sha1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

speed: sha1
	time ./sha1
	time $(GEMU) sha1

# interpreter test
interp: interp.c interploop.c
	$(CC) $(CFLAGS) -fomit-frame-pointer $(LDFLAGS) -o $@ $^
