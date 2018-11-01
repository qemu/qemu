/*
 * Fsdev Throttle
 *
 * Copyright (C) 2016 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Pradeep Jagadeesh <pradeep.jagadeesh@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 */

#ifndef _FSDEV_THROTTLE_H
#define _FSDEV_THROTTLE_H

#include "qemu/coroutine.h"
#include "qemu/throttle.h"
#include "qapi/qapi-types-fsdev.h"

typedef struct FsThrottle {
    ThrottleState ts;
    ThrottleTimers tt;
    ThrottleConfig cfg;
    AioContext *ctx;
    CoQueue      throttled_reqs[2];
} FsThrottle;

void fsdev_throttle_parse_opts(QemuOpts *, FsThrottle *, Error **);

void fsdev_throttle_init(FsThrottle *);

void coroutine_fn fsdev_co_throttle_request(FsThrottle *, bool ,
                                            struct iovec *, int);

void fsdev_throttle_cleanup(FsThrottle *);
void fsdev_set_io_throttle(FsdevIOThrottle *, FsThrottle *, Error **errp);
void fsdev_get_io_throttle(FsThrottle *, FsdevIOThrottle **iothp, char *);
#endif /* _FSDEV_THROTTLE_H */
