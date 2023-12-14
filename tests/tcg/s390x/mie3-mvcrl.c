#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static void mvcrl(const char *dst, const char *src, size_t len)
{
    register long r0 asm("r0") = len;

    asm volatile (
        ".insn sse, 0xE50A00000000, 0(%[dst]), 0(%[src])"
        : : [dst] "d" (dst), [src] "d" (src), "r" (r0)
        : "memory");
}

static bool test(void)
{
    const char *alpha = "abcdefghijklmnop";

    /* array missing 'i' */
    char tstr[17] = "abcdefghjklmnop\0";

    /* mvcrl reference use: 'open a hole in an array' */
    mvcrl(tstr + 9, tstr + 8, 8);

    /* place missing 'i' */
    tstr[8] = 'i';

    return strncmp(alpha, tstr, 16ul) == 0;
}

static bool test_bad_r0(void)
{
    char src[256] = { 0 };

    /*
     * PoP says: Bits 32-55 of general register 0 should contain zeros;
     * otherwise, the program may not operate compatibly in the future.
     *
     * Try it anyway in order to check whether this would crash QEMU itself.
     */
    mvcrl(src, src, (size_t)-1);

    return true;
}

int main(void)
{
    bool ok = true;

    ok &= test();
    ok &= test_bad_r0();

    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
