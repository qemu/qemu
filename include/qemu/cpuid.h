/* cpuid.h: Macros to identify the properties of an x86 host.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef QEMU_CPUID_H
#define QEMU_CPUID_H

#ifndef CONFIG_CPUID_H
# error "<cpuid.h> is unusable with this compiler"
#endif

#include <cpuid.h>

/* Cover the uses that we have within qemu.  */
/* ??? Irritating that we have the same information in target/i386/.  */

/* Leaf 1, %edx */
#ifndef bit_CMOV
#define bit_CMOV        (1 << 15)
#endif
#ifndef bit_SSE2
#define bit_SSE2        (1 << 26)
#endif

/* Leaf 1, %ecx */
#ifndef bit_SSE4_1
#define bit_SSE4_1      (1 << 19)
#endif
#ifndef bit_MOVBE
#define bit_MOVBE       (1 << 22)
#endif
#ifndef bit_OSXSAVE
#define bit_OSXSAVE     (1 << 27)
#endif
#ifndef bit_AVX
#define bit_AVX         (1 << 28)
#endif

/* Leaf 7, %ebx */
#ifndef bit_BMI
#define bit_BMI         (1 << 3)
#endif
#ifndef bit_AVX2
#define bit_AVX2        (1 << 5)
#endif
#ifndef bit_AVX512F
#define bit_AVX512F        (1 << 16)
#endif
#ifndef bit_BMI2
#define bit_BMI2        (1 << 8)
#endif

/* Leaf 0x80000001, %ecx */
#ifndef bit_LZCNT
#define bit_LZCNT       (1 << 5)
#endif

#endif /* QEMU_CPUID_H */
