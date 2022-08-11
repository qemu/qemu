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
