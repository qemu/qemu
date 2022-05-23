/*
 * Export QEMU block device via VDUSE
 *
 * Copyright (C) 2022 Bytedance Inc. and/or its affiliates. All rights reserved.
 *
 * Author:
 *   Xie Yongji <xieyongji@bytedance.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VDUSE_BLK_H
#define VDUSE_BLK_H

#include "block/export.h"

extern const BlockExportDriver blk_exp_vduse_blk;

#endif /* VDUSE_BLK_H */
