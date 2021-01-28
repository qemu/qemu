/*
 * CAN generic CAN host connection support
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
#include "qom/object_interfaces.h"
#include "net/can_emu.h"
#include "net/can_host.h"

struct CanBusState {
    Object object;

    QTAILQ_HEAD(, CanBusClientState) clients;
};

static void can_host_disconnect(CanHostState *ch)
{
    CanHostClass *chc = CAN_HOST_GET_CLASS(ch);

    can_bus_remove_client(&ch->bus_client);
    chc->disconnect(ch);
}

static void can_host_connect(CanHostState *ch, Error **errp)
{
    CanHostClass *chc = CAN_HOST_GET_CLASS(ch);
    Error *local_err = NULL;

    if (ch->bus == NULL) {
        error_setg(errp, "'canbus' property not set");
        return;
    }

    chc->connect(ch, &local_err);
    if (local_err) {
        error_propagate(errp, local_err);
        return;
    }

    can_bus_insert_client(ch->bus, &ch->bus_client);
}

static void can_host_unparent(Object *obj)
{
    can_host_disconnect(CAN_HOST(obj));
}

static void can_host_complete(UserCreatable *uc, Error **errp)
{
    can_host_connect(CAN_HOST(uc), errp);
}

static void can_host_class_init(ObjectClass *klass,
                                void *class_data G_GNUC_UNUSED)
{
    UserCreatableClass *uc_klass = USER_CREATABLE_CLASS(klass);

    object_class_property_add_link(klass, "canbus", TYPE_CAN_BUS,
                                   offsetof(CanHostState, bus),
                                   object_property_allow_set_link,
                                   OBJ_PROP_LINK_STRONG);

    klass->unparent = can_host_unparent;
    uc_klass->complete = can_host_complete;
}

static const TypeInfo can_host_info = {
    .parent = TYPE_OBJECT,
    .name = TYPE_CAN_HOST,
    .instance_size = sizeof(CanHostState),
    .class_size = sizeof(CanHostClass),
    .abstract = true,
    .class_init = can_host_class_init,
    .interfaces = (InterfaceInfo[]) {
        { TYPE_USER_CREATABLE },
        { }
    }
};

static void can_host_register_types(void)
{
    type_register_static(&can_host_info);
}

type_init(can_host_register_types);
