include ../config.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=

ifeq ($(ARCH),i386)
TESTS=hello test2 sha1 test-i386
endif

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

# speed test
sha1: sha1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

speed: sha1
	time ./sha1
	time $(GEMU) sha1

clean:
	rm -f *~ *.o $(TESTS)
