/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu indentification for x86.
 */

#ifndef HOST_CPUINFO_H
#define HOST_CPUINFO_H

/* Digested version of <cpuid.h> */

#define CPUINFO_ALWAYS          (1u << 0)  /* so cpuinfo is nonzero */
#define CPUINFO_CMOV            (1u << 1)
#define CPUINFO_MOVBE           (1u << 2)
#define CPUINFO_LZCNT           (1u << 3)
#define CPUINFO_POPCNT          (1u << 4)
#define CPUINFO_BMI1            (1u << 5)
#define CPUINFO_BMI2            (1u << 6)
#define CPUINFO_SSE2            (1u << 7)
#define CPUINFO_SSE4            (1u << 8)
#define CPUINFO_AVX1            (1u << 9)
#define CPUINFO_AVX2            (1u << 10)
#define CPUINFO_AVX512F         (1u << 11)
#define CPUINFO_AVX512VL        (1u << 12)
#define CPUINFO_AVX512BW        (1u << 13)
#define CPUINFO_AVX512DQ        (1u << 14)
#define CPUINFO_AVX512VBMI2     (1u << 15)
#define CPUINFO_ATOMIC_VMOVDQA  (1u << 16)
#define CPUINFO_ATOMIC_VMOVDQU  (1u << 17)

/* Initialized with a constructor. */
extern unsigned cpuinfo;

/*
 * We cannot rely on constructor ordering, so other constructors must
 * use the function interface rather than the variable above.
 */
unsigned cpuinfo_init(void);

#endif /* HOST_CPUINFO_H */
