/*
 *  Copyright(c) 2021-2023 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

static void test_load_tmp(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;

    for (int i = 0; i < BUFSIZE; i++) {
        /*
         * Load into v12 as .tmp, then use it in the next packet
         * Should get the new value within the same packet and
         * the old value in the next packet
         */
        asm("v3 = vmem(%0 + #0)\n\t"
            "r1 = #1\n\t"
            "v12 = vsplat(r1)\n\t"
            "{\n\t"
            "    v12.tmp = vmem(%1 + #0)\n\t"
            "    v4.w = vadd(v12.w, v3.w)\n\t"
            "}\n\t"
            "v4.w = vadd(v4.w, v12.w)\n\t"
            "vmem(%2 + #0) = v4\n\t"
            : : "r"(p0), "r"(p1), "r"(pout)
            : "r1", "v12", "v3", "v4", "v6", "memory");
        p0 += sizeof(MMVector);
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] = buffer0[i].w[j] + buffer1[i].w[j] + 1;
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_load_tmp2(void)
{
    void *pout0 = &output[0];
    void *pout1 = &output[1];

    asm volatile(
        "r0 = #0x03030303\n\t"
        "v16 = vsplat(r0)\n\t"
        "r0 = #0x04040404\n\t"
        "v18 = vsplat(r0)\n\t"
        "r0 = #0x05050505\n\t"
        "v21 = vsplat(r0)\n\t"
        "{\n\t"
        "   v25:24 += vmpyo(v18.w, v14.h)\n\t"
        "   v15:14.tmp = vcombine(v21, v16)\n\t"
        "}\n\t"
        "vmem(%0 + #0) = v24\n\t"
        "vmem(%1 + #0) = v25\n\t"
        : : "r"(pout0), "r"(pout1)
        : "r0", "v16", "v18", "v21", "v24", "v25", "memory"
    );

    for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; ++i) {
        expect[0].w[i] = 0x180c0000;
        expect[1].w[i] = 0x000c1818;
    }

    check_output_w(__LINE__, 2);
}

static void test_load_cur(void)
{
    void *p0 = buffer0;
    void *pout = output;

    for (int i = 0; i < BUFSIZE; i++) {
        asm("{\n\t"
            "    v2.cur = vmem(%0 + #0)\n\t"
            "    vmem(%1 + #0) = v2\n\t"
            "}\n\t"
            : : "r"(p0), "r"(pout) : "v2", "memory");
        p0 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].uw[j] = buffer0[i].uw[j];
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_load_aligned(void)
{
    /* Aligned loads ignore the low bits of the address */
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 13;

    p0 += offset;    /* Create an unaligned address */
    asm("v2 = vmem(%0 + #0)\n\t"
        "vmem(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    expect[0] = buffer0[0];

    check_output_w(__LINE__, 1);
}

static void test_load_unaligned(void)
{
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 12;

    p0 += offset;    /* Create an unaligned address */
    asm("v2 = vmemu(%0 + #0)\n\t"
        "vmem(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    memcpy(expect, &buffer0[0].ub[offset], sizeof(MMVector));

    check_output_w(__LINE__, 1);
}

static void test_store_aligned(void)
{
    /* Aligned stores ignore the low bits of the address */
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 13;

    pout += offset;    /* Create an unaligned address */
    asm("v2 = vmem(%0 + #0)\n\t"
        "vmem(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    expect[0] = buffer0[0];

    check_output_w(__LINE__, 1);
}

static void test_store_unaligned(void)
{
    void *p0 = buffer0;
    void *pout = output;
    const size_t offset = 12;

    pout += offset;    /* Create an unaligned address */
    asm("v2 = vmem(%0 + #0)\n\t"
        "vmemu(%1 + #0) = v2\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    memcpy(expect, buffer0, 2 * sizeof(MMVector));
    memcpy(&expect[0].ub[offset], buffer0, sizeof(MMVector));

    check_output_w(__LINE__, 2);
}

static void test_masked_store(bool invert)
{
    void *p0 = buffer0;
    void *pmask = mask;
    void *pout = output;

    memset(expect, 0xff, sizeof(expect));
    memset(output, 0xff, sizeof(expect));

    for (int i = 0; i < BUFSIZE; i++) {
        if (invert) {
            asm("r4 = #0\n\t"
                "v4 = vsplat(r4)\n\t"
                "v5 = vmem(%0 + #0)\n\t"
                "q0 = vcmp.eq(v4.w, v5.w)\n\t"
                "v5 = vmem(%1)\n\t"
                "if (!q0) vmem(%2) = v5\n\t"             /* Inverted test */
                : : "r"(pmask), "r"(p0), "r"(pout)
                : "r4", "v4", "v5", "q0", "memory");
        } else {
            asm("r4 = #0\n\t"
                "v4 = vsplat(r4)\n\t"
                "v5 = vmem(%0 + #0)\n\t"
                "q0 = vcmp.eq(v4.w, v5.w)\n\t"
                "v5 = vmem(%1)\n\t"
                "if (q0) vmem(%2) = v5\n\t"             /* Non-inverted test */
                : : "r"(pmask), "r"(p0), "r"(pout)
                : "r4", "v4", "v5", "q0", "memory");
        }
        p0 += sizeof(MMVector);
        pmask += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            if (invert) {
                if (i + j % MASKMOD != 0) {
                    expect[i].w[j] = buffer0[i].w[j];
                }
            } else {
                if (i + j % MASKMOD == 0) {
                    expect[i].w[j] = buffer0[i].w[j];
                }
            }
        }
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_new_value_store(void)
{
    void *p0 = buffer0;
    void *pout = output;

    asm("{\n\t"
        "    v2 = vmem(%0 + #0)\n\t"
        "    vmem(%1 + #0) = v2.new\n\t"
        "}\n\t"
        : : "r"(p0), "r"(pout) : "v2", "memory");

    expect[0] = buffer0[0];

    check_output_w(__LINE__, 1);
}

static void test_max_temps()
{
    void *p0 = buffer0;
    void *pout = output;

    asm("v0 = vmem(%0 + #0)\n\t"
        "v1 = vmem(%0 + #1)\n\t"
        "v2 = vmem(%0 + #2)\n\t"
        "v3 = vmem(%0 + #3)\n\t"
        "v4 = vmem(%0 + #4)\n\t"
        "{\n\t"
        "    v1:0.w = vadd(v3:2.w, v1:0.w)\n\t"
        "    v2.b = vshuffe(v3.b, v2.b)\n\t"
        "    v3.w = vadd(v1.w, v4.w)\n\t"
        "    v4.tmp = vmem(%0 + #5)\n\t"
        "}\n\t"
        "vmem(%1 + #0) = v0\n\t"
        "vmem(%1 + #1) = v1\n\t"
        "vmem(%1 + #2) = v2\n\t"
        "vmem(%1 + #3) = v3\n\t"
        "vmem(%1 + #4) = v4\n\t"
        : : "r"(p0), "r"(pout) : "memory");

        /* The first two vectors come from the vadd-pair instruction */
        for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
            expect[0].w[i] = buffer0[0].w[i] + buffer0[2].w[i];
            expect[1].w[i] = buffer0[1].w[i] + buffer0[3].w[i];
        }
        /* The third vector comes from the vshuffe instruction */
        for (int i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
            expect[2].uh[i] = (buffer0[2].uh[i] & 0xff) |
                              (buffer0[3].uh[i] & 0xff) << 8;
        }
        /* The fourth vector comes from the vadd-single instruction */
        for (int i = 0; i < MAX_VEC_SIZE_BYTES / 4; i++) {
            expect[3].w[i] = buffer0[1].w[i] + buffer0[5].w[i];
        }
        /*
         * The fifth vector comes from the load to v4
         * make sure the .tmp is dropped
         */
        expect[4] = buffer0[4];

        check_output_b(__LINE__, 5);
}

TEST_VEC_OP2(vadd_w, vadd, .w, w, 4, +)
TEST_VEC_OP2(vadd_h, vadd, .h, h, 2, +)
TEST_VEC_OP2(vadd_b, vadd, .b, b, 1, +)
TEST_VEC_OP2(vsub_w, vsub, .w, w, 4, -)
TEST_VEC_OP2(vsub_h, vsub, .h, h, 2, -)
TEST_VEC_OP2(vsub_b, vsub, .b, b, 1, -)
TEST_VEC_OP2(vxor, vxor, , d, 8, ^)
TEST_VEC_OP2(vand, vand, , d, 8, &)
TEST_VEC_OP2(vor, vor, , d, 8, |)
TEST_VEC_OP1(vnot, vnot, , d, 8, ~)

TEST_PRED_OP2(pred_or, or, |, "")
TEST_PRED_OP2(pred_or_n, or, |, "!")
TEST_PRED_OP2(pred_and, and, &, "")
TEST_PRED_OP2(pred_and_n, and, &, "!")
TEST_PRED_OP2(pred_xor, xor, ^, "")

static void test_vadduwsat(void)
{
    /*
     * Test for saturation by adding two numbers that add to more than UINT_MAX
     * and make sure the result saturates to UINT_MAX
     */
    const uint32_t x = 0xffff0000;
    const uint32_t y = 0x000fffff;

    memset(expect, 0x12, sizeof(MMVector));
    memset(output, 0x34, sizeof(MMVector));

    asm volatile ("v10 = vsplat(%0)\n\t"
                  "v11 = vsplat(%1)\n\t"
                  "v21.uw = vadd(v11.uw, v10.uw):sat\n\t"
                  "vmem(%2+#0) = v21\n\t"
                  : /* no outputs */
                  : "r"(x), "r"(y), "r"(output)
                  : "v10", "v11", "v21", "memory");

    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        expect[0].uw[j] = UINT_MAX;
    }

    check_output_w(__LINE__, 1);
}

static void test_vsubuwsat_dv(void)
{
    /*
     * Test for saturation by subtracting two numbers where the result is
     * negative and make sure the result saturates to zero
     *
     * vsubuwsat_dv operates on an HVX register pair, so we'll have a
     * pair of subtractions
     *     w - x < 0
     *     y - z < 0
     */
    const uint32_t w = 0x000000b7;
    const uint32_t x = 0xffffff4e;
    const uint32_t y = 0x31fe88e7;
    const uint32_t z = 0x7fffff79;

    memset(expect, 0x12, sizeof(MMVector) * 2);
    memset(output, 0x34, sizeof(MMVector) * 2);

    asm volatile ("v16 = vsplat(%0)\n\t"
                  "v17 = vsplat(%1)\n\t"
                  "v26 = vsplat(%2)\n\t"
                  "v27 = vsplat(%3)\n\t"
                  "v25:24.uw = vsub(v17:16.uw, v27:26.uw):sat\n\t"
                  "vmem(%4+#0) = v24\n\t"
                  "vmem(%4+#1) = v25\n\t"
                  : /* no outputs */
                  : "r"(w), "r"(y), "r"(x), "r"(z), "r"(output)
                  : "v16", "v17", "v24", "v25", "v26", "v27", "memory");

    for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
        expect[0].uw[j] = 0x00000000;
        expect[1].uw[j] = 0x00000000;
    }

    check_output_w(__LINE__, 2);
}

static void test_load_tmp_predicated(void)
{
    void *p0 = buffer0;
    void *p1 = buffer1;
    void *pout = output;
    bool pred = true;

    for (int i = 0; i < BUFSIZE; i++) {
        /*
         * Load into v12 as .tmp with a predicate
         * When the predicate is true, we get the vector from buffer1[i]
         * When the predicate is false, we get a vector of all 1's
         * Regardless of the predicate, the next packet should have
         * a vector of all 1's
         */
        asm("v3 = vmem(%0 + #0)\n\t"
            "r1 = #1\n\t"
            "v12 = vsplat(r1)\n\t"
            "p1 = !cmp.eq(%3, #0)\n\t"
            "{\n\t"
            "    if (p1) v12.tmp = vmem(%1 + #0)\n\t"
            "    v4.w = vadd(v12.w, v3.w)\n\t"
            "}\n\t"
            "v4.w = vadd(v4.w, v12.w)\n\t"
            "vmem(%2 + #0) = v4\n\t"
            : : "r"(p0), "r"(p1), "r"(pout), "r"(pred)
            : "r1", "p1", "v12", "v3", "v4", "v6", "memory");
        p0 += sizeof(MMVector);
        p1 += sizeof(MMVector);
        pout += sizeof(MMVector);

        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[i].w[j] =
                pred ? buffer0[i].w[j] + buffer1[i].w[j] + 1
                     : buffer0[i].w[j] + 2;
        }
        pred = !pred;
    }

    check_output_w(__LINE__, BUFSIZE);
}

static void test_load_cur_predicated(void)
{
    bool pred = true;
    for (int i = 0; i < BUFSIZE; i++) {
        asm volatile("p0 = !cmp.eq(%3, #0)\n\t"
                     "v3 = vmem(%0+#0)\n\t"
                     /*
                      * Preload v4 to make sure that the assignment from the
                      * packet below is not being ignored when pred is false.
                      */
                     "r0 = #0x01237654\n\t"
                     "v4 = vsplat(r0)\n\t"
                     "{\n\t"
                     "    if (p0) v3.cur = vmem(%1+#0)\n\t"
                     "    v4 = v3\n\t"
                     "}\n\t"
                     "vmem(%2+#0) = v4\n\t"
                     :
                     : "r"(&buffer0[i]), "r"(&buffer1[i]),
                       "r"(&output[i]), "r"(pred)
                     : "r0", "p0", "v3", "v4", "memory");
        expect[i] = pred ? buffer1[i] : buffer0[i];
        pred = !pred;
    }
    check_output_w(__LINE__, BUFSIZE);
}

static void test_vcombine(void)
{
    for (int i = 0; i < BUFSIZE / 2; i++) {
        asm volatile("v2 = vsplat(%0)\n\t"
                     "v3 = vsplat(%1)\n\t"
                     "v3:2 = vcombine(v2, v3)\n\t"
                     "vmem(%2+#0) = v2\n\t"
                     "vmem(%2+#1) = v3\n\t"
                     :
                     : "r"(2 * i), "r"(2 * i + 1), "r"(&output[2 * i])
                     : "v2", "v3", "memory");
        for (int j = 0; j < MAX_VEC_SIZE_BYTES / 4; j++) {
            expect[2 * i].w[j] = 2 * i + 1;
            expect[2 * i + 1].w[j] = 2 * i;
        }
    }
    check_output_w(__LINE__, BUFSIZE);
}

int main()
{
    init_buffers();

    test_load_tmp();
    test_load_tmp2();
    test_load_cur();
    test_load_aligned();
    test_load_unaligned();
    test_store_aligned();
    test_store_unaligned();
    test_masked_store(false);
    test_masked_store(true);
    test_new_value_store();
    test_max_temps();

    test_vadd_w();
    test_vadd_h();
    test_vadd_b();
    test_vsub_w();
    test_vsub_h();
    test_vsub_b();
    test_vxor();
    test_vand();
    test_vor();
    test_vnot();

    test_pred_or(false);
    test_pred_or_n(true);
    test_pred_and(false);
    test_pred_and_n(true);
    test_pred_xor(false);

    test_vadduwsat();
    test_vsubuwsat_dv();

    test_load_tmp_predicated();
    test_load_cur_predicated();

    test_vcombine();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
