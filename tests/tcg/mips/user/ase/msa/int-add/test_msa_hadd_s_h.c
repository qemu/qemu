/*
 *  Test program for MSA instruction HADD_S.H
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
    char *instruction_name =  "HADD_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffa9ffa9ffa9ffa9ULL, 0xffa9ffa9ffa9ffa9ULL, },
        { 0x0054005400540054ULL, 0x0054005400540054ULL, },
        { 0xffcbffcbffcbffcbULL, 0xffcbffcbffcbffcbULL, },
        { 0x0032003200320032ULL, 0x0032003200320032ULL, },
        { 0xff8dffe20037ff8dULL, 0xffe20037ff8dffe2ULL, },
        { 0x0070001bffc60070ULL, 0x001bffc60070001bULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffaaffaaffaaffaaULL, 0xffaaffaaffaaffaaULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0xffccffccffccffccULL, 0xffccffccffccffccULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0xff8effe30038ff8eULL, 0xffe30038ff8effe3ULL, },
        { 0x0071001cffc70071ULL, 0x001cffc70071001cULL, },
        { 0xffa9ffa9ffa9ffa9ULL, 0xffa9ffa9ffa9ffa9ULL, },    /*  16  */
        { 0xffaaffaaffaaffaaULL, 0xffaaffaaffaaffaaULL, },
        { 0xff54ff54ff54ff54ULL, 0xff54ff54ff54ff54ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xff76ff76ff76ff76ULL, 0xff76ff76ff76ff76ULL, },
        { 0xffddffddffddffddULL, 0xffddffddffddffddULL, },
        { 0xff38ff8dffe2ff38ULL, 0xff8dffe2ff38ff8dULL, },
        { 0x001bffc6ff71001bULL, 0xffc6ff71001bffc6ULL, },
        { 0x0054005400540054ULL, 0x0054005400540054ULL, },    /*  24  */
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0021002100210021ULL, 0x0021002100210021ULL, },
        { 0x0088008800880088ULL, 0x0088008800880088ULL, },
        { 0xffe30038008dffe3ULL, 0x0038008dffe30038ULL, },
        { 0x00c60071001c00c6ULL, 0x0071001c00c60071ULL, },
        { 0xffcbffcbffcbffcbULL, 0xffcbffcbffcbffcbULL, },    /*  32  */
        { 0xffccffccffccffccULL, 0xffccffccffccffccULL, },
        { 0xff76ff76ff76ff76ULL, 0xff76ff76ff76ff76ULL, },
        { 0x0021002100210021ULL, 0x0021002100210021ULL, },
        { 0xff98ff98ff98ff98ULL, 0xff98ff98ff98ff98ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xff5affaf0004ff5aULL, 0xffaf0004ff5affafULL, },
        { 0x003dffe8ff93003dULL, 0xffe8ff93003dffe8ULL, },
        { 0x0032003200320032ULL, 0x0032003200320032ULL, },    /*  40  */
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0xffddffddffddffddULL, 0xffddffddffddffddULL, },
        { 0x0088008800880088ULL, 0x0088008800880088ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0066006600660066ULL, 0x0066006600660066ULL, },
        { 0xffc10016006bffc1ULL, 0x0016006bffc10016ULL, },
        { 0x00a4004ffffa00a4ULL, 0x004ffffa00a4004fULL, },
        { 0xffe20037ff8dffe2ULL, 0x0037ff8dffe20037ULL, },    /*  48  */
        { 0xffe30038ff8effe3ULL, 0x0038ff8effe30038ULL, },
        { 0xff8dffe2ff38ff8dULL, 0xffe2ff38ff8dffe2ULL, },
        { 0x0038008dffe30038ULL, 0x008dffe30038008dULL, },
        { 0xffaf0004ff5affafULL, 0x0004ff5affaf0004ULL, },
        { 0x0016006bffc10016ULL, 0x006bffc10016006bULL, },
        { 0xff71001bffc6ff71ULL, 0x001bffc6ff71001bULL, },
        { 0x00540054ff550054ULL, 0x0054ff5500540054ULL, },
        { 0x001bffc60070001bULL, 0xffc60070001bffc6ULL, },    /*  56  */
        { 0x001cffc70071001cULL, 0xffc70071001cffc7ULL, },
        { 0xffc6ff71001bffc6ULL, 0xff71001bffc6ff71ULL, },
        { 0x0071001c00c60071ULL, 0x001c00c60071001cULL, },
        { 0xffe8ff93003dffe8ULL, 0xff93003dffe8ff93ULL, },
        { 0x004ffffa00a4004fULL, 0xfffa00a4004ffffaULL, },
        { 0xffaaffaa00a9ffaaULL, 0xffaa00a9ffaaffaaULL, },
        { 0x008dffe30038008dULL, 0xffe30038008dffe3ULL, },
        { 0xfff2ffb2008a0095ULL, 0x00b200690079ffbcULL, },    /*  64  */
        { 0xff460049ffbb005dULL, 0x00420025003dffacULL, },
        { 0xffe2ff90fff7ffd5ULL, 0x0023000a0029ffc4ULL, },
        { 0xffd70033005900a3ULL, 0x003cffe30040ff50ULL, },
        { 0x0065ffcc00af0007ULL, 0x007900190090005eULL, },
        { 0xffb90063ffe0ffcfULL, 0x0009ffd50054004eULL, },
        { 0x0055ffaa001cff47ULL, 0xffeaffba00400066ULL, },
        { 0x004a004d007e0015ULL, 0x0003ff930057fff2ULL, },
        { 0x0016ff7a001bffcbULL, 0x008e002400260031ULL, },    /*  72  */
        { 0xff6a0011ff4cff93ULL, 0x001effe0ffea0021ULL, },
        { 0x0006ff58ff88ff0bULL, 0xffffffc5ffd60039ULL, },
        { 0xfffbfffbffeaffd9ULL, 0x0018ff9effedffc5ULL, },
        { 0x00daffe200c00022ULL, 0xfff4ffe60024ffeeULL, },
        { 0x002e0079fff1ffeaULL, 0xff84ffa2ffe8ffdeULL, },
        { 0x00caffc0002dff62ULL, 0xff65ff87ffd4fff6ULL, },
        { 0x00bf0063008f0030ULL, 0xff7eff60ffebff82ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HADD_S_H(b128_random[i], b128_random[j],
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
