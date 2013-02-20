# This makefile is very special as it's meant to build as part of the build
# process and also within the source tree to update the translation files.

VERSION=$(shell cat ../VERSION)
TRANSLATIONS=de_DE it
SRCS=$(addsuffix .po, $(TRANSLATIONS))
OBJS=$(addsuffix .mo, $(TRANSLATIONS))

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

%.mo:
	@msgfmt -o $@ $(SRC_PATH)/po/`basename $@ .mo`.po

messages.po: $(SRC_PATH)/ui/gtk.c
	@xgettext -o $@ --foreign-user --package-name=QEMU --package-version=1.0.50 --msgid-bugs-address=qemu-devel@nongnu.org -k_ -C $<

de_DE.po: messages.po $(SRC_PATH)/ui/gtk.c
	@msgmerge $@ $< > $@.bak && mv $@.bak $@

it.po: messages.po $(SRC_PATH)/ui/gtk.c
	@msgmerge $@ $< > $@.bak && mv $@.bak $@

.PHONY: $(SRCS) clean all
