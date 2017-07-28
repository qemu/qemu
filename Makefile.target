# -*- Mode: makefile -*-

BUILD_DIR?=$(CURDIR)/..

include ../config-host.mak
include config-target.mak
include config-devices.mak
include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH):$(BUILD_DIR))
ifdef CONFIG_LINUX
QEMU_CFLAGS += -I../linux-headers
endif
QEMU_CFLAGS += -I.. -I$(SRC_PATH)/target/$(TARGET_BASE_ARCH) -DNEED_CPU_H

QEMU_CFLAGS+=-I$(SRC_PATH)/include

ifdef CONFIG_USER_ONLY
# user emulator name
QEMU_PROG=qemu-$(TARGET_NAME)
QEMU_PROG_BUILD = $(QEMU_PROG)
else
# system emulator name
QEMU_PROG=qemu-system-$(TARGET_NAME)$(EXESUF)
ifneq (,$(findstring -mwindows,$(libs_softmmu)))
# Terminate program name with a 'w' because the linker builds a windows executable.
QEMU_PROGW=qemu-system-$(TARGET_NAME)w$(EXESUF)
$(QEMU_PROG): $(QEMU_PROGW)
	$(call quiet-command,$(OBJCOPY) --subsystem console $(QEMU_PROGW) $(QEMU_PROG),"GEN","$(TARGET_DIR)$(QEMU_PROG)")
QEMU_PROG_BUILD = $(QEMU_PROGW)
else
QEMU_PROG_BUILD = $(QEMU_PROG)
endif
endif

PROGS=$(QEMU_PROG) $(QEMU_PROGW)
STPFILES=

config-target.h: config-target.h-timestamp
config-target.h-timestamp: config-target.mak

ifdef CONFIG_TRACE_SYSTEMTAP
stap: $(QEMU_PROG).stp-installed $(QEMU_PROG).stp $(QEMU_PROG)-simpletrace.stp

ifdef CONFIG_USER_ONLY
TARGET_TYPE=user
else
TARGET_TYPE=system
endif

tracetool-y = $(SRC_PATH)/scripts/tracetool.py
tracetool-y += $(shell find $(SRC_PATH)/scripts/tracetool -name "*.py")

$(QEMU_PROG).stp-installed: $(BUILD_DIR)/trace-events-all $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=all \
		--format=stap \
		--backends=$(TRACE_BACKENDS) \
		--binary=$(bindir)/$(QEMU_PROG) \
		--target-name=$(TARGET_NAME) \
		--target-type=$(TARGET_TYPE) \
		$< > $@,"GEN","$(TARGET_DIR)$(QEMU_PROG).stp-installed")

$(QEMU_PROG).stp: $(BUILD_DIR)/trace-events-all $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=all \
		--format=stap \
		--backends=$(TRACE_BACKENDS) \
		--binary=$(realpath .)/$(QEMU_PROG) \
		--target-name=$(TARGET_NAME) \
		--target-type=$(TARGET_TYPE) \
		$< > $@,"GEN","$(TARGET_DIR)$(QEMU_PROG).stp")

$(QEMU_PROG)-simpletrace.stp: $(BUILD_DIR)/trace-events-all $(tracetool-y)
	$(call quiet-command,$(TRACETOOL) \
		--group=all \
		--format=simpletrace-stap \
		--backends=$(TRACE_BACKENDS) \
		--probe-prefix=qemu.$(TARGET_TYPE).$(TARGET_NAME) \
		$< > $@,"GEN","$(TARGET_DIR)$(QEMU_PROG)-simpletrace.stp")

else
stap:
endif
.PHONY: stap

all: $(PROGS) stap

# Dummy command so that make thinks it has done something
	@true

#########################################################
# cpu emulator library
obj-y += exec.o
obj-y += accel/
obj-$(CONFIG_TCG) += tcg/tcg.o tcg/tcg-op.o tcg/optimize.o
obj-$(CONFIG_TCG) += tcg/tcg-common.o tcg/tcg-runtime.o
obj-$(CONFIG_TCG_INTERPRETER) += tcg/tci.o
obj-$(CONFIG_TCG_INTERPRETER) += disas/tci.o
obj-y += fpu/softfloat.o
obj-y += target/$(TARGET_BASE_ARCH)/
obj-y += disas.o
obj-$(call notempty,$(TARGET_XML_FILES)) += gdbstub-xml.o
obj-$(call lnot,$(CONFIG_HAX)) += hax-stub.o

obj-$(CONFIG_LIBDECNUMBER) += libdecnumber/decContext.o
obj-$(CONFIG_LIBDECNUMBER) += libdecnumber/decNumber.o
obj-$(CONFIG_LIBDECNUMBER) += libdecnumber/dpd/decimal32.o
obj-$(CONFIG_LIBDECNUMBER) += libdecnumber/dpd/decimal64.o
obj-$(CONFIG_LIBDECNUMBER) += libdecnumber/dpd/decimal128.o

#########################################################
# Linux user emulator target

ifdef CONFIG_LINUX_USER

QEMU_CFLAGS+=-I$(SRC_PATH)/linux-user/$(TARGET_ABI_DIR) \
             -I$(SRC_PATH)/linux-user/host/$(ARCH) \
             -I$(SRC_PATH)/linux-user

obj-y += linux-user/
obj-y += gdbstub.o thunk.o user-exec.o user-exec-stub.o

endif #CONFIG_LINUX_USER

#########################################################
# BSD user emulator target

ifdef CONFIG_BSD_USER

QEMU_CFLAGS+=-I$(SRC_PATH)/bsd-user -I$(SRC_PATH)/bsd-user/$(TARGET_ABI_DIR) \
			 -I$(SRC_PATH)/bsd-user/$(HOST_VARIANT_DIR)

obj-y += bsd-user/
obj-y += gdbstub.o user-exec.o user-exec-stub.o

endif #CONFIG_BSD_USER

#########################################################
# System emulator target
ifdef CONFIG_SOFTMMU
obj-y += arch_init.o cpus.o monitor.o gdbstub.o balloon.o ioport.o numa.o
obj-y += qtest.o
obj-y += hw/
obj-y += memory.o
obj-y += memory_mapping.o
obj-y += dump.o
obj-y += migration/ram.o
LIBS := $(libs_softmmu) $(LIBS)

# Hardware support
ifeq ($(TARGET_NAME), sparc64)
obj-y += hw/sparc64/
else
obj-y += hw/$(TARGET_BASE_ARCH)/
endif

GENERATED_FILES += hmp-commands.h hmp-commands-info.h

endif # CONFIG_SOFTMMU

# Workaround for http://gcc.gnu.org/PR55489, see configure.
%/translate.o: QEMU_CFLAGS += $(TRANSLATE_OPT_CFLAGS)

dummy := $(call unnest-vars,,obj-y)
all-obj-y := $(obj-y)

target-obj-y :=
block-obj-y :=
common-obj-y :=
chardev-obj-y :=
include $(SRC_PATH)/Makefile.objs
dummy := $(call unnest-vars,,target-obj-y)
target-obj-y-save := $(target-obj-y)
dummy := $(call unnest-vars,.., \
               block-obj-y \
               block-obj-m \
               chardev-obj-y \
               crypto-obj-y \
               crypto-aes-obj-y \
               qom-obj-y \
               io-obj-y \
               common-obj-y \
               common-obj-m)
target-obj-y := $(target-obj-y-save)
all-obj-y += $(common-obj-y)
all-obj-y += $(target-obj-y)
all-obj-y += $(qom-obj-y)
all-obj-$(CONFIG_SOFTMMU) += $(block-obj-y) $(chardev-obj-y)
all-obj-$(CONFIG_USER_ONLY) += $(crypto-aes-obj-y)
all-obj-$(CONFIG_SOFTMMU) += $(crypto-obj-y)
all-obj-$(CONFIG_SOFTMMU) += $(io-obj-y)

$(QEMU_PROG_BUILD): config-devices.mak

COMMON_LDADDS = ../libqemuutil.a ../libqemustub.a

# build either PROG or PROGW
$(QEMU_PROG_BUILD): $(all-obj-y) $(COMMON_LDADDS)
	$(call LINK, $(filter-out %.mak, $^))
ifdef CONFIG_DARWIN
	$(call quiet-command,Rez -append $(SRC_PATH)/pc-bios/qemu.rsrc -o $@,"REZ","$(TARGET_DIR)$@")
	$(call quiet-command,SetFile -a C $@,"SETFILE","$(TARGET_DIR)$@")
endif

gdbstub-xml.c: $(TARGET_XML_FILES) $(SRC_PATH)/scripts/feature_to_c.sh
	$(call quiet-command,rm -f $@ && $(SHELL) $(SRC_PATH)/scripts/feature_to_c.sh $@ $(TARGET_XML_FILES),"GEN","$(TARGET_DIR)$@")

hmp-commands.h: $(SRC_PATH)/hmp-commands.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$(TARGET_DIR)$@")

hmp-commands-info.h: $(SRC_PATH)/hmp-commands-info.hx $(SRC_PATH)/scripts/hxtool
	$(call quiet-command,sh $(SRC_PATH)/scripts/hxtool -h < $< > $@,"GEN","$(TARGET_DIR)$@")

clean: clean-target
	rm -f *.a *~ $(PROGS)
	rm -f $(shell find . -name '*.[od]')
	rm -f hmp-commands.h gdbstub-xml.c
ifdef CONFIG_TRACE_SYSTEMTAP
	rm -f *.stp
endif

install: all
ifneq ($(PROGS),)
	$(call install-prog,$(PROGS),$(DESTDIR)$(bindir))
endif
ifdef CONFIG_TRACE_SYSTEMTAP
	$(INSTALL_DIR) "$(DESTDIR)$(qemu_datadir)/../systemtap/tapset"
	$(INSTALL_DATA) $(QEMU_PROG).stp-installed "$(DESTDIR)$(qemu_datadir)/../systemtap/tapset/$(QEMU_PROG).stp"
	$(INSTALL_DATA) $(QEMU_PROG)-simpletrace.stp "$(DESTDIR)$(qemu_datadir)/../systemtap/tapset/$(QEMU_PROG)-simpletrace.stp"
endif

GENERATED_FILES += config-target.h
Makefile: $(GENERATED_FILES)
