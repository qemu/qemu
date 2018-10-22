# This makefile is very special as it's meant to build as part of the build
# process and also within the source tree to update the translation files.

# Set SRC_PATH for in-tree builds without configuration.
SRC_PATH=..

# The default target must come before any include statements.
all:

.PHONY:	all build clean install update

%.mo: %.po
	$(call quiet-command, msgfmt -o $@ $<,"GEN","$@")

-include ../config-host.mak
include $(SRC_PATH)/rules.mak

PO_PATH=$(SRC_PATH)/po

VERSION=$(shell cat $(SRC_PATH)/VERSION)
SRCS=$(filter-out $(PO_PATH)/messages.po,$(wildcard $(PO_PATH)/*.po))
OBJS=$(patsubst $(PO_PATH)/%.po,%.mo,$(SRCS))

vpath %.po $(PO_PATH)

all:
	@echo "Use 'make update' to update translation files or use 'make build'"
	@echo "or 'make install' to build and install the translation files."

update: $(SRCS)

build: $(OBJS)

clean:
	rm -f $(OBJS)

install: $(OBJS)
	for obj in $(OBJS); do \
	    base=$$(basename $$obj .mo); \
	    $(INSTALL) -d $(DESTDIR)$(prefix)/share/locale/$$base/LC_MESSAGES; \
	    $(INSTALL) -m644 $$obj $(DESTDIR)$(prefix)/share/locale/$$base/LC_MESSAGES/qemu.mo; \
	done

$(PO_PATH)/messages.po: $(SRC_PATH)/ui/gtk.c
	$(call quiet-command, ( cd $(SRC_PATH) && \
          xgettext -o - --from-code=UTF-8 --foreign-user --no-location \
	    --package-name=QEMU --package-version=$(VERSION) \
	    --msgid-bugs-address=qemu-devel@nongnu.org -k_ -C ui/gtk.c | \
	  sed -e s/CHARSET/UTF-8/) >$@,"GEN","$@")

$(PO_PATH)/%.po: $(PO_PATH)/messages.po
	$(call quiet-command, msgmerge -q $@ $< > $@.bak && mv $@.bak $@,"GEN","$@")
