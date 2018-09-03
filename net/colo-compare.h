/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2017 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2017 FUJITSU LIMITED
 * Copyright (c) 2017 Intel Corporation
 *
 * Authors:
 *    zhanghailiang <zhang.zhanghailiang@huawei.com>
 *    Zhang Chen <zhangckid@gmail.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef QEMU_COLO_COMPARE_H
#define QEMU_COLO_COMPARE_H

void colo_notify_compares_event(void *opaque, int event, Error **errp);

#endif /* QEMU_COLO_COMPARE_H */
