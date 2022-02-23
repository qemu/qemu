#include <stdint.h>

#define Fi3(S, ASM) uint64_t S(uint64_t a, uint64_t b, uint64_t c) \
{                            \
    uint64_t res = 0;        \
    asm (                    \
         "lg %%r2, %[a]\n"   \
         "lg %%r3, %[b]\n"   \
         "lg %%r0, %[c]\n"   \
         "ltgr %%r0, %%r0\n" \
         ASM                 \
         "stg %%r0, %[res] " \
         : [res] "=m" (res)  \
         : [a] "m" (a),      \
           [b] "m" (b),      \
           [c] "m" (c)       \
         : "r0", "r2",       \
           "r3", "r4"        \
    );                       \
    return res;              \
}

Fi3 (_selre,     ".insn rrf, 0xB9F00000, %%r0, %%r3, %%r2, 8\n")
Fi3 (_selgrz,    ".insn rrf, 0xB9E30000, %%r0, %%r3, %%r2, 8\n")
Fi3 (_selfhrnz,  ".insn rrf, 0xB9C00000, %%r0, %%r3, %%r2, 7\n")

int main(int argc, char *argv[])
{
    uint64_t a = ~0, b = ~0, c = ~0;
    a =    _selre(0x066600000066ull, 0x066600000006ull, a);
    b =   _selgrz(0xF00D00000005ull, 0xF00D00000055ull, b);
    c = _selfhrnz(0x043200000044ull, 0x065400000004ull, c);

    return (int) (
        (0xFFFFFFFF00000066ull != a) ||
        (0x0000F00D00000005ull != b) ||
        (0x00000654FFFFFFFFull != c));
}
