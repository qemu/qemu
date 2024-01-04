#
# asm-offsets adapted from the kernel, see
#   Kbuild
#   scripts/Kbuild.include
#   scripts/Makefile.build
#
#   Authors: Andrew Jones <drjones@redhat.com>
#

define sed-y
	"/^->/{s:->#\(.*\):/* \1 */:; \
	s:^->\([^ ]*\) [\$$#]*\([-0-9]*\) \(.*\):#define \1 \2 /* \3 */:; \
	s:^->\([^ ]*\) [\$$#]*\([^ ]*\) \(.*\):#define \1 \2 /* \3 */:; \
	s:->::; p;}"
endef

define make_asm_offsets
	(set -e; \
	 echo "#ifndef __ASM_OFFSETS_H__"; \
	 echo "#define __ASM_OFFSETS_H__"; \
	 echo "/*"; \
	 echo " * Generated file. DO NOT MODIFY."; \
	 echo " *"; \
	 echo " */"; \
	 echo ""; \
	 sed -ne $(sed-y) $<; \
	 echo ""; \
	 echo "#endif" ) > $@
endef

$(asm-offsets:.h=.s): $(asm-offsets:.h=.c)
	$(CC) $(CFLAGS) -fverbose-asm -S -o $@ $<

$(asm-offsets): $(asm-offsets:.h=.s)
	$(call make_asm_offsets)
	cp -f $(asm-offsets) lib/generated

asm_offsets_clean:
	$(RM) $(asm-offsets) $(asm-offsets:.h=.s) \
	      $(addprefix lib/generated/,$(notdir $(asm-offsets)))

