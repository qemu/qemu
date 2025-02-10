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

void replay_save_input_event(InputEvent *evt)
{
    InputKeyEvent *key;
    InputBtnEvent *btn;
    InputMoveEvent *move;
    InputMultiTouchEvent *mtt;
    replay_put_dword(evt->type);

    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        key = evt->u.key.data;
        replay_put_dword(key->key->type);

        switch (key->key->type) {
        case KEY_VALUE_KIND_NUMBER:
            replay_put_qword(key->key->u.number.data);
            replay_put_byte(key->down);
            break;
        case KEY_VALUE_KIND_QCODE:
            replay_put_dword(key->key->u.qcode.data);
            replay_put_byte(key->down);
            break;
        case KEY_VALUE_KIND__MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        replay_put_dword(btn->button);
        replay_put_byte(btn->down);
        break;
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        replay_put_dword(move->axis);
        replay_put_qword(move->value);
        break;
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        replay_put_dword(move->axis);
        replay_put_qword(move->value);
        break;
    case INPUT_EVENT_KIND_MTT:
        mtt = evt->u.mtt.data;
        replay_put_dword(mtt->type);
        replay_put_qword(mtt->slot);
        replay_put_qword(mtt->tracking_id);
        replay_put_dword(mtt->axis);
        replay_put_qword(mtt->value);
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }
}

InputEvent *replay_read_input_event(void)
{
    InputEvent evt;
    KeyValue keyValue;
    InputKeyEvent key;
    key.key = &keyValue;
    InputBtnEvent btn;
    InputMoveEvent rel;
    InputMoveEvent abs;
    InputMultiTouchEvent mtt;

    evt.type = replay_get_dword();
    switch (evt.type) {
    case INPUT_EVENT_KIND_KEY:
        evt.u.key.data = &key;
        evt.u.key.data->key->type = replay_get_dword();

        switch (evt.u.key.data->key->type) {
        case KEY_VALUE_KIND_NUMBER:
            evt.u.key.data->key->u.number.data = replay_get_qword();
            evt.u.key.data->down = replay_get_byte();
            break;
        case KEY_VALUE_KIND_QCODE:
            evt.u.key.data->key->u.qcode.data = (QKeyCode)replay_get_dword();
            evt.u.key.data->down = replay_get_byte();
            break;
        case KEY_VALUE_KIND__MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        evt.u.btn.data = &btn;
        evt.u.btn.data->button = (InputButton)replay_get_dword();
        evt.u.btn.data->down = replay_get_byte();
        break;
    case INPUT_EVENT_KIND_REL:
        evt.u.rel.data = &rel;
        evt.u.rel.data->axis = (InputAxis)replay_get_dword();
        evt.u.rel.data->value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND_ABS:
        evt.u.abs.data = &abs;
        evt.u.abs.data->axis = (InputAxis)replay_get_dword();
        evt.u.abs.data->value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND_MTT:
        evt.u.mtt.data = &mtt;
        evt.u.mtt.data->type = (InputMultiTouchType)replay_get_dword();
        evt.u.mtt.data->slot = replay_get_qword();
        evt.u.mtt.data->tracking_id = replay_get_qword();
        evt.u.mtt.data->axis = (InputAxis)replay_get_dword();
        evt.u.mtt.data->value = replay_get_qword();
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }

    return QAPI_CLONE(InputEvent, &evt);
}

void replay_input_event(QemuConsole *src, InputEvent *evt)
{
    if (replay_mode == REPLAY_MODE_PLAY) {
        /* Nothing */
    } else if (replay_mode == REPLAY_MODE_RECORD) {
        replay_add_input_event(QAPI_CLONE(InputEvent, evt));
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
