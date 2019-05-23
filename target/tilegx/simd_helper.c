/*
 * QEMU TILE-Gx helpers
 *
 *  Copyright (c) 2015 Chen Gang
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see
 * <http://www.gnu.org/licenses/lgpl-2.1.html>
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "exec/helper-proto.h"


/* Broadcast a value to all elements of a vector.  */
#define V1(X)      (((X) & 0xff) * 0x0101010101010101ull)
#define V2(X)      (((X) & 0xffff) * 0x0001000100010001ull)


uint64_t helper_v1multu(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 64; i += 8) {
        unsigned ae = extract64(a, i, 8);
        unsigned be = extract64(b, i, 8);
        r = deposit64(r, i, 8, ae * be);
    }
    return r;
}

uint64_t helper_v2mults(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    /* While the instruction talks about signed inputs, with a
       truncated result the sign of the inputs doesn't matter.  */
    for (i = 0; i < 64; i += 16) {
        unsigned ae = extract64(a, i, 16);
        unsigned be = extract64(b, i, 16);
        r = deposit64(r, i, 16, ae * be);
    }
    return r;
}

uint64_t helper_v1shl(uint64_t a, uint64_t b)
{
    uint64_t m;

    b &= 7;
    m = V1(0xff >> b);
    return (a & m) << b;
}

uint64_t helper_v2shl(uint64_t a, uint64_t b)
{
    uint64_t m;

    b &= 15;
    m = V2(0xffff >> b);
    return (a & m) << b;
}

uint64_t helper_v1shru(uint64_t a, uint64_t b)
{
    uint64_t m;

    b &= 7;
    m = V1(0xff << b);
    return (a & m) >> b;
}

uint64_t helper_v2shru(uint64_t a, uint64_t b)
{
    uint64_t m;

    b &= 15;
    m = V2(0xffff << b);
    return (a & m) >> b;
}

uint64_t helper_v1shrs(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    b &= 7;
    for (i = 0; i < 64; i += 8) {
        r = deposit64(r, i, 8, sextract64(a, i + b, 8 - b));
    }
    return r;
}

uint64_t helper_v2shrs(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    b &= 15;
    for (i = 0; i < 64; i += 16) {
        r = deposit64(r, i, 16, sextract64(a, i + b, 16 - b));
    }
    return r;
}

uint64_t helper_v1int_h(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 32; i += 8) {
        r = deposit64(r, 2 * i + 8, 8, extract64(a, i + 32, 8));
        r = deposit64(r, 2 * i, 8, extract64(b, i + 32, 8));
    }
    return r;
}

uint64_t helper_v1int_l(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 32; i += 8) {
        r = deposit64(r, 2 * i + 8, 8, extract64(a, i, 8));
        r = deposit64(r, 2 * i, 8, extract64(b, i, 8));
    }
    return r;
}

uint64_t helper_v2int_h(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 32; i += 16) {
        r = deposit64(r, 2 * i + 16, 16, extract64(a, i + 32, 16));
        r = deposit64(r, 2 * i, 16, extract64(b, i + 32, 16));
    }
    return r;
}

uint64_t helper_v2int_l(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    for (i = 0; i < 32; i += 16) {
        r = deposit64(r, 2 * i + 16, 16, extract64(a, i, 16));
        r = deposit64(r, 2 * i, 16, extract64(b, i, 16));
    }
    return r;
}
