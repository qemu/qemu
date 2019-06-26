/*
 *  Test program for MSA instruction DPSUB_U.H
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
    char *instruction_name =  "DPSUB_U.H";
    int32_t ret;
    uint32_t i, j;
    struct timeval start, end;
    double elapsed_time;

    uint64_t b128_result[TEST_COUNT_TOTAL][2];
    uint64_t b128_expect[TEST_COUNT_TOTAL][2] = {
        { 0x03fe03fe03fe03feULL, 0x03fe03fe03fe03feULL, },    /*   0  */
        { 0x03fe03fe03fe03feULL, 0x03fe03fe03fe03feULL, },
        { 0xb152b152b152b152ULL, 0xb152b152b152b152ULL, },
        { 0x07fc07fc07fc07fcULL, 0x07fc07fc07fc07fcULL, },
        { 0x7194719471947194ULL, 0x7194719471947194ULL, },
        { 0x0bfa0bfa0bfa0bfaULL, 0x0bfa0bfa0bfa0bfaULL, },
        { 0x9c6bf21546c09c6bULL, 0xf21546c09c6bf215ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },    /*   8  */
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0x0ff80ff80ff80ff8ULL, 0x0ff80ff80ff80ff8ULL, },
        { 0xbd4cbd4cbd4cbd4cULL, 0xbd4cbd4cbd4cbd4cULL, },    /*  16  */
        { 0xbd4cbd4cbd4cbd4cULL, 0xbd4cbd4cbd4cbd4cULL, },
        { 0xdb84db84db84db84ULL, 0xdb84db84db84db84ULL, },
        { 0x6aa06aa06aa06aa0ULL, 0x6aa06aa06aa06aa0ULL, },
        { 0x5bb05bb05bb05bb0ULL, 0x5bb05bb05bb05bb0ULL, },
        { 0x17f417f417f417f4ULL, 0x17f417f417f417f4ULL, },
        { 0x22ea5c06947822eaULL, 0x5c06947822ea5c06ULL, },
        { 0xc548c548c548c548ULL, 0xc548c548c548c548ULL, },
        { 0x1bf21bf21bf21bf2ULL, 0x1bf21bf21bf21bf2ULL, },    /*  24  */
        { 0x1bf21bf21bf21bf2ULL, 0x1bf21bf21bf21bf2ULL, },
        { 0xab0eab0eab0eab0eULL, 0xab0eab0eab0eab0eULL, },
        { 0x729c729c729c729cULL, 0x729c729c729c729cULL, },
        { 0xeb24eb24eb24eb24ULL, 0xeb24eb24eb24eb24ULL, },
        { 0xc946c946c946c946ULL, 0xc946c946c946c946ULL, },
        { 0x4ec16b4f87884ec1ULL, 0x6b4f87884ec16b4fULL, },
        { 0x1ff01ff01ff01ff0ULL, 0x1ff01ff01ff01ff0ULL, },
        { 0x8988898889888988ULL, 0x8988898889888988ULL, },    /*  32  */
        { 0x8988898889888988ULL, 0x8988898889888988ULL, },
        { 0x7a987a987a987a98ULL, 0x7a987a987a987a98ULL, },
        { 0xf320f320f320f320ULL, 0xf320f320f320f320ULL, },
        { 0xae00ae00ae00ae00ULL, 0xae00ae00ae00ae00ULL, },
        { 0x5cb85cb85cb85cb8ULL, 0x5cb85cb85cb85cb8ULL, },
        { 0x36ac7b34bef036acULL, 0x7b34bef036ac7b34ULL, },
        { 0xc650c650c650c650ULL, 0xc650c650c650c650ULL, },
        { 0x60b660b660b660b6ULL, 0x60b660b660b660b6ULL, },    /*  40  */
        { 0x60b660b660b660b6ULL, 0x60b660b660b660b6ULL, },
        { 0x1cfa1cfa1cfa1cfaULL, 0x1cfa1cfa1cfa1cfaULL, },
        { 0xfb1cfb1cfb1cfb1cULL, 0xfb1cfb1cfb1cfb1cULL, },
        { 0xa9d4a9d4a9d4a9d4ULL, 0xa9d4a9d4a9d4a9d4ULL, },
        { 0x9582958295829582ULL, 0x9582958295829582ULL, },
        { 0x4bff5d216e104bffULL, 0x5d216e104bff5d21ULL, },
        { 0x2fe82fe82fe82fe8ULL, 0x2fe82fe82fe82fe8ULL, },
        { 0xc05916036aaec059ULL, 0x16036aaec0591603ULL, },    /*  48  */
        { 0xc05916036aaec059ULL, 0x16036aaec0591603ULL, },
        { 0xcb4f5a15e732cb4fULL, 0x5a15e732cb4f5a15ULL, },
        { 0x50cafc1ea57450caULL, 0xfc1ea57450cafc1eULL, },
        { 0x2abe1a9a07ac2abeULL, 0x1a9a07ac2abe1a9aULL, },
        { 0xe13be239e03ae13bULL, 0xe239e03ae13be239ULL, },
        { 0xc92e0cb08536c92eULL, 0x0cb08536c92e0cb0ULL, },
        { 0x71acc8541b0071acULL, 0xc8541b0071acc854ULL, },
        { 0xe539e637e438e539ULL, 0xe637e438e539e637ULL, },    /*  56  */
        { 0xe539e637e438e539ULL, 0xe637e438e539e637ULL, },
        { 0x87974f7915088797ULL, 0x4f79150887974f79ULL, },
        { 0x58c6041aad7058c6ULL, 0x041aad7058c6041aULL, },
        { 0xe86a4f36b4d0e86aULL, 0x4f36b4d0e86a4f36ULL, },
        { 0xcc5321fd76a8cc53ULL, 0x21fd76a8cc5321fdULL, },
        { 0x74d1dda10c7274d1ULL, 0xdda10c7274d1dda1ULL, },
        { 0x3fe03fe03fe03fe0ULL, 0x3fe03fe03fe03fe0ULL, },
        { 0xcbbcceac141c13a7ULL, 0x00761ce308c3c650ULL, },    /*  64  */
        { 0xf7b87fc8cfcecf94ULL, 0x97cf0b4ed5a88220ULL, },
        { 0x77145bfc63a8816dULL, 0x357aa52a175567c0ULL, },
        { 0x1ade0adc423622e3ULL, 0xab3450024ff1c4e0ULL, },
        { 0x46dabbf8fde8ded0ULL, 0x428d3e6d1cd680b0ULL, },
        { 0xc3bd95af925643dfULL, 0x52f8b3300b9c6e5cULL, },
        { 0xd84d53f1e3d4d3d2ULL, 0x7fd208a8f3004ed2ULL, },
        { 0x2fdb362aab6b21b4ULL, 0x8d618f60d4e568eeULL, },
        { 0xaf37125e3f45d38dULL, 0x2b0c293c16924e8eULL, },    /*  72  */
        { 0xc3c7d0a090c36380ULL, 0x57e67eb4fdf62f04ULL, },
        { 0x3093e97863b1d807ULL, 0x9bb5e78f8484281bULL, },
        { 0xc98da762f8243651ULL, 0xbae2a737088bfaf1ULL, },
        { 0x6d575642d6b2d7c7ULL, 0x309c520f41275811ULL, },
        { 0xc4e5387b9e4925a9ULL, 0x3e2bd8c7230c722dULL, },
        { 0x5ddff66532bc83f3ULL, 0x5d58986fa7134503ULL, },
        { 0x147edd5806d7a4abULL, 0x2cce99ef267e197fULL, },
        { 0xd5b2d0aab3994377ULL, 0xcd083b9ac440025bULL, },    /*  80  */
        { 0x80bf8eec25e70baaULL, 0xb6e600dda46ca823ULL, },
        { 0xe79991b05061b0b1ULL, 0xd91c24ba24bc8d1fULL, },
        { 0x5352504a2070df63ULL, 0x473b74aadc80fd45ULL, },
        { 0x0546cd72f0907c98ULL, 0x1ab13142c4b84c19ULL, },
        { 0xcc6ba15c55b01774ULL, 0x6e1606c3875c1b25ULL, },
        { 0x1dbdf6d689f3d0f7ULL, 0x4ac43fe21dbb145aULL, },
        { 0xd6baa1542922ce15ULL, 0x697e5fbada60ca72ULL, },
        { 0x1806cdbe15b6846fULL, 0x18091759d3f43a3aULL, },    /*  88  */
        { 0xfc0a8444a6e31a5bULL, 0x0daafd828699ee8eULL, },
        { 0x4f36fd647760debdULL, 0x7c3fb8561364c110ULL, },
        { 0x1bfcc992394ee12bULL, 0xfca40e06ed110caeULL, },
        { 0xa54ca0a4128a8bb6ULL, 0x70d40b38f9c0fc46ULL, },
        { 0xcb1d6138bde219f9ULL, 0x9c68fd7fb61366a6ULL, },
        { 0x3887fa1a7e8f8fe6ULL, 0x2ce4bb5039504af0ULL, },
        { 0xf65edccc34eccb94ULL, 0x3e041478ff0f739cULL, },
        { 0x4cc27494d274632dULL, 0x2a3ee78cfad81d3cULL, },    /*  96  */
        { 0xd40e966c853c370eULL, 0x04feaa379b04067cULL, },
        { 0x5da2b998597c214bULL, 0x9da08eb7ff4efc8cULL, },
        { 0xe9269a421c1c0396ULL, 0x2f41456bdcd248bcULL, },
        { 0xe87f80bc039cfc91ULL, 0xed3c08269718789cULL, },
        { 0xa6c53808a9213425ULL, 0xa2aefe7284cdb89cULL, },
        { 0x71cd34f063590a91ULL, 0xef6839544786e41cULL, },
        { 0x6adcd8201277fe43ULL, 0x7a42072920b97f84ULL, },
        { 0xd64c3010a53c52d9ULL, 0x2ffcd8e8ec4662d9ULL, },    /* 104  */
        { 0x2bcc04d0fd7bb9d3ULL, 0x54334ac042e043bbULL, },
        { 0xc73077f8e331ebe0ULL, 0x1c5f5244f12a2b70ULL, },
        { 0x309c82661787fc47ULL, 0xc7f3cf1c49211c79ULL, },
        { 0xeb78588cf53e082dULL, 0x75954984106eb821ULL, },
        { 0x5fa026e08f6af367ULL, 0xa8dfb35ce9820111ULL, },
        { 0x04b0e03c469efd7fULL, 0x7a6806a42e2df58fULL, },
        { 0xcca0baf00eacf773ULL, 0xd54e79140435c3e5ULL, },
    };

    reset_msa_registers();

    gettimeofday(&start, NULL);

    for (i = 0; i < PATTERN_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < PATTERN_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_H(b128_pattern[i], b128_pattern[j],
                             b128_result[PATTERN_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_H(b128_random[i], b128_random[j],
                             b128_result[((PATTERN_INPUTS_SHORT_COUNT) *
                                          (PATTERN_INPUTS_SHORT_COUNT)) +
                                         RANDOM_INPUTS_SHORT_COUNT * i + j]);
        }
    }

    for (i = 0; i < RANDOM_INPUTS_SHORT_COUNT; i++) {
        for (j = 0; j < RANDOM_INPUTS_SHORT_COUNT; j++) {
            do_msa_DPSUB_U_H__DDT(b128_random[i], b128_random[j],
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
            do_msa_DPSUB_U_H__DSD(b128_random[i], b128_random[j],
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
