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
