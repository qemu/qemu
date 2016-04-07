/*
 * QEMU block throttling group infrastructure
 *
 * Copyright (C) Nodalink, EURL. 2014
 * Copyright (C) Igalia, S.L. 2015
 *
 * Authors:
 *   Benoît Canet <benoit.canet@nodalink.com>
 *   Alberto Garcia <berto@igalia.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 or
 * (at your option) version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef THROTTLE_GROUPS_H
#define THROTTLE_GROUPS_H

#include "qemu/throttle.h"
#include "block/block_int.h"

const char *throttle_group_get_name(BlockDriverState *bs);

ThrottleState *throttle_group_incref(const char *name);
void throttle_group_unref(ThrottleState *ts);

void throttle_group_config(BlockDriverState *bs, ThrottleConfig *cfg);
void throttle_group_get_config(BlockDriverState *bs, ThrottleConfig *cfg);

void throttle_group_register_bs(BlockDriverState *bs, const char *groupname);
void throttle_group_unregister_bs(BlockDriverState *bs);
void throttle_group_restart_bs(BlockDriverState *bs);

void coroutine_fn throttle_group_co_io_limits_intercept(BlockDriverState *bs,
                                                        unsigned int bytes,
                                                        bool is_write);

#endif
