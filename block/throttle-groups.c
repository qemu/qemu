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

#include "qemu/osdep.h"
#include "block/throttle-groups.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "sysemu/qtest.h"

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
 * @ret:  the ThrottleState member of the ThrottleGroup
 */
ThrottleState *throttle_group_incref(const char *name)
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

    return &tg->ts;
}

/* Decrease the reference count of a ThrottleGroup.
 *
 * When the reference count reaches zero the ThrottleGroup is
 * destroyed.
 *
 * @ts:  The ThrottleGroup to unref, given by its ThrottleState member
 */
void throttle_group_unref(ThrottleState *ts)
{
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);

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

/* Return the next BlockDriverState in the round-robin sequence with
 * pending I/O requests.
 *
 * This assumes that tg->lock is held.
 *
 * @bs:        the current BlockDriverState
 * @is_write:  the type of operation (read/write)
 * @ret:       the next BlockDriverState with pending requests, or bs
 *             if there is none.
 */
static BlockDriverState *next_throttle_token(BlockDriverState *bs,
                                             bool is_write)
{
    ThrottleGroup *tg = container_of(bs->throttle_state, ThrottleGroup, ts);
    BlockDriverState *token, *start;

    start = token = tg->tokens[is_write];

    /* get next bs round in round robin style */
    token = throttle_group_next_bs(token);
    while (token != start && !token->pending_reqs[is_write]) {
        token = throttle_group_next_bs(token);
    }

    /* If no IO are queued for scheduling on the next round robin token
     * then decide the token is the current bs because chances are
     * the current bs get the current request queued.
     */
    if (token == start && !token->pending_reqs[is_write]) {
        token = bs;
    }

    return token;
}

/* Check if the next I/O request for a BlockDriverState needs to be
 * throttled or not. If there's no timer set in this group, set one
 * and update the token accordingly.
 *
 * This assumes that tg->lock is held.
 *
 * @bs:         the current BlockDriverState
 * @is_write:   the type of operation (read/write)
 * @ret:        whether the I/O request needs to be throttled or not
 */
static bool throttle_group_schedule_timer(BlockDriverState *bs,
                                          bool is_write)
{
    ThrottleState *ts = bs->throttle_state;
    ThrottleTimers *tt = &bs->throttle_timers;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    bool must_wait;

    /* Check if any of the timers in this group is already armed */
    if (tg->any_timer_armed[is_write]) {
        return true;
    }

    must_wait = throttle_schedule_timer(ts, tt, is_write);

    /* If a timer just got armed, set bs as the current token */
    if (must_wait) {
        tg->tokens[is_write] = bs;
        tg->any_timer_armed[is_write] = true;
    }

    return must_wait;
}

/* Look for the next pending I/O request and schedule it.
 *
 * This assumes that tg->lock is held.
 *
 * @bs:        the current BlockDriverState
 * @is_write:  the type of operation (read/write)
 */
static void schedule_next_request(BlockDriverState *bs, bool is_write)
{
    ThrottleGroup *tg = container_of(bs->throttle_state, ThrottleGroup, ts);
    bool must_wait;
    BlockDriverState *token;

    /* Check if there's any pending request to schedule next */
    token = next_throttle_token(bs, is_write);
    if (!token->pending_reqs[is_write]) {
        return;
    }

    /* Set a timer for the request if it needs to be throttled */
    must_wait = throttle_group_schedule_timer(token, is_write);

    /* If it doesn't have to wait, queue it for immediate execution */
    if (!must_wait) {
        /* Give preference to requests from the current bs */
        if (qemu_in_coroutine() &&
            qemu_co_queue_next(&bs->throttled_reqs[is_write])) {
            token = bs;
        } else {
            ThrottleTimers *tt = &token->throttle_timers;
            int64_t now = qemu_clock_get_ns(tt->clock_type);
            timer_mod(tt->timers[is_write], now + 1);
            tg->any_timer_armed[is_write] = true;
        }
        tg->tokens[is_write] = token;
    }
}

/* Check if an I/O request needs to be throttled, wait and set a timer
 * if necessary, and schedule the next request using a round robin
 * algorithm.
 *
 * @bs:        the current BlockDriverState
 * @bytes:     the number of bytes for this I/O
 * @is_write:  the type of operation (read/write)
 */
void coroutine_fn throttle_group_co_io_limits_intercept(BlockDriverState *bs,
                                                        unsigned int bytes,
                                                        bool is_write)
{
    bool must_wait;
    BlockDriverState *token;

    ThrottleGroup *tg = container_of(bs->throttle_state, ThrottleGroup, ts);
    qemu_mutex_lock(&tg->lock);

    /* First we check if this I/O has to be throttled. */
    token = next_throttle_token(bs, is_write);
    must_wait = throttle_group_schedule_timer(token, is_write);

    /* Wait if there's a timer set or queued requests of this type */
    if (must_wait || bs->pending_reqs[is_write]) {
        bs->pending_reqs[is_write]++;
        qemu_mutex_unlock(&tg->lock);
        qemu_co_queue_wait(&bs->throttled_reqs[is_write]);
        qemu_mutex_lock(&tg->lock);
        bs->pending_reqs[is_write]--;
    }

    /* The I/O will be executed, so do the accounting */
    throttle_account(bs->throttle_state, is_write, bytes);

    /* Schedule the next request */
    schedule_next_request(bs, is_write);

    qemu_mutex_unlock(&tg->lock);
}

void throttle_group_restart_bs(BlockDriverState *bs)
{
    int i;

    for (i = 0; i < 2; i++) {
        while (qemu_co_enter_next(&bs->throttled_reqs[i])) {
            ;
        }
    }
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
    /* throttle_config() cancels the timers */
    if (timer_pending(tt->timers[0])) {
        tg->any_timer_armed[0] = false;
    }
    if (timer_pending(tt->timers[1])) {
        tg->any_timer_armed[1] = false;
    }
    throttle_config(ts, tt, cfg);
    qemu_mutex_unlock(&tg->lock);

    qemu_co_enter_next(&bs->throttled_reqs[0]);
    qemu_co_enter_next(&bs->throttled_reqs[1]);
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

/* ThrottleTimers callback. This wakes up a request that was waiting
 * because it had been throttled.
 *
 * @bs:        the BlockDriverState whose request had been throttled
 * @is_write:  the type of operation (read/write)
 */
static void timer_cb(BlockDriverState *bs, bool is_write)
{
    ThrottleState *ts = bs->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    bool empty_queue;

    /* The timer has just been fired, so we can update the flag */
    qemu_mutex_lock(&tg->lock);
    tg->any_timer_armed[is_write] = false;
    qemu_mutex_unlock(&tg->lock);

    /* Run the request that was waiting for this timer */
    empty_queue = !qemu_co_enter_next(&bs->throttled_reqs[is_write]);

    /* If the request queue was empty then we have to take care of
     * scheduling the next one */
    if (empty_queue) {
        qemu_mutex_lock(&tg->lock);
        schedule_next_request(bs, is_write);
        qemu_mutex_unlock(&tg->lock);
    }
}

static void read_timer_cb(void *opaque)
{
    timer_cb(opaque, false);
}

static void write_timer_cb(void *opaque)
{
    timer_cb(opaque, true);
}

/* Register a BlockDriverState in the throttling group, also
 * initializing its timers and updating its throttle_state pointer to
 * point to it. If a throttling group with that name does not exist
 * yet, it will be created.
 *
 * @bs:        the BlockDriverState to insert
 * @groupname: the name of the group
 */
void throttle_group_register_bs(BlockDriverState *bs, const char *groupname)
{
    int i;
    ThrottleState *ts = throttle_group_incref(groupname);
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    int clock_type = QEMU_CLOCK_REALTIME;

    if (qtest_enabled()) {
        /* For testing block IO throttling only */
        clock_type = QEMU_CLOCK_VIRTUAL;
    }

    bs->throttle_state = ts;

    qemu_mutex_lock(&tg->lock);
    /* If the ThrottleGroup is new set this BlockDriverState as the token */
    for (i = 0; i < 2; i++) {
        if (!tg->tokens[i]) {
            tg->tokens[i] = bs;
        }
    }

    QLIST_INSERT_HEAD(&tg->head, bs, round_robin);

    throttle_timers_init(&bs->throttle_timers,
                         bdrv_get_aio_context(bs),
                         clock_type,
                         read_timer_cb,
                         write_timer_cb,
                         bs);

    qemu_mutex_unlock(&tg->lock);
}

/* Unregister a BlockDriverState from its group, removing it from the
 * list, destroying the timers and setting the throttle_state pointer
 * to NULL.
 *
 * The BlockDriverState must not have pending throttled requests, so
 * the caller has to drain them first.
 *
 * The group will be destroyed if it's empty after this operation.
 *
 * @bs: the BlockDriverState to remove
 */
void throttle_group_unregister_bs(BlockDriverState *bs)
{
    ThrottleGroup *tg = container_of(bs->throttle_state, ThrottleGroup, ts);
    int i;

    assert(bs->pending_reqs[0] == 0 && bs->pending_reqs[1] == 0);
    assert(qemu_co_queue_empty(&bs->throttled_reqs[0]));
    assert(qemu_co_queue_empty(&bs->throttled_reqs[1]));

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
    throttle_timers_destroy(&bs->throttle_timers);
    qemu_mutex_unlock(&tg->lock);

    throttle_group_unref(&tg->ts);
    bs->throttle_state = NULL;
}

static void throttle_groups_init(void)
{
    qemu_mutex_init(&throttle_groups_lock);
}

block_init(throttle_groups_init);
