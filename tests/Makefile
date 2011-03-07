-include ../config-host.mak
-include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH)/tests)

QEMU=../i386-linux-user/qemu-i386
QEMU_X86_64=../x86_64-linux-user/qemu-x86_64
CC_X86_64=$(CC_I386) -m64

QEMU_INCLUDES += -I..
CFLAGS=-Wall -O2 -g -fno-strict-aliasing
#CFLAGS+=-msse2
LDFLAGS=

# TODO: automatically detect ARM and MIPS compilers, and run those too

# runcom maps page 0, so it requires root privileges
# also, pi_10.com runs indefinitely

I386_TESTS=hello-i386 \
	   linux-test \
	   testthread \
	   sha1-i386 \
	   test-i386 \
	   test-mmap \
	   # runcom

# native i386 compilers sometimes are not biarch.  assume cross-compilers are
ifneq ($(ARCH),i386)
I386_TESTS+=run-test-x86_64
endif

TESTS = test_path
ifneq ($(call find-in-path, $(CC_I386)),)
TESTS += $(I386_TESTS)
endif

all: $(patsubst %,run-%,$(TESTS))

# rules to run tests

.PHONY: $(patsubst %,run-%,$(TESTS))

run-%: %
	-$(QEMU) ./$*

run-hello-i386: hello-i386
run-linux-test: linux-test
run-testthread: testthread
run-sha1-i386: sha1-i386

run-test-i386: test-i386
	./test-i386 > test-i386.ref
	-$(QEMU) test-i386 > test-i386.out
	@if diff -u test-i386.ref test-i386.out ; then echo "Auto Test OK"; fi

run-test-x86_64: test-x86_64
	./test-x86_64 > test-x86_64.ref
	-$(QEMU_X86_64) test-x86_64 > test-x86_64.out
	@if diff -u test-x86_64.ref test-x86_64.out ; then echo "Auto Test OK"; fi

run-test-mmap: test-mmap
	-$(QEMU) ./test-mmap
	-$(QEMU) -p 8192 ./test-mmap 8192
	-$(QEMU) -p 16384 ./test-mmap 16384
	-$(QEMU) -p 32768 ./test-mmap 32768

run-runcom: runcom
	-$(QEMU) ./runcom $(SRC_PATH)/tests/pi_10.com

run-test_path: test_path
	./test_path

# rules to compile tests

test_path: test_path.o
test_path.o: test_path.c

hello-i386: hello-i386.c
	$(CC_I386) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<
	strip $@

testthread: testthread.c
	$(CC_I386) $(CFLAGS) $(LDFLAGS) -o $@ $< -lpthread

# i386/x86_64 emulation test (test various opcodes) */
test-i386: test-i386.c test-i386-code16.S test-i386-vm86.S \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC_I386) $(CFLAGS) $(LDFLAGS) -o $@ \
              $(<D)/test-i386.c $(<D)/test-i386-code16.S $(<D)/test-i386-vm86.S -lm

test-x86_64: test-i386.c \
           test-i386.h test-i386-shift.h test-i386-muldiv.h
	$(CC_X86_64) $(CFLAGS) $(LDFLAGS) -o $@ $(<D)/test-i386.c -lm

# generic Linux and CPU test
linux-test: linux-test.c
	$(CC_I386) $(CFLAGS) $(LDFLAGS) -o $@ $< -lm

# vm86 test
runcom: runcom.c
	$(CC_I386) $(CFLAGS) $(LDFLAGS) -o $@ $<

test-mmap: test-mmap.c
	$(CC_I386) -m32 $(CFLAGS) -Wall -O2 $(LDFLAGS) -o $@ $<

# speed test
sha1-i386: sha1.c
	$(CC_I386) $(CFLAGS) $(LDFLAGS) -o $@ $<

sha1: sha1.c
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $<

speed: sha1 sha1-i386
	time ./sha1
	time $(QEMU) ./sha1-i386

# broken test
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

# testsuite for the LM32 port.
test-lm32:
	$(MAKE) -C lm32 check

clean:
	rm -f *~ *.o test-i386.out test-i386.ref \
           test-x86_64.log test-x86_64.ref qruncom $(TESTS)
