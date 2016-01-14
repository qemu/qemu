/*
 * COarse-grain LOck-stepping Virtual Machines for Non-stop Service (COLO)
 * (a.k.a. Fault Tolerance or Continuous Replication)
 *
 * Copyright (c) 2015 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2015 FUJITSU LIMITED
 * Copyright (c) 2015 Intel Corporation
 *
 * Author: Zhang Chen <zhangchen.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */


#ifndef QEMU_COLO_PROXY_H
#define QEMU_COLO_PROXY_H

int colo_proxy_start(int mode);
void colo_proxy_stop(int mode);
int colo_proxy_do_checkpoint(int mode);
bool colo_proxy_query_checkpoint(void);
bool colo_proxy_wait_for_diff(uint64_t wait_ms);

#endif /* QEMU_COLO_PROXY_H */
