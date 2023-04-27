/*
 *  Copyright(c) 2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#define fVROUND(VAL, SHAMT) \
    ((VAL) + (((SHAMT) > 0) ? (1LL << ((SHAMT) - 1)) : 0))

#define fVSATUB(VAL) \
    ((((VAL) & 0xffLL) == (VAL)) ? \
        (VAL) : \
        ((((int32_t)(VAL)) < 0) ? 0 : 0xff))

#define fVSATUH(VAL) \
    ((((VAL) & 0xffffLL) == (VAL)) ? \
        (VAL) : \
        ((((int32_t)(VAL)) < 0) ? 0 : 0xffff))

static void test_vasrvuhubrndsat(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE / 2; i++) {
        asm("v4 = vmem(%0 + #0)\n\t"
            "v5 = vmem(%0 + #1)\n\t"
            "v6 = vmem(%1 + #0)\n\t"
            "v5.ub = vasr(v5:4.uh, v6.ub):rnd:sat\n\t"
            "vmem(%2) = v5\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "v4", "v5", "v6", "memory");
        p0 += sizeof(MMVector) * 2;
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) {
            int shamt;
            uint8_t byte0;
            uint8_t byte1;

            shamt = buffer1[i].ub[2 * j + 0] & 0x7;
            byte0 = fVSATUB(fVROUND(buffer0[2 * i + 0].uh[j], shamt) >> shamt);
            shamt = buffer1[i].ub[2 * j + 1] & 0x7;
            byte1 = fVSATUB(fVROUND(buffer0[2 * i + 1].uh[j], shamt) >> shamt);
            expect[i].uh[j] = (byte1 << 8) | (byte0 & 0xff);
        }
    }

    check_output_h(__LINE__, BUFSIZE / 2);
}

static void test_vasrvuhubsat(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE / 2; i++) {
        asm("v4 = vmem(%0 + #0)\n\t"
            "v5 = vmem(%0 + #1)\n\t"
            "v6 = vmem(%1 + #0)\n\t"
            "v5.ub = vasr(v5:4.uh, v6.ub):sat\n\t"
            "vmem(%2) = v5\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "v4", "v5", "v6", "memory");
        p0 += sizeof(MMVector) * 2;
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) {
            int shamt;
            uint8_t byte0;
            uint8_t byte1;

            shamt = buffer1[i].ub[2 * j + 0] & 0x7;
            byte0 = fVSATUB(buffer0[2 * i + 0].uh[j] >> shamt);
            shamt = buffer1[i].ub[2 * j + 1] & 0x7;
            byte1 = fVSATUB(buffer0[2 * i + 1].uh[j] >> shamt);
            expect[i].uh[j] = (byte1 << 8) | (byte0 & 0xff);
        }
    }

    check_output_h(__LINE__, BUFSIZE / 2);
}

static void test_vasrvwuhrndsat(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE / 2; i++) {
        asm("v4 = vmem(%0 + #0)\n\t"
            "v5 = vmem(%0 + #1)\n\t"
            "v6 = vmem(%1 + #0)\n\t"
            "v5.uh = vasr(v5:4.w, v6.uh):rnd:sat\n\t"
            "vmem(%2) = v5\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "v4", "v5", "v6", "memory");
        p0 += sizeof(MMVector) * 2;
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            int shamt;
            uint16_t half0;
            uint16_t half1;

            shamt = buffer1[i].uh[2 * j + 0] & 0xf;
            half0 = fVSATUH(fVROUND(buffer0[2 * i + 0].w[j], shamt) >> shamt);
            shamt = buffer1[i].uh[2 * j + 1] & 0xf;
            half1 = fVSATUH(fVROUND(buffer0[2 * i + 1].w[j], shamt) >> shamt);
            expect[i].w[j] = (half1 << 16) | (half0 & 0xffff);
        }
    }

    check_output_w(__LINE__, BUFSIZE / 2);
}

static void test_vasrvwuhsat(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE / 2; i++) {
        asm("v4 = vmem(%0 + #0)\n\t"
            "v5 = vmem(%0 + #1)\n\t"
            "v6 = vmem(%1 + #0)\n\t"
            "v5.uh = vasr(v5:4.w, v6.uh):sat\n\t"
            "vmem(%2) = v5\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "v4", "v5", "v6", "memory");
        p0 += sizeof(MMVector) * 2;
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            int shamt;
            uint16_t half0;
            uint16_t half1;

            shamt = buffer1[i].uh[2 * j + 0] & 0xf;
            half0 = fVSATUH(buffer0[2 * i + 0].w[j] >> shamt);
            shamt = buffer1[i].uh[2 * j + 1] & 0xf;
            half1 = fVSATUH(buffer0[2 * i + 1].w[j] >> shamt);
            expect[i].w[j] = (half1 << 16) | (half0 & 0xffff);
        }
    }

    check_output_w(__LINE__, BUFSIZE / 2);
}

static void test_vassign_tmp(void)
{
    void *p0 = buffer0;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE; i++) {
        /*
         * Assign into v12 as .tmp, then use it in the next packet
         * Should get the new value within the same packet and
         * the old value in the next packet
         */
        asm("v3 = vmem(%0 + #0)\n\t"
            "r1 = #1\n\t"
            "v12 = vsplat(r1)\n\t"
            "r1 = #2\n\t"
            "v13 = vsplat(r1)\n\t"
            "{\n\t"
            "    v12.tmp = v13\n\t"
            "    v4.w = vadd(v12.w, v3.w)\n\t"
            "}\n\t"
            "v4.w = vadd(v4.w, v12.w)\n\t"
            "vmem(%1 + #0) = v4\n\t"
            : : "r"(p0), "r"(pout)
            : "r1", "v3", "v4", "v12", "v13", "memory");
        p0 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] = buffer0[i].w[j] + 3;
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_vcombine_tmp(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE; i++) {
        /*
         * Combine into v13:12 as .tmp, then use it in the next packet
         * Should get the new value within the same packet and
         * the old value in the next packet
         */
        asm("v3 = vmem(%0 + #0)\n\t"
            "r1 = #1\n\t"
            "v12 = vsplat(r1)\n\t"
            "r1 = #2\n\t"
            "v13 = vsplat(r1)\n\t"
            "r1 = #3\n\t"
            "v14 = vsplat(r1)\n\t"
            "r1 = #4\n\t"
            "v15 = vsplat(r1)\n\t"
            "{\n\t"
            "    v13:12.tmp = vcombine(v15, v14)\n\t"
            "    v4.w = vadd(v12.w, v3.w)\n\t"
            "    v16 = v13\n\t"
            "}\n\t"
            "v4.w = vadd(v4.w, v12.w)\n\t"
            "v4.w = vadd(v4.w, v13.w)\n\t"
            "v4.w = vadd(v4.w, v16.w)\n\t"
            "vmem(%2 + #0) = v4\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "r1", "v3", "v4", "v12", "v13", "v14", "v15", "v16", "memory");
        p0 += sizeof(MMVector);
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] = buffer0[i].w[j] + 10;
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_vmpyuhvs(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    memset(expect, 0xaa, sizeof(expect));
    memset(output, 0xbb, sizeof(output));

    for (int i = 0; i < BUFSIZE; i++) {
        asm("v4 = vmem(%0 + #0)\n\t"
            "v5 = vmem(%1 + #0)\n\t"
            "v4.uh = vmpy(V4.uh, v5.uh):>>16\n\t"
            "vmem(%2) = v4\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "v4", "v5", "memory");
        p0 += sizeof(MMVector);
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 2; j++) {
            expect[i].uh[j] = (buffer0[i].uh[j] * buffer1[i].uh[j]) >> 16;
        }
    }

    check_output_h(__LINE__, BUFSIZE);
}

int main()
{
    init_buffers();

    test_vasrvuhubrndsat();
    test_vasrvuhubsat();
    test_vasrvwuhrndsat();
    test_vasrvwuhsat();

    test_vassign_tmp();
    test_vcombine_tmp();

    test_vmpyuhvs();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
