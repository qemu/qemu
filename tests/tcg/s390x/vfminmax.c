#define _GNU_SOURCE
#include <fenv.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "float.h"

/*
 * vfmin/vfmax instruction execution.
 */
#define VFMIN 0xEE
#define VFMAX 0xEF

extern char insn[6];
asm(".pushsection .rwx,\"awx\",@progbits\n"
    ".globl insn\n"
    /* e7 89 a0 00 2e ef */
    "insn: vfmaxsb %v24,%v25,%v26,0\n"
    ".popsection\n");

static void vfminmax(unsigned int op,
                     unsigned int m4, unsigned int m5, unsigned int m6,
                     void *v1, const void *v2, const void *v3)
{
    insn[3] = (m6 << 4) | m5;
    insn[4] = (m4 << 4) | 0x0e;
    insn[5] = op;

    asm("vl %%v25,%[v2]\n"
        "vl %%v26,%[v3]\n"
        "ex 0,%[insn]\n"
        "vst %%v24,%[v1]\n"
        : [v1] "=m" (*(char (*)[16])v1)
        : [v2] "m" (*(const char (*)[16])v2)
        , [v3] "m" (*(const char (*)[16])v3)
        , [insn] "m" (insn)
        : "v24", "v25", "v26");
}

/*
 * PoP tables as close to the original as possible.
 */
struct signed_test {
    int op;
    int m6;
    const char *m6_desc;
    const char *table[N_SIGNED_CLASSES][N_SIGNED_CLASSES];
} signed_tests[] = {
    {
        .op = VFMIN,
        .m6 = 0,
        .m6_desc = "IEEE MinNum",
        .table = {
             /*         -inf         -Fn          -0           +0           +Fn          +inf         QNaN         SNaN     */
            {/* -inf */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* -Fn  */ "T(b)",      "T(M(a,b))", "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* -0   */ "T(b)",      "T(b)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* +0   */ "T(b)",      "T(b)",      "T(b)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* +Fn  */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(M(a,b))", "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* +inf */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* QNaN */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* SNaN */ "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)"},
        },
    },
    {
        .op = VFMIN,
        .m6 = 1,
        .m6_desc = "JAVA Math.Min()",
        .table = {
             /*         -inf         -Fn          -0           +0           +Fn          +inf         QNaN         SNaN     */
            {/* -inf */ "T(b)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(b)",      "Xi: T(b*)"},
            {/* -Fn  */ "T(b)",      "T(M(a,b))", "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(b)",      "Xi: T(b*)"},
            {/* -0   */ "T(b)",      "T(b)",      "T(b)",      "T(a)",      "T(a)",      "T(a)",      "T(b)",      "Xi: T(b*)"},
            {/* +0   */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "T(a)",      "T(b)",      "Xi: T(b*)"},
            {/* +Fn  */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(M(a,b))", "T(a)",      "T(b)",      "Xi: T(b*)"},
            {/* +inf */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "Xi: T(b*)"},
            {/* QNaN */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* SNaN */ "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)"},
        },
    },
    {
        .op = VFMIN,
        .m6 = 2,
        .m6_desc = "C-style Min Macro",
        .table = {
             /*         -inf        -Fn          -0          +0          +Fn          +inf        QNaN        SNaN    */
            {/* -inf */ "T(b)",     "T(a)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(b)", "Xi: T(b)"},
            {/* -Fn  */ "T(b)",     "T(M(a,b))", "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(b)", "Xi: T(b)"},
            {/* -0   */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(a)",      "T(a)",     "Xi: T(b)", "Xi: T(b)"},
            {/* +0   */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(a)",      "T(a)",     "Xi: T(b)", "Xi: T(b)"},
            {/* +Fn  */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(M(a,b))", "T(a)",     "Xi: T(b)", "Xi: T(b)"},
            {/* +inf */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(a)",     "Xi: T(b)", "Xi: T(b)"},
            {/* QNaN */ "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)"},
            {/* SNaN */ "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)"},
        },
    },
    {
        .op = VFMIN,
        .m6 = 3,
        .m6_desc = "C++ algorithm.min()",
        .table = {
             /*         -inf        -Fn          -0          +0          +Fn          +inf        QNaN        SNaN    */
            {/* -inf */ "T(b)",     "T(a)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* -Fn  */ "T(b)",     "T(M(a,b))", "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* -0   */ "T(b)",     "T(b)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* +0   */ "T(b)",     "T(b)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* +Fn  */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(M(a,b))", "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* +inf */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* QNaN */ "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)"},
            {/* SNaN */ "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)"},
        },
    },
    {
        .op = VFMIN,
        .m6 = 4,
        .m6_desc = "fmin()",
        .table = {
             /*         -inf        -Fn          -0          +0          +Fn          +inf        QNaN        SNaN    */
            {/* -inf */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* -Fn  */ "T(b)",     "T(M(a,b))", "T(a)",     "T(a)",     "T(a)",      "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* -0   */ "T(b)",     "T(b)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* +0   */ "T(b)",     "T(b)",      "T(b)",     "T(a)",     "T(a)",      "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* +Fn  */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(M(a,b))", "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* +inf */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* QNaN */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* SNaN */ "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(a)", "Xi: T(a)"},
        },
    },

    {
        .op = VFMAX,
        .m6 = 0,
        .m6_desc = "IEEE MaxNum",
        .table = {
             /*         -inf         -Fn          -0           +0           +Fn          +inf         QNaN         SNaN     */
            {/* -inf */ "T(a)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* -Fn  */ "T(a)",      "T(M(a,b))", "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* -0   */ "T(a)",      "T(a)",      "T(a)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* +0   */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(b)",      "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* +Fn  */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(M(a,b))", "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* +inf */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* QNaN */ "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(a)",      "Xi: T(b*)"},
            {/* SNaN */ "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)"},
        },
    },
    {
        .op = VFMAX,
        .m6 = 1,
        .m6_desc = "JAVA Math.Max()",
        .table = {
             /*         -inf         -Fn          -0           +0           +Fn          +inf         QNaN         SNaN     */
            {/* -inf */ "T(a)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "Xi: T(b*)"},
            {/* -Fn  */ "T(a)",      "T(M(a,b))", "T(b)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "Xi: T(b*)"},
            {/* -0   */ "T(a)",      "T(a)",      "T(a)",      "T(b)",      "T(b)",      "T(b)",      "T(b)",      "Xi: T(b*)"},
            {/* +0   */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(b)",      "T(b)",      "T(b)",      "Xi: T(b*)"},
            {/* +Fn  */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(M(a,b))", "T(b)",      "T(b)",      "Xi: T(b*)"},
            {/* +inf */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(b)",      "Xi: T(b*)"},
            {/* QNaN */ "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "T(a)",      "Xi: T(b*)"},
            {/* SNaN */ "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)", "Xi: T(a*)"},
        },
    },
    {
        .op = VFMAX,
        .m6 = 2,
        .m6_desc = "C-style Max Macro",
        .table = {
             /*         -inf        -Fn          -0          +0          +Fn          +inf        QNaN        SNaN    */
            {/* -inf */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "Xi: T(b)", "Xi: T(b)"},
            {/* -Fn  */ "T(a)",     "T(M(a,b))", "T(b)",     "T(b)",     "T(b)",      "T(b)",     "Xi: T(b)", "Xi: T(b)"},
            {/* -0   */ "T(a)",     "T(a)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "Xi: T(b)", "Xi: T(b)"},
            {/* +0   */ "T(a)",     "T(a)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "Xi: T(b)", "Xi: T(b)"},
            {/* +Fn  */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(M(a,b))", "T(b)",     "Xi: T(b)", "Xi: T(b)"},
            {/* +inf */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(a)",      "T(b)",     "Xi: T(b)", "Xi: T(b)"},
            {/* QNaN */ "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)"},
            {/* SNaN */ "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)"},
        },
    },
    {
        .op = VFMAX,
        .m6 = 3,
        .m6_desc = "C++ algorithm.max()",
        .table = {
             /*         -inf        -Fn          -0          +0          +Fn          +inf        QNaN        SNaN    */
            {/* -inf */ "T(a)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "Xi: T(a)", "Xi: T(a)"},
            {/* -Fn  */ "T(a)",     "T(M(a,b))", "T(b)",     "T(b)",     "T(b)",      "T(b)",     "Xi: T(a)", "Xi: T(a)"},
            {/* -0   */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(b)",      "T(b)",     "Xi: T(a)", "Xi: T(a)"},
            {/* +0   */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(b)",      "T(b)",     "Xi: T(a)", "Xi: T(a)"},
            {/* +Fn  */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(M(a,b))", "T(b)",     "Xi: T(a)", "Xi: T(a)"},
            {/* +inf */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "Xi: T(a)", "Xi: T(a)"},
            {/* QNaN */ "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)"},
            {/* SNaN */ "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)",  "Xi: T(a)", "Xi: T(a)", "Xi: T(a)"},
        },
    },
    {
        .op = VFMAX,
        .m6 = 4,
        .m6_desc = "fmax()",
        .table = {
             /*         -inf        -Fn          -0          +0          +Fn          +inf        QNaN        SNaN    */
            {/* -inf */ "T(a)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* -Fn  */ "T(a)",     "T(M(a,b))", "T(b)",     "T(b)",     "T(b)",      "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* -0   */ "T(a)",     "T(a)",      "T(a)",     "T(b)",     "T(b)",      "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* +0   */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(b)",      "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* +Fn  */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(M(a,b))", "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* +inf */ "T(a)",     "T(a)",      "T(a)",     "T(a)",     "T(a)",      "T(a)",     "T(a)",     "Xi: T(a)"},
            {/* QNaN */ "T(b)",     "T(b)",      "T(b)",     "T(b)",     "T(b)",      "T(b)",     "T(a)",     "Xi: T(a)"},
            {/* SNaN */ "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(b)", "Xi: T(b)",  "Xi: T(b)", "Xi: T(a)", "Xi: T(a)"},
        },
    },
};

static int signed_test(struct signed_test *test, int m4, int m5,
                       const void *v1_exp, bool xi_exp,
                       const void *v2, const void *v3)
{
    size_t n = (m5 & 8) ? float_sizes[m4 - 2] : 16;
    char v1[16];
    bool xi;

    feclearexcept(FE_ALL_EXCEPT);
    vfminmax(test->op, m4, m5, test->m6, v1, v2, v3);
    xi = fetestexcept(FE_ALL_EXCEPT) == FE_INVALID;

    if (memcmp(v1, v1_exp, n) != 0 || xi != xi_exp) {
        fprintf(stderr, "[  FAILED  ] %s ", test->m6_desc);
        dump_v(stderr, v2, n);
        fprintf(stderr, ", ");
        dump_v(stderr, v3, n);
        fprintf(stderr, ", %d, %d, %d: actual=", m4, m5, test->m6);
        dump_v(stderr, v1, n);
        fprintf(stderr, "/%d, expected=", (int)xi);
        dump_v(stderr, v1_exp, n);
        fprintf(stderr, "/%d\n", (int)xi_exp);
        return 1;
    }

    return 0;
}

struct iter {
    int cls[2];
    int val[2];
};

static bool iter_next(struct iter *it, int fmt)
{
    int i;

    for (i = 1; i >= 0; i--) {
        if (++it->val[i] != signed_floats[fmt][it->cls[i]].n) {
            return true;
        }
        it->val[i] = 0;

        if (++it->cls[i] != N_SIGNED_CLASSES) {
            return true;
        }
        it->cls[i] = 0;
    }

    return false;
}

int main(void)
{
    int ret = 0;
    size_t i;

    for (i = 0; i < sizeof(signed_tests) / sizeof(signed_tests[0]); i++) {
        struct signed_test *test = &signed_tests[i];
        int fmt;

        for (fmt = 0; fmt < N_FORMATS; fmt++) {
            size_t float_size = float_sizes[fmt];
            int m4 = fmt + 2;
            int m5;

            for (m5 = 0; m5 <= 8; m5 += 8) {
                char v1_exp[16], v2[16], v3[16];
                bool xi_exp = false;
                struct iter it = {};
                int pos = 0;

                do {
                    const char *spec = test->table[it.cls[0]][it.cls[1]];

                    memcpy(&v2[pos],
                           signed_floats[fmt][it.cls[0]].v[it.val[0]],
                           float_size);
                    memcpy(&v3[pos],
                           signed_floats[fmt][it.cls[1]].v[it.val[1]],
                           float_size);
                    if (strcmp(spec, "T(a)") == 0 ||
                        strcmp(spec, "Xi: T(a)") == 0) {
                        memcpy(&v1_exp[pos], &v2[pos], float_size);
                    } else if (strcmp(spec, "T(b)") == 0 ||
                               strcmp(spec, "Xi: T(b)") == 0) {
                        memcpy(&v1_exp[pos], &v3[pos], float_size);
                    } else if (strcmp(spec, "Xi: T(a*)") == 0) {
                        memcpy(&v1_exp[pos], &v2[pos], float_size);
                        snan_to_qnan(&v1_exp[pos], fmt);
                    } else if (strcmp(spec, "Xi: T(b*)") == 0) {
                        memcpy(&v1_exp[pos], &v3[pos], float_size);
                        snan_to_qnan(&v1_exp[pos], fmt);
                    } else if (strcmp(spec, "T(M(a,b))") == 0) {
                        /*
                         * Comparing floats is risky, since the compiler might
                         * generate the same instruction that we are testing.
                         * Compare ints instead. This works, because we get
                         * here only for +-Fn, and the corresponding test
                         * values have identical exponents.
                         */
                        int v2_int = *(int *)&v2[pos];
                        int v3_int = *(int *)&v3[pos];

                        if ((v2_int < v3_int) ==
                            ((test->op == VFMIN) != (v2_int < 0))) {
                            memcpy(&v1_exp[pos], &v2[pos], float_size);
                        } else {
                            memcpy(&v1_exp[pos], &v3[pos], float_size);
                        }
                    } else {
                        fprintf(stderr, "Unexpected spec: %s\n", spec);
                        return 1;
                    }
                    xi_exp |= spec[0] == 'X';
                    pos += float_size;

                    if ((m5 & 8) || pos == 16) {
                        ret |= signed_test(test, m4, m5,
                                           v1_exp, xi_exp, v2, v3);
                        pos = 0;
                        xi_exp = false;
                    }
                } while (iter_next(&it, fmt));

                if (pos != 0) {
                    ret |= signed_test(test, m4, m5, v1_exp, xi_exp, v2, v3);
                }
            }
        }
    }

    return ret;
}
