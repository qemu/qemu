/*
 * Sharing QEMU block devices via vhost-user protocol
 *
 * Copyright (c) Coiby Xu <coiby.xu@gmail.com>.
 * Copyright (c) 2020 Red Hat, Inc.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * later.  See the COPYING file in the top-level directory.
 */

#ifndef VHOST_USER_BLK_SERVER_H
#define VHOST_USER_BLK_SERVER_H

#include "block/export.h"

/* For block/export/export.c */
extern const BlockExportDriver blk_exp_vhost_user_blk;

#endif /* VHOST_USER_BLK_SERVER_H */
