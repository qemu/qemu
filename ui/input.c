#include "sysemu/sysemu.h"
#include "qapi-types.h"
#include "qmp-commands.h"
#include "trace.h"
#include "ui/input.h"
#include "ui/console.h"

struct QemuInputHandlerState {
    DeviceState       *dev;
    QemuInputHandler  *handler;
    int               id;
    int               events;
    QTAILQ_ENTRY(QemuInputHandlerState) node;
};
static QTAILQ_HEAD(, QemuInputHandlerState) handlers =
    QTAILQ_HEAD_INITIALIZER(handlers);
static NotifierList mouse_mode_notifiers =
    NOTIFIER_LIST_INITIALIZER(mouse_mode_notifiers);

QemuInputHandlerState *qemu_input_handler_register(DeviceState *dev,
                                                   QemuInputHandler *handler)
{
    QemuInputHandlerState *s = g_new0(QemuInputHandlerState, 1);
    static int id = 1;

    s->dev = dev;
    s->handler = handler;
    s->id = id++;
    QTAILQ_INSERT_TAIL(&handlers, s, node);

    qemu_input_check_mode_change();
    return s;
}

void qemu_input_handler_activate(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    QTAILQ_INSERT_HEAD(&handlers, s, node);
    qemu_input_check_mode_change();
}

void qemu_input_handler_unregister(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    g_free(s);
    qemu_input_check_mode_change();
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

static void qemu_input_transform_abs_rotate(InputEvent *evt)
{
    switch (graphic_rotate) {
    case 90:
        if (evt->abs->axis == INPUT_AXIS_X) {
            evt->abs->axis = INPUT_AXIS_Y;
        } else if (evt->abs->axis == INPUT_AXIS_Y) {
            evt->abs->axis = INPUT_AXIS_X;
            evt->abs->value = INPUT_EVENT_ABS_SIZE - 1 - evt->abs->value;
        }
        break;
    case 180:
        evt->abs->value = INPUT_EVENT_ABS_SIZE - 1 - evt->abs->value;
        break;
    case 270:
        if (evt->abs->axis == INPUT_AXIS_X) {
            evt->abs->axis = INPUT_AXIS_Y;
            evt->abs->value = INPUT_EVENT_ABS_SIZE - 1 - evt->abs->value;
        } else if (evt->abs->axis == INPUT_AXIS_Y) {
            evt->abs->axis = INPUT_AXIS_X;
        }
        break;
    }
}

static void qemu_input_event_trace(QemuConsole *src, InputEvent *evt)
{
    const char *name;
    int idx = -1;

    if (src) {
        idx = qemu_console_get_index(src);
    }
    switch (evt->kind) {
    case INPUT_EVENT_KIND_KEY:
        switch (evt->key->key->kind) {
        case KEY_VALUE_KIND_NUMBER:
            trace_input_event_key_number(idx, evt->key->key->number,
                                         evt->key->down);
            break;
        case KEY_VALUE_KIND_QCODE:
            name = QKeyCode_lookup[evt->key->key->qcode];
            trace_input_event_key_qcode(idx, name, evt->key->down);
            break;
        case KEY_VALUE_KIND_MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        name = InputButton_lookup[evt->btn->button];
        trace_input_event_btn(idx, name, evt->btn->down);
        break;
    case INPUT_EVENT_KIND_REL:
        name = InputAxis_lookup[evt->rel->axis];
        trace_input_event_rel(idx, name, evt->rel->value);
        break;
    case INPUT_EVENT_KIND_ABS:
        name = InputAxis_lookup[evt->abs->axis];
        trace_input_event_abs(idx, name, evt->abs->value);
        break;
    case INPUT_EVENT_KIND_MAX:
        /* keep gcc happy */
        break;
    }
}

void qemu_input_event_send(QemuConsole *src, InputEvent *evt)
{
    QemuInputHandlerState *s;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    qemu_input_event_trace(src, evt);

    /* pre processing */
    if (graphic_rotate && (evt->kind == INPUT_EVENT_KIND_ABS)) {
            qemu_input_transform_abs_rotate(evt);
    }

    /* send event */
    s = qemu_input_find_handler(1 << evt->kind);
    if (!s) {
        return;
    }
    s->handler->event(s->dev, src, evt);
    s->events++;
}

void qemu_input_event_sync(void)
{
    QemuInputHandlerState *s;

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    trace_input_event_sync();

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

bool qemu_input_is_absolute(void)
{
    QemuInputHandlerState *s;

    s = qemu_input_find_handler(INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS);
    return (s != NULL) && (s->handler->mask & INPUT_EVENT_MASK_ABS);
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

void qemu_input_check_mode_change(void)
{
    static int current_is_absolute;
    int is_absolute;

    is_absolute = qemu_input_is_absolute();

    if (is_absolute != current_is_absolute) {
        trace_input_mouse_mode(is_absolute);
        notifier_list_notify(&mouse_mode_notifiers, NULL);
    }

    current_is_absolute = is_absolute;
}

void qemu_add_mouse_mode_change_notifier(Notifier *notify)
{
    notifier_list_add(&mouse_mode_notifiers, notify);
}

void qemu_remove_mouse_mode_change_notifier(Notifier *notify)
{
    notifier_remove(notify);
}

MouseInfoList *qmp_query_mice(Error **errp)
{
    MouseInfoList *mice_list = NULL;
    MouseInfoList *info;
    QemuInputHandlerState *s;
    bool current = true;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (!(s->handler->mask &
              (INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS))) {
            continue;
        }

        info = g_new0(MouseInfoList, 1);
        info->value = g_new0(MouseInfo, 1);
        info->value->index = s->id;
        info->value->name = g_strdup(s->handler->name);
        info->value->absolute = s->handler->mask & INPUT_EVENT_MASK_ABS;
        info->value->current = current;

        current = false;
        info->next = mice_list;
        mice_list = info;
    }

    return mice_list;
}

void do_mouse_set(Monitor *mon, const QDict *qdict)
{
    QemuInputHandlerState *s;
    int index = qdict_get_int(qdict, "index");
    int found = 0;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->id != index) {
            continue;
        }
        if (!(s->handler->mask & (INPUT_EVENT_MASK_REL |
                                  INPUT_EVENT_MASK_ABS))) {
            error_report("Input device '%s' is not a mouse", s->handler->name);
            return;
        }
        found = 1;
        qemu_input_handler_activate(s);
        break;
    }

    if (!found) {
        error_report("Mouse at index '%d' not found", index);
    }

    qemu_input_check_mode_change();
}
