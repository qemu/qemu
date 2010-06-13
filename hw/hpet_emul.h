/*
 * QEMU Emulated HPET support
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Beth Kon   <bkon@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */
#ifndef QEMU_HPET_EMUL_H
#define QEMU_HPET_EMUL_H

#define HPET_BASE               0xfed00000
#define HPET_CLK_PERIOD         10000000ULL /* 10000000 femtoseconds == 10ns*/

#define FS_PER_NS 1000000
#define HPET_NUM_TIMERS 3

#define HPET_NUM_IRQ_ROUTES     32

#define HPET_CFG_ENABLE 0x001
#define HPET_CFG_LEGACY 0x002

#define HPET_ID         0x000
#define HPET_PERIOD     0x004
#define HPET_CFG        0x010
#define HPET_STATUS     0x020
#define HPET_COUNTER    0x0f0
#define HPET_TN_CFG     0x000
#define HPET_TN_CMP     0x008
#define HPET_TN_ROUTE   0x010
#define HPET_CFG_WRITE_MASK  0x3

#define HPET_TN_TYPE_LEVEL       0x002
#define HPET_TN_ENABLE           0x004
#define HPET_TN_PERIODIC         0x008
#define HPET_TN_PERIODIC_CAP     0x010
#define HPET_TN_SIZE_CAP         0x020
#define HPET_TN_SETVAL           0x040
#define HPET_TN_32BIT            0x100
#define HPET_TN_INT_ROUTE_MASK  0x3e00
#define HPET_TN_CFG_WRITE_MASK  0x3f4e
#define HPET_TN_INT_ROUTE_SHIFT      9
#define HPET_TN_INT_ROUTE_CAP_SHIFT 32
#define HPET_TN_CFG_BITS_READONLY_OR_RESERVED 0xffff80b1U

#endif
