#
# NOTE: only compilable with x86 cross compile tools
#
include ../config-host.mak

DEFINES=

TARGETS=
ifeq ($(ARCH),i386)
TARGETS+=linux_boot.bin
endif

all: $(TARGETS)

linux_boot.bin: linux_boot.o
	ld --oformat binary -Ttext 0 -o $@ $<
	chmod a-x $@

%.o: %.S
	$(CC) $(DEFINES) -c -o $@ $<

clean:
	rm -f $(TARGETS) *.o *~

