/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */
#ifndef HEXAGON_HLX_H
#define HEXAGON_HLX_H

#define NUM_LREGS 16
#define HLX_VEC_SIZE_BYTES 2048

typedef union {
    uint8_t  ub[4];
    int8_t   b[4];
    uint16_t uh[2];
    int16_t  h[2];
    uint32_t uw;
    int32_t  w;
} lreg_element_t;

typedef struct {
    union {
        uint64_t ud[HLX_VEC_SIZE_BYTES / 8];
        int64_t  d[HLX_VEC_SIZE_BYTES / 8];
        uint32_t uw[HLX_VEC_SIZE_BYTES / 4];
        int32_t  w[HLX_VEC_SIZE_BYTES / 4];
        uint16_t uh[HLX_VEC_SIZE_BYTES / 2];
        int16_t  h[HLX_VEC_SIZE_BYTES / 2];
        uint8_t  ub[HLX_VEC_SIZE_BYTES / 1];
        int8_t   b[HLX_VEC_SIZE_BYTES / 1];
    };
} HLXVector;

typedef union {
    lreg_element_t l[HLX_VEC_SIZE_BYTES / 2];
} HLXVectorPair;

static inline HLXVector hlx_zero_vector(void)
{
    HLXVector ret;
    memset(&ret, 0, sizeof(ret));
    return ret;
}

/* Interleave separate low and high halfwords as {lo, hi}. */
static inline void hlx_interleave_pair(HLXVectorPair *pair,
                                       const HLXVector *lo,
                                       const HLXVector *hi)
{
    for (int i = 0; i < HLX_VEC_SIZE_BYTES / 2; i++) {
        pair->l[i].h[0] = lo->h[i];
        pair->l[i].h[1] = hi->h[i];
    }
}

static inline void hlx_deinterleave_pair(HLXVector *lo, HLXVector *hi,
                                         const HLXVectorPair *pair)
{
    for (int i = 0; i < HLX_VEC_SIZE_BYTES / 2; i++) {
        lo->h[i] = pair->l[i].h[0];
        hi->h[i] = pair->l[i].h[1];
    }
}

#endif
