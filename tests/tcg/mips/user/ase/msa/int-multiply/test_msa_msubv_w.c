/*
 *  Test program for MSA instruction MSUBV.W
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
    char *group_name = "Int Multiply";
    char *instruction_name =  "MSUBV.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaa9aaaaaaa9ULL, 0xaaaaaaa9aaaaaaa9ULL, },
        { 0xfffffffefffffffeULL, 0xfffffffefffffffeULL, },
        { 0xcccccccacccccccaULL, 0xcccccccacccccccaULL, },
        { 0xfffffffdfffffffdULL, 0xfffffffdfffffffdULL, },
        { 0xe38e38e08e38e38bULL, 0x38e38e35e38e38e0ULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },    /*   8  */
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xfffffffcfffffffcULL, 0xfffffffcfffffffcULL, },
        { 0xaaaaaaa6aaaaaaa6ULL, 0xaaaaaaa6aaaaaaa6ULL, },    /*  16  */
        { 0xaaaaaaa6aaaaaaa6ULL, 0xaaaaaaa6aaaaaaa6ULL, },
        { 0xc71c71c2c71c71c2ULL, 0xc71c71c2c71c71c2ULL, },
        { 0x5555555055555550ULL, 0x5555555055555550ULL, },
        { 0xddddddd8ddddddd8ULL, 0xddddddd8ddddddd8ULL, },
        { 0xfffffffafffffffaULL, 0xfffffffafffffffaULL, },
        { 0xed097b3c5ed097aeULL, 0x7b425ecaed097b3cULL, },
        { 0xaaaaaaa4aaaaaaa4ULL, 0xaaaaaaa4aaaaaaa4ULL, },
        { 0xfffffff9fffffff9ULL, 0xfffffff9fffffff9ULL, },    /*  24  */
        { 0xfffffff9fffffff9ULL, 0xfffffff9fffffff9ULL, },
        { 0x8e38e3878e38e387ULL, 0x8e38e3878e38e387ULL, },
        { 0x5555554e5555554eULL, 0x5555554e5555554eULL, },
        { 0x9999999299999992ULL, 0x9999999299999992ULL, },
        { 0xaaaaaaa3aaaaaaa3ULL, 0xaaaaaaa3aaaaaaa3ULL, },
        { 0xa12f6844da12f67dULL, 0x684bda0ba12f6844ULL, },
        { 0xfffffff8fffffff8ULL, 0xfffffff8fffffff8ULL, },
        { 0xccccccc4ccccccc4ULL, 0xccccccc4ccccccc4ULL, },    /*  32  */
        { 0xccccccc4ccccccc4ULL, 0xccccccc4ccccccc4ULL, },
        { 0x5555554c5555554cULL, 0x5555554c5555554cULL, },
        { 0x9999999099999990ULL, 0x9999999099999990ULL, },
        { 0x70a3d70070a3d700ULL, 0x70a3d70070a3d700ULL, },
        { 0x6666665c6666665cULL, 0x6666665c6666665cULL, },
        { 0x82d82d783e93e934ULL, 0xc71c71bc82d82d78ULL, },
        { 0x3333332833333328ULL, 0x3333332833333328ULL, },
        { 0x6666665b6666665bULL, 0x6666665b6666665bULL, },    /*  40  */
        { 0x6666665b6666665bULL, 0x6666665b6666665bULL, },
        { 0x8888887d8888887dULL, 0x8888887d8888887dULL, },
        { 0x9999998e9999998eULL, 0x9999998e9999998eULL, },
        { 0x8f5c28ea8f5c28eaULL, 0x8f5c28ea8f5c28eaULL, },
        { 0xccccccc1ccccccc1ULL, 0xccccccc1ccccccc1ULL, },
        { 0x93e93e8882d82d77ULL, 0xa4fa4f9993e93e88ULL, },
        { 0xfffffff4fffffff4ULL, 0xfffffff4fffffff4ULL, },
        { 0xe38e38d78e38e382ULL, 0x38e38e2ce38e38d7ULL, },    /*  48  */
        { 0xe38e38d78e38e382ULL, 0x38e38e2ce38e38d7ULL, },
        { 0xd097b419ed097b36ULL, 0xb425ecfcd097b419ULL, },
        { 0xc71c71ba1c71c710ULL, 0x71c71c64c71c71baULL, },
        { 0xe38e38d6f49f49e8ULL, 0xd27d27c4e38e38d6ULL, },
        { 0xaaaaaa9daaaaaa9eULL, 0xaaaaaa9caaaaaa9dULL, },
        { 0xf0329154ca4587daULL, 0xa4587e5cf0329154ULL, },
        { 0x8e38e38038e38e2cULL, 0xe38e38d48e38e380ULL, },
        { 0xaaaaaa9caaaaaa9dULL, 0xaaaaaa9baaaaaa9cULL, },    /*  56  */
        { 0xaaaaaa9caaaaaa9dULL, 0xaaaaaa9baaaaaa9cULL, },
        { 0x684bda04f684bd93ULL, 0xda12f675684bda04ULL, },
        { 0xc71c71b81c71c70eULL, 0x71c71c62c71c71b8ULL, },
        { 0x7777776811111102ULL, 0xddddddce77777768ULL, },
        { 0xe38e38d48e38e37fULL, 0x38e38e29e38e38d4ULL, },
        { 0x81948b00fcd6e9d1ULL, 0x781948a181948b00ULL, },
        { 0xfffffff0fffffff0ULL, 0xfffffff0fffffff0ULL, },
        { 0x4efccd609e9c6ff0ULL, 0xc5dac96c8b677f60ULL, },    /*  64  */
        { 0x3e3d8c7cb78505f0ULL, 0x4463f7e01c4e5b90ULL, },
        { 0xcaa9a104f350a5f0ULL, 0x8ca4f13ec42edea0ULL, },
        { 0x19b8ada804d82c70ULL, 0xb62b69ee365e3f20ULL, },
        { 0x08f96cc41dc0c270ULL, 0x34b49862c7451b50ULL, },
        { 0x5405467b1fd35230ULL, 0xf7c999be7c56b340ULL, },
        { 0x5cc7babd61667630ULL, 0xa4601ed86811cb90ULL, },
        { 0xe20c1af64022c1c0ULL, 0x927a70e878437610ULL, },
        { 0x6e782f7e7bee61c0ULL, 0xdabb6a462023f920ULL, },    /*  72  */
        { 0x773aa3c0bd8185c0ULL, 0x8751ef600bdf1170ULL, },
        { 0xc0871adcd87d45c0ULL, 0x6c527d5fd9c847e0ULL, },
        { 0xd7c7f5ba4e99c4c0ULL, 0xdaa41e3704ed7360ULL, },
        { 0x26d7025e60214b40ULL, 0x042a96e7771cd3e0ULL, },
        { 0xac1b62973edd96d0ULL, 0xf244e8f7874e7e60ULL, },
        { 0xc35c3d75b4fa15d0ULL, 0x609689cfb273a9e0ULL, },
        { 0x9de4ea4c0310460cULL, 0x80c0538fcf54c5e0ULL, },
        { 0xbd81edbc2724c70cULL, 0x7301800d7cb17f60ULL, },    /*  80  */
        { 0xaebafe086f603aacULL, 0x35c5ffbbaa8b5ce0ULL, },
        { 0xdf14dcb8c25380acULL, 0x3ef9a276f99bbb60ULL, },
        { 0x5e0ea9600c5e7444ULL, 0x8ef3dee6ce1bdf60ULL, },
        { 0x1c7370e0761ecf44ULL, 0x864a2472681b66e0ULL, },
        { 0xb58eca4059fe7924ULL, 0x8c252ade750e6260ULL, },
        { 0xfcc4fbc036df5b24ULL, 0x36a7c3bc9596d2e0ULL, },
        { 0x57a2c300a677ce2cULL, 0x2922bd1cb36946e0ULL, },
        { 0x88bd5f007437a72cULL, 0x45fd18d49c1ff460ULL, },    /*  88  */
        { 0x2581a200554339ccULL, 0x6c99b74cacc4a5e0ULL, },
        { 0x2d500e000b508fccULL, 0x1f975a9844ce5060ULL, },
        { 0x5907d8000bc6a7a4ULL, 0x0eaa2a5808275460ULL, },
        { 0xeab7b80057ab4aa4ULL, 0x8af4d608d22d5fe0ULL, },
        { 0x95ab9000431f7984ULL, 0x840741386cad3f60ULL, },
        { 0xf5ddf000f6ac0b84ULL, 0xd51bfa701fdb6be0ULL, },
        { 0xdf7cc0003fd2014cULL, 0xb5052bf0d1bc3fe0ULL, },
        { 0x3393c00031cb724cULL, 0x06abb9d0a05f4160ULL, },    /*  96  */
        { 0xdb56c00090e3a34cULL, 0x7ff18f70f5d630e0ULL, },
        { 0xa1b5c0005faa944cULL, 0x9e0514507291e660ULL, },
        { 0xfa60c0002cb0454cULL, 0xc4182ef0d53919e0ULL, },
        { 0xa6f680001aac06ecULL, 0x05ca1a90e899e160ULL, },
        { 0x15a3000096665b8cULL, 0x0cec37f0504f46e0ULL, },
        { 0xb79a0000a4a5ab2cULL, 0x578239900c71c260ULL, },
        { 0xb70c000031251dccULL, 0xaa4c30f0a693abe0ULL, },
        { 0x01140000f37473ccULL, 0x400dd1e0cc92de60ULL, },    /* 104  */
        { 0xb1cc0000f066c9ccULL, 0x8cf683c01cd59ee0ULL, },
        { 0xf8540000557c1fccULL, 0x0f82c780ac8ed560ULL, },
        { 0xf88c0000103475ccULL, 0xa1f10f00956f49e0ULL, },
        { 0x2e7000005c2e79a4ULL, 0xcf94670004e95de0ULL, },
        { 0x96c00000be3ea1acULL, 0xdca57f00da6ef1e0ULL, },
        { 0xbf00000062838744ULL, 0x368a570027d005e0ULL, },
        { 0x4c0000006502488cULL, 0xcc98ef003cdc99e0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_W(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_W(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUBV_W__DSD(b128_random[i], b128_random[j],
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
