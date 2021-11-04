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

#ifndef HEXAGON_MMVEC_H
#define HEXAGON_MMVEC_H

#define MAX_VEC_SIZE_LOGBYTES 7
#define MAX_VEC_SIZE_BYTES  (1 << MAX_VEC_SIZE_LOGBYTES)

#define NUM_VREGS           32
#define NUM_QREGS           4

typedef uint32_t VRegMask; /* at least NUM_VREGS bits */
typedef uint32_t QRegMask; /* at least NUM_QREGS bits */

#define VECTOR_SIZE_BYTE    (fVECSIZE())

typedef union {
    uint64_t ud[MAX_VEC_SIZE_BYTES / 8];
    int64_t   d[MAX_VEC_SIZE_BYTES / 8];
    uint32_t uw[MAX_VEC_SIZE_BYTES / 4];
    int32_t   w[MAX_VEC_SIZE_BYTES / 4];
    uint16_t uh[MAX_VEC_SIZE_BYTES / 2];
    int16_t   h[MAX_VEC_SIZE_BYTES / 2];
    uint8_t  ub[MAX_VEC_SIZE_BYTES / 1];
    int8_t    b[MAX_VEC_SIZE_BYTES / 1];
} MMVector;

typedef union {
    uint64_t ud[2 * MAX_VEC_SIZE_BYTES / 8];
    int64_t   d[2 * MAX_VEC_SIZE_BYTES / 8];
    uint32_t uw[2 * MAX_VEC_SIZE_BYTES / 4];
    int32_t   w[2 * MAX_VEC_SIZE_BYTES / 4];
    uint16_t uh[2 * MAX_VEC_SIZE_BYTES / 2];
    int16_t   h[2 * MAX_VEC_SIZE_BYTES / 2];
    uint8_t  ub[2 * MAX_VEC_SIZE_BYTES / 1];
    int8_t    b[2 * MAX_VEC_SIZE_BYTES / 1];
    MMVector v[2];
} MMVectorPair;

typedef union {
    uint64_t ud[MAX_VEC_SIZE_BYTES / 8 / 8];
    int64_t   d[MAX_VEC_SIZE_BYTES / 8 / 8];
    uint32_t uw[MAX_VEC_SIZE_BYTES / 4 / 8];
    int32_t   w[MAX_VEC_SIZE_BYTES / 4 / 8];
    uint16_t uh[MAX_VEC_SIZE_BYTES / 2 / 8];
    int16_t   h[MAX_VEC_SIZE_BYTES / 2 / 8];
    uint8_t  ub[MAX_VEC_SIZE_BYTES / 1 / 8];
    int8_t    b[MAX_VEC_SIZE_BYTES / 1 / 8];
} MMQReg;

typedef struct {
    MMVector data;
    DECLARE_BITMAP(mask, MAX_VEC_SIZE_BYTES);
    target_ulong va[MAX_VEC_SIZE_BYTES];
    bool op;
    int op_size;
} VTCMStoreLog;


/* Types of vector register assignment */
typedef enum {
    EXT_DFL,      /* Default */
    EXT_NEW,      /* New - value used in the same packet */
    EXT_TMP       /* Temp - value used but not stored to register */
} VRegWriteType;

#endif
