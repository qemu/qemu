#include <assert.h>
#include <limits.h>

struct TestS {
    long x, y, q, r;
};

static struct TestS test_s[] = {
    { 4, 2, 2, 0 },                 /* normal cases */
    { 9, 7, 1, 2 },
    { 0, 0, -1, 0 },                /* div by zero cases */
    { 9, 0, -1, 9 },
    { LONG_MIN, -1, LONG_MIN, 0 },  /* overflow case */
};

struct TestU {
    unsigned long x, y, q, r;
};

static struct TestU test_u[] = {
    { 4, 2, 2, 0 },                 /* normal cases */
    { 9, 7, 1, 2 },
    { 0, 0, ULONG_MAX, 0 },         /* div by zero cases */
    { 9, 0, ULONG_MAX, 9 },
};

#define ARRAY_SIZE(X)  (sizeof(X) / sizeof(*(X)))

int main (void)
{
    int i;

    for (i = 0; i < ARRAY_SIZE(test_s); i++) {
        long q, r;

        asm("div %0, %2, %3\n\t"
            "rem %1, %2, %3"
            : "=&r" (q), "=r" (r)
            : "r" (test_s[i].x), "r" (test_s[i].y));

        assert(q == test_s[i].q);
        assert(r == test_s[i].r);
    }

    for (i = 0; i < ARRAY_SIZE(test_u); i++) {
        unsigned long q, r;

        asm("divu %0, %2, %3\n\t"
            "remu %1, %2, %3"
            : "=&r" (q), "=r" (r)
            : "r" (test_u[i].x), "r" (test_u[i].y));

        assert(q == test_u[i].q);
        assert(r == test_u[i].r);
    }

    return 0;
}
