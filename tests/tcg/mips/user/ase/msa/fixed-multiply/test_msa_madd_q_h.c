/*
 *  Test program for MSA instruction MADD_Q.H
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
            3 * (RANDOM_INPUTS_SHORT_COUNT) * (RANDOM_INPUTS_SHORT_COUNT))


int32_t main(void)
{
    char *isa_ase_name = "MSA";
    char *group_name = "Fixed Multiply";
    char *instruction_name =  "MADD_Q.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },    /*   0  */
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0x0000000000000000ULL, 0x0000000000000000ULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xfffefffdfffefffeULL, 0xfffdfffefffefffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },    /*   8  */
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },    /*  16  */
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0x38e138e138e138e1ULL, 0x38e138e138e138e1ULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0x221f221f221f221fULL, 0x221f221f221f221fULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0x12f2da0f4bd712f2ULL, 0xda0f4bd712f2da0fULL, },
        { 0xfffbfffcfffcfffbULL, 0xfffcfffcfffbfffcULL, },
        { 0xfffafffbfffbfffaULL, 0xfffbfffbfffafffbULL, },    /*  24  */
        { 0xfffafffbfffbfffaULL, 0xfffbfffbfffafffbULL, },
        { 0xc716c717c717c716ULL, 0xc717c717c716c717ULL, },
        { 0xfff9fffafffafff9ULL, 0xfffafffafff9fffaULL, },
        { 0xddd6ddd7ddd7ddd6ULL, 0xddd7ddd7ddd6ddd7ULL, },
        { 0xfff7fff8fff8fff7ULL, 0xfff8fff8fff7fff8ULL, },
        { 0xed0025e4b41ded00ULL, 0x25e4b41ded0025e4ULL, },
        { 0xfff5fff6fff6fff5ULL, 0xfff6fff6fff5fff6ULL, },
        { 0xfff5fff6fff6fff5ULL, 0xfff6fff6fff5fff6ULL, },    /*  32  */
        { 0xfff5fff6fff6fff5ULL, 0xfff6fff6fff5fff6ULL, },
        { 0x2217221822182217ULL, 0x2218221822172218ULL, },
        { 0xfff4fff5fff5fff4ULL, 0xfff5fff5fff4fff5ULL, },
        { 0x146f14701470146fULL, 0x14701470146f1470ULL, },
        { 0xfff3fff4fff4fff3ULL, 0xfff4fff4fff3fff4ULL, },
        { 0x0b53e9322d770b53ULL, 0xe9322d770b53e932ULL, },
        { 0xfff2fff3fff3fff2ULL, 0xfff3fff3fff2fff3ULL, },
        { 0xfff1fff2fff2fff1ULL, 0xfff2fff2fff1fff2ULL, },    /*  40  */
        { 0xfff1fff2fff2fff1ULL, 0xfff2fff2fff1fff2ULL, },
        { 0xddceddcfddcfddceULL, 0xddcfddcfddceddcfULL, },
        { 0xffeffff0fff0ffefULL, 0xfff0fff0ffeffff0ULL, },
        { 0xeb73eb74eb74eb73ULL, 0xeb74eb74eb73eb74ULL, },
        { 0xffedffeeffeeffedULL, 0xffeeffeeffedffeeULL, },
        { 0xf48c16afd26af48cULL, 0x16afd26af48c16afULL, },
        { 0xffecffedffecffecULL, 0xffedffecffecffedULL, },
        { 0xffecffecffecffecULL, 0xffecffecffecffecULL, },    /*  48  */
        { 0xffecffecffecffecULL, 0xffecffecffecffecULL, },
        { 0x12e2d9ff4bc712e2ULL, 0xd9ff4bc712e2d9ffULL, },
        { 0xffebffebffecffebULL, 0xffebffecffebffebULL, },
        { 0x0b4be9292d6f0b4bULL, 0xe9292d6f0b4be929ULL, },
        { 0xffeaffeaffebffeaULL, 0xffeaffebffeaffeaULL, },
        { 0x063c1932650f063cULL, 0x1932650f063c1932ULL, },
        { 0xffe9ffe9ffebffe9ULL, 0xffe9ffebffe9ffe9ULL, },
        { 0xffe8ffe9ffeaffe8ULL, 0xffe9ffeaffe8ffe9ULL, },    /*  56  */
        { 0xffe8ffe9ffeaffe8ULL, 0xffe9ffeaffe8ffe9ULL, },
        { 0xecf125d6b40fecf1ULL, 0x25d6b40fecf125d6ULL, },
        { 0xffe6ffe8ffe8ffe6ULL, 0xffe8ffe8ffe6ffe8ULL, },
        { 0xf48516a9d264f485ULL, 0x16a9d264f48516a9ULL, },
        { 0xffe5ffe7ffe6ffe5ULL, 0xffe7ffe6ffe5ffe7ULL, },
        { 0xf992e69e9ac2f992ULL, 0xe69e9ac2f992e69eULL, },
        { 0xffe3ffe7ffe4ffe3ULL, 0xffe7ffe4ffe3ffe7ULL, },
        { 0x6f9c04dd0ca138aaULL, 0x2c5200e6ffe731d8ULL, },    /*  64  */
        { 0x739604c9251a12b8ULL, 0x377dfac7ffa6fe02ULL, },
        { 0x7fff14cc0ef4c520ULL, 0x4ef5f5b700a7e6d8ULL, },
        { 0x171110672cabb158ULL, 0x0bc4eb2201aef931ULL, },
        { 0x1b0b105345248b66ULL, 0x16efe503016dc55bULL, },
        { 0x1b2f10537427a4c0ULL, 0x19be0a1804f3fb27ULL, },
        { 0x1df71014499cd899ULL, 0x1fa528c6f6de1330ULL, },
        { 0x1a3a10257fffe5d0ULL, 0x0ebe68e9e8780024ULL, },
        { 0x6860202869d99838ULL, 0x263663d9e979e8faULL, },    /*  72  */
        { 0x6b281fe93f4ecc11ULL, 0x2c1d7fffdb640103ULL, },
        { 0x7fff539865cb3619ULL, 0x38847fff139c0bc0ULL, },
        { 0x369a456c32245120ULL, 0x15027fff4d19033dULL, },
        { 0xcdac41074fdb3d58ULL, 0xd1d1756a4e201596ULL, },
        { 0xc9ef41187fff4a8fULL, 0xc0ea7fff3fba028aULL, },
        { 0x808a32ec4c586596ULL, 0x9d687fff7937fa07ULL, },
        { 0xe31436ce7fff6c79ULL, 0x030a7fff7fff00c4ULL, },
        { 0xfe192c037fff7fffULL, 0x04d47fff7e7a0049ULL, },    /*  80  */
        { 0xfe292c257fff4707ULL, 0x058b3b197fff0078ULL, },
        { 0xff5c101739ce0661ULL, 0x074420c72b2a009aULL, },
        { 0xfecc12e4645704e6ULL, 0x00ca02430de90076ULL, },
        { 0xffeb0f2b7fff0829ULL, 0x014002760dbe002cULL, },
        { 0xffeb0f367fff0487ULL, 0x016f012210050048ULL, },
        { 0xfff8058b39ce0068ULL, 0x01e100a00567005cULL, },
        { 0xfff006826457004fULL, 0x0034000b01bd0046ULL, },
        { 0xfffe05397fff0083ULL, 0x0052000b01b7001aULL, },    /*  88  */
        { 0xfffe053d7fff0048ULL, 0x005e000501ff002aULL, },
        { 0xffff01e839ce0006ULL, 0x007b000200ac0036ULL, },
        { 0xfffe023d64570004ULL, 0x000d000000370029ULL, },
        { 0xffff01cc7fff0006ULL, 0x001400000036000fULL, },
        { 0xffff01cd7fff0003ULL, 0x00160000003e0018ULL, },
        { 0xffff00a839ce0000ULL, 0x001c00000014001eULL, },
        { 0xfffe00c564570000ULL, 0x0003000000060017ULL, },
        { 0xffff009e7fff0000ULL, 0x0004000000050008ULL, },    /*  96  */
        { 0xffff007e7fff0000ULL, 0x0006000000040003ULL, },
        { 0xffff00657fff0000ULL, 0x0009000000030001ULL, },
        { 0xffff00517fff0000ULL, 0x000e000000020000ULL, },
        { 0xffff00517fff0000ULL, 0x0010000000020000ULL, },
        { 0xffff00517fff0000ULL, 0x0012000000020000ULL, },
        { 0xffff00517fff0000ULL, 0x0014000000020000ULL, },
        { 0xffff00517fff0000ULL, 0x0016000000020000ULL, },
        { 0xffff001d39ce0000ULL, 0x001c000000000000ULL, },    /* 104  */
        { 0xffff000a1a1b0000ULL, 0x0024000000000000ULL, },
        { 0xffff00030bca0000ULL, 0x002f000000000000ULL, },
        { 0xffff000105530000ULL, 0x003d000000000000ULL, },
        { 0xfffe0001093d0000ULL, 0x0006000000000000ULL, },
        { 0xfffc000110090000ULL, 0x0000000000000000ULL, },
        { 0xfff800011bd50000ULL, 0x0000000000000000ULL, },
        { 0xfff0000130500000ULL, 0x0000000000000000ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADD_Q_H(b128_pattern[i], b128_pattern[j],
                            b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADD_Q_H(b128_random[i], b128_random[j],
                            b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                         (PATTERN_INPUTS_SHORT_COUNT)) +
                                        RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MADD_Q_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_MADD_Q_H__DSD(b128_random[i], b128_random[j],
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
