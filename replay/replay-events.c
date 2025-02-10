/*
 * replay-events.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "system/replay.h"
#include "replay-internal.h"
#include "block/aio.h"
#include "ui/input.h"
#include "hw/core/cpu.h"

typedef struct Event {
    ReplayAsyncEventKind event_kind;
    void *opaque;
    void *opaque2;
    uint64_t id;

    QTAILQ_ENTRY(Event) events;
} Event;

static QTAILQ_HEAD(, Event) events_list = QTAILQ_HEAD_INITIALIZER(events_list);
static bool events_enabled;

/* Functions */

static void replay_run_event(Event *event)
{
    switch (event->event_kind) {
    case REPLAY_ASYNC_EVENT_BH:
        aio_bh_call(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_BH_ONESHOT:
        ((QEMUBHFunc *)event->opaque)(event->opaque2);
        break;
    case REPLAY_ASYNC_EVENT_INPUT:
        qemu_input_event_send_impl(NULL, (InputEvent *)event->opaque);
        qapi_free_InputEvent((InputEvent *)event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_INPUT_SYNC:
        qemu_input_event_sync_impl();
        break;
    case REPLAY_ASYNC_EVENT_CHAR_READ:
        replay_event_char_read_run(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_BLOCK:
        aio_bh_call(event->opaque);
        break;
    case REPLAY_ASYNC_EVENT_NET:
        replay_event_net_run(event->opaque);
        break;
    default:
        error_report("Replay: invalid async event ID (%d) in the queue",
                    event->event_kind);
        exit(1);
        break;
    }
}

void replay_enable_events(void)
{
    if (replay_mode != REPLAY_MODE_NONE) {
        events_enabled = true;
    }
}

bool replay_has_events(void)
{
    return !QTAILQ_EMPTY(&events_list);
}

void replay_flush_events(void)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }

    g_assert(replay_mutex_locked());

    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        replay_run_event(event);
        QTAILQ_REMOVE(&events_list, event, events);
        g_free(event);
    }
}

/*! Adds specified async event to the queue */
void replay_add_event(ReplayAsyncEventKind event_kind,
                      void *opaque,
                      void *opaque2, uint64_t id)
{
    assert(event_kind < REPLAY_ASYNC_COUNT);

    if (!replay_file || replay_mode == REPLAY_MODE_NONE
        || !events_enabled) {
        Event e;
        e.event_kind = event_kind;
        e.opaque = opaque;
        e.opaque2 = opaque2;
        e.id = id;
        replay_run_event(&e);
        return;
    }

    Event *event = g_new0(Event, 1);
    event->event_kind = event_kind;
    event->opaque = opaque;
    event->opaque2 = opaque2;
    event->id = id;

    g_assert(replay_mutex_locked());
    QTAILQ_INSERT_TAIL(&events_list, event, events);
    qemu_cpu_kick(first_cpu);
}

void replay_bh_schedule_event(QEMUBH *bh)
{
    if (events_enabled) {
        uint64_t id = replay_get_current_icount();
        replay_add_event(REPLAY_ASYNC_EVENT_BH, bh, NULL, id);
    } else {
        qemu_bh_schedule(bh);
    }
}

void replay_bh_schedule_oneshot_event(AioContext *ctx,
    QEMUBHFunc *cb, void *opaque)
{
    if (events_enabled) {
        uint64_t id = replay_get_current_icount();
        replay_add_event(REPLAY_ASYNC_EVENT_BH_ONESHOT, cb, opaque, id);
    } else {
        aio_bh_schedule_oneshot(ctx, cb, opaque);
    }
}

void replay_add_input_event(struct InputEvent *event)
{
    replay_add_event(REPLAY_ASYNC_EVENT_INPUT, event, NULL, 0);
}

void replay_add_input_sync_event(void)
{
    replay_add_event(REPLAY_ASYNC_EVENT_INPUT_SYNC, NULL, NULL, 0);
}

void replay_block_event(QEMUBH *bh, uint64_t id)
{
    if (events_enabled) {
        replay_add_event(REPLAY_ASYNC_EVENT_BLOCK, bh, NULL, id);
    } else {
        qemu_bh_schedule(bh);
    }
}

static void replay_save_event(Event *event)
{
    if (replay_mode != REPLAY_MODE_PLAY) {
        /* put the event into the file */
        g_assert(event->event_kind < REPLAY_ASYNC_COUNT);
        replay_put_event(EVENT_ASYNC + event->event_kind);

        /* save event-specific data */
        switch (event->event_kind) {
        case REPLAY_ASYNC_EVENT_BH:
        case REPLAY_ASYNC_EVENT_BH_ONESHOT:
            replay_put_qword(event->id);
            break;
        case REPLAY_ASYNC_EVENT_INPUT:
            replay_save_input_event(event->opaque);
            break;
        case REPLAY_ASYNC_EVENT_INPUT_SYNC:
            break;
        case REPLAY_ASYNC_EVENT_CHAR_READ:
            replay_event_char_read_save(event->opaque);
            break;
        case REPLAY_ASYNC_EVENT_BLOCK:
            replay_put_qword(event->id);
            break;
        case REPLAY_ASYNC_EVENT_NET:
            replay_event_net_save(event->opaque);
            break;
        default:
            error_report("Unknown ID %" PRId64 " of replay event", event->id);
            exit(1);
        }
    }
}

/* Called with replay mutex locked */
void replay_save_events(void)
{
    g_assert(replay_mutex_locked());
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        replay_save_event(event);
        replay_run_event(event);
        QTAILQ_REMOVE(&events_list, event, events);
        g_free(event);
    }
}

static Event *replay_read_event(void)
{
    Event *event;
    ReplayAsyncEventKind event_kind = replay_state.data_kind - EVENT_ASYNC;

    /* Events that has not to be in the queue */
    switch (event_kind) {
    case REPLAY_ASYNC_EVENT_BH:
    case REPLAY_ASYNC_EVENT_BH_ONESHOT:
        if (replay_state.read_event_id == -1) {
            replay_state.read_event_id = replay_get_qword();
        }
        break;
    case REPLAY_ASYNC_EVENT_INPUT:
        event = g_new0(Event, 1);
        event->event_kind = event_kind;
        event->opaque = replay_read_input_event();
        return event;
    case REPLAY_ASYNC_EVENT_INPUT_SYNC:
        event = g_new0(Event, 1);
        event->event_kind = event_kind;
        event->opaque = 0;
        return event;
    case REPLAY_ASYNC_EVENT_CHAR_READ:
        event = g_new0(Event, 1);
        event->event_kind = event_kind;
        event->opaque = replay_event_char_read_load();
        return event;
    case REPLAY_ASYNC_EVENT_BLOCK:
        if (replay_state.read_event_id == -1) {
            replay_state.read_event_id = replay_get_qword();
        }
        break;
    case REPLAY_ASYNC_EVENT_NET:
        event = g_new0(Event, 1);
        event->event_kind = event_kind;
        event->opaque = replay_event_net_load();
        return event;
    default:
        error_report("Unknown ID %d of replay event", event_kind);
        exit(1);
        break;
    }

    QTAILQ_FOREACH(event, &events_list, events) {
        if (event->event_kind == event_kind
            && (replay_state.read_event_id == -1
                || replay_state.read_event_id == event->id)) {
            break;
        }
    }

    if (event) {
        QTAILQ_REMOVE(&events_list, event, events);
    }

    return event;
}

/* Called with replay mutex locked */
void replay_read_events(void)
{
    g_assert(replay_mutex_locked());
    while (replay_state.data_kind >= EVENT_ASYNC
        && replay_state.data_kind <= EVENT_ASYNC_LAST) {
        Event *event = replay_read_event();
        if (!event) {
            break;
        }
        replay_finish_event();
        replay_state.read_event_id = -1;
        replay_run_event(event);

        g_free(event);
    }
}

void replay_init_events(void)
{
    replay_state.read_event_id = -1;
}

void replay_finish_events(void)
{
    events_enabled = false;
    replay_flush_events();
}

bool replay_events_enabled(void)
{
    return events_enabled;
}

uint64_t blkreplay_next_id(void)
{
    if (replay_events_enabled()) {
        return replay_state.block_request_id++;
    }
    return 0;
}
