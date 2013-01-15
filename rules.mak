
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

# Flags for dependency generation
QEMU_DGFLAGS += -MMD -MP -MT $@ -MF $(*D)/$(*F).d

# Same as -I$(SRC_PATH) -I., but for the nested source/object directories
QEMU_CFLAGS += -I$(<D) -I$(@D)

%.o: %.c
	$(call quiet-command,$(CC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")

ifeq ($(LIBTOOL),)
LIBTOOL = /bin/false
LINK = $(call quiet-command,$(CC) $(QEMU_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
       $(sort $(filter %.o, $1)) $(filter-out %.o, $1) $(version-obj-y) \
       $(LIBS),"  LINK  $(TARGET_DIR)$@")
else
LIBTOOL += $(if $(V),,--quiet)
%.lo: %.c
	$(call quiet-command,$(LIBTOOL) --mode=compile --tag=CC $(CC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -c -o $@ $<,"  lt CC $@")
%.lo: %.dtrace
	$(call quiet-command,$(LIBTOOL) --mode=compile --tag=CC dtrace -o $@ -G -s $<, " lt GEN $(TARGET_DIR)$@")

LINK = $(call quiet-command,\
       $(if $(filter %.lo %.la,$^),$(LIBTOOL) --mode=link --tag=CC \
       )$(CC) $(QEMU_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
       $(sort $(filter %.o, $1)) $(filter-out %.o, $1) $(version-obj-y) \
       $(LIBS),$(if $(filter %.lo %.la,$^),"lt LINK ", "  LINK  ")"$(TARGET_DIR)$@")
endif

%.asm: %.S
	$(call quiet-command,$(CPP) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -o $@ $<,"  CPP   $(TARGET_DIR)$@")

%.o: %.asm
	$(call quiet-command,$(AS) $(ASFLAGS) -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.m
	$(call quiet-command,$(OBJCC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

%.o: %.dtrace
	$(call quiet-command,dtrace -o $@ -G -s $<, "  GEN   $(TARGET_DIR)$@")

%$(EXESUF): %.o
	$(call LINK,$^)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))

# cc-option
# Usage: CFLAGS+=$(call cc-option, -falign-functions=0, -malign-functions=0)

cc-option = $(if $(shell $(CC) $1 $2 -S -o /dev/null -xc /dev/null \
              >/dev/null 2>&1 && echo OK), $2, $3)

VPATH_SUFFIXES = %.c %.h %.S %.m %.mak %.texi %.sh
set-vpath = $(if $1,$(foreach PATTERN,$(VPATH_SUFFIXES),$(eval vpath $(PATTERN) $1)))

# find-in-path
# Usage: $(call find-in-path, prog)
# Looks in the PATH if the argument contains no slash, else only considers one
# specific directory.  Returns an # empty string if the program doesn't exist
# there.
find-in-path = $(if $(find-string /, $1), \
        $(wildcard $1), \
        $(wildcard $(patsubst %, %/$1, $(subst :, ,$(PATH)))))

# Generate files with tracetool
TRACETOOL=$(PYTHON) $(SRC_PATH)/scripts/tracetool.py

# Generate timestamp files for .h include files

config-%.h: config-%.h-timestamp
	@cmp $< $@ >/dev/null 2>&1 || cp $< $@

config-%.h-timestamp: config-%.mak
	$(call quiet-command, sh $(SRC_PATH)/scripts/create_config < $< > $@, "  GEN   $(TARGET_DIR)config-$*.h")

.PHONY: clean-timestamp
clean-timestamp:
	rm -f *.timestamp
clean: clean-timestamp

# will delete the target of a rule if commands exit with a nonzero exit status
.DELETE_ON_ERROR:

# magic to descend into other directories

obj := .
old-nested-dirs :=

define push-var
$(eval save-$2-$1 = $(value $1))
$(eval $1 :=)
endef

define pop-var
$(eval subdir-$2-$1 := $(if $(filter $2,$(save-$2-$1)),$(addprefix $2,$($1))))
$(eval $1 = $(value save-$2-$1) $$(subdir-$2-$1))
$(eval save-$2-$1 :=)
endef

define unnest-dir
$(foreach var,$(nested-vars),$(call push-var,$(var),$1/))
$(eval obj := $(obj)/$1)
$(eval include $(SRC_PATH)/$1/Makefile.objs)
$(eval obj := $(patsubst %/$1,%,$(obj)))
$(foreach var,$(nested-vars),$(call pop-var,$(var),$1/))
endef

define unnest-vars-1
$(eval nested-dirs := $(filter-out \
    $(old-nested-dirs), \
    $(sort $(foreach var,$(nested-vars), $(filter %/, $($(var)))))))
$(if $(nested-dirs),
  $(foreach dir,$(nested-dirs),$(call unnest-dir,$(patsubst %/,%,$(dir))))
  $(eval old-nested-dirs := $(old-nested-dirs) $(nested-dirs))
  $(call unnest-vars-1))
endef

define unnest-vars
$(call unnest-vars-1)
$(foreach var,$(nested-vars),$(eval $(var) := $(filter-out %/, $($(var)))))
$(shell mkdir -p $(sort $(foreach var,$(nested-vars),$(dir $($(var))))))
$(foreach var,$(nested-vars), $(eval \
  -include $(addsuffix *.d, $(sort $(dir $($(var)))))))
endef
