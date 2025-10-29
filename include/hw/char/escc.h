#ifndef HW_ESCC_H
#define HW_ESCC_H

#include "chardev/char-fe.h"
#include "chardev/char-serial.h"
#include "hw/sysbus.h"
#include "ui/input.h"
#include "qom/object.h"

/* escc.c */
#define TYPE_ESCC "escc"
#define ESCC_SIZE 4

OBJECT_DECLARE_SIMPLE_TYPE(ESCCState, ESCC)

typedef enum {
    escc_chn_a, escc_chn_b,
} ESCCChnID;

typedef enum {
    escc_serial, escc_kbd, escc_mouse,
} ESCCChnType;

#define ESCC_SERIO_QUEUE_SIZE 256

typedef struct {
    uint8_t data[ESCC_SERIO_QUEUE_SIZE];
    int rptr, wptr, count;
} ESCCSERIOQueue;

#define ESCC_SERIAL_REGS 16
typedef struct ESCCChannelState {
    qemu_irq irq;
    uint32_t rxint, txint, rxint_under_svc, txint_under_svc;
    struct ESCCChannelState *otherchn;
    uint32_t reg;
    uint8_t wregs[ESCC_SERIAL_REGS], rregs[ESCC_SERIAL_REGS];
    ESCCSERIOQueue queue;
    CharFrontend chr;
    int e0_mode, led_mode, caps_lock_mode, num_lock_mode;
    int disabled;
    int clock;
    uint32_t vmstate_dummy;
    ESCCChnID chn; /* this channel, A (base+4) or B (base+0) */
    ESCCChnType type;
    uint8_t rx, tx;
    QemuInputHandlerState *hs;
    char *sunkbd_layout;
    int sunmouse_dx;
    int sunmouse_dy;
    int sunmouse_buttons;
} ESCCChannelState;

struct ESCCState {
    SysBusDevice parent_obj;

    struct ESCCChannelState chn[2];
    uint32_t it_shift;
    bool bit_swap;
    MemoryRegion mmio;
    uint32_t disabled;
    uint32_t frequency;
};

#endif
