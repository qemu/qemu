/*
 * vhost-backend
 *
 * Copyright (c) 2013 Virtual Open Systems Sarl.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 *
 */

#ifndef VHOST_BACKEND_H_
#define VHOST_BACKEND_H_

typedef enum VhostBackendType {
    VHOST_BACKEND_TYPE_NONE = 0,
    VHOST_BACKEND_TYPE_KERNEL = 1,
    VHOST_BACKEND_TYPE_USER = 2,
    VHOST_BACKEND_TYPE_MAX = 3,
} VhostBackendType;

struct vhost_dev;

typedef int (*vhost_call)(struct vhost_dev *dev, unsigned long int request,
             void *arg);
typedef int (*vhost_backend_init)(struct vhost_dev *dev, void *opaque);
typedef int (*vhost_backend_cleanup)(struct vhost_dev *dev);

typedef struct VhostOps {
    VhostBackendType backend_type;
    vhost_call vhost_call;
    vhost_backend_init vhost_backend_init;
    vhost_backend_cleanup vhost_backend_cleanup;
} VhostOps;

extern const VhostOps user_ops;

int vhost_set_backend_type(struct vhost_dev *dev,
                           VhostBackendType backend_type);

#endif /* VHOST_BACKEND_H_ */
