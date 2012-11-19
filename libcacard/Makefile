-include ../config-host.mak
-include $(SRC_PATH)/rules.mak
-include $(SRC_PATH)/Makefile.objs

libcacard_includedir=$(includedir)/cacard

$(call set-vpath, $(SRC_PATH))

# objects linked into a shared library, built with libtool with -fPIC if required
QEMU_OBJS=$(oslib-obj-y) qemu-timer-common.o $(trace-obj-y) $(stub-obj-y)
QEMU_OBJS_LIB=$(patsubst %.o,%.lo,$(QEMU_OBJS))

QEMU_CFLAGS+=-I../

libcacard.lib-y=$(patsubst %.o,%.lo,$(libcacard-y))

vscclient: $(libcacard-y) $(QEMU_OBJS) vscclient.o cutils.o
	$(call quiet-command,$(CC) -o $@ $^ $(libcacard_libs) $(LIBS),"  LINK  $@")

clean:
	rm -f *.o */*.o *.d */*.d *.a */*.a *~ */*~ vscclient *.lo */*.lo .libs/* */.libs/* *.la */*.la *.pc
	rm -Rf .libs */.libs

all: libcacard.la libcacard.pc
# Dummy command so that make thinks it has done something
	@true

#########################################################################
# Rules for building libcacard standalone library

ifeq ($(LIBTOOL),)
libcacard.la:
	@echo "libtool is missing, please install and rerun configure"; exit 1

install-libcacard:
	@echo "libtool is missing, please install and rerun configure"; exit 1
else
libcacard.la: $(libcacard.lib-y) $(QEMU_OBJS_LIB)
	$(call quiet-command,$(LIBTOOL) --mode=link --quiet --tag=CC $(CC) -rpath $(libdir) -o $@ $^ $(libcacard_libs),"  lt LINK $@")

libcacard_srcpath=$(SRC_PATH)/libcacard
libcacard.pc: $(libcacard_srcpath)/libcacard.pc.in
	$(call quiet-command,sed -e 's|@LIBDIR@|$(libdir)|' \
		-e 's|@INCLUDEDIR@|$(libcacard_includedir)|' \
	    -e 's|@VERSION@|$(shell cat $(SRC_PATH)/VERSION)|' \
		-e 's|@PREFIX@|$(prefix)|' \
		< $(libcacard_srcpath)/libcacard.pc.in > libcacard.pc,\
	"  GEN   $@")

.PHONY: install-libcacard

install-libcacard: libcacard.pc libcacard.la vscclient
	$(INSTALL_DIR) "$(DESTDIR)$(libdir)"
	$(INSTALL_DIR) "$(DESTDIR)$(libdir)/pkgconfig"
	$(INSTALL_DIR) "$(DESTDIR)$(libcacard_includedir)"
	$(INSTALL_DIR) "$(DESTDIR)$(bindir)"
	$(LIBTOOL) --mode=install $(INSTALL_PROG) vscclient "$(DESTDIR)$(bindir)"
	$(LIBTOOL) --mode=install $(INSTALL_DATA) libcacard.la "$(DESTDIR)$(libdir)"
	$(LIBTOOL) --mode=install $(INSTALL_DATA) libcacard.pc "$(DESTDIR)$(libdir)/pkgconfig"
	for inc in *.h; do \
		$(LIBTOOL) --mode=install $(INSTALL_DATA) $(libcacard_srcpath)/$$inc "$(DESTDIR)$(libcacard_includedir)"; \
	done
endif
