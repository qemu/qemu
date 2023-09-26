#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

unsigned int vhost_get_max_memslots(void)
{
    return UINT_MAX;
}

unsigned int vhost_get_free_memslots(void)
{
    return UINT_MAX;
}

bool vhost_user_init(VhostUserState *user, CharBackend *chr, Error **errp)
{
    return false;
}

void vhost_user_cleanup(VhostUserState *user)
{
}

void vhost_toggle_device_iotlb(VirtIODevice *vdev)
{
}
