#ifndef VHOST_NET_H
#define VHOST_NET_H

#include "net.h"

struct vhost_net;
typedef struct vhost_net VHostNetState;

VHostNetState *vhost_net_init(VLANClientState *backend, int devfd, bool force);

bool vhost_net_query(VHostNetState *net, VirtIODevice *dev);
int vhost_net_start(VHostNetState *net, VirtIODevice *dev);
void vhost_net_stop(VHostNetState *net, VirtIODevice *dev);

void vhost_net_cleanup(VHostNetState *net);

unsigned vhost_net_get_features(VHostNetState *net, unsigned features);
void vhost_net_ack_features(VHostNetState *net, unsigned features);

#endif
