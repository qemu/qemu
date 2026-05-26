#include "qemu/osdep.h"
#include "system/system.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-ui.h"
#include "trace.h"
#include "ui/input.h"
#include "ui/console.h"
#include "system/replay.h"
#include "system/runstate.h"

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
    QemuInputEvent evt;
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
        InputEvent *qapi = e->value;
        QemuInputEvent evt;

        evt.type = qapi->type;

        switch (qapi->type) {
        case INPUT_EVENT_KIND_KEY:
            evt.key.key = qemu_input_key_value_to_linux(qapi->u.key.data->key);
            evt.key.down = qapi->u.key.data->down;
            break;

        case INPUT_EVENT_KIND_BTN:
            evt.btn = *qapi->u.btn.data;
            break;

        case INPUT_EVENT_KIND_REL:
            evt.rel = *qapi->u.rel.data;
            break;

        case INPUT_EVENT_KIND_ABS:
            evt.abs = *qapi->u.abs.data;
            break;

        case INPUT_EVENT_KIND_MTT:
            evt.mtt = *qapi->u.mtt.data;
            break;

        default:
            g_assert_not_reached();
        }

        qemu_input_event_send(con, &evt);
    }

    qemu_input_event_sync();
}

static void qemu_input_event_trace(QemuConsole *src, QemuInputEvent *evt)
{
    const char *name;
    int idx = -1;
    QemuInputKeyEvent *key;
    InputBtnEvent *btn;
    InputMoveEvent *move;
    InputMultiTouchEvent *mtt;

    if (src) {
        idx = qemu_console_get_index(src);
    }
    switch (evt->type) {
    case INPUT_EVENT_KIND_KEY:
        key = &evt->key;
        name = QKeyCode_str(qemu_input_linux_to_qcode(key->key));
        trace_input_event_key_qcode(idx, name, key->down);
        break;
    case INPUT_EVENT_KIND_BTN:
        btn = &evt->btn;
        name = btn->button < INPUT_BUTTON__MAX ? InputButton_str(btn->button) : "invalid";
        trace_input_event_btn(idx, name, btn->down);
        break;
    case INPUT_EVENT_KIND_REL:
        move = &evt->rel;
        name = move->axis < INPUT_AXIS__MAX ? InputAxis_str(move->axis) : "invalid";
        trace_input_event_rel(idx, name, move->value);
        break;
    case INPUT_EVENT_KIND_ABS:
        move = &evt->abs;
        name = move->axis < INPUT_AXIS__MAX ? InputAxis_str(move->axis) : "invalid";
        trace_input_event_abs(idx, name, move->value);
        break;
    case INPUT_EVENT_KIND_MTT:
        mtt = &evt->mtt;
        name = mtt->axis < INPUT_AXIS__MAX ? InputAxis_str(mtt->axis) : "invalid";
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
            qemu_input_event_send(item->src, &item->evt);
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
                                   QemuConsole *src, QemuInputEvent *evt)
{
    QemuInputEventQueue *item = g_new0(QemuInputEventQueue, 1);

    item->type = QEMU_INPUT_QUEUE_EVENT;
    item->src = src;
    item->evt = *evt;
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

void qemu_input_event_send_impl(QemuConsole *src, QemuInputEvent *evt)
{
    QemuInputHandlerState *s;

    qemu_input_event_trace(src, evt);

    /* send event */
    s = qemu_input_find_handler(1 << evt->type, src);
    if (!s) {
        return;
    }
    s->handler->event(s->dev, src, evt);
    s->events++;
}

void qemu_input_event_send(QemuConsole *src, QemuInputEvent *evt)
{
    if (evt->type == INPUT_EVENT_KIND_KEY && !evt->key.key) {
        return;
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

void qemu_input_event_send_key_linux(QemuConsole *src, unsigned int lnx,
                                     bool down)
{
    QemuInputEvent evt = {
        .type = INPUT_EVENT_KIND_KEY,
        .key = {
            .key = lnx,
            .down = down,
        },
    };

    if (QTAILQ_EMPTY(&kbd_queue)) {
        qemu_input_event_send(src, &evt);
        qemu_input_event_sync();
    } else if (queue_count < queue_limit) {
        qemu_input_queue_event(&kbd_queue, src, &evt);
        qemu_input_queue_sync(&kbd_queue);
    }
}

void qemu_input_event_send_key_number(QemuConsole *src, int num, bool down)
{
    unsigned int lnx = qemu_input_key_number_to_linux(num);
    qemu_input_event_send_key_linux(src, lnx, down);
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
    QemuInputEvent evt = {
        .type = INPUT_EVENT_KIND_BTN,
        .btn = {
            .button = btn,
            .down = down,
        }
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
    QemuInputEvent evt = {
        .type = INPUT_EVENT_KIND_REL,
        .rel = {
            .axis = axis,
            .value = value,
        },
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_queue_abs(QemuConsole *src, InputAxis axis, int value,
                          int min_in, int max_in)
{
    QemuInputEvent evt = {
        .type = INPUT_EVENT_KIND_ABS,
        .abs = {
            .axis = axis,
            .value = qemu_input_scale_axis(value, min_in, max_in,
                                           INPUT_EVENT_ABS_MIN,
                                           INPUT_EVENT_ABS_MAX),
        },
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_queue_mtt(QemuConsole *src, InputMultiTouchType type,
                          int slot, int tracking_id)
{
    QemuInputEvent evt = {
        .type = INPUT_EVENT_KIND_MTT,
        .mtt = {
            .type = type,
            .slot = slot,
            .tracking_id = tracking_id,
        },
    };

    qemu_input_event_send(src, &evt);
}

void qemu_input_queue_mtt_abs(QemuConsole *src, InputAxis axis, int value,
                              int min_in, int max_in, int slot, int tracking_id)
{
    QemuInputEvent evt = {
        .type = INPUT_EVENT_KIND_MTT,
        .mtt = {
            .type = INPUT_MULTI_TOUCH_TYPE_DATA,
            .slot = slot,
            .tracking_id = tracking_id,
            .axis = axis,
            .value = qemu_input_scale_axis(value, min_in, max_in,
                                           INPUT_EVENT_ABS_MIN,
                                           INPUT_EVENT_ABS_MAX),
        }
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

void qemu_input_touch_event(QemuConsole *con,
                            struct touch_slot touch_slots[INPUT_EVENT_SLOTS_MAX],
                            uint64_t num_slot,
                            int width, int height,
                            double x, double y,
                            InputMultiTouchType type,
                            Error **errp)
{
    struct touch_slot *slot;
    bool needs_sync = false;
    int update;
    int i;

    if (num_slot >= INPUT_EVENT_SLOTS_MAX) {
        error_setg(errp,
                   "Unexpected touch slot number: % " PRId64" >= %d",
                   num_slot, INPUT_EVENT_SLOTS_MAX);
        return;
    }

    slot = &touch_slots[num_slot];
    slot->x = x;
    slot->y = y;

    if (type == INPUT_MULTI_TOUCH_TYPE_BEGIN) {
        slot->tracking_id = num_slot;
    }

    for (i = 0; i < INPUT_EVENT_SLOTS_MAX; ++i) {
        if (i == num_slot) {
            update = type;
        } else {
            update = INPUT_MULTI_TOUCH_TYPE_UPDATE;
        }

        slot = &touch_slots[i];

        if (slot->tracking_id == -1) {
            continue;
        }

        if (update == INPUT_MULTI_TOUCH_TYPE_END) {
            slot->tracking_id = -1;
            qemu_input_queue_mtt(con, update, i, slot->tracking_id);
            needs_sync = true;
        } else {
            qemu_input_queue_mtt(con, update, i, slot->tracking_id);
            qemu_input_queue_btn(con, INPUT_BUTTON_TOUCH, true);
            qemu_input_queue_mtt_abs(con,
                                    INPUT_AXIS_X, (int) slot->x,
                                    0, width,
                                    i, slot->tracking_id);
            qemu_input_queue_mtt_abs(con,
                                    INPUT_AXIS_Y, (int) slot->y,
                                    0, height,
                                    i, slot->tracking_id);
            needs_sync = true;
        }
    }

    if (needs_sync) {
        qemu_input_event_sync();
    }
}
