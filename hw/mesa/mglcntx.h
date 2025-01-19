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

#ifndef MGLCNTX_H
#define MGLCNTX_H

#include "qemu/atomic.h"

void SetMesaFuncPtr(void *);
void *MesaGLGetProc(const char *);
int MGLExtIsAvail(const char *, const char *);
int MGLUpdateGuestBufo(mapbufo_t *, int);
void MGLTmpContext(void);
void MGLWndRelease(void);
int MGLMakeCurrent(uint32_t, int);
int MGLSwapBuffers(void);
int MGLChoosePixelFormat(void);
int MGLDescribePixelFormat(int, unsigned int, void *);
int MGLSetPixelFormat(int, const void *);
void MGLActivateHandler(const int, const int);
void MGLCursorDefine(int, int, int, int, const void *);
void MGLMouseWarp(const uint32_t);
int NumPbuffer(void);
int DrawableContext(void);
void MGLFuncHandler(const char *);
void MGLDeleteContext(int);
int MGLCreateContext(uint32_t);

int glwnd_ready(void);
void deactivateCancel(void);
void deactivateSched(const int);
void deactivateGuiRefSched(void);
int find_xstr(const char *, const char *);

typedef struct _perfstat {
    void (*stat)(void);
    void (*last)(void);
} PERFSTAT, *PPERFSTAT;

void mesastat(PPERFSTAT);

#endif // MGLCNTX_H
