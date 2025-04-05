TARGET_ARCH=microblaze
TARGET_BIG_ENDIAN=y
# needed by boot.c
TARGET_NEED_FDT=y
TARGET_XML_FILES=gdb-xml/microblaze-core.xml gdb-xml/microblaze-stack-protect.xml
# System mode can address up to 64 bits via lea/sea instructions.
# TODO: These bypass the mmu, so we could emulate these differently.
TARGET_LONG_BITS=64
