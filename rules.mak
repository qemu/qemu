
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

WL_U := -Wl,-u,
find-symbols = $(if $1, $(sort $(shell $(NM) -P -g $1 | $2)))
defined-symbols = $(call find-symbols,$1,awk '$$2!="U"{print $$1}')
undefined-symbols = $(call find-symbols,$1,awk '$$2=="U"{print $$1}')

# All the .mo objects in -m variables are also added into corresponding -y
# variable in unnest-vars, but filtered out here, when LINK is called.
#
# The .mo objects are supposed to be linked as a DSO, for module build. So here
# they are only used as a placeholders to generate those "archive undefined"
# symbol options (-Wl,-u,$symbol_name), which are the archive functions
# referenced by the code in the DSO.
#
# Also the presence in -y variables will also guarantee they are built before
# linking executables that will load them. So we can look up symbol reference
# in LINK.
#
# This is necessary because the exectuable itself may not use the function, in
# which case the function would not be linked in. Then the DSO loading will
# fail because of the missing symbol.
process-archive-undefs = $(filter-out %.a %.mo,$1) \
                $(addprefix $(WL_U), \
                     $(filter $(call defined-symbols,$(filter %.a, $1)), \
                              $(call undefined-symbols,$(filter %.mo,$1)))) \
                $(filter %.a,$1)

extract-libs = $(strip $(foreach o,$1,$($o-libs)))
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
LINK = $(call quiet-command, $(LINKPROG) $(QEMU_CFLAGS) $(CFLAGS) $(LDFLAGS) -o $@ \
       $(call process-archive-undefs, $1) \
       $(version-obj-y) $(call extract-libs,$1) $(LIBS),"  LINK  $(TARGET_DIR)$@")
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
       $(call process-archive-undefs, $1)\
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

%$(DSOSUF): CFLAGS += -fPIC -DBUILD_DSO
%$(DSOSUF): LDFLAGS += $(LDFLAGS_SHARED)
%$(DSOSUF): %.mo
	$(call LINK,$^)
	@# Copy to build root so modules can be loaded when program started without install
	$(if $(findstring /,$@),$(call quiet-command,cp $@ $(subst /,-,$@), "  CP    $(subst /,-,$@)"))


LD_REL := $(CC) -nostdlib -Wl,-r

%.mo:
	$(call quiet-command,$(LD_REL) -o $@ $^,"  LD -r $(TARGET_DIR)$@")

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

# install-prog list, dir
define install-prog
	$(INSTALL_DIR) "$2"
	$(INSTALL_PROG) $1 "$2"
	$(if $(STRIP),$(STRIP) $(foreach T,$1,"$2/$(notdir $T)"),)
endef

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

# save-vars
# Usage: $(call save-vars, vars)
# Save each variable $v in $vars as save-vars-$v, save their object's
# variables, then clear $v.
define save-vars
    $(foreach v,$1,
        $(eval save-vars-$v := $(value $v))
        $(foreach o,$($v),
            $(foreach k,cflags libs objs,
                $(if $($o-$k),
                    $(eval save-vars-$o-$k := $($o-$k))
                    $(eval $o-$k := ))))
        $(eval $v := ))
endef

# load-vars
# Usage: $(call load-vars, vars, add_var)
# Load the saved value for each variable in @vars, and the per object
# variables.
# Append @add_var's current value to the loaded value.
define load-vars
    $(eval $2-new-value := $(value $2))
    $(foreach v,$1,
        $(eval $v := $(value save-vars-$v))
        $(foreach o,$($v),
            $(foreach k,cflags libs objs,
                $(if $(save-vars-$o-$k),
                    $(eval $o-$k := $(save-vars-$o-$k))
                    $(eval save-vars-$o-$k := ))))
        $(eval save-vars-$v := ))
    $(eval $2 := $(value $2) $($2-new-value))
endef

# fix-paths
# Usage: $(call fix-paths, obj_path, src_path, vars)
# Add prefix @obj_path to all objects in @vars, and add prefix @src_path to all
# directories in @vars.
define fix-paths
    $(foreach v,$3,
        $(foreach o,$($v),
            $(if $($o-libs),
                $(eval $1$o-libs := $($o-libs)))
            $(if $($o-cflags),
                $(eval $1$o-cflags := $($o-cflags)))
            $(if $($o-objs),
                $(eval $1$o-objs := $(addprefix $1,$($o-objs)))))
        $(eval $v := $(addprefix $1,$(filter-out %/,$($v))) \
                     $(addprefix $2,$(filter %/,$($v)))))
endef

# unnest-var-recursive
# Usage: $(call unnest-var-recursive, obj_prefix, vars, var)
#
# Unnest @var by including subdir Makefile.objs, while protect others in @vars
# unchanged.
#
# @obj_prefix is the starting point of object path prefix.
#
define unnest-var-recursive
    $(eval dirs := $(sort $(filter %/,$($3))))
    $(eval $3 := $(filter-out %/,$($3)))
    $(foreach d,$(dirs:%/=%),
            $(call save-vars,$2)
            $(eval obj := $(if $1,$1/)$d)
            $(eval -include $(SRC_PATH)/$d/Makefile.objs)
            $(call fix-paths,$(if $1,$1/)$d/,$d/,$2)
            $(call load-vars,$2,$3)
            $(call unnest-var-recursive,$1,$2,$3))
endef

# unnest-vars
# Usage: $(call unnest-vars, obj_prefix, vars)
#
# @obj_prefix: object path prefix, can be empty, or '..', etc. Don't include
# ending '/'.
#
# @vars: the list of variable names to unnest.
#
# This macro will scan subdirectories's Makefile.objs, include them, to build
# up each variable listed in @vars.
#
# Per object and per module cflags and libs are saved with relative path fixed
# as well, those variables include -libs, -cflags and -objs. Items in -objs are
# also fixed to relative path against SRC_PATH plus the prefix @obj_prefix.
#
# All nested variables postfixed by -m in names are treated as DSO variables,
# and will be built as modules, if enabled.
#
# A simple example of the unnest:
#
#     obj_prefix = ..
#     vars = hot cold
#     hot  = fire.o sun.o season/
#     cold = snow.o water/ season/
#
# Unnest through a faked source directory structure:
#
#     SRC_PATH
#        ├── water
#        │   └── Makefile.objs──────────────────┐
#        │       │ hot += steam.o               │
#        │       │ cold += ice.mo               │
#        │       │ ice.mo-libs := -licemaker    │
#        │       │ ice.mo-objs := ice1.o ice2.o │
#        │       └──────────────────────────────┘
#        │
#        └── season
#            └── Makefile.objs──────┐
#                │ hot += summer.o  │
#                │ cold += winter.o │
#                └──────────────────┘
#
# In the end, the result will be:
#
#     hot  = ../fire.o ../sun.o ../season/summer.o
#     cold = ../snow.o ../water/ice.mo ../season/winter.o
#     ../water/ice.mo-libs = -licemaker
#     ../water/ice.mo-objs = ../water/ice1.o ../water/ice2.o
#
# Note that 'hot' didn't include 'season/' in the input, so 'summer.o' is not
# included.
#
define unnest-vars
    # In the case of target build (i.e. $1 == ..), fix path for top level
    # Makefile.objs objects
    $(if $1,$(call fix-paths,$1/,,$2))

    # Descend and include every subdir Makefile.objs
    $(foreach v, $2, $(call unnest-var-recursive,$1,$2,$v))

    $(foreach v,$(filter %-m,$2),
        # All .o found in *-m variables are single object modules, create .mo
        # for them
        $(foreach o,$(filter %.o,$($v)),
            $(eval $(o:%.o=%.mo)-objs := $o))
        # Now unify .o in -m variable to .mo
        $(eval $v := $($v:%.o=%.mo))
        $(eval modules-m += $($v))

        # For module build, build shared libraries during "make modules"
        # For non-module build, add -m to -y
        $(if $(CONFIG_MODULES),
             $(foreach o,$($v),
                   $(eval $o: $($o-objs)))
             $(eval $(patsubst %-m,%-y,$v) += $($v))
             $(eval modules: $($v:%.mo=%$(DSOSUF))),
             $(eval $(patsubst %-m,%-y,$v) += $(call expand-objs, $($v)))))

    # Post-process all the unnested vars
    $(foreach v,$2,
        $(foreach o, $(filter %.mo,$($v)),
            # Find all the .mo objects in variables and add dependency rules
            # according to .mo-objs. Report error if not set
            $(if $($o-objs),
                $(eval $(o:%.mo=%$(DSOSUF)): module-common.o $($o-objs)),
                $(error $o added in $v but $o-objs is not set))
            # Pass the .mo-cflags and .mo-libs along to member objects
            $(foreach p,$($o-objs),
                $(if $($o-cflags), $(eval $p-cflags += $($o-cflags)))
                $(if $($o-libs), $(eval $p-libs += $($o-libs)))))
        $(shell mkdir -p ./ $(sort $(dir $($v))))
        # Include all the .d files
        $(eval -include $(addsuffix *.d, $(sort $(dir $($v)))))
        $(eval $v := $(filter-out %/,$($v))))
endef
