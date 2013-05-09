#ifndef QEMU_HID_H
#define QEMU_HID_H

#include "migration/vmstate.h"

#define HID_MOUSE     1
#define HID_TABLET    2
#define HID_KEYBOARD  3

typedef struct HIDPointerEvent {
    int32_t xdx, ydy; /* relative iff it's a mouse, otherwise absolute */
    int32_t dz, buttons_state;
} HIDPointerEvent;

#define QUEUE_LENGTH    16 /* should be enough for a triple-click */
#define QUEUE_MASK      (QUEUE_LENGTH-1u)
#define QUEUE_INCR(v)   ((v)++, (v) &= QUEUE_MASK)

typedef struct HIDState HIDState;
typedef void (*HIDEventFunc)(HIDState *s);

typedef struct HIDMouseState {
    HIDPointerEvent queue[QUEUE_LENGTH];
    int mouse_grabbed;
    QEMUPutMouseEntry *eh_entry;
} HIDMouseState;

typedef struct HIDKeyboardState {
    uint32_t keycodes[QUEUE_LENGTH];
    uint16_t modifiers;
    uint8_t leds;
    uint8_t key[16];
    int32_t keys;
    QEMUPutKbdEntry *eh_entry;
} HIDKeyboardState;

struct HIDState {
    union {
        HIDMouseState ptr;
        HIDKeyboardState kbd;
    };
    uint32_t head; /* index into circular queue */
    uint32_t n;
    int kind;
    int32_t protocol;
    uint8_t idle;
    bool idle_pending;
    QEMUTimer *idle_timer;
    HIDEventFunc event;
};

void hid_init(HIDState *hs, int kind, HIDEventFunc event);
void hid_reset(HIDState *hs);
void hid_free(HIDState *hs);

bool hid_has_events(HIDState *hs);
void hid_set_next_idle(HIDState *hs);
void hid_pointer_activate(HIDState *hs);
int hid_pointer_poll(HIDState *hs, uint8_t *buf, int len);
int hid_keyboard_poll(HIDState *hs, uint8_t *buf, int len);
int hid_keyboard_write(HIDState *hs, uint8_t *buf, int len);

extern const VMStateDescription vmstate_hid_keyboard_device;

#define VMSTATE_HID_KEYBOARD_DEVICE(_field, _state) {                \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(HIDState),                                  \
    .vmsd       = &vmstate_hid_keyboard_device,                      \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, HIDState),    \
}

extern const VMStateDescription vmstate_hid_ptr_device;

#define VMSTATE_HID_POINTER_DEVICE(_field, _state) {                 \
    .name       = (stringify(_field)),                               \
    .size       = sizeof(HIDState),                                  \
    .vmsd       = &vmstate_hid_ptr_device,                           \
    .flags      = VMS_STRUCT,                                        \
    .offset     = vmstate_offset_value(_state, _field, HIDState),    \
}


#endif /* QEMU_HID_H */
