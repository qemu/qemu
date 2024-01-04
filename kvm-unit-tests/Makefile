
ifeq ($(wildcard config.mak),)
$(error run ./configure first. See ./configure -h)
endif

include config.mak

DESTDIR := $(PREFIX)/share/qemu/tests

.PHONY: arch_clean clean distclean cscope

#make sure env CFLAGS variable is not used
CFLAGS =

libgcc := $(shell $(CC) --print-libgcc-file-name)

libcflat := lib/libcflat.a
cflatobjs := \
	lib/argv.o \
	lib/printf.o \
	lib/string.o \
	lib/abort.o \
	lib/report.o

# libfdt paths
LIBFDT_objdir = lib/libfdt
LIBFDT_srcdir = lib/libfdt
LIBFDT_archive = $(LIBFDT_objdir)/libfdt.a
LIBFDT_include = $(addprefix $(LIBFDT_srcdir)/,$(LIBFDT_INCLUDES))
LIBFDT_version = $(addprefix $(LIBFDT_srcdir)/,$(LIBFDT_VERSION))

#include architecure specific make rules
include config/config-$(ARCH).mak

# cc-option
# Usage: OP_CFLAGS+=$(call cc-option, -falign-functions=0, -malign-functions=0)

cc-option = $(shell if $(CC) $(1) -S -o /dev/null -xc /dev/null \
              > /dev/null 2>&1; then echo "$(1)"; else echo "$(2)"; fi ;)

CFLAGS += -g
CFLAGS += $(autodepend-flags) -Wall
CFLAGS += $(call cc-option, -fomit-frame-pointer, "")
CFLAGS += $(call cc-option, -fno-stack-protector, "")
CFLAGS += $(call cc-option, -fno-stack-protector-all, "")

CXXFLAGS += $(CFLAGS)

autodepend-flags = -MMD -MF $(dir $*).$(notdir $*).d

LDFLAGS += $(CFLAGS)
LDFLAGS += -pthread -lrt

$(libcflat): $(cflatobjs)
	$(AR) rcs $@ $^

include $(LIBFDT_srcdir)/Makefile.libfdt
$(LIBFDT_archive): CFLAGS += -ffreestanding -I lib -I lib/libfdt -Wno-sign-compare
$(LIBFDT_archive): $(addprefix $(LIBFDT_objdir)/,$(LIBFDT_OBJS))
	$(AR) rcs $@ $^

%.o: %.S
	$(CC) $(CFLAGS) -c -nostdlib -o $@ $<

-include */.*.d */*/.*.d

install:
	mkdir -p $(DESTDIR)
	install $(tests_and_config) $(DESTDIR)

clean: arch_clean
	$(RM) lib/.*.d $(libcflat) $(cflatobjs)

libfdt_clean:
	$(RM) $(LIBFDT_archive) \
	$(addprefix $(LIBFDT_objdir)/,$(LIBFDT_OBJS)) \
	$(LIBFDT_objdir)/.*.d

distclean: clean libfdt_clean
	$(RM) lib/asm config.mak $(TEST_DIR)-run test.log msr.out cscope.*

cscope: common_dirs = lib lib/libfdt lib/asm lib/asm-generic
cscope:
	$(RM) ./cscope.*
	find -L $(TEST_DIR) lib/$(TEST_DIR) lib/$(ARCH) $(common_dirs) -maxdepth 1 \
		-name '*.[chsS]' -print | sed 's,^\./,,' | sort -u > ./cscope.files
	cscope -bk
