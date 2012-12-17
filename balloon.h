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

#include "monitor/monitor.h"
#include "qapi-types.h"

typedef void (QEMUBalloonEvent)(void *opaque, ram_addr_t target);
typedef void (QEMUBalloonStatus)(void *opaque, BalloonInfo *info);

int qemu_add_balloon_handler(QEMUBalloonEvent *event_func,
			     QEMUBalloonStatus *stat_func, void *opaque);
void qemu_remove_balloon_handler(void *opaque);

void qemu_balloon_changed(int64_t actual);

#endif
