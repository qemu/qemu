/*
 * Hexagon HMX (Matrix eXtensions) State Definitions
 *
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HMX_STATE_H
#define HEXAGON_HMX_STATE_H

#define HMX_SPATIAL_DIM_FXP     64
#define HMX_SPATIAL_DIM_FP      32
#define HMX_OUTPUT_CHANNELS     32
#define HMX_NUM_ACC_SETS        2
#define HMX_NUM_CVT_AGES        3
#define HMX_NUM_BIAS_SETS       4

#define HMX_ACT_CROUTON_SIZE    2048
#define HMX_ACT_CACHE_SIZE      (2 * HMX_ACT_CROUTON_SIZE)

typedef struct HmxAccFxp {
    int32_t data[HMX_SPATIAL_DIM_FXP][HMX_OUTPUT_CHANNELS];
} HmxAccFxp;

/* FP accumulator cell storage: IEEE binary64 bit patterns */
typedef struct HmxAccFp {
    uint64_t data[HMX_SPATIAL_DIM_FP][HMX_OUTPUT_CHANNELS];
} HmxAccFp;

typedef struct HmxAccSet {
    HmxAccFxp fxp_primary;
    HmxAccFp  fp_primary;
} HmxAccSet;

typedef struct HmxCvtStateFxp {
    uint16_t data[HMX_SPATIAL_DIM_FXP][HMX_OUTPUT_CHANNELS];
} HmxCvtStateFxp;

/* FP convert slot: one FP16 value, or an FP8 byte per spatial half */
typedef struct HmxCvtStateFp {
    uint16_t data[HMX_SPATIAL_DIM_FP][HMX_OUTPUT_CHANNELS];
} HmxCvtStateFp;

typedef struct HmxState {
    HmxAccSet acc[HMX_NUM_ACC_SETS];

    HmxCvtStateFxp cvt_fxp[HMX_NUM_CVT_AGES];
    HmxCvtStateFp  cvt_fp[HMX_NUM_CVT_AGES];

    HmxCvtStateFxp cvt_future_fxp;

    uint64_t bias_raw[HMX_NUM_BIAS_SETS][HMX_OUTPUT_CHANNELS];

    uint32_t current_acc_set;

    uint8_t act_buffer[HMX_ACT_CACHE_SIZE];

    uint32_t act_rs;
    uint32_t act_type;
    uint8_t  is_f8_odd;     /* Rs[0]: FP8 activation reads odd byte of pair */
    uint8_t  fp8_odd_sel;   /* cvt Rs[11]: FP8 convert writes odd (1) half */

    uint32_t format_offset;     /* SM=2, DM=0 */
    uint32_t tile_x_mask;
    uint32_t tile_y_mask;
    uint32_t tile_x_inc;
    uint32_t tile_y_inc;
    uint32_t fx;
    uint32_t fy;
    int32_t  ch_start;          /* pre-computed channel start (raw) */
    int32_t  ch_stop;           /* pre-computed channel stop (exclusive) */
    uint32_t y_start;
    uint32_t y_stop;
    uint32_t y_dilate;
    uint32_t blocks;
    int32_t  dY;                /* offset to second activation crouton */
    uint32_t group_conv;        /* ch_start > ch_stop */
    uint32_t group_size;
    uint32_t group_count;

    int32_t  mac_cycle_limit;

    /* Deferred CVT pipeline commit, at the next consumer or end of packet */
    uint32_t cvt_fxp_pending;      /* 1 = future has uncommitted data */
    uint32_t cvt_fxp_pending_age;  /* 1 = pipeline should age at commit */

    /* Deferred accumulator clear/flip, at packet end or next accumulator use */
    uint32_t cvt_acc_clear_pending;
    uint32_t cvt_acc_clear_set;
} HmxState;

#endif /* HEXAGON_HMX_STATE_H */
