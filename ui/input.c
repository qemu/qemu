#include "qemu/osdep.h"
#include "sysemu/sysemu.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "trace.h"
#include "ui/input.h"
#include "ui/console.h"
#include "sysemu/replay.h"
#include "sysemu/runstate.h"

struct QemuInputHandlerState {
    DeviceState       *dev;
    const QemuInputHandler *handler;
    int               id;
    int               events;
    QemuConsole       *con;
    QTAILQ_ENTRY(QemuInputHandlerState) node;
};

typedef struct QemuInputEventQueue QemuInputEventQueue;
typedef QTAILQ_HEAD(QemuInputEventQueueHead, QemuInputEventQueue)
    QemuInputEventQueueHead;

struct QemuInputEventQueue {
    enum {
        QEMU_INPUT_QUEUE_DELAY = 1,
        QEMU_INPUT_QUEUE_EVENT,
        QEMU_INPUT_QUEUE_SYNC,
    } type;
    QEMUTimer *timer;
    uint32_t delay_ms;
    QemuConsole *src;
    InputEvent *evt;
    QTAILQ_ENTRY(QemuInputEventQueue) node;
};

static QTAILQ_HEAD(, QemuInputHandlerState) handlers =
    QTAILQ_HEAD_INITIALIZER(handlers);
static NotifierList mouse_mode_notifiers =
    NOTIFIER_LIST_INITIALIZER(mouse_mode_notifiers);

static QemuInputEventQueueHead kbd_queue = QTAILQ_HEAD_INITIALIZER(kbd_queue);
static QEMUTimer *kbd_timer;
static uint32_t kbd_default_delay_ms = 10;
static uint32_t queue_count;
static uint32_t queue_limit = 1024;

QemuInputHandlerState *qemu_input_handler_register(DeviceState *dev,
                                            const QemuInputHandler *handler)
{
    QemuInputHandlerState *s = g_new0(QemuInputHandlerState, 1);
    static int id = 1;

    s->dev = dev;
    s->handler = handler;
    s->id = id++;
    QTAILQ_INSERT_TAIL(&handlers, s, node);

    notifier_list_notify(&mouse_mode_notifiers, NULL);
    return s;
}

void qemu_input_handler_activate(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    QTAILQ_INSERT_HEAD(&handlers, s, node);
    notifier_list_notify(&mouse_mode_notifiers, NULL);
}

void qemu_input_handler_deactivate(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    QTAILQ_INSERT_TAIL(&handlers, s, node);
    notifier_list_notify(&mouse_mode_notifiers, NULL);
}

void qemu_input_handler_unregister(QemuInputHandlerState *s)
{
    QTAILQ_REMOVE(&handlers, s, node);
    g_free(s);
    notifier_list_notify(&mouse_mode_notifiers, NULL);
}

void qemu_input_handler_bind(QemuInputHandlerState *s,
                             const char *device_id, int head,
                             Error **errp)
{
    QemuConsole *con;
    Error *err = NULL;

    con = qemu_console_lookup_by_device_name(device_id, head, &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    s->con = con;
}

static QemuInputHandlerState*
qemu_input_find_handler(uint32_t mask, QemuConsole *con)
{
    QemuInputHandlerState *s;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->con == NULL || s->con != con) {
            continue;
        }
        if (mask & s->handler->mask) {
            return s;
        }
    }

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->con != NULL) {
            continue;
        }
        if (mask & s->handler->mask) {
            return s;
        }
    }
    return NULL;
}

void qmp_input_send_event(const char *device,
                          bool has_head, int64_t head,
                          InputEventList *events, Error **errp)
{
    InputEventList *e;
    QemuConsole *con;
    Error *err = NULL;

    con = NULL;
    if (device) {
        if (!has_head) {
            head = 0;
        }
        con = qemu_console_lookup_by_device_name(device, head, &err);
        if (err) {
            error_propagate(errp, err);
            return;
        }
    }

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        error_setg(errp, "VM not running");
        return;
    }

    for (e = events; e != NULL; e = e->next) {
        InputEvent *event = e->value;

        if (!qemu_input_find_handler(1 << event->type, con)) {
            error_setg(errp, "Input handler not found for "
                             "event type %s",
                            InputEventKind_str(event->type));
            return;
        }
    }

    for (e = events; e != NULL; e = e->next) {
        InputEvent *evt = e->value;

        if (evt->type == INPUT_EVENT_KIND_KEY &&
            evt->u.key.data->key->type == KEY_VALUE_KIND_NUMBER) {
            KeyValue *key = evt->u.key.data->key;
            QKeyCode code = qemu_input_key_number_to_qcode(key->u.number.data);
            qemu_input_event_send_key_qcode(con, code, evt->u.key.data->down);
        } else {
            qemu_input_event_send(con, evt);
        }
    }

    qemu_input_event_sync();
}

static int qemu_input_transform_invert_abs_value(int value)
{
  return (int64_t)INPUT_EVENT_ABS_MAX - value + INPUT_EVENT_ABS_MIN;
}

static void qemu_input_transform_abs_rotate(InputEvent *evt)
{
    InputMoveEvent *move = evt->u.abs.data;
    switch (graphic_rotate) {
    case 90:
        if (move->axis == INPUT_AXIS_X) {
            move->axis = INPUT_AXIS_Y;
        } else if (move->axis == INPUT_AXIS_Y) {
            move->axis = INPUT_AXIS_X;
            move->value = qemu_input_transform_invert_abs_value(move->value);
        }
        break;
    case 180:
        move->value = qemu_input_transform_invert_abs_value(move->value);
        break;
    case 270:
        if (move->axis == INPUT_AXIS_X) {
            move->axis = INPUT_AXIS_Y;
            move->value = qemu_input_transform_invert_abs_value(move->value);
        } else if (move->axis == INPUT_AXIS_Y) {
            move->axis = INPUT_AXIS_X;
        }
        break;
    }
}

static void qemu_input_event_trace(QemuConsole *src, InputEvent *evt)
{
    const char *name;
    int qcode, idx = -1;
    InputKeyEvent *key;
    InputBtnEvent *btn;
    InputMoveEvent *move;
    InputMultiTouchEvent *mtt;

    if (src) {
        idx = qemu_console_get_index(src);
    }
    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        key = evt->u.key.data;
        switch (key->key->type) {
        case KEY_VALUE_KIND_NUMBER:
            qcode = qemu_input_key_number_to_qcode(key->key->u.number.data);
            name = QKeyCode_str(qcode);
            trace_input_event_key_number(idx, key->key->u.number.data,
                                         name, key->down);
            break;
        case KEY_VALUE_KIND_QCODE:
            name = QKeyCode_str(key->key->u.qcode.data);
            trace_input_event_key_qcode(idx, name, key->down);
            break;
        case KEY_VALUE_KIND__MAX:
            /* keep gcc happy */
            break;
        }
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = evt->u.btn.data;
        name = InputButton_str(btn->button);
        trace_input_event_btn(idx, name, btn->down);
        break;
    case INPUT_EVENT_KIND_REL:
        move = evt->u.rel.data;
        name = InputAxis_str(move->axis);
        trace_input_event_rel(idx, name, move->value);
        break;
    case INPUT_EVENT_KIND_ABS:
        move = evt->u.abs.data;
        name = InputAxis_str(move->axis);
        trace_input_event_abs(idx, name, move->value);
        break;
    case INPUT_EVENT_KIND_MTT:
        mtt = evt->u.mtt.data;
        name = InputAxis_str(mtt->axis);
        trace_input_event_mtt(idx, name, mtt->value);
        break;
    case INPUT_EVENT_KIND__MAX:
        /* keep gcc happy */
        break;
    }
}

static void qemu_input_queue_process(void *opaque)
{
    QemuInputEventQueueHead *queue = opaque;
    QemuInputEventQueue *item;

    g_assert(!QTAILQ_EMPTY(queue));
    item = QTAILQ_FIRST(queue);
    g_assert(item->type == QEMU_INPUT_QUEUE_DELAY);
    QTAILQ_REMOVE(queue, item, node);
    queue_count--;
    g_free(item);

    while (!QTAILQ_EMPTY(queue)) {
        item = QTAILQ_FIRST(queue);
        switch (item->type) {
        case QEMU_INPUT_QUEUE_DELAY:
            timer_mod(item->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL)
                      + item->delay_ms);
            return;
        case QEMU_INPUT_QUEUE_EVENT:
            qemu_input_event_send(item->src, item->evt);
            qapi_free_InputEvent(item->evt);
            break;
        case QEMU_INPUT_QUEUE_SYNC:
            qemu_input_event_sync();
            break;
        }
        QTAILQ_REMOVE(queue, item, node);
        queue_count--;
        g_free(item);
    }
}

static void qemu_input_queue_delay(QemuInputEventQueueHead *queue,
                                   QEMUTimer *timer, uint32_t delay_ms)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);
    bool start_timer = QTAILQ_EMPTY(queue);

    item->type = QEMU_INPUT_QUEUE_DELAY;
    item->delay_ms = delay_ms;
    item->timer = timer;
    QTAILQ_INSERT_TAIL(queue, item, node);
    queue_count++;

    if (start_timer) {
        timer_mod(item->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL)
                  + item->delay_ms);
    }
}

static void qemu_input_queue_event(QemuInputEventQueueHead *queue,
                                   QemuConsole *src, InputEvent *evt)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);

    item->type = QEMU_INPUT_QUEUE_EVENT;
    item->src = src;
    item->evt = evt;
    QTAILQ_INSERT_TAIL(queue, item, node);
    queue_count++;
}

static void qemu_input_queue_sync(QemuInputEventQueueHead *queue)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);

    item->type = QEMU_INPUT_QUEUE_SYNC;
    QTAILQ_INSERT_TAIL(queue, item, node);
    queue_count++;
}

void qemu_input_event_send_impl(QemuConsole *src, InputEvent *evt)
{
    QemuInputHandlerState *s;

    qemu_input_event_trace(src, evt);

    /* pre processing */
    if (graphic_rotate && (evt->type == INPUT_EVENT_KIND_ABS)) {
            qemu_input_transform_abs_rotate(evt);
    }

    /* send event */
    s = qemu_input_find_handler(1 << evt->type, src);
    if (!s) {
        return;
    }
    s->handler->event(s->dev, src, evt);
    s->events++;
}

void qemu_input_event_send(QemuConsole *src, InputEvent *evt)
{
    /* Expect all parts of QEMU to send events with QCodes exclusively.
     * Key numbers are only supported as end-user input via QMP */
    assert(!(evt->type == INPUT_EVENT_KIND_KEY &&
             evt->u.key.data->key->type == KEY_VALUE_KIND_NUMBER));


    /*
     * 'sysrq' was mistakenly added to hack around the fact that
     * the ps2 driver was not generating correct scancodes sequences
     * when 'alt+print' was pressed. This flaw is now fixed and the
     * 'sysrq' key serves no further purpose. We normalize it to
     * 'print', so that downstream receivers of the event don't
     * need to deal with this mistake
     */
    if (evt->type == INPUT_EVENT_KIND_KEY &&
        evt->u.key.data->key->u.qcode.data == Q_KEY_CODE_SYSRQ) {
        evt->u.key.data->key->u.qcode.data = Q_KEY_CODE_PRINT;
    }

    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    replay_input_event(src, evt);
}

void qemu_input_event_sync_impl(void)
{
    QemuInputHandlerState *s;

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

void qemu_input_event_sync(void)
{
    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    replay_input_sync_event();
}

static InputEvent *qemu_input_event_new_key(KeyValue *key, bool down)
{
    InputEvent *evt = g_new0(InputEvent, 1);
    evt->u.key.data = g_new0(InputKeyEvent, 1);
    evt->type = INPUT_EVENT_KIND_KEY;
    evt->u.key.data->key = key;
    evt->u.key.data->down = down;
    return evt;
}

void qemu_input_event_send_key(QemuConsole *src, KeyValue *key, bool down)
{
    InputEvent *evt;
    evt = qemu_input_event_new_key(key, down);
    if (QTAILQ_EMPTY(&kbd_queue)) {
        qemu_input_event_send(src, evt);
        qemu_input_event_sync();
        qapi_free_InputEvent(evt);
    } else if (queue_count < queue_limit) {
        qemu_input_queue_event(&kbd_queue, src, evt);
        qemu_input_queue_sync(&kbd_queue);
    } else {
        qapi_free_InputEvent(evt);
    }
}

void qemu_input_event_send_key_number(QemuConsole *src, int num, bool down)
{
    QKeyCode code = qemu_input_key_number_to_qcode(num);
    qemu_input_event_send_key_qcode(src, code, down);
}

void qemu_input_event_send_key_qcode(QemuConsole *src, QKeyCode q, bool down)
{
    KeyValue *key = g_new0(KeyValue, 1);
    key->type = KEY_VALUE_KIND_QCODE;
    key->u.qcode.data = q;
    qemu_input_event_send_key(src, key, down);
}

void qemu_input_event_send_key_delay(uint32_t delay_ms)
{
    if (!runstate_is_running() && !runstate_check(RUN_STATE_SUSPENDED)) {
        return;
    }

    if (!kbd_timer) {
        kbd_timer = timer_new_full(NULL, QEMU_CLOCK_VIRTUAL,
                                   SCALE_MS, QEMU_TIMER_ATTR_EXTERNAL,
                                   qemu_input_queue_process, &kbd_queue);
    }
    if (queue_count < queue_limit) {
        qemu_input_queue_delay(&kbd_queue, kbd_timer,
                               delay_ms ? delay_ms : kbd_default_delay_ms);
    }
}

void qemu_input_queue_btn(QemuConsole *src, InputButton btn, bool down)
{
    InputBtnEvent bevt = {
        .button = btn,
        .down = down,
    };
    InputEvent evt = {
        .type = INPUT_EVENT_KIND_BTN,
        .u.btn.data = &bevt,
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new)
{
    InputButton btn;
    uint32_t mask;

    for (btn = 0; btn < INPUT_BUTTON__MAX; btn++) {
        mask = button_map[btn];
        if ((button_old & mask) == (button_new & mask)) {
            continue;
        }
        qemu_input_queue_btn(src, btn, button_new & mask);
    }
}

bool qemu_input_is_absolute(QemuConsole *con)
{
    QemuInputHandlerState *s;

    s = qemu_input_find_handler(INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS,
                                con);
    return (s != NULL) && (s->handler->mask & INPUT_EVENT_MASK_ABS);
}

int qemu_input_scale_axis(int value,
                          int min_in, int max_in,
                          int min_out, int max_out)
{
    int64_t range_in = (int64_t)max_in - min_in;
    int64_t range_out = (int64_t)max_out - min_out;

    if (range_in < 1) {
        return min_out + range_out / 2;
    }
    return ((int64_t)value - min_in) * range_out / range_in + min_out;
}

void qemu_input_queue_rel(QemuConsole *src, InputAxis axis, int value)
{
    InputMoveEvent move = {
        .axis = axis,
        .value = value,
    };
    InputEvent evt = {
        .type = INPUT_EVENT_KIND_REL,
        .u.rel.data = &move,
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_queue_abs(QemuConsole *src, InputAxis axis, int value,
                          int min_in, int max_in)
{
    InputMoveEvent move = {
        .axis = axis,
        .value = qemu_input_scale_axis(value, min_in, max_in,
                                       INPUT_EVENT_ABS_MIN,
                                       INPUT_EVENT_ABS_MAX),
    };
    InputEvent evt = {
        .type = INPUT_EVENT_KIND_ABS,
        .u.abs.data = &move,
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_queue_mtt(QemuConsole *src, InputMultiTouchType type,
                          int slot, int tracking_id)
{
    InputMultiTouchEvent mtt = {
        .type = type,
        .slot = slot,
        .tracking_id = tracking_id,
    };
    InputEvent evt = {
        .type = INPUT_EVENT_KIND_MTT,
        .u.mtt.data = &mtt,
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_queue_mtt_abs(QemuConsole *src, InputAxis axis, int value,
                              int min_in, int max_in, int slot, int tracking_id)
{
    InputMultiTouchEvent mtt = {
        .type = INPUT_MULTI_TOUCH_TYPE_DATA,
        .slot = slot,
        .tracking_id = tracking_id,
        .axis = axis,
        .value = qemu_input_scale_axis(value, min_in, max_in,
                                       INPUT_EVENT_ABS_MIN,
                                       INPUT_EVENT_ABS_MAX),
    };
    InputEvent evt = {
        .type = INPUT_EVENT_KIND_MTT,
        .u.mtt.data = &mtt,
    };

    qemu_input_event_send(src, &evt);
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
    MouseInfo *info;
    QemuInputHandlerState *s;
    bool current = true;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (!(s->handler->mask &
              (INPUT_EVENT_MASK_REL | INPUT_EVENT_MASK_ABS))) {
            continue;
        }

        info = g_new0(MouseInfo, 1);
        info->index = s->id;
        info->name = g_strdup(s->handler->name);
        info->absolute = s->handler->mask & INPUT_EVENT_MASK_ABS;
        info->current = current;

        current = false;
        QAPI_LIST_PREPEND(mice_list, info);
    }

    return mice_list;
}

bool qemu_mouse_set(int index, Error **errp)
{
    QemuInputHandlerState *s;

    QTAILQ_FOREACH(s, &handlers, node) {
        if (s->id == index) {
            break;
        }
    }

    if (!s) {
        error_setg(errp, "Mouse at index '%d' not found", index);
        return false;
    }

    if (!(s->handler->mask & (INPUT_EVENT_MASK_REL |
                              INPUT_EVENT_MASK_ABS))) {
        error_setg(errp, "Input device '%s' is not a mouse",
                   s->handler->name);
        return false;
    }

    qemu_input_handler_activate(s);
    notifier_list_notify(&mouse_mode_notifiers, NULL);
    return true;
}
