/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu indentification for x86.
 */

#include "qemu/osdep.h"
#include "host/cpuinfo.h"
#ifdef CONFIG_CPUID_H
# include "qemu/cpuid.h"
#endif

unsigned cpuinfo;

/* Called both as constructor and (possibly) via other constructors. */
unsigned __attribute__((constructor)) cpuinfo_init(void)
{
    unsigned info = cpuinfo;

    if (info) {
        return info;
    }

#ifdef CONFIG_CPUID_H
    unsigned max, a, b, c, d, b7 = 0, c7 = 0;

    max = __get_cpuid_max(0, 0);

    if (max >= 7) {
        __cpuid_count(7, 0, a, b7, c7, d);
        info |= (b7 & bit_BMI ? CPUINFO_BMI1 : 0);
        info |= (b7 & bit_BMI2 ? CPUINFO_BMI2 : 0);
    }

    if (max >= 1) {
        __cpuid(1, a, b, c, d);

        info |= (d & bit_CMOV ? CPUINFO_CMOV : 0);
        info |= (d & bit_SSE2 ? CPUINFO_SSE2 : 0);
        info |= (c & bit_SSE4_1 ? CPUINFO_SSE4 : 0);
        info |= (c & bit_MOVBE ? CPUINFO_MOVBE : 0);
        info |= (c & bit_POPCNT ? CPUINFO_POPCNT : 0);

        /* Our AES support requires PSHUFB as well. */
        info |= ((c & bit_AES) && (c & bit_SSSE3) ? CPUINFO_AES : 0);

        /* For AVX features, we must check available and usable. */
        if ((c & bit_AVX) && (c & bit_OSXSAVE)) {
            unsigned bv = xgetbv_low(0);

            if ((bv & 6) == 6) {
                info |= CPUINFO_AVX1;
                info |= (b7 & bit_AVX2 ? CPUINFO_AVX2 : 0);

                if ((bv & 0xe0) == 0xe0) {
                    info |= (b7 & bit_AVX512F ? CPUINFO_AVX512F : 0);
                    info |= (b7 & bit_AVX512VL ? CPUINFO_AVX512VL : 0);
                    info |= (b7 & bit_AVX512BW ? CPUINFO_AVX512BW : 0);
                    info |= (b7 & bit_AVX512DQ ? CPUINFO_AVX512DQ : 0);
                    info |= (c7 & bit_AVX512VBMI2 ? CPUINFO_AVX512VBMI2 : 0);
                }

                /*
                 * The Intel SDM has added:
                 *   Processors that enumerate support for Intel® AVX
                 *   (by setting the feature flag CPUID.01H:ECX.AVX[bit 28])
                 *   guarantee that the 16-byte memory operations performed
                 *   by the following instructions will always be carried
                 *   out atomically:
                 *   - MOVAPD, MOVAPS, and MOVDQA.
                 *   - VMOVAPD, VMOVAPS, and VMOVDQA when encoded with VEX.128.
                 *   - VMOVAPD, VMOVAPS, VMOVDQA32, and VMOVDQA64 when encoded
                 *     with EVEX.128 and k0 (masking disabled).
                 * Note that these instructions require the linear addresses
                 * of their memory operands to be 16-byte aligned.
                 *
                 * AMD has provided an even stronger guarantee that processors
                 * with AVX provide 16-byte atomicity for all cachable,
                 * naturally aligned single loads and stores, e.g. MOVDQU.
                 *
                 * See https://gcc.gnu.org/bugzilla/show_bug.cgi?id=104688
                 */
                __cpuid(0, a, b, c, d);
                if (c == signature_INTEL_ecx) {
                    info |= CPUINFO_ATOMIC_VMOVDQA;
                } else if (c == signature_AMD_ecx) {
                    info |= CPUINFO_ATOMIC_VMOVDQA | CPUINFO_ATOMIC_VMOVDQU;
                }
            }
        }
    }

    max = __get_cpuid_max(0x8000000, 0);
    if (max >= 1) {
        __cpuid(0x80000001, a, b, c, d);
        info |= (c & bit_LZCNT ? CPUINFO_LZCNT : 0);
    }
#endif

    info |= CPUINFO_ALWAYS;
    cpuinfo = info;
    return info;
}
