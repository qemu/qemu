/*
 *  Test program for MSA instruction DPADD_U.H
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
    char *instruction_name =  "DPADD_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfc02fc02fc02fc02ULL, 0xfc02fc02fc02fc02ULL, },    /*   0  */
        { 0xfc02fc02fc02fc02ULL, 0xfc02fc02fc02fc02ULL, },
        { 0x4eae4eae4eae4eaeULL, 0x4eae4eae4eae4eaeULL, },
        { 0xf804f804f804f804ULL, 0xf804f804f804f804ULL, },
        { 0x8e6c8e6c8e6c8e6cULL, 0x8e6c8e6c8e6c8e6cULL, },
        { 0xf406f406f406f406ULL, 0xf406f406f406f406ULL, },
        { 0x63950debb9406395ULL, 0x0debb94063950debULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },    /*   8  */
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0xf008f008f008f008ULL, 0xf008f008f008f008ULL, },
        { 0x42b442b442b442b4ULL, 0x42b442b442b442b4ULL, },    /*  16  */
        { 0x42b442b442b442b4ULL, 0x42b442b442b442b4ULL, },
        { 0x247c247c247c247cULL, 0x247c247c247c247cULL, },
        { 0x9560956095609560ULL, 0x9560956095609560ULL, },
        { 0xa450a450a450a450ULL, 0xa450a450a450a450ULL, },
        { 0xe80ce80ce80ce80cULL, 0xe80ce80ce80ce80cULL, },
        { 0xdd16a3fa6b88dd16ULL, 0xa3fa6b88dd16a3faULL, },
        { 0x3ab83ab83ab83ab8ULL, 0x3ab83ab83ab83ab8ULL, },
        { 0xe40ee40ee40ee40eULL, 0xe40ee40ee40ee40eULL, },    /*  24  */
        { 0xe40ee40ee40ee40eULL, 0xe40ee40ee40ee40eULL, },
        { 0x54f254f254f254f2ULL, 0x54f254f254f254f2ULL, },
        { 0x8d648d648d648d64ULL, 0x8d648d648d648d64ULL, },
        { 0x14dc14dc14dc14dcULL, 0x14dc14dc14dc14dcULL, },
        { 0x36ba36ba36ba36baULL, 0x36ba36ba36ba36baULL, },
        { 0xb13f94b17878b13fULL, 0x94b17878b13f94b1ULL, },
        { 0xe010e010e010e010ULL, 0xe010e010e010e010ULL, },
        { 0x7678767876787678ULL, 0x7678767876787678ULL, },    /*  32  */
        { 0x7678767876787678ULL, 0x7678767876787678ULL, },
        { 0x8568856885688568ULL, 0x8568856885688568ULL, },
        { 0x0ce00ce00ce00ce0ULL, 0x0ce00ce00ce00ce0ULL, },
        { 0x5200520052005200ULL, 0x5200520052005200ULL, },
        { 0xa348a348a348a348ULL, 0xa348a348a348a348ULL, },
        { 0xc95484cc4110c954ULL, 0x84cc4110c95484ccULL, },
        { 0x39b039b039b039b0ULL, 0x39b039b039b039b0ULL, },
        { 0x9f4a9f4a9f4a9f4aULL, 0x9f4a9f4a9f4a9f4aULL, },    /*  40  */
        { 0x9f4a9f4a9f4a9f4aULL, 0x9f4a9f4a9f4a9f4aULL, },
        { 0xe306e306e306e306ULL, 0xe306e306e306e306ULL, },
        { 0x04e404e404e404e4ULL, 0x04e404e404e404e4ULL, },
        { 0x562c562c562c562cULL, 0x562c562c562c562cULL, },
        { 0x6a7e6a7e6a7e6a7eULL, 0x6a7e6a7e6a7e6a7eULL, },
        { 0xb401a2df91f0b401ULL, 0xa2df91f0b401a2dfULL, },
        { 0xd018d018d018d018ULL, 0xd018d018d018d018ULL, },
        { 0x3fa7e9fd95523fa7ULL, 0xe9fd95523fa7e9fdULL, },    /*  48  */
        { 0x3fa7e9fd95523fa7ULL, 0xe9fd95523fa7e9fdULL, },
        { 0x34b1a5eb18ce34b1ULL, 0xa5eb18ce34b1a5ebULL, },
        { 0xaf3603e25a8caf36ULL, 0x03e25a8caf3603e2ULL, },
        { 0xd542e566f854d542ULL, 0xe566f854d542e566ULL, },
        { 0x1ec51dc71fc61ec5ULL, 0x1dc71fc61ec51dc7ULL, },
        { 0x36d2f3507aca36d2ULL, 0xf3507aca36d2f350ULL, },
        { 0x8e5437ace5008e54ULL, 0x37ace5008e5437acULL, },
        { 0x1ac719c91bc81ac7ULL, 0x19c91bc81ac719c9ULL, },    /*  56  */
        { 0x1ac719c91bc81ac7ULL, 0x19c91bc81ac719c9ULL, },
        { 0x7869b087eaf87869ULL, 0xb087eaf87869b087ULL, },
        { 0xa73afbe65290a73aULL, 0xfbe65290a73afbe6ULL, },
        { 0x1796b0ca4b301796ULL, 0xb0ca4b301796b0caULL, },
        { 0x33adde03895833adULL, 0xde03895833adde03ULL, },
        { 0x8b2f225ff38e8b2fULL, 0x225ff38e8b2f225fULL, },
        { 0xc020c020c020c020ULL, 0xc020c020c020c020ULL, },
        { 0x34443154ebe4ec59ULL, 0xff8ae31df73d39b0ULL, },    /*  64  */
        { 0x084880383032306cULL, 0x6831f4b22a587de0ULL, },
        { 0x88eca4049c587e93ULL, 0xca865ad6e8ab9840ULL, },
        { 0xe522f524bdcadd1dULL, 0x54ccaffeb00f3b20ULL, },
        { 0xb926440802182130ULL, 0xbd73c193e32a7f50ULL, },
        { 0x3c436a516daabc21ULL, 0xad084cd0f46491a4ULL, },
        { 0x27b3ac0f1c2c2c2eULL, 0x802ef7580d00b12eULL, },
        { 0xd025c9d65495de4cULL, 0x729f70a02b1b9712ULL, },
        { 0x50c9eda2c0bb2c73ULL, 0xd4f4d6c4e96eb172ULL, },    /*  72  */
        { 0x3c392f606f3d9c80ULL, 0xa81a814c020ad0fcULL, },
        { 0xcf6d16889c4f27f9ULL, 0x644b18717b7cd7e5ULL, },
        { 0x3673589e07dcc9afULL, 0x451e58c9f775050fULL, },
        { 0x92a9a9be294e2839ULL, 0xcf64adf1bed9a7efULL, },
        { 0x3b1bc78561b7da57ULL, 0xc1d52739dcf48dd3ULL, },
        { 0xa221099bcd447c0dULL, 0xa2a8679158edbafdULL, },
        { 0xeb8222a8f9295b55ULL, 0xd3326611d982e681ULL, },
        { 0x9e2ec7142fc38eccULL, 0x252170b1ef468aadULL, },    /*  80  */
        { 0x5b3cced0addf038eULL, 0x4792d47b141b612dULL, },
        { 0xad78e4f4df354c2fULL, 0xcd93f2f8260072b6ULL, },
        { 0x1e3041f03b3c9d99ULL, 0xc8df44c83f16491aULL, },
        { 0x42003b965b6cf7faULL, 0x5d309124882a7c82ULL, },
        { 0x82b67598b4cfbfcbULL, 0x920afeb79da82432ULL, },
        { 0x1a0a2a0ede448d00ULL, 0xb0b8797422bf2d4eULL, },
        { 0x288031e03ccc097aULL, 0xbee01b9c6a6f85c8ULL, },
        { 0x72c0106694442af7ULL, 0x50aa560d08f0ea98ULL, },    /*  88  */
        { 0x710637d8e7d45355ULL, 0xfa50963144a8cb2cULL, },
        { 0xbf0eecaa3a2faae6ULL, 0x63e63b048e4cebf3ULL, },
        { 0x16f03414587a870eULL, 0x72f35dbcffa25349ULL, },
        { 0x860072bc94eeb761ULL, 0xf61ea6c34a7a8fc5ULL, },
        { 0x0962bb704a1c48aaULL, 0x245c33d36e927f7fULL, },
        { 0x31e284ea963ac4c2ULL, 0x77782d72d0929bc6ULL, },
        { 0x8d10d6a4d868ace6ULL, 0x29fba58a7f86a05cULL, },
        { 0xde98199821f81f82ULL, 0x9afbdf4d3dea12acULL, },    /*  96  */
        { 0x9378a92e86104a4dULL, 0x2d160528eade271cULL, },
        { 0x134065aca120761fULL, 0x431f140f3db4433cULL, },
        { 0x37d8497ac688a50dULL, 0x63391a6dd0b6741cULL, },
        { 0x0e1578a8502e25b8ULL, 0xa12e387d0e90b4d4ULL, },
        { 0x2b65b9a082a8483bULL, 0xd8e26e173326bf2cULL, },
        { 0xa084f7800a3a820bULL, 0xc220c0c740af27aaULL, },
        { 0x9f5c29002e8ae771ULL, 0xeea4613d7100db80ULL, },
        { 0x2a8844debf5e9d5eULL, 0x9d46e906bc7b0527ULL, },    /* 104  */
        { 0x769006829567219dULL, 0xf041a3364eb808ecULL, },
        { 0xf87860ea545d8208ULL, 0x4ba95712a1ba1c84ULL, },
        { 0xc9483d8edc44cc9eULL, 0xe5aeac4a2c832ae0ULL, },
        { 0x37706d823a10b0daULL, 0x079d461a6b55dbf4ULL, },
        { 0x72109dfa526c8ea6ULL, 0x9f45813ac7e235caULL, },
        { 0xa8e0f6aa85343e96ULL, 0x37cdf6b28585e2d4ULL, },
        { 0x37803ef0bffea306ULL, 0x17150f92ff9c2ed8ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_H(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_H(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPADD_U_H__DSD(b128_random[i], b128_random[j],
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
