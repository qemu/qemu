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

#ifndef HEXAGON_CONV_EMU_H
#define HEXAGON_CONV_EMU_H

uint64_t conv_sf_to_8u(float32 in, float_status *fp_status);
uint32_t conv_sf_to_4u(float32 in, float_status *fp_status);
int64_t conv_sf_to_8s(float32 in, float_status *fp_status);
int32_t conv_sf_to_4s(float32 in, float_status *fp_status);

uint64_t conv_df_to_8u(float64 in, float_status *fp_status);
uint32_t conv_df_to_4u(float64 in, float_status *fp_status);
int64_t conv_df_to_8s(float64 in, float_status *fp_status);
int32_t conv_df_to_4s(float64 in, float_status *fp_status);

#endif
