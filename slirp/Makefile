ROOT_DIR := $(shell dirname $(realpath $(lastword $(MAKEFILE_LIST))))
BUILD_DIR ?= .

LIBSLIRP = $(BUILD_DIR)/libslirp.a

all: $(LIBSLIRP)

SRCS := $(wildcard src/*.c)
OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)
DEPS := $(OBJS:%.o=%.d)

INC_DIRS := $(BUILD_DIR)/src
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

override CFLAGS +=					\
	-DG_LOG_DOMAIN='"Slirp"'			\
	$(shell $(PKG_CONFIG) --cflags glib-2.0)	\
	$(INC_FLAGS)					\
	-MMD -MP
override LDFLAGS += $(shell $(PKG_CONFIG) --libs glib-2.0)

$(LIBSLIRP): $(OBJS)

.PHONY: clean

clean:
	rm -r $(OBJS) $(DEPS) $(LIBSLIRP)

$(BUILD_DIR)/src/%.o: $(ROOT_DIR)/src/%.c
	@$(MKDIR_P) $(dir $@)
	$(call quiet-command,$(CC) $(CFLAGS) -c -o $@ $<,"CC","$@")

%.a:
	$(call quiet-command,rm -f $@ && $(AR) rcs $@ $^,"AR","$@")

PKG_CONFIG ?= pkg-config
MKDIR_P ?= mkdir -p
quiet-command-run = $(if $(V),,$(if $2,printf "  %-7s %s\n" $2 $3 && ))$1
quiet-@ = $(if $(V),,@)
quiet-command = $(quiet-@)$(call quiet-command-run,$1,$2,$3)

print-%:
	@echo '$*=$($*)'

.SUFFIXES:

-include $(DEPS)
