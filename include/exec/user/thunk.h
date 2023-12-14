/*
 *  Generic thunking code to convert data between host and target CPU
 *
 *  Copyright (c) 2003 Fabrice Bellard
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef THUNK_H
#define THUNK_H

#include "cpu.h"
#include "exec/user/abitypes.h"

/* types enums definitions */

typedef enum argtype {
    TYPE_NULL,
    TYPE_CHAR,
    TYPE_SHORT,
    TYPE_INT,
    TYPE_LONG,
    TYPE_ULONG,
    TYPE_PTRVOID, /* pointer on unknown data */
    TYPE_LONGLONG,
    TYPE_ULONGLONG,
    TYPE_PTR,
    TYPE_ARRAY,
    TYPE_STRUCT,
    TYPE_OLDDEVT,
} argtype;

#define MK_PTR(type) TYPE_PTR, type
#define MK_ARRAY(type, size) TYPE_ARRAY, (int)(size), type
#define MK_STRUCT(id) TYPE_STRUCT, id

#define THUNK_TARGET 0
#define THUNK_HOST   1

typedef struct {
    /* standard struct handling */
    const argtype *field_types;
    int nb_fields;
    int *field_offsets[2];
    /* special handling */
    void (*convert[2])(void *dst, const void *src);
    void (*print)(void *arg);
    int size[2];
    int align[2];
    const char *name;
} StructEntry;

/* Translation table for bitmasks... */
typedef struct bitmask_transtbl {
    unsigned int target_mask;
    unsigned int target_bits;
    unsigned int host_mask;
    unsigned int host_bits;
} bitmask_transtbl;

void thunk_register_struct(int id, const char *name, const argtype *types);
void thunk_register_struct_direct(int id, const char *name,
                                  const StructEntry *se1);
const argtype *thunk_convert(void *dst, const void *src,
                             const argtype *type_ptr, int to_host);
const argtype *thunk_print(void *arg, const argtype *type_ptr);

extern StructEntry *struct_entries;

int thunk_type_size_array(const argtype *type_ptr, int is_host);
int thunk_type_align_array(const argtype *type_ptr, int is_host);

static inline int thunk_type_size(const argtype *type_ptr, int is_host)
{
    int type, size;
    const StructEntry *se;

    type = *type_ptr;
    switch(type) {
    case TYPE_CHAR:
        return 1;
    case TYPE_SHORT:
        return 2;
    case TYPE_INT:
        return 4;
    case TYPE_LONGLONG:
    case TYPE_ULONGLONG:
        return 8;
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
    case TYPE_PTR:
        if (is_host) {
            return sizeof(void *);
        } else {
            return TARGET_ABI_BITS / 8;
        }
        break;
    case TYPE_OLDDEVT:
        if (is_host) {
#if defined(HOST_X86_64)
            return 8;
#elif defined(HOST_MIPS) || defined(HOST_SPARC64)
            return 4;
#elif defined(HOST_PPC)
            return sizeof(void *);
#else
            return 2;
#endif
        } else {
#if defined(TARGET_X86_64)
            return 8;
#elif defined(TARGET_ALPHA) || defined(TARGET_IA64) || defined(TARGET_MIPS) || \
      defined(TARGET_PARISC) || defined(TARGET_SPARC64)
            return 4;
#elif defined(TARGET_PPC)
            return TARGET_ABI_BITS / 8;
#else
            return 2;
#endif
        }
        break;
    case TYPE_ARRAY:
        size = type_ptr[1];
        return size * thunk_type_size_array(type_ptr + 2, is_host);
    case TYPE_STRUCT:
        se = struct_entries + type_ptr[1];
        return se->size[is_host];
    default:
        g_assert_not_reached();
    }
}

static inline int thunk_type_align(const argtype *type_ptr, int is_host)
{
    int type;
    const StructEntry *se;

    type = *type_ptr;
    switch(type) {
    case TYPE_CHAR:
        return 1;
    case TYPE_SHORT:
        if (is_host) {
            return __alignof__(short);
        } else {
            return ABI_SHORT_ALIGNMENT;
        }
    case TYPE_INT:
        if (is_host) {
            return __alignof__(int);
        } else {
            return ABI_INT_ALIGNMENT;
        }
    case TYPE_LONGLONG:
    case TYPE_ULONGLONG:
        if (is_host) {
            return __alignof__(long long);
        } else {
            return ABI_LLONG_ALIGNMENT;
        }
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
    case TYPE_PTR:
        if (is_host) {
            return __alignof__(long);
        } else {
            return ABI_LONG_ALIGNMENT;
        }
        break;
    case TYPE_OLDDEVT:
        return thunk_type_size(type_ptr, is_host);
    case TYPE_ARRAY:
        return thunk_type_align_array(type_ptr + 2, is_host);
    case TYPE_STRUCT:
        se = struct_entries + type_ptr[1];
        return se->align[is_host];
    default:
        g_assert_not_reached();
    }
}

unsigned int target_to_host_bitmask_len(unsigned int target_mask,
                                        const bitmask_transtbl *trans_tbl,
                                        size_t trans_len);
unsigned int host_to_target_bitmask_len(unsigned int host_mask,
                                        const bitmask_transtbl * trans_tbl,
                                        size_t trans_len);

#define target_to_host_bitmask(M, T) \
    target_to_host_bitmask_len(M, T, ARRAY_SIZE(T))
#define host_to_target_bitmask(M, T) \
    host_to_target_bitmask_len(M, T, ARRAY_SIZE(T))

void thunk_init(unsigned int max_structs);

#endif
