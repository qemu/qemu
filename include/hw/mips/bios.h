#ifndef HW_MIPS_BIOS_H
#define HW_MIPS_BIOS_H

#include "qemu/units.h"
#include "cpu.h"

#define BIOS_SIZE (4 * MiB)
#if TARGET_BIG_ENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif

#endif
