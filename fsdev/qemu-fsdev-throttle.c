/*
 * Fsdev Throttle
 *
 * Copyright (C) 2016 Huawei Technologies Duesseldorf GmbH
 *
 * Author: Pradeep Jagadeesh <pradeep.jagadeesh@huawei.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or
 * (at your option) any later version.
 *
 * See the COPYING file in the top-level directory for details.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu-fsdev-throttle.h"
#include "qemu/iov.h"
#include "qemu/option.h"
#include "qemu/main-loop.h"
#include "qemu/throttle-options.h"

static void fsdev_throttle_read_timer_cb(void *opaque)
{
    FsThrottle *fst = opaque;
    qemu_co_enter_next(&fst->throttled_reqs[false], NULL);
}

static void fsdev_throttle_write_timer_cb(void *opaque)
{
    FsThrottle *fst = opaque;
    qemu_co_enter_next(&fst->throttled_reqs[true], NULL);
}

typedef struct {
    FsThrottle *fst;
    bool is_write;
} RestartData;

static bool coroutine_fn throttle_co_restart_queue(FsThrottle *fst,
                                                   bool is_write)
{
    return qemu_co_queue_next(&fst->throttled_reqs[is_write]);
}

static void schedule_next_request(FsThrottle *fst, bool is_write)
{
    bool must_wait = throttle_schedule_timer(&fst->ts, &fst->tt, is_write);
    if (!must_wait) {
        if (qemu_in_coroutine() &&
            throttle_co_restart_queue(fst, is_write)) {
            return;
        } else {
            int64_t now = qemu_clock_get_ns(QEMU_CLOCK_REALTIME);
            timer_mod(fst->tt.timers[is_write], now);
        }
    }
}

static void coroutine_fn throttle_restart_queue_entry(void *opaque)
{
    RestartData *data = opaque;
    bool is_write = data->is_write;
    bool empty_queue = !throttle_co_restart_queue(data->fst, is_write);
    if (empty_queue) {
        schedule_next_request(data->fst, is_write);
    }
}

static void throttle_restart_queues(FsThrottle *fst)
{
    Coroutine *co;
    RestartData rd = {
        .fst = fst,
        .is_write = true
    };
     co = qemu_coroutine_create(throttle_restart_queue_entry, &rd);
    aio_co_enter(fst->ctx, co);
     rd.is_write = false;
     co = qemu_coroutine_create(throttle_restart_queue_entry, &rd);
    aio_co_enter(fst->ctx, co);
}

static void coroutine_fn fsdev_throttle_config(FsThrottle *fst)
{
    if (throttle_enabled(&fst->cfg)) {
        throttle_config(&fst->ts, QEMU_CLOCK_REALTIME, &fst->cfg);
    } else {
        throttle_restart_queues(fst);
    }
}

void fsdev_set_io_throttle(FsdevIOThrottle *arg, FsThrottle *fst, Error **errp)
{
    ThrottleConfig cfg;
    ThrottleLimits *tlimits;

    throttle_get_config(&fst->ts, &cfg);
    tlimits = qapi_FsdevIOThrottle_base(arg);
    throttle_limits_to_config(tlimits, &cfg, errp);

    if (*errp == NULL) {
        fst->cfg = cfg;
        if (!throttle_timers_are_initialized(&fst->tt)) {
            fsdev_throttle_init(fst);
        } else {
            fsdev_throttle_config(fst);
        }
    }
}

void fsdev_get_io_throttle(FsThrottle *fst, FsdevIOThrottle **fs9pcfg,
                           char *fsdevice)
{
    ThrottleConfig cfg = fst->cfg;
    ThrottleLimits *tlimits;
    FsdevIOThrottle *fscfg = g_malloc(sizeof(*fscfg));
    tlimits = qapi_FsdevIOThrottle_base(fscfg);
    fscfg->has_id = true;
    fscfg->id = g_strdup(fsdevice);
    throttle_config_to_limits(&cfg, tlimits);
    *fs9pcfg = fscfg;
}

void fsdev_throttle_parse_opts(QemuOpts *opts, FsThrottle *fst, Error **errp)
{
    throttle_parse_options(&fst->cfg, opts);
    throttle_is_valid(&fst->cfg, errp);
}

void fsdev_throttle_init(FsThrottle *fst)
{
    if (throttle_enabled(&fst->cfg)) {
        throttle_init(&fst->ts);
        fst->ctx = qemu_get_aio_context();
        throttle_timers_init(&fst->tt,
                             fst->ctx,
                             QEMU_CLOCK_REALTIME,
                             fsdev_throttle_read_timer_cb,
                             fsdev_throttle_write_timer_cb,
                             fst);
        throttle_config(&fst->ts, QEMU_CLOCK_REALTIME, &fst->cfg);
        qemu_co_queue_init(&fst->throttled_reqs[0]);
        qemu_co_queue_init(&fst->throttled_reqs[1]);
    }
}

void coroutine_fn fsdev_co_throttle_request(FsThrottle *fst, bool is_write,
                                            struct iovec *iov, int iovcnt)
{
    if (throttle_enabled(&fst->cfg)) {
        if (throttle_schedule_timer(&fst->ts, &fst->tt, is_write) ||
            !qemu_co_queue_empty(&fst->throttled_reqs[is_write])) {
            qemu_co_queue_wait(&fst->throttled_reqs[is_write], NULL);
        }

        throttle_account(&fst->ts, is_write, iov_size(iov, iovcnt));
        schedule_next_request(fst, is_write);
    }
}

void fsdev_throttle_cleanup(FsThrottle *fst)
{
    if (throttle_enabled(&fst->cfg)) {
        throttle_timers_destroy(&fst->tt);
    }
}
