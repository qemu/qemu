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

#ifndef HEXAGON_HEX_ARCH_TYPES_H
#define HEXAGON_HEX_ARCH_TYPES_H

#include "qemu/osdep.h"
#include "mmvec/mmvec.h"
#include "qemu/int128.h"

/*
 * These types are used by the code imported from the Hexagon
 * architecture library.
 */
typedef uint8_t     size1u_t;
typedef int8_t      size1s_t;
typedef uint16_t    size2u_t;
typedef int16_t     size2s_t;
typedef uint32_t    size4u_t;
typedef int32_t     size4s_t;
typedef uint64_t    size8u_t;
typedef int64_t     size8s_t;
typedef Int128      size16s_t;

typedef MMVector          mmvector_t;
typedef MMVectorPair      mmvector_pair_t;
typedef MMQReg            mmqret_t;

#endif
