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

#include "block/throttle-groups.h"

/* The ThrottleGroup structure (with its ThrottleState) is shared
 * among different BlockDriverState and it's independent from
 * AioContext, so in order to use it from different threads it needs
 * its own locking.
 *
 * This locking is however handled internally in this file, so it's
 * transparent to outside users.
 *
 * The whole ThrottleGroup structure is private and invisible to
 * outside users, that only use it through its ThrottleState.
 *
 * In addition to the ThrottleGroup structure, BlockDriverState has
 * fields that need to be accessed by other members of the group and
 * therefore also need to be protected by this lock. Once a BDS is
 * registered in a group those fields can be accessed by other threads
 * any time.
 *
 * Again, all this is handled internally and is mostly transparent to
 * the outside. The 'throttle_timers' field however has an additional
 * constraint because it may be temporarily invalid (see for example
 * bdrv_set_aio_context()). Therefore in this file a thread will
 * access some other BDS's timers only after verifying that that BDS
 * has throttled requests in the queue.
 */
typedef struct ThrottleGroup {
    char *name; /* This is constant during the lifetime of the group */

    QemuMutex lock; /* This lock protects the following four fields */
    ThrottleState ts;
    QLIST_HEAD(, BlockDriverState) head;
    BlockDriverState *tokens[2];
    bool any_timer_armed[2];

    /* These two are protected by the global throttle_groups_lock */
    unsigned refcount;
    QTAILQ_ENTRY(ThrottleGroup) list;
} ThrottleGroup;

static QemuMutex throttle_groups_lock;
static QTAILQ_HEAD(, ThrottleGroup) throttle_groups =
    QTAILQ_HEAD_INITIALIZER(throttle_groups);

/* Increments the reference count of a ThrottleGroup given its name.
 *
 * If no ThrottleGroup is found with the given name a new one is
 * created.
 *
 * @name: the name of the ThrottleGroup
 * @ret:  the ThrottleGroup
 */
static ThrottleGroup *throttle_group_incref(const char *name)
{
    ThrottleGroup *tg = NULL;
    ThrottleGroup *iter;

    qemu_mutex_lock(&throttle_groups_lock);

    /* Look for an existing group with that name */
    QTAILQ_FOREACH(iter, &throttle_groups, list) {
        if (!strcmp(name, iter->name)) {
            tg = iter;
            break;
        }
    }

    /* Create a new one if not found */
    if (!tg) {
        tg = g_new0(ThrottleGroup, 1);
        tg->name = g_strdup(name);
        qemu_mutex_init(&tg->lock);
        throttle_init(&tg->ts);
        QLIST_INIT(&tg->head);

        QTAILQ_INSERT_TAIL(&throttle_groups, tg, list);
    }

    tg->refcount++;

    qemu_mutex_unlock(&throttle_groups_lock);

    return tg;
}

/* Decrease the reference count of a ThrottleGroup.
 *
 * When the reference count reaches zero the ThrottleGroup is
 * destroyed.
 *
 * @tg:  The ThrottleGroup to unref
 */
static void throttle_group_unref(ThrottleGroup *tg)
{
    qemu_mutex_lock(&throttle_groups_lock);
    if (--tg->refcount == 0) {
        QTAILQ_REMOVE(&throttle_groups, tg, list);
        qemu_mutex_destroy(&tg->lock);
        g_free(tg->name);
        g_free(tg);
    }
    qemu_mutex_unlock(&throttle_groups_lock);
}

/* Get the name from a BlockDriverState's ThrottleGroup. The name (and
 * the pointer) is guaranteed to remain constant during the lifetime
 * of the group.
 *
 * @bs:   a BlockDriverState that is member of a throttling group
 * @ret:  the name of the group.
 */
const char *throttle_group_get_name(BlockDriverState *bs)
{
    ThrottleGroup *tg = container_of(bs->throttle_state, ThrottleGroup, ts);
    return tg->name;
}

/* Return the next BlockDriverState in the round-robin sequence,
 * simulating a circular list.
 *
 * This assumes that tg->lock is held.
 *
 * @bs:  the current BlockDriverState
 * @ret: the next BlockDriverState in the sequence
 */
static BlockDriverState *throttle_group_next_bs(BlockDriverState *bs)
{
    ThrottleState *ts = bs->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    BlockDriverState *next = QLIST_NEXT(bs, round_robin);

    if (!next) {
        return QLIST_FIRST(&tg->head);
    }

    return next;
}

/* Update the throttle configuration for a particular group. Similar
 * to throttle_config(), but guarantees atomicity within the
 * throttling group.
 *
 * @bs:  a BlockDriverState that is member of the group
 * @cfg: the configuration to set
 */
void throttle_group_config(BlockDriverState *bs, ThrottleConfig *cfg)
{
    ThrottleTimers *tt = &bs->throttle_timers;
    ThrottleState *ts = bs->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    qemu_mutex_lock(&tg->lock);
    throttle_config(ts, tt, cfg);
    /* throttle_config() cancels the timers */
    tg->any_timer_armed[0] = tg->any_timer_armed[1] = false;
    qemu_mutex_unlock(&tg->lock);
}

/* Get the throttle configuration from a particular group. Similar to
 * throttle_get_config(), but guarantees atomicity within the
 * throttling group.
 *
 * @bs:  a BlockDriverState that is member of the group
 * @cfg: the configuration will be written here
 */
void throttle_group_get_config(BlockDriverState *bs, ThrottleConfig *cfg)
{
    ThrottleState *ts = bs->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    qemu_mutex_lock(&tg->lock);
    throttle_get_config(ts, cfg);
    qemu_mutex_unlock(&tg->lock);
}

/* Register a BlockDriverState in the throttling group, also updating
 * its throttle_state pointer to point to it. If a throttling group
 * with that name does not exist yet, it will be created.
 *
 * @bs:        the BlockDriverState to insert
 * @groupname: the name of the group
 */
void throttle_group_register_bs(BlockDriverState *bs, const char *groupname)
{
    int i;
    ThrottleGroup *tg = throttle_group_incref(groupname);

    bs->throttle_state = &tg->ts;

    qemu_mutex_lock(&tg->lock);
    /* If the ThrottleGroup is new set this BlockDriverState as the token */
    for (i = 0; i < 2; i++) {
        if (!tg->tokens[i]) {
            tg->tokens[i] = bs;
        }
    }

    QLIST_INSERT_HEAD(&tg->head, bs, round_robin);
    qemu_mutex_unlock(&tg->lock);
}

/* Unregister a BlockDriverState from its group, removing it from the
 * list and setting the throttle_state pointer to NULL.
 *
 * The group will be destroyed if it's empty after this operation.
 *
 * @bs: the BlockDriverState to remove
 */
void throttle_group_unregister_bs(BlockDriverState *bs)
{
    ThrottleGroup *tg = container_of(bs->throttle_state, ThrottleGroup, ts);
    int i;

    qemu_mutex_lock(&tg->lock);
    for (i = 0; i < 2; i++) {
        if (tg->tokens[i] == bs) {
            BlockDriverState *token = throttle_group_next_bs(bs);
            /* Take care of the case where this is the last bs in the group */
            if (token == bs) {
                token = NULL;
            }
            tg->tokens[i] = token;
        }
    }

    /* remove the current bs from the list */
    QLIST_REMOVE(bs, round_robin);
    qemu_mutex_unlock(&tg->lock);

    throttle_group_unref(tg);
    bs->throttle_state = NULL;
}

static void throttle_groups_init(void)
{
    qemu_mutex_init(&throttle_groups_lock);
}

block_init(throttle_groups_init);
