CC=gcc
CFLAGS=-Wall -O2 -g
LDFLAGS=

TESTS=hello test1 test2 sha1 test-i386 
GEMU=../gemu

all: $(TESTS)

hello: hello.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<

test1: test1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

test2: test2.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

# i386 emulation test (dump various opcodes) */
test-i386: test-i386.c test-i386.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

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

# interpreter test
interp: interp.c interploop.c
	$(CC) $(CFLAGS) -fomit-frame-pointer $(LDFLAGS) -o $@ $^
