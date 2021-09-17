/*
 * QEMU model of the Xilinx eFuse core
 *
 * Copyright (c) 2015 Xilinx Inc.
 *
 * Written by Edgar E. Iglesias <edgari@xilinx.com>
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

#ifndef XLNX_EFUSE_H
#define XLNX_EFUSE_H

#include "sysemu/block-backend.h"
#include "hw/qdev-core.h"

#define TYPE_XLNX_EFUSE "xlnx,efuse"
OBJECT_DECLARE_SIMPLE_TYPE(XlnxEFuse, XLNX_EFUSE);

struct XlnxEFuse {
    DeviceState parent_obj;
    BlockBackend *blk;
    bool blk_ro;
    uint32_t *fuse32;

    DeviceState *dev;

    bool init_tbits;

    uint8_t efuse_nr;
    uint32_t efuse_size;

    uint32_t *ro_bits;
    uint32_t ro_bits_cnt;
};

/**
 * xlnx_efuse_calc_crc:
 * @data: an array of 32-bit words for which the CRC should be computed
 * @u32_cnt: the array size in number of 32-bit words
 * @zpads: the number of 32-bit zeros prepended to @data before computation
 *
 * This function is used to compute the CRC for an array of 32-bit words,
 * using a Xilinx-specific data padding.
 *
 * Returns: the computed 32-bit CRC
 */
uint32_t xlnx_efuse_calc_crc(const uint32_t *data, unsigned u32_cnt,
                             unsigned zpads);

/**
 * xlnx_efuse_get_bit:
 * @s: the efuse object
 * @bit: the efuse bit-address to read the data
 *
 * Returns: the bit, 0 or 1, at @bit of object @s
 */
bool xlnx_efuse_get_bit(XlnxEFuse *s, unsigned int bit);

/**
 * xlnx_efuse_set_bit:
 * @s: the efuse object
 * @bit: the efuse bit-address to be written a value of 1
 *
 * Returns: true on success, false on failure
 */
bool xlnx_efuse_set_bit(XlnxEFuse *s, unsigned int bit);

/**
 * xlnx_efuse_k256_check:
 * @s: the efuse object
 * @crc: the 32-bit CRC to be compared with
 * @start: the efuse bit-address (which must be multiple of 32) of the
 *         start of a 256-bit array
 *
 * This function computes the CRC of a 256-bit array starting at @start
 * then compares to the given @crc
 *
 * Returns: true of @crc == computed, false otherwise
 */
bool xlnx_efuse_k256_check(XlnxEFuse *s, uint32_t crc, unsigned start);

/**
 * xlnx_efuse_tbits_check:
 * @s: the efuse object
 *
 * This function inspects a number of efuse bits at specific addresses
 * to see if they match a validation pattern. Each pattern is a group
 * of 4 bits, and there are 3 groups.
 *
 * Returns: a 3-bit mask, where a bit of '1' means the corresponding
 * group has a valid pattern.
 */
uint32_t xlnx_efuse_tbits_check(XlnxEFuse *s);

/**
 * xlnx_efuse_get_row:
 * @s: the efuse object
 * @bit: the efuse bit address for which a 32-bit value is read
 *
 * Returns: the entire 32 bits of the efuse, starting at a bit
 * address that is multiple of 32 and contains the bit at @bit
 */
static inline uint32_t xlnx_efuse_get_row(XlnxEFuse *s, unsigned int bit)
{
    if (!(s->fuse32)) {
        return 0;
    } else {
        unsigned int row_idx = bit / 32;

        assert(row_idx < (s->efuse_size * s->efuse_nr / 32));
        return s->fuse32[row_idx];
    }
}

#endif
