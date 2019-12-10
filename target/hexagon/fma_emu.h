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

#ifndef FMA_EMU_H
#define FMA_EMU_H

extern float internal_fmafx(float a_in, float b_in, float c_in, int scale);
extern float internal_fmaf(float a_in, float b_in, float c_in);
extern double internal_fma(double a_in, double b_in, double c_in);
extern double internal_fmax(double a_in, double b_in, double c_in, int scale);
extern float internal_mpyf(float a_in, float b_in);
extern double internal_mpy(double a_in, double b_in);
extern double internal_mpyhh(double a_in, double b_in,
                             unsigned long long int accumulated);

#endif
