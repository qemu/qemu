/*
 *  Generic thunking code to convert data between host and target CPU
 *
 *  Copyright (c) 2003 Fabrice Bellard
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
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>

#include "qemu.h"
#include "thunk.h"

//#define DEBUG

#define MAX_STRUCTS 128

/* XXX: make it dynamic */
StructEntry struct_entries[MAX_STRUCTS];

static const argtype *thunk_type_next_ptr(const argtype *type_ptr);

static inline const argtype *thunk_type_next(const argtype *type_ptr)
{
    int type;

    type = *type_ptr++;
    switch(type) {
    case TYPE_CHAR:
    case TYPE_SHORT:
    case TYPE_INT:
    case TYPE_LONGLONG:
    case TYPE_ULONGLONG:
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
        return type_ptr;
    case TYPE_PTR:
        return thunk_type_next_ptr(type_ptr);
    case TYPE_ARRAY:
        return thunk_type_next_ptr(type_ptr + 1);
    case TYPE_STRUCT:
        return type_ptr + 1;
    default:
        return NULL;
    }
}

static const argtype *thunk_type_next_ptr(const argtype *type_ptr)
{
    return thunk_type_next(type_ptr);
}

void thunk_register_struct(int id, const char *name, const argtype *types)
{
    const argtype *type_ptr;
    StructEntry *se;
    int nb_fields, offset, max_align, align, size, i, j;

    se = struct_entries + id;

    /* first we count the number of fields */
    type_ptr = types;
    nb_fields = 0;
    while (*type_ptr != TYPE_NULL) {
        type_ptr = thunk_type_next(type_ptr);
        nb_fields++;
    }
    se->field_types = types;
    se->nb_fields = nb_fields;
    se->name = name;
#ifdef DEBUG
    printf("struct %s: id=%d nb_fields=%d\n",
           se->name, id, se->nb_fields);
#endif
    /* now we can alloc the data */

    for(i = 0;i < 2; i++) {
        offset = 0;
        max_align = 1;
        se->field_offsets[i] = malloc(nb_fields * sizeof(int));
        type_ptr = se->field_types;
        for(j = 0;j < nb_fields; j++) {
            size = thunk_type_size(type_ptr, i);
            align = thunk_type_align(type_ptr, i);
            offset = (offset + align - 1) & ~(align - 1);
            se->field_offsets[i][j] = offset;
            offset += size;
            if (align > max_align)
                max_align = align;
            type_ptr = thunk_type_next(type_ptr);
        }
        offset = (offset + max_align - 1) & ~(max_align - 1);
        se->size[i] = offset;
        se->align[i] = max_align;
#ifdef DEBUG
        printf("%s: size=%d align=%d\n",
               i == THUNK_HOST ? "host" : "target", offset, max_align);
#endif
    }
}

void thunk_register_struct_direct(int id, const char *name,
                                  const StructEntry *se1)
{
    StructEntry *se;
    se = struct_entries + id;
    *se = *se1;
    se->name = name;
}


/* now we can define the main conversion functions */
const argtype *thunk_convert(void *dst, const void *src,
                             const argtype *type_ptr, int to_host)
{
    int type;

    type = *type_ptr++;
    switch(type) {
    case TYPE_CHAR:
        *(uint8_t *)dst = *(uint8_t *)src;
        break;
    case TYPE_SHORT:
        *(uint16_t *)dst = tswap16(*(uint16_t *)src);
        break;
    case TYPE_INT:
        *(uint32_t *)dst = tswap32(*(uint32_t *)src);
        break;
    case TYPE_LONGLONG:
    case TYPE_ULONGLONG:
        *(uint64_t *)dst = tswap64(*(uint64_t *)src);
        break;
#if HOST_LONG_BITS == 32 && TARGET_ABI_BITS == 32
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
        *(uint32_t *)dst = tswap32(*(uint32_t *)src);
        break;
#elif HOST_LONG_BITS == 64 && TARGET_ABI_BITS == 32
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
        if (to_host) {
            if (type == TYPE_LONG) {
                /* sign extension */
                *(uint64_t *)dst = (int32_t)tswap32(*(uint32_t *)src);
            } else {
                *(uint64_t *)dst = tswap32(*(uint32_t *)src);
            }
        } else {
            *(uint32_t *)dst = tswap32(*(uint64_t *)src & 0xffffffff);
        }
        break;
#elif HOST_LONG_BITS == 64 && TARGET_ABI_BITS == 64
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
        *(uint64_t *)dst = tswap64(*(uint64_t *)src);
        break;
#elif HOST_LONG_BITS == 32 && TARGET_ABI_BITS == 64
    case TYPE_LONG:
    case TYPE_ULONG:
    case TYPE_PTRVOID:
        if (to_host) {
            *(uint32_t *)dst = tswap64(*(uint64_t *)src);
        } else {
            if (type == TYPE_LONG) {
                /* sign extension */
                *(uint64_t *)dst = tswap64(*(int32_t *)src);
            } else {
                *(uint64_t *)dst = tswap64(*(uint32_t *)src);
            }
        }
        break;
#else
#warning unsupported conversion
#endif
    case TYPE_ARRAY:
        {
            int array_length, i, dst_size, src_size;
            const uint8_t *s;
            uint8_t  *d;

            array_length = *type_ptr++;
            dst_size = thunk_type_size(type_ptr, to_host);
            src_size = thunk_type_size(type_ptr, 1 - to_host);
            d = dst;
            s = src;
            for(i = 0;i < array_length; i++) {
                thunk_convert(d, s, type_ptr, to_host);
                d += dst_size;
                s += src_size;
            }
            type_ptr = thunk_type_next(type_ptr);
        }
        break;
    case TYPE_STRUCT:
        {
            int i;
            const StructEntry *se;
            const uint8_t *s;
            uint8_t  *d;
            const argtype *field_types;
            const int *dst_offsets, *src_offsets;

            se = struct_entries + *type_ptr++;
            if (se->convert[0] != NULL) {
                /* specific conversion is needed */
                (*se->convert[to_host])(dst, src);
            } else {
                /* standard struct conversion */
                field_types = se->field_types;
                dst_offsets = se->field_offsets[to_host];
                src_offsets = se->field_offsets[1 - to_host];
                d = dst;
                s = src;
                for(i = 0;i < se->nb_fields; i++) {
                    field_types = thunk_convert(d + dst_offsets[i],
                                                s + src_offsets[i],
                                                field_types, to_host);
                }
            }
        }
        break;
    default:
        fprintf(stderr, "Invalid type 0x%x\n", type);
        break;
    }
    return type_ptr;
}

/* from em86 */

/* Utility function: Table-driven functions to translate bitmasks
 * between X86 and Alpha formats...
 */
unsigned int target_to_host_bitmask(unsigned int x86_mask,
                                    bitmask_transtbl * trans_tbl)
{
    bitmask_transtbl *	btp;
    unsigned int	alpha_mask = 0;

    for(btp = trans_tbl; btp->x86_mask && btp->alpha_mask; btp++) {
	if((x86_mask & btp->x86_mask) == btp->x86_bits) {
	    alpha_mask |= btp->alpha_bits;
	}
    }
    return(alpha_mask);
}

unsigned int host_to_target_bitmask(unsigned int alpha_mask,
                                    bitmask_transtbl * trans_tbl)
{
    bitmask_transtbl *	btp;
    unsigned int	x86_mask = 0;

    for(btp = trans_tbl; btp->x86_mask && btp->alpha_mask; btp++) {
	if((alpha_mask & btp->alpha_mask) == btp->alpha_bits) {
	    x86_mask |= btp->x86_bits;
	}
    }
    return(x86_mask);
}

#ifndef NO_THUNK_TYPE_SIZE
int thunk_type_size_array(const argtype *type_ptr, int is_host)
{
    return thunk_type_size(type_ptr, is_host);
}

int thunk_type_align_array(const argtype *type_ptr, int is_host)
{
    return thunk_type_align(type_ptr, is_host);
}
#endif /* ndef NO_THUNK_TYPE_SIZE */
