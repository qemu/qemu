#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qapi/qapi-commands-virtio.h"

static void *qmp_virtio_unsupported(Error **errp)
{
    error_setg(errp, "Virtio is disabled");
    return NULL;
}

VirtioInfoList *qmp_x_query_virtio(Error **errp)
{
    return qmp_virtio_unsupported(errp);
}

VirtioStatus *qmp_x_query_virtio_status(const char *path, Error **errp)
{
    return qmp_virtio_unsupported(errp);
}

VirtVhostQueueStatus *qmp_x_query_virtio_vhost_queue_status(const char *path,
                                                            uint16_t queue,
                                                            Error **errp)
{
    return qmp_virtio_unsupported(errp);
}

VirtQueueStatus *qmp_x_query_virtio_queue_status(const char *path,
                                                 uint16_t queue,
                                                 Error **errp)
{
    return qmp_virtio_unsupported(errp);
}

VirtioQueueElement *qmp_x_query_virtio_queue_element(const char *path,
                                                     uint16_t queue,
                                                     bool has_index,
                                                     uint16_t index,
                                                     Error **errp)
{
    return qmp_virtio_unsupported(errp);
}
