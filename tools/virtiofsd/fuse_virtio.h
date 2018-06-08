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

int virtio_loop(struct fuse_session *se);

#endif
