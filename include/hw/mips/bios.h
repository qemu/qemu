#include "cpu.h"

#define BIOS_SIZE (4 * 1024 * 1024)
#ifdef TARGET_WORDS_BIGENDIAN
#define BIOS_FILENAME "mips_bios.bin"
#else
#define BIOS_FILENAME "mipsel_bios.bin"
#endif
