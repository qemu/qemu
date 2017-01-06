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
#include "qemu-common.h"
#include "qemu/error-report.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "block/aio.h"
#include "ui/input.h"

typedef struct Event {
    ReplayAsyncEventKind event_kind;
    void *opaque;
    void *opaque2;
    uint64_t id;

    QTAILQ_ENTRY(Event) events;
} Event;

static QTAILQ_HEAD(, Event) events_list = QTAILQ_HEAD_INITIALIZER(events_list);
static unsigned int read_event_kind = -1;
static uint64_t read_id = -1;
static int read_checkpoint = -1;

static bool events_enabled;

/* Functions */

static void replay_run_event(Event *event)
{
    switch (event->event_kind) {
    case REPLAY_ASYNC_EVENT_BH:
        aio_bh_call(event->opaque);
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
    events_enabled = true;
}

bool replay_has_events(void)
{
    return !QTAILQ_EMPTY(&events_list);
}

void replay_flush_events(void)
{
    replay_mutex_lock();
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        replay_mutex_unlock();
        replay_run_event(event);
        replay_mutex_lock();
        QTAILQ_REMOVE(&events_list, event, events);
        g_free(event);
    }
    replay_mutex_unlock();
}

void replay_disable_events(void)
{
    if (replay_mode != REPLAY_MODE_NONE) {
        events_enabled = false;
        /* Flush events queue before waiting of completion */
        replay_flush_events();
    }
}

void replay_clear_events(void)
{
    replay_mutex_lock();
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        QTAILQ_REMOVE(&events_list, event, events);

        g_free(event);
    }
    replay_mutex_unlock();
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

    Event *event = g_malloc0(sizeof(Event));
    event->event_kind = event_kind;
    event->opaque = opaque;
    event->opaque2 = opaque2;
    event->id = id;

    replay_mutex_lock();
    QTAILQ_INSERT_TAIL(&events_list, event, events);
    replay_mutex_unlock();
}

void replay_bh_schedule_event(QEMUBH *bh)
{
    if (replay_mode != REPLAY_MODE_NONE && events_enabled) {
        uint64_t id = replay_get_current_step();
        replay_add_event(REPLAY_ASYNC_EVENT_BH, bh, NULL, id);
    } else {
        qemu_bh_schedule(bh);
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
    if (replay_mode != REPLAY_MODE_NONE && events_enabled) {
        replay_add_event(REPLAY_ASYNC_EVENT_BLOCK, bh, NULL, id);
    } else {
        qemu_bh_schedule(bh);
    }
}

static void replay_save_event(Event *event, int checkpoint)
{
    if (replay_mode != REPLAY_MODE_PLAY) {
        /* put the event into the file */
        replay_put_event(EVENT_ASYNC);
        replay_put_byte(checkpoint);
        replay_put_byte(event->event_kind);

        /* save event-specific data */
        switch (event->event_kind) {
        case REPLAY_ASYNC_EVENT_BH:
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
void replay_save_events(int checkpoint)
{
    while (!QTAILQ_EMPTY(&events_list)) {
        Event *event = QTAILQ_FIRST(&events_list);
        replay_save_event(event, checkpoint);

        replay_mutex_unlock();
        replay_run_event(event);
        replay_mutex_lock();
        QTAILQ_REMOVE(&events_list, event, events);
        g_free(event);
    }
}

static Event *replay_read_event(int checkpoint)
{
    Event *event;
    if (read_event_kind == -1) {
        read_checkpoint = replay_get_byte();
        read_event_kind = replay_get_byte();
        read_id = -1;
        replay_check_error();
    }

    if (checkpoint != read_checkpoint) {
        return NULL;
    }

    /* Events that has not to be in the queue */
    switch (read_event_kind) {
    case REPLAY_ASYNC_EVENT_BH:
        if (read_id == -1) {
            read_id = replay_get_qword();
        }
        break;
    case REPLAY_ASYNC_EVENT_INPUT:
        event = g_malloc0(sizeof(Event));
        event->event_kind = read_event_kind;
        event->opaque = replay_read_input_event();
        return event;
    case REPLAY_ASYNC_EVENT_INPUT_SYNC:
        event = g_malloc0(sizeof(Event));
        event->event_kind = read_event_kind;
        event->opaque = 0;
        return event;
    case REPLAY_ASYNC_EVENT_CHAR_READ:
        event = g_malloc0(sizeof(Event));
        event->event_kind = read_event_kind;
        event->opaque = replay_event_char_read_load();
        return event;
    case REPLAY_ASYNC_EVENT_BLOCK:
        if (read_id == -1) {
            read_id = replay_get_qword();
        }
        break;
    case REPLAY_ASYNC_EVENT_NET:
        event = g_malloc0(sizeof(Event));
        event->event_kind = read_event_kind;
        event->opaque = replay_event_net_load();
        return event;
    default:
        error_report("Unknown ID %d of replay event", read_event_kind);
        exit(1);
        break;
    }

    QTAILQ_FOREACH(event, &events_list, events) {
        if (event->event_kind == read_event_kind
            && (read_id == -1 || read_id == event->id)) {
            break;
        }
    }

    if (event) {
        QTAILQ_REMOVE(&events_list, event, events);
    } else {
        return NULL;
    }

    /* Read event-specific data */

    return event;
}

/* Called with replay mutex locked */
void replay_read_events(int checkpoint)
{
    while (replay_state.data_kind == EVENT_ASYNC) {
        Event *event = replay_read_event(checkpoint);
        if (!event) {
            break;
        }
        replay_mutex_unlock();
        replay_run_event(event);
        replay_mutex_lock();

        g_free(event);
        replay_finish_event();
        read_event_kind = -1;
    }
}

void replay_init_events(void)
{
    read_event_kind = -1;
}

void replay_finish_events(void)
{
    events_enabled = false;
    replay_clear_events();
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
