/*
 * Xilinx eFuse/bbram CRC calculator
 *
 * Copyright (c) 2021 Xilinx Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "qemu/osdep.h"
#include "hw/nvram/xlnx-efuse.h"

static uint32_t xlnx_efuse_u37_crc(uint32_t prev_crc, uint32_t data,
                                   uint32_t addr)
{
    /* A table for 7-bit slicing */
    static const uint32_t crc_tab[128] = {
        0x00000000, 0xe13b70f7, 0xc79a971f, 0x26a1e7e8,
        0x8ad958cf, 0x6be22838, 0x4d43cfd0, 0xac78bf27,
        0x105ec76f, 0xf165b798, 0xd7c45070, 0x36ff2087,
        0x9a879fa0, 0x7bbcef57, 0x5d1d08bf, 0xbc267848,
        0x20bd8ede, 0xc186fe29, 0xe72719c1, 0x061c6936,
        0xaa64d611, 0x4b5fa6e6, 0x6dfe410e, 0x8cc531f9,
        0x30e349b1, 0xd1d83946, 0xf779deae, 0x1642ae59,
        0xba3a117e, 0x5b016189, 0x7da08661, 0x9c9bf696,
        0x417b1dbc, 0xa0406d4b, 0x86e18aa3, 0x67dafa54,
        0xcba24573, 0x2a993584, 0x0c38d26c, 0xed03a29b,
        0x5125dad3, 0xb01eaa24, 0x96bf4dcc, 0x77843d3b,
        0xdbfc821c, 0x3ac7f2eb, 0x1c661503, 0xfd5d65f4,
        0x61c69362, 0x80fde395, 0xa65c047d, 0x4767748a,
        0xeb1fcbad, 0x0a24bb5a, 0x2c855cb2, 0xcdbe2c45,
        0x7198540d, 0x90a324fa, 0xb602c312, 0x5739b3e5,
        0xfb410cc2, 0x1a7a7c35, 0x3cdb9bdd, 0xdde0eb2a,
        0x82f63b78, 0x63cd4b8f, 0x456cac67, 0xa457dc90,
        0x082f63b7, 0xe9141340, 0xcfb5f4a8, 0x2e8e845f,
        0x92a8fc17, 0x73938ce0, 0x55326b08, 0xb4091bff,
        0x1871a4d8, 0xf94ad42f, 0xdfeb33c7, 0x3ed04330,
        0xa24bb5a6, 0x4370c551, 0x65d122b9, 0x84ea524e,
        0x2892ed69, 0xc9a99d9e, 0xef087a76, 0x0e330a81,
        0xb21572c9, 0x532e023e, 0x758fe5d6, 0x94b49521,
        0x38cc2a06, 0xd9f75af1, 0xff56bd19, 0x1e6dcdee,
        0xc38d26c4, 0x22b65633, 0x0417b1db, 0xe52cc12c,
        0x49547e0b, 0xa86f0efc, 0x8ecee914, 0x6ff599e3,
        0xd3d3e1ab, 0x32e8915c, 0x144976b4, 0xf5720643,
        0x590ab964, 0xb831c993, 0x9e902e7b, 0x7fab5e8c,
        0xe330a81a, 0x020bd8ed, 0x24aa3f05, 0xc5914ff2,
        0x69e9f0d5, 0x88d28022, 0xae7367ca, 0x4f48173d,
        0xf36e6f75, 0x12551f82, 0x34f4f86a, 0xd5cf889d,
        0x79b737ba, 0x988c474d, 0xbe2da0a5, 0x5f16d052
    };

    /*
     * eFuse calculation is shown here:
     *  https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_utils.c#L1496
     *
     * Each u32 word is appended a 5-bit value, for a total of 37 bits; see:
     *  https://github.com/Xilinx/embeddedsw/blob/release-2019.2/lib/sw_services/xilskey/src/xilskey_utils.c#L1356
     */
    uint32_t crc = prev_crc;
    const unsigned rshf = 7;
    const uint32_t im = (1 << rshf) - 1;
    const uint32_t rm = (1 << (32 - rshf)) - 1;
    const uint32_t i2 = (1 << 2) - 1;
    const uint32_t r2 = (1 << 30) - 1;

    unsigned j;
    uint32_t i, r;
    uint64_t w;

    w = (uint64_t)(addr) << 32;
    w |= data;

    /* Feed 35 bits, in 5 rounds, each a slice of 7 bits */
    for (j = 0; j < 5; j++) {
        r = rm & (crc >> rshf);
        i = im & (crc ^ w);
        crc = crc_tab[i] ^ r;

        w >>= rshf;
    }

    /* Feed the remaining 2 bits */
    r = r2 & (crc >> 2);
    i = i2 & (crc ^ w);
    crc = crc_tab[i << (rshf - 2)] ^ r;

    return crc;
}

uint32_t xlnx_efuse_calc_crc(const uint32_t *data, unsigned u32_cnt,
                             unsigned zpads)
{
    uint32_t crc = 0;
    unsigned index;

    for (index = zpads; index; index--) {
        crc = xlnx_efuse_u37_crc(crc, 0, (index + u32_cnt));
    }

    for (index = u32_cnt; index; index--) {
        crc = xlnx_efuse_u37_crc(crc, data[index - 1], index);
    }

    return crc;
}
