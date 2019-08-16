#ifndef INPUT_H
#define INPUT_H

#include "qapi/qapi-types-ui.h"
#include "qemu/notify.h"

#define INPUT_EVENT_MASK_KEY   (1<<INPUT_EVENT_KIND_KEY)
#define INPUT_EVENT_MASK_BTN   (1<<INPUT_EVENT_KIND_BTN)
#define INPUT_EVENT_MASK_REL   (1<<INPUT_EVENT_KIND_REL)
#define INPUT_EVENT_MASK_ABS   (1<<INPUT_EVENT_KIND_ABS)

#define INPUT_EVENT_ABS_MIN    0x0000
#define INPUT_EVENT_ABS_MAX    0x7FFF

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
void qemu_input_event_send_impl(QemuConsole *src, InputEvent *evt);
void qemu_input_event_sync(void);
void qemu_input_event_sync_impl(void);

void qemu_input_event_send_key(QemuConsole *src, KeyValue *key, bool down);
void qemu_input_event_send_key_number(QemuConsole *src, int num, bool down);
void qemu_input_event_send_key_qcode(QemuConsole *src, QKeyCode q, bool down);
void qemu_input_event_send_key_delay(uint32_t delay_ms);
int qemu_input_key_number_to_qcode(unsigned int nr);
int qemu_input_key_value_to_number(const KeyValue *value);
int qemu_input_key_value_to_qcode(const KeyValue *value);
int qemu_input_key_value_to_scancode(const KeyValue *value, bool down,
                                     int *codes);
int qemu_input_linux_to_qcode(unsigned int lnx);

void qemu_input_queue_btn(QemuConsole *src, InputButton btn, bool down);
void qemu_input_update_buttons(QemuConsole *src, uint32_t *button_map,
                               uint32_t button_old, uint32_t button_new);

bool qemu_input_is_absolute(void);
int qemu_input_scale_axis(int value,
                          int min_in, int max_in,
                          int min_out, int max_out);
void qemu_input_queue_rel(QemuConsole *src, InputAxis axis, int value);
void qemu_input_queue_abs(QemuConsole *src, InputAxis axis, int value,
                          int min_in, int max_in);

void qemu_input_check_mode_change(void);
void qemu_add_mouse_mode_change_notifier(Notifier *notify);
void qemu_remove_mouse_mode_change_notifier(Notifier *notify);

extern const guint qemu_input_map_atset1_to_qcode_len;
extern const guint16 qemu_input_map_atset1_to_qcode[];

extern const guint qemu_input_map_linux_to_qcode_len;
extern const guint16 qemu_input_map_linux_to_qcode[];

extern const guint qemu_input_map_qcode_to_atset1_len;
extern const guint16 qemu_input_map_qcode_to_atset1[];

extern const guint qemu_input_map_qcode_to_atset2_len;
extern const guint16 qemu_input_map_qcode_to_atset2[];

extern const guint qemu_input_map_qcode_to_atset3_len;
extern const guint16 qemu_input_map_qcode_to_atset3[];

extern const guint qemu_input_map_qcode_to_linux_len;
extern const guint16 qemu_input_map_qcode_to_linux[];

extern const guint qemu_input_map_qcode_to_qnum_len;
extern const guint16 qemu_input_map_qcode_to_qnum[];

extern const guint qemu_input_map_qcode_to_sun_len;
extern const guint16 qemu_input_map_qcode_to_sun[];

extern const guint qemu_input_map_qnum_to_qcode_len;
extern const guint16 qemu_input_map_qnum_to_qcode[];

extern const guint qemu_input_map_usb_to_qcode_len;
extern const guint16 qemu_input_map_usb_to_qcode[];

extern const guint qemu_input_map_win32_to_qcode_len;
extern const guint16 qemu_input_map_win32_to_qcode[];

extern const guint qemu_input_map_x11_to_qcode_len;
extern const guint16 qemu_input_map_x11_to_qcode[];

extern const guint qemu_input_map_xorgevdev_to_qcode_len;
extern const guint16 qemu_input_map_xorgevdev_to_qcode[];

extern const guint qemu_input_map_xorgkbd_to_qcode_len;
extern const guint16 qemu_input_map_xorgkbd_to_qcode[];

extern const guint qemu_input_map_xorgxquartz_to_qcode_len;
extern const guint16 qemu_input_map_xorgxquartz_to_qcode[];

extern const guint qemu_input_map_xorgxwin_to_qcode_len;
extern const guint16 qemu_input_map_xorgxwin_to_qcode[];

extern const guint qemu_input_map_osx_to_qcode_len;
extern const guint16 qemu_input_map_osx_to_qcode[];

#endif /* INPUT_H */
