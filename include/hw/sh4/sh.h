/*
 * Definitions for SH board emulation
 *
 * Copyright (c) 2005 Samuel Tardieu
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef QEMU_HW_SH_H
#define QEMU_HW_SH_H

#include "hw/sh4/sh_intc.h"
#include "target/sh4/cpu-qom.h"

#define A7ADDR(x) ((x) & 0x1fffffff)
#define P4ADDR(x) ((x) | 0xe0000000)

/* sh7750.c */
struct SH7750State;

struct SH7750State *sh7750_init(SuperHCPU *cpu, MemoryRegion *sysmem);

typedef struct {
    /* The callback will be triggered if any of the designated lines change */
    uint16_t portamask_trigger;
    uint16_t portbmask_trigger;
    /* Return 0 if no action was taken */
    int (*port_change_cb) (uint16_t porta, uint16_t portb,
                           uint16_t *periph_pdtra,
                           uint16_t *periph_portdira,
                           uint16_t *periph_pdtrb,
                           uint16_t *periph_portdirb);
} sh7750_io_device;

int sh7750_register_io_device(struct SH7750State *s,
                              sh7750_io_device *device);

/* sh_serial.c */
#define TYPE_SH_SERIAL "sh-serial"
#define SH_SERIAL_FEAT_SCIF (1 << 0)

/* sh7750.c */
qemu_irq sh7750_irl(struct SH7750State *s);

/* tc58128.c */
int tc58128_init(struct SH7750State *s, const char *zone1, const char *zone2);

#endif
