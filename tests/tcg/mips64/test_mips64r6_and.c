/*
 *  Test program for MIPS64R6 instruction AND
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
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x5555555555555555,
        0xcccccccccccccccc,
        0x3333333333333333,
        0xe38e38e38e38e38e,
        0x1c71c71c71c71c71,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x0,
        0x8888888888888888,
        0x2222222222222222,
        0xa28a28a28a28a28a,
        0x820820820820820,
        0x5555555555555555,
        0x0,
        0x0,
        0x5555555555555555,
        0x4444444444444444,
        0x1111111111111111,
        0x4104104104104104,
        0x1451451451451451,
        0xcccccccccccccccc,
        0x0,
        0x8888888888888888,
        0x4444444444444444,
        0xcccccccccccccccc,
        0x0,
        0xc08c08c08c08c08c,
        0xc40c40c40c40c40,
        0x3333333333333333,
        0x0,
        0x2222222222222222,
        0x1111111111111111,
        0x0,
        0x3333333333333333,
        0x2302302302302302,
        0x1031031031031031,
        0xe38e38e38e38e38e,
        0x0,
        0xa28a28a28a28a28a,
        0x4104104104104104,
        0xc08c08c08c08c08c,
        0x2302302302302302,
        0xe38e38e38e38e38e,
        0x0,
        0x1c71c71c71c71c71,
        0x0,
        0x820820820820820,
        0x1451451451451451,
        0xc40c40c40c40c40,
        0x1031031031031031,
        0x0,
        0x1c71c71c71c71c71,
        0x886ae6cc28625540,
        0x882a004008024500,
        0x884aa68828420100,
        0x4a064c08204040,
        0x882a004008024500,
        0xfbbe00634d93c708,
        0xa81a002209838300,
        0x700e00414c11c208,
        0x884aa68828420100,
        0xa81a002209838300,
        0xac5aaeaab9cf8b80,
        0x204a060818018200,
        0x4a064c08204040,
        0x700e00414c11c208,
        0x204a060818018200,
        0x704f164d5e31e24e,
    };

    return !check_binary_op_64(do_mips64r6_AND, "and", b64_expect);
}
