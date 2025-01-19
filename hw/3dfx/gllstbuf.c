/*
 * QEMU 3Dfx Glide Pass-Through
 *
 *  Copyright (c) 2018-2020
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

#include "gllstbuf.h"

typedef struct _llstbuf {
    uint32_t id;
    int sz;
    uint8_t *st;
    struct _llstbuf *next;
} LLSTBUF, * PLLSTBUF;

static PLLSTBUF llGrState = NULL;
static PLLSTBUF llVtxLayout = NULL;

static void *LookupStBuf(PLLSTBUF *pbuf, int st_size, uint32_t handle)
{
    PLLSTBUF p = *pbuf;

    while (p) {
        if ((p->id == handle) && (p->sz == st_size))
            break;
        if (p->next == NULL)
            break;
        p = p->next;
    }
    
    if (p == NULL) {
        p = g_new(LLSTBUF, 1);
        p->id = handle;
        p->sz = st_size;
        p->st = g_malloc(p->sz);
        p->next = NULL;
        *pbuf = p;
    }
    else {
        if (!((p->id == handle) && (p->sz == st_size))) {
            p->next = g_new(LLSTBUF, 1);
            p = p->next;
            p->id = handle;
            p->sz = st_size;
            p->st = g_malloc(p->sz);
            p->next = NULL;
        }
    }

    return p->st;
}

static int FreeStBuf(PLLSTBUF *pbuf)
{
    PLLSTBUF p = *pbuf;
    int cnt = 0;
    while (p) {
        PLLSTBUF next = p->next;
        g_free(p->st);
        g_free(p);
        p = next;
        cnt++;
    }
    *pbuf = p;
    return cnt;
}

void *LookupGrState(uint32_t handle, int size)
{
    return LookupStBuf(&llGrState, size, handle);
}
void *LookupVtxLayout(uint32_t handle, int size)
{
    return LookupStBuf(&llVtxLayout, size, handle);
}
int FreeGrState(void)
{
    return FreeStBuf(&llGrState);
}
int FreeVtxLayout(void)
{
    return FreeStBuf(&llVtxLayout);
}

