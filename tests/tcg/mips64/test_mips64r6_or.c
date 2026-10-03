/*
 *  Test program for MIPS64R6 instruction OR
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
        0xffffffffffffffff,
        0x0,
        0xaaaaaaaaaaaaaaaa,
        0x5555555555555555,
        0xcccccccccccccccc,
        0x3333333333333333,
        0xe38e38e38e38e38e,
        0x1c71c71c71c71c71,
        0xffffffffffffffff,
        0xaaaaaaaaaaaaaaaa,
        0xaaaaaaaaaaaaaaaa,
        0xffffffffffffffff,
        0xeeeeeeeeeeeeeeee,
        0xbbbbbbbbbbbbbbbb,
        0xebaebaebaebaebae,
        0xbefbefbefbefbefb,
        0xffffffffffffffff,
        0x5555555555555555,
        0xffffffffffffffff,
        0x5555555555555555,
        0xdddddddddddddddd,
        0x7777777777777777,
        0xf7df7df7df7df7df,
        0x5d75d75d75d75d75,
        0xffffffffffffffff,
        0xcccccccccccccccc,
        0xeeeeeeeeeeeeeeee,
        0xdddddddddddddddd,
        0xcccccccccccccccc,
        0xffffffffffffffff,
        0xefcefcefcefcefce,
        0xdcfdcfdcfdcfdcfd,
        0xffffffffffffffff,
        0x3333333333333333,
        0xbbbbbbbbbbbbbbbb,
        0x7777777777777777,
        0xffffffffffffffff,
        0x3333333333333333,
        0xf3bf3bf3bf3bf3bf,
        0x3f73f73f73f73f73,
        0xffffffffffffffff,
        0xe38e38e38e38e38e,
        0xebaebaebaebaebae,
        0xf7df7df7df7df7df,
        0xefcefcefcefcefce,
        0xf3bf3bf3bf3bf3bf,
        0xe38e38e38e38e38e,
        0xffffffffffffffff,
        0xffffffffffffffff,
        0x1c71c71c71c71c71,
        0xbefbefbefbefbefb,
        0x5d75d75d75d75d75,
        0xdcfdcfdcfdcfdcfd,
        0x3f73f73f73f73f73,
        0xffffffffffffffff,
        0x1c71c71c71c71c71,
        0x886ae6cc28625540,
        0xfbfee6ef6df3d748,
        0xac7aeeeeb9efdfc0,
        0xf86ff6cd7e73f74e,
        0xfbfee6ef6df3d748,
        0xfbbe00634d93c708,
        0xfffeaeebfddfcf88,
        0xfbff166f5fb3e74e,
        0xac7aeeeeb9efdfc0,
        0xfffeaeebfddfcf88,
        0xac5aaeaab9cf8b80,
        0xfc5fbeefffffebce,
        0xf86ff6cd7e73f74e,
        0xfbff166f5fb3e74e,
        0xfc5fbeefffffebce,
        0x704f164d5e31e24e,
    };

    return !check_binary_op_64(do_mips64r6_OR, "or", b64_expect);
}
