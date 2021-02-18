/*
 *  Copyright(c) 2019-2021 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_ARCH_H
#define HEXAGON_ARCH_H

#include "qemu/int128.h"

uint64_t interleave(uint32_t odd, uint32_t even);
uint64_t deinterleave(uint64_t src);
uint32_t carry_from_add64(uint64_t a, uint64_t b, uint32_t c);
int32_t conv_round(int32_t a, int n);
void arch_fpop_start(CPUHexagonState *env);
void arch_fpop_end(CPUHexagonState *env);
int arch_sf_recip_common(float32 *Rs, float32 *Rt, float32 *Rd,
                         int *adjust, float_status *fp_status);
int arch_sf_invsqrt_common(float32 *Rs, float32 *Rd, int *adjust,
                          float_status *fp_status);

#endif
