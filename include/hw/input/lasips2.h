/*
 * QEMU LASI PS/2 emulation
 *
 * Copyright (c) 2019 Sven Schnelle
 *
 */
#ifndef HW_INPUT_LASIPS2_H
#define HW_INPUT_LASIPS2_H

#include "exec/hwaddr.h"

#define TYPE_LASIPS2 "lasips2"

void lasips2_init(MemoryRegion *address_space, hwaddr base, qemu_irq irq);

#endif /* HW_INPUT_LASIPS2_H */
