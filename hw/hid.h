#ifndef QEMU_HID_H
#define QEMU_HID_H

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
} HIDKeyboardState;

struct HIDState {
    union {
        HIDMouseState ptr;
        HIDKeyboardState kbd;
    };
    uint32_t head; /* index into circular queue */
    uint32_t n;
    int kind;
    HIDEventFunc event;
};

void hid_init(HIDState *hs, int kind, HIDEventFunc event);
void hid_reset(HIDState *hs);
void hid_free(HIDState *hs);

bool hid_has_events(HIDState *hs);
int hid_pointer_poll(HIDState *hs, uint8_t *buf, int len);
int hid_keyboard_poll(HIDState *hs, uint8_t *buf, int len);
int hid_keyboard_write(HIDState *hs, uint8_t *buf, int len);

#endif /* QEMU_HID_H */
