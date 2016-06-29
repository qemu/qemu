#ifndef STREAM_H
#define STREAM_H

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

typedef void (*StreamCanPushNotifyFn)(void *opaque);

typedef struct StreamSlaveClass {
    InterfaceClass parent;
    /**
     * can push - determine if a stream slave is capable of accepting at least
     * one byte of data. Returns false if cannot accept. If not implemented, the
     * slave is assumed to always be capable of receiving.
     * @notify: Optional callback that the slave will call when the slave is
     * capable of receiving again. Only called if false is returned.
     * @notify_opaque: opaque data to pass to notify call.
     */
    bool (*can_push)(StreamSlave *obj, StreamCanPushNotifyFn notify,
                     void *notify_opaque);
    /**
     * push - push data to a Stream slave. The number of bytes pushed is
     * returned. If the slave short returns, the master must wait before trying
     * again, the slave may continue to just return 0 waiting for the vm time to
     * advance. The can_push() function can be used to trap the point in time
     * where the slave is ready to receive again, otherwise polling on a QEMU
     * timer will work.
     * @obj: Stream slave to push to
     * @buf: Data to write
     * @len: Maximum number of bytes to write
     */
    size_t (*push)(StreamSlave *obj, unsigned char *buf, size_t len);
} StreamSlaveClass;

size_t
stream_push(StreamSlave *sink, uint8_t *buf, size_t len);

bool
stream_can_push(StreamSlave *sink, StreamCanPushNotifyFn notify,
                void *notify_opaque);


#endif /* STREAM_H */
