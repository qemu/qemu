/*
 * QEMU ADB wacom support
 *
 * This file is part of the QEMU distribution
 * (https://gitlab.com/qemu-project/qemu).
 * Copyright (c) 2024 Patrick Eads.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "ui/console.h"
#include "hw/input/adb.h"
#include "migration/vmstate.h"
#include "qemu/module.h"
#include "adb-internal.h"
#include "trace.h"
#include "qom/object.h"

OBJECT_DECLARE_TYPE(WacomState, ADBWacomClass, ADB_WACOM)

struct WacomState {
    /*< public >*/
    ADBDevice parent_obj;
    /*< private >*/

    QemuInputHandlerState *hs;
    int buttons_state, last_buttons_state;
    uint16_t dx, dy, dz;
};

struct ADBWacomClass {
    /*< public >*/
    ADBDeviceClass parent_class;
    /*< private >*/

    DeviceRealize parent_realize;
};

#define ADB_WACOM_BUTTON_LEFT   0x01
#define ADB_WACOM_BUTTON_RIGHT  0x02

static void adb_wacom_handle_event(DeviceState *dev, QemuConsole *src,
                                    InputEvent *evt) {
    WacomState *s = (WacomState *) dev;
    InputMoveEvent *move;
    InputBtnEvent *btn;
    static const int bmap[INPUT_BUTTON__MAX] = {
            [INPUT_BUTTON_LEFT]   = ADB_WACOM_BUTTON_LEFT,
            [INPUT_BUTTON_RIGHT]  = ADB_WACOM_BUTTON_RIGHT,
    };

    switch (evt->type) {
        case INPUT_EVENT_KIND_ABS:

            move = evt->u.abs.data;
            switch (move->axis) {
                case INPUT_AXIS_X:
                    // Digitizer II / Artz lpi 2540 => dpi 5080
                    s->dx = (uint16_t) (move->value * qemu_console_get_width(src, 640) / 2450);
//                    s->dx = (uint16_t) (move->value * qemu_console_get_width(src, 640) / 5080);
                    break;
                case INPUT_AXIS_Y:
                    // 6x8 inch interactive surface => 4:3 aspect ratio
                    s->dy = (uint16_t) (move->value * qemu_console_get_height(src, 480) / 1905);
//                    s->dy = (uint16_t) (move->value * qemu_console_get_height(src, 480) / 3810);
                    break;
                default:
                    break;
            }
            break;

        case INPUT_EVENT_KIND_BTN:
            btn = evt->u.btn.data;
            if (bmap[btn->button]) {
                if (btn->down) {
                    s->buttons_state |= bmap[btn->button];
                } else {
                    s->buttons_state &= ~bmap[btn->button];
                }
            }
            break;

        default:
            /* keep gcc happy */
            break;
    }
}

static const QemuInputHandler adb_wacom_handler = {
        .name  = "QEMU ADB Wacom",
        .mask  = INPUT_EVENT_MASK_BTN | INPUT_EVENT_MASK_ABS,
        .event = adb_wacom_handle_event,
        /*
         * We do not need the .sync handler because unlike e.g. PS/2 where async
         * wacom events are sent over the serial port, an ADB wacom is constantly
         * polled by the host via the adb_wacom_poll() callback.
         */
};

static int adb_wacom_poll(ADBDevice *d, uint8_t *obuf) {
    WacomState *s = ADB_WACOM(d);

    if (s->last_buttons_state == s->buttons_state && !(s->dx || s->dy)) {
        return 0;
    }
    // Not quite any of the WACOM II-S/IV/IVe, BitPad One/Two, or MM 1201/961
    // protocols described in "WACOM Software Interface Reference Manual UD- KT-
    // SD-Series Graphics Wacoms" (herein: "the manual", Ch. 4).
    // It's closest to WACOM II, but possibly because the sync bit is unnecessary
    // with ADB unlike with a standard serial bus, the packet can be condensed
    // into five bytes instead of requiring seven.
    s->last_buttons_state = s->buttons_state;
    obuf[0] = 0xC0 | ((s->dx >> 8) & 0x3F);
    obuf[1] = s->dx & 0xFF;
    obuf[2] = ((s->dy >> 8) & 0xFF);
    obuf[3] = s->dy & 0xFF;
    obuf[4] = s->buttons_state;
    s->dx = s->dy = 0;
    return 5;
}

static int adb_wacom_request(ADBDevice *d, uint8_t *obuf, const uint8_t *buf,
                              int len) {

    WacomState *s = ADB_WACOM(d);
    int cmd, reg, olen;

    if ((buf[0] & 0x0f) == ADB_FLUSH) {
        /* flush wacom fifo */
        s->buttons_state = s->last_buttons_state;
        s->dx = 0;
        s->dy = 0;
        s->dz = 0;
        trace_adb_device_wacom_flush();
        return 0;
    }

    cmd = buf[0] & 0xc;
    reg = buf[0] & 0x3;
    olen = 0;
    switch (cmd) {
        case ADB_WRITEREG:
            switch (reg) {
                default:
//                case 1: // receives 0xFE0449 on initialization. seems to be settings packet
                        // described (the manual, p. 47)?
//                case 2: // receives 0x204A when modifying certain settings in control panel
                        // doesn't seem to vary based on settings values chosen.
                    break;
                case 3:
                    /*
                     * MacOS 9 has a bug in its ADB driver whereby after configuring
                     * the ADB bus devices it sends another write of invalid length
                     * to reg 3. Make sure we ignore it to prevent an address clash
                     * with the previous device.
                     */
                    if (len != 3) {
                        return 0;
                    }

                    switch (buf[2]) {
                        case ADB_CMD_SELF_TEST:
                            break;
                        case ADB_CMD_CHANGE_ID:
                        case ADB_CMD_CHANGE_ID_AND_ACT:
                        case ADB_CMD_CHANGE_ID_AND_ENABLE:
                            d->devaddr = buf[1] & 0xf;
                            break;
                        default:
                            d->devaddr = buf[1] & 0xf;
                            /*
                             * 0x3A: Wacom tablet
                             */
                            if (0x3A == buf[2]) {
                                d->handler = buf[2];
                            }

                            trace_adb_device_wacom_request_change_addr_and_handler(
                                    d->devaddr, d->handler);
                            break;
                    }
            }
            if (reg != 3) trace_adb_device_wacom_writereg(reg, *(uint64_t *) buf);
            break;
        case ADB_READREG:
            switch (reg) {
                case 0:
                    olen = adb_wacom_poll(d, obuf);
                    break;
                case 1:
                    // "WAC 0608 4" -- EISA ID, product ID, buttons - ASCII
                    // it doesn't like the first three being anything else;
                    // the next four don't seem to affect operation
                    // nor does the last (the manual, p. 40).
                    obuf[0] = 0x57;
                    obuf[1] = 0x41;
                    obuf[2] = 0x43;
                    obuf[3] = 0x30;
                    obuf[4] = 0x36;
                    obuf[5] = 0x30;
                    obuf[6] = 0x38;
                    obuf[7] = 4;
                    olen = 8;
                    break;
                case 3:
                    obuf[0] = d->devaddr;
                    obuf[1] = d->handler;
                    olen = 2;
                    break;
                default:
                    break;
            }
            if (reg) {
                trace_adb_device_wacom_readreg(reg, *(uint64_t *) obuf);
            }
            break;
        default:
            break;
    }
    return olen;
}

static bool adb_wacom_has_data(ADBDevice *d) {
    WacomState *s = ADB_WACOM(d);

    return !(s->last_buttons_state == s->buttons_state &&
             s->dx == 0 && s->dy == 0);
}

static void adb_wacom_reset(DeviceState *dev) {
    ADBDevice *d = ADB_DEVICE(dev);
    WacomState *s = ADB_WACOM(dev);

    d->handler = 0x3A;
    d->devaddr = ADB_DEVID_TABLET;
    s->last_buttons_state = s->buttons_state = 0;
    s->dx = s->dy = s->dz = 0;
}

static const VMStateDescription vmstate_adb_wacom = {
        .name = "adb_wacom",
        .version_id = 2,
        .minimum_version_id = 2,
        .fields = (const VMStateField[]) {
                VMSTATE_STRUCT(parent_obj, WacomState, 0, vmstate_adb_device,
                               ADBDevice),
                VMSTATE_INT32(buttons_state, WacomState),
                VMSTATE_INT32(last_buttons_state, WacomState),
                VMSTATE_UINT16(dx, WacomState),
                VMSTATE_UINT16(dy, WacomState),
                VMSTATE_UINT16(dz, WacomState),
                VMSTATE_END_OF_LIST()
        }
};

static void adb_wacom_realizefn(DeviceState *dev, Error **errp) {
    WacomState *s = ADB_WACOM(dev);
    ADBWacomClass *amc = ADB_WACOM_GET_CLASS(dev);

    amc->parent_realize(dev, errp);

    s->hs = qemu_input_handler_register(dev, &adb_wacom_handler);
}

static void adb_wacom_initfn(Object *obj) {
    ADBDevice *d = ADB_DEVICE(obj);

    d->devaddr = ADB_DEVID_TABLET;
}

static void adb_wacom_class_init(ObjectClass *oc, void *data) {
    DeviceClass *dc = DEVICE_CLASS(oc);
    ADBDeviceClass *adc = ADB_DEVICE_CLASS(oc);
    ADBWacomClass *amc = ADB_WACOM_CLASS(oc);

    device_class_set_parent_realize(
            dc, adb_wacom_realizefn,
            &amc->parent_realize);
    set_bit(DEVICE_CATEGORY_INPUT, dc->categories);

    adc->devreq = adb_wacom_request;
    adc->devhasdata = adb_wacom_has_data;
    device_class_set_legacy_reset(dc, adb_wacom_reset);
    dc->vmsd = &vmstate_adb_wacom;
}

static const TypeInfo adb_wacom_type_info = {
        .name = TYPE_ADB_WACOM,
        .parent = TYPE_ADB_DEVICE,
        .instance_size = sizeof(WacomState),
        .instance_init = adb_wacom_initfn,
        .class_init = adb_wacom_class_init,
        .class_size = sizeof(ADBWacomClass),
};

static void adb_wacom_register_types(void) {
    type_register_static(&adb_wacom_type_info);
}

type_init(adb_wacom_register_types)
