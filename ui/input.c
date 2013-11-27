#include "sysemu/sysemu.h"
#include "qapi-types.h"
#include "ui/input.h"

struct QemuInputHandlerState {
    DeviceState       *dev;
    QemuInputHandler  *handler;
    int               id;
    int               events;
    QTAILQ_ENTRY(QemuInputHandlerState) node;
};
static QTAILQ_HEAD(, QemuInputHandlerState) handlers =
    QTAILQ_HEAD_INITIALIZER(handlers);

QemuInputHandlerState *qemu_input_handler_register(DeviceState *dev,
                                                   QemuInputHandler *handler)
{
    QemuInputHandlerState *s = g_new0(QemuInputHandlerState, 1);
    static int id = 1;

    s->dev = dev;
    s->handler = handler;
    s->id = id++;
    QTAILQ_INSERT_TAIL(&handlers, s, node);
    return s;
}

void qemu_input_handler_activate(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    QTAILQ_INSERT_HEAD(&handlers, s, node);
}

void qemu_input_handler_unregister(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    g_free(s);
}

static QemuInputHandlerState*
qemu_input_find_handler(uint32_t mask)
{
    QemuInputHandlerState *s;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (mask & s->handler->mask) {
            return s;
        }
    }
    return NULL;
}

void qemu_input_event_send(QemuConsole *src, InputEvent *evt)
{
    QemuInputHandlerState *s;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    s = qemu_input_find_handler(1 << evt->kind);
    s->handler->event(s->dev, src, evt);
    s->events++;
}

void qemu_input_event_sync(void)
{
    QemuInputHandlerState *s;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    QTAILQ_FOREACH(s, &handlers, node) {
        if (!s->events) {
            continue;
        }
        if (s->handler->sync) {
            s->handler->sync(s->dev);
        }
        s->events = 0;
    }
}

InputEvent *qemu_input_event_new_key(KeyValue *key, bool down)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    evt->key = g_new0(InputKeyEvent, 1);
    evt->kind = INPUT_EVENT_KIND_KEY;
    evt->key->key = key;
    evt->key->down = down;
    return evt;
}

void qemu_input_event_send_key(QemuConsole *src, KeyValue *key, bool down)
{
    InputEvent *evt;
    evt = qemu_input_event_new_key(key, down);
    qemu_input_event_send(src, evt);
    qemu_input_event_sync();
    qapi_free_InputEvent(evt);
}

void qemu_input_event_send_key_number(QemuConsole *src, int num, bool down)
{
    KeyValue *key = g_new0(KeyValue, 1);
    key->kind = KEY_VALUE_KIND_NUMBER;
    key->number = num;
    qemu_input_event_send_key(src, key, down);
}

void qemu_input_event_send_key_qcode(QemuConsole *src, QKeyCode q, bool down)
{
    KeyValue *key = g_new0(KeyValue, 1);
    key->kind = KEY_VALUE_KIND_QCODE;
    key->qcode = q;
    qemu_input_event_send_key(src, key, down);
}

InputEvent *qemu_input_event_new_btn(InputButton btn, bool down)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    evt->btn = g_new0(InputBtnEvent, 1);
    evt->kind = INPUT_EVENT_KIND_BTN;
    evt->btn->button = btn;
    evt->btn->down = down;
    return evt;
}

void qemu_input_queue_btn(QemuConsole *src, InputButton btn, bool down)
{
    InputEvent *evt;
    evt = qemu_input_event_new_btn(btn, down);
    qemu_input_event_send(src, evt);
    qapi_free_InputEvent(evt);
}

void qemu_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new)
{
    InputButton btn;
    uint32_t mask;

    for (btn = 0; btn < INPUT_BUTTON_MAX; btn++) {
        mask = button_map[btn];
        if ((button_old & mask) == (button_new & mask)) {
            continue;
        }
        qemu_input_queue_btn(src, btn, button_new & mask);
    }
}

int qemu_input_scale_axis(int value, int size_in, int size_out)
{
    if (size_in < 2) {
        return size_out / 2;
    }
    return (int64_t)value * (size_out - 1) / (size_in - 1);
}

InputEvent *qemu_input_event_new_move(InputEventKind kind,
                                      InputAxis axis, int value)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    InputMoveEvent *move = g_new0(InputMoveEvent, 1);

    evt->kind = kind;
    evt->data = move;
    move->axis = axis;
    move->value = value;
    return evt;
}

void qemu_input_queue_rel(QemuConsole *src, InputAxis axis, int value)
{
    InputEvent *evt;
    evt = qemu_input_event_new_move(INPUT_EVENT_KIND_REL, axis, value);
    qemu_input_event_send(src, evt);
    qapi_free_InputEvent(evt);
}

void qemu_input_queue_abs(QemuConsole *src, InputAxis axis, int value, int size)
{
    InputEvent *evt;
    int scaled = qemu_input_scale_axis(value, size, INPUT_EVENT_ABS_SIZE);
    evt = qemu_input_event_new_move(INPUT_EVENT_KIND_ABS, axis, scaled);
    qemu_input_event_send(src, evt);
    qapi_free_InputEvent(evt);
}
