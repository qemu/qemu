/* See if various BMI2 instructions give expected results */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __x86_64
typedef uint64_t reg_t;
#else
typedef uint32_t reg_t;
#endif

#define insn1q(name, arg0)                                                           \
static inline reg_t name##q(reg_t arg0)                                              \
{                                                                                    \
    reg_t result64;                                                                  \
    asm volatile (#name "q   %1, %0" : "=r"(result64) : "rm"(arg0));                 \
    return result64;                                                                 \
}

#define insn1l(name, arg0)                                                           \
static inline reg_t name##l(reg_t arg0)                                              \
{                                                                                    \
    reg_t result32;                                                                  \
    asm volatile (#name "l   %k1, %k0" : "=r"(result32) : "rm"(arg0));               \
    return result32;                                                                 \
}

#define insn2q(name, arg0, c0, arg1, c1)                                             \
static inline reg_t name##q(reg_t arg0, reg_t arg1)                                  \
{                                                                                    \
    reg_t result64;                                                                  \
    asm volatile (#name "q   %2, %1, %0" : "=r"(result64) : c0(arg0), c1(arg1));     \
    return result64;                                                                 \
}

#define insn2l(name, arg0, c0, arg1, c1)                                             \
static inline reg_t name##l(reg_t arg0, reg_t arg1)                                  \
{                                                                                    \
    reg_t result32;                                                                  \
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
    reg_t result;

#ifdef __x86_64
    /* 64 bits */
    result = andnq(mask, ehlo);
    assert(result == 0x002020204d4c4844);

    result = pextq(ehlo, mask);
    assert(result == 133);

    result = pdepq(result, mask);
    assert(result == (ehlo & mask));

    result = pextq(-1ull, mask);
    assert(result == 511); /* mask has 9 bits set */

    result = pdepq(-1ull, mask);
    assert(result == mask);

    result = bextrq(mask, 0x3f00);
    assert(result == (mask & ~INT64_MIN));

    result = bextrq(mask, 0x1038);
    assert(result == 0xa0);

    result = bextrq(mask, 0x10f8);
    assert(result == 0);

    result = bextrq(0xfedcba9876543210ull, 0x7f00);
    assert(result == 0xfedcba9876543210ull);

    result = blsiq(0x30);
    assert(result == 0x10);

    result = blsiq(0x30ull << 32);
    assert(result == 0x10ull << 32);

    result = blsmskq(0x30);
    assert(result == 0x1f);

    result = blsrq(0x30);
    assert(result == 0x20);

    result = blsrq(0x30ull << 32);
    assert(result == 0x20ull << 32);

    result = bzhiq(mask, 0x3f);
    assert(result == (mask & ~INT64_MIN));

    result = bzhiq(mask, 0x1f);
    assert(result == (mask & ~(-1 << 30)));

    result = bzhiq(mask, 0x40);
    assert(result == mask);

    result = rorxq(0x2132435465768798, 8);
    assert(result == 0x9821324354657687);

    result = sarxq(0xffeeddccbbaa9988, 8);
    assert(result == 0xffffeeddccbbaa99);

    result = sarxq(0x77eeddccbbaa9988, 8 | 64);
    assert(result == 0x0077eeddccbbaa99);

    result = shrxq(0xffeeddccbbaa9988, 8);
    assert(result == 0x00ffeeddccbbaa99);

    result = shrxq(0x77eeddccbbaa9988, 8 | 192);
    assert(result == 0x0077eeddccbbaa99);

    result = shlxq(0xffeeddccbbaa9988, 8);
    assert(result == 0xeeddccbbaa998800);
#endif

    /* 32 bits */
    result = andnl(mask, ehlo);
    assert(result == 0x04d4c4844);

    result = pextl((uint32_t) ehlo, mask);
    assert(result == 5);

    result = pdepl(result, mask);
    assert(result == (uint32_t)(ehlo & mask));

    result = pextl(-1u, mask);
    assert(result == 7); /* mask has 3 bits set */

    result = pdepl(-1u, mask);
    assert(result == (uint32_t)mask);

    result = bextrl(mask, 0x1f00);
    assert(result == (mask & ~INT32_MIN));

    result = bextrl(ehlo, 0x1018);
    assert(result == 0x4f);

    result = bextrl(mask, 0x1038);
    assert(result == 0);

    result = bextrl((reg_t)0x8f635a775ad3b9b4ull, 0x3018);
    assert(result == 0x5a);

    result = bextrl((reg_t)0xfedcba9876543210ull, 0x7f00);
    assert(result == 0x76543210u);

    result = bextrl(-1, 0);
    assert(result == 0);

    result = blsil(0xffff);
    assert(result == 1);

    result = blsmskl(0x300);
    assert(result == 0x1ff);

    result = blsrl(0xffc);
    assert(result == 0xff8);

    result = bzhil(mask, 0xf);
    assert(result == 1);

    result = rorxl(0x65768798, 8);
    assert(result == 0x98657687);

    result = sarxl(0xffeeddcc, 8);
    assert(result == 0xffffeedd);

    result = sarxl(0x77eeddcc, 8 | 32);
    assert(result == 0x0077eedd);

    result = shrxl(0xffeeddcc, 8);
    assert(result == 0x00ffeedd);

    result = shrxl(0x77eeddcc, 8 | 128);
    assert(result == 0x0077eedd);

    result = shlxl(0xffeeddcc, 8);
    assert(result == 0xeeddcc00);

    return 0;
}

