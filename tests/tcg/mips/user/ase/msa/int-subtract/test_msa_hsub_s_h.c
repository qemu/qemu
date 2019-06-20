/*
 *  Test program for MSA instruction HSUB_S.H
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
    char *instruction_name =  "HSUB_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0xffaaffaaffaaffaaULL, 0xffaaffaaffaaffaaULL, },
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0xffccffccffccffccULL, 0xffccffccffccffccULL, },
        { 0x0071001cffc70071ULL, 0x001cffc70071001cULL, },
        { 0xff8effe30038ff8eULL, 0xffe30038ff8effe3ULL, },
        { 0x0001000100010001ULL, 0x0001000100010001ULL, },    /*   8  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0056005600560056ULL, 0x0056005600560056ULL, },
        { 0xffabffabffabffabULL, 0xffabffabffabffabULL, },
        { 0x0034003400340034ULL, 0x0034003400340034ULL, },
        { 0xffcdffcdffcdffcdULL, 0xffcdffcdffcdffcdULL, },
        { 0x0072001dffc80072ULL, 0x001dffc80072001dULL, },
        { 0xff8fffe40039ff8fULL, 0xffe40039ff8fffe4ULL, },
        { 0xffabffabffabffabULL, 0xffabffabffabffabULL, },    /*  16  */
        { 0xffaaffaaffaaffaaULL, 0xffaaffaaffaaffaaULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xff55ff55ff55ff55ULL, 0xff55ff55ff55ff55ULL, },
        { 0xffdeffdeffdeffdeULL, 0xffdeffdeffdeffdeULL, },
        { 0xff77ff77ff77ff77ULL, 0xff77ff77ff77ff77ULL, },
        { 0x001cffc7ff72001cULL, 0xffc7ff72001cffc7ULL, },
        { 0xff39ff8effe3ff39ULL, 0xff8effe3ff39ff8eULL, },
        { 0x0056005600560056ULL, 0x0056005600560056ULL, },    /*  24  */
        { 0x0055005500550055ULL, 0x0055005500550055ULL, },
        { 0x00ab00ab00ab00abULL, 0x00ab00ab00ab00abULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0089008900890089ULL, 0x0089008900890089ULL, },
        { 0x0022002200220022ULL, 0x0022002200220022ULL, },
        { 0x00c70072001d00c7ULL, 0x0072001d00c70072ULL, },
        { 0xffe40039008effe4ULL, 0x0039008effe40039ULL, },
        { 0xffcdffcdffcdffcdULL, 0xffcdffcdffcdffcdULL, },    /*  32  */
        { 0xffccffccffccffccULL, 0xffccffccffccffccULL, },
        { 0x0022002200220022ULL, 0x0022002200220022ULL, },
        { 0xff77ff77ff77ff77ULL, 0xff77ff77ff77ff77ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xff99ff99ff99ff99ULL, 0xff99ff99ff99ff99ULL, },
        { 0x003effe9ff94003eULL, 0xffe9ff94003effe9ULL, },
        { 0xff5bffb00005ff5bULL, 0xffb00005ff5bffb0ULL, },
        { 0x0034003400340034ULL, 0x0034003400340034ULL, },    /*  40  */
        { 0x0033003300330033ULL, 0x0033003300330033ULL, },
        { 0x0089008900890089ULL, 0x0089008900890089ULL, },
        { 0xffdeffdeffdeffdeULL, 0xffdeffdeffdeffdeULL, },
        { 0x0067006700670067ULL, 0x0067006700670067ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x00a50050fffb00a5ULL, 0x0050fffb00a50050ULL, },
        { 0xffc20017006cffc2ULL, 0x0017006cffc20017ULL, },
        { 0xffe40039ff8fffe4ULL, 0x0039ff8fffe40039ULL, },    /*  48  */
        { 0xffe30038ff8effe3ULL, 0x0038ff8effe30038ULL, },
        { 0x0039008effe40039ULL, 0x008effe40039008eULL, },
        { 0xff8effe3ff39ff8eULL, 0xffe3ff39ff8effe3ULL, },
        { 0x0017006cffc20017ULL, 0x006cffc20017006cULL, },
        { 0xffb00005ff5bffb0ULL, 0x0005ff5bffb00005ULL, },
        { 0x00550055ff560055ULL, 0x0055ff5600550055ULL, },
        { 0xff72001cffc7ff72ULL, 0x001cffc7ff72001cULL, },
        { 0x001dffc80072001dULL, 0xffc80072001dffc8ULL, },    /*  56  */
        { 0x001cffc70071001cULL, 0xffc70071001cffc7ULL, },
        { 0x0072001d00c70072ULL, 0x001d00c70072001dULL, },
        { 0xffc7ff72001cffc7ULL, 0xff72001cffc7ff72ULL, },
        { 0x0050fffb00a50050ULL, 0xfffb00a50050fffbULL, },
        { 0xffe9ff94003effe9ULL, 0xff94003effe9ff94ULL, },
        { 0x008effe40039008eULL, 0xffe40039008effe4ULL, },
        { 0xffabffab00aaffabULL, 0xffab00aaffabffabULL, },
        { 0xff1e001affc60015ULL, 0xffe4ffadff83ffa4ULL, },    /*  64  */
        { 0xffcaff830095004dULL, 0x0054fff1ffbfffb4ULL, },
        { 0xff2e003c005900d5ULL, 0x0073000cffd3ff9cULL, },
        { 0xff39ff99fff70007ULL, 0x005a0033ffbc0010ULL, },
        { 0xff910034ffebff87ULL, 0xffabff5dff9a0046ULL, },
        { 0x003dff9d00baffbfULL, 0x001bffa1ffd60056ULL, },
        { 0xffa10056007e0047ULL, 0x003affbcffea003eULL, },
        { 0xffacffb3001cff79ULL, 0x0021ffe3ffd300b2ULL, },
        { 0xff42ffe2ff57ff4bULL, 0xffc0ff68ff300019ULL, },    /*  72  */
        { 0xffeeff4b0026ff83ULL, 0x0030ffacff6c0029ULL, },
        { 0xff520004ffea000bULL, 0x004fffc7ff800011ULL, },
        { 0xff5dff61ff88ff3dULL, 0x0036ffeeff690085ULL, },
        { 0x0006004afffcffa2ULL, 0xff26ff2aff2effd6ULL, },
        { 0x00b2ffb300cbffdaULL, 0xff96ff6eff6affe6ULL, },
        { 0x0016006c008f0062ULL, 0xffb5ff89ff7effceULL, },
        { 0x0021ffc9002dff94ULL, 0xff9cffb0ff670042ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_S_H(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_HSUB_S_H(b128_random[i], b128_random[j],
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
