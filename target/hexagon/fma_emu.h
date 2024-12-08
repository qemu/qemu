/*
 *  Copyright(c) 2019-2022 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef HEXAGON_FMA_EMU_H
#define HEXAGON_FMA_EMU_H

static inline bool is_finite(float64 x)
{
    return !float64_is_any_nan(x) && !float64_is_infinity(x);
}

int32_t float64_getexp(float64 f64);
static inline uint32_t float32_getexp_raw(float32 f32)
{
    return extract32(f32, 23, 8);
}
int32_t float32_getexp(float32 f32);
float32 infinite_float32(uint8_t sign);
float64 internal_mpyhh(float64 a, float64 b,
                       unsigned long long int accumulated,
                       float_status *fp_status);

#endif
