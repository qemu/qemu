libcacard_includedir=$(includedir)/cacard

TOOLS += vscclient$(EXESUF)

# objects linked into a shared library, built with libtool with -fPIC if required
libcacard-obj-y = $(libcacard-y)
libcacard-lobj-y=$(patsubst %.o,%.lo,$(libcacard-obj-y))

# libtool will build the .o files, too
$(libcacard-obj-y): | $(libcacard-lobj-y)

all: libcacard.la libcacard.pc

vscclient$(EXESUF): libcacard/vscclient.o libcacard.la
	$(call LINK,$^)

#########################################################################
# Rules for building libcacard standalone library

libcacard.la: LDFLAGS += -rpath $(libdir) -no-undefined \
	-export-symbols $(SRC_PATH)/libcacard/libcacard.syms
libcacard.la: $(libcacard-lobj-y)
	$(call LINK,$^)

libcacard.pc: $(SRC_PATH)/libcacard/libcacard.pc.in
	$(call quiet-command,sed -e 's|@LIBDIR@|$(libdir)|' \
		-e 's|@INCLUDEDIR@|$(libcacard_includedir)|' \
	    -e 's|@VERSION@|$(shell cat $(SRC_PATH)/VERSION)|' \
		-e 's|@PREFIX@|$(prefix)|' $< > libcacard.pc,\
	"  GEN   $@")

.PHONY: install-libcacard

install: install-libcacard
install-libcacard: libcacard.pc libcacard.la
	$(INSTALL_DIR) "$(DESTDIR)$(libdir)"
	$(INSTALL_DIR) "$(DESTDIR)$(libdir)/pkgconfig"
	$(INSTALL_DIR) "$(DESTDIR)$(libcacard_includedir)"
	$(INSTALL_LIB) libcacard.la "$(DESTDIR)$(libdir)"
	$(INSTALL_DATA) libcacard.pc "$(DESTDIR)$(libdir)/pkgconfig"
	for inc in $(SRC_PATH)/libcacard/*.h; do \
		$(INSTALL_DATA) $$inc "$(DESTDIR)$(libcacard_includedir)"; \
	done
