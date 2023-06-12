/*
 * QEMU Hyper-V Dynamic Memory Protocol driver
 *
 * Copyright (C) 2020-2023 Oracle and/or its affiliates.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_HYPERV_HV_BALLOON_INTERNAL_H
#define HW_HYPERV_HV_BALLOON_INTERNAL_H

#include "qemu/osdep.h"

#define HV_BALLOON_PFN_SHIFT 12
#define HV_BALLOON_PAGE_SIZE (1 << HV_BALLOON_PFN_SHIFT)

#define SUM_OVERFLOW_U64(in1, in2) ((in1) > UINT64_MAX - (in2))
#define SUM_SATURATE_U64(in1, in2)              \
    ({                                          \
        uint64_t _in1 = (in1), _in2 = (in2);    \
        uint64_t _result;                       \
                                                \
        if (!SUM_OVERFLOW_U64(_in1, _in2)) {    \
            _result = _in1 + _in2;              \
        } else {                                \
            _result = UINT64_MAX;               \
        }                                       \
                                                \
        _result;                                \
    })

#endif
