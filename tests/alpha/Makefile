CROSS=alpha-linux-gnu-
CC=$(CROSS)gcc
AS=$(CROSS)as

SIM=../../alpha-linux-user/qemu-alpha

CFLAGS=-O
LINK=$(CC) -v -o $@ crt.o $< -nostdlib

TESTS=test-cond test-cmov

all: hello-alpha $(TESTS)

hello-alpha: hello-alpha.o crt.o
	$(LINK)

test-cond: test-cond.o crt.o
	$(LINK)

test-cmov.o: test-cond.c
	$(CC) -c $(CFLAGS) -DTEST_CMOV -o $@ $<

test-cmov: test-cmov.o crt.o
	$(LINK)

clean:
	$(RM) *.o *~ hello-alpha $(TESTS)

.PHONY: clean all
