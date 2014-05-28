#ifndef INPUT_H
#define INPUT_H

#include "qapi-types.h"

#define INPUT_EVENT_MASK_KEY   (1<<INPUT_EVENT_KIND_KEY)
#define INPUT_EVENT_MASK_BTN   (1<<INPUT_EVENT_KIND_BTN)
#define INPUT_EVENT_MASK_REL   (1<<INPUT_EVENT_KIND_REL)
#define INPUT_EVENT_MASK_ABS   (1<<INPUT_EVENT_KIND_ABS)

#define INPUT_EVENT_ABS_SIZE   0x8000

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
void qemu_input_handler_deactivate(QemuInputHandlerState *s);
void qemu_input_handler_unregister(QemuInputHandlerState *s);
void qemu_input_handler_bind(QemuInputHandlerState *s,
                             const char *device_id, int head,
                             Error **errp);
void qemu_input_event_send(QemuConsole *src, InputEvent *evt);
void qemu_input_event_sync(void);

InputEvent *qemu_input_event_new_key(KeyValue *key, bool down);
void qemu_input_event_send_key(QemuConsole *src, KeyValue *key, bool down);
void qemu_input_event_send_key_number(QemuConsole *src, int num, bool down);
void qemu_input_event_send_key_qcode(QemuConsole *src, QKeyCode q, bool down);
int qemu_input_key_number_to_qcode(uint8_t nr);
int qemu_input_key_value_to_number(const KeyValue *value);
int qemu_input_key_value_to_qcode(const KeyValue *value);
int qemu_input_key_value_to_scancode(const KeyValue *value, bool down,
                                     int *codes);

InputEvent *qemu_input_event_new_btn(InputButton btn, bool down);
void qemu_input_queue_btn(QemuConsole *src, InputButton btn, bool down);
void qemu_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new);

bool qemu_input_is_absolute(void);
int qemu_input_scale_axis(int value, int size_in, int size_out);
InputEvent *qemu_input_event_new_move(InputEventKind kind,
                                      InputAxis axis, int value);
void qemu_input_queue_rel(QemuConsole *src, InputAxis axis, int value);
void qemu_input_queue_abs(QemuConsole *src, InputAxis axis,
                          int value, int size);

void qemu_input_check_mode_change(void);
void qemu_add_mouse_mode_change_notifier(Notifier *notify);
void qemu_remove_mouse_mode_change_notifier(Notifier *notify);

#endif /* INPUT_H */
