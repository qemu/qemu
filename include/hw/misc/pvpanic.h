/*
 * QEMU simulated pvpanic device.
 *
 * Copyright Fujitsu, Corp. 2013
 *
 * Authors:
 *     Wen Congyang <wency@cn.fujitsu.com>
 *     Hu Tao <hutao@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */
#ifndef HW_MISC_PVPANIC_H
#define HW_MISC_PVPANIC_H

#define TYPE_PVPANIC "pvpanic"

uint16_t pvpanic_port(void);

#endif
