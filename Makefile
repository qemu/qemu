CFLAGS=-Wall -O2 -g
LDFLAGS=-g
DEFINES=-D_GNU_SOURCE -DGEMU -DDOSEMU #-DNO_TRACE_MSGS

OBJS= i386/fp87.o i386/interp_main.o i386/interp_modrm.o i386/interp_16_32.o \
      i386/interp_32_16.o i386/interp_32_32.o i386/emu-utils.o \
      i386/dis8086.o i386/emu-ldt.o
OBJS+= elfload.o main.o thunk.o syscall.o

SRCS = $(OBJS:.o=.c)

all: gemu

gemu: $(OBJS)
	$(CC) -Wl,-T,i386.ld $(LDFLAGS) -o $@ $(OBJS)

depend: $(SRCS)
	$(CC) -MM $(CFLAGS) $^ 1>.depend

%.o: %.c
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

clean:
	rm -f *.o *~ i386/*.o i386/*~ gemu hello test1 test2 TAGS

hello: hello.c
	$(CC) -nostdlib $(CFLAGS) -static $(LDFLAGS) -o $@ $<

test1: test1.c
	$(CC) $(CFLAGS) -static $(LDFLAGS) -o $@ $<

test2: test2.c
	$(CC) $(CFLAGS) -static $(LDFLAGS) -o $@ $<

ifneq ($(wildcard .depend),)
include .depend
endif
