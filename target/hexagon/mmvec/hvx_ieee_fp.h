/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HVX_IEEE_H
#define HEXAGON_HVX_IEEE_H

#include "fpu/softfloat.h"

#define f16_to_f32(A) float16_to_float32((A), true, &env->hvx_fp_status)

float32 fp_mult_sf_hf(float16 a1, float16 a2, float_status *fp_status);
float32 fp_vdmpy(float16 a1, float16 a2, float16 a3, float16 a4,
                 float_status *fp_status);

/* Qfloat min/max treat +NaN as greater than +INF and -NaN as smaller than -INF */
float32 qf_max_sf(float32 a1, float32 a2, float_status *fp_status);
float32 qf_min_sf(float32 a1, float32 a2, float_status *fp_status);
float16 qf_max_hf(float16 a1, float16 a2, float_status *fp_status);
float16 qf_min_hf(float16 a1, float16 a2, float_status *fp_status);

#endif
