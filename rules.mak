
# Don't use implicit rules or variables
# we have explicit rules for everything
MAKEFLAGS += -rR

# Files with this suffixes are final, don't try to generate them
# using implicit rules
%.d:
%.h:
%.c:
%.cc:
%.cpp:
%.m:
%.mak:

# Flags for C++ compilation
QEMU_CXXFLAGS = -D__STDC_LIMIT_MACROS $(filter-out -Wstrict-prototypes -Wmissing-prototypes -Wnested-externs -Wold-style-declaration -Wold-style-definition -Wredundant-decls, $(QEMU_CFLAGS))

# Flags for dependency generation
QEMU_DGFLAGS += -MMD -MP -MT $@ -MF $(*D)/$(*F).d

# Same as -I$(SRC_PATH) -I., but for the nested source/object directories
QEMU_INCLUDES += -I$(<D) -I$(@D)

maybe-add = $(filter-out $1, $2) $1
extract-libs = $(strip $(sort $(foreach o,$1,$($o-libs))) \
                  $(foreach o,$(call expand-objs,$1),$($o-libs)))
expand-objs = $(strip $(sort $(filter %.o,$1)) \
                  $(foreach o,$(filter %.mo,$1),$($o-objs)) \
                  $(filter-out %.o %.mo,$1))

%.o: %.c
	$(call quiet-command,$(CC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) $($@-cflags) -c -o $@ $<,"  CC    $(TARGET_DIR)$@")
%.o: %.rc
	$(call quiet-command,$(WINDRES) -I. -o $@ $<,"  RC    $(TARGET_DIR)$@")

# If we have a CXX we might have some C++ objects, in which case we
# must link with the C++ compiler, not the plain C compiler.
LINKPROG = $(or $(CXX),$(CC))

ifeq ($(LIBTOOL),)
LINK = $(call quiet-command,$(LINKPROG) $(QEMU_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
       $(call expand-objs,$1) $(version-obj-y) \
       $(call extract-libs,$1) $(LIBS),"  LINK  $(TARGET_DIR)$@")
else
LIBTOOL += $(if $(V),,--quiet)
%.lo: %.c
	$(call quiet-command,$(LIBTOOL) --mode=compile --tag=CC $(CC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) $($*.o-cflags) -c -o $@ $<,"  lt CC $@")
%.lo: %.rc
	$(call quiet-command,$(LIBTOOL) --mode=compile --tag=RC $(WINDRES) -I. -o $@ $<,"lt RC   $(TARGET_DIR)$@")
%.lo: %.dtrace
	$(call quiet-command,$(LIBTOOL) --mode=compile --tag=CC dtrace -o $@ -G -s $<, " lt GEN $(TARGET_DIR)$@")

LINK = $(call quiet-command,\
       $(if $(filter %.lo %.la,$1),$(LIBTOOL) --mode=link --tag=CC \
       )$(LINKPROG) $(QEMU_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
       $(call expand-objs,$1) \
       $(if $(filter %.lo %.la,$1),$(version-lobj-y),$(version-obj-y)) \
       $(if $(filter %.lo %.la,$1),$(LIBTOOLFLAGS)) \
       $(call extract-libs,$(1:.lo=.o)) $(LIBS),$(if $(filter %.lo %.la,$1),"lt LINK ", "  LINK  ")"$(TARGET_DIR)$@")
endif

%.asm: %.S
	$(call quiet-command,$(CPP) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) -o $@ $<,"  CPP   $(TARGET_DIR)$@")

%.o: %.asm
	$(call quiet-command,$(AS) $(ASFLAGS) -o $@ $<,"  AS    $(TARGET_DIR)$@")

%.o: %.cc
	$(call quiet-command,$(CXX) $(QEMU_INCLUDES) $(QEMU_CXXFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) $($@-cflags) -c -o $@ $<,"  CXX   $(TARGET_DIR)$@")

%.o: %.cpp
	$(call quiet-command,$(CXX) $(QEMU_INCLUDES) $(QEMU_CXXFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) $($@-cflags) -c -o $@ $<,"  CXX   $(TARGET_DIR)$@")

%.o: %.m
	$(call quiet-command,$(OBJCC) $(QEMU_INCLUDES) $(QEMU_CFLAGS) $(QEMU_DGFLAGS) $(CFLAGS) $($@-cflags) -c -o $@ $<,"  OBJC  $(TARGET_DIR)$@")

%.o: %.dtrace
	$(call quiet-command,dtrace -o $@ -G -s $<, "  GEN   $(TARGET_DIR)$@")

DSO_CFLAGS := -fPIC -DBUILD_DSO
%$(DSOSUF): LDFLAGS += $(LDFLAGS_SHARED)
%$(DSOSUF): %.mo libqemustub.a
	$(call LINK,$^)
	@# Copy to build root so modules can be loaded when program started without install
	$(if $(findstring /,$@),$(call quiet-command,cp $@ $(subst /,-,$@), "  CP    $(subst /,-,$@)"))

.PHONY: modules
modules:

%$(EXESUF): %.o
	$(call LINK,$^)

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"  AR    $(TARGET_DIR)$@")

quiet-command = $(if $(V),$1,$(if $(2),@echo $2 && $1, @$1))

# cc-option
# Usage: CFLAGS+=$(call cc-option, -falign-functions=0, -malign-functions=0)

cc-option = $(if $(shell $(CC) $1 $2 -S -o /dev/null -xc /dev/null \
              >/dev/null 2>&1 && echo OK), $2, $3)

VPATH_SUFFIXES = %.c %.h %.S %.cc %.cpp %.m %.mak %.texi %.sh %.rc
set-vpath = $(if $1,$(foreach PATTERN,$(VPATH_SUFFIXES),$(eval vpath $(PATTERN) $1)))

# find-in-path
# Usage: $(call find-in-path, prog)
# Looks in the PATH if the argument contains no slash, else only considers one
# specific directory.  Returns an # empty string if the program doesn't exist
# there.
find-in-path = $(if $(find-string /, $1), \
        $(wildcard $1), \
        $(wildcard $(patsubst %, %/$1, $(subst :, ,$(PATH)))))

# Logical functions (for operating on y/n values like CONFIG_FOO vars)
# Inputs to these must be either "y" (true) or "n" or "" (both false)
# Output is always either "y" or "n".
# Usage: $(call land,$(CONFIG_FOO),$(CONFIG_BAR))
# Logical NOT
lnot = $(if $(subst n,,$1),n,y)
# Logical AND
land = $(if $(findstring yy,$1$2),y,n)
# Logical OR
lor = $(if $(findstring y,$1$2),y,n)
# Logical XOR (note that this is the inverse of leqv)
lxor = $(if $(filter $(call lnot,$1),$(call lnot,$2)),n,y)
# Logical equivalence (note that leqv "","n" is true)
leqv = $(if $(filter $(call lnot,$1),$(call lnot,$2)),y,n)
# Logical if: like make's $(if) but with an leqv-like test
lif = $(if $(subst n,,$1),$2,$3)

# String testing functions: inputs to these can be any string;
# the output is always either "y" or "n". Leading and trailing whitespace
# is ignored when comparing strings.
# String equality
eq = $(if $(subst $2,,$1)$(subst $1,,$2),n,y)
# String inequality
ne = $(if $(subst $2,,$1)$(subst $1,,$2),y,n)
# Emptiness/non-emptiness tests:
isempty = $(if $1,n,y)
notempty = $(if $1,y,n)

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

define push-var
$(eval save-$2-$1 = $(value $1))
$(eval $1 :=)
endef

define pop-var
$(eval subdir-$2-$1 := $(if $(filter $2,$(save-$2-$1)),$(addprefix $2,$($1))))
$(eval $1 = $(value save-$2-$1) $$(subdir-$2-$1))
$(eval save-$2-$1 :=)
endef

define fix-obj-vars
$(if $2, $(foreach v,$($1), \
	$(if $($v-cflags), \
		$(eval $2/$v-cflags := $($v-cflags)) \
		$(eval $v-cflags := )) \
	$(if $($v-libs), \
		$(eval $2/$v-libs := $($v-libs)) \
		$(eval $v-libs := )) \
	$(if $($v-objs), \
		$(eval $2/$v-objs := $(addprefix $2/,$($v-objs))) \
		$(eval $v-objs := ))))
endef

define unnest-dir
$(foreach var,$(nested-vars),$(call push-var,$(var),$1/))
$(eval obj-parent-$1 := $(obj))
$(eval obj := $(if $(obj),$(obj)/$1,$1))
$(eval include $(SRC_PATH)/$1/Makefile.objs)
$(foreach v,$(nested-vars),$(call fix-obj-vars,$v,$(obj)))
$(eval obj := $(obj-parent-$1))
$(eval obj-parent-$1 := )
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

define process-modules
$(foreach o,$(filter %.o,$($1)),
	$(eval $(patsubst %.o,%.mo,$o): $o) \
	$(eval $(patsubst %.o,%.mo,$o)-objs := $o))
$(foreach o,$(filter-out $(modules-m), $(patsubst %.o,%.mo,$($1))), \
    $(eval $o-objs += module-common.o)
    $(eval $o: $($o-objs))
    $(eval modules-objs-m += $($o-objs))
    $(eval modules-m += $o)
    $(eval $o:; $$(call quiet-command,touch $$@,"  GEN   $$(TARGET_DIR)$$@"))
    $(if $(CONFIG_MODULES),$(eval modules: $(patsubst %.mo,%$(DSOSUF),$o)))) \
$(eval modules-objs-m := $(sort $(modules-objs-m)))
$(foreach o,$(modules-objs-m), \
    $(if $(CONFIG_MODULES),$(eval $o-cflags := $(call maybe-add, $(DSO_CFLAGS), $($o-cflags)))))
$(eval $(patsubst %-m,%-$(call lnot,$(CONFIG_MODULES)),$1) += $($1))
endef

define unnest-vars
$(eval obj := $1)
$(eval nested-vars := $2)
$(foreach v,$(nested-vars),$(call fix-obj-vars,$v,$(obj)))
$(eval old-nested-dirs := )
$(call unnest-vars-1)
$(if $1,$(foreach v,$(nested-vars),$(eval \
	$v := $(addprefix $1/,$($v)))))
$(foreach var,$(nested-vars),$(eval $(var) := $(filter-out %/, $($(var)))))
$(shell mkdir -p $(sort $(foreach var,$(nested-vars),$(dir $($(var))))))
$(foreach var,$(nested-vars), $(eval \
  -include $(addsuffix *.d, $(sort $(dir $($(var)))))))
$(foreach v,$(filter %-m,$(nested-vars)), \
    $(call process-modules,$v))
endef
