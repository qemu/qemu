/*
 *  Test program for MSA instruction BNEG.B
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
    char *instruction_name =  "BNEG.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x7f7f7f7f7f7f7f7fULL, 0x7f7f7f7f7f7f7f7fULL, },    /*   0  */
        { 0xfefefefefefefefeULL, 0xfefefefefefefefeULL, },
        { 0xfbfbfbfbfbfbfbfbULL, 0xfbfbfbfbfbfbfbfbULL, },
        { 0xdfdfdfdfdfdfdfdfULL, 0xdfdfdfdfdfdfdfdfULL, },
        { 0xefefefefefefefefULL, 0xefefefefefefefefULL, },
        { 0xf7f7f7f7f7f7f7f7ULL, 0xf7f7f7f7f7f7f7f7ULL, },
        { 0xf7bffef7bffef7bfULL, 0xfef7bffef7bffef7ULL, },
        { 0xeffd7feffd7feffdULL, 0x7feffd7feffd7fefULL, },
        { 0x8080808080808080ULL, 0x8080808080808080ULL, },    /*   8  */
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },
        { 0x0404040404040404ULL, 0x0404040404040404ULL, },
        { 0x2020202020202020ULL, 0x2020202020202020ULL, },
        { 0x1010101010101010ULL, 0x1010101010101010ULL, },
        { 0x0808080808080808ULL, 0x0808080808080808ULL, },
        { 0x0840010840010840ULL, 0x0108400108400108ULL, },
        { 0x1002801002801002ULL, 0x8010028010028010ULL, },
        { 0x2a2a2a2a2a2a2a2aULL, 0x2a2a2a2a2a2a2a2aULL, },    /*  16  */
        { 0xababababababababULL, 0xababababababababULL, },
        { 0xaeaeaeaeaeaeaeaeULL, 0xaeaeaeaeaeaeaeaeULL, },
        { 0x8a8a8a8a8a8a8a8aULL, 0x8a8a8a8a8a8a8a8aULL, },
        { 0xbabababababababaULL, 0xbabababababababaULL, },
        { 0xa2a2a2a2a2a2a2a2ULL, 0xa2a2a2a2a2a2a2a2ULL, },
        { 0xa2eaaba2eaaba2eaULL, 0xaba2eaaba2eaaba2ULL, },
        { 0xbaa82abaa82abaa8ULL, 0x2abaa82abaa82abaULL, },
        { 0xd5d5d5d5d5d5d5d5ULL, 0xd5d5d5d5d5d5d5d5ULL, },    /*  24  */
        { 0x5454545454545454ULL, 0x5454545454545454ULL, },
        { 0x5151515151515151ULL, 0x5151515151515151ULL, },
        { 0x7575757575757575ULL, 0x7575757575757575ULL, },
        { 0x4545454545454545ULL, 0x4545454545454545ULL, },
        { 0x5d5d5d5d5d5d5d5dULL, 0x5d5d5d5d5d5d5d5dULL, },
        { 0x5d15545d15545d15ULL, 0x545d15545d15545dULL, },
        { 0x4557d54557d54557ULL, 0xd54557d54557d545ULL, },
        { 0x4c4c4c4c4c4c4c4cULL, 0x4c4c4c4c4c4c4c4cULL, },    /*  32  */
        { 0xcdcdcdcdcdcdcdcdULL, 0xcdcdcdcdcdcdcdcdULL, },
        { 0xc8c8c8c8c8c8c8c8ULL, 0xc8c8c8c8c8c8c8c8ULL, },
        { 0xececececececececULL, 0xececececececececULL, },
        { 0xdcdcdcdcdcdcdcdcULL, 0xdcdcdcdcdcdcdcdcULL, },
        { 0xc4c4c4c4c4c4c4c4ULL, 0xc4c4c4c4c4c4c4c4ULL, },
        { 0xc48ccdc48ccdc48cULL, 0xcdc48ccdc48ccdc4ULL, },
        { 0xdcce4cdcce4cdcceULL, 0x4cdcce4cdcce4cdcULL, },
        { 0xb3b3b3b3b3b3b3b3ULL, 0xb3b3b3b3b3b3b3b3ULL, },    /*  40  */
        { 0x3232323232323232ULL, 0x3232323232323232ULL, },
        { 0x3737373737373737ULL, 0x3737373737373737ULL, },
        { 0x1313131313131313ULL, 0x1313131313131313ULL, },
        { 0x2323232323232323ULL, 0x2323232323232323ULL, },
        { 0x3b3b3b3b3b3b3b3bULL, 0x3b3b3b3b3b3b3b3bULL, },
        { 0x3b73323b73323b73ULL, 0x323b73323b73323bULL, },
        { 0x2331b32331b32331ULL, 0xb32331b32331b323ULL, },
        { 0x630eb8630eb8630eULL, 0xb8630eb8630eb863ULL, },    /*  48  */
        { 0xe28f39e28f39e28fULL, 0x39e28f39e28f39e2ULL, },
        { 0xe78a3ce78a3ce78aULL, 0x3ce78a3ce78a3ce7ULL, },
        { 0xc3ae18c3ae18c3aeULL, 0x18c3ae18c3ae18c3ULL, },
        { 0xf39e28f39e28f39eULL, 0x28f39e28f39e28f3ULL, },
        { 0xeb8630eb8630eb86ULL, 0x30eb8630eb8630ebULL, },
        { 0xebce39ebce39ebceULL, 0x39ebce39ebce39ebULL, },
        { 0xf38cb8f38cb8f38cULL, 0xb8f38cb8f38cb8f3ULL, },
        { 0x9cf1479cf1479cf1ULL, 0x479cf1479cf1479cULL, },    /*  56  */
        { 0x1d70c61d70c61d70ULL, 0xc61d70c61d70c61dULL, },
        { 0x1875c31875c31875ULL, 0xc31875c31875c318ULL, },
        { 0x3c51e73c51e73c51ULL, 0xe73c51e73c51e73cULL, },
        { 0x0c61d70c61d70c61ULL, 0xd70c61d70c61d70cULL, },
        { 0x1479cf1479cf1479ULL, 0xcf1479cf1479cf14ULL, },
        { 0x1431c61431c61431ULL, 0xc61431c61431c614ULL, },
        { 0x0c73470c73470c73ULL, 0x470c73470c73470cULL, },
        { 0x896ea6dc29667541ULL, 0x43e7031ebe73b11cULL, },    /*  64  */
        { 0x802ae7c4086ad541ULL, 0x4fe7035adefbb41cULL, },
        { 0x986ea6c82ae25d41ULL, 0xcb664bdef673901cULL, },
        { 0x89eaa6ec68605100ULL, 0x6b650a5ffc7fb40dULL, },
        { 0xfaba40734c97e709ULL, 0x1a77b35a553753ecULL, },
        { 0xf3fe016b6d9b4709ULL, 0x1677b31e35bf56ecULL, },
        { 0xebba40674f13cf09ULL, 0x92f6fb9a1d3772ecULL, },
        { 0xfa3e40430d91c348ULL, 0x32f5ba1b173b56fdULL, },
        { 0xad5eeebab8cbab81ULL, 0x2f58cebfeb232404ULL, },    /*  72  */
        { 0xa41aafa299c70b81ULL, 0x2358cefb8bab2104ULL, },
        { 0xbc5eeeaebb4f8381ULL, 0xa7d9867fa3230504ULL, },
        { 0xaddaee8af9cd8fc0ULL, 0x07dac7fea92f2115ULL, },
        { 0x714b565d5f35c24fULL, 0x85718098e94ae3b0ULL, },
        { 0x780f17457e39624fULL, 0x897180dc89c2e6b0ULL, },
        { 0x604b56495cb1ea4fULL, 0x0df0c858a14ac2b0ULL, },
        { 0x71cf566d1e33e60eULL, 0xadf389d9ab46e6a1ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BNEG_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BNEG_B(b128_random[i], b128_random[j],
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
