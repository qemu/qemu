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

#ifndef HEXAGON_ARCH_TYPES_H
#define HEXAGON_ARCH_TYPES_H

/*
 * These types are used by the code generated from the Hexagon
 * architecture library.
 */
typedef unsigned char size1u_t;
typedef char size1s_t;
typedef unsigned short int size2u_t;
typedef short size2s_t;
typedef unsigned int size4u_t;
typedef int size4s_t;
typedef unsigned long long int size8u_t;
typedef long long int size8s_t;
typedef size8u_t paddr_t;
typedef size4u_t vaddr_t;
typedef size8u_t pcycles_t;

typedef struct size16s {
    size8s_t hi;
    size8u_t lo;
} size16s_t;

#endif
