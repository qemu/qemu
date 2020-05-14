#include "qemu/osdep.h"
#include "hw/stream.h"
#include "qemu/module.h"

size_t
stream_push(StreamSlave *sink, uint8_t *buf, size_t len, bool eop)
{
    StreamSlaveClass *k =  STREAM_SLAVE_GET_CLASS(sink);

    return k->push(sink, buf, len, eop);
}

bool
stream_can_push(StreamSlave *sink, StreamCanPushNotifyFn notify,
                void *notify_opaque)
{
    StreamSlaveClass *k =  STREAM_SLAVE_GET_CLASS(sink);

    return k->can_push ? k->can_push(sink, notify, notify_opaque) : true;
}

static const TypeInfo stream_slave_info = {
    .name          = TYPE_STREAM_SLAVE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(StreamSlaveClass),
};


static void stream_slave_register_types(void)
{
    type_register_static(&stream_slave_info);
}

type_init(stream_slave_register_types)
