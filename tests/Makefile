include ../config.mak

CFLAGS=-Wall -O2 -g
LDFLAGS=

ifeq ($(ARCH),i386)
TESTS=testclone testsig testthread sha1-i386 test-i386 runcom
endif
TESTS+=sha1 test_path

QEMU=../qemu

all: $(TESTS)

hello: hello.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<
	strip hello

testclone: testclone.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

testsig: testsig.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

testthread: testthread.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpthread

test_path: test_path.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	./$@ || { rm $@; exit 1; }

# i386 emulation test (test various opcodes) */
test-i386: test-i386.c test-i386-code16.S test-i386-vm86.S \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ test-i386.c \
              test-i386-code16.S test-i386-vm86.S -lm

ifeq ($(ARCH),i386)
test: test-i386
	./test-i386 > test-i386.ref
else
test:
endif
	$(QEMU) test-i386 > test-i386.out
	@if diff -u test-i386.ref test-i386.out ; then echo "Auto Test OK"; fi

# speed test
sha1-i386: sha1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

sha1: sha1.c
	$(HOST_CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

speed: sha1 sha1-i386
	time ./sha1
	time $(QEMU) ./sha1-i386

# vm86 test
runcom: runcom.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

clean:
	rm -f *~ *.o $(TESTS)
