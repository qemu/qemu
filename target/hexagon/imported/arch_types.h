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

/*
 *
 * This file defines the architectural  type information
 *
 * 
 *
 */

#ifndef _ARCH_TYPES_H_
#define _ARCH_TYPES_H_

#include "global_types.h"

/* Data types */
typedef size4u_t Reg_t;
typedef size4s_t reg_t;
typedef size4u_t VA_t;
typedef size8u_t PA_t;

typedef union {
	size2u_t i;
#ifndef SLOWLARIS
	struct {
		size2u_t mant:10;
		size2u_t exp:5;
		size2u_t sign:1;
	} x;
#else
	struct {
		size2u_t sign:1;
		size2u_t exp:5;
		size2u_t mant:10;
	} x;
#endif
} hf_t;

#endif							/* #ifndef _ARCH_TYPES_H_ */
