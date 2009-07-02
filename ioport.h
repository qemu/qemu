/*
 * defines ioport related functions
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
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA  02110-1301 USA
 */

/**************************************************************************
 * IO ports API
 */

#ifndef IOPORT_H
#define IOPORT_H

#include "qemu-common.h"

#define MAX_IOPORTS     (64 * 1024)
#define IOPORTS_MASK    (MAX_IOPORTS - 1)

/* These should really be in isa.h, but are here to make pc.h happy.  */
typedef void (IOPortWriteFunc)(void *opaque, uint32_t address, uint32_t data);
typedef uint32_t (IOPortReadFunc)(void *opaque, uint32_t address);

int register_ioport_read(int start, int length, int size,
                         IOPortReadFunc *func, void *opaque);
int register_ioport_write(int start, int length, int size,
                          IOPortWriteFunc *func, void *opaque);
void isa_unassign_ioport(int start, int length);


/* NOTE: as these functions may be even used when there is an isa
   brige on non x86 targets, we always defined them */
#if !defined(NO_CPU_IO_DEFS) && defined(NEED_CPU_H)
void cpu_outb(CPUState *env, int addr, int val);
void cpu_outw(CPUState *env, int addr, int val);
void cpu_outl(CPUState *env, int addr, int val);
int cpu_inb(CPUState *env, int addr);
int cpu_inw(CPUState *env, int addr);
int cpu_inl(CPUState *env, int addr);
#endif

#endif /* IOPORT_H */
