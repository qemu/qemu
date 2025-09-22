#ifndef VHOST_NET_H
#define VHOST_NET_H

#include "net/net.h"
#include "hw/virtio/virtio-features.h"
#include "hw/virtio/vhost-backend.h"

struct vhost_net;
typedef struct vhost_net VHostNetState;

typedef uint64_t (GetAckedFeatures)(NetClientState *nc);
typedef void (SaveAcketFeatures)(NetClientState *nc);

typedef struct VhostNetOptions {
    VhostBackendType backend_type;
    NetClientState *net_backend;
    uint32_t busyloop_timeout;
    unsigned int nvqs;
    const int *feature_bits;
    int max_tx_queue_size;
    bool is_vhost_user;
    GetAckedFeatures *get_acked_features;
    SaveAcketFeatures *save_acked_features;
    void *opaque;
} VhostNetOptions;

uint64_t vhost_net_get_max_queues(VHostNetState *net);
struct vhost_net *vhost_net_init(VhostNetOptions *options);

int vhost_net_start(VirtIODevice *dev, NetClientState *ncs,
                    int data_queue_pairs, int cvq);
void vhost_net_stop(VirtIODevice *dev, NetClientState *ncs,
                    int data_queue_pairs, int cvq);

void vhost_net_cleanup(VHostNetState *net);

void vhost_net_get_features_ex(VHostNetState *net, uint64_t *features);
static inline uint64_t vhost_net_get_features(VHostNetState *net,
                                              uint64_t features)
{
    uint64_t features_array[VIRTIO_FEATURES_NU64S];

    virtio_features_from_u64(features_array, features);
    vhost_net_get_features_ex(net, features_array);
    return features_array[0];
}

void vhost_net_ack_features_ex(VHostNetState *net, const uint64_t *features);
static inline void vhost_net_ack_features(VHostNetState *net,
                                          uint64_t features)
{
    uint64_t features_array[VIRTIO_FEATURES_NU64S];

    virtio_features_from_u64(features_array, features);
    vhost_net_ack_features_ex(net, features_array);
}

int vhost_net_get_config(struct vhost_net *net,  uint8_t *config,
                         uint32_t config_len);

int vhost_net_set_config(struct vhost_net *net, const uint8_t *data,
                         uint32_t offset, uint32_t size, uint32_t flags);
bool vhost_net_virtqueue_pending(VHostNetState *net, int n);
void vhost_net_virtqueue_mask(VHostNetState *net, VirtIODevice *dev,
                              int idx, bool mask);
bool vhost_net_config_pending(VHostNetState *net);
void vhost_net_config_mask(VHostNetState *net, VirtIODevice *dev, bool mask);
int vhost_net_notify_migration_done(VHostNetState *net, char* mac_addr);
VHostNetState *get_vhost_net(NetClientState *nc);

int vhost_net_set_vring_enable(NetClientState *nc, int enable);

void vhost_net_get_acked_features_ex(VHostNetState *net, uint64_t *features);
static inline uint64_t vhost_net_get_acked_features(VHostNetState *net)
{
    uint64_t features[VIRTIO_FEATURES_NU64S];

    vhost_net_get_acked_features_ex(net, features);
    assert(!virtio_features_use_ex(features));
    return features[0];
}

int vhost_net_set_mtu(struct vhost_net *net, uint16_t mtu);

void vhost_net_virtqueue_reset(VirtIODevice *vdev, NetClientState *nc,
                               int vq_index);
int vhost_net_virtqueue_restart(VirtIODevice *vdev, NetClientState *nc,
                                int vq_index);

void vhost_net_save_acked_features(NetClientState *nc);
#endif
