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

#ifndef _MGL_MAPBO_H
#define _MGL_MAPBO_H

typedef struct {
    int idx;
    int lvl;
    uintptr_t hva;
    uintptr_t gpa;
    uint32_t mused;
    uint32_t mapsz;
    uint32_t offst;
    uint32_t range;
    uint32_t acc;
} mapbufo_t;

void InitBufObj(void);
mapbufo_t *LookupBufObj(const int);
int FreeBufObj(const int);
int MapBufObjGpa(mapbufo_t *);

void InitSyncObj(void);
uint32_t AddSyncObj(const uintptr_t);
uintptr_t LookupSyncObj(const uint32_t);
uintptr_t DeleteSyncObj(const uintptr_t);

#endif //_MGL_MAPBO_H

