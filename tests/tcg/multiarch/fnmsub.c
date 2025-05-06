/* SPDX-License-Identifier: GPL-2.0-or-later */

#include <stdio.h>
#include <math.h>
#include <fenv.h>

union U {
  double d;
  unsigned long long l;
};

union U x = { .l = 0x4ff0000000000000ULL };
union U y = { .l = 0x2ff0000000000000ULL };
union U r;

int main()
{
#ifdef FE_DOWNWARD
    fesetround(FE_DOWNWARD);

#if defined(__loongarch__)
    asm("fnmsub.d %0, %1, %1, %2" : "=f"(r.d) : "f"(x.d), "f"(y.d));
#elif defined(__powerpc64__)
    asm("fnmsub %0,%1,%1,%2" : "=f"(r.d) : "f"(x.d), "f"(y.d));
#elif defined(__s390x__) && 0 /* need -march=z14 */
    asm("vfnms %0,%1,%1,%2,0,3" : "=f"(r.d) : "f"(x.d), "f"(y.d));
#else
    r.d = -fma(x.d, x.d, -y.d);
#endif

    if (r.l != 0xdfefffffffffffffULL) {
        printf("r = %.18a (%016llx)\n", r.d, r.l);
        return 1;
    }
#endif
    return 0;
}
