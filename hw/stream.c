#include "stream.h"

void
stream_push(StreamSlave *sink, uint8_t *buf, size_t len, uint32_t *app)
{
    StreamSlaveClass *k =  STREAM_SLAVE_GET_CLASS(sink);

    k->push(sink, buf, len, app);
}

static TypeInfo stream_slave_info = {
    .name          = TYPE_STREAM_SLAVE,
    .parent        = TYPE_INTERFACE,
    .class_size = sizeof(StreamSlaveClass),
};


static void stream_slave_register_types(void)
{
    type_register_static(&stream_slave_info);
}

type_init(stream_slave_register_types)
