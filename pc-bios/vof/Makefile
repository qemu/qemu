include config.mak
VPATH=$(SRC_DIR)
all: vof.bin

NULL :=
SPACE := $(NULL) #
TARGET_PREFIX := $(patsubst %/,%:$(SPACE),$(TARGET_DIR))

quiet-@ = $(if $(V),,@$(if $1,,printf "%s\n" "$(TARGET_PREFIX)$1" && ))
quiet-command = $(call quiet-@,$2 $@)$1

EXTRA_CFLAGS += -mcpu=power4

%.o: %.S
	$(call quiet-command, $(CC) $(EXTRA_CFLAGS) -c -o $@ $<,Assembling)

%.o: %.c
	$(call quiet-command, $(CC) $(EXTRA_CFLAGS) -c -fno-stack-protector -o $@ $<,Compiling)

vof.elf: entry.o main.o ci.o bootmem.o libc.o
	$(call quiet-command, $(LD) -nostdlib -e_start -T$(SRC_DIR)/vof.lds -EB -o $@ $^,Linking)

%.bin: %.elf
	$(call quiet-command, $(OBJCOPY) -O binary -j .text -j .data -j .toc -j .got2 $^ $@,Extracting raw object)

clean:
	rm -f *.o vof.bin vof.elf *~

distclean: clean

.PHONY: all clean distclean
