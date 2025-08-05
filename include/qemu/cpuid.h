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
#ifndef bit_PCLMUL
#define bit_PCLMUL      (1 << 1)
#endif
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
#ifndef bit_BMI2
#define bit_BMI2        (1 << 8)
#endif
#ifndef bit_AVX512F
#define bit_AVX512F     (1 << 16)
#endif
#ifndef bit_AVX512DQ
#define bit_AVX512DQ    (1 << 17)
#endif
#ifndef bit_AVX512BW
#define bit_AVX512BW    (1 << 30)
#endif
#ifndef bit_AVX512VL
#define bit_AVX512VL    (1u << 31)
#endif

/* Leaf 7, %ecx */
#ifndef bit_AVX512VBMI2
#define bit_AVX512VBMI2 (1 << 6)
#endif
#ifndef bit_GFNI
#define bit_GFNI        (1 << 8)
#endif

/* Leaf 0x80000001, %ecx */
#ifndef bit_LZCNT
#define bit_LZCNT       (1 << 5)
#endif

/*
 * Signatures for different CPU implementations as returned from Leaf 0.
 */

#ifndef signature_INTEL_ecx
/* "Genu" "ineI" "ntel" */
#define signature_INTEL_ebx     0x756e6547
#define signature_INTEL_edx     0x49656e69
#define signature_INTEL_ecx     0x6c65746e
#endif

#ifndef signature_AMD_ecx
/* "Auth" "enti" "cAMD" */
#define signature_AMD_ebx       0x68747541
#define signature_AMD_edx       0x69746e65
#define signature_AMD_ecx       0x444d4163
#endif

static inline unsigned xgetbv_low(unsigned c)
{
    unsigned a, d;
    asm("xgetbv" : "=a"(a), "=d"(d) : "c"(c));
    return a;
}

#endif /* QEMU_CPUID_H */
