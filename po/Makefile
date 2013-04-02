# This makefile is very special as it's meant to build as part of the build
# process and also within the source tree to update the translation files.

VERSION=$(shell cat ../VERSION)
SRCS=$(filter-out messages.po,$(wildcard *.po))
OBJS=$(patsubst %.po,%.mo,$(SRCS))

SRC_PATH=..

-include ../config-host.mak

vpath %.po $(SRC_PATH)/po

all:
	@echo Use 'make update' to update translation files
	@echo or us 'make build' or 'make install' to build and install
	@echo the translation files

update: $(SRCS)

build: $(OBJS)

clean:
	$(RM) $(OBJS)

install: $(OBJS)
	for obj in $(OBJS); do \
	    base=`basename $$obj .mo`; \
	    $(INSTALL) -d $(DESTDIR)$(prefix)/share/locale/$$base/LC_MESSAGES; \
	    $(INSTALL) -m644 $$obj $(DESTDIR)$(prefix)/share/locale/$$base/LC_MESSAGES/qemu.mo; \
	done

%.mo: %.po
	@msgfmt -o $@ $(SRC_PATH)/po/`basename $@ .mo`.po

messages.po: $(SRC_PATH)/ui/gtk.c
	@xgettext -o $@ --foreign-user --package-name=QEMU --package-version=$(VERSION) --msgid-bugs-address=qemu-devel@nongnu.org -k_ -C $<

%.po: messages.po
	@msgmerge $@ $< > $@.bak && mv $@.bak $@

.PHONY: clean all
