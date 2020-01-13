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

#include "qemu/osdep.h"
#include "chardev/char.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "qapi/error.h"
#include "net/can_emu.h"
#include "qom/object_interfaces.h"

/* CAN DLC to real data length conversion helpers */

static const uint8_t dlc2len[] = {
    0, 1, 2, 3, 4, 5, 6, 7,
    8, 12, 16, 20, 24, 32, 48, 64
};

/* get data length from can_dlc with sanitized can_dlc */
uint8_t can_dlc2len(uint8_t can_dlc)
{
    return dlc2len[can_dlc & 0x0F];
}

static const uint8_t len2dlc[] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8,                              /* 0 - 8 */
    9, 9, 9, 9,                                             /* 9 - 12 */
    10, 10, 10, 10,                                         /* 13 - 16 */
    11, 11, 11, 11,                                         /* 17 - 20 */
    12, 12, 12, 12,                                         /* 21 - 24 */
    13, 13, 13, 13, 13, 13, 13, 13,                         /* 25 - 32 */
    14, 14, 14, 14, 14, 14, 14, 14,                         /* 33 - 40 */
    14, 14, 14, 14, 14, 14, 14, 14,                         /* 41 - 48 */
    15, 15, 15, 15, 15, 15, 15, 15,                         /* 49 - 56 */
    15, 15, 15, 15, 15, 15, 15, 15                          /* 57 - 64 */
};

/* map the sanitized data length to an appropriate data length code */
uint8_t can_len2dlc(uint8_t len)
{
    if (unlikely(len > 64)) {
        return 0xF;
    }

    return len2dlc[len];
}

struct CanBusState {
    Object object;

    QTAILQ_HEAD(, CanBusClientState) clients;
};

static void can_bus_instance_init(Object *object)
{
    CanBusState *bus = (CanBusState *)object;

    QTAILQ_INIT(&bus->clients);
}

int can_bus_insert_client(CanBusState *bus, CanBusClientState *client)
{
    client->bus = bus;
    QTAILQ_INSERT_TAIL(&bus->clients, client, next);
    return 0;
}

int can_bus_remove_client(CanBusClientState *client)
{
    CanBusState *bus = client->bus;
    if (bus == NULL) {
        return 0;
    }

    QTAILQ_REMOVE(&bus->clients, client, next);
    client->bus = NULL;
    return 1;
}

ssize_t can_bus_client_send(CanBusClientState *client,
             const struct qemu_can_frame *frames, size_t frames_cnt)
{
    int ret = 0;
    CanBusState *bus = client->bus;
    CanBusClientState *peer;
    if (bus == NULL) {
        return -1;
    }

    QTAILQ_FOREACH(peer, &bus->clients, next) {
        if (peer->info->can_receive(peer)) {
            if (peer == client) {
                /* No loopback support for now */
                continue;
            }
            if (peer->info->receive(peer, frames, frames_cnt) > 0) {
                ret = 1;
            }
        }
    }

    return ret;
}

int can_bus_filter_match(struct qemu_can_filter *filter, qemu_canid_t can_id)
{
    int m;
    if (((can_id | filter->can_mask) & QEMU_CAN_ERR_FLAG)) {
        return (filter->can_mask & QEMU_CAN_ERR_FLAG) != 0;
    }
    m = (can_id & filter->can_mask) == (filter->can_id & filter->can_mask);
    return filter->can_id & QEMU_CAN_INV_FILTER ? !m : m;
}

int can_bus_client_set_filters(CanBusClientState *client,
             const struct qemu_can_filter *filters, size_t filters_cnt)
{
    return 0;
}


static bool can_bus_can_be_deleted(UserCreatable *uc)
{
    return false;
}

static void can_bus_class_init(ObjectClass *klass,
                                void *class_data G_GNUC_UNUSED)
{
    UserCreatableClass *uc_klass = USER_CREATABLE_CLASS(klass);

    uc_klass->can_be_deleted = can_bus_can_be_deleted;
}

static const TypeInfo can_bus_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_CAN_BUS,
    .instance_size = sizeof(CanBusState),
    .instance_init = can_bus_instance_init,
    .class_init = can_bus_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void can_bus_register_types(void)
{
    type_register_static(&can_bus_info);
}

type_init(can_bus_register_types);
