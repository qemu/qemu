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

#include "exec/target_long.h"
#include "qemu/bitmap.h"
#include "qemu/bitops.h"

#define MAX_VEC_SIZE_LOGBYTES 7
#define MAX_VEC_SIZE_BYTES  (1 << MAX_VEC_SIZE_LOGBYTES)

#define NUM_VREGS           32
#define NUM_QREGS           4

typedef uint32_t VRegMask; /* at least NUM_VREGS bits */
typedef uint32_t QRegMask; /* at least NUM_QREGS bits */

#define VECTOR_SIZE_BYTE    (fVECSIZE())

/*
 * Fill value for a vector's qfloat extended-precision bits (MMVector.ext,
 * below) whenever an instruction that isn't qfloat-aware writes that
 * vector: architecturally the ext bits are unspecified in that case, and
 * this repeating, recognizably-not-zero byte pattern is used instead of an
 * all-zero fill so stray reads of stale ext state show up distinctly
 * rather than silently looking like a valid (all-zero) qfloat result.
 */
#define V_EXTENDED_BYTEVAL 0x0a

typedef struct {
    union {
        uint64_t ud[MAX_VEC_SIZE_BYTES / 8];
        int64_t   d[MAX_VEC_SIZE_BYTES / 8];
        uint32_t uw[MAX_VEC_SIZE_BYTES / 4];
        int32_t   w[MAX_VEC_SIZE_BYTES / 4];
        uint16_t uh[MAX_VEC_SIZE_BYTES / 2];
        int16_t   h[MAX_VEC_SIZE_BYTES / 2];
        uint8_t  ub[MAX_VEC_SIZE_BYTES / 1];
        int8_t    b[MAX_VEC_SIZE_BYTES / 1];
        float32  sf[MAX_VEC_SIZE_BYTES / 4];
        float16  hf[MAX_VEC_SIZE_BYTES / 2];
        bfloat16 bf[MAX_VEC_SIZE_BYTES / 2];
        /* qfloat "visible" mantissa/exponent, sans the extended bits below */
        int32_t  qf32[MAX_VEC_SIZE_BYTES / 4];
        int16_t  qf16[MAX_VEC_SIZE_BYTES / 2];
    };
    /*
     * Extended precision bits for qfloat: one byte per qf32 element (only
     * the low 4 bits, the LREQ nibble, are meaningful); two qf16 elements
     * share a byte, 2 bits (LR) each.  Any instruction that overwrites this
     * vector without itself being qfloat-aware fills these with
     * V_EXTENDED_BYTEVAL, since they're only meaningful as the tail end of
     * a qfloat computation.
     */
    uint8_t ext[MAX_VEC_SIZE_BYTES / 4];
} MMVector;

typedef struct {
    MMVector v[2];
} MMVectorPair;

/*
 * val is expected to already be masked to the width of the extended-bits
 * field being written (1 bit for a qf32 element, 2 bits for a qf16
 * element); size selects which.
 */
static inline void set_extended_bits(MMVector *v, int i, int size, uint8_t val)
{
    if (size == 32) {
        v->ext[i] = val;
    } else {
        /* Two qf16 elements share an ext byte: low 2 bits, then high 2 */
        if (i % 2 == 1) {
            v->ext[i / 2] = ((val & 0x3) << 2) | (v->ext[i / 2] & 0x3);
        } else {
            v->ext[i / 2] = (v->ext[i / 2] & 0xc) | (val & 0x3);
        }
    }
}

static inline uint8_t get_extended_bits(const MMVector *v, int i, int size)
{
    if (size == 32) {
        return v->ext[i];
    } else {
        if (i % 2 == 1) {
            return (v->ext[i / 2] >> 2) & 0x3;
        } else {
            return v->ext[i / 2] & 0x3;
        }
    }
}

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

static inline uint8_t hexagon_mmvec_get_byte(const MMVector *v, size_t index)
{
    return extract64(v->ud[index / 8], (index % 8) * 8, 8);
}

static inline void hexagon_mmvec_set_byte(MMVector *v, size_t index,
                                          uint8_t value)
{
    v->ud[index / 8] = deposit64(v->ud[index / 8], (index % 8) * 8, 8,
                                 value);
}

static inline uint8_t hexagon_mmqreg_get_byte(const MMQReg *q, size_t index)
{
    return extract32(q->uw[index / 4], (index % 4) * 8, 8);
}

typedef struct {
    MMVector data;
    DECLARE_BITMAP(mask, sizeof_field(MMVector, ub));
    uint32_t va[sizeof_field(MMVector, ub)];
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
