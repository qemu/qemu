/*
 *  Test program for MSA instruction BSET.B
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

#include "../../../../include/wrappers_msa.h"
#include "../../../../include/test_inputs_128.h"
#include "../../../../include/test_utils_128.h"

#define TEST_COUNT_TOTAL (                                                \
            (PATTERN_INPUTS_SHORT_COUNT) * (PATTERN_INPUTS_SHORT_COUNT) + \
            (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Bit Set";
    char *instruction_name =  "BSET.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*   8  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x2020202020202020ULL, 0x2020202020202020ULL, },
        { 0x1010101010101010ULL, 0x1010101010101010ULL, },
        { 0x0808080808080808ULL, 0x0808080808080808ULL, },
        { 0x0840010840010840ULL, 0x0108400108400108ULL, },
        { 0x1002801002801002ULL, 0x8010028010028010ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0xababababababababULL, 0xababababababababULL, },
        { 0xaeaeaeaeaeaeaeaeULL, 0xaeaeaeaeaeaeaeaeULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xbabababababababaULL, 0xbabababababababaULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0xaaeaabaaeaabaaeaULL, 0xabaaeaabaaeaabaaULL, },
        { 0xbaaaaabaaaaabaaaULL, 0xaabaaaaabaaaaabaULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x7575757575757575ULL, 0x7575757575757575ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5d5d5d5d5d5d5d5dULL, 0x5d5d5d5d5d5d5d5dULL, },
        { 0x5d55555d55555d55ULL, 0x555d55555d55555dULL, },
        { 0x5557d55557d55557ULL, 0xd55557d55557d555ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },    /*  32  */
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xececececececececULL, 0xececececececececULL, },
        { 0xdcdcdcdcdcdcdcdcULL, 0xdcdcdcdcdcdcdcdcULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0xcccccdcccccdccccULL, 0xcdcccccdcccccdccULL, },
        { 0xdcceccdcceccdcceULL, 0xccdcceccdcceccdcULL, },
        { 0xb3b3b3b3b3b3b3b3ULL, 0xb3b3b3b3b3b3b3b3ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3737373737373737ULL, 0x3737373737373737ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x3b3b3b3b3b3b3b3bULL, 0x3b3b3b3b3b3b3b3bULL, },
        { 0x3b73333b73333b73ULL, 0x333b73333b73333bULL, },
        { 0x3333b33333b33333ULL, 0xb33333b33333b333ULL, },
        { 0xe38eb8e38eb8e38eULL, 0xb8e38eb8e38eb8e3ULL, },    /*  48  */
        { 0xe38f39e38f39e38fULL, 0x39e38f39e38f39e3ULL, },
        { 0xe78e3ce78e3ce78eULL, 0x3ce78e3ce78e3ce7ULL, },
        { 0xe3ae38e3ae38e3aeULL, 0x38e3ae38e3ae38e3ULL, },
        { 0xf39e38f39e38f39eULL, 0x38f39e38f39e38f3ULL, },
        { 0xeb8e38eb8e38eb8eULL, 0x38eb8e38eb8e38ebULL, },
        { 0xebce39ebce39ebceULL, 0x39ebce39ebce39ebULL, },
        { 0xf38eb8f38eb8f38eULL, 0xb8f38eb8f38eb8f3ULL, },
        { 0x9cf1c79cf1c79cf1ULL, 0xc79cf1c79cf1c79cULL, },    /*  56  */
        { 0x1d71c71d71c71d71ULL, 0xc71d71c71d71c71dULL, },
        { 0x1c75c71c75c71c75ULL, 0xc71c75c71c75c71cULL, },
        { 0x3c71e73c71e73c71ULL, 0xe73c71e73c71e73cULL, },
        { 0x1c71d71c71d71c71ULL, 0xd71c71d71c71d71cULL, },
        { 0x1c79cf1c79cf1c79ULL, 0xcf1c79cf1c79cf1cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c73c71c73c71c73ULL, 0xc71c73c71c73c71cULL, },
        { 0x896ee6dc29667541ULL, 0x4be70b5efe7bb11cULL, },    /*  64  */
        { 0x886ae7cc286ad541ULL, 0x4fe70b5efefbb41cULL, },
        { 0x986ee6cc2ae25d41ULL, 0xcb674bdefe7bb01cULL, },
        { 0x89eae6ec68625540ULL, 0x6b670b5ffe7fb40dULL, },
        { 0xfbbe40734d97e709ULL, 0x1af7bb5a553f53fcULL, },
        { 0xfbfe016b6d9bc709ULL, 0x16f7bb1e35bf56fcULL, },
        { 0xfbbe40674f93cf09ULL, 0x92f7fb9a1d3f72fcULL, },
        { 0xfbbe40634d93c748ULL, 0x32f7bb1b173f56fdULL, },
        { 0xad5eeebab9cfab81ULL, 0x2fd8ceffeb2b2514ULL, },    /*  72  */
        { 0xac5aafaab9cf8b81ULL, 0x27d8ceffabab2514ULL, },
        { 0xbc5eeeaebbcf8b81ULL, 0xa7d9c6ffab2b2514ULL, },
        { 0xaddaeeaaf9cf8fc0ULL, 0x27dac7ffab2f2515ULL, },
        { 0x714f565d5f35e24fULL, 0x8df188d8e94ae3b0ULL, },
        { 0x784f174d7e39e24fULL, 0x8df188dca9c2e6b0ULL, },
        { 0x704f564d5eb1ea4fULL, 0x8df1c8d8a94ae2b0ULL, },
        { 0x71cf566d5e33e64eULL, 0xadf389d9ab46e6a1ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSET_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSET_B(b128_random[i], b128_random[j],
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
