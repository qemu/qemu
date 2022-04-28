/*
 * vxeh2_vcvt: vector-enhancements facility 2 vector convert *
 */
#include <stdint.h>
#include "vx.h"

#define M_S 8
#define M4_XxC 4
#define M4_def M4_XxC

static inline void vcfps(S390Vector *v1, S390Vector *v2,
    const uint8_t m3,  const uint8_t m4,  const uint8_t m5)
{
    asm volatile("vcfps %[v1], %[v2], %[m3], %[m4], %[m5]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [m3]  "i" (m3)
                , [m4]  "i" (m4)
                , [m5]  "i" (m5));
}

static inline void vcfpl(S390Vector *v1, S390Vector *v2,
    const uint8_t m3,  const uint8_t m4,  const uint8_t m5)
{
    asm volatile("vcfpl %[v1], %[v2], %[m3], %[m4], %[m5]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [m3]  "i" (m3)
                , [m4]  "i" (m4)
                , [m5]  "i" (m5));
}

static inline void vcsfp(S390Vector *v1, S390Vector *v2,
    const uint8_t m3,  const uint8_t m4,  const uint8_t m5)
{
    asm volatile("vcsfp %[v1], %[v2], %[m3], %[m4], %[m5]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [m3]  "i" (m3)
                , [m4]  "i" (m4)
                , [m5]  "i" (m5));
}

static inline void vclfp(S390Vector *v1, S390Vector *v2,
    const uint8_t m3,  const uint8_t m4,  const uint8_t m5)
{
    asm volatile("vclfp %[v1], %[v2], %[m3], %[m4], %[m5]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [m3]  "i" (m3)
                , [m4]  "i" (m4)
                , [m5]  "i" (m5));
}

int main(int argc, char *argv[])
{
    S390Vector vd;
    S390Vector vs_i32 = { .w[0] = 1, .w[1] = 64, .w[2] = 1024, .w[3] = -10 };
    S390Vector vs_u32 = { .w[0] = 2, .w[1] = 32, .w[2] = 4096, .w[3] = 8888 };
    S390Vector vs_f32 = { .f[0] = 3.987, .f[1] = 5.123,
                          .f[2] = 4.499, .f[3] = 0.512 };

    vd.d[0] = vd.d[1] = 0;
    vcfps(&vd, &vs_i32, 2, M4_def, 0);
    if (1 != vd.f[0] || 1024 != vd.f[2] || 64 != vd.f[1] || -10 != vd.f[3]) {
        return 1;
    }

    vd.d[0] = vd.d[1] = 0;
    vcfpl(&vd, &vs_u32, 2, M4_def, 0);
    if (2 != vd.f[0] || 4096 != vd.f[2] || 32 != vd.f[1] || 8888 != vd.f[3]) {
        return 1;
    }

    vd.d[0] = vd.d[1] = 0;
    vcsfp(&vd, &vs_f32, 2, M4_def, 0);
    if (4 != vd.w[0] || 4 != vd.w[2] || 5 != vd.w[1] || 1 != vd.w[3]) {
        return 1;
    }

    vd.d[0] = vd.d[1] = 0;
    vclfp(&vd, &vs_f32, 2, M4_def, 0);
    if (4 != vd.w[0] || 4 != vd.w[2] || 5 != vd.w[1] || 1 != vd.w[3]) {
        return 1;
    }

    return 0;
}
