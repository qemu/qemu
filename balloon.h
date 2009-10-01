/*
 * Balloon
 *
 * Copyright IBM, Corp. 2008
 *
 * Authors:
 *  Anthony Liguori   <aliguori@us.ibm.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 *
 */

#ifndef _QEMU_BALLOON_H
#define _QEMU_BALLOON_H

#include "cpu-defs.h"

typedef ram_addr_t (QEMUBalloonEvent)(void *opaque, ram_addr_t target);

void qemu_add_balloon_handler(QEMUBalloonEvent *func, void *opaque);

void qemu_balloon(ram_addr_t target);

ram_addr_t qemu_balloon_status(void);

#endif
