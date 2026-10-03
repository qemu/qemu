/*
 *  Test program for MIPS64R6 instruction SRAV
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 */

#include <sys/time.h>
#include <stdint.h>

#include "../mips/include/wrappers_mips64r6.h"
#include "../mips/include/test_inputs_64.h"
#include "../mips/include/test_utils_64.h"

#define TEST_COUNT_TOTAL (PATTERN_INPUTS_64_COUNT + RANDOM_INPUTS_64_COUNT)


int32_t main(void)
{
    uint64_t b64_expect[TEST_COUNT_TOTAL] = {
        0xffffffffffffffff,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0xffffffffffffffff,
        0xffffffffaaaaaaaa,
        0xffffffffffeaaaaa,
        0xfffffffffffffd55,
        0xfffffffffffaaaaa,
        0xfffffffffffff555,
        0xfffffffffffeaaaa,
        0xffffffffffffd555,
        0x0,
        0x55555555,
        0x155555,
        0x2aa,
        0x55555,
        0xaaa,
        0x15555,
        0x2aaa,
        0xffffffffffffffff,
        0xffffffffcccccccc,
        0xfffffffffff33333,
        0xfffffffffffffe66,
        0xfffffffffffccccc,
        0xfffffffffffff999,
        0xffffffffffff3333,
        0xffffffffffffe666,
        0x0,
        0x33333333,
        0xccccc,
        0x199,
        0x33333,
        0x666,
        0xcccc,
        0x1999,
        0xffffffffffffffff,
        0xffffffff8e38e38e,
        0xffffffffffe38e38,
        0xfffffffffffffc71,
        0xfffffffffff8e38e,
        0xfffffffffffff1c7,
        0xfffffffffffe38e3,
        0xffffffffffffc71c,
        0x0,
        0x71c71c71,
        0x1c71c7,
        0x38e,
        0x71c71,
        0xe38,
        0x1c71c,
        0x38e3,
        0x28625540,
        0x286255,
        0x28625540,
        0xa189,
        0x4d93c708,
        0x4d93c7,
        0x4d93c708,
        0x1364f,
        0xffffffffb9cf8b80,
        0xffffffffffb9cf8b,
        0xffffffffb9cf8b80,
        0xfffffffffffee73e,
        0x5e31e24e,
        0x5e31e2,
        0x5e31e24e,
        0x178c7,
    };

    return !check_binary_op_64(do_mips64r6_SRAV, "srav", b64_expect);
}
