#include <stdint.h>
#include <string.h>


static inline void mvcrl_8(const char *dst, const char *src)
{
    asm volatile (
        "llill %%r0, 8\n"
        ".insn sse, 0xE50A00000000, 0(%[dst]), 0(%[src])"
        : : [dst] "d" (dst), [src] "d" (src)
        : "r0", "memory");
}


int main(int argc, char *argv[])
{
    const char *alpha = "abcdefghijklmnop";

    /* array missing 'i' */
    char tstr[17] = "abcdefghjklmnop\0" ;

    /* mvcrl reference use: 'open a hole in an array' */
    mvcrl_8(tstr + 9, tstr + 8);

    /* place missing 'i' */
    tstr[8] = 'i';

    return strncmp(alpha, tstr, 16ul);
}
