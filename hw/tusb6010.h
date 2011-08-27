/*
 * tusb6010 interfaces
 *
 * Copyright 2011 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *  Avi Kivity <avi@redhat.com>
 *
 * Derived from hw/devices.h.
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef TUSB6010_H
#define TUSB6010_H

#include "targphys.h"
#include "memory.h"

typedef struct TUSBState TUSBState;
TUSBState *tusb6010_init(qemu_irq intr);
MemoryRegion *tusb6010_sync_io(TUSBState *s);
MemoryRegion *tusb6010_async_io(TUSBState *s);
void tusb6010_power(TUSBState *s, int on);

#endif
