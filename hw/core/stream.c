#include "qemu/osdep.h"
#include "hw/stream.h"
#include "qemu/module.h"

size_t
stream_push(StreamSink *sink, uint8_t *buf, size_t len, bool eop)
{
    StreamSinkClass *k =  STREAM_SINK_GET_CLASS(sink);

    return k->push(sink, buf, len, eop);
}

bool
stream_can_push(StreamSink *sink, StreamCanPushNotifyFn notify,
                void *notify_opaque)
{
    StreamSinkClass *k =  STREAM_SINK_GET_CLASS(sink);

    return k->can_push ? k->can_push(sink, notify, notify_opaque) : true;
}

static const TypeInfo stream_sink_info = {
    .name          = TYPE_STREAM_SINK,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(StreamSinkClass),
};


static void stream_sink_register_types(void)
{
    type_register_static(&stream_sink_info);
}

type_init(stream_sink_register_types)
