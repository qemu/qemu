/*
 *  Test program for MSA instruction MSUB_Q.H
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
    char *instruction_name =  "MSUB_Q.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffdfffdfffdfffdULL, 0xfffdfffdfffdfffdULL, },
        { 0xfffcfffdfffcfffcULL, 0xfffdfffcfffcfffdULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },    /*   8  */
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xfffbfffbfffbfffbULL, 0xfffbfffbfffbfffbULL, },    /*  16  */
        { 0xfffbfffbfffbfffbULL, 0xfffbfffbfffbfffbULL, },
        { 0xc716c716c716c716ULL, 0xc716c716c716c716ULL, },
        { 0xfff9fff9fff9fff9ULL, 0xfff9fff9fff9fff9ULL, },
        { 0xddd6ddd6ddd6ddd6ULL, 0xddd6ddd6ddd6ddd6ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xed0125e4b41ced01ULL, 0x25e4b41ced0125e4ULL, },
        { 0xfff7fff6fff6fff7ULL, 0xfff6fff6fff7fff6ULL, },
        { 0xfff7fff6fff6fff7ULL, 0xfff6fff6fff7fff6ULL, },    /*  24  */
        { 0xfff7fff6fff6fff7ULL, 0xfff6fff6fff7fff6ULL, },
        { 0x38da38d938d938daULL, 0x38d938d938da38d9ULL, },
        { 0xfff6fff5fff5fff6ULL, 0xfff5fff5fff6fff5ULL, },
        { 0x2218221722172218ULL, 0x2217221722182217ULL, },
        { 0xfff6fff5fff5fff6ULL, 0xfff5fff5fff6fff5ULL, },
        { 0x12ecda084bcf12ecULL, 0xda084bcf12ecda08ULL, },
        { 0xfff6fff5fff5fff6ULL, 0xfff5fff5fff6fff5ULL, },
        { 0xfff5fff4fff4fff5ULL, 0xfff4fff4fff5fff4ULL, },    /*  32  */
        { 0xfff5fff4fff4fff5ULL, 0xfff4fff4fff5fff4ULL, },
        { 0xddd2ddd1ddd1ddd2ULL, 0xddd1ddd1ddd2ddd1ULL, },
        { 0xfff4fff3fff3fff4ULL, 0xfff3fff3fff4fff3ULL, },
        { 0xeb78eb77eb77eb78ULL, 0xeb77eb77eb78eb77ULL, },
        { 0xfff3fff2fff2fff3ULL, 0xfff2fff2fff3fff2ULL, },
        { 0xf49216b3d26ef492ULL, 0x16b3d26ef49216b3ULL, },
        { 0xfff2fff1fff1fff2ULL, 0xfff1fff1fff2fff1ULL, },
        { 0xfff2fff1fff1fff2ULL, 0xfff1fff1fff2fff1ULL, },    /*  40  */
        { 0xfff2fff1fff1fff2ULL, 0xfff1fff1fff2fff1ULL, },
        { 0x2214221322132214ULL, 0x2213221322142213ULL, },
        { 0xfff2fff1fff1fff2ULL, 0xfff1fff1fff2fff1ULL, },
        { 0x146d146c146c146dULL, 0x146c146c146d146cULL, },
        { 0xfff2fff1fff1fff2ULL, 0xfff1fff1fff2fff1ULL, },
        { 0x0b52e92f2d740b52ULL, 0xe92f2d740b52e92fULL, },
        { 0xfff1fff0fff1fff1ULL, 0xfff0fff1fff1fff0ULL, },
        { 0xfff0fff0fff0fff0ULL, 0xfff0fff0fff0fff0ULL, },    /*  48  */
        { 0xfff0fff0fff0fff0ULL, 0xfff0fff0fff0fff0ULL, },
        { 0xecf925dcb414ecf9ULL, 0x25dcb414ecf925dcULL, },
        { 0xffefffefffeeffefULL, 0xffefffeeffefffefULL, },
        { 0xf48e16b0d26af48eULL, 0x16b0d26af48e16b0ULL, },
        { 0xffeeffeeffedffeeULL, 0xffeeffedffeeffeeULL, },
        { 0xf99be6a59ac8f99bULL, 0xe6a59ac8f99be6a5ULL, },
        { 0xffedffedffebffedULL, 0xffedffebffedffedULL, },
        { 0xffedffecffebffedULL, 0xffecffebffedffecULL, },    /*  56  */
        { 0xffedffecffebffedULL, 0xffecffebffedffecULL, },
        { 0x12e3d9fe4bc512e3ULL, 0xd9fe4bc512e3d9feULL, },
        { 0xffedffebffebffedULL, 0xffebffebffedffebULL, },
        { 0x0b4de9292d6e0b4dULL, 0xe9292d6e0b4de929ULL, },
        { 0xffecffeaffebffecULL, 0xffeaffebffecffeaULL, },
        { 0x063e1932650e063eULL, 0x1932650e063e1932ULL, },
        { 0xffecffe8ffebffecULL, 0xffe8ffebffecffe8ULL, },
        { 0x9032faf1f32dc724ULL, 0xd37cfee8ffe7cdf6ULL, },    /*  64  */
        { 0x8c37fb04dab3ed15ULL, 0xc8500506002701cbULL, },
        { 0x8000eb00f0d83aacULL, 0xb0d70a15ff2518f4ULL, },
        { 0xe8edef64d3204e73ULL, 0xf40714a9fe1d069aULL, },
        { 0xe4f2ef77baa67464ULL, 0xe8db1ac7fe5d3a6fULL, },
        { 0xe4cdef768ba25b09ULL, 0xe60bf5b1fad604a2ULL, },
        { 0xe204efb4b62c272fULL, 0xe023d70208eaec98ULL, },
        { 0xe5c0efa2800019f7ULL, 0xf10996de174fffa3ULL, },
        { 0x9799df9e9625678eULL, 0xd9909bed164d16ccULL, },    /*  72  */
        { 0x94d0dfdcc0af33b4ULL, 0xd3a880002461fec2ULL, },
        { 0x8000ac2c9a31c9abULL, 0xc7408000ec28f404ULL, },
        { 0xc964ba57cdd7aea3ULL, 0xeac18000b2aafc86ULL, },
        { 0x3251bebbb01fc26aULL, 0x2df18a94b1a2ea2cULL, },
        { 0x360dbea98000b532ULL, 0x3ed78000c007fd37ULL, },
        { 0x7f71ccd4b3a69a2aULL, 0x62588000868905b9ULL, },
        { 0x1ce6c8f180009346ULL, 0xfcb580008000fefbULL, },
        { 0x37e5be19a862dbafULL, 0xfea58b5e8000fe57ULL, },    /*  80  */
        { 0x39c0be4bdd7bcb85ULL, 0xfed88000953fff6aULL, },
        { 0x5f7d948aca8d9bc1ULL, 0xff3480008000ff95ULL, },
        { 0x0bb4a742f1e1847fULL, 0xfe7e80008000ff7cULL, },
        { 0x16a395c8f655d6c0ULL, 0xff618b5e8000ff29ULL, },
        { 0x1763961afc30c464ULL, 0xff788000953fffb4ULL, },
        { 0x26ab8000fa188e23ULL, 0xffa280008000ffcaULL, },
        { 0x04bd964dfe708000ULL, 0xff4e80008000ffbdULL, },
        { 0x092a817dfeeed540ULL, 0xffb68b5e8000ff93ULL, },    /*  88  */
        { 0x097881deff94c239ULL, 0xffc08000953fffd9ULL, },
        { 0x0fa88000ff5889feULL, 0xffd380008000ffe4ULL, },
        { 0x01eb964dffd38000ULL, 0xffaa80008000ffddULL, },
        { 0x03b5817dffe1d540ULL, 0xffdc8b5e8000ffc7ULL, },
        { 0x03d481defff3c239ULL, 0xffe18000953fffebULL, },
        { 0x06548000ffeb89feULL, 0xffea80008000fff1ULL, },
        { 0x00c6964dfffa8000ULL, 0xffd680008000ffedULL, },
        { 0x017e817dfffbd540ULL, 0xffee8b5e8000ffe1ULL, },    /*  96  */
        { 0x02e28000fffcf1b8ULL, 0xfff895b98000ffcdULL, },
        { 0x05938000fffdfb3aULL, 0xfffc9f298000ffadULL, },
        { 0x0ac88000fffdfe67ULL, 0xfffea7c28000ff79ULL, },
        { 0x0b238063fffefdb0ULL, 0xfffe8000953fffd0ULL, },
        { 0x0b8180c5fffffca8ULL, 0xfffe8000a6f7ffefULL, },
        { 0x0be28127fffffb2bULL, 0xfffe8000b5befffaULL, },
        { 0x0c478189fffff904ULL, 0xfffe8000c211fffdULL, },
        { 0x144c8000fffef2a8ULL, 0xfffe80009905fffdULL, },    /* 104  */
        { 0x218f8000fffce682ULL, 0xfffe80008000fffdULL, },
        { 0x377d8000fff9cf4eULL, 0xfffe80008000fffdULL, },
        { 0x5bc08000fff5a2fbULL, 0xfffe80008000fffdULL, },
        { 0x0b3f964dfffd8d66ULL, 0xfffc80008000fffcULL, },
        { 0x0160a8b7ffff8000ULL, 0xfff880008000fffbULL, },
        { 0x002bb7ecffff8000ULL, 0xfff080008000fff9ULL, },
        { 0x0005c47affff8000ULL, 0xffe180008000fff7ULL, },
};

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUB_Q_H(b128_pattern[i], b128_pattern[j],
                            b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUB_Q_H(b128_random[i], b128_random[j],
                            b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                         (PATTERN_INPUTS_SHORT_COUNT)) +
                                        RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUB_Q_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUB_Q_H__DSD(b128_random[i], b128_random[j],
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
