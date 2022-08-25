/* See if various BMI2 instructions give expected results */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#define insn1q(name, arg0)                                                           \
static inline uint64_t name##q(uint64_t arg0)                                        \
{                                                                                    \
    uint64_t result64;                                                               \
    asm volatile (#name "q   %1, %0" : "=r"(result64) : "rm"(arg0));                 \
    return result64;                                                                 \
}

#define insn1l(name, arg0)                                                           \
static inline uint32_t name##l(uint32_t arg0)                                        \
{                                                                                    \
    uint32_t result32;                                                               \
    asm volatile (#name "l   %k1, %k0" : "=r"(result32) : "rm"(arg0));               \
    return result32;                                                                 \
}

#define insn2q(name, arg0, c0, arg1, c1)                                             \
static inline uint64_t name##q(uint64_t arg0, uint64_t arg1)                         \
{                                                                                    \
    uint64_t result64;                                                               \
    asm volatile (#name "q   %2, %1, %0" : "=r"(result64) : c0(arg0), c1(arg1));     \
    return result64;                                                                 \
}

#define insn2l(name, arg0, c0, arg1, c1)                                             \
static inline uint32_t name##l(uint32_t arg0, uint32_t arg1)                         \
{                                                                                    \
    uint32_t result32;                                                               \
    asm volatile (#name "l   %k2, %k1, %k0" : "=r"(result32) : c0(arg0), c1(arg1));  \
    return result32;                                                                 \
}

#ifdef __x86_64
insn2q(pext, src, "r", mask, "rm")
insn2q(pdep, src, "r", mask, "rm")
insn2q(andn, clear, "rm", val, "r")
insn2q(bextr, range, "rm", val, "r")
insn2q(bzhi, pos, "rm", val, "r")
insn2q(rorx, val, "r", n, "i")
insn2q(sarx, val, "rm", n, "r")
insn2q(shlx, val, "rm", n, "r")
insn2q(shrx, val, "rm", n, "r")
insn1q(blsi, src)
insn1q(blsmsk, src)
insn1q(blsr, src)
#endif
insn2l(pext, src, "r", mask, "rm")
insn2l(pdep, src, "r", mask, "rm")
insn2l(andn, clear, "rm", val, "r")
insn2l(bextr, range, "rm", val, "r")
insn2l(bzhi, pos, "rm", val, "r")
insn2l(rorx, val, "r", n, "i")
insn2l(sarx, val, "rm", n, "r")
insn2l(shlx, val, "rm", n, "r")
insn2l(shrx, val, "rm", n, "r")
insn1l(blsi, src)
insn1l(blsmsk, src)
insn1l(blsr, src)

int main(int argc, char *argv[]) {
    uint64_t ehlo = 0x202020204f4c4845ull;
    uint64_t mask = 0xa080800302020001ull;
    uint32_t result32;

#ifdef __x86_64
    uint64_t result64;

    /* 64 bits */
    result64 = andnq(mask, ehlo);
    assert(result64 == 0x002020204d4c4844);

    result64 = pextq(ehlo, mask);
    assert(result64 == 133);

    result64 = pdepq(result64, mask);
    assert(result64 == (ehlo & mask));

    result64 = pextq(-1ull, mask);
    assert(result64 == 511); /* mask has 9 bits set */

    result64 = pdepq(-1ull, mask);
    assert(result64 == mask);

    result64 = bextrq(mask, 0x3f00);
    assert(result64 == (mask & ~INT64_MIN));

    result64 = bextrq(mask, 0x1038);
    assert(result64 == 0xa0);

    result64 = bextrq(mask, 0x10f8);
    assert(result64 == 0);

    result64 = blsiq(0x30);
    assert(result64 == 0x10);

    result64 = blsiq(0x30ull << 32);
    assert(result64 == 0x10ull << 32);

    result64 = blsmskq(0x30);
    assert(result64 == 0x1f);

    result64 = blsrq(0x30);
    assert(result64 == 0x20);

    result64 = blsrq(0x30ull << 32);
    assert(result64 == 0x20ull << 32);

    result64 = bzhiq(mask, 0x3f);
    assert(result64 == (mask & ~INT64_MIN));

    result64 = bzhiq(mask, 0x1f);
    assert(result64 == (mask & ~(-1 << 30)));

    result64 = rorxq(0x2132435465768798, 8);
    assert(result64 == 0x9821324354657687);

    result64 = sarxq(0xffeeddccbbaa9988, 8);
    assert(result64 == 0xffffeeddccbbaa99);

    result64 = sarxq(0x77eeddccbbaa9988, 8 | 64);
    assert(result64 == 0x0077eeddccbbaa99);

    result64 = shrxq(0xffeeddccbbaa9988, 8);
    assert(result64 == 0x00ffeeddccbbaa99);

    result64 = shrxq(0x77eeddccbbaa9988, 8 | 192);
    assert(result64 == 0x0077eeddccbbaa99);

    result64 = shlxq(0xffeeddccbbaa9988, 8);
    assert(result64 == 0xeeddccbbaa998800);
#endif

    /* 32 bits */
    result32 = andnl(mask, ehlo);
    assert(result32 == 0x04d4c4844);

    result32 = pextl((uint32_t) ehlo, mask);
    assert(result32 == 5);

    result32 = pdepl(result32, mask);
    assert(result32 == (uint32_t)(ehlo & mask));

    result32 = pextl(-1u, mask);
    assert(result32 == 7); /* mask has 3 bits set */

    result32 = pdepl(-1u, mask);
    assert(result32 == (uint32_t)mask);

    result32 = bextrl(mask, 0x1f00);
    assert(result32 == (mask & ~INT32_MIN));

    result32 = bextrl(ehlo, 0x1018);
    assert(result32 == 0x4f);

    result32 = bextrl(mask, 0x1038);
    assert(result32 == 0);

    result32 = blsil(0xffff);
    assert(result32 == 1);

    result32 = blsmskl(0x300);
    assert(result32 == 0x1ff);

    result32 = blsrl(0xffc);
    assert(result32 == 0xff8);

    result32 = bzhil(mask, 0xf);
    assert(result32 == 1);

    result32 = rorxl(0x65768798, 8);
    assert(result32 == 0x98657687);

    result32 = sarxl(0xffeeddcc, 8);
    assert(result32 == 0xffffeedd);

    result32 = sarxl(0x77eeddcc, 8 | 32);
    assert(result32 == 0x0077eedd);

    result32 = shrxl(0xffeeddcc, 8);
    assert(result32 == 0x00ffeedd);

    result32 = shrxl(0x77eeddcc, 8 | 128);
    assert(result32 == 0x0077eedd);

    result32 = shlxl(0xffeeddcc, 8);
    assert(result32 == 0xeeddcc00);

    return 0;
}

