/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HLX_GVEC_TABLES_H
#define HLX_GVEC_TABLES_H

#include "tcg/tcg-op-gvec-common.h"

extern const GVecGen3 hlx_ushsat_add_op;
extern const GVecGen3 hlx_ushsat_sub_op;
extern const GVecGen2 hlx_abs_uh_sat_op;

extern const GVecGen3 hlx_avg_sh_op;
extern const GVecGen3 hlx_avg_sh_rnd_op;
extern const GVecGen3 hlx_avg_uh_op;
extern const GVecGen3 hlx_avg_uh_rnd_op;

extern const GVecGen3 hlx_absdiff_sh_op;
extern const GVecGen3 hlx_absdiff_uh_op;

extern const GVecGen2 hlx_sari15_h_op;

#endif /* HLX_GVEC_TABLES_H */
