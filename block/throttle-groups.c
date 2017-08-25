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
#include "sysemu/block-backend.h"
#include "block/throttle-groups.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "sysemu/qtest.h"

/* The ThrottleGroup structure (with its ThrottleState) is shared
 * among different ThrottleGroupMembers and it's independent from
 * AioContext, so in order to use it from different threads it needs
 * its own locking.
 *
 * This locking is however handled internally in this file, so it's
 * transparent to outside users.
 *
 * The whole ThrottleGroup structure is private and invisible to
 * outside users, that only use it through its ThrottleState.
 *
 * In addition to the ThrottleGroup structure, ThrottleGroupMember has
 * fields that need to be accessed by other members of the group and
 * therefore also need to be protected by this lock. Once a
 * ThrottleGroupMember is registered in a group those fields can be accessed
 * by other threads any time.
 *
 * Again, all this is handled internally and is mostly transparent to
 * the outside. The 'throttle_timers' field however has an additional
 * constraint because it may be temporarily invalid (see for example
 * blk_set_aio_context()). Therefore in this file a thread will
 * access some other ThrottleGroupMember's timers only after verifying that
 * that ThrottleGroupMember has throttled requests in the queue.
 */
typedef struct ThrottleGroup {
    char *name; /* This is constant during the lifetime of the group */

    QemuMutex lock; /* This lock protects the following four fields */
    ThrottleState ts;
    QLIST_HEAD(, ThrottleGroupMember) head;
    ThrottleGroupMember *tokens[2];
    bool any_timer_armed[2];
    QEMUClockType clock_type;

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
        tg->clock_type = QEMU_CLOCK_REALTIME;

        if (qtest_enabled()) {
            /* For testing block IO throttling only */
            tg->clock_type = QEMU_CLOCK_VIRTUAL;
        }
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

/* Get the name from a ThrottleGroupMember's group. The name (and the pointer)
 * is guaranteed to remain constant during the lifetime of the group.
 *
 * @tgm:  a ThrottleGroupMember
 * @ret:  the name of the group.
 */
const char *throttle_group_get_name(ThrottleGroupMember *tgm)
{
    ThrottleGroup *tg = container_of(tgm->throttle_state, ThrottleGroup, ts);
    return tg->name;
}

/* Return the next ThrottleGroupMember in the round-robin sequence, simulating
 * a circular list.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm: the current ThrottleGroupMember
 * @ret: the next ThrottleGroupMember in the sequence
 */
static ThrottleGroupMember *throttle_group_next_tgm(ThrottleGroupMember *tgm)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleGroupMember *next = QLIST_NEXT(tgm, round_robin);

    if (!next) {
        next = QLIST_FIRST(&tg->head);
    }

    return next;
}

/*
 * Return whether a ThrottleGroupMember has pending requests.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:        the ThrottleGroupMember
 * @is_write:   the type of operation (read/write)
 * @ret:        whether the ThrottleGroupMember has pending requests.
 */
static inline bool tgm_has_pending_reqs(ThrottleGroupMember *tgm,
                                        bool is_write)
{
    return tgm->pending_reqs[is_write];
}

/* Return the next ThrottleGroupMember in the round-robin sequence with pending
 * I/O requests.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:       the current ThrottleGroupMember
 * @is_write:  the type of operation (read/write)
 * @ret:       the next ThrottleGroupMember with pending requests, or tgm if
 *             there is none.
 */
static ThrottleGroupMember *next_throttle_token(ThrottleGroupMember *tgm,
                                                bool is_write)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleGroupMember *token, *start;

    start = token = tg->tokens[is_write];

    /* get next bs round in round robin style */
    token = throttle_group_next_tgm(token);
    while (token != start && !tgm_has_pending_reqs(token, is_write)) {
        token = throttle_group_next_tgm(token);
    }

    /* If no IO are queued for scheduling on the next round robin token
     * then decide the token is the current tgm because chances are
     * the current tgm got the current request queued.
     */
    if (token == start && !tgm_has_pending_reqs(token, is_write)) {
        token = tgm;
    }

    /* Either we return the original TGM, or one with pending requests */
    assert(token == tgm || tgm_has_pending_reqs(token, is_write));

    return token;
}

/* Check if the next I/O request for a ThrottleGroupMember needs to be
 * throttled or not. If there's no timer set in this group, set one and update
 * the token accordingly.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:        the current ThrottleGroupMember
 * @is_write:   the type of operation (read/write)
 * @ret:        whether the I/O request needs to be throttled or not
 */
static bool throttle_group_schedule_timer(ThrottleGroupMember *tgm,
                                          bool is_write)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleTimers *tt = &tgm->throttle_timers;
    bool must_wait;

    if (atomic_read(&tgm->io_limits_disabled)) {
        return false;
    }

    /* Check if any of the timers in this group is already armed */
    if (tg->any_timer_armed[is_write]) {
        return true;
    }

    must_wait = throttle_schedule_timer(ts, tt, is_write);

    /* If a timer just got armed, set tgm as the current token */
    if (must_wait) {
        tg->tokens[is_write] = tgm;
        tg->any_timer_armed[is_write] = true;
    }

    return must_wait;
}

/* Start the next pending I/O request for a ThrottleGroupMember. Return whether
 * any request was actually pending.
 *
 * @tgm:       the current ThrottleGroupMember
 * @is_write:  the type of operation (read/write)
 */
static bool coroutine_fn throttle_group_co_restart_queue(ThrottleGroupMember *tgm,
                                                         bool is_write)
{
    bool ret;

    qemu_co_mutex_lock(&tgm->throttled_reqs_lock);
    ret = qemu_co_queue_next(&tgm->throttled_reqs[is_write]);
    qemu_co_mutex_unlock(&tgm->throttled_reqs_lock);

    return ret;
}

/* Look for the next pending I/O request and schedule it.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:       the current ThrottleGroupMember
 * @is_write:  the type of operation (read/write)
 */
static void schedule_next_request(ThrottleGroupMember *tgm, bool is_write)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    bool must_wait;
    ThrottleGroupMember *token;

    /* Check if there's any pending request to schedule next */
    token = next_throttle_token(tgm, is_write);
    if (!tgm_has_pending_reqs(token, is_write)) {
        return;
    }

    /* Set a timer for the request if it needs to be throttled */
    must_wait = throttle_group_schedule_timer(token, is_write);

    /* If it doesn't have to wait, queue it for immediate execution */
    if (!must_wait) {
        /* Give preference to requests from the current tgm */
        if (qemu_in_coroutine() &&
            throttle_group_co_restart_queue(tgm, is_write)) {
            token = tgm;
        } else {
            ThrottleTimers *tt = &token->throttle_timers;
            int64_t now = qemu_clock_get_ns(tg->clock_type);
            timer_mod(tt->timers[is_write], now);
            tg->any_timer_armed[is_write] = true;
        }
        tg->tokens[is_write] = token;
    }
}

/* Check if an I/O request needs to be throttled, wait and set a timer
 * if necessary, and schedule the next request using a round robin
 * algorithm.
 *
 * @tgm:       the current ThrottleGroupMember
 * @bytes:     the number of bytes for this I/O
 * @is_write:  the type of operation (read/write)
 */
void coroutine_fn throttle_group_co_io_limits_intercept(ThrottleGroupMember *tgm,
                                                        unsigned int bytes,
                                                        bool is_write)
{
    bool must_wait;
    ThrottleGroupMember *token;
    ThrottleGroup *tg = container_of(tgm->throttle_state, ThrottleGroup, ts);
    qemu_mutex_lock(&tg->lock);

    /* First we check if this I/O has to be throttled. */
    token = next_throttle_token(tgm, is_write);
    must_wait = throttle_group_schedule_timer(token, is_write);

    /* Wait if there's a timer set or queued requests of this type */
    if (must_wait || tgm->pending_reqs[is_write]) {
        tgm->pending_reqs[is_write]++;
        qemu_mutex_unlock(&tg->lock);
        qemu_co_mutex_lock(&tgm->throttled_reqs_lock);
        qemu_co_queue_wait(&tgm->throttled_reqs[is_write],
                           &tgm->throttled_reqs_lock);
        qemu_co_mutex_unlock(&tgm->throttled_reqs_lock);
        qemu_mutex_lock(&tg->lock);
        tgm->pending_reqs[is_write]--;
    }

    /* The I/O will be executed, so do the accounting */
    throttle_account(tgm->throttle_state, is_write, bytes);

    /* Schedule the next request */
    schedule_next_request(tgm, is_write);

    qemu_mutex_unlock(&tg->lock);
}

typedef struct {
    ThrottleGroupMember *tgm;
    bool is_write;
} RestartData;

static void coroutine_fn throttle_group_restart_queue_entry(void *opaque)
{
    RestartData *data = opaque;
    ThrottleGroupMember *tgm = data->tgm;
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    bool is_write = data->is_write;
    bool empty_queue;

    empty_queue = !throttle_group_co_restart_queue(tgm, is_write);

    /* If the request queue was empty then we have to take care of
     * scheduling the next one */
    if (empty_queue) {
        qemu_mutex_lock(&tg->lock);
        schedule_next_request(tgm, is_write);
        qemu_mutex_unlock(&tg->lock);
    }
}

static void throttle_group_restart_queue(ThrottleGroupMember *tgm, bool is_write)
{
    Coroutine *co;
    RestartData rd = {
        .tgm = tgm,
        .is_write = is_write
    };

    co = qemu_coroutine_create(throttle_group_restart_queue_entry, &rd);
    aio_co_enter(tgm->aio_context, co);
}

void throttle_group_restart_tgm(ThrottleGroupMember *tgm)
{
    if (tgm->throttle_state) {
        throttle_group_restart_queue(tgm, 0);
        throttle_group_restart_queue(tgm, 1);
    }
}

/* Update the throttle configuration for a particular group. Similar
 * to throttle_config(), but guarantees atomicity within the
 * throttling group.
 *
 * @tgm:    a ThrottleGroupMember that is a member of the group
 * @cfg: the configuration to set
 */
void throttle_group_config(ThrottleGroupMember *tgm, ThrottleConfig *cfg)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    qemu_mutex_lock(&tg->lock);
    throttle_config(ts, tg->clock_type, cfg);
    qemu_mutex_unlock(&tg->lock);

    throttle_group_restart_tgm(tgm);
}

/* Get the throttle configuration from a particular group. Similar to
 * throttle_get_config(), but guarantees atomicity within the
 * throttling group.
 *
 * @tgm:    a ThrottleGroupMember that is a member of the group
 * @cfg: the configuration will be written here
 */
void throttle_group_get_config(ThrottleGroupMember *tgm, ThrottleConfig *cfg)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    qemu_mutex_lock(&tg->lock);
    throttle_get_config(ts, cfg);
    qemu_mutex_unlock(&tg->lock);
}

/* ThrottleTimers callback. This wakes up a request that was waiting
 * because it had been throttled.
 *
 * @tgm:       the ThrottleGroupMember whose request had been throttled
 * @is_write:  the type of operation (read/write)
 */
static void timer_cb(ThrottleGroupMember *tgm, bool is_write)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);

    /* The timer has just been fired, so we can update the flag */
    qemu_mutex_lock(&tg->lock);
    tg->any_timer_armed[is_write] = false;
    qemu_mutex_unlock(&tg->lock);

    /* Run the request that was waiting for this timer */
    throttle_group_restart_queue(tgm, is_write);
}

static void read_timer_cb(void *opaque)
{
    timer_cb(opaque, false);
}

static void write_timer_cb(void *opaque)
{
    timer_cb(opaque, true);
}

/* Register a ThrottleGroupMember from the throttling group, also initializing
 * its timers and updating its throttle_state pointer to point to it. If a
 * throttling group with that name does not exist yet, it will be created.
 *
 * @tgm:       the ThrottleGroupMember to insert
 * @groupname: the name of the group
 * @ctx:       the AioContext to use
 */
void throttle_group_register_tgm(ThrottleGroupMember *tgm,
                                 const char *groupname,
                                 AioContext *ctx)
{
    int i;
    ThrottleState *ts = throttle_group_incref(groupname);
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);

    tgm->throttle_state = ts;
    tgm->aio_context = ctx;

    qemu_mutex_lock(&tg->lock);
    /* If the ThrottleGroup is new set this ThrottleGroupMember as the token */
    for (i = 0; i < 2; i++) {
        if (!tg->tokens[i]) {
            tg->tokens[i] = tgm;
        }
    }

    QLIST_INSERT_HEAD(&tg->head, tgm, round_robin);

    throttle_timers_init(&tgm->throttle_timers,
                         tgm->aio_context,
                         tg->clock_type,
                         read_timer_cb,
                         write_timer_cb,
                         tgm);

    qemu_mutex_unlock(&tg->lock);
}

/* Unregister a ThrottleGroupMember from its group, removing it from the list,
 * destroying the timers and setting the throttle_state pointer to NULL.
 *
 * The ThrottleGroupMember must not have pending throttled requests, so the
 * caller has to drain them first.
 *
 * The group will be destroyed if it's empty after this operation.
 *
 * @tgm the ThrottleGroupMember to remove
 */
void throttle_group_unregister_tgm(ThrottleGroupMember *tgm)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleGroupMember *token;
    int i;

    assert(tgm->pending_reqs[0] == 0 && tgm->pending_reqs[1] == 0);
    assert(qemu_co_queue_empty(&tgm->throttled_reqs[0]));
    assert(qemu_co_queue_empty(&tgm->throttled_reqs[1]));

    qemu_mutex_lock(&tg->lock);
    for (i = 0; i < 2; i++) {
        if (tg->tokens[i] == tgm) {
            token = throttle_group_next_tgm(tgm);
            /* Take care of the case where this is the last tgm in the group */
            if (token == tgm) {
                token = NULL;
            }
            tg->tokens[i] = token;
        }
    }

    /* remove the current tgm from the list */
    QLIST_REMOVE(tgm, round_robin);
    throttle_timers_destroy(&tgm->throttle_timers);
    qemu_mutex_unlock(&tg->lock);

    throttle_group_unref(&tg->ts);
    tgm->throttle_state = NULL;
}

void throttle_group_attach_aio_context(ThrottleGroupMember *tgm,
                                       AioContext *new_context)
{
    ThrottleTimers *tt = &tgm->throttle_timers;
    throttle_timers_attach_aio_context(tt, new_context);
    tgm->aio_context = new_context;
}

void throttle_group_detach_aio_context(ThrottleGroupMember *tgm)
{
    ThrottleTimers *tt = &tgm->throttle_timers;
    throttle_timers_detach_aio_context(tt);
    tgm->aio_context = NULL;
}

static void throttle_groups_init(void)
{
    qemu_mutex_init(&throttle_groups_lock);
}

block_init(throttle_groups_init);
