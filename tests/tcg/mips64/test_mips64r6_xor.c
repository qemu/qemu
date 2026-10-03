/*
 *  Test program for MIPS64R6 instruction XOR
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
        0x0,
        0xffffffffffffffff,
        0x5555555555555555,
        0xaaaaaaaaaaaaaaaa,
        0x3333333333333333,
        0xcccccccccccccccc,
        0x1c71c71c71c71c71,
        0xe38e38e38e38e38e,
        0xffffffffffffffff,
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x5555555555555555,
        0xcccccccccccccccc,
        0x3333333333333333,
        0xe38e38e38e38e38e,
        0x1c71c71c71c71c71,
        0x5555555555555555,
        0xaaaaaaaaaaaaaaaa,
        0x0,
        0xffffffffffffffff,
        0x6666666666666666,
        0x9999999999999999,
        0x4924924924924924,
        0xb6db6db6db6db6db,
        0xaaaaaaaaaaaaaaaa,
        0x5555555555555555,
        0xffffffffffffffff,
        0x0,
        0x9999999999999999,
        0x6666666666666666,
        0xb6db6db6db6db6db,
        0x4924924924924924,
        0x3333333333333333,
        0xcccccccccccccccc,
        0x6666666666666666,
        0x9999999999999999,
        0x0,
        0xffffffffffffffff,
        0x2f42f42f42f42f42,
        0xd0bd0bd0bd0bd0bd,
        0xcccccccccccccccc,
        0x3333333333333333,
        0x9999999999999999,
        0x6666666666666666,
        0xffffffffffffffff,
        0x0,
        0xd0bd0bd0bd0bd0bd,
        0x2f42f42f42f42f42,
        0x1c71c71c71c71c71,
        0xe38e38e38e38e38e,
        0x4924924924924924,
        0xb6db6db6db6db6db,
        0x2f42f42f42f42f42,
        0xd0bd0bd0bd0bd0bd,
        0x0,
        0xffffffffffffffff,
        0xe38e38e38e38e38e,
        0x1c71c71c71c71c71,
        0xb6db6db6db6db6db,
        0x4924924924924924,
        0xd0bd0bd0bd0bd0bd,
        0x2f42f42f42f42f42,
        0xffffffffffffffff,
        0x0,
        0x0,
        0x73d4e6af65f19248,
        0x2430486691addec0,
        0xf825f0817653b70e,
        0x73d4e6af65f19248,
        0x0,
        0x57e4aec9f45c4c88,
        0x8bf1162e13a22546,
        0x2430486691addec0,
        0x57e4aec9f45c4c88,
        0x0,
        0xdc15b8e7e7fe69ce,
        0xf825f0817653b70e,
        0x8bf1162e13a22546,
        0xdc15b8e7e7fe69ce,
        0x0,
    };

    return !check_binary_op_64(do_mips64r6_XOR, "xor", b64_expect);
}
