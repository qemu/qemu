#include "qemu/osdep.h"
#include "hw/virtio/vhost-user.h"

void vhost_user_qmp_status(struct vhost_dev *dev, VirtioStatus *status)
{
}

bool vhost_user_has_protocol_feature(struct vhost_dev *dev, uint64_t feature)
{
    return false;
}
