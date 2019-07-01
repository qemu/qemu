/*
 *  Test program for MSA instruction DPADD_U.D
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
    char *instruction_name =  "DPADD_U.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffffffc00000002ULL, 0xfffffffc00000002ULL, },    /*   0  */
        { 0xfffffffc00000002ULL, 0xfffffffc00000002ULL, },
        { 0x5555554eaaaaaaaeULL, 0x5555554eaaaaaaaeULL, },
        { 0xfffffff800000004ULL, 0xfffffff800000004ULL, },
        { 0x9999998e6666666cULL, 0x9999998e6666666cULL, },
        { 0xfffffff400000006ULL, 0xfffffff400000006ULL, },
        { 0x71c71c638e38e395ULL, 0x1c71c70de38e38ebULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },    /*   8  */
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0xfffffff000000008ULL, 0xfffffff000000008ULL, },
        { 0x55555542aaaaaab4ULL, 0x55555542aaaaaab4ULL, },    /*  16  */
        { 0x55555542aaaaaab4ULL, 0x55555542aaaaaab4ULL, },
        { 0x38e38e2471c71c7cULL, 0x38e38e2471c71c7cULL, },
        { 0xaaaaaa9555555560ULL, 0xaaaaaa9555555560ULL, },
        { 0xbbbbbba444444450ULL, 0xbbbbbba444444450ULL, },
        { 0xffffffe80000000cULL, 0xffffffe80000000cULL, },
        { 0xf684bd87b425ed16ULL, 0xbda12f4e97b425faULL, },
        { 0x5555553aaaaaaab8ULL, 0x5555553aaaaaaab8ULL, },
        { 0xffffffe40000000eULL, 0xffffffe40000000eULL, },    /*  24  */
        { 0xffffffe40000000eULL, 0xffffffe40000000eULL, },
        { 0x71c71c54e38e38f2ULL, 0x71c71c54e38e38f2ULL, },
        { 0xaaaaaa8d55555564ULL, 0xaaaaaa8d55555564ULL, },
        { 0x33333314ccccccdcULL, 0x33333314ccccccdcULL, },
        { 0x55555536aaaaaabaULL, 0x55555536aaaaaabaULL, },
        { 0xd097b40684bda13fULL, 0xb425ece9f684bdb1ULL, },
        { 0xffffffe000000010ULL, 0xffffffe000000010ULL, },
        { 0x9999997666666678ULL, 0x9999997666666678ULL, },    /*  32  */
        { 0x9999997666666678ULL, 0x9999997666666678ULL, },
        { 0xaaaaaa8555555568ULL, 0xaaaaaa8555555568ULL, },
        { 0x3333330ccccccce0ULL, 0x3333330ccccccce0ULL, },
        { 0x7ae147851eb85200ULL, 0x7ae147851eb85200ULL, },
        { 0xcccccca333333348ULL, 0xcccccca333333348ULL, },
        { 0xf49f49c93e93e954ULL, 0xb05b0584b60b60ccULL, },
        { 0x66666639999999b0ULL, 0x66666639999999b0ULL, },
        { 0xcccccc9f3333334aULL, 0xcccccc9f3333334aULL, },    /*  40  */
        { 0xcccccc9f3333334aULL, 0xcccccc9f3333334aULL, },
        { 0x111110e2eeeeef06ULL, 0x111110e2eeeeef06ULL, },
        { 0x33333304cccccce4ULL, 0x33333304cccccce4ULL, },
        { 0x851eb822e147ae2cULL, 0x851eb822e147ae2cULL, },
        { 0x9999996a6666667eULL, 0x9999996a6666667eULL, },
        { 0xe38e38b3e93e9401ULL, 0xd27d27a2c71c71dfULL, },
        { 0xffffffd000000018ULL, 0xffffffd000000018ULL, },
        { 0x71c71c3f8e38e3a7ULL, 0x1c71c6e9e38e38fdULL, },    /*  48  */
        { 0x71c71c3f8e38e3a7ULL, 0x1c71c6e9e38e38fdULL, },
        { 0x684bd9df425ed0b1ULL, 0xda12f6507b425eebULL, },
        { 0xe38e38af1c71c736ULL, 0x38e38e03c71c71e2ULL, },
        { 0x0b60b5d527d27d42ULL, 0x1c71c6e549f49f66ULL, },
        { 0x5555551eaaaaaac5ULL, 0x5555551daaaaaac7ULL, },
        { 0x6e9e061a4587e6d2ULL, 0x2c3f35816b74f050ULL, },
        { 0xc71c718e38e38e54ULL, 0x71c71c378e38e3acULL, },
        { 0x5555551aaaaaaac7ULL, 0x55555519aaaaaac9ULL, },    /*  56  */
        { 0x5555551aaaaaaac7ULL, 0x55555519aaaaaac9ULL, },
        { 0xb425eccda12f6869ULL, 0xed097b05bda12f87ULL, },
        { 0xe38e38a71c71c73aULL, 0x38e38dfbc71c71e6ULL, },
        { 0x5555551777777796ULL, 0xeeeeeeb0aaaaaacaULL, },
        { 0x71c71c338e38e3adULL, 0x1c71c6dde38e3903ULL, },
        { 0xca4587a781948b2fULL, 0x61f9ad9406522c5fULL, },
        { 0xffffffc000000020ULL, 0xffffffc000000020ULL, },
        { 0x4f10a2061266c2b0ULL, 0x132f36fdaebdb734ULL, },    /*  64  */
        { 0xe173955d0a3d6d94ULL, 0x2de485b19f4dac90ULL, },
        { 0x5a9b88364205b90cULL, 0xe3c89435af2c3022ULL, },
        { 0xa5506be1e16f25e8ULL, 0xb5d99e2c137656f2ULL, },
        { 0x37b35f38d945d0ccULL, 0xd08eece004064c4eULL, },
        { 0x46c3bc088c276755ULL, 0xd3ba26318bdfb302ULL, },
        { 0x288f407241d1cf13ULL, 0xe4e2d49bf38e1598ULL, },
        { 0xb38b871fddd1234aULL, 0xfd7386eef5421908ULL, },
        { 0x2cb379f915996ec2ULL, 0xb357957305209c9aULL, },    /*  72  */
        { 0x0e7efe62cb43d680ULL, 0xc48043dd6cceff30ULL, },
        { 0x0966991866fb9f64ULL, 0x3d26b2ddb9e53ac1ULL, },
        { 0x9961eeb6d99e4586ULL, 0xc46ae4f9206e6e69ULL, },
        { 0xe416d2627907b262ULL, 0x967beeef84b89539ULL, },
        { 0x6f13191015070699ULL, 0xaf0ca142866c98a9ULL, },
        { 0xff0e6eae87a9acbbULL, 0x3650d35decf5cc51ULL, },
        { 0x52fc668a5f0acfa8ULL, 0xf4ee28afafeae691ULL, },
        { 0x8e335693216733a0ULL, 0xebf294e7e1b7da9fULL, },    /*  80  */
        { 0x242889888a96ab79ULL, 0x1029e138e123d999ULL, },
        { 0xa117d2200713df49ULL, 0xa936d669733f9d55ULL, },
        { 0xea5eaf7c9d524d27ULL, 0x533cccdee6d6ad0dULL, },
        { 0x8014252a44e6c8b7ULL, 0x5139a5a2ff917d2dULL, },
        { 0x12e82535692eaeadULL, 0x6c74742f3b1a47edULL, },
        { 0x6bfad303a455af5fULL, 0xa4da8c7753e03c42ULL, },
        { 0xd7d1673544f2b638ULL, 0x37b76789ca48e5eaULL, },
        { 0x55b32da89b1ab874ULL, 0x1136a063291c7430ULL, },    /*  88  */
        { 0xd8fa08f2c6e9500cULL, 0x15e6a0cfa25fce7eULL, },
        { 0xfb6ec0cb14ee46c0ULL, 0x85e0ab776ca06e87ULL, },
        { 0x7170744f4e43c44fULL, 0x17ee0476d6f5954fULL, },
        { 0xba3c379c6c72bc03ULL, 0xf4a9e78f41249a57ULL, },
        { 0x923c97db1bf9726fULL, 0x0c32ba5fa7655f81ULL, },
        { 0x08ff0c9a1b07a05dULL, 0x7e05b61db39e9936ULL, },
        { 0x16e37ad7ce0b9d05ULL, 0x3aa86333e7ca176eULL, },
        { 0x4396d885c2a89499ULL, 0x3259d55cbbd56e50ULL, },    /*  96  */
        { 0x86505184e2848fd5ULL, 0xfbe6ef6acb48e5d8ULL, },
        { 0xf19ecbd2f0d9cb45ULL, 0x102d8886fc3ba2e4ULL, },
        { 0x985e99073ad19cddULL, 0x0fae6c4a600fe8c8ULL, },
        { 0x40076fc7eafc7c7aULL, 0x18d0edce69b82b2cULL, },
        { 0xc633d71b8943703fULL, 0x236de461c55a6368ULL, },
        { 0xb2b44afd6be31aa8ULL, 0x366f22bc07569aa2ULL, },
        { 0x832148e5fdab87bfULL, 0x3b138b90c7099132ULL, },
        { 0x9388b611f0bd2a51ULL, 0xc95a7ba92714878aULL, },    /* 104  */
        { 0xa598b2d7184dc31bULL, 0x02d31201c0d1f3a9ULL, },
        { 0x26b9d9c7d27ede61ULL, 0x84305afc61d71edcULL, },
        { 0xd994c5da2b819a07ULL, 0xda2ed7517c38dd10ULL, },
        { 0x490b25198d55f4bbULL, 0xa54a7d332b34db68ULL, },
        { 0x9d17b063519fea3aULL, 0x1d81a65b0c1f8770ULL, },
        { 0x000b355286100badULL, 0x35e1e113d0b4c238ULL, },
        { 0x316423fb99a16a0dULL, 0xddbffc10af9e9540ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_D(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_D(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_D__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPADD_U_D__DSD(b128_random[i], b128_random[j],
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
