-include ../config-host.mak

$(call set-vpath, $(SRC_PATH)/tests)

CFLAGS=-Wall -O2 -g -fno-strict-aliasing
#CFLAGS+=-msse2
LDFLAGS=

ifeq ($(ARCH),i386)
TESTS=linux-test testthread sha1-i386 test-i386
endif
ifeq ($(ARCH),x86_64)
TESTS=test-x86_64
endif
TESTS+=sha1# test_path
#TESTS+=test_path
#TESTS+=runcom

QEMU=../i386-linux-user/qemu-i386

all: $(TESTS)

hello-i386: hello-i386.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<
	strip $@

testthread: testthread.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpthread

test_path: test_path.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<
	./$@ || { rm $@; exit 1; }

# i386/x86_64 emulation test (test various opcodes) */
test-i386: test-i386.c test-i386-code16.S test-i386-vm86.S \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC) -m32 $(CFLAGS) $(LDFLAGS) -static -o $@ \
              $(<D)/test-i386.c $(<D)/test-i386-code16.S $(<D)/test-i386-vm86.S -lm

test-x86_64: test-i386.c \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC) -m64 $(CFLAGS) $(LDFLAGS) -static -o $@ $(<D)/test-i386.c -lm

ifeq ($(ARCH),i386)
test: test-i386
	./test-i386 > test-i386.ref
else
test:
endif
	$(QEMU) test-i386 > test-i386.out
	@if diff -u test-i386.ref test-i386.out ; then echo "Auto Test OK"; fi

.PHONY: test-mmap
test-mmap: test-mmap.c
	$(CC) $(CFLAGS) -Wall -static -O2 $(LDFLAGS) -o $@ $<
	-./test-mmap
	-$(QEMU) ./test-mmap
	-$(QEMU) -p 8192 ./test-mmap 8192
	-$(QEMU) -p 16384 ./test-mmap 16384
	-$(QEMU) -p 32768 ./test-mmap 32768

# generic Linux and CPU test
linux-test: linux-test.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm

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

# NOTE: -fomit-frame-pointer is currently needed : this is a bug in libqemu
qruncom: qruncom.c ../ioport-user.c ../i386-user/libqemu.a
	$(CC) $(CFLAGS) -fomit-frame-pointer $(LDFLAGS) -I../target-i386 -I.. -I../i386-user -I../fpu \
              -o $@ $(filter %.c, $^) -L../i386-user -lqemu -lm

# arm test
hello-arm: hello-arm.o
	arm-linux-ld -o $@ $<

hello-arm.o: hello-arm.c
	arm-linux-gcc -Wall -g -O2 -c -o $@ $<

test-arm-iwmmxt: test-arm-iwmmxt.s
	cpp < $< | arm-linux-gnu-gcc -Wall -static -march=iwmmxt -mabi=aapcs -x assembler - -o $@

# MIPS test
hello-mips: hello-mips.c
	mips-linux-gnu-gcc -nostdlib -static -mno-abicalls -fno-PIC -mabi=32 -Wall -Wextra -g -O2 -o $@ $<

hello-mipsel: hello-mips.c
	mipsel-linux-gnu-gcc -nostdlib -static -mno-abicalls -fno-PIC -mabi=32 -Wall -Wextra -g -O2 -o $@ $<

# testsuite for the CRIS port.
test-cris:
	$(MAKE) -C cris check

clean:
	rm -f *~ *.o test-i386.out test-i386.ref \
           test-x86_64.log test-x86_64.ref qruncom $(TESTS)
