/*
 *  Test program for MSA instruction DPADD_U.W
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
    char *instruction_name =  "DPADD_U.W";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0xfffc0002fffc0002ULL, 0xfffc0002fffc0002ULL, },    /*   0  */
        { 0xfffc0002fffc0002ULL, 0xfffc0002fffc0002ULL, },
        { 0x554eaaae554eaaaeULL, 0x554eaaae554eaaaeULL, },
        { 0xfff80004fff80004ULL, 0xfff80004fff80004ULL, },
        { 0x998e666c998e666cULL, 0x998e666c998e666cULL, },
        { 0xfff40006fff40006ULL, 0xfff40006fff40006ULL, },
        { 0x1c63e39571b88e40ULL, 0xc70e38eb1c63e395ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },    /*   8  */
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0xfff00008fff00008ULL, 0xfff00008fff00008ULL, },
        { 0x5542aab45542aab4ULL, 0x5542aab45542aab4ULL, },    /*  16  */
        { 0x5542aab45542aab4ULL, 0x5542aab45542aab4ULL, },
        { 0x38cf1c7c38cf1c7cULL, 0x38cf1c7c38cf1c7cULL, },
        { 0xaa955560aa955560ULL, 0xaa955560aa955560ULL, },
        { 0xbba44450bba44450ULL, 0xbba44450bba44450ULL, },
        { 0xffe8000cffe8000cULL, 0xffe8000cffe8000cULL, },
        { 0xbd87ed16f66b0988ULL, 0x84a425fabd87ed16ULL, },
        { 0x553aaab8553aaab8ULL, 0x553aaab8553aaab8ULL, },
        { 0xffe4000effe4000eULL, 0xffe4000effe4000eULL, },    /*  24  */
        { 0xffe4000effe4000eULL, 0xffe4000effe4000eULL, },
        { 0x71aa38f271aa38f2ULL, 0x71aa38f271aa38f2ULL, },
        { 0xaa8d5564aa8d5564ULL, 0xaa8d5564aa8d5564ULL, },
        { 0x3314ccdc3314ccdcULL, 0x3314ccdc3314ccdcULL, },
        { 0x5536aaba5536aabaULL, 0x5536aaba5536aabaULL, },
        { 0xb406a13fd0782f78ULL, 0x9794bdb1b406a13fULL, },
        { 0xffe00010ffe00010ULL, 0xffe00010ffe00010ULL, },
        { 0x9976667899766678ULL, 0x9976667899766678ULL, },    /*  32  */
        { 0x9976667899766678ULL, 0x9976667899766678ULL, },
        { 0xaa855568aa855568ULL, 0xaa855568aa855568ULL, },
        { 0x330ccce0330ccce0ULL, 0x330ccce0330ccce0ULL, },
        { 0x7ab852007ab85200ULL, 0x7ab852007ab85200ULL, },
        { 0xcca33348cca33348ULL, 0xcca33348cca33348ULL, },
        { 0xb02fe954f473a510ULL, 0x6beb60ccb02fe954ULL, },
        { 0x663999b0663999b0ULL, 0x663999b0663999b0ULL, },
        { 0xcc9f334acc9f334aULL, 0xcc9f334acc9f334aULL, },    /*  40  */
        { 0xcc9f334acc9f334aULL, 0xcc9f334acc9f334aULL, },
        { 0x10e2ef0610e2ef06ULL, 0x10e2ef0610e2ef06ULL, },
        { 0x3304cce43304cce4ULL, 0x3304cce43304cce4ULL, },
        { 0x84efae2c84efae2cULL, 0x84efae2c84efae2cULL, },
        { 0x996a667e996a667eULL, 0x996a667e996a667eULL, },
        { 0xd24d9401e35e82f0ULL, 0xc13c71dfd24d9401ULL, },
        { 0xffd00018ffd00018ULL, 0xffd00018ffd00018ULL, },
        { 0x1c3fe3a771948e52ULL, 0xc6ea38fd1c3fe3a7ULL, },    /*  48  */
        { 0x1c3fe3a771948e52ULL, 0xc6ea38fd1c3fe3a7ULL, },
        { 0xd9dfd0b1681797ceULL, 0x4ba65eebd9dfd0b1ULL, },
        { 0x38afc736e3591c8cULL, 0x8e0471e238afc736ULL, },
        { 0x1c3c7d420b298e54ULL, 0x2d4c9f661c3c7d42ULL, },
        { 0x551faac5551daac6ULL, 0x551eaac7551faac5ULL, },
        { 0x2c08e6d26e64f9caULL, 0xb0c4f0502c08e6d2ULL, },
        { 0x718f8e54c6e23900ULL, 0x1c38e3ac718f8e54ULL, },
        { 0x551baac75519aac8ULL, 0x551aaac9551baac7ULL, },    /*  56  */
        { 0x551baac75519aac8ULL, 0x551aaac9551baac7ULL, },
        { 0xecce6869b3e94bf8ULL, 0x25b12f87ecce6869ULL, },
        { 0x38a7c73ae3511c90ULL, 0x8dfc71e638a7c73aULL, },
        { 0xeeb1779655171130ULL, 0x884aaacaeeb17796ULL, },
        { 0x1c33e3ad71888e58ULL, 0xc6de39031c33e3adULL, },
        { 0x61ba8b2fca05cd8eULL, 0x32522c5f61ba8b2fULL, },
        { 0xffc00020ffc00020ULL, 0xffc00020ffc00020ULL, },
        { 0x1883fe94228255a4ULL, 0x1676ba1575c8cfc9ULL, },    /*  64  */
        { 0x9f026c24710669eaULL, 0x245b8a02c3f8aadeULL, },
        { 0x985184e0bcca4328ULL, 0x38ede08c879f0f77ULL, },
        { 0xe844f0f21702736aULL, 0x68d01ed3cbb87dadULL, },
        { 0x6ec35e82658687b0ULL, 0x76b4eec019e858c2ULL, },
        { 0x6651a5cf17c5ba59ULL, 0x00db97b536922653ULL, },
        { 0x10115a59bc888b36ULL, 0x953fb40350cbb498ULL, },
        { 0x7e8ac9c2890512c9ULL, 0x03c7477aa84e1b56ULL, },
        { 0x77d9e27ed4c8ec07ULL, 0x18599e046bf47fefULL, },    /*  72  */
        { 0x21999708798bbce4ULL, 0xacbdba52862e0e34ULL, },
        { 0x0cce2f904c6cd245ULL, 0x4da0b293fdff50fdULL, },
        { 0x67a1e4780c1be5e4ULL, 0xce178c138ffda993ULL, },
        { 0xb795508a66541626ULL, 0xfdf9ca5ad41717c9ULL, },
        { 0x260ebff332d09db9ULL, 0x6c815dd12b997e87ULL, },
        { 0x80e274dbf27fb158ULL, 0xecf83751bd97d71dULL, },
        { 0xb4190065dd35867dULL, 0x84d1ca72f61ef021ULL, },
        { 0x146be93b2ce39d07ULL, 0xb4edb1658fe8e617ULL, },    /*  80  */
        { 0x28da2b76b4930398ULL, 0x43fbb752e67034d3ULL, },
        { 0x6202107639989575ULL, 0xdd1056c8882a591fULL, },
        { 0x8e704692d2e83f33ULL, 0x8605bb9831163f53ULL, },
        { 0x19f6294a0938f7c3ULL, 0xb5d3886b8d6db0c9ULL, },
        { 0x338d977ccca46e03ULL, 0x26ffd0ded278d778ULL, },
        { 0xbd9d53669d1f0d1fULL, 0xcf6d52287e678700ULL, },
        { 0x18106087e287df80ULL, 0x6e5a3285497c7c8eULL, },
        { 0x7be90cbb50b10f2eULL, 0x91193a91e83049caULL, },    /*  88  */
        { 0xf5c762fa74f1dd41ULL, 0xc6a6d96a1360b472ULL, },
        { 0xdec724f4426380a0ULL, 0x8e924c103a77a87aULL, },
        { 0x43bb09c1cc850053ULL, 0x06479b02f6444a68ULL, },
        { 0x709d98fbece3b6fdULL, 0x0f02ef4f1e3d11f4ULL, },
        { 0xdf964592c2f0673eULL, 0xbf06914326915827ULL, },
        { 0xa595174288afc04eULL, 0x4dac2c104d1f338eULL, },
        { 0xf0400b1764f99f91ULL, 0x904ab47cadc0214cULL, },
        { 0x7a4505ebaa0a3823ULL, 0xc2ce09ca715dec1cULL, },    /*  96  */
        { 0xc0c227c1d78e87b7ULL, 0xfc9e0ad8846cfb1bULL, },
        { 0x4b501be126c0ecd3ULL, 0x47813bbab4be1843ULL, },
        { 0x8c94284d7bbb0613ULL, 0x5f37b7ed7918a6b1ULL, },
        { 0x16e12feca5f2470cULL, 0xecb24110b92e33d5ULL, },
        { 0x2d734e2e0f77e762ULL, 0x2dc8706ed959cbd3ULL, },
        { 0x5a430652c80bfcc7ULL, 0x835871922d75cf6eULL, },
        { 0xb30826c2c930c150ULL, 0xe0148a4e74790481ULL, },
        { 0x46021066c48e3720ULL, 0x6e76bee0c30066e8ULL, },    /* 104  */
        { 0x80543cd67141b3f2ULL, 0x14074d905449ba08ULL, },
        { 0x003ba47a25839f81ULL, 0x536fe6e8a79655ebULL, },
        { 0x709b823c97a86aeeULL, 0x13e9a6a824155b79ULL, },
        { 0xad5a661d2dfbd29aULL, 0x780997c18cea8383ULL, },
        { 0x024c799cf912e891ULL, 0x0bb620125e8129b7ULL, },
        { 0x0de66afc224e0f31ULL, 0x23590398c1ea5059ULL, },
        { 0x1d512ac23c5b270dULL, 0x38de17a18940924dULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_W(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_W(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPADD_U_W__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPADD_U_W__DSD(b128_random[i], b128_random[j],
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
