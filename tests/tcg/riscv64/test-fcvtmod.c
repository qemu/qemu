#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

#define FFLAG_NX_SHIFT 0 /* inexact */
#define FFLAG_UF_SHIFT 1 /* underflow */
#define FFLAG_OF_SHIFT 2 /* overflow */
#define FFLAG_DZ_SHIFT 3 /* divide by zero */
#define FFLAG_NV_SHIFT 4 /* invalid operation */

#define FFLAG_NV (1UL << FFLAG_NV_SHIFT)
#define FFLAG_DZ (1UL << FFLAG_DZ_SHIFT)
#define FFLAG_OF (1UL << FFLAG_OF_SHIFT)
#define FFLAG_UF (1UL << FFLAG_UF_SHIFT)
#define FFLAG_NX (1UL << FFLAG_NX_SHIFT)

typedef struct fp64_fcvt_fcvtmod_testcase {
    const char* name;
    union {
        uint64_t inp_lu;
        double inp_lf;
    };
    uint64_t exp_fcvt;
    uint8_t exp_fcvt_fflags;
    uint64_t exp_fcvtmod;
    uint8_t exp_fcvtmod_fflags;
} fp64_fcvt_fcvtmod_testcase_t;

void print_fflags(uint8_t fflags)
{
    int set = 0;

    if (fflags == 0) {
        printf("-");
        return;
    }

    if (fflags & FFLAG_NV) {
        printf("%sFFLAG_NV", set ? " | " : "");
        set = 1;
    }
    if (fflags & FFLAG_DZ) {
        printf("%sFFLAG_DZ", set ? " | " : "");
        set = 1;
    }
    if (fflags & FFLAG_OF) {
        printf("%sFFLAG_OF", set ? " | " : "");
        set = 1;
    }
    if (fflags & FFLAG_UF) {
        printf("%sFFLAG_UF", set ? " | " : "");
        set = 1;
    }
    if (fflags & FFLAG_NX) {
        printf("%sFFLAG_NX", set ? " | " : "");
        set = 1;
    }
}

/* Clear all FP flags. */
static inline void clear_fflags()
{
    __asm__ __volatile__("fsflags zero");
}

/* Read all FP flags. */
static inline uint8_t get_fflags()
{
    uint64_t v;
    __asm__ __volatile__("frflags %0" : "=r"(v));
    return (uint8_t)v;
}

/* Move input value (without conversations) into an FP register. */
static inline double do_fmv_d_x(uint64_t inp)
{
    double fpr;
    __asm__ __volatile__("fmv.d.x %0, %1" : "=f"(fpr) : "r"(inp));
    return fpr;
}

static inline uint64_t do_fcvt_w_d(uint64_t inp, uint8_t *fflags)
{
    uint64_t ret;
    double fpr = do_fmv_d_x(inp);

    clear_fflags();

    __asm__ __volatile__("fcvt.w.d %0, %1, rtz" : "=r"(ret) : "f"(fpr));

    *fflags = get_fflags();

    return ret;
}

static inline uint64_t do_fcvtmod_w_d(uint64_t inp, uint8_t *fflags)
{
    uint64_t ret;
    double fpr = do_fmv_d_x(inp);

    clear_fflags();

    /* fcvtmod.w.d rd, rs1, rtz = 1100001 01000 rs1 001 rd 1010011 */
    asm(".insn r  0x53, 0x1, 0x61, %0, %1, f8" : "=r"(ret) : "f"(fpr));

    *fflags = get_fflags();

    return ret;
}

static const fp64_fcvt_fcvtmod_testcase_t tests[] = {
    /* Zero (exp=0, frac=0) */
    { .name = "+0.0",
      .inp_lf = 0x0p0,
      .exp_fcvt = 0x0000000000000000,
      .exp_fcvt_fflags = 0,
      .exp_fcvtmod = 0x0000000000000000,
      .exp_fcvtmod_fflags = 0 },
    { .name = "-0.0",
      .inp_lf = -0x0p0,
      .exp_fcvt = 0x0000000000000000,
      .exp_fcvt_fflags = 0,
      .exp_fcvtmod = 0x0000000000000000,
      .exp_fcvtmod_fflags = 0 },

    /* Subnormal: exp=0 frac!=0 */
    { .name = "Subnormal frac=1",
      .inp_lu = 0x0000000000000001,
      .exp_fcvt = 0x0000000000000000,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "Subnormal frac=0xf..f",
      .inp_lu = 0x0000ffffffffffff,
      .exp_fcvt = 0x0000000000000000,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "Neg subnormal frac=1",
      .inp_lu = 0x0000000000000001,
      .exp_fcvt = 0x0000000000000000,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "Neg subnormal frac=0xf..f",
      .inp_lu = 0x8000ffffffffffff,
      .exp_fcvt = 0x0000000000000000,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },

    /* Infinity: exp=0x7ff, frac=0 */
    { .name = "+INF",
      .inp_lu = 0x7ff0000000000000,
      .exp_fcvt = 0x000000007fffffff, /* int32 max */
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NV },
    { .name = "-INF",
      .inp_lu = 0xfff0000000000000,
      .exp_fcvt = 0xffffffff80000000, /* int32 min */
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NV },

    /* NaN: exp=7ff, frac!=0 */
    { .name = "canonical NaN",
      .inp_lu = 0x7ff8000000000000,
      .exp_fcvt = 0x000000007fffffff, /* int32 max */
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NV },
    { .name = "non-canonical NaN",
      .inp_lu = 0x7ff8000000100000,
      .exp_fcvt = 0x000000007fffffff, /* int32 min */
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NV },

    /* Normal numbers: exp!=0, exp!=7ff */
    { .name = "+smallest normal value",
      .inp_lu = 0x0010000000000000,
      .exp_fcvt = 0,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "-smallest normal value",
      .inp_lu = 0x8010000000000000,
      .exp_fcvt = 0,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },

    { .name = "+0.5",
      .inp_lf = 0x1p-1,
      .exp_fcvt = 0,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "-0.5",
      .inp_lf = -0x1p-1,
      .exp_fcvt = 0,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },

    { .name = "+value just below 1.0",
      .inp_lu = 0x3fefffffffffffff,
      .exp_fcvt = 0,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "-value just above -1.0",
      .inp_lu = 0xbfefffffffffffff,
      .exp_fcvt = 0,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0,
      .exp_fcvtmod_fflags = FFLAG_NX },

    { .name = "+1.0",
      .inp_lf = 0x1p0,
      .exp_fcvt = 0x0000000000000001,
      .exp_fcvt_fflags = 0,
      .exp_fcvtmod = 0x0000000000000001,
      .exp_fcvtmod_fflags = 0 },
    { .name = "-1.0",
      .inp_lf = -0x1p0,
      .exp_fcvt = 0xffffffffffffffff,
      .exp_fcvt_fflags = 0,
      .exp_fcvtmod = 0xffffffffffffffff,
      .exp_fcvtmod_fflags = 0 },

    { .name = "+1.5",
      .inp_lu = 0x3ff8000000000000,
      .exp_fcvt = 1,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 1,
      .exp_fcvtmod_fflags = FFLAG_NX },
    { .name = "-1.5",
      .inp_lu = 0xbff8000000000000,
      .exp_fcvt = 0xffffffffffffffff,
      .exp_fcvt_fflags = FFLAG_NX,
      .exp_fcvtmod = 0xffffffffffffffff,
      .exp_fcvtmod_fflags = FFLAG_NX },

    { .name = "+max int32 (2147483647)",
      .inp_lu = 0x41dfffffffc00000,
      .exp_fcvt = 0x000000007fffffff,
      .exp_fcvt_fflags = 0,
      .exp_fcvtmod = 0x000000007fffffff,
      .exp_fcvtmod_fflags = 0 },
    { .name = "+max int32 +1 (2147483648)",
      .inp_lf = 0x1p31,
      .exp_fcvt = 0x000000007fffffff,
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = (uint64_t)-2147483648l, /* int32 min */
      .exp_fcvtmod_fflags = FFLAG_NV },
    { .name = "+max int32 +2 (2147483649)",
      .inp_lu = 0x41e0000000200000,
      .exp_fcvt = 0x000000007fffffff,
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = (uint64_t)-2147483647l, /* int32 min +1 */
      .exp_fcvtmod_fflags = FFLAG_NV },

    { .name = "-max int32 (-2147483648)",
      .inp_lf = -0x1p31,
      .exp_fcvt = 0xffffffff80000000,
      .exp_fcvt_fflags = 0,
      .exp_fcvtmod = 0xffffffff80000000,
      .exp_fcvtmod_fflags = 0 },
    { .name = "-max int32 -1 (-2147483649)",
      .inp_lf = -0x1.00000002p+31,
      .exp_fcvt = 0xffffffff80000000,
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = 2147483647, /* int32 max */
      .exp_fcvtmod_fflags = FFLAG_NV },
    { .name = "-max int32 -2 (-2147483650)",
      .inp_lf = -0x1.00000004p+31,
      .exp_fcvt = 0xffffffff80000000,
      .exp_fcvt_fflags = FFLAG_NV,
      .exp_fcvtmod = 2147483646, /* int32 max -1 */
      .exp_fcvtmod_fflags = FFLAG_NV },
};

int run_fcvtmod_tests()
{
    uint64_t act_fcvt;
    uint8_t act_fcvt_fflags;
    uint64_t act_fcvtmod;
    uint8_t act_fcvtmod_fflags;

    for (size_t i = 0; i < sizeof(tests)/sizeof(tests[0]); i++) {
        const fp64_fcvt_fcvtmod_testcase_t *t = &tests[i];

        act_fcvt = do_fcvt_w_d(t->inp_lu, &act_fcvt_fflags);
        int fcvt_correct = act_fcvt == t->exp_fcvt &&
                    act_fcvt_fflags == t->exp_fcvt_fflags;
        act_fcvtmod = do_fcvtmod_w_d(t->inp_lu, &act_fcvtmod_fflags);
        int fcvtmod_correct = act_fcvtmod == t->exp_fcvtmod &&
                       act_fcvtmod_fflags == t->exp_fcvtmod_fflags;

        if (fcvt_correct && fcvtmod_correct) {
            continue;
        }

        printf("Test %zu (%s) failed!\n", i, t->name);

        double fpr = do_fmv_d_x(t->inp_lu);
        printf("inp_lu: 0x%016lx == %lf\n", t->inp_lu, fpr);
        printf("inp_lf: %lf\n", t->inp_lf);

        uint32_t sign = (t->inp_lu >> 63);
        uint32_t exp = (uint32_t)(t->inp_lu >> 52) & 0x7ff;
        uint64_t frac = t->inp_lu & 0xfffffffffffffull; /* significand */
        int true_exp = exp - 1023;
        int shift = true_exp - 52;
        uint64_t true_frac = frac | 1ull << 52;

        printf("sign=%d, exp=0x%03x, frac=0x%012lx\n", sign, exp, frac);
        printf("true_exp=%d, shift=%d, true_frac=0x%016lx\n", true_exp, shift, true_frac);

        if (!fcvt_correct) {
            printf("act_fcvt: 0x%016lx == %li\n", act_fcvt, act_fcvt);
            printf("exp_fcvt: 0x%016lx == %li\n", t->exp_fcvt, t->exp_fcvt);
            printf("act_fcvt_fflags: "); print_fflags(act_fcvt_fflags); printf("\n");
            printf("exp_fcvt_fflags: "); print_fflags(t->exp_fcvt_fflags); printf("\n");
        }

        if (!fcvtmod_correct) {
            printf("act_fcvtmod: 0x%016lx == %li\n", act_fcvtmod, act_fcvtmod);
            printf("exp_fcvtmod: 0x%016lx == %li\n", t->exp_fcvtmod, t->exp_fcvtmod);
            printf("act_fcvtmod_fflags: "); print_fflags(act_fcvtmod_fflags); printf("\n");
            printf("exp_fcvtmod_fflags: "); print_fflags(t->exp_fcvtmod_fflags); printf("\n");
        }

        return 1;
    }

    return 0;
}

int main()
{
    return run_fcvtmod_tests();
}
