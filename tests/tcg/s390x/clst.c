#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>

static int clst(char sep, const char **s1, const char **s2)
{
    const char *r1 = *s1;
    const char *r2 = *s2;
    int cc;

    do {
        register int r0 asm("r0") = sep;

        asm("clst %[r1],%[r2]\n"
            "ipm %[cc]\n"
            "srl %[cc],28"
            : [r1] "+r" (r1), [r2] "+r" (r2), "+r" (r0), [cc] "=r" (cc)
            :
            : "cc");
        *s1 = r1;
        *s2 = r2;
    } while (cc == 3);

    return cc;
}

static const struct test {
    const char *name;
    char sep;
    const char *s1;
    const char *s2;
    int exp_cc;
    int exp_off;
} tests[] = {
    {
        .name = "cc0",
        .sep = 0,
        .s1 = "aa",
        .s2 = "aa",
        .exp_cc = 0,
        .exp_off = 0,
    },
    {
        .name = "cc1",
        .sep = 1,
        .s1 = "a\x01",
        .s2 = "aa\x01",
        .exp_cc = 1,
        .exp_off = 1,
    },
    {
        .name = "cc2",
        .sep = 2,
        .s1 = "abc\x02",
        .s2 = "abb\x02",
        .exp_cc = 2,
        .exp_off = 2,
    },
};

int main(void)
{
    const struct test *t;
    const char *s1, *s2;
    size_t i;
    int cc;

    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        t = &tests[i];
        s1 = t->s1;
        s2 = t->s2;
        cc = clst(t->sep, &s1, &s2);
        if (cc != t->exp_cc ||
                s1 != t->s1 + t->exp_off ||
                s2 != t->s2 + t->exp_off) {
            fprintf(stderr, "%s\n", t->name);
            return EXIT_FAILURE;
        }
    }

    return EXIT_SUCCESS;
}
