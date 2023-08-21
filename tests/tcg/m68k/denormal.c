/*
 * Test m68k extended double denormals.
 */

#include <stdio.h>
#include <stdint.h>

#define TEST(X, Y)  { X, Y, X * Y }

static volatile long double test[][3] = {
    TEST(0x1p+16383l, 0x1p-16446l),
    TEST(0x1.1p-8223l, 0x1.1p-8224l),
    TEST(1.0l, 0x1p-16383l),
};

#undef TEST

static void dump_ld(const char *label, long double ld)
{
    union {
        long double  d;
        struct {
            uint32_t exp:16;
            uint32_t space:16;
            uint32_t h;
            uint32_t l;
        };
    } u;

    u.d = ld;
    printf("%12s: % -27La 0x%04x 0x%08x 0x%08x\n", label, u.d, u.exp, u.h, u.l);
}

int main(void)
{
    int i, n = sizeof(test) / sizeof(test[0]), err = 0;

    for (i = 0; i < n; ++i) {
        long double x = test[i][0];
        long double y = test[i][1];
        long double build_mul = test[i][2];
        long double runtime_mul = x * y;

        if (runtime_mul != build_mul) {
            dump_ld("x", x);
            dump_ld("y", y);
            dump_ld("build_mul", build_mul);
            dump_ld("runtime_mul", runtime_mul);
            err = 1;
        }
    }
    return err;
}
