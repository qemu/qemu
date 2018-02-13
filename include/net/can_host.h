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

#ifndef NET_CAN_HOST_H
#define NET_CAN_HOST_H

#include "net/can_emu.h"

#define TYPE_CAN_HOST "can-host"
#define CAN_HOST_CLASS(klass) \
     OBJECT_CLASS_CHECK(CanHostClass, (klass), TYPE_CAN_HOST)
#define CAN_HOST_GET_CLASS(obj) \
     OBJECT_GET_CLASS(CanHostClass, (obj), TYPE_CAN_HOST)
#define CAN_HOST(obj) \
     OBJECT_CHECK(CanHostState, (obj), TYPE_CAN_HOST)

typedef struct CanHostState {
    ObjectClass oc;

    CanBusState *bus;
    CanBusClientState bus_client;
} CanHostState;

typedef struct CanHostClass {
    ObjectClass oc;

    void (*connect)(CanHostState *ch, Error **errp);
    void (*disconnect)(CanHostState *ch);
} CanHostClass;

#endif
