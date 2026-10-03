/*
 *  Test program for MIPS64R6 instruction NOR
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
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0xffffffffffffffff,
        0x5555555555555555,
        0xaaaaaaaaaaaaaaaa,
        0x3333333333333333,
        0xcccccccccccccccc,
        0x1c71c71c71c71c71,
        0xe38e38e38e38e38e,
        0x0,
        0x5555555555555555,
        0x5555555555555555,
        0x0,
        0x1111111111111111,
        0x4444444444444444,
        0x1451451451451451,
        0x4104104104104104,
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x2222222222222222,
        0x8888888888888888,
        0x820820820820820,
        0xa28a28a28a28a28a,
        0x0,
        0x3333333333333333,
        0x1111111111111111,
        0x2222222222222222,
        0x3333333333333333,
        0x0,
        0x1031031031031031,
        0x2302302302302302,
        0x0,
        0xcccccccccccccccc,
        0x4444444444444444,
        0x8888888888888888,
        0x0,
        0xcccccccccccccccc,
        0xc40c40c40c40c40,
        0xc08c08c08c08c08c,
        0x0,
        0x1c71c71c71c71c71,
        0x1451451451451451,
        0x820820820820820,
        0x1031031031031031,
        0xc40c40c40c40c40,
        0x1c71c71c71c71c71,
        0x0,
        0x0,
        0xe38e38e38e38e38e,
        0x4104104104104104,
        0xa28a28a28a28a28a,
        0x2302302302302302,
        0xc08c08c08c08c08c,
        0x0,
        0xe38e38e38e38e38e,
        0x77951933d79daabf,
        0x4011910920c28b7,
        0x538511114610203f,
        0x7900932818c08b1,
        0x4011910920c28b7,
        0x441ff9cb26c38f7,
        0x1511402203077,
        0x400e990a04c18b1,
        0x538511114610203f,
        0x1511402203077,
        0x53a551554630747f,
        0x3a0411000001431,
        0x7900932818c08b1,
        0x400e990a04c18b1,
        0x3a0411000001431,
        0x8fb0e9b2a1ce1db1,
    };

    return !check_binary_op_64(do_mips64r6_NOR, "nor", b64_expect);
}
