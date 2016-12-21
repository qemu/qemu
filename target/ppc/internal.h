/*
 *  PowerPC interal definitions for qemu.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef PPC_INTERNAL_H
#define PPC_INTERNAL_H

#define FUNC_MASK(name, ret_type, size, max_val)                  \
static inline ret_type name(uint##size##_t start,                 \
                              uint##size##_t end)                 \
{                                                                 \
    ret_type ret, max_bit = size - 1;                             \
                                                                  \
    if (likely(start == 0)) {                                     \
        ret = max_val << (max_bit - end);                         \
    } else if (likely(end == max_bit)) {                          \
        ret = max_val >> start;                                   \
    } else {                                                      \
        ret = (((uint##size##_t)(-1ULL)) >> (start)) ^            \
            (((uint##size##_t)(-1ULL) >> (end)) >> 1);            \
        if (unlikely(start > end)) {                              \
            return ~ret;                                          \
        }                                                         \
    }                                                             \
                                                                  \
    return ret;                                                   \
}

#if defined(TARGET_PPC64)
FUNC_MASK(MASK, target_ulong, 64, UINT64_MAX);
#else
FUNC_MASK(MASK, target_ulong, 32, UINT32_MAX);
#endif
FUNC_MASK(mask_u32, uint32_t, 32, UINT32_MAX);
FUNC_MASK(mask_u64, uint64_t, 64, UINT64_MAX);

#endif /* PPC_INTERNAL_H */
