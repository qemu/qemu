
%.o: %.c
	$(call quiet-command,$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<,CC $@)

%.o: %.S
	$(call quiet-command,$(CC) $(CPPFLAGS) -c -o $@ $<,AS $@)

%.o: %.m
	$(call quiet-command,$(CC) $(CFLAGS) $(CPPFLAGS) -c -o $@ $<,OBJC $@)

LINK = $(call quiet-command,$(CC) $(LDFLAGS) -o $@ $^ $(LIBS),LINK $@)

%$(EXESUF): %.o
	$(LINK)

quiet-command = $(if $(V),$1,@echo $2 && $1)
