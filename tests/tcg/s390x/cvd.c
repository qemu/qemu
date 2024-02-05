/*
 * Test the CONVERT TO DECIMAL instruction.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>

static uint64_t cvd(int32_t x)
{
    uint64_t ret;

    asm("cvd %[x],%[ret]" : [ret] "=R" (ret) : [x] "r" (x));

    return ret;
}

static uint64_t cvdy(int32_t x)
{
    uint64_t ret;

    asm("cvdy %[x],%[ret]" : [ret] "=T" (ret) : [x] "r" (x));

    return ret;
}

static __uint128_t cvdg(int64_t x)
{
    __uint128_t ret;

    asm("cvdg %[x],%[ret]" : [ret] "=T" (ret) : [x] "r" (x));

    return ret;
}

int main(void)
{
    __uint128_t m = (((__uint128_t)0x9223372036854775) << 16) | 0x8070;

    assert(cvd(0) == 0xc);
    assert(cvd(1) == 0x1c);
    assert(cvd(25594) == 0x25594c);
    assert(cvd(-1) == 0x1d);
    assert(cvd(0x7fffffff) == 0x2147483647c);
    assert(cvd(-0x80000000) == 0x2147483648d);

    assert(cvdy(0) == 0xc);
    assert(cvdy(1) == 0x1c);
    assert(cvdy(25594) == 0x25594c);
    assert(cvdy(-1) == 0x1d);
    assert(cvdy(0x7fffffff) == 0x2147483647c);
    assert(cvdy(-0x80000000) == 0x2147483648d);

    assert(cvdg(0) == 0xc);
    assert(cvdg(1) == 0x1c);
    assert(cvdg(25594) == 0x25594c);
    assert(cvdg(-1) == 0x1d);
    assert(cvdg(0x7fffffffffffffff) == (m + 0xc));
    assert(cvdg(-0x8000000000000000) == (m + 0x1d));

    return EXIT_SUCCESS;
}
