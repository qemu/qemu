
%.o: %.c
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")

%.o: %.S
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CLAGS) -c -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.m
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CFLAGS) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

LINK = $(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(1) $(ARLIBS_BEGIN) $(ARLIBS) $(ARLIBS_END) $(LIBS),"  LINK  $(TARGET_DIR)$@")

%$(EXESUF): %.o
	$(call LINK,$^)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))

# cc-option
# Usage: CFLAGS+=$(call cc-option, $(CFLAGS), -falign-functions=0, -malign-functions=0)

cc-option = $(shell if $(CC) $(1) $(2) -S -o /dev/null -xc /dev/null \
              > /dev/null 2>&1; then echo "$(2)"; else echo "$(3)"; fi ;)
