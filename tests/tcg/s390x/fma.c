/*
 * Test floating-point multiply-and-add instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <fenv.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "float.h"

union val {
    float e;
    double d;
    long double x;
    char buf[16];
};

/*
 * PoP tables as close to the original as possible.
 */
static const char *table1[N_SIGNED_CLASSES][N_SIGNED_CLASSES] = {
     /*         -inf           -Fn          -0             +0             +Fn          +inf           QNaN         SNaN     */
    {/* -inf */ "P(+inf)",     "P(+inf)",   "Xi: T(dNaN)", "Xi: T(dNaN)", "P(-inf)",   "P(-inf)",     "P(b)",      "Xi: T(b*)"},
    {/* -Fn  */ "P(+inf)",     "P(a*b)",    "P(+0)",       "P(-0)",       "P(a*b)",    "P(-inf)",     "P(b)",      "Xi: T(b*)"},
    {/* -0   */ "Xi: T(dNaN)", "P(+0)",     "P(+0)",       "P(-0)",       "P(-0)",     "Xi: T(dNaN)", "P(b)",      "Xi: T(b*)"},
    {/* +0   */ "Xi: T(dNaN)", "P(-0)",     "P(-0)",       "P(+0)",       "P(+0)",     "Xi: T(dNaN)", "P(b)",      "Xi: T(b*)"},
    {/* +Fn  */ "P(-inf)",     "P(a*b)",    "P(-0)",       "P(+0)",       "P(a*b)",    "P(+inf)",     "P(b)",      "Xi: T(b*)"},
    {/* +inf */ "P(-inf)",     "P(-inf)",   "Xi: T(dNaN)", "Xi: T(dNaN)", "P(+inf)",   "P(+inf)",     "P(b)",      "Xi: T(b*)"},
    {/* QNaN */ "P(a)",        "P(a)",      "P(a)",        "P(a)",        "P(a)",      "P(a)",        "P(a)",      "Xi: T(b*)"},
    {/* SNaN */ "Xi: T(a*)",   "Xi: T(a*)", "Xi: T(a*)",   "Xi: T(a*)",   "Xi: T(a*)", "Xi: T(a*)",   "Xi: T(a*)", "Xi: T(a*)"},
};

static const char *table2[N_SIGNED_CLASSES][N_SIGNED_CLASSES] = {
     /*         -inf           -Fn        -0         +0         +Fn        +inf           QNaN    SNaN     */
    {/* -inf */ "T(-inf)",     "T(-inf)", "T(-inf)", "T(-inf)", "T(-inf)", "Xi: T(dNaN)", "T(c)", "Xi: T(c*)"},
    {/* -Fn  */ "T(-inf)",     "R(p+c)",  "R(p)",    "R(p)",    "R(p+c)",  "T(+inf)",     "T(c)", "Xi: T(c*)"},
    {/* -0   */ "T(-inf)",     "R(c)",    "T(-0)",   "Rezd",    "R(c)",    "T(+inf)",     "T(c)", "Xi: T(c*)"},
    {/* +0   */ "T(-inf)",     "R(c)",    "Rezd",    "T(+0)",   "R(c)",    "T(+inf)",     "T(c)", "Xi: T(c*)"},
    {/* +Fn  */ "T(-inf)",     "R(p+c)",  "R(p)",    "R(p)",    "R(p+c)",  "T(+inf)",     "T(c)", "Xi: T(c*)"},
    {/* +inf */ "Xi: T(dNaN)", "T(+inf)", "T(+inf)", "T(+inf)", "T(+inf)", "T(+inf)",     "T(c)", "Xi: T(c*)"},
    {/* QNaN */ "T(p)",        "T(p)",    "T(p)",    "T(p)",    "T(p)",    "T(p)",        "T(p)", "Xi: T(c*)"},
     /* SNaN: can't happen */
};

static void interpret_tables(union val *r, bool *xi, int fmt,
                             int cls_a, const union val *a,
                             int cls_b, const union val *b,
                             int cls_c, const union val *c)
{
    const char *spec1 = table1[cls_a][cls_b];
    const char *spec2;
    union val p;
    int cls_p;

    *xi = false;

    if (strcmp(spec1, "P(-inf)") == 0) {
        cls_p = CLASS_MINUS_INF;
    } else if (strcmp(spec1, "P(+inf)") == 0) {
        cls_p = CLASS_PLUS_INF;
    } else if (strcmp(spec1, "P(-0)") == 0) {
        cls_p = CLASS_MINUS_ZERO;
    } else if (strcmp(spec1, "P(+0)") == 0) {
        cls_p = CLASS_PLUS_ZERO;
    } else if (strcmp(spec1, "P(a)") == 0) {
        cls_p = cls_a;
        memcpy(&p, a, sizeof(p));
    } else if (strcmp(spec1, "P(b)") == 0) {
        cls_p = cls_b;
        memcpy(&p, b, sizeof(p));
    } else if (strcmp(spec1, "P(a*b)") == 0) {
        /*
         * In the general case splitting fma into multiplication and addition
         * doesn't work, but this is the case with our test inputs.
         */
        cls_p = cls_a == cls_b ? CLASS_PLUS_FN : CLASS_MINUS_FN;
        switch (fmt) {
        case 0:
            p.e = a->e * b->e;
            break;
        case 1:
            p.d = a->d * b->d;
            break;
        case 2:
            p.x = a->x * b->x;
            break;
        default:
            fprintf(stderr, "Unsupported fmt: %d\n", fmt);
            exit(1);
        }
    } else if (strcmp(spec1, "Xi: T(dNaN)") == 0) {
        memcpy(r, default_nans[fmt], sizeof(*r));
        *xi = true;
        return;
    } else if (strcmp(spec1, "Xi: T(a*)") == 0) {
        memcpy(r, a, sizeof(*r));
        snan_to_qnan(r->buf, fmt);
        *xi = true;
        return;
    } else if (strcmp(spec1, "Xi: T(b*)") == 0) {
        memcpy(r, b, sizeof(*r));
        snan_to_qnan(r->buf, fmt);
        *xi = true;
        return;
    } else {
        fprintf(stderr, "Unsupported spec1: %s\n", spec1);
        exit(1);
    }

    spec2 = table2[cls_p][cls_c];
    if (strcmp(spec2, "T(-inf)") == 0) {
        memcpy(r, signed_floats[fmt][CLASS_MINUS_INF].v[0], sizeof(*r));
    } else if (strcmp(spec2, "T(+inf)") == 0) {
        memcpy(r, signed_floats[fmt][CLASS_PLUS_INF].v[0], sizeof(*r));
    } else if (strcmp(spec2, "T(-0)") == 0) {
        memcpy(r, signed_floats[fmt][CLASS_MINUS_ZERO].v[0], sizeof(*r));
    } else if (strcmp(spec2, "T(+0)") == 0 || strcmp(spec2, "Rezd") == 0) {
        memcpy(r, signed_floats[fmt][CLASS_PLUS_ZERO].v[0], sizeof(*r));
    } else if (strcmp(spec2, "R(c)") == 0 || strcmp(spec2, "T(c)") == 0) {
        memcpy(r, c, sizeof(*r));
    } else if (strcmp(spec2, "R(p)") == 0 || strcmp(spec2, "T(p)") == 0) {
        memcpy(r, &p, sizeof(*r));
    } else if (strcmp(spec2, "R(p+c)") == 0 || strcmp(spec2, "T(p+c)") == 0) {
        switch (fmt) {
        case 0:
            r->e = p.e + c->e;
            break;
        case 1:
            r->d = p.d + c->d;
            break;
        case 2:
            r->x = p.x + c->x;
            break;
        default:
            fprintf(stderr, "Unsupported fmt: %d\n", fmt);
            exit(1);
        }
    } else if (strcmp(spec2, "Xi: T(dNaN)") == 0) {
        memcpy(r, default_nans[fmt], sizeof(*r));
        *xi = true;
    } else if (strcmp(spec2, "Xi: T(c*)") == 0) {
        memcpy(r, c, sizeof(*r));
        snan_to_qnan(r->buf, fmt);
        *xi = true;
    } else {
        fprintf(stderr, "Unsupported spec2: %s\n", spec2);
        exit(1);
    }
}

struct iter {
    int fmt;
    int cls[3];
    int val[3];
};

static bool iter_next(struct iter *it)
{
    int i;

    for (i = 2; i >= 0; i--) {
        if (++it->val[i] != signed_floats[it->fmt][it->cls[i]].n) {
            return true;
        }
        it->val[i] = 0;

        if (++it->cls[i] != N_SIGNED_CLASSES) {
            return true;
        }
        it->cls[i] = 0;
    }

    return ++it->fmt != N_FORMATS;
}

int main(void)
{
    int ret = EXIT_SUCCESS;
    struct iter it = {};

    do {
        size_t n = float_sizes[it.fmt];
        union val a, b, c, exp, res;
        bool xi_exp, xi;

        memcpy(&a, signed_floats[it.fmt][it.cls[0]].v[it.val[0]], sizeof(a));
        memcpy(&b, signed_floats[it.fmt][it.cls[1]].v[it.val[1]], sizeof(b));
        memcpy(&c, signed_floats[it.fmt][it.cls[2]].v[it.val[2]], sizeof(c));

        interpret_tables(&exp, &xi_exp, it.fmt,
                         it.cls[1], &b, it.cls[2], &c, it.cls[0], &a);

        memcpy(&res, &a, sizeof(res));
        feclearexcept(FE_ALL_EXCEPT);
        switch (it.fmt) {
        case 0:
            asm("maebr %[a],%[b],%[c]"
                : [a] "+f" (res.e) : [b] "f" (b.e), [c] "f" (c.e));
            break;
        case 1:
            asm("madbr %[a],%[b],%[c]"
                : [a] "+f" (res.d) : [b] "f" (b.d), [c] "f" (c.d));
            break;
        case 2:
            asm("wfmaxb %[a],%[c],%[b],%[a]"
                : [a] "+v" (res.x) : [b] "v" (b.x), [c] "v" (c.x));
            break;
        default:
            fprintf(stderr, "Unsupported fmt: %d\n", it.fmt);
            exit(1);
        }
        xi = fetestexcept(FE_ALL_EXCEPT) == FE_INVALID;

        if (memcmp(&res, &exp, n) != 0 || xi != xi_exp) {
            fprintf(stderr, "[  FAILED  ] ");
            dump_v(stderr, &b, n);
            fprintf(stderr, " * ");
            dump_v(stderr, &c, n);
            fprintf(stderr, " + ");
            dump_v(stderr, &a, n);
            fprintf(stderr, ": actual=");
            dump_v(stderr, &res, n);
            fprintf(stderr, "/%d, expected=", (int)xi);
            dump_v(stderr, &exp, n);
            fprintf(stderr, "/%d\n", (int)xi_exp);
            ret = EXIT_FAILURE;
        }
    } while (iter_next(&it));

    return ret;
}
