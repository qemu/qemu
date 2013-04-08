#ifndef STREAM_H
#define STREAM_H 1

#include "qemu-common.h"
#include "qom/object.h"

/* stream slave. Used until qdev provides a generic way.  */
#define TYPE_STREAM_SLAVE "stream-slave"

#define STREAM_SLAVE_CLASS(klass) \
     OBJECT_CLASS_CHECK(StreamSlaveClass, (klass), TYPE_STREAM_SLAVE)
#define STREAM_SLAVE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(StreamSlaveClass, (obj), TYPE_STREAM_SLAVE)
#define STREAM_SLAVE(obj) \
     INTERFACE_CHECK(StreamSlave, (obj), TYPE_STREAM_SLAVE)

typedef struct StreamSlave {
    Object Parent;
} StreamSlave;

typedef struct StreamSlaveClass {
    InterfaceClass parent;

    void (*push)(StreamSlave *obj, unsigned char *buf, size_t len,
                                                    uint32_t *app);
} StreamSlaveClass;

void
stream_push(StreamSlave *sink, uint8_t *buf, size_t len, uint32_t *app);

#endif /* STREAM_H */
