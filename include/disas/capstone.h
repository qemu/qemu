#ifndef QEMU_CAPSTONE_H
#define QEMU_CAPSTONE_H

#ifdef CONFIG_CAPSTONE

#define CAPSTONE_AARCH64_COMPAT_HEADER
#define CAPSTONE_SYSTEMZ_COMPAT_HEADER
#include <capstone.h>

#else

/* Just enough to allow backends to init without ifdefs.  */

#define CS_ARCH_ARM     -1
#define CS_ARCH_ARM64   -1
#define CS_ARCH_MIPS    -1
#define CS_ARCH_X86     -1
#define CS_ARCH_PPC     -1
#define CS_ARCH_SPARC   -1
#define CS_ARCH_SYSZ    -1

#define CS_MODE_LITTLE_ENDIAN    0
#define CS_MODE_BIG_ENDIAN       0
#define CS_MODE_ARM              0
#define CS_MODE_16               0
#define CS_MODE_32               0
#define CS_MODE_64               0
#define CS_MODE_THUMB            0
#define CS_MODE_MCLASS           0
#define CS_MODE_V8               0
#define CS_MODE_MICRO            0
#define CS_MODE_MIPS3            0
#define CS_MODE_MIPS32R6         0
#define CS_MODE_MIPSGP64         0
#define CS_MODE_V9               0
#define CS_MODE_MIPS32           0
#define CS_MODE_MIPS64           0

#endif /* CONFIG_CAPSTONE */
#endif /* QEMU_CAPSTONE_H */
