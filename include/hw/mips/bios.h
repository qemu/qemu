#ifndef HW_MIPS_BIOS_H
#define HW_MIPS_BIOS_H

#include "qemu/units.h"
#include "cpu.h"

#define BIOS_SIZE (4 * MiB)
#ifdef TARGET_WORDS_BIGENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif

#endif
