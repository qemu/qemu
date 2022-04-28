/*
 * vxeh2_vlstr: vector-enhancements facility 2 vector load/store reversed *
 */
#include <stdint.h>
#include "vx.h"

#define vtst(v1, v2) \
    if (v1.d[0] != v2.d[0] || v1.d[1] != v2.d[1]) { \
        return 1;     \
    }

static inline void vler(S390Vector *v1, const void *va, uint8_t m3)
{
    asm volatile("vler %[v1], 0(%[va]), %[m3]\n"
                : [v1] "+v" (v1->v)
                : [va]  "a" (va)
                , [m3]  "i" (m3)
                : "memory");
}

static inline void vster(S390Vector *v1, const void *va, uint8_t m3)
{
    asm volatile("vster %[v1], 0(%[va]), %[m3]\n"
                : [va] "+a" (va)
                : [v1]  "v" (v1->v)
                , [m3]  "i" (m3)
                : "memory");
}

static inline void vlbr(S390Vector *v1, void *va, const uint8_t m3)
{
    asm volatile("vlbr %[v1], 0(%[va]), %[m3]\n"
                : [v1] "+v" (v1->v)
                : [va]  "a" (va)
                , [m3]  "i" (m3)
                : "memory");
}

static inline void vstbr(S390Vector *v1, void *va, const uint8_t m3)
{
    asm volatile("vstbr %[v1], 0(%[va]), %[m3]\n"
                : [va] "+a" (va)
                : [v1]  "v" (v1->v)
                , [m3]  "i" (m3)
                : "memory");
}


static inline void vlebrh(S390Vector *v1, void *va, const uint8_t m3)
{
    asm volatile("vlebrh %[v1], 0(%[va]), %[m3]\n"
                : [v1] "+v" (v1->v)
                : [va]  "a" (va)
                , [m3]  "i" (m3)
                : "memory");
}

static inline void vstebrh(S390Vector *v1, void *va, const uint8_t m3)
{
    asm volatile("vstebrh %[v1], 0(%[va]), %[m3]\n"
                : [va] "+a" (va)
                : [v1]  "v" (v1->v)
                , [m3]  "i" (m3)
                : "memory");
}

static inline void vllebrz(S390Vector *v1, void *va, const uint8_t m3)
{
    asm volatile("vllebrz %[v1], 0(%[va]), %[m3]\n"
                : [v1] "+v" (v1->v)
                : [va]  "a" (va)
                , [m3]  "i" (m3)
                : "memory");
}

static inline void vlbrrep(S390Vector *v1, void *va, const uint8_t m3)
{
    asm volatile("vlbrrep %[v1], 0(%[va]), %[m3]\n"
                : [v1] "+v" (v1->v)
                : [va]  "a" (va)
                , [m3]  "i" (m3)
                : "memory");
}

int main(int argc, char *argv[])
{
    S390Vector vd = { .d[0] = 0, .d[1] = 0 };
    S390Vector vs = { .d[0] = 0x8FEEDDCCBBAA9988ull,
                      .d[1] = 0x7766554433221107ull };

    const S390Vector vt_v_er16 = {
        .h[0] = 0x1107, .h[1] = 0x3322, .h[2] = 0x5544, .h[3] = 0x7766,
        .h[4] = 0x9988, .h[5] = 0xBBAA, .h[6] = 0xDDCC, .h[7] = 0x8FEE };

    const S390Vector vt_v_br16 = {
        .h[0] = 0xEE8F, .h[1] = 0xCCDD, .h[2] = 0xAABB, .h[3] = 0x8899,
        .h[4] = 0x6677, .h[5] = 0x4455, .h[6] = 0x2233, .h[7] = 0x0711 };

    int ix;
    uint64_t ss64 = 0xFEEDFACE0BADBEEFull, sd64 = 0;

    vler(&vd, &vs, ES16);
    vtst(vd, vt_v_er16);

    vster(&vs, &vd, ES16);
    vtst(vd, vt_v_er16);

    vlbr(&vd, &vs, ES16);
    vtst(vd, vt_v_br16);

    vstbr(&vs, &vd, ES16);
    vtst(vd, vt_v_br16);

    vlebrh(&vd, &ss64, 5);
    if (0xEDFE != vd.h[5]) {
        return 1;
    }

    vstebrh(&vs, (uint8_t *)&sd64 + 4, 7);
    if (0x0000000007110000ull != sd64) {
        return 1;
    }

    vllebrz(&vd, (uint8_t *)&ss64 + 3, 2);
    for (ix = 0; ix < 4; ix++) {
        if (vd.w[ix] != (ix != 1 ? 0 : 0xBEAD0BCE)) {
            return 1;
        }
    }

    vlbrrep(&vd, (uint8_t *)&ss64 + 4, 1);
    for (ix = 0; ix < 8; ix++) {
        if (0xAD0B != vd.h[ix]) {
            return 1;
        }
    }

    return 0;
}
