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

#ifndef GLIDEWND_H
#define GLIDEWND_H

int GRFifoTrace(void);
int GRFuncTrace(void);
int glide_fpslimit(void);
int glide_vsyncoff(void);
int glide_lfbmerge(void);
int glide_lfbdirty(void);
int glide_lfbnoaux(void);
int glide_lfbmode(void);
void glide_winres(const int, int *, int *);
int stat_window(const int, void *);
void init_window(const int, const char *, void *);
void fini_window(void *);

typedef struct {
    int activate;
    uint32_t *arg;
    uint32_t FEnum;
    uintptr_t GrContext;
} window_cb;

typedef struct {
    uintptr_t hva;
    uint32_t mapsz;
    uint32_t acc;
} mapbufo_t;

int glide_mapbufo(mapbufo_t *, int);

typedef struct _perfstat {
    void (*stat)(void);
    void (*last)(void);
} PERFSTAT, *PPERFSTAT;

void glidestat(PPERFSTAT);

#endif // GLIDEWND_H
