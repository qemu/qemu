all: build-all
# Dummy command so that make thinks it has done something
	@true

include config-host.mak
CFLAGS = -O2 -g -I $(SRC_PATH)/../../include/hw/s390x/ipl
LDFLAGS ?=
MAKEFLAGS += -rR

GIT_SUBMODULES = roms/SLOF

NULL :=
SPACE := $(NULL) #
TARGET_PREFIX := $(patsubst %/,%:$(SPACE),$(TARGET_DIR))

quiet-@ = $(if $(V),,@$(if $1,printf "%s\n" "$(TARGET_PREFIX)$1" && ))
quiet-command = $(call quiet-@,$2 $@)$1

VPATH_SUFFIXES = %.c %.h %.S %.m %.mak %.sh %.rc Kconfig% %.json.in
set-vpath = $(if $1,$(foreach PATTERN,$(VPATH_SUFFIXES),$(eval vpath $(PATTERN) $1)))
$(call set-vpath, $(SRC_PATH))

# Flags for dependency generation
QEMU_DGFLAGS = -MMD -MP -MT $@ -MF $(@D)/$(*F).d

%.o: %.c
	$(call quiet-command,$(CC) $(EXTRA_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) \
	       -c -o $@ $<,Compiling)

%.o: %.S
	$(call quiet-command,$(CCAS) $(EXTRA_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) \
	       -c -o $@ $<,Assembling)

.PHONY : all clean build-all distclean

OBJECTS = start.o main.o bootmap.o jump2ipl.o sclp.o menu.o netmain.o \
	  virtio.o virtio-net.o virtio-scsi.o virtio-blkdev.o cio.o dasd-ipl.o

SLOF_DIR := $(SRC_PATH)/../../roms/SLOF

LIBC_INC := -nostdinc -I$(SLOF_DIR)/lib/libc/include
LIBNET_INC := -I$(SLOF_DIR)/lib/libnet

EXTRA_CFLAGS += -Wall
EXTRA_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -fno-common -fPIE
EXTRA_CFLAGS += -fwrapv -fno-strict-aliasing -fno-asynchronous-unwind-tables
EXTRA_CFLAGS += -msoft-float
EXTRA_CFLAGS += -std=gnu99
EXTRA_CFLAGS += $(LIBC_INC) $(LIBNET_INC)
EXTRA_LDFLAGS += -Wl,-pie -nostdlib -z noexecstack -z text

cc-test = $(CC) -Werror $1 -c -o /dev/null -xc /dev/null >/dev/null 2>/dev/null
cc-option = if $(call cc-test, $1); then \
    echo "$(TARGET_PREFIX)$1 detected" && echo "EXTRA_CFLAGS += $1" >&3; else \
    echo "$(TARGET_PREFIX)$1 not detected" $(if $2,&& echo "EXTRA_CFLAGS += $2" >&3); fi

config-cc.mak: Makefile
	$(quiet-@)($(call cc-option,-Wno-stringop-overflow); \
	    $(call cc-option,-fno-stack-protector); \
	    $(call cc-option,-Wno-array-bounds); \
	    $(call cc-option,-Wno-gnu); \
	    $(call cc-option,-march=z900,-march=z10)) 3> config-cc.mak
-include config-cc.mak

# libc files:

LIBC_CFLAGS = $(EXTRA_CFLAGS) $(CFLAGS) $(LIBC_INC) $(LIBNET_INC) \
	      -MMD -MP -MT $@ -MF $(@:%.o=%.d)

CTYPE_OBJS = isdigit.o isxdigit.o toupper.o
%.o : $(SLOF_DIR)/lib/libc/ctype/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,Compiling)

STRING_OBJS = strcat.o strchr.o strrchr.o strcpy.o strlen.o strncpy.o \
	      strcmp.o strncmp.o strcasecmp.o strncasecmp.o strstr.o \
	      memset.o memcpy.o memmove.o memcmp.o
%.o : $(SLOF_DIR)/lib/libc/string/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,Compiling)

STDLIB_OBJS = atoi.o atol.o strtoul.o strtol.o rand.o malloc.o free.o
%.o : $(SLOF_DIR)/lib/libc/stdlib/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,Compiling)

STDIO_OBJS = sprintf.o snprintf.o vfprintf.o vsnprintf.o vsprintf.o fprintf.o \
	     printf.o putc.o puts.o putchar.o stdchnls.o fileno.o
%.o : $(SLOF_DIR)/lib/libc/stdio/%.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,Compiling)

sbrk.o: $(SLOF_DIR)/slof/sbrk.c
	$(call quiet-command,$(CC) $(LIBC_CFLAGS) -c -o $@ $<,Compiling)

LIBCOBJS := $(STRING_OBJS) $(CTYPE_OBJS) $(STDLIB_OBJS) $(STDIO_OBJS) sbrk.o

libc.a: $(LIBCOBJS)
	$(call quiet-command,$(AR) -rc $@ $^,Creating static library)

# libnet files:

LIBNETOBJS := args.o dhcp.o dns.o icmpv6.o ipv6.o tcp.o udp.o bootp.o \
	      dhcpv6.o ethernet.o ipv4.o ndp.o tftp.o pxelinux.o
LIBNETCFLAGS = $(EXTRA_CFLAGS) $(CFLAGS) $(LIBC_INC) $(LIBNET_INC) \
	       -DDHCPARCH=0x1F -MMD -MP -MT $@ -MF $(@:%.o=%.d)

%.o : $(SLOF_DIR)/lib/libnet/%.c
	$(call quiet-command,$(CC) $(LIBNETCFLAGS) -c -o $@ $<,Compiling)

libnet.a: $(LIBNETOBJS)
	$(call quiet-command,$(AR) -rc $@ $^,Creating static library)

# Main targets:

build-all: s390-ccw.img

s390-ccw.elf: $(OBJECTS) libnet.a libc.a
	$(call quiet-command,$(CC) $(EXTRA_LDFLAGS) $(LDFLAGS) -o $@ $^,Linking)

s390-ccw.img: s390-ccw.elf
	$(call quiet-command,$(STRIP) --strip-unneeded $< -o $@,Stripping $< into)

$(OBJECTS): Makefile

ALL_OBJS = $(sort $(OBJECTS) $(LIBCOBJS) $(LIBNETOBJS))
-include $(ALL_OBJS:%.o=%.d)

clean:
	rm -f *.o *.d *.img *.elf *~ *.a

distclean:
	rm -f config-cc.mak

.PHONY: git-submodule-update
$(SRC_PATH)/../../.git-submodule-status: git-submodule-update config-host.mak
Makefile: $(SRC_PATH)/../../.git-submodule-status

git-submodule-update:
ifneq ($(GIT_SUBMODULES_ACTION),ignore)
	$(quiet-@)GIT=git "$(SRC_PATH)/../../scripts/git-submodule.sh" $(GIT_SUBMODULES_ACTION) $(GIT_SUBMODULES)
endif
