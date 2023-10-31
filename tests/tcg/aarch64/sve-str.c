#include <stdio.h>
#include <sys/prctl.h>

#define N  (256 + 16)

static int __attribute__((noinline)) test(int vl)
{
    unsigned char buf[N];
    int err = 0;

    for (int i = 0; i < N; ++i) {
        buf[i] = (unsigned char)i;
    }

    asm volatile (
        "mov z0.b, #255\n\t"
        "str z0, %0"
        : : "m" (buf) : "z0", "memory");

    for (int i = 0; i < vl; ++i) {
        if (buf[i] != 0xff) {
            fprintf(stderr, "vl %d, index %d, expected 255, got %d\n",
                    vl, i, buf[i]);
            err = 1;
        }
    }

    for (int i = vl; i < N; ++i) {
        if (buf[i] != (unsigned char)i) {
            fprintf(stderr, "vl %d, index %d, expected %d, got %d\n",
                    vl, i, (unsigned char)i, buf[i]);
            err = 1;
        }
    }

    return err;
}

int main()
{
    int err = 0;

    for (int i = 16; i <= 256; i += 16) {
        if (prctl(PR_SVE_SET_VL, i, 0, 0, 0, 0) == i) {
            err |= test(i);
        }
    }
    return err;
}
