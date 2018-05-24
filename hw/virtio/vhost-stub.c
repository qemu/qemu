#include "qemu/osdep.h"
#include "hw/virtio/vhost.h"
#include "hw/virtio/vhost-user.h"

bool vhost_has_free_slot(void)
{
    return true;
}

VhostUserState *vhost_user_init(void)
{
    return NULL;
}

void vhost_user_cleanup(VhostUserState *user)
{
}
