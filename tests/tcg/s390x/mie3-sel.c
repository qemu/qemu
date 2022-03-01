#include <stdint.h>


#define Fi3(S, ASM) uint64_t S(uint64_t a, uint64_t b, uint64_t c) \
{                       \
asm volatile (          \
    "ltgr %[c], %[c]\n" \
    ASM                 \
    : [c] "+r" (c)      \
    : [a]  "r" (a)      \
    , [b]  "r" (b)      \
);                      \
    return c;           \
}

Fi3 (_selre,     ".insn rrf, 0xB9F00000, %[c], %[b], %[a], 8\n")
Fi3 (_selgrz,    ".insn rrf, 0xB9E30000, %[c], %[b], %[a], 8\n")
Fi3 (_selfhrnz,  ".insn rrf, 0xB9C00000, %[c], %[b], %[a], 7\n")


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
