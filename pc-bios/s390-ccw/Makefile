all: build-all
# Dummy command so that make thinks it has done something
	@true

include config-host.mak
CFLAGS = -O2 -g
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

OBJECTS = start.o main.o bootmap.o jump2ipl.o sclp.o menu.o \
	  virtio.o virtio-scsi.o virtio-blkdev.o libc.o cio.o dasd-ipl.o

EXTRA_CFLAGS += -Wall
EXTRA_CFLAGS += -ffreestanding -fno-delete-null-pointer-checks -fno-common -fPIE
EXTRA_CFLAGS += -fwrapv -fno-strict-aliasing -fno-asynchronous-unwind-tables
EXTRA_CFLAGS += -msoft-float
EXTRA_CFLAGS += -std=gnu99
LDFLAGS += -Wl,-pie -nostdlib

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

LDFLAGS += -Wl,-pie -nostdlib -z noexecstack

build-all: s390-ccw.img s390-netboot.img

s390-ccw.elf: $(OBJECTS)
	$(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(OBJECTS),Linking)

s390-ccw.img: s390-ccw.elf
	$(call quiet-command,$(STRIP) --strip-unneeded $< -o $@,Stripping $< into)

$(OBJECTS): Makefile

include $(SRC_PATH)/netboot.mak

ALL_OBJS = $(sort $(OBJECTS) $(NETOBJS) $(LIBCOBJS) $(LIBNETOBJS))
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
