/*
 *  Test program for MSA instruction DPSUB_S.H
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
    char *group_name = "Int Dot Product";
    char *instruction_name =  "DPSUB_S.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },    /*   0  */
        { 0xfffefffefffefffeULL, 0xfffefffefffefffeULL, },
        { 0xff52ff52ff52ff52ULL, 0xff52ff52ff52ff52ULL, },
        { 0xfffcfffcfffcfffcULL, 0xfffcfffcfffcfffcULL, },
        { 0xff94ff94ff94ff94ULL, 0xff94ff94ff94ff94ULL, },
        { 0xfffafffafffafffaULL, 0xfffafffafffafffaULL, },
        { 0xff6b0015ffc0ff6bULL, 0x0015ffc0ff6b0015ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },    /*   8  */
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xfff8fff8fff8fff8ULL, 0xfff8fff8fff8fff8ULL, },
        { 0xff4cff4cff4cff4cULL, 0xff4cff4cff4cff4cULL, },    /*  16  */
        { 0xff4cff4cff4cff4cULL, 0xff4cff4cff4cff4cULL, },
        { 0xc584c584c584c584ULL, 0xc584c584c584c584ULL, },
        { 0xfea0fea0fea0fea0ULL, 0xfea0fea0fea0fea0ULL, },
        { 0xdbb0dbb0dbb0dbb0ULL, 0xdbb0dbb0dbb0dbb0ULL, },
        { 0xfdf4fdf4fdf4fdf4ULL, 0xfdf4fdf4fdf4fdf4ULL, },
        { 0xcdea0706ea78cdeaULL, 0x0706ea78cdea0706ULL, },
        { 0xfd48fd48fd48fd48ULL, 0xfd48fd48fd48fd48ULL, },
        { 0xfdf2fdf2fdf2fdf2ULL, 0xfdf2fdf2fdf2fdf2ULL, },    /*  24  */
        { 0xfdf2fdf2fdf2fdf2ULL, 0xfdf2fdf2fdf2fdf2ULL, },
        { 0x370e370e370e370eULL, 0x370e370e370e370eULL, },
        { 0xfe9cfe9cfe9cfe9cULL, 0xfe9cfe9cfe9cfe9cULL, },
        { 0x2124212421242124ULL, 0x2124212421242124ULL, },
        { 0xff46ff46ff46ff46ULL, 0xff46ff46ff46ff46ULL, },
        { 0x2ec1f64f12882ec1ULL, 0xf64f12882ec1f64fULL, },
        { 0xfff0fff0fff0fff0ULL, 0xfff0fff0fff0fff0ULL, },
        { 0xff88ff88ff88ff88ULL, 0xff88ff88ff88ff88ULL, },    /*  32  */
        { 0xff88ff88ff88ff88ULL, 0xff88ff88ff88ff88ULL, },
        { 0xdc98dc98dc98dc98ULL, 0xdc98dc98dc98dc98ULL, },
        { 0xff20ff20ff20ff20ULL, 0xff20ff20ff20ff20ULL, },
        { 0xea00ea00ea00ea00ULL, 0xea00ea00ea00ea00ULL, },
        { 0xfeb8feb8feb8feb8ULL, 0xfeb8feb8feb8feb8ULL, },
        { 0xe1ac0434f2f0e1acULL, 0x0434f2f0e1ac0434ULL, },
        { 0xfe50fe50fe50fe50ULL, 0xfe50fe50fe50fe50ULL, },
        { 0xfeb6feb6feb6feb6ULL, 0xfeb6feb6feb6feb6ULL, },    /*  40  */
        { 0xfeb6feb6feb6feb6ULL, 0xfeb6feb6feb6feb6ULL, },
        { 0x20fa20fa20fa20faULL, 0x20fa20fa20fa20faULL, },
        { 0xff1cff1cff1cff1cULL, 0xff1cff1cff1cff1cULL, },
        { 0x13d413d413d413d4ULL, 0x13d413d413d413d4ULL, },
        { 0xff82ff82ff82ff82ULL, 0xff82ff82ff82ff82ULL, },
        { 0x1bfffa210b101bffULL, 0xfa210b101bfffa21ULL, },
        { 0xffe8ffe8ffe8ffe8ULL, 0xffe8ffe8ffe8ffe8ULL, },
        { 0xff590003ffaeff59ULL, 0x0003ffaeff590003ULL, },    /*  48  */
        { 0xff590003ffaeff59ULL, 0x0003ffaeff590003ULL, },
        { 0xcf4f0915ec32cf4fULL, 0x0915ec32cf4f0915ULL, },
        { 0xfeca001eff74fecaULL, 0x001eff74feca001eULL, },
        { 0xe1be059af3ace1beULL, 0x059af3ace1be059aULL, },
        { 0xfe3b0039ff3afe3bULL, 0x0039ff3afe3b0039ULL, },
        { 0xc82ef0b0c036c82eULL, 0xf0b0c036c82ef0b0ULL, },
        { 0xfdac0054ff00fdacULL, 0x0054ff00fdac0054ULL, },
        { 0xfe390037ff38fe39ULL, 0x0037ff38fe390037ULL, },    /*  56  */
        { 0xfe390037ff38fe39ULL, 0x0037ff38fe390037ULL, },
        { 0x2d97f67912082d97ULL, 0xf67912082d97f679ULL, },
        { 0xfec6001aff70fec6ULL, 0x001aff70fec6001aULL, },
        { 0x1b6afa360ad01b6aULL, 0xfa360ad01b6afa36ULL, },
        { 0xff53fffdffa8ff53ULL, 0xfffdffa8ff53fffdULL, },
        { 0x34d10fa13e7234d1ULL, 0x0fa13e7234d10fa1ULL, },
        { 0xffe0ffe0ffe0ffe0ULL, 0xffe0ffe0ffe0ffe0ULL, },
        { 0x9bbcf2acd41cd3a7ULL, 0xc076dce3c4c3e650ULL, },    /*  64  */
        { 0xb4b806c8f1cee494ULL, 0xbecfd64ea6a80020ULL, },
        { 0x6814ecfc0fa82b6dULL, 0xc37ad92a91550ac0ULL, },
        { 0x7bdefedcee3621e3ULL, 0xeb34ed0270f105e0ULL, },
        { 0x94da12f80be832d0ULL, 0xe98de66d52d61fb0ULL, },
        { 0x83bdecafc65625dfULL, 0xe7f8d130419c055cULL, },
        { 0x994d0df1c6d40fd2ULL, 0xe3d2c1a83e00f9d2ULL, },
        { 0xafdbf02abf6b06b4ULL, 0xeb61a56034e501eeULL, },
        { 0x6337d65edd454d8dULL, 0xf00ca83c1f920c8eULL, },    /*  72  */
        { 0x78c7f7a0ddc33780ULL, 0xebe698b41bf60104ULL, },
        { 0x3d93c078c0b1c207ULL, 0xdfb58b8ff884fa1bULL, },
        { 0x468de162e424db51ULL, 0xeee27037d08b05f1ULL, },
        { 0x5a57f342c2b2d1c7ULL, 0x169c840fb0270111ULL, },
        { 0x70e5d57bbb49c8a9ULL, 0x1e2b67c7a70c092dULL, },
        { 0x79dff665debce1f3ULL, 0x2d584c6f7f131503ULL, },
        { 0x307edd58b2d7c6abULL, 0xf8ce0def507eed7fULL, },
        { 0x12d2ebaaceb9ef2dULL, 0x0f44139e1494e19bULL, },    /*  80  */
        { 0x07500cecbf88e9fcULL, 0x109a22b12d84e9f5ULL, },
        { 0xed7c0a0c9689dd79ULL, 0xfe3a2a165149ee24ULL, },
        { 0xcf880594d43cb481ULL, 0x00ba413659fef988ULL, },
        { 0xea40f026c424ed7dULL, 0x1ce42a975ba6fcf8ULL, },
        { 0xfa52e174e584e55aULL, 0x19f040936a55fe20ULL, },
        { 0xdb86fe7ec64b0603ULL, 0x13a14ea67f40fbeaULL, },
        { 0x115cd8c4cd3c05cdULL, 0x1699652699e9f314ULL, },
        { 0xf33cc884be3c10e4ULL, 0x399852dba428ee14ULL, },    /*  88  */
        { 0x0273f878eba21554ULL, 0x31ee6cb7a1dcf428ULL, },
        { 0xdaad1e38d3d148edULL, 0x27a784e6885df2c4ULL, },
        { 0x04ea0acced565727ULL, 0x33f546b6479bdaa0ULL, },
        { 0x0fe60140cf623084ULL, 0x29715ee078b0d340ULL, },
        { 0x097de88007d93f14ULL, 0x2a887b768288e2aaULL, },
        { 0xe07fb5d0025365dfULL, 0x116297ca6cdaedb8ULL, },
        { 0xc74ecab2f1b47bc3ULL, 0x1ec35e229b5ad07eULL, },
        { 0x8c4ab55e1124622cULL, 0x2e844d9c6f52bb96ULL, },    /*  96  */
        { 0x3746c0d800b436a2ULL, 0x52ee6f0548caaafeULL, },
        { 0x3412b2381dcc3c34ULL, 0x4226686a634c9036ULL, },
        { 0x44feb5ac2d2c1b48ULL, 0x1f863d063f8e6aaeULL, },
        { 0x45ced628325f1f0bULL, 0x190e4cdb56714772ULL, },
        { 0x3a43c6b04bc8259aULL, 0x17ca65193394327cULL, },
        { 0x4cabe5a01d613107ULL, 0x14467dc849f92468ULL, },
        { 0x383d0ac03df53bb8ULL, 0x1554a52945b51a80ULL, },
        { 0x352bf8744cc532afULL, 0x1f4190b4693720beULL, },    /* 104  */
        { 0x37711cdc568e2109ULL, 0x24b0770882d72146ULL, },
        { 0x21c319bc5896349eULL, 0x12b492065fe41709ULL, },
        { 0x42090ae65cb41b62ULL, 0x0416792084231302ULL, },
        { 0x226211dc497800b0ULL, 0x072cb6d850f915fcULL, },
        { 0xf5441b3a17b21910ULL, 0x0ce58de86df716f2ULL, },
        { 0xe51807761e2e171eULL, 0x10b4544095541446ULL, },
        { 0xe980e35e0a5c10acULL, 0x137085a05b4f30deULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_H(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_H(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_S_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPSUB_S_H__DSD(b128_random[i], b128_random[j],
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
