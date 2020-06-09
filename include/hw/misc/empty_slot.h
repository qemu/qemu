/*
 * QEMU Empty Slot
 *
 * The empty_slot device emulates known to a bus but not connected devices.
 *
 * Copyright (c) 2010 Artyom Tarasenko
 *
 * This code is licensed under the GNU GPL v2 or (at your option) any later
 * version.
 */

#ifndef HW_EMPTY_SLOT_H
#define HW_EMPTY_SLOT_H

#include "exec/hwaddr.h"

void empty_slot_init(const char *name, hwaddr addr, uint64_t slot_size);

#endif
