/*
 * replay-input.c
 *
 * Copyright (c) 2010-2015 Institute for System Programming
 *                         of the Russian Academy of Sciences.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#include "qemu/osdep.h"
#include "system/replay.h"
#include "replay-internal.h"
#include "qemu/notify.h"
#include "ui/input.h"
#include "qapi/clone-visitor.h"
#include "qapi/qapi-visit-ui.h"

void replay_save_input_event(QemuInputEvent *evt)
{
    replay_put_dword(evt->type);

    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        replay_put_dword(evt->key.key);
        replay_put_byte(evt->key.down);
        break;
    case INPUT_EVENT_KIND_BTN:
        replay_put_dword(evt->btn.button);
        replay_put_byte(evt->btn.down);
        break;
    case INPUT_EVENT_KIND_REL:
        replay_put_dword(evt->rel.axis);
        replay_put_qword(evt->rel.value);
        break;
    case INPUT_EVENT_KIND_ABS:
        replay_put_dword(evt->abs.axis);
        replay_put_qword(evt->abs.value);
        break;
    case INPUT_EVENT_KIND_MTT:
        replay_put_dword(evt->mtt.type);
        replay_put_qword(evt->mtt.slot);
        replay_put_qword(evt->mtt.tracking_id);
        replay_put_dword(evt->mtt.axis);
        replay_put_qword(evt->mtt.value);
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }
}

QemuInputEvent *replay_read_input_event(void)
{
    QemuInputEvent *evt = g_new(QemuInputEvent, 1);

    evt->type = replay_get_dword();
    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        evt->key.key = replay_get_dword();
        evt->key.down = replay_get_byte();
        break;
    case INPUT_EVENT_KIND_BTN:
        evt->btn.button = (InputButton)replay_get_dword();
        evt->btn.down = replay_get_byte();
        break;
    case INPUT_EVENT_KIND_REL:
        evt->rel.axis = (InputAxis)replay_get_dword();
        evt->rel.value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND_ABS:
        evt->abs.axis = (InputAxis)replay_get_dword();
        evt->abs.value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND_MTT:
        evt->mtt.type = (InputMultiTouchType)replay_get_dword();
        evt->mtt.slot = replay_get_qword();
        evt->mtt.tracking_id = replay_get_qword();
        evt->mtt.axis = (InputAxis)replay_get_dword();
        evt->mtt.value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }

    return evt;
}

void replay_input_event(QemuConsole *src, QemuInputEvent *evt)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        /* Nothing */
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        QemuInputEvent *clone = g_new(QemuInputEvent, 1);
        *clone = *evt;
        replay_add_input_event(clone);
    } else {
        qemu_input_event_send_impl(src, evt);
    }
}

void replay_input_sync_event(void)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        /* Nothing */
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_input_sync_event();
    } else {
        qemu_input_event_sync_impl();
    }
}
