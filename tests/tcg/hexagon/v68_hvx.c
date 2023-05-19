/*
 *  Copyright(c) 2022-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <limits.h>

int err;

#include "hvx_misc.h"

MMVector v6mpy_buffer0[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
MMVector v6mpy_buffer1[BUFSIZE] __attribute__((aligned(MAX_VEC_SIZE_BYTES)));

static void init_v6mpy_buffers(void)
{
    int counter0 = 0;
    int counter1 = 17;
    for (int i = 0; i < BUFSIZE; i++) {
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            v6mpy_buffer0[i].w[j] = counter0++;
            v6mpy_buffer1[i].w[j] = counter1++;
        }
    }
}

int v6mpy_ref[BUFSIZE][MAX_VEC_SIZE_BYTES / 4] = {
#include "v6mpy_ref.c.inc"
};

static void test_v6mpy(void)
{
    void *p00 = buffer0;
    void *p01 = v6mpy_buffer0;
    void *p10 = buffer1;
    void *p11 = v6mpy_buffer1;
    void *pout = output;

    memset(expect, 0xff, sizeof(expect));
    memset(output, 0xff, sizeof(expect));

    for (int i = 0; i < BUFSIZE; i++) {
        asm("v2 = vmem(%0 + #0)\n\t"
            "v3 = vmem(%1 + #0)\n\t"
            "v4 = vmem(%2 + #0)\n\t"
            "v5 = vmem(%3 + #0)\n\t"
            "v5:4.w = v6mpy(v5:4.ub, v3:2.b, #1):v\n\t"
            "vmem(%4 + #0) = v4\n\t"
            : : "r"(p00), "r"(p01), "r"(p10), "r"(p11), "r"(pout)
            : "v2", "v3", "v4", "v5", "memory");
        p00 += sizeof(MMVector);
        p01 += sizeof(MMVector);
        p10 += sizeof(MMVector);
        p11 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] = v6mpy_ref[i][j];
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

int main()
{
    init_buffers();
    init_v6mpy_buffers();

    test_v6mpy();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
