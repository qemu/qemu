/*
 *  AArch64 specific helpers
 *
 *  Copyright (c) 2013 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "cpu.h"
#include "exec/gdbstub.h"
#include "helper.h"
#include "qemu/host-utils.h"
#include "sysemu/sysemu.h"
#include "qemu/bitops.h"

/* C2.4.7 Multiply and divide */
/* special cases for 0 and LLONG_MIN are mandated by the standard */
uint64_t HELPER(udiv64)(uint64_t num, uint64_t den)
{
    if (den == 0) {
        return 0;
    }
    return num / den;
}

int64_t HELPER(sdiv64)(int64_t num, int64_t den)
{
    if (den == 0) {
        return 0;
    }
    if (num == LLONG_MIN && den == -1) {
        return LLONG_MIN;
    }
    return num / den;
}

uint64_t HELPER(clz64)(uint64_t x)
{
    return clz64(x);
}

uint64_t HELPER(cls64)(uint64_t x)
{
    return clrsb64(x);
}

uint32_t HELPER(cls32)(uint32_t x)
{
    return clrsb32(x);
}

uint64_t HELPER(rbit64)(uint64_t x)
{
    /* assign the correct byte position */
    x = bswap64(x);

    /* assign the correct nibble position */
    x = ((x & 0xf0f0f0f0f0f0f0f0ULL) >> 4)
        | ((x & 0x0f0f0f0f0f0f0f0fULL) << 4);

    /* assign the correct bit position */
    x = ((x & 0x8888888888888888ULL) >> 3)
        | ((x & 0x4444444444444444ULL) >> 1)
        | ((x & 0x2222222222222222ULL) << 1)
        | ((x & 0x1111111111111111ULL) << 3);

    return x;
}
