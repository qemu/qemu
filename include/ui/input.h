#ifndef INPUT_H
#define INPUT_H

#include "qapi-types.h"

#define INPUT_EVENT_MASK_KEY   (1<<INPUT_EVENT_KIND_KEY)
#define INPUT_EVENT_MASK_BTN   (1<<INPUT_EVENT_KIND_BTN)
#define INPUT_EVENT_MASK_REL   (1<<INPUT_EVENT_KIND_REL)
#define INPUT_EVENT_MASK_ABS   (1<<INPUT_EVENT_KIND_ABS)

typedef struct QemuInputHandler QemuInputHandler;
typedef struct QemuInputHandlerState QemuInputHandlerState;

typedef void (*QemuInputHandlerEvent)(DeviceState *dev, QemuConsole *src,
                                      InputEvent *evt);
typedef void (*QemuInputHandlerSync)(DeviceState *dev);

struct QemuInputHandler {
    const char             *name;
    uint32_t               mask;
    QemuInputHandlerEvent  event;
    QemuInputHandlerSync   sync;
};

QemuInputHandlerState *qemu_input_handler_register(DeviceState *dev,
                                                   QemuInputHandler *handler);
void qemu_input_handler_activate(QemuInputHandlerState *s);
void qemu_input_handler_unregister(QemuInputHandlerState *s);
void qemu_input_event_send(QemuConsole *src, InputEvent *evt);
void qemu_input_event_sync(void);

#endif /* INPUT_H */
