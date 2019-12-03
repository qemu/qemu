/*
 *  Copyright (c) 2019 Qualcomm Innovation Center, Inc. All Rights Reserved.
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

#ifndef CONV_EMU_H
#define CONV_EMU_H 1
#include "global_types.h"
#include "arch_types.h"
size8u_t conv_sf_to_8u(float in);
size4u_t conv_sf_to_4u(float in);
size8s_t conv_sf_to_8s(float in);
size4s_t conv_sf_to_4s(float in);

size8u_t conv_df_to_8u(double in);
size4u_t conv_df_to_4u(double in);
size8s_t conv_df_to_8s(double in);
size4s_t conv_df_to_4s(double in);

double conv_8u_to_df(size8u_t in);
double conv_4u_to_df(size4u_t in);
double conv_8s_to_df(size8s_t in);
double conv_4s_to_df(size4s_t in);

float conv_8u_to_sf(size8u_t in);
float conv_4u_to_sf(size4u_t in);
float conv_8s_to_sf(size8s_t in);
float conv_4s_to_sf(size4s_t in);

float conv_df_to_sf(double in);	/* Implemented in fma_emu.c */

static inline double conv_sf_to_df(float in)
{
	return in;
}

hf_t conv_df_to_hf(double in);
static inline hf_t conv_sf_to_hf(float in) { return conv_df_to_hf(in); }
float conv_hf_to_sf(hf_t in);

#endif
