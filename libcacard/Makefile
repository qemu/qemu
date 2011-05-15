-include ../config-host.mak
-include $(SRC_PATH)/Makefile.objs
-include $(SRC_PATH)/rules.mak

$(call set-vpath, $(SRC_PATH):$(SRC_PATH)/libcacard)

# objects linked against normal qemu binaries, not compiled with libtool
QEMU_OBJS=$(addprefix ../,$(oslib-obj-y) qemu-malloc.o qemu-timer-common.o $(trace-obj-y))

# objects linked into a shared library, built with libtool with -fPIC if required
QEMU_OBJS_LIB=$(addsuffix .lo,$(basename $(QEMU_OBJS)))

QEMU_CFLAGS+=-I../

libcacard.lib-y=$(addsuffix .lo,$(basename $(libcacard-y)))

vscclient: $(libcacard-y) $(QEMU_OBJS) vscclient.o
	$(call quiet-command,$(CC) $(libcacard_libs) -lrt -o $@ $^,"  LINK  $@")

clean:
	rm -f *.o */*.o *.d */*.d *.a */*.a *~ */*~ vscclient *.lo .libs/* *.la
	rm -Rf .libs

all: vscclient

#########################################################################
# Rules for building libcacard standalone library

ifeq ($(LIBTOOL),)
libcacard.la:
	@echo "libtool is missing, please install and rerun configure"; exit 1

install-libcacard:
	@echo "libtool is missing, please install and rerun configure"; exit 1
else
libcacard.la: $(libcacard.lib-y) $(QEMU_OBJS_LIB)
	$(call quiet-command,libtool --mode=link --quiet --tag=CC $(CC) $(libcacard_libs) -lrt -rpath $(libdir) -o $@ $^,"  lt LINK $@")

install-libcacard: libcacard.la
	$(INSTALL_DIR) "$(DESTDIR)$(libdir)"
	libtool --mode=install $(INSTALL_PROG) libcacard.la "$(DESTDIR)$(libdir)"
endif
