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
