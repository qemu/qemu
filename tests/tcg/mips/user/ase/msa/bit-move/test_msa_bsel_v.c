/*
 *  Test program for MSA instruction BSEL.V
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *`
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
            3 * (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Bit Move";
    char *instruction_name =  "BSEL.V";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0xeeeeeeeeeeeeeeeeULL, 0xeeeeeeeeeeeeeeeeULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0xefcefcefcefcefceULL, 0xfcefcefcefcefcefULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },    /*  16  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xaaaaaaaaaaaaaaaaULL, 0xaaaaaaaaaaaaaaaaULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0xaa8aa8aa8aa8aa8aULL, 0xa8aa8aa8aa8aa8aaULL, },
        { 0x0820820820820820ULL, 0x8208208208208208ULL, },
        { 0x5d75d75d75d75d75ULL, 0xd75d75d75d75d75dULL, },    /*  24  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x5555555555555555ULL, 0x5555555555555555ULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x4544544544544544ULL, 0x5445445445445445ULL, },
        { 0x1451451451451451ULL, 0x4514514514514514ULL, },
        { 0xdcddcddcddcddcddULL, 0xcddcddcddcddcddcULL, },    /*  32  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x4444444444444444ULL, 0x4444444444444444ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xccccccccccccccccULL, 0xccccccccccccccccULL, },
        { 0x0c40c40c40c40c40ULL, 0xc40c40c40c40c40cULL, },
        { 0x3f73f73f73f73f73ULL, 0xf73f73f73f73f73fULL, },    /*  40  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x1111111111111111ULL, 0x1111111111111111ULL, },
        { 0x2222222222222222ULL, 0x2222222222222222ULL, },
        { 0x3333333333333333ULL, 0x3333333333333333ULL, },
        { 0x2302302302302302ULL, 0x3023023023023023ULL, },
        { 0x1031031031031031ULL, 0x0310310310310310ULL, },
        { 0xf3bf3bf3bf3bf3bfULL, 0x3bf3bf3bf3bf3bf3ULL, },    /*  48  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x4104104104104104ULL, 0x1041041041041041ULL, },
        { 0xe28e28e28e28e28eULL, 0x28e28e28e28e28e2ULL, },
        { 0x2302302302302302ULL, 0x3023023023023023ULL, },
        { 0xe38e38e38e38e38eULL, 0x38e38e38e38e38e3ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },    /*  56  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x1451451451451451ULL, 0x4514514514514514ULL, },
        { 0x0c60c60c60c60c60ULL, 0xc60c60c60c60c60cULL, },
        { 0x1031031031031031ULL, 0x0310310310310310ULL, },
        { 0x0c40c40c40c40c40ULL, 0xc40c40c40c40c40cULL, },
        { 0x1c71c71c71c71c71ULL, 0xc71c71c71c71c71cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  64  */
        { 0x882a004008024500ULL, 0x02670b1a143b100cULL, },
        { 0x884ae68c28621140ULL, 0x4b40025eea6ba004ULL, },
        { 0x006a064c08204440ULL, 0x09670958bc52b008ULL, },
        { 0xfbfe066f4db3c748ULL, 0x1bf7bb5abd7ff2fcULL, },
        { 0xfbbe00634d93c708ULL, 0x12f7bb1a153f52fcULL, },
        { 0xa81a002209838300ULL, 0x02d0821a012b0014ULL, },
        { 0x73ae00414c11c608ULL, 0x10f7b918151652e8ULL, },
        { 0x8c7aaeeab9ce4d80ULL, 0x276f4fffbe3b351cULL, },    /*  72  */
        { 0xa83a00620983c700ULL, 0x02f78b1a153b101cULL, },
        { 0xac5aaeaab9cf8b80ULL, 0x27d8c6ffab2b2514ULL, },
        { 0x204a060818018200ULL, 0x05d080d8a9022000ULL, },
        { 0x504f164d4e30604eULL, 0x89610858a842e2a0ULL, },
        { 0x700e00415c11c208ULL, 0x04f18898010242a0ULL, },
        { 0x204b160c1a21a246ULL, 0x8dd080d8a942a000ULL, },
        { 0x704f164d5e31e24eULL, 0x8df188d8a942e2a0ULL, },
        { 0x004a064c08204040ULL, 0x09610858a842a000ULL, },    /*  80  */
        { 0x000a004008004000ULL, 0x0061081800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },    /*  88  */
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x000a000008000000ULL, 0x0040001800020000ULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },    /*  96  */
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },
        { 0x886ae6cc28625540ULL, 0x4b670b5efe7bb00cULL, },
        { 0xfbfee6ef6df3d748ULL, 0x5bf7bb5eff7ff2fcULL, },
        { 0xfbfee6ef6df3d748ULL, 0x5bf7bb5eff7ff2fcULL, },
        { 0xfbfee6ef6df3d748ULL, 0x5bf7bb5eff7ff2fcULL, },
        { 0xfbfee6ef6df3d748ULL, 0x5bf7bb5eff7ff2fcULL, },
        { 0xfffeeeeffdffdfc8ULL, 0x7fffffffff7ff7fcULL, },    /* 104  */
        { 0xfffeeeeffdffdfc8ULL, 0x7fffffffff7ff7fcULL, },
        { 0xfffeeeeffdffdfc8ULL, 0x7fffffffff7ff7fcULL, },
        { 0xfffeeeeffdffdfc8ULL, 0x7fffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
        { 0xfffffeefffffffceULL, 0xffffffffff7ff7fcULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSEL_V(b128_pattern[i], b128_pattern[j],
                          b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSEL_V(b128_random[i], b128_random[j],
                          b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                       (PATTERN_INPUTS_SHORT_COUNT)) +
                                      RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSEL_V__DDT(b128_random[i], b128_random[j],
                               b128_result[
                                   ((PATTERN_INPUTS_SHORT_COUNT) *
                                    (PATTERN_INPUTS_SHORT_COUNT)) +
                                   ((RANDOM_INPUTS_SHORT_COUNT) *
                                    (RANDOM_INPUTS_SHORT_COUNT)) +
                                   RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_BSEL_V__DSD(b128_random[i], b128_random[j],
                               b128_result[
                                   ((PATTERN_INPUTS_SHORT_COUNT) *
                                    (PATTERN_INPUTS_SHORT_COUNT)) +
                                   (2 * (RANDOM_INPUTS_SHORT_COUNT) *
                                    (RANDOM_INPUTS_SHORT_COUNT)) +
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
