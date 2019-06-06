/*
 * virtio-fs glue for FUSE
 * Copyright (C) 2018 Red Hat, Inc. and/or its affiliates
 *
 * Authors:
 *   Dave Gilbert  <dgilbert@redhat.com>
 *
 * Implements the glue between libfuse and libvhost-user
 *
 * This program can be distributed under the terms of the GNU LGPLv2.
 *  See the file COPYING.LIB
 */

#ifndef FUSE_VIRTIO_H
#define FUSE_VIRTIO_H

#include "fuse_i.h"

struct fuse_session;

int virtio_session_mount(struct fuse_session *se);
void virtio_session_close(struct fuse_session *se);
int virtio_loop(struct fuse_session *se);


int virtio_send_msg(struct fuse_session *se, struct fuse_chan *ch,
                    struct iovec *iov, int count);

int virtio_send_data_iov(struct fuse_session *se, struct fuse_chan *ch,
                         struct iovec *iov, int count,
                         struct fuse_bufvec *buf, size_t len);

#endif
