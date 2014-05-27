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

struct vhost_dev;

typedef int (*vhost_call)(struct vhost_dev *dev, unsigned long int request,
             void *arg);
typedef int (*vhost_backend_init)(struct vhost_dev *dev, void *opaque);
typedef int (*vhost_backend_cleanup)(struct vhost_dev *dev);

typedef struct VhostOps {
    vhost_call vhost_call;
    vhost_backend_init vhost_backend_init;
    vhost_backend_cleanup vhost_backend_cleanup;
} VhostOps;

#endif /* VHOST_BACKEND_H_ */
