/*
 * QEMU MESA GL Pass-Through
 *
 *  Copyright (c) 2020
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this library;
 * if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"

#include "mglfuncs.h"
#include "mglvarry.h"

typedef struct _vertArry {
    uint32_t tagLo;
    uint32_t tagHi;
    uint8_t *ptr;
    struct _vertArry *next;
} VERTARRY, * PVERTARRY;

static PVERTARRY vertexArry = NULL;

static void *LookupVertArry(PVERTARRY *pArry, uint32_t handle, uint32_t size)
{
    PVERTARRY p = *pArry;

    if (handle == 0)
        return NULL;

    while (p) {
        if (((handle >= p->tagLo) && (handle < p->tagHi)) &&
            ((p->tagHi - handle) >= (size >> 1)))
            break;
        if (p->next == NULL)
            break;
        p = p->next;
    }
    
    if (p == NULL) {
        p = g_new(VERTARRY, 1);
        p->tagLo = (handle > size)? (handle - size):PAGE_SIZE;
        p->tagHi = p->tagLo + (size << 1);
        p->ptr = g_malloc(size << 1);
        p->next = NULL;
        fprintf(stderr, " alloc vaddr %08x-%08x from hndl %08x\n", p->tagLo, p->tagHi, handle);
        *pArry = p;
    }
    else {
        if (((handle >= p->tagLo) && (handle < p->tagHi)) && 
            ((p->tagHi - handle) >= (size >> 1))) { }
        else {
            p->next = g_new(VERTARRY, 1);
            p = p->next;
            p->tagLo = (handle > size)? (handle - size):PAGE_SIZE;
            p->tagHi = p->tagLo + (size << 1);
            p->ptr = g_malloc(size << 1);
            fprintf(stderr, " alloc vaddr %08x-%08x from hndl %08x\n", p->tagLo, p->tagHi, handle);
            p->next = NULL;
        }
    }

    return p->ptr + (handle - p->tagLo);
}

static int FreeVertArry(PVERTARRY *pArry)
{
    PVERTARRY p = *pArry;
    int cnt = 0;
    while (p) {
        PVERTARRY next = p->next;
        g_free(p->ptr);
        g_free(p);
        p = next;
        cnt++;
    }
    *pArry = p;
    return cnt;
}

void *LookupVertex(uint32_t handle, uint32_t size) { return LookupVertArry(&vertexArry, handle, size); }
int FreeVertex(void) { return FreeVertArry(&vertexArry); }

