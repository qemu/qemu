/*
 * SMSC LAN9118 Ethernet interface emulation
 *
 * Copyright (c) 2009 CodeSourcery, LLC.
 * Written by Paul Brook
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#ifndef HW_NET_LAN9118_H
#define HW_NET_LAN9118_H

#include "net/net.h"

#define TYPE_LAN9118 "lan9118"

void lan9118_init(NICInfo *, uint32_t, qemu_irq);

#endif
