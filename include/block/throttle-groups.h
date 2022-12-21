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

#include "qemu/coroutine.h"
#include "qemu/throttle.h"
#include "qom/object.h"

/* The ThrottleGroupMember structure indicates membership in a ThrottleGroup
 * and holds related data.
 */

typedef struct ThrottleGroupMember {
    AioContext   *aio_context;
    /* throttled_reqs_lock protects the CoQueues for throttled requests.  */
    CoMutex      throttled_reqs_lock;
    CoQueue      throttled_reqs[2];

    /* Nonzero if the I/O limits are currently being ignored; generally
     * it is zero.  Accessed with atomic operations.
     */
    unsigned int io_limits_disabled;

    /* Number of pending throttle_group_restart_queue_entry() coroutines.
     * Accessed with atomic operations.
     */
    unsigned int restart_pending;

    /* The following fields are protected by the ThrottleGroup lock.
     * See the ThrottleGroup documentation for details.
     * throttle_state tells us if I/O limits are configured. */
    ThrottleState *throttle_state;
    ThrottleTimers throttle_timers;
    unsigned       pending_reqs[2];
    QLIST_ENTRY(ThrottleGroupMember) round_robin;

} ThrottleGroupMember;

#define TYPE_THROTTLE_GROUP "throttle-group"
OBJECT_DECLARE_SIMPLE_TYPE(ThrottleGroup, THROTTLE_GROUP)

const char *throttle_group_get_name(ThrottleGroupMember *tgm);

ThrottleState *throttle_group_incref(const char *name);
void throttle_group_unref(ThrottleState *ts);

void throttle_group_config(ThrottleGroupMember *tgm, ThrottleConfig *cfg);
void throttle_group_get_config(ThrottleGroupMember *tgm, ThrottleConfig *cfg);

void throttle_group_register_tgm(ThrottleGroupMember *tgm,
                                const char *groupname,
                                AioContext *ctx);
void throttle_group_unregister_tgm(ThrottleGroupMember *tgm);
void throttle_group_restart_tgm(ThrottleGroupMember *tgm);

void coroutine_fn throttle_group_co_io_limits_intercept(ThrottleGroupMember *tgm,
                                                        int64_t bytes,
                                                        bool is_write);
void throttle_group_attach_aio_context(ThrottleGroupMember *tgm,
                                       AioContext *new_context);
void throttle_group_detach_aio_context(ThrottleGroupMember *tgm);
/*
 * throttle_group_exists() must be called under the global
 * mutex.
 */
bool throttle_group_exists(const char *name);

#endif
