/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/*
 * Test HVX weighted histogram instructions (vwhist128/vwhist256 variants).
 *
 * Each halfword in the input vector (loaded via Vx.tmp) contains a bucket
 * index (low byte) and a weight (high byte).  The instruction accumulates
 * the weight into the appropriate element of the destination vector.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>

int err;

#include "hex_test.h"

#define MAX_VEC_SIZE_BYTES  128

typedef union {
    uint32_t uw[MAX_VEC_SIZE_BYTES / 4];
    uint16_t uh[MAX_VEC_SIZE_BYTES / 2];
    uint8_t  ub[MAX_VEC_SIZE_BYTES];
} MMVector;

/*
 * Build input vector for vwhist256: each halfword is (weight << 8 | bucket).
 * We put bucket=0, weight=1 in every slot so that v0.uh[0] gets incremented
 * by 1 for each of the 64 halfwords -> v0.uh[0] should equal 64.
 */
static MMVector input __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
static MMVector zero_vec __attribute__((aligned(MAX_VEC_SIZE_BYTES)));
static MMVector result __attribute__((aligned(MAX_VEC_SIZE_BYTES)));

/*
 * Test vwhist256: 256-bin histogram with 16-bit accumulation.
 * Each halfword of the input: low byte = bucket, high byte = weight.
 * bucket bits [7:3] -> vindex (which vector register), [2:0] + lane -> element.
 * All entries have bucket=0 weight=1, so v0.uh[0..7] get incremented.
 */
static void test_vwhist256(void)
{
    int i;

    /* Set all input halfwords to bucket=0, weight=1 */
    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0x0100;  /* weight=1, bucket=0 */
    }

    asm volatile(
        /* Zero out v0 (the target for bucket 0) */
        "v0 = vmem(%[zero] + #0)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist256\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result)
        : "v0", "v12", "memory");

    /*
     * 64 input halfwords all targeting bucket 0.
     * elindex = (i & ~7) | (bucket & 7) = i & ~7  (since bucket=0).
     * So elements 0,8,16,24,32,40,48,56 each get 8 increments.
     */
    for (i = 0; i < 64; i++) {
        uint16_t expected = ((i & 7) == 0) ? 8 : 0;
        check16(result.uh[i], expected);
    }
}

/*
 * Test vwhist256:sat -- saturating variant.
 * Use weight=0xFF to test that saturation to 0xFFFF works.
 */
static void test_vwhist256_sat(void)
{
    int i;

    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0xFF00;  /* weight=0xFF, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist256:sat\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result)
        : "v0", "v12", "memory");

    /*
     * Same distribution as vwhist256: elements 0,8,16,...,56.
     * Each gets 8 * 0xFF = 2040 = 0x7F8 (within uint16 range).
     */
    for (i = 0; i < 64; i++) {
        uint16_t expected = ((i & 7) == 0) ? 8 * 0xFF : 0;
        check16(result.uh[i], expected);
    }
}

/*
 * Test vwhist128: 128-bin histogram with 32-bit accumulation.
 * bucket bits [7:3] -> vindex, [2:1] + lane -> element (word granularity).
 */
static void test_vwhist128(void)
{
    int i;

    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0x0100;  /* weight=1, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist128\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result)
        : "v0", "v12", "memory");

    /*
     * 128-bin: 64 halfwords all targeting bucket 0.
     * bucket 0 -> vindex=0, elindex based on (i>>1)&(~3) | (bucket>>1)&3.
     * With bucket=0, elindex = (i>>1)&(~3).
     * For i=0,1: elindex=0; i=2,3: elindex=0; ... up to i=6,7: elindex=0
     * i ranges 0..63. (i>>1) ranges 0..31.
     * (i>>1)&(~3) = 0,0,0,0,4,4,4,4,8,...
     * So elements 0,4,8,12,16,20,24,28 each get 8 increments.
     */
    for (i = 0; i < 32; i++) {
        uint32_t expected = ((i & 3) == 0) ? 8 : 0;
        check32(result.uw[i], expected);
    }
}

/*
 * Test vwhist128(#0) -- masked variant.
 * Only processes elements where (bucket & 1) == mode.
 */
static void test_vwhist128m(void)
{
    int i;

    /* All bucket=0, weight=1.  bucket&1==0, so mode=0 matches all. */
    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0x0100;  /* weight=1, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist128(#0)\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result)
        : "v0", "v12", "memory");

    /* Same distribution as vwhist128 since all buckets have bit0=0 */
    for (i = 0; i < 32; i++) {
        uint32_t expected = ((i & 3) == 0) ? 8 : 0;
        check32(result.uw[i], expected);
    }

    /* Now test with mode=1: bucket=0 has bit0=0, so nothing should match */
    memset(&result, 0, sizeof(result));
    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist128(#1)\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result)
        : "v0", "v12", "memory");

    for (i = 0; i < 32; i++) {
        check32(result.uw[i], 0);
    }
}

static MMVector ones_vec __attribute__((aligned(MAX_VEC_SIZE_BYTES)));

/*
 * Test vwhist256(Qv4) -- Q-masked vwhist256.
 * Set Q0 to all-ones so all elements pass the mask -> same as vwhist256.
 */
static void test_vwhist256q(void)
{
    int i;

    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0x0100;  /* weight=1, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "v1 = vmem(%[ones] + #0)\n\t"
        "q0 = vcmp.eq(v1.b, v1.b)\n\t"  /* all-ones Q0 */
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist256(q0)\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result), [ones] "r"(&ones_vec)
        : "v0", "v1", "v12", "q0", "memory");

    for (i = 0; i < 64; i++) {
        uint16_t expected = ((i & 7) == 0) ? 8 : 0;
        check16(result.uh[i], expected);
    }
}

/*
 * Test vwhist256(Qv4):sat -- Q-masked saturating vwhist256.
 */
static void test_vwhist256q_sat(void)
{
    int i;

    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0xFF00;  /* weight=0xFF, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "v1 = vmem(%[ones] + #0)\n\t"
        "q0 = vcmp.eq(v1.b, v1.b)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist256(q0):sat\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result), [ones] "r"(&ones_vec)
        : "v0", "v1", "v12", "q0", "memory");

    for (i = 0; i < 64; i++) {
        uint16_t expected = ((i & 7) == 0) ? 8 * 0xFF : 0;
        check16(result.uh[i], expected);
    }
}

/*
 * Test vwhist128(Qv4) -- Q-masked vwhist128.
 */
static void test_vwhist128q(void)
{
    int i;

    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0x0100;  /* weight=1, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "v1 = vmem(%[ones] + #0)\n\t"
        "q0 = vcmp.eq(v1.b, v1.b)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist128(q0)\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result), [ones] "r"(&ones_vec)
        : "v0", "v1", "v12", "q0", "memory");

    for (i = 0; i < 32; i++) {
        uint32_t expected = ((i & 3) == 0) ? 8 : 0;
        check32(result.uw[i], expected);
    }
}

/*
 * Test vwhist128(Qv4,#0) -- Q-masked mode vwhist128.
 */
static void test_vwhist128qm(void)
{
    int i;

    for (i = 0; i < MAX_VEC_SIZE_BYTES / 2; i++) {
        input.uh[i] = 0x0100;  /* weight=1, bucket=0 */
    }

    asm volatile(
        "v0 = vmem(%[zero] + #0)\n\t"
        "v1 = vmem(%[ones] + #0)\n\t"
        "q0 = vcmp.eq(v1.b, v1.b)\n\t"
        "{\n\t"
        "    v12.tmp = vmem(%[inp] + #0)\n\t"
        "    vwhist128(q0,#0)\n\t"
        "}\n\t"
        "vmem(%[out] + #0) = v0\n\t"
        :
        : [inp] "r"(&input), [zero] "r"(&zero_vec),
          [out] "r"(&result), [ones] "r"(&ones_vec)
        : "v0", "v1", "v12", "q0", "memory");

    /* bucket=0, bit0=0, mode=0 matches -> same as vwhist128 */
    for (i = 0; i < 32; i++) {
        uint32_t expected = ((i & 3) == 0) ? 8 : 0;
        check32(result.uw[i], expected);
    }
}

int main(void)
{
    memset(&zero_vec, 0, sizeof(zero_vec));
    memset(&ones_vec, 0xff, sizeof(ones_vec));

    test_vwhist256();
    test_vwhist256_sat();
    test_vwhist128();
    test_vwhist128m();
    test_vwhist256q();
    test_vwhist256q_sat();
    test_vwhist128q();
    test_vwhist128qm();

    puts(err ? "FAIL" : "PASS");
    return err ? 1 : 0;
}
