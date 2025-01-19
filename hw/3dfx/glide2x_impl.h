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

#ifndef GLIDE2X_IMPL_H
#define GLIDE2X_IMPL_H

#include <stdint.h>

#include "glidewnd.h"
#include "g2xfuncs.h"
#include "szgrdata.h"

#define GLIDEPT_MM_BASE 0xfbdff000

#define GR_RESOLUTION_320x200   0x0
#define GR_RESOLUTION_320x240   0x1
#define GR_RESOLUTION_400x256   0x2
#define GR_RESOLUTION_512x384   0x3
#define GR_RESOLUTION_640x200   0x4
#define GR_RESOLUTION_640x350   0x5
#define GR_RESOLUTION_640x400   0x6
#define GR_RESOLUTION_640x480   0x7
#define GR_RESOLUTION_800x600   0x8
#define GR_RESOLUTION_960x720   0x9
#define GR_RESOLUTION_856x480   0xa
#define GR_RESOLUTION_512x256   0xb
#define GR_RESOLUTION_1024x768  0xC
#define GR_RESOLUTION_1280x1024 0xD
#define GR_RESOLUTION_1600x1200 0xE
#define GR_RESOLUTION_400x300   0xF

#define GR_TEXTABLE_PALETTE     0x2

#define GR_TEXFMT_YIQ_422       0x1
#define GR_TEXFMT_P_8           0x5 /* 8-bit palette */
#define GR_TEXFMT_AYIQ_8422     0x9
#define GR_TEXFMT_AP_88         0xe /* 8-bit alpha 8-bit palette */

#define GR_CONTROL_ACTIVATE     0x1
#define GR_CONTROL_DEACTIVATE   0x2
#define GR_PASSTHRU_SHOW_SST1   0x1
#define GR_PASSTHRU_SHOW_VGA    0x0
#define GR_PASSTHRU             0x3

typedef struct {
    uint32_t small;
    uint32_t large;
    uint32_t aspect;
    uint32_t format;
    void *data;
} wrTexInfo;

typedef struct {
    uint32_t small;
    uint32_t large;
    uint32_t aspect;
    uint32_t format;
    uint32_t data;
} wrgTexInfo;

typedef struct {
    uint32_t width, height;
    uint32_t small, large;
    uint32_t aspect;
    uint32_t format;
} wr3dfHeader;

typedef struct {
    uint8_t header[SIZE_GU3DFHEADER];
    uint8_t table[SIZE_GUTEXTABLE];
    void *data;
    uint32_t mem_required;
} wr3dfInfo;

typedef struct {
    uint8_t header[SIZE_GU3DFHEADER];
    uint8_t table[SIZE_GUTEXTABLE];
    uint32_t data;
    uint32_t mem_required;
} wrg3dfInfo;

typedef struct {
    wr3dfInfo *info3df;
    wrTexInfo *texInfo;
    void *fbuf;
    uint32_t flen;
} wrTexStruct;

typedef struct {
    int size;
    void *lfbPtr;
    uint32_t stride;
    uint32_t writeMode;
    uint32_t origin;
} wrLfbInfo;

typedef struct {
    int size;
    uint32_t lfbPtr;
    uint32_t stride;
    uint32_t writeMode;
    uint32_t origin;
} wrgLfbInfo;

int GRFEnumArgsCnt(int);
uint32_t texTableValid(uint32_t);
uint32_t wrReadRegion(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uintptr_t arg6);
uint32_t wrWriteRegion(uint32_t arg0, uint32_t arg1, uint32_t arg2, uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6, uintptr_t arg7);
uintptr_t wrGetProcAddress(uintptr_t);
const char *wrGetString(uint32_t);
const char *getGRFuncStr(int);

#ifndef CONSOLE_H
void glide_renderer_stat(const int);
#endif //CONSOLE_H
void doGlideFunc(int, uint32_t *, uintptr_t *, uintptr_t *, int);
void conf_glide2x(const uint32_t, const int);
void cwnd_glide2x(void *, void *, void *);
int init_glide2x(const char *);
void fini_glide2x(void);
int init_g3ext(void);

#endif // GLIDE2X_IMPL_H

