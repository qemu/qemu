/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

/* .word encodings let these tests build without HLX assembler support. */

#ifndef HLX_TEST_H
#define HLX_TEST_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hlx_asm.h"

static int err;

static inline void check_h(const char *name, const uint16_t *out,
                     const uint16_t *expected, int count)
{
    for (int i = 0; i < count; i++) {
        if (out[i] != expected[i]) {
            printf("FAIL %s [%d]: expected 0x%04x, got 0x%04x\n",
                   name, i, expected[i], out[i]);
            err++;
            if (err > 20) {
                printf("Too many errors, stopping %s\n", name);
                return;
            }
        }
    }
}

static inline void check_w(const char *name, const uint32_t *out,
                     const uint32_t *expected, int count)
{
    for (int i = 0; i < count; i++) {
        if (out[i] != expected[i]) {
            printf("FAIL %s [%d]: expected 0x%08lx, got 0x%08lx\n",
                   name, i, (unsigned long)expected[i],
                   (unsigned long)out[i]);
            err++;
            if (err > 20) {
                printf("Too many errors, stopping %s\n", name);
                return;
            }
        }
    }
}

/* Reconstruct word-pair values from interleaved L-register buffers. */
static inline void check_wp(const char *name, const uint32_t *out_lo,
                      const uint32_t *out_hi, const uint32_t *expected,
                      int count)
{
    const uint16_t *lo = (const uint16_t *)out_lo;
    const uint16_t *hi = (const uint16_t *)out_hi;
    for (int i = 0; i < count; i++) {
        uint32_t got = ((uint32_t)hi[i] << 16) | lo[i];
        if (got != expected[i]) {
            printf("FAIL %s [%d]: expected 0x%08lx, got 0x%08lx\n",
                   name, i, (unsigned long)expected[i],
                   (unsigned long)got);
            err++;
            if (err > 20) {
                printf("Too many errors, stopping %s\n", name);
                return;
            }
        }
    }
}

static inline int16_t sat_h(int32_t val)
{
    if (val > 32767) {
        return 32767;
    }
    if (val < -32768) {
        return -32768;
    }
    return (int16_t)val;
}

static inline uint16_t sat_uh(int32_t val)
{
    if (val > 65535) {
        return 65535;
    }
    if (val < 0) {
        return 0;
    }
    return (uint16_t)val;
}

static inline int32_t sat_w(int64_t val)
{
    if (val > 2147483647LL) {
        return 2147483647;
    }
    if (val < -2147483648LL) {
        return -2147483648;
    }
    return (int32_t)val;
}

static inline uint32_t sat_uw(int64_t val)
{
    if (val > 4294967295LL) {
        return 4294967295U;
    }
    if (val < 0) {
        return 0;
    }
    return (uint32_t)val;
}

/* HLX registers and their load/store buffers are 2048-byte aligned. */
#define HLX_HALF_ELEMS  1024
#define HLX_WORD_ELEMS  512
#define HLX_VEC_BYTES   2048
#define HLX_ALIGN       __attribute__((aligned(2048)))

/* Linux user mode uses aligned heap buffers and enables HLX. */

/* Linux does not need VTCM setup. */
static inline void init_vtcm(void) { }

/* Linux already enables HLX. */
static inline void enable_hlx(void) { }

/* Allocate a 2KB-aligned heap buffer. */
static inline void *vtcm_alloc(size_t bytes)
{
    size_t aligned_bytes = (bytes + (HLX_VEC_BYTES - 1))
                           & ~(size_t)(HLX_VEC_BYTES - 1);
    void *ptr = aligned_alloc(HLX_VEC_BYTES, aligned_bytes);
    if (!ptr) {
        fprintf(stderr, "aligned_alloc failed for %zu bytes\n",
                aligned_bytes);
        exit(1);
    }
    memset(ptr, 0, aligned_bytes);
    return ptr;
}

/* Set VSP to the end of an aligned L-register spill area. */
#define HLX_VSP_SIZE  (16 * HLX_VEC_BYTES) /* 32 KB for spill slots */

static inline void init_hlx_vsp(void)
{
    void *vsp_area = vtcm_alloc(HLX_VSP_SIZE);
    /* VSP grows downward. */
    uint32_t vsp_top = (uint32_t)(uintptr_t)vsp_area + HLX_VSP_SIZE;
    asm volatile("r0 = %[top]\n"
                 ".word 0x6220c01d\n" /* vsp = r0 */
                 : : [top] "r"(vsp_top) : "r0");
}

#endif /* HLX_TEST_H */
