
# Don't use implicit rules or variables
# we have explicit rules for everything
MAKEFLAGS += -rR

# Files with this suffixes are final, don't try to generate them
# using implicit rules
%.d:
%.h:
%.c:
%.m:
%.mak:

QEMU_CFLAGS += -MMD -MP -MT $@

%.o: %.c $(GENERATED_HEADERS)
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")

%.o: %.S
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CLAGS) -c -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.m
	$(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CFLAGS) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

LINK = $(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ $(1) $(ARLIBS_BEGIN) $(ARLIBS) $(ARLIBS_END) $(LIBS),"  LINK  $(TARGET_DIR)$@")

%$(EXESUF): %.o
	$(call LINK,$^)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))

# cc-option
# Usage: CFLAGS+=$(call cc-option, -falign-functions=0, -malign-functions=0)

cc-option = $(if $(shell $(CC) $1 $2 -S -o /dev/null -xc /dev/null \
              >/dev/null 2>&1 && echo OK), $2, $3)

# Generate timestamp files for .h include files

%.h: %.h-timestamp
	@test -f $@ || cp $< $@

%.h-timestamp: %.mak
	$(call quiet-command, $(SRC_PATH)/create_config < $< > $@, "  GEN   $*.h")
	@cmp $@ $*.h >/dev/null 2>&1 || cp $@ $*.h
