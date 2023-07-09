/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Host specific cpu indentification for ppc.
 */

#ifndef HOST_CPUINFO_H
#define HOST_CPUINFO_H

/* Digested version of <cpuid.h> */

#define CPUINFO_ALWAYS          (1u << 0)  /* so cpuinfo is nonzero */
#define CPUINFO_V2_06           (1u << 1)
#define CPUINFO_V2_07           (1u << 2)
#define CPUINFO_V3_0            (1u << 3)
#define CPUINFO_V3_1            (1u << 4)
#define CPUINFO_ISEL            (1u << 5)
#define CPUINFO_ALTIVEC         (1u << 6)
#define CPUINFO_VSX             (1u << 7)
#define CPUINFO_CRYPTO          (1u << 8)

/* Initialized with a constructor. */
extern unsigned cpuinfo;

/*
 * We cannot rely on constructor ordering, so other constructors must
 * use the function interface rather than the variable above.
 */
unsigned cpuinfo_init(void);

#endif /* HOST_CPUINFO_H */
