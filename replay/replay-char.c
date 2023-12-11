/*
 * replay-char.c
 *
 * Copyright (c) 2010-2016 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "sysemu/replay.h"
#include "replay-internal.h"
#include "chardev/char.h"

/* Char drivers that generate qemu_chr_be_write events
   that should be saved into the log. */
static Chardev **char_drivers;
static int drivers_count;

/* Char event attributes. */
typedef struct CharEvent {
    int id;
    uint8_t *buf;
    size_t len;
} CharEvent;

static int find_char_driver(Chardev *chr)
{
    int i = 0;
    for ( ; i < drivers_count ; ++i) {
        if (char_drivers[i] == chr) {
            return i;
        }
    }
    return -1;
}

void replay_register_char_driver(Chardev *chr)
{
    if (replay_mode == REPLAY_MODE_NONE) {
        return;
    }
    char_drivers = g_realloc(char_drivers,
                             sizeof(*char_drivers) * (drivers_count + 1));
    char_drivers[drivers_count++] = chr;
}

void replay_chr_be_write(Chardev *s, const uint8_t *buf, int len)
{
    CharEvent *event = g_new0(CharEvent, 1);

    event->id = find_char_driver(s);
    if (event->id < 0) {
        fprintf(stderr, "Replay: cannot find char driver\n");
        exit(1);
    }
    event->buf = g_malloc(len);
    memcpy(event->buf, buf, len);
    event->len = len;

    replay_add_event(REPLAY_ASYNC_EVENT_CHAR_READ, event, NULL, 0);
}

void replay_event_char_read_run(void *opaque)
{
    CharEvent *event = (CharEvent *)opaque;

    qemu_chr_be_write_impl(char_drivers[event->id], event->buf,
                           (int)event->len);

    g_free(event->buf);
    g_free(event);
}

void replay_event_char_read_save(void *opaque)
{
    CharEvent *event = (CharEvent *)opaque;

    replay_put_byte(event->id);
    replay_put_array(event->buf, event->len);
}

void *replay_event_char_read_load(void)
{
    CharEvent *event = g_new0(CharEvent, 1);

    event->id = replay_get_byte();
    replay_get_array_alloc(&event->buf, &event->len);

    return event;
}

void replay_char_write_event_save(int res, int offset)
{
    g_assert(replay_mutex_locked());

    replay_save_instructions();
    replay_put_event(EVENT_CHAR_WRITE);
    replay_put_dword(res);
    replay_put_dword(offset);
}

void replay_char_write_event_load(int *res, int *offset)
{
    g_assert(replay_mutex_locked());

    replay_account_executed_instructions();
    if (replay_next_event_is(EVENT_CHAR_WRITE)) {
        *res = replay_get_dword();
        *offset = replay_get_dword();
        replay_finish_event();
    } else {
        replay_sync_error("Missing character write event in the replay log");
    }
}

int replay_char_read_all_load(uint8_t *buf)
{
    g_assert(replay_mutex_locked());

    if (replay_next_event_is(EVENT_CHAR_READ_ALL)) {
        size_t size;
        int res;
        replay_get_array(buf, &size);
        replay_finish_event();
        res = (int)size;
        assert(res >= 0);
        return res;
    } else if (replay_next_event_is(EVENT_CHAR_READ_ALL_ERROR)) {
        int res = replay_get_dword();
        replay_finish_event();
        return res;
    } else {
        replay_sync_error("Missing character read all event in the replay log");
    }
}

void replay_char_read_all_save_error(int res)
{
    g_assert(replay_mutex_locked());
    assert(res < 0);
    replay_save_instructions();
    replay_put_event(EVENT_CHAR_READ_ALL_ERROR);
    replay_put_dword(res);
}

void replay_char_read_all_save_buf(uint8_t *buf, int offset)
{
    g_assert(replay_mutex_locked());
    replay_save_instructions();
    replay_put_event(EVENT_CHAR_READ_ALL);
    replay_put_array(buf, offset);
}
