-include ../config-host.mak
-include $(SRC_PATH)/rules.mak
-include $(SRC_PATH)/Makefile.objs

libcacard_includedir=$(includedir)/cacard

$(call set-vpath, $(SRC_PATH))

# objects linked into a shared library, built with libtool with -fPIC if required
libcacard-obj-y=$(oslib-obj-y) error.o $(trace-obj-y) $(stub-obj-y) $(libcacard-y)
libcacard-lobj-y=$(patsubst %.o,%.lo,$(libcacard-obj-y))

# libtool will build the .o files, too
$(libcacard-obj-y): | $(libcacard-lobj-y)

QEMU_CFLAGS+=-I../

vscclient: LIBS += $(libcacard_libs)
vscclient: vscclient.o $(libcacard-obj-y)
	$(call LINK,$^)

clean:
	rm -f *.o */*.o *.d */*.d *.a */*.a *~ */*~ vscclient *.lo */*.lo .libs/* */.libs/* *.la */*.la *.pc
	rm -Rf .libs */.libs

all: libcacard.la libcacard.pc
# Dummy command so that make thinks it has done something
	@true

#########################################################################
# Rules for building libcacard standalone library

libcacard.la: LDFLAGS += -rpath $(libdir) -no-undefined \
	-export-syms $(SRC_PATH)/libcacard/libcacard.syms
libcacard.la: LIBS += $(libcacard_libs)
libcacard.la: $(libcacard-lobj-y)
	$(call LINK,$^)

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
	$(INSTALL_PROG) vscclient "$(DESTDIR)$(bindir)"
	$(INSTALL_LIB) libcacard.la "$(DESTDIR)$(libdir)"
	$(INSTALL_DATA) libcacard.pc "$(DESTDIR)$(libdir)/pkgconfig"
	for inc in *.h; do \
		$(INSTALL_DATA) $(libcacard_srcpath)/$$inc "$(DESTDIR)$(libcacard_includedir)"; \
	done
