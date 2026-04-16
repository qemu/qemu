/*
 *  Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hvx_ieee_fp.h"

float32 fp_mult_sf_hf(float16 a1, float16 a2, float_status *fp_status)
{
    return float32_mul(float16_to_float32(a1, true, fp_status),
                       float16_to_float32(a2, true, fp_status), fp_status);
}

float32 fp_vdmpy(float16 a1, float16 a2, float16 a3, float16 a4,
                 float_status *fp_status)
{
    return float32_add(fp_mult_sf_hf(a1, a3, fp_status),
                       fp_mult_sf_hf(a2, a4, fp_status), fp_status);
}
