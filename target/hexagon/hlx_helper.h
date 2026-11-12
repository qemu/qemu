/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HEXAGON_HLX_HELPER_H
#define HEXAGON_HLX_HELPER_H

#include "cpu.h"

void hlx_load_vec_ra(CPUHexagonState *env, void *dst_void,
                     target_ulong ea, uint32_t above, uint32_t below,
                     uintptr_t ra);

void hlx_store_vec_ra(CPUHexagonState *env, target_ulong ea,
                      void *src_void, uint32_t above, uint32_t below,
                      uintptr_t ra);

#endif
