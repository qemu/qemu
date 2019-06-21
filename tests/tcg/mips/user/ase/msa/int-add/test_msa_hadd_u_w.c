/*
 *  Test program for MSA instruction HADD_U.W
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *  Copyright (C) 2019  RT-RK Computer Based Systems LLC
 *  Copyright (C) 2019  Mateja Marjanovic <mateja.marjanovic@rt-rk.com>
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

#include "../../../../include/wrappers_msa.h"
#include "../../../../include/test_inputs_128.h"
#include "../../../../include/test_utils_128.h"

#define TEST_COUNT_TOTAL (                                                \
            (PATTERN_INPUTS_SHORT_COUNT) * (PATTERN_INPUTS_SHORT_COUNT) + \
            (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Int Add";
    char *instruction_name =  "HADD_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0001fffe0001fffeULL, 0x0001fffe0001fffeULL, },    /*   0  */
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0x0001aaa90001aaa9ULL, 0x0001aaa90001aaa9ULL, },
        { 0x0001555400015554ULL, 0x0001555400015554ULL, },
        { 0x0001cccb0001cccbULL, 0x0001cccb0001cccbULL, },
        { 0x0001333200013332ULL, 0x0001333200013332ULL, },
        { 0x000138e20001e38dULL, 0x00018e37000138e2ULL, },
        { 0x0001c71b00011c70ULL, 0x000171c60001c71bULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x000038e30000e38eULL, 0x00008e38000038e3ULL, },
        { 0x0000c71c00001c71ULL, 0x000071c70000c71cULL, },
        { 0x0001aaa90001aaa9ULL, 0x0001aaa90001aaa9ULL, },    /*  16  */
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0001555400015554ULL, 0x0001555400015554ULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0x0001777600017776ULL, 0x0001777600017776ULL, },
        { 0x0000dddd0000ddddULL, 0x0000dddd0000ddddULL, },
        { 0x0000e38d00018e38ULL, 0x000138e20000e38dULL, },
        { 0x000171c60000c71bULL, 0x00011c71000171c6ULL, },
        { 0x0001555400015554ULL, 0x0001555400015554ULL, },    /*  24  */
        { 0x0000555500005555ULL, 0x0000555500005555ULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0x0000aaaa0000aaaaULL, 0x0000aaaa0000aaaaULL, },
        { 0x0001222100012221ULL, 0x0001222100012221ULL, },
        { 0x0000888800008888ULL, 0x0000888800008888ULL, },
        { 0x00008e38000138e3ULL, 0x0000e38d00008e38ULL, },
        { 0x00011c71000071c6ULL, 0x0000c71c00011c71ULL, },
        { 0x0001cccb0001cccbULL, 0x0001cccb0001cccbULL, },    /*  32  */
        { 0x0000cccc0000ccccULL, 0x0000cccc0000ccccULL, },
        { 0x0001777600017776ULL, 0x0001777600017776ULL, },
        { 0x0001222100012221ULL, 0x0001222100012221ULL, },
        { 0x0001999800019998ULL, 0x0001999800019998ULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0x000105af0001b05aULL, 0x00015b04000105afULL, },
        { 0x000193e80000e93dULL, 0x00013e93000193e8ULL, },
        { 0x0001333200013332ULL, 0x0001333200013332ULL, },    /*  40  */
        { 0x0000333300003333ULL, 0x0000333300003333ULL, },
        { 0x0000dddd0000ddddULL, 0x0000dddd0000ddddULL, },
        { 0x0000888800008888ULL, 0x0000888800008888ULL, },
        { 0x0000ffff0000ffffULL, 0x0000ffff0000ffffULL, },
        { 0x0000666600006666ULL, 0x0000666600006666ULL, },
        { 0x00006c16000116c1ULL, 0x0000c16b00006c16ULL, },
        { 0x0000fa4f00004fa4ULL, 0x0000a4fa0000fa4fULL, },
        { 0x0001e38d00018e37ULL, 0x000138e20001e38dULL, },    /*  48  */
        { 0x0000e38e00008e38ULL, 0x000038e30000e38eULL, },
        { 0x00018e38000138e2ULL, 0x0000e38d00018e38ULL, },
        { 0x000138e30000e38dULL, 0x00008e38000138e3ULL, },
        { 0x0001b05a00015b04ULL, 0x000105af0001b05aULL, },
        { 0x000116c10000c16bULL, 0x00006c16000116c1ULL, },
        { 0x00011c71000171c6ULL, 0x0000c71b00011c71ULL, },
        { 0x0001aaaa0000aaa9ULL, 0x0000aaaa0001aaaaULL, },
        { 0x00011c70000171c6ULL, 0x0001c71b00011c70ULL, },    /*  56  */
        { 0x00001c71000071c7ULL, 0x0000c71c00001c71ULL, },
        { 0x0000c71b00011c71ULL, 0x000171c60000c71bULL, },
        { 0x000071c60000c71cULL, 0x00011c71000071c6ULL, },
        { 0x0000e93d00013e93ULL, 0x000193e80000e93dULL, },
        { 0x00004fa40000a4faULL, 0x0000fa4f00004fa4ULL, },
        { 0x0000555400015555ULL, 0x0001555400005554ULL, },
        { 0x0000e38d00008e38ULL, 0x000138e30000e38dULL, },
        { 0x00016f3600007da2ULL, 0x000056c50001ae87ULL, },    /*  64  */
        { 0x000088cd0000ef6aULL, 0x0001068100015177ULL, },
        { 0x000137140000b3e2ULL, 0x000112660001238fULL, },
        { 0x00009eb700010ab0ULL, 0x0000d43f0001e11bULL, },
        { 0x0001e28a0000a2d3ULL, 0x00001e550000c54bULL, },
        { 0x0000fc210001149bULL, 0x0000ce110000683bULL, },
        { 0x0001aa680000d913ULL, 0x0000d9f600003a53ULL, },
        { 0x0001120b00012fe1ULL, 0x00009bcf0000f7dfULL, },
        { 0x0001932600010f0fULL, 0x0000333600015b37ULL, },    /*  72  */
        { 0x0000acbd000180d7ULL, 0x0000e2f20000fe27ULL, },
        { 0x00015b040001454fULL, 0x0000eed70000d03fULL, },
        { 0x0000c2a700019c1dULL, 0x0000b0b000018dcbULL, },
        { 0x0001571b0000b371ULL, 0x0000994f0001594eULL, },
        { 0x000070b200012539ULL, 0x0001490b0000fc3eULL, },
        { 0x00011ef90000e9b1ULL, 0x000154f00000ce56ULL, },
        { 0x0000869c0001407fULL, 0x000116c900018be2ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_U_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_U_W(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    gettimeofday(&end, NULL);

    elapsed_time = (end.tv_sec - start.tv_sec) * 1000.0;
    elapsed_time += (end.tv_usec - start.tv_usec) / 1000.0;

    ret = check_results_128(isa_ase_name, group_name, instruction_name,
                            TEST_COUNT_TOTAL, elapsed_time,
                            &b128_result[0][0], &b128_expect[0][0]);

    return ret;
}
