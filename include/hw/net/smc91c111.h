/*
 * SMSC 91C111 Ethernet interface emulation
 *
 * Copyright (c) 2005 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_NET_SMC91C111_H
#define HW_NET_SMC91C111_H

#include "net/net.h"

void smc91c111_init(NICInfo *, uint32_t, qemu_irq);

#endif
