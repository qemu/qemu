/*
 *  Test program for MSA instruction HSUB_U.H
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
    char *group_name = "Int Subtract";
    char *instruction_name =  "HSUB_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x00ff00ff00ff00ffULL, 0x00ff00ff00ff00ffULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0071001c00c70071ULL, 0x001c00c70071001cULL, },
        { 0x008e00e30038008eULL, 0x00e30038008e00e3ULL, },
        { 0xff01ff01ff01ff01ULL, 0xff01ff01ff01ff01ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xff56ff56ff56ff56ULL, 0xff56ff56ff56ff56ULL, },
        { 0xffabffabffabffabULL, 0xffabffabffabffabULL, },
        { 0xff34ff34ff34ff34ULL, 0xff34ff34ff34ff34ULL, },
        { 0xffcdffcdffcdffcdULL, 0xffcdffcdffcdffcdULL, },
        { 0xff72ff1dffc8ff72ULL, 0xff1dffc8ff72ff1dULL, },
        { 0xff8fffe4ff39ff8fULL, 0xffe4ff39ff8fffe4ULL, },
        { 0xffabffabffabffabULL, 0xffabffabffabffabULL, },    /*  16  */
        { 0x00aa00aa00aa00aaULL, 0x00aa00aa00aa00aaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0xffdeffdeffdeffdeULL, 0xffdeffdeffdeffdeULL, },
        { 0x0077007700770077ULL, 0x0077007700770077ULL, },
        { 0x001cffc70072001cULL, 0xffc70072001cffc7ULL, },
        { 0x0039008effe30039ULL, 0x008effe30039008eULL, },
        { 0xff56ff56ff56ff56ULL, 0xff56ff56ff56ff56ULL, },    /*  24  */
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0xffabffabffabffabULL, 0xffabffabffabffabULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xff89ff89ff89ff89ULL, 0xff89ff89ff89ff89ULL, },
        { 0x0022002200220022ULL, 0x0022002200220022ULL, },
        { 0xffc7ff72001dffc7ULL, 0xff72001dffc7ff72ULL, },
        { 0xffe40039ff8effe4ULL, 0x0039ff8effe40039ULL, },
        { 0xffcdffcdffcdffcdULL, 0xffcdffcdffcdffcdULL, },    /*  32  */
        { 0x00cc00cc00cc00ccULL, 0x00cc00cc00cc00ccULL, },
        { 0x0022002200220022ULL, 0x0022002200220022ULL, },
        { 0x0077007700770077ULL, 0x0077007700770077ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0099009900990099ULL, 0x0099009900990099ULL, },
        { 0x003effe90094003eULL, 0xffe90094003effe9ULL, },
        { 0x005b00b00005005bULL, 0x00b00005005b00b0ULL, },
        { 0xff34ff34ff34ff34ULL, 0xff34ff34ff34ff34ULL, },    /*  40  */
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0xff89ff89ff89ff89ULL, 0xff89ff89ff89ff89ULL, },
        { 0xffdeffdeffdeffdeULL, 0xffdeffdeffdeffdeULL, },
        { 0xff67ff67ff67ff67ULL, 0xff67ff67ff67ff67ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffa5ff50fffbffa5ULL, 0xff50fffbffa5ff50ULL, },
        { 0xffc20017ff6cffc2ULL, 0x0017ff6cffc20017ULL, },
        { 0xffe4ff39ff8fffe4ULL, 0xff39ff8fffe4ff39ULL, },    /*  48  */
        { 0x00e30038008e00e3ULL, 0x0038008e00e30038ULL, },
        { 0x0039ff8effe40039ULL, 0xff8effe40039ff8eULL, },
        { 0x008effe30039008eULL, 0xffe30039008effe3ULL, },
        { 0x0017ff6cffc20017ULL, 0xff6cffc20017ff6cULL, },
        { 0x00b00005005b00b0ULL, 0x0005005b00b00005ULL, },
        { 0x0055ff5500560055ULL, 0xff5500560055ff55ULL, },
        { 0x0072001cffc70072ULL, 0x001cffc70072001cULL, },
        { 0xff1dffc8ff72ff1dULL, 0xffc8ff72ff1dffc8ULL, },    /*  56  */
        { 0x001c00c70071001cULL, 0x00c70071001c00c7ULL, },
        { 0xff72001dffc7ff72ULL, 0x001dffc7ff72001dULL, },
        { 0xffc70072001cffc7ULL, 0x0072001cffc70072ULL, },
        { 0xff50fffbffa5ff50ULL, 0xfffbffa5ff50fffbULL, },
        { 0xffe90094003effe9ULL, 0x0094003effe90094ULL, },
        { 0xff8effe40039ff8eULL, 0xffe40039ff8effe4ULL, },
        { 0xffab00abffaaffabULL, 0x00abffaaffab00abULL, },
        { 0x001e001affc60015ULL, 0xffe4ffad008300a4ULL, },    /*  64  */
        { 0xffca0083ff95004dULL, 0xff54fff100bfffb4ULL, },
        { 0x002e003cff59ffd5ULL, 0xff73ff0c00d3009cULL, },
        { 0x00390099fff70007ULL, 0xff5aff3300bc0010ULL, },
        { 0x0091ff34ffeb0087ULL, 0xffab005dff9a0046ULL, },
        { 0x003dff9dffba00bfULL, 0xff1b00a1ffd6ff56ULL, },
        { 0x00a1ff56ff7e0047ULL, 0xff3affbcffea003eULL, },
        { 0x00acffb3001c0079ULL, 0xff21ffe3ffd3ffb2ULL, },
        { 0x0042ffe20057004bULL, 0xffc0006800300019ULL, },    /*  72  */
        { 0xffee004b00260083ULL, 0xff3000ac006cff29ULL, },
        { 0x00520004ffea000bULL, 0xff4fffc700800011ULL, },
        { 0x005d00610088003dULL, 0xff36ffee0069ff85ULL, },
        { 0x0006ff4afffc00a2ULL, 0x0026002a002e00d6ULL, },
        { 0xffb2ffb3ffcb00daULL, 0xff96006e006affe6ULL, },
        { 0x0016ff6cff8f0062ULL, 0xffb5ff89007e00ceULL, },
        { 0x0021ffc9002d0094ULL, 0xff9cffb000670042ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_U_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_U_H(b128_random[i], b128_random[j],
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
