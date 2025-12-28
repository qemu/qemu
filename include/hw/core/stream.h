#ifndef STREAM_H
#define STREAM_H

#include "qom/object.h"

#define TYPE_STREAM_SINK "stream-sink"

typedef struct StreamSinkClass StreamSinkClass;
DECLARE_CLASS_CHECKERS(StreamSinkClass, STREAM_SINK,
                       TYPE_STREAM_SINK)
#define STREAM_SINK(obj) \
     INTERFACE_CHECK(StreamSink, (obj), TYPE_STREAM_SINK)

typedef struct StreamSink StreamSink;

typedef void (*StreamCanPushNotifyFn)(void *opaque);

struct StreamSinkClass {
    InterfaceClass parent;
    /**
     * can push - determine if a stream sink is capable of accepting at least
     * one byte of data. Returns false if cannot accept. If not implemented, the
     * sink is assumed to always be capable of receiving.
     * @notify: Optional callback that the sink will call when the sink is
     * capable of receiving again. Only called if false is returned.
     * @notify_opaque: opaque data to pass to notify call.
     */
    bool (*can_push)(StreamSink *obj, StreamCanPushNotifyFn notify,
                     void *notify_opaque);
    /**
     * push - push data to a Stream sink. The number of bytes pushed is
     * returned. If the sink short returns, the master must wait before trying
     * again, the sink may continue to just return 0 waiting for the vm time to
     * advance. The can_push() function can be used to trap the point in time
     * where the sink is ready to receive again, otherwise polling on a QEMU
     * timer will work.
     * @obj: Stream sink to push to
     * @buf: Data to write
     * @len: Maximum number of bytes to write
     * @eop: End of packet flag
     */
    size_t (*push)(StreamSink *obj, unsigned char *buf, size_t len, bool eop);
};

size_t
stream_push(StreamSink *sink, uint8_t *buf, size_t len, bool eop);

bool
stream_can_push(StreamSink *sink, StreamCanPushNotifyFn notify,
                void *notify_opaque);


#endif /* STREAM_H */
