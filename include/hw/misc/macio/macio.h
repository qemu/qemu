/*
 * PowerMac MacIO device emulation
 *
 * Copyright (c) 2005-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef MACIO_H
#define MACIO_H

#include "hw/intc/heathrow_pic.h"
#include "hw/misc/macio/cuda.h"
#include "hw/ppc/mac_dbdma.h"
#include "hw/ppc/openpic.h"

#define TYPE_MACIO "macio"
#define MACIO(obj) OBJECT_CHECK(MacIOState, (obj), TYPE_MACIO)

typedef struct MacIOState {
    /*< private >*/
    PCIDevice parent;
    /*< public >*/

    MemoryRegion bar;
    CUDAState cuda;
    DBDMAState dbdma;
    ESCCState escc;
    uint64_t frequency;
} MacIOState;

#define TYPE_OLDWORLD_MACIO "macio-oldworld"
#define OLDWORLD_MACIO(obj) \
    OBJECT_CHECK(OldWorldMacIOState, (obj), TYPE_OLDWORLD_MACIO)

typedef struct OldWorldMacIOState {
    /*< private >*/
    MacIOState parent_obj;
    /*< public >*/

    HeathrowState *pic;
    qemu_irq irqs[7];

    MacIONVRAMState nvram;
    MACIOIDEState ide[2];
} OldWorldMacIOState;

#define TYPE_NEWWORLD_MACIO "macio-newworld"
#define NEWWORLD_MACIO(obj) \
    OBJECT_CHECK(NewWorldMacIOState, (obj), TYPE_NEWWORLD_MACIO)

typedef struct NewWorldMacIOState {
    /*< private >*/
    MacIOState parent_obj;
    /*< public >*/

    OpenPICState *pic;
    qemu_irq irqs[7];
    MACIOIDEState ide[2];
} NewWorldMacIOState;

#endif /* MACIO_H */
