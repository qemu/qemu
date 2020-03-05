/*
 * CAN common CAN bus emulation support
 *
 * Copyright (c) 2013-2014 Jin Yang
 * Copyright (c) 2014-2018 Pavel Pisa
 *
 * Initial development supported by Google GSoC 2013 from RTEMS project slot
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

#ifndef NET_CAN_EMU_H
#define NET_CAN_EMU_H

#include "qemu/queue.h"
#include "qom/object.h"

/* NOTE: the following two structures is copied from <linux/can.h>. */

/*
 * Controller Area Network Identifier structure
 *
 * bit 0-28    : CAN identifier (11/29 bit)
 * bit 29      : error frame flag (0 = data frame, 1 = error frame)
 * bit 30      : remote transmission request flag (1 = rtr frame)
 * bit 31      : frame format flag (0 = standard 11 bit, 1 = extended 29 bit)
 */
typedef uint32_t qemu_canid_t;

typedef struct qemu_can_frame {
    qemu_canid_t    can_id;  /* 32 bit CAN_ID + EFF/RTR/ERR flags */
    uint8_t         can_dlc; /* data length code: 0 .. 8 */
    uint8_t         data[8] QEMU_ALIGNED(8);
} qemu_can_frame;

/* Keep defines for QEMU separate from Linux ones for now */

#define QEMU_CAN_EFF_FLAG 0x80000000U /* EFF/SFF is set in the MSB */
#define QEMU_CAN_RTR_FLAG 0x40000000U /* remote transmission request */
#define QEMU_CAN_ERR_FLAG 0x20000000U /* error message frame */

#define QEMU_CAN_SFF_MASK 0x000007FFU /* standard frame format (SFF) */
#define QEMU_CAN_EFF_MASK 0x1FFFFFFFU /* extended frame format (EFF) */

/**
 * struct qemu_can_filter - CAN ID based filter in can_register().
 * @can_id:   relevant bits of CAN ID which are not masked out.
 * @can_mask: CAN mask (see description)
 *
 * Description:
 * A filter matches, when
 *
 *          <received_can_id> & mask == can_id & mask
 *
 * The filter can be inverted (QEMU_CAN_INV_FILTER bit set in can_id) or it can
 * filter for error message frames (QEMU_CAN_ERR_FLAG bit set in mask).
 */
typedef struct qemu_can_filter {
    qemu_canid_t    can_id;
    qemu_canid_t    can_mask;
} qemu_can_filter;

/* QEMU_CAN_INV_FILTER can be set in qemu_can_filter.can_id */
#define QEMU_CAN_INV_FILTER 0x20000000U

typedef struct CanBusClientState CanBusClientState;
typedef struct CanBusState CanBusState;

typedef struct CanBusClientInfo {
    bool (*can_receive)(CanBusClientState *);
    ssize_t (*receive)(CanBusClientState *,
        const struct qemu_can_frame *frames, size_t frames_cnt);
} CanBusClientInfo;

struct CanBusClientState {
    CanBusClientInfo *info;
    CanBusState *bus;
    int link_down;
    QTAILQ_ENTRY(CanBusClientState) next;
    CanBusClientState *peer;
    char *model;
    char *name;
    void (*destructor)(CanBusClientState *);
};

#define TYPE_CAN_BUS "can-bus"
#define CAN_BUS_CLASS(klass) \
     OBJECT_CLASS_CHECK(CanBusClass, (klass), TYPE_CAN_BUS)
#define CAN_BUS_GET_CLASS(obj) \
     OBJECT_GET_CLASS(CanBusClass, (obj), TYPE_CAN_BUS)
#define CAN_BUS(obj) \
     OBJECT_CHECK(CanBusState, (obj), TYPE_CAN_BUS)

int can_bus_filter_match(struct qemu_can_filter *filter, qemu_canid_t can_id);

int can_bus_insert_client(CanBusState *bus, CanBusClientState *client);

int can_bus_remove_client(CanBusClientState *client);

ssize_t can_bus_client_send(CanBusClientState *,
                            const struct qemu_can_frame *frames,
                            size_t frames_cnt);

int can_bus_client_set_filters(CanBusClientState *,
                               const struct qemu_can_filter *filters,
                               size_t filters_cnt);

#endif
