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
#include "system/block-backend.h"
#include "block/throttle-groups.h"
#include "qemu/throttle-options.h"
#include "qemu/main-loop.h"
#include "qemu/queue.h"
#include "qemu/thread.h"
#include "system/qtest.h"
#include "qapi/error.h"
#include "qapi/qapi-visit-block-core.h"
#include "qom/object.h"
#include "qom/object_interfaces.h"

static void throttle_group_obj_init(Object *obj);
static void throttle_group_obj_complete(UserCreatable *obj, Error **errp);
static void timer_cb(ThrottleGroupMember *tgm, ThrottleDirection direction);

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
struct ThrottleGroup {
    Object parent_obj;

    /* refuse individual property change if initialization is complete */
    bool is_initialized;
    char *name; /* This is constant during the lifetime of the group */

    QemuMutex lock; /* This lock protects the following four fields */
    ThrottleState ts;
    QLIST_HEAD(, ThrottleGroupMember) head;
    ThrottleGroupMember *tokens[THROTTLE_MAX];
    bool any_timer_armed[THROTTLE_MAX];
    QEMUClockType clock_type;

    /* This field is protected by the global QEMU mutex */
    QTAILQ_ENTRY(ThrottleGroup) list;
};

/* This is protected by the global QEMU mutex */
static QTAILQ_HEAD(, ThrottleGroup) throttle_groups =
    QTAILQ_HEAD_INITIALIZER(throttle_groups);


/* This function reads throttle_groups and must be called under the global
 * mutex.
 */
static ThrottleGroup *throttle_group_by_name(const char *name)
{
    ThrottleGroup *iter;

    /* Look for an existing group with that name */
    QTAILQ_FOREACH(iter, &throttle_groups, list) {
        if (!g_strcmp0(name, iter->name)) {
            return iter;
        }
    }

    return NULL;
}

/* This function reads throttle_groups and must be called under the global
 * mutex.
 */
bool throttle_group_exists(const char *name)
{
    return throttle_group_by_name(name) != NULL;
}

/* Increments the reference count of a ThrottleGroup given its name.
 *
 * If no ThrottleGroup is found with the given name a new one is
 * created.
 *
 * This function edits throttle_groups and must be called under the global
 * mutex.
 *
 * @name: the name of the ThrottleGroup
 * @ret:  the ThrottleState member of the ThrottleGroup
 */
ThrottleState *throttle_group_incref(const char *name)
{
    ThrottleGroup *tg = NULL;

    /* Look for an existing group with that name */
    tg = throttle_group_by_name(name);

    if (tg) {
        object_ref(OBJECT(tg));
    } else {
        /* Create a new one if not found */
        /* new ThrottleGroup obj will have a refcnt = 1 */
        tg = THROTTLE_GROUP(object_new(TYPE_THROTTLE_GROUP));
        tg->name = g_strdup(name);
        throttle_group_obj_complete(USER_CREATABLE(tg), &error_abort);
    }

    return &tg->ts;
}

/* Decrease the reference count of a ThrottleGroup.
 *
 * When the reference count reaches zero the ThrottleGroup is
 * destroyed.
 *
 * This function edits throttle_groups and must be called under the global
 * mutex.
 *
 * @ts:  The ThrottleGroup to unref, given by its ThrottleState member
 */
void throttle_group_unref(ThrottleState *ts)
{
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    object_unref(OBJECT(tg));
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
 * @direction:  the ThrottleDirection
 * @ret:        whether the ThrottleGroupMember has pending requests.
 */
static inline bool tgm_has_pending_reqs(ThrottleGroupMember *tgm,
                                        ThrottleDirection direction)
{
    return tgm->pending_reqs[direction];
}

/* Return the next ThrottleGroupMember in the round-robin sequence with pending
 * I/O requests.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:       the current ThrottleGroupMember
 * @direction: the ThrottleDirection
 * @ret:       the next ThrottleGroupMember with pending requests, or tgm if
 *             there is none.
 */
static ThrottleGroupMember *next_throttle_token(ThrottleGroupMember *tgm,
                                                ThrottleDirection direction)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleGroupMember *token, *start;

    /* If this member has its I/O limits disabled then it means that
     * it's being drained. Skip the round-robin search and return tgm
     * immediately if it has pending requests. Otherwise we could be
     * forcing it to wait for other member's throttled requests. */
    if (tgm_has_pending_reqs(tgm, direction) &&
        qatomic_read(&tgm->io_limits_disabled)) {
        return tgm;
    }

    start = token = tg->tokens[direction];

    /* get next bs round in round robin style */
    token = throttle_group_next_tgm(token);
    while (token != start && !tgm_has_pending_reqs(token, direction)) {
        token = throttle_group_next_tgm(token);
    }

    /* If no IO are queued for scheduling on the next round robin token
     * then decide the token is the current tgm because chances are
     * the current tgm got the current request queued.
     */
    if (token == start && !tgm_has_pending_reqs(token, direction)) {
        token = tgm;
    }

    /* Either we return the original TGM, or one with pending requests */
    assert(token == tgm || tgm_has_pending_reqs(token, direction));

    return token;
}

/* Check if the next I/O request for a ThrottleGroupMember needs to be
 * throttled or not. If there's no timer set in this group, set one and update
 * the token accordingly.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:        the current ThrottleGroupMember
 * @direction:  the ThrottleDirection
 * @ret:        whether the I/O request needs to be throttled or not
 */
static bool throttle_group_schedule_timer(ThrottleGroupMember *tgm,
                                          ThrottleDirection direction)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleTimers *tt = &tgm->throttle_timers;
    bool must_wait;

    if (qatomic_read(&tgm->io_limits_disabled)) {
        return false;
    }

    /* Check if any of the timers in this group is already armed */
    if (tg->any_timer_armed[direction]) {
        return true;
    }

    must_wait = throttle_schedule_timer(ts, tt, direction);

    /* If a timer just got armed, set tgm as the current token */
    if (must_wait) {
        tg->tokens[direction] = tgm;
        tg->any_timer_armed[direction] = true;
    }

    return must_wait;
}

/* Start the next pending I/O request for a ThrottleGroupMember. Return whether
 * any request was actually pending.
 *
 * @tgm:       the current ThrottleGroupMember
 * @direction: the ThrottleDirection
 */
static bool coroutine_fn throttle_group_co_restart_queue(ThrottleGroupMember *tgm,
                                                         ThrottleDirection direction)
{
    bool ret;

    qemu_co_mutex_lock(&tgm->throttled_reqs_lock);
    ret = qemu_co_queue_next(&tgm->throttled_reqs[direction]);
    qemu_co_mutex_unlock(&tgm->throttled_reqs_lock);

    return ret;
}

/* Look for the next pending I/O request and schedule it.
 *
 * This assumes that tg->lock is held.
 *
 * @tgm:       the current ThrottleGroupMember
 * @direction: the ThrottleDirection
 */
static void coroutine_mixed_fn schedule_next_request(ThrottleGroupMember *tgm,
                                                     ThrottleDirection direction)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    bool must_wait;
    ThrottleGroupMember *token;

    /* Check if there's any pending request to schedule next */
    token = next_throttle_token(tgm, direction);
    if (!tgm_has_pending_reqs(token, direction)) {
        return;
    }

    /* Set a timer for the request if it needs to be throttled */
    must_wait = throttle_group_schedule_timer(token, direction);

    /* If it doesn't have to wait, queue it for immediate execution */
    if (!must_wait) {
        /* Give preference to requests from the current tgm */
        if (qemu_in_coroutine() &&
            throttle_group_co_restart_queue(tgm, direction)) {
            token = tgm;
        } else {
            ThrottleTimers *tt = &token->throttle_timers;
            int64_t now = qemu_clock_get_ns(tg->clock_type);
            timer_mod(tt->timers[direction], now);
            tg->any_timer_armed[direction] = true;
        }
        tg->tokens[direction] = token;
    }
}

/* Check if an I/O request needs to be throttled, wait and set a timer
 * if necessary, and schedule the next request using a round robin
 * algorithm.
 *
 * @tgm:       the current ThrottleGroupMember
 * @bytes:     the number of bytes for this I/O
 * @direction: the ThrottleDirection
 */
void coroutine_fn throttle_group_co_io_limits_intercept(ThrottleGroupMember *tgm,
                                                        int64_t bytes,
                                                        ThrottleDirection direction)
{
    bool must_wait;
    ThrottleGroupMember *token;
    ThrottleGroup *tg = container_of(tgm->throttle_state, ThrottleGroup, ts);

    assert(bytes >= 0);
    assert(direction < THROTTLE_MAX);

    qemu_mutex_lock(&tg->lock);

    /* First we check if this I/O has to be throttled. */
    token = next_throttle_token(tgm, direction);
    must_wait = throttle_group_schedule_timer(token, direction);

    /* Wait if there's a timer set or queued requests of this type */
    if (must_wait || tgm->pending_reqs[direction]) {
        tgm->pending_reqs[direction]++;
        qemu_mutex_unlock(&tg->lock);
        qemu_co_mutex_lock(&tgm->throttled_reqs_lock);
        qemu_co_queue_wait(&tgm->throttled_reqs[direction],
                           &tgm->throttled_reqs_lock);
        qemu_co_mutex_unlock(&tgm->throttled_reqs_lock);
        qemu_mutex_lock(&tg->lock);
        tgm->pending_reqs[direction]--;
    }

    /* The I/O will be executed, so do the accounting */
    throttle_account(tgm->throttle_state, direction, bytes);

    /* Schedule the next request */
    schedule_next_request(tgm, direction);

    qemu_mutex_unlock(&tg->lock);
}

typedef struct {
    ThrottleGroupMember *tgm;
    ThrottleDirection direction;
} RestartData;

static void coroutine_fn throttle_group_restart_queue_entry(void *opaque)
{
    RestartData *data = opaque;
    ThrottleGroupMember *tgm = data->tgm;
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);
    ThrottleDirection direction = data->direction;
    bool empty_queue;

    empty_queue = !throttle_group_co_restart_queue(tgm, direction);

    /* If the request queue was empty then we have to take care of
     * scheduling the next one */
    if (empty_queue) {
        qemu_mutex_lock(&tg->lock);
        schedule_next_request(tgm, direction);
        qemu_mutex_unlock(&tg->lock);
    }

    g_free(data);

    qatomic_dec(&tgm->restart_pending);
    aio_wait_kick();
}

static void throttle_group_restart_queue(ThrottleGroupMember *tgm,
                                        ThrottleDirection direction)
{
    Coroutine *co;
    RestartData *rd = g_new0(RestartData, 1);

    rd->tgm = tgm;
    rd->direction = direction;

    /* This function is called when a timer is fired or when
     * throttle_group_restart_tgm() is called. Either way, there can
     * be no timer pending on this tgm at this point */
    assert(!timer_pending(tgm->throttle_timers.timers[direction]));

    qatomic_inc(&tgm->restart_pending);

    co = qemu_coroutine_create(throttle_group_restart_queue_entry, rd);
    aio_co_enter(tgm->aio_context, co);
}

void throttle_group_restart_tgm(ThrottleGroupMember *tgm)
{
    ThrottleDirection dir;

    if (tgm->throttle_state) {
        for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
            QEMUTimer *t = tgm->throttle_timers.timers[dir];
            if (timer_pending(t)) {
                /* If there's a pending timer on this tgm, fire it now */
                timer_del(t);
                timer_cb(tgm, dir);
            } else {
                /* Else run the next request from the queue manually */
                throttle_group_restart_queue(tgm, dir);
            }
        }
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
 * @direction: the ThrottleDirection
 */
static void timer_cb(ThrottleGroupMember *tgm, ThrottleDirection direction)
{
    ThrottleState *ts = tgm->throttle_state;
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);

    /* The timer has just been fired, so we can update the flag */
    qemu_mutex_lock(&tg->lock);
    tg->any_timer_armed[direction] = false;
    qemu_mutex_unlock(&tg->lock);

    /* Run the request that was waiting for this timer */
    throttle_group_restart_queue(tgm, direction);
}

static void read_timer_cb(void *opaque)
{
    timer_cb(opaque, THROTTLE_READ);
}

static void write_timer_cb(void *opaque)
{
    timer_cb(opaque, THROTTLE_WRITE);
}

/* Register a ThrottleGroupMember from the throttling group, also initializing
 * its timers and updating its throttle_state pointer to point to it. If a
 * throttling group with that name does not exist yet, it will be created.
 *
 * This function edits throttle_groups and must be called under the global
 * mutex.
 *
 * @tgm:       the ThrottleGroupMember to insert
 * @groupname: the name of the group
 * @ctx:       the AioContext to use
 */
void throttle_group_register_tgm(ThrottleGroupMember *tgm,
                                 const char *groupname,
                                 AioContext *ctx)
{
    ThrottleDirection dir;
    ThrottleState *ts = throttle_group_incref(groupname);
    ThrottleGroup *tg = container_of(ts, ThrottleGroup, ts);

    tgm->throttle_state = ts;
    tgm->aio_context = ctx;
    qatomic_set(&tgm->restart_pending, 0);

    QEMU_LOCK_GUARD(&tg->lock);
    /* If the ThrottleGroup is new set this ThrottleGroupMember as the token */
    for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
        if (!tg->tokens[dir]) {
            tg->tokens[dir] = tgm;
        }
        qemu_co_queue_init(&tgm->throttled_reqs[dir]);
    }

    QLIST_INSERT_HEAD(&tg->head, tgm, round_robin);

    throttle_timers_init(&tgm->throttle_timers,
                         tgm->aio_context,
                         tg->clock_type,
                         read_timer_cb,
                         write_timer_cb,
                         tgm);
    qemu_co_mutex_init(&tgm->throttled_reqs_lock);
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
    ThrottleDirection dir;

    if (!ts) {
        /* Discard already unregistered tgm */
        return;
    }

    /* Wait for throttle_group_restart_queue_entry() coroutines to finish */
    AIO_WAIT_WHILE(tgm->aio_context, qatomic_read(&tgm->restart_pending) > 0);

    WITH_QEMU_LOCK_GUARD(&tg->lock) {
        for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
            assert(tgm->pending_reqs[dir] == 0);
            assert(qemu_co_queue_empty(&tgm->throttled_reqs[dir]));
            assert(!timer_pending(tgm->throttle_timers.timers[dir]));
            if (tg->tokens[dir] == tgm) {
                token = throttle_group_next_tgm(tgm);
                /* Take care of the case where this is the last tgm in the group */
                if (token == tgm) {
                    token = NULL;
                }
                tg->tokens[dir] = token;
            }
        }

        /* remove the current tgm from the list */
        QLIST_REMOVE(tgm, round_robin);
        throttle_timers_destroy(&tgm->throttle_timers);
    }

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
    ThrottleGroup *tg = container_of(tgm->throttle_state, ThrottleGroup, ts);
    ThrottleTimers *tt = &tgm->throttle_timers;
    ThrottleDirection dir;

    /* Requests must have been drained */
    for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
        assert(tgm->pending_reqs[dir] == 0);
        assert(qemu_co_queue_empty(&tgm->throttled_reqs[dir]));
    }

    /* Kick off next ThrottleGroupMember, if necessary */
    WITH_QEMU_LOCK_GUARD(&tg->lock) {
        for (dir = THROTTLE_READ; dir < THROTTLE_MAX; dir++) {
            if (timer_pending(tt->timers[dir])) {
                tg->any_timer_armed[dir] = false;
                schedule_next_request(tgm, dir);
            }
        }
    }

    throttle_timers_detach_aio_context(tt);
    tgm->aio_context = NULL;
}

#undef THROTTLE_OPT_PREFIX
#define THROTTLE_OPT_PREFIX "x-"

/* Helper struct and array for QOM property setter/getter */
typedef struct {
    const char *name;
    BucketType type;
    enum {
        AVG,
        MAX,
        BURST_LENGTH,
        IOPS_SIZE,
    } category;
} ThrottleParamInfo;

static ThrottleParamInfo properties[] = {
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_TOTAL,
        THROTTLE_OPS_TOTAL, AVG,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_TOTAL_MAX,
        THROTTLE_OPS_TOTAL, MAX,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_TOTAL_MAX_LENGTH,
        THROTTLE_OPS_TOTAL, BURST_LENGTH,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_READ,
        THROTTLE_OPS_READ, AVG,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_READ_MAX,
        THROTTLE_OPS_READ, MAX,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_READ_MAX_LENGTH,
        THROTTLE_OPS_READ, BURST_LENGTH,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_WRITE,
        THROTTLE_OPS_WRITE, AVG,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_WRITE_MAX,
        THROTTLE_OPS_WRITE, MAX,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_WRITE_MAX_LENGTH,
        THROTTLE_OPS_WRITE, BURST_LENGTH,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_TOTAL,
        THROTTLE_BPS_TOTAL, AVG,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_TOTAL_MAX,
        THROTTLE_BPS_TOTAL, MAX,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_TOTAL_MAX_LENGTH,
        THROTTLE_BPS_TOTAL, BURST_LENGTH,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_READ,
        THROTTLE_BPS_READ, AVG,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_READ_MAX,
        THROTTLE_BPS_READ, MAX,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_READ_MAX_LENGTH,
        THROTTLE_BPS_READ, BURST_LENGTH,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_WRITE,
        THROTTLE_BPS_WRITE, AVG,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_WRITE_MAX,
        THROTTLE_BPS_WRITE, MAX,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_BPS_WRITE_MAX_LENGTH,
        THROTTLE_BPS_WRITE, BURST_LENGTH,
    },
    {
        THROTTLE_OPT_PREFIX QEMU_OPT_IOPS_SIZE,
        0, IOPS_SIZE,
    }
};

/* This function edits throttle_groups and must be called under the global
 * mutex */
static void throttle_group_obj_init(Object *obj)
{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);

    tg->clock_type = QEMU_CLOCK_REALTIME;
    if (qtest_enabled()) {
        /* For testing block IO throttling only */
        tg->clock_type = QEMU_CLOCK_VIRTUAL;
    }
    tg->is_initialized = false;
    qemu_mutex_init(&tg->lock);
    throttle_init(&tg->ts);
    QLIST_INIT(&tg->head);
}

/* This function edits throttle_groups and must be called under the global
 * mutex */
static void throttle_group_obj_complete(UserCreatable *obj, Error **errp)
{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);
    ThrottleConfig cfg;

    /* set group name to object id if it exists */
    if (!tg->name && tg->parent_obj.parent) {
        tg->name = g_strdup(object_get_canonical_path_component(OBJECT(obj)));
    }
    /* We must have a group name at this point */
    assert(tg->name);

    /* error if name is duplicate */
    if (throttle_group_exists(tg->name)) {
        error_setg(errp, "A group with this name already exists");
        return;
    }

    /* check validity */
    throttle_get_config(&tg->ts, &cfg);
    if (!throttle_is_valid(&cfg, errp)) {
        return;
    }
    throttle_config(&tg->ts, tg->clock_type, &cfg);
    QTAILQ_INSERT_TAIL(&throttle_groups, tg, list);
    tg->is_initialized = true;
}

/* This function edits throttle_groups and must be called under the global
 * mutex */
static void throttle_group_obj_finalize(Object *obj)
{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);
    if (tg->is_initialized) {
        QTAILQ_REMOVE(&throttle_groups, tg, list);
    }
    qemu_mutex_destroy(&tg->lock);
    g_free(tg->name);
}

static void throttle_group_set(Object *obj, Visitor *v, const char * name,
                               void *opaque, Error **errp)

{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);
    ThrottleConfig *cfg;
    ThrottleParamInfo *info = opaque;
    int64_t value;

    /* If we have finished initialization, don't accept individual property
     * changes through QOM. Throttle configuration limits must be set in one
     * transaction, as certain combinations are invalid.
     */
    if (tg->is_initialized) {
        error_setg(errp, "Property cannot be set after initialization");
        return;
    }

    if (!visit_type_int64(v, name, &value, errp)) {
        return;
    }
    if (value < 0) {
        error_setg(errp, "Property values cannot be negative");
        return;
    }

    cfg = &tg->ts.cfg;
    switch (info->category) {
    case AVG:
        cfg->buckets[info->type].avg = value;
        break;
    case MAX:
        cfg->buckets[info->type].max = value;
        break;
    case BURST_LENGTH:
        if (value > UINT_MAX) {
            error_setg(errp, "%s value must be in the" "range [0, %u]",
                       info->name, UINT_MAX);
            return;
        }
        cfg->buckets[info->type].burst_length = value;
        break;
    case IOPS_SIZE:
        cfg->op_size = value;
        break;
    }
}

static void throttle_group_get(Object *obj, Visitor *v, const char *name,
                               void *opaque, Error **errp)
{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);
    ThrottleConfig cfg;
    ThrottleParamInfo *info = opaque;
    int64_t value;

    throttle_get_config(&tg->ts, &cfg);
    switch (info->category) {
    case AVG:
        value = cfg.buckets[info->type].avg;
        break;
    case MAX:
        value = cfg.buckets[info->type].max;
        break;
    case BURST_LENGTH:
        value = cfg.buckets[info->type].burst_length;
        break;
    case IOPS_SIZE:
        value = cfg.op_size;
        break;
    }

    visit_type_int64(v, name, &value, errp);
}

static void throttle_group_set_limits(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)

{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);
    ThrottleConfig cfg;
    ThrottleLimits *argp;
    Error *local_err = NULL;

    if (!visit_type_ThrottleLimits(v, name, &argp, errp)) {
        return;
    }
    qemu_mutex_lock(&tg->lock);
    throttle_get_config(&tg->ts, &cfg);
    throttle_limits_to_config(argp, &cfg, &local_err);
    if (local_err) {
        goto unlock;
    }
    throttle_config(&tg->ts, tg->clock_type, &cfg);

unlock:
    qemu_mutex_unlock(&tg->lock);
    qapi_free_ThrottleLimits(argp);
    error_propagate(errp, local_err);
}

static void throttle_group_get_limits(Object *obj, Visitor *v,
                                      const char *name, void *opaque,
                                      Error **errp)
{
    ThrottleGroup *tg = THROTTLE_GROUP(obj);
    ThrottleConfig cfg;
    ThrottleLimits arg = { 0 };
    ThrottleLimits *argp = &arg;

    qemu_mutex_lock(&tg->lock);
    throttle_get_config(&tg->ts, &cfg);
    qemu_mutex_unlock(&tg->lock);

    throttle_config_to_limits(&cfg, argp);

    visit_type_ThrottleLimits(v, name, &argp, errp);
}

static bool throttle_group_can_be_deleted(UserCreatable *uc)
{
    return OBJECT(uc)->ref == 1;
}

static void throttle_group_obj_class_init(ObjectClass *klass,
                                          const void *class_data)
{
    size_t i = 0;
    UserCreatableClass *ucc = USER_CREATABLE_CLASS(klass);

    ucc->complete = throttle_group_obj_complete;
    ucc->can_be_deleted = throttle_group_can_be_deleted;

    /* individual properties */
    for (i = 0; i < sizeof(properties) / sizeof(ThrottleParamInfo); i++) {
        object_class_property_add(klass,
                                  properties[i].name,
                                  "int",
                                  throttle_group_get,
                                  throttle_group_set,
                                  NULL, &properties[i]);
    }

    /* ThrottleLimits */
    object_class_property_add(klass,
                              "limits", "ThrottleLimits",
                              throttle_group_get_limits,
                              throttle_group_set_limits,
                              NULL, NULL);
}

static const TypeInfo throttle_group_info = {
    .name = TYPE_THROTTLE_GROUP,
    .parent = TYPE_OBJECT,
    .class_init = throttle_group_obj_class_init,
    .instance_size = sizeof(ThrottleGroup),
    .instance_init = throttle_group_obj_init,
    .instance_finalize = throttle_group_obj_finalize,
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    },
};

static void throttle_groups_init(void)
{
    type_register_static(&throttle_group_info);
}

type_init(throttle_groups_init);
