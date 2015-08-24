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

#include "cpu.h"
#include "qemu-common.h"
#include "exec/helper-proto.h"


uint64_t helper_v1shl(uint64_t a, uint64_t b)
{
    uint64_t m;

    b &= 7;
    m = 0x0101010101010101ULL * (0xff >> b);
    return (a & m) << b;
}

uint64_t helper_v1shru(uint64_t a, uint64_t b)
{
    uint64_t m;

    b &= 7;
    m = 0x0101010101010101ULL * ((0xff << b) & 0xff);
    return (a & m) >> b;
}

uint64_t helper_v1shrs(uint64_t a, uint64_t b)
{
    uint64_t r = 0;
    int i;

    b &= 7;
    for (i = 0; i < 64; i += 8) {
        int64_t ae = (int8_t)(a >> i);
        r |= ((ae >> b) & 0xff) << i;
    }
    return r;
}
