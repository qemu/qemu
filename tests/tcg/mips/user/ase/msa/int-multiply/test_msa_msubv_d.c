/*
 *  Test program for MSA instruction MSUBV.D
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
    char *instruction_name =  "MSUBV.D";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },    /*   0  */
        { 0xffffffffffffffffULL, 0xffffffffffffffffULL, },
        { 0xaaaaaaaaaaaaaaa9ULL, 0xaaaaaaaaaaaaaaa9ULL, },
        { 0xfffffffffffffffeULL, 0xfffffffffffffffeULL, },
        { 0xcccccccccccccccaULL, 0xcccccccccccccccaULL, },
        { 0xfffffffffffffffdULL, 0xfffffffffffffffdULL, },
        { 0xe38e38e38e38e38bULL, 0x38e38e38e38e38e0ULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },    /*   8  */
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xfffffffffffffffcULL, 0xfffffffffffffffcULL, },
        { 0xaaaaaaaaaaaaaaa6ULL, 0xaaaaaaaaaaaaaaa6ULL, },    /*  16  */
        { 0xaaaaaaaaaaaaaaa6ULL, 0xaaaaaaaaaaaaaaa6ULL, },
        { 0x71c71c71c71c71c2ULL, 0x71c71c71c71c71c2ULL, },
        { 0x5555555555555550ULL, 0x5555555555555550ULL, },
        { 0xddddddddddddddd8ULL, 0xddddddddddddddd8ULL, },
        { 0xfffffffffffffffaULL, 0xfffffffffffffffaULL, },
        { 0xed097b425ed097aeULL, 0xd097b425ed097b3cULL, },
        { 0xaaaaaaaaaaaaaaa4ULL, 0xaaaaaaaaaaaaaaa4ULL, },
        { 0xfffffffffffffff9ULL, 0xfffffffffffffff9ULL, },    /*  24  */
        { 0xfffffffffffffff9ULL, 0xfffffffffffffff9ULL, },
        { 0xe38e38e38e38e387ULL, 0xe38e38e38e38e387ULL, },
        { 0x555555555555554eULL, 0x555555555555554eULL, },
        { 0x9999999999999992ULL, 0x9999999999999992ULL, },
        { 0xaaaaaaaaaaaaaaa3ULL, 0xaaaaaaaaaaaaaaa3ULL, },
        { 0xa12f684bda12f67dULL, 0x12f684bda12f6844ULL, },
        { 0xfffffffffffffff8ULL, 0xfffffffffffffff8ULL, },
        { 0xccccccccccccccc4ULL, 0xccccccccccccccc4ULL, },    /*  32  */
        { 0xccccccccccccccc4ULL, 0xccccccccccccccc4ULL, },
        { 0x555555555555554cULL, 0x555555555555554cULL, },
        { 0x9999999999999990ULL, 0x9999999999999990ULL, },
        { 0xa3d70a3d70a3d700ULL, 0xa3d70a3d70a3d700ULL, },
        { 0x666666666666665cULL, 0x666666666666665cULL, },
        { 0xe93e93e93e93e934ULL, 0x2d82d82d82d82d78ULL, },
        { 0x3333333333333328ULL, 0x3333333333333328ULL, },
        { 0x666666666666665bULL, 0x666666666666665bULL, },    /*  40  */
        { 0x666666666666665bULL, 0x666666666666665bULL, },
        { 0x888888888888887dULL, 0x888888888888887dULL, },
        { 0x999999999999998eULL, 0x999999999999998eULL, },
        { 0x5c28f5c28f5c28eaULL, 0x5c28f5c28f5c28eaULL, },
        { 0xccccccccccccccc1ULL, 0xccccccccccccccc1ULL, },
        { 0x2d82d82d82d82d77ULL, 0x3e93e93e93e93e88ULL, },
        { 0xfffffffffffffff4ULL, 0xfffffffffffffff4ULL, },
        { 0xe38e38e38e38e382ULL, 0x38e38e38e38e38d7ULL, },    /*  48  */
        { 0xe38e38e38e38e382ULL, 0x38e38e38e38e38d7ULL, },
        { 0xd097b425ed097b36ULL, 0x097b425ed097b419ULL, },
        { 0xc71c71c71c71c710ULL, 0x71c71c71c71c71baULL, },
        { 0x49f49f49f49f49e8ULL, 0x38e38e38e38e38d6ULL, },
        { 0xaaaaaaaaaaaaaa9eULL, 0xaaaaaaaaaaaaaa9dULL, },
        { 0xf9add3c0ca4587daULL, 0x587e6b74f0329154ULL, },
        { 0x8e38e38e38e38e2cULL, 0xe38e38e38e38e380ULL, },
        { 0xaaaaaaaaaaaaaa9dULL, 0xaaaaaaaaaaaaaa9cULL, },    /*  56  */
        { 0xaaaaaaaaaaaaaa9dULL, 0xaaaaaaaaaaaaaa9cULL, },
        { 0x684bda12f684bd93ULL, 0x84bda12f684bda04ULL, },
        { 0xc71c71c71c71c70eULL, 0x71c71c71c71c71b8ULL, },
        { 0x1111111111111102ULL, 0x7777777777777768ULL, },
        { 0xe38e38e38e38e37fULL, 0x38e38e38e38e38d4ULL, },
        { 0x781948b0fcd6e9d1ULL, 0xc3f35ba781948b00ULL, },
        { 0xfffffffffffffff0ULL, 0xfffffffffffffff0ULL, },
        { 0x52ba41969e9c6ff0ULL, 0xcd6802158b677f60ULL, },    /*  64  */
        { 0x63129bf5b78505f0ULL, 0x1556f7f61c4e5b90ULL, },
        { 0x5a4c8855f350a5f0ULL, 0x6a36586fc42edea0ULL, },
        { 0x5e6b001b04d82c70ULL, 0xe819332c365e3f20ULL, },
        { 0x6ec35a7a1dc0c270ULL, 0x3008290cc7451b50ULL, },
        { 0x37152f411fd35230ULL, 0xc7e3b2957c56b340ULL, },
        { 0xcc49f1d861667630ULL, 0x1808e0646811cb90ULL, },
        { 0xde8a7f544022c1c0ULL, 0x9886bc9978437610ULL, },
        { 0xd5c46bb47bee61c0ULL, 0xed661d132023f920ULL, },    /*  72  */
        { 0x6af92e4bbd8185c0ULL, 0x3d8b4ae20bdf1170ULL, },
        { 0xe4d44869d87d45c0ULL, 0x6409d23bd9c847e0ULL, },
        { 0x6e2e9ce94e99c4c0ULL, 0xc30837db04ed7360ULL, },
        { 0x724d14ae60214b40ULL, 0x40eb1297771cd3e0ULL, },
        { 0x848da22a3edd96d0ULL, 0xc168eecc874e7e60ULL, },
        { 0x0de7f6a9b4fa15d0ULL, 0x2067546bb273a9e0ULL, },
        { 0xc233bfd40310460cULL, 0x0d9585bacf54c5e0ULL, },
        { 0x061015122724c70cULL, 0x0169d01f7cb17f60ULL, },    /*  80  */
        { 0x23dacc726f603aacULL, 0xf3ea8c4eaa8b5ce0ULL, },
        { 0xd82df953c25380acULL, 0xba87b7f0f99bbb60ULL, },
        { 0x546cb94a0c5e7444ULL, 0x3818c320ce1bdf60ULL, },
        { 0xa38f9428761ecf44ULL, 0x63113b9e681b66e0ULL, },
        { 0x7dc23fbe59fe7924ULL, 0x156ddd68750e6260ULL, },
        { 0x8a17717d36df5b24ULL, 0x36b1f5939596d2e0ULL, },
        { 0x7e854cd9a677ce2cULL, 0xf2b6202eb36946e0ULL, },
        { 0x246d8d067437a72cULL, 0x04c6347e9c1ff460ULL, },    /*  88  */
        { 0xc48a013a554339ccULL, 0xcb81fd31acc4a5e0ULL, },
        { 0xb971282c0b508fccULL, 0x20d62d6344ce5060ULL, },
        { 0x835f812f0bc6a7a4ULL, 0x17bd6b5a08275460ULL, },
        { 0xc0ee1b9557ab4aa4ULL, 0x170471a9d22d5fe0ULL, },
        { 0xc6f66d89431f7984ULL, 0x5c6f5a646cad3f60ULL, },
        { 0x5ae0b289f6ac0b84ULL, 0x6f9f6bc81fdb6be0ULL, },
        { 0x2f584ee03fd2014cULL, 0xa7e34ccbd1bc3fe0ULL, },
        { 0x5947927731cb724cULL, 0xf76af1f9a05f4160ULL, },    /*  96  */
        { 0x68112ad490e3a34cULL, 0x7f944a22f5d630e0ULL, },
        { 0x1cf6705c5faa944cULL, 0x801292d47291e660ULL, },
        { 0x5519f2782cb0454cULL, 0x3d691c2dd53919e0ULL, },
        { 0xe5c979861aac06ecULL, 0x585247d6e899e160ULL, },
        { 0x2450b27896665b8cULL, 0x8276d8ad504f46e0ULL, },
        { 0x2716d456a4a5ab2cULL, 0x46e1f3460c71c260ULL, },
        { 0x5751460331251dccULL, 0xdc1dc7a4a693abe0ULL, },
        { 0x3bf387b7f37473ccULL, 0x8efb4ff7cc92de60ULL, },    /* 104  */
        { 0xc3103a3df066c9ccULL, 0x7d3b07351cd59ee0ULL, },
        { 0x0d612554557c1fccULL, 0x5dbabfc2ac8ed560ULL, },
        { 0x1cd018ef103475ccULL, 0xca277277956f49e0ULL, },
        { 0x15d520225c2e79a4ULL, 0x08f2025804e95de0ULL, },
        { 0x820f9c65be3ea1acULL, 0x37094edbda6ef1e0ULL, },
        { 0x0f18515c62838744ULL, 0xcfbd4b5627d005e0ULL, },
        { 0x11d549f26502488cULL, 0x8de999d53cdc99e0ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_D(b128_pattern[i], b128_pattern[j],
                           b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_D(b128_random[i], b128_random[j],
                           b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                        (PATTERN_INPUTS_SHORT_COUNT)) +
                                       RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_MSUBV_D__DDT(b128_random[i], b128_random[j],
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
            do_msa_MSUBV_D__DSD(b128_random[i], b128_random[j],
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
