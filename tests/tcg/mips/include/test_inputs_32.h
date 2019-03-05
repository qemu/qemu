/*
 *  Header file for pattern and random test inputs
 *
 *  Copyright (C) 2019  Wave Computing, Inc.
 *  Copyright (C) 2019  Aleksandar Markovic <amarkovic@wavecomp.com>
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

#ifndef TEST_INPUTS_32_H
#define TEST_INPUTS_32_H

#include <stdint.h>


#define PATTERN_INPUTS_32_COUNT          64
#define PATTERN_INPUTS_32_SHORT_COUNT     8

static const uint32_t b32_pattern[PATTERN_INPUTS_32_COUNT] = {
    0xFFFFFFFF,                                          /*   0 */
    0x00000000,
    0xAAAAAAAA,
    0x55555555,
    0xCCCCCCCC,
    0x33333333,
    0xE38E38E3,
    0x1C71C71C,
    0xF0F0F0F0,                                          /*   8 */
    0x0F0F0F0F,
    0xF83E0F83,
    0x07C1F07C,
    0xFC0FC0FC,
    0x03F03F03,
    0xFE03F80F,
    0x01FC07F0,
    0xFF00FF00,                                          /*  16 */
    0x00FF00FF,
    0xFF803FE0,
    0x007FC01F,
    0xFFC00FFC,
    0x003FF003,
    0xFFE003FF,
    0x001FFC00,
    0xFFF000FF,                                          /*  24 */
    0x000FFF00,
    0xFFF8003F,
    0x0007FFC0,
    0xFFFC000F,
    0x0003FFF0,
    0xFFFE0003,
    0x0001FFFC,
    0xFFFF0000,                                          /*  32 */
    0x0000FFFF,
    0xFFFF8000,
    0x00007FFF,
    0xFFFFC000,
    0x00003FFF,
    0xFFFFE000,
    0x00001FFF,
    0xFFFFF000,                                          /*  40 */
    0x00000FFF,
    0xFFFFF800,
    0x000007FF,
    0xFFFFFC00,
    0x000003FF,
    0xFFFFFE00,
    0x000001FF,
    0xFFFFFF00,                                          /*  48 */
    0x000000FF,
    0xFFFFFF80,
    0x0000007F,
    0xFFFFFFC0,
    0x0000003F,
    0xFFFFFFE0,
    0x0000001F,
    0xFFFFFFF0,                                          /*  56 */
    0x0000000F,
    0xFFFFFFF8,
    0x00000007,
    0xFFFFFFFC,
    0x00000003,
    0xFFFFFFFE,
    0x00000001,
};


#define RANDOM_INPUTS_32_COUNT           16
#define RANDOM_INPUTS_32_SHORT_COUNT      4

static const uint32_t b32_random[RANDOM_INPUTS_32_COUNT] = {
    0x886AE6CC,                                          /*   0 */
    0xFBBE0063,
    0xAC5AAEAA,
    0x704F164D,
    0xB9926B7C,
    0xD027BE89,
    0xB83B5806,
    0xFC8F23F0,
    0x201E09CD,                                          /*   8 */
    0xA57CD913,
    0xA2E8F6F5,
    0xA89CF2F1,
    0xE61438E9,
    0x944A35FD,
    0x46304263,
    0x8B5AA7A2,
};


#endif
