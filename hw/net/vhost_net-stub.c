/*
 * vhost-net support
 *
 * Copyright Red Hat, Inc. 2010
 *
 * Authors:
 *  Michael S. Tsirkin <mst@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "net/net.h"
#include "net/tap.h"
#include "net/vhost-user.h"

#include "hw/virtio/virtio-net.h"
#include "net/vhost_net.h"
#include "qemu/error-report.h"


uint64_t vhost_net_get_max_queues(VHostNetState *net)
{
    return 1;
}

struct vhost_net *vhost_net_init(VhostNetOptions *options)
{
    error_report("vhost-net support is not compiled in");
    return NULL;
}

int vhost_net_start(VirtIODevice *dev,
                    NetClientState *ncs,
                    int data_queue_pairs, int cvq)
{
    return -ENOSYS;
}
void vhost_net_stop(VirtIODevice *dev,
                    NetClientState *ncs,
                    int data_queue_pairs, int cvq)
{
}

void vhost_net_cleanup(struct vhost_net *net)
{
}

uint64_t vhost_net_get_features(struct vhost_net *net, uint64_t features)
{
    return features;
}

int vhost_net_get_config(struct vhost_net *net,  uint8_t *config,
                         uint32_t config_len)
{
    return 0;
}
int vhost_net_set_config(struct vhost_net *net, const uint8_t *data,
                         uint32_t offset, uint32_t size, uint32_t flags)
{
    return 0;
}

void vhost_net_ack_features(struct vhost_net *net, uint64_t features)
{
}

uint64_t vhost_net_get_acked_features(VHostNetState *net)
{
    return 0;
}

bool vhost_net_virtqueue_pending(VHostNetState *net, int idx)
{
    return false;
}

void vhost_net_virtqueue_mask(VHostNetState *net, VirtIODevice *dev,
                              int idx, bool mask)
{
}

int vhost_net_notify_migration_done(struct vhost_net *net, char* mac_addr)
{
    return -1;
}

VHostNetState *get_vhost_net(NetClientState *nc)
{
    return 0;
}

int vhost_set_vring_enable(NetClientState *nc, int enable)
{
    return 0;
}

int vhost_net_set_mtu(struct vhost_net *net, uint16_t mtu)
{
    return 0;
}

void vhost_net_virtqueue_reset(VirtIODevice *vdev, NetClientState *nc,
                               int vq_index)
{

}
