/*
 *  Test program for MSA instruction SRLR.B
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
    char *group_name = "Shift";
    char *instruction_name =  "SRLR.B";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x4040404040404040ULL, 0x4040404040404040ULL, },
        { 0x0808080808080808ULL, 0x0808080808080808ULL, },
        { 0x1010101010101010ULL, 0x1010101010101010ULL, },
        { 0x2020202020202020ULL, 0x2020202020202020ULL, },
        { 0x2004ff2004ff2004ULL, 0xff2004ff2004ff20ULL, },
        { 0x1080021080021080ULL, 0x0210800210800210ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x2b2b2b2b2b2b2b2bULL, 0x2b2b2b2b2b2b2b2bULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0b0b0b0b0b0b0b0bULL, 0x0b0b0b0b0b0b0b0bULL, },
        { 0x1515151515151515ULL, 0x1515151515151515ULL, },
        { 0x1503aa1503aa1503ULL, 0xaa1503aa1503aa15ULL, },
        { 0x0b55010b55010b55ULL, 0x010b55010b55010bULL, },
        { 0x0101010101010101ULL, 0x0101010101010101ULL, },    /*  24  */
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x1515151515151515ULL, 0x1515151515151515ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0505050505050505ULL, 0x0505050505050505ULL, },
        { 0x0b0b0b0b0b0b0b0bULL, 0x0b0b0b0b0b0b0b0bULL, },
        { 0x0b01550b01550b01ULL, 0x550b01550b01550bULL, },
        { 0x052b01052b01052bULL, 0x01052b01052b0105ULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },    /*  32  */
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0x0d0d0d0d0d0d0d0dULL, 0x0d0d0d0d0d0d0d0dULL, },
        { 0x1a1a1a1a1a1a1a1aULL, 0x1a1a1a1a1a1a1a1aULL, },
        { 0x1a03cc1a03cc1a03ULL, 0xcc1a03cc1a03cc1aULL, },
        { 0x0d66020d66020d66ULL, 0x020d66020d66020dULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*  40  */
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x0d0d0d0d0d0d0d0dULL, 0x0d0d0d0d0d0d0d0dULL, },
        { 0x0202020202020202ULL, 0x0202020202020202ULL, },
        { 0x0303030303030303ULL, 0x0303030303030303ULL, },
        { 0x0606060606060606ULL, 0x0606060606060606ULL, },
        { 0x0601330601330601ULL, 0x3306013306013306ULL, },
        { 0x031a00031a00031aULL, 0x00031a00031a0003ULL, },
        { 0x0201000201000201ULL, 0x0002010002010002ULL, },    /*  48  */
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x39240e39240e3924ULL, 0x0e39240e39240e39ULL, },
        { 0x0704020704020704ULL, 0x0207040207040207ULL, },
        { 0x0e09040e09040e09ULL, 0x040e09040e09040eULL, },
        { 0x1c12071c12071c12ULL, 0x071c12071c12071cULL, },
        { 0x1c02381c02381c02ULL, 0x381c02381c02381cULL, },
        { 0x0e47000e47000e47ULL, 0x000e47000e47000eULL, },
        { 0x0001020001020001ULL, 0x0200010200010200ULL, },    /*  56  */
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x071c32071c32071cULL, 0x32071c32071c3207ULL, },
        { 0x0104060104060104ULL, 0x0601040601040601ULL, },
        { 0x02070c02070c0207ULL, 0x0c02070c02070c02ULL, },
        { 0x040e19040e19040eULL, 0x19040e19040e1904ULL, },
        { 0x0402c70402c70402ULL, 0xc70402c70402c704ULL, },
        { 0x0239020239020239ULL, 0x0202390202390202ULL, },
        { 0x881b040d28190340ULL, 0x09010101040fb001ULL, },    /*  64  */
        { 0x1102e61a010c0140ULL, 0x1301011808012c01ULL, },
        { 0x091b043314010b40ULL, 0x01670001200f0601ULL, },
        { 0x8801040601311501ULL, 0x02340b5e7f1f2c0cULL, },
        { 0xfb3000064d250608ULL, 0x0202170000085210ULL, },
        { 0x1f03000c02120208ULL, 0x0502170701001510ULL, },
        { 0x1030001927011908ULL, 0x00f7030003080310ULL, },
        { 0xfb010003014a3200ULL, 0x017cbb1a0b1015fcULL, },
        { 0xac17030bb9340480ULL, 0x0502190403052501ULL, },    /*  72  */
        { 0x1601ae15061a0180ULL, 0x0a02194005000901ULL, },
        { 0x0b17032b5d021180ULL, 0x00d8030215050101ULL, },
        { 0xac01030503682302ULL, 0x016cc6ff560b0914ULL, },
        { 0x701400055e0c074eULL, 0x120211030308e20aULL, },
        { 0x0e01160a0306024eULL, 0x230211360501390aULL, },
        { 0x071400132f001c4eULL, 0x01f102021508070aULL, },
        { 0x7001000201193901ULL, 0x047988d8551139a0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRLR_B(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_SRLR_B(b128_random[i], b128_random[j],
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
