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
#include "mglmapbo.h"

#if defined(__x86_64__)
#include <nmmintrin.h>
#elif defined(__aarch64__)
#define _mm_crc32_u64 __builtin_arm_crc32cd
#else
#define _mm_crc32_u64(a,b) (b & INT32_MAX)
#endif

typedef struct _bufobj {
    mapbufo_t bo;
    struct _bufobj *next;
} MAPBO, *PMAPBO;

typedef struct _syncobj {
    uintptr_t sync;
    uint32_t g_sync;
    struct _syncobj *next;
} SYNCO, *PSYNCO;

static PMAPBO pbufo = NULL;
static PSYNCO psynco = NULL;

void InitSyncObj(void) {
    PSYNCO p = psynco;
    while (p) {
        PSYNCO next = p->next;
        g_free(p);
        p = next;
    }
    psynco = p;
}

#ifdef __SSE4_2__
#include <smmintrin.h>
#endif

uint32_t AddSyncObj(const uintptr_t sync) {
    PSYNCO p = psynco;

    if (!sync)
        return 0;

    if (!p) {
        p = g_new0(SYNCO, 1);
        p->sync = sync;
        p->g_sync = 0; // Initialize g_sync
        #ifdef __SSE4_2__
        p->g_sync = _mm_crc32_u64(p->g_sync, p->sync);
        #else
        // Alternative implementation if SSE4.2 is not supported
        #endif
        psynco = p;
    } else {
        while (p->sync != sync && p->next)
            p = p->next;
        if (p->sync != sync) {
            p->next = g_new0(SYNCO, 1);
            p = p->next;
            p->sync = sync;
            p->g_sync = 0; // Initialize g_sync
            #ifdef __SSE4_2__
            p->g_sync = _mm_crc32_u64(p->g_sync, p->sync);
            #else
            // Alternative implementation if SSE4.2 is not supported
            #endif
        }
    }

    return p->g_sync;
}

uintptr_t LookupSyncObj(const uint32_t g_sync)
{
    PSYNCO p = psynco;

    if (!p)
        return INT32_MAX;

    while (p->g_sync != g_sync && p->next)
        p = p->next;

    if (p->g_sync != g_sync)
        return INT32_MAX;

    return p->sync;
}

uintptr_t DeleteSyncObj(const uintptr_t sync)
{
    PSYNCO p = psynco, q = NULL;

    if (p) {
        while (p->sync != sync && p->next) {
            q = p;
            p = p->next;
        }
        if (p->sync == sync) {
            if (!q) {
                q = p->next;
                psynco = q;
            }
            else
                q->next = p->next;
            g_free(p);
        }
    }
    return sync;
}

void InitBufObj(void)
{
    PMAPBO p = pbufo;
    while (p) {
        PMAPBO next = p->next;
        g_free(p);
        p = next;
    }
    pbufo = p;
}

mapbufo_t *LookupBufObj(const int idx)
{
    PMAPBO p = pbufo;

    if (p == NULL) {
        p = g_new0(MAPBO, 1);
        p->bo.idx = idx;
        pbufo = p;
    }
    else {
        while ((idx != p->bo.idx) && p->next)
            p = p->next;
        if (idx != p->bo.idx) {
            p->next = g_new0(MAPBO, 1);
            p = p->next;
            p->bo.idx = idx;
        }
    }
    return &p->bo;
}

int FreeBufObj(const int idx)
{
    PMAPBO curr = pbufo, prev = NULL;

    int cnt;

    if (curr) {
        while ((idx != curr->bo.idx) && curr->next) {
            prev = curr;
            curr = curr->next;
        }
        if (idx == curr->bo.idx) {
            if (!prev) {
                prev = curr->next;
                pbufo = prev;
            }
            else
                prev->next = curr->next;
            g_free(curr);
        }
    }

    cnt = 0;
    curr = pbufo;

    while (curr) {
        cnt++;
        curr = curr->next;
    }
    return cnt;
}

int MapBufObjGpa(mapbufo_t *bufo)
{
    PMAPBO curr = pbufo;
    int ret = 0;

    bufo->gpa = bufo->hva & (MBUFO_SIZE - 1);
    if (bufo != &curr->bo) {
        uintptr_t addr_lo = MBUFO_SIZE - 1, addr_hi = 0;
        uint32_t bufo_sz = ALIGNBO(bufo->mapsz) + (uint32_t)(bufo->hva & (qemu_real_host_page_size() - 1));
        while (bufo != &curr->bo) {
            uint32_t curr_sz = curr->bo.mapsz + (uint32_t)(curr->bo.hva & (qemu_real_host_page_size() - 1));
            addr_lo = ((curr->bo.gpa & qemu_real_host_page_mask()) < addr_lo)?
                (curr->bo.gpa & qemu_real_host_page_mask()):addr_lo;
            addr_hi = (((curr->bo.gpa + curr_sz) & qemu_real_host_page_mask()) > addr_hi)?
                ((curr->bo.gpa + curr_sz) & qemu_real_host_page_mask()):addr_hi;
            curr = curr->next;
            ret++;
        }
        if (((bufo->gpa + bufo_sz) < addr_lo) || (bufo->gpa >= addr_hi))
            return ret;
        bufo->gpa = 0;
        if (!bufo->gpa && (addr_lo > bufo_sz))
            bufo->gpa = addr_lo - bufo_sz;
        if (!bufo->gpa && ((addr_hi + bufo_sz) < MBUFO_SIZE))
            bufo->gpa = addr_hi;
    }
    return ret;
}

