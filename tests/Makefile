include ../config.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=

ifeq ($(ARCH),i386)
TESTS=test2 sha1-i386 test-i386
endif
TESTS+=sha1

GEMU=../gemu

all: $(TESTS)

hello: hello.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<
	strip hello

test2: test2.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

# i386 emulation test (test various opcodes) */
test-i386: test-i386.c test-i386-code16.S \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC) $(CFLAGS) $(LDFLAGS) -static -o $@ test-i386.c test-i386-code16.S -lm

test: test-i386
ifeq ($(ARCH),i386)
	./test-i386 > test-i386.ref
endif
	$(GEMU) test-i386 > test-i386.out
	@if diff -u test-i386.ref test-i386.out ; then echo "Auto Test OK"; fi

# speed test
sha1-i386: sha1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

sha1: sha1.c
	$(HOST_CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

speed: sha1 sha1-i386
	time ./sha1
	time $(GEMU) ./sha1-i386

clean:
	rm -f *~ *.o $(TESTS)
