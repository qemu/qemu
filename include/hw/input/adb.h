/*
 * QEMU ADB emulation shared definitions and prototypes
 *
 * Copyright (c) 2004-2007 Fabrice Bellard
 * Copyright (c) 2007 Jocelyn Mayer
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#ifndef ADB_H
#define ADB_H

#include "hw/qdev-core.h"
#include "qom/object.h"

#define MAX_ADB_DEVICES 16

#define ADB_MAX_OUT_LEN 16

typedef struct ADBDevice ADBDevice;

/* buf = NULL means polling */
typedef int ADBDeviceRequest(ADBDevice *d, uint8_t *buf_out,
                              const uint8_t *buf, int len);

typedef bool ADBDeviceHasData(ADBDevice *d);

#define TYPE_ADB_DEVICE "adb-device"
OBJECT_DECLARE_TYPE(ADBDevice, ADBDeviceClass, ADB_DEVICE)

struct ADBDevice {
    /*< private >*/
    DeviceState parent_obj;
    /*< public >*/

    int devaddr;
    int handler;
};


struct ADBDeviceClass {
    /*< private >*/
    DeviceClass parent_class;
    /*< public >*/

    ADBDeviceRequest *devreq;
    ADBDeviceHasData *devhasdata;
};

#define TYPE_ADB_BUS "apple-desktop-bus"
OBJECT_DECLARE_SIMPLE_TYPE(ADBBusState, ADB_BUS)

#define ADB_STATUS_BUSTIMEOUT  0x1
#define ADB_STATUS_POLLREPLY   0x2

struct ADBBusState {
    /*< private >*/
    BusState parent_obj;
    /*< public >*/

    ADBDevice *devices[MAX_ADB_DEVICES];
    uint16_t pending;
    int nb_devices;
    int poll_index;
    uint8_t status;

    QEMUTimer *autopoll_timer;
    bool autopoll_enabled;
    bool autopoll_blocked;
    uint8_t autopoll_rate_ms;
    uint16_t autopoll_mask;
    void (*autopoll_cb)(void *opaque);
    void *autopoll_cb_opaque;
};

int adb_request(ADBBusState *s, uint8_t *buf_out,
                const uint8_t *buf, int len);
int adb_poll(ADBBusState *s, uint8_t *buf_out, uint16_t poll_mask);

void adb_autopoll_block(ADBBusState *s);
void adb_autopoll_unblock(ADBBusState *s);

void adb_set_autopoll_enabled(ADBBusState *s, bool enabled);
void adb_set_autopoll_rate_ms(ADBBusState *s, int rate_ms);
void adb_set_autopoll_mask(ADBBusState *s, uint16_t mask);
void adb_register_autopoll_callback(ADBBusState *s, void (*cb)(void *opaque),
                                    void *opaque);

#define TYPE_ADB_KEYBOARD "adb-keyboard"
#define TYPE_ADB_MOUSE "adb-mouse"

#endif /* ADB_H */
