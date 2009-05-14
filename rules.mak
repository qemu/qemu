
%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")

%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) -c -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.m
	$(call quiet-command,$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

WAS=-Wl,--whole-archive
WAE=-Wl,--no-whole-archive

LINK = $(call quiet-command,$(CC) $(LDFLAGS) -o $@ $(1) $(LIBS) $(WAS) $(ARLIBS) $(WAE),"  LINK  $(TARGET_DIR)$@")

%$(EXESUF): %.o
	$(call LINK,$^)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))
