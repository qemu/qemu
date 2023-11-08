/*
 * Test ADD LOGICAL WITH CARRY instructions.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <stdio.h>
#include <stdlib.h>

static const struct test {
    const char *name;
    unsigned long values[3];
    unsigned long exp_sum;
    int exp_cc;
} tests[] = {
    /*
     * Each test starts with CC 0 and executes two chained ADD LOGICAL WITH
     * CARRY instructions on three input values. The values must be compatible
     * with both 32- and 64-bit test functions.
     */

    /* NAME       VALUES       EXP_SUM EXP_CC */
    { "cc0->cc0", {0, 0, 0},   0,      0, },
    { "cc0->cc1", {0, 0, 42},  42,     1, },
    /* cc0->cc2 is not possible */
    /* cc0->cc3 is not possible */
    /* cc1->cc0 is not possible */
    { "cc1->cc1", {-3, 1, 1},  -1,     1, },
    { "cc1->cc2", {-3, 1, 2},  0,      2, },
    { "cc1->cc3", {-3, 1, -1}, -3,     3, },
    /* cc2->cc0 is not possible */
    { "cc2->cc1", {-1, 1, 1},  2,      1, },
    { "cc2->cc2", {-1, 1, -1}, 0,      2, },
    /* cc2->cc3 is not possible */
    /* cc3->cc0 is not possible */
    { "cc3->cc1", {-1, 2, 1},  3,      1, },
    { "cc3->cc2", {-1, 2, -2}, 0,      2, },
    { "cc3->cc3", {-1, 2, -1}, 1,      3, },
};

/* Test ALCR (register variant) followed by ALC (memory variant). */
static unsigned long test32rm(unsigned long a, unsigned long b,
                              unsigned long c, int *cc)
{
    unsigned int a32 = a, b32 = b, c32 = c;

    asm("xr %[cc],%[cc]\n"
        "alcr %[a],%[b]\n"
        "alc %[a],%[c]\n"
        "ipm %[cc]"
        : [a] "+&r" (a32), [cc] "+&r" (*cc)
        : [b] "r" (b32), [c] "T" (c32)
        : "cc");
    *cc >>= 28;

    return (int)a32;
}

/* Test ALC (memory variant) followed by ALCR (register variant). */
static unsigned long test32mr(unsigned long a, unsigned long b,
                              unsigned long c, int *cc)
{
    unsigned int a32 = a, b32 = b, c32 = c;

    asm("xr %[cc],%[cc]\n"
        "alc %[a],%[b]\n"
        "alcr %[c],%[a]\n"
        "ipm %[cc]"
        : [a] "+&r" (a32), [c] "+&r" (c32), [cc] "+&r" (*cc)
        : [b] "T" (b32)
        : "cc");
    *cc >>= 28;

    return (int)c32;
}

/* Test ALCGR (register variant) followed by ALCG (memory variant). */
static unsigned long test64rm(unsigned long a, unsigned long b,
                              unsigned long c, int *cc)
{
    asm("xr %[cc],%[cc]\n"
        "alcgr %[a],%[b]\n"
        "alcg %[a],%[c]\n"
        "ipm %[cc]"
        : [a] "+&r" (a), [cc] "+&r" (*cc)
        : [b] "r" (b), [c] "T" (c)
        : "cc");
    *cc >>= 28;
    return a;
}

/* Test ALCG (memory variant) followed by ALCGR (register variant). */
static unsigned long test64mr(unsigned long a, unsigned long b,
                              unsigned long c, int *cc)
{
    asm("xr %[cc],%[cc]\n"
        "alcg %[a],%[b]\n"
        "alcgr %[c],%[a]\n"
        "ipm %[cc]"
        : [a] "+&r" (a), [c] "+&r" (c), [cc] "+&r" (*cc)
        : [b] "T" (b)
        : "cc");
    *cc >>= 28;
    return c;
}

static const struct test_func {
    const char *name;
    unsigned long (*ptr)(unsigned long, unsigned long, unsigned long, int *);
} test_funcs[] = {
    { "test32rm", test32rm },
    { "test32mr", test32mr },
    { "test64rm", test64rm },
    { "test64mr", test64mr },
};

static const struct test_perm {
    const char *name;
    size_t a_idx, b_idx, c_idx;
} test_perms[] = {
    { "a, b, c", 0, 1, 2 },
    { "b, a, c", 1, 0, 2 },
};

int main(void)
{
    unsigned long a, b, c, sum;
    int result = EXIT_SUCCESS;
    const struct test_func *f;
    const struct test_perm *p;
    size_t i, j, k;
    const struct test *t;
    int cc;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        t = &tests[i];
        for (j = 0; j < sizeof(test_funcs) / sizeof(test_funcs[0]); j++) {
            f = &test_funcs[j];
            for (k = 0; k < sizeof(test_perms) / sizeof(test_perms[0]); k++) {
                p = &test_perms[k];
                a = t->values[p->a_idx];
                b = t->values[p->b_idx];
                c = t->values[p->c_idx];
                sum = f->ptr(a, b, c, &cc);
                if (sum != t->exp_sum || cc != t->exp_cc) {
                    fprintf(stderr,
                            "[  FAILED  ] %s %s(0x%lx, 0x%lx, 0x%lx) returned 0x%lx cc %d, expected 0x%lx cc %d\n",
                            t->name, f->name, a, b, c, sum, cc,
                            t->exp_sum, t->exp_cc);
                    result = EXIT_FAILURE;
                }
            }
        }
    }

    return result;
}
