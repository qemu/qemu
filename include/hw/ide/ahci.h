/*
 * QEMU AHCI Emulation
 *
 * Copyright (c) 2010 qiaochong@loongson.cn
 * Copyright (c) 2010 Roland Elek <elek.roland@gmail.com>
 * Copyright (c) 2010 Sebastian Herbszt <herbszt@gmx.de>
 * Copyright (c) 2010 Alexander Graf <agraf@suse.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef HW_IDE_AHCI_H
#define HW_IDE_AHCI_H

#include "exec/memory.h"

typedef struct AHCIDevice AHCIDevice;

typedef struct AHCIControlRegs {
    uint32_t    cap;
    uint32_t    ghc;
    uint32_t    irqstatus;
    uint32_t    impl;
    uint32_t    version;
} AHCIControlRegs;

typedef struct AHCIState {
    DeviceState *container;

    AHCIDevice *dev;
    AHCIControlRegs control_regs;
    MemoryRegion mem;
    MemoryRegion idp;       /* Index-Data Pair I/O port space */
    unsigned idp_offset;    /* Offset of index in I/O port space */
    uint32_t idp_index;     /* Current IDP index */
    uint32_t ports;
    qemu_irq irq;
    AddressSpace *as;
} AHCIState;


void ahci_ide_create_devs(AHCIState *ahci, DriveInfo **hd);

#endif /* HW_IDE_AHCI_H */
