/*
 * QEMU backup
 *
 * Copyright (c) 2013 Proxmox Server Solutions
 * Copyright (c) 2016 HUAWEI TECHNOLOGIES CO., LTD.
 * Copyright (c) 2016 Intel Corporation
 * Copyright (c) 2016 FUJITSU LIMITED
 *
 * Authors:
 *  Dietmar Maurer <dietmar@proxmox.com>
 *  Changlong Xie <xiecl.fnst@cn.fujitsu.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef BLOCK_BACKUP_H
#define BLOCK_BACKUP_H

#include "block/blockjob.h"

void backup_do_checkpoint(BlockJob *job, Error **errp);

#endif
