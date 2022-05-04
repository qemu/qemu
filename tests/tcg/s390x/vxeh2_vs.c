/*
 * vxeh2_vs: vector-enhancements facility 2 vector shift
 */
#include <stdint.h>
#include "vx.h"

#define vtst(v1, v2) \
    if (v1.d[0] != v2.d[0] || v1.d[1] != v2.d[1]) { \
        return 1;     \
    }

static inline void vsl(S390Vector *v1, S390Vector *v2, S390Vector *v3)
{
    asm volatile("vsl %[v1], %[v2], %[v3]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [v3]  "v" (v3->v));
}

static inline void vsra(S390Vector *v1, S390Vector *v2, S390Vector *v3)
{
    asm volatile("vsra %[v1], %[v2], %[v3]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [v3]  "v" (v3->v));
}

static inline void vsrl(S390Vector *v1, S390Vector *v2, S390Vector *v3)
{
    asm volatile("vsrl %[v1], %[v2], %[v3]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [v3]  "v" (v3->v));
}

static inline void vsld(S390Vector *v1, S390Vector *v2,
    S390Vector *v3, const uint8_t I)
{
    asm volatile("vsld %[v1], %[v2], %[v3], %[I]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [v3]  "v" (v3->v)
                , [I]   "i" (I & 7));
}

static inline void vsrd(S390Vector *v1, S390Vector *v2,
    S390Vector *v3, const uint8_t I)
{
    asm volatile("vsrd %[v1], %[v2], %[v3], %[I]\n"
                : [v1] "=v" (v1->v)
                : [v2]  "v" (v2->v)
                , [v3]  "v" (v3->v)
                , [I]   "i" (I & 7));
}

int main(int argc, char *argv[])
{
    const S390Vector vt_vsl  = { .d[0] = 0x7FEDBB32D5AA311Dull,
                                 .d[1] = 0xBB65AA10912220C0ull };
    const S390Vector vt_vsra = { .d[0] = 0xF1FE6E7399AA5466ull,
                                 .d[1] = 0x0E762A5188221044ull };
    const S390Vector vt_vsrl = { .d[0] = 0x11FE6E7399AA5466ull,
                                 .d[1] = 0x0E762A5188221044ull };
    const S390Vector vt_vsld = { .d[0] = 0x7F76EE65DD54CC43ull,
                                 .d[1] = 0xBB32AA2199108838ull };
    const S390Vector vt_vsrd = { .d[0] = 0x0E060802040E000Aull,
                                 .d[1] = 0x0C060802040E000Aull };
    S390Vector vs  = { .d[0] = 0x8FEEDDCCBBAA9988ull,
                       .d[1] = 0x7766554433221107ull };
    S390Vector  vd = { .d[0] = 0, .d[1] = 0 };
    S390Vector vsi = { .d[0] = 0, .d[1] = 0 };

    for (int ix = 0; ix < 16; ix++) {
        vsi.b[ix] = (1 + (5 ^ ~ix)) & 7;
    }

    vsl(&vd, &vs, &vsi);
    vtst(vd, vt_vsl);

    vsra(&vd, &vs, &vsi);
    vtst(vd, vt_vsra);

    vsrl(&vd, &vs, &vsi);
    vtst(vd, vt_vsrl);

    vsld(&vd, &vs, &vsi, 3);
    vtst(vd, vt_vsld);

    vsrd(&vd, &vs, &vsi, 15);
    vtst(vd, vt_vsrd);

    return 0;
}
