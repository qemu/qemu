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
 * See the file COPYING.LIB
 */

#include "fuse_virtio.h"
#include "fuse_i.h"
#include "standard-headers/linux/fuse.h"
#include "fuse_misc.h"
#include "fuse_opt.h"

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "contrib/libvhost-user/libvhost-user.h"

/*
 * We pass the dev element into libvhost-user
 * and then use it to get back to the outer
 * container for other data.
 */
struct fv_VuDev {
    VuDev dev;
    struct fuse_session *se;
};

/* From spec */
struct virtio_fs_config {
    char tag[36];
    uint32_t num_queues;
};

/* Callback from libvhost-user */
static uint64_t fv_get_features(VuDev *dev)
{
    return 1ULL << VIRTIO_F_VERSION_1;
}

/* Callback from libvhost-user */
static void fv_set_features(VuDev *dev, uint64_t features)
{
}

/*
 * Callback from libvhost-user if there's a new fd we're supposed to listen
 * to, typically a queue kick?
 */
static void fv_set_watch(VuDev *dev, int fd, int condition, vu_watch_cb cb,
                         void *data)
{
    fuse_log(FUSE_LOG_WARNING, "%s: TODO! fd=%d\n", __func__, fd);
}

/*
 * Callback from libvhost-user if we're no longer supposed to listen on an fd
 */
static void fv_remove_watch(VuDev *dev, int fd)
{
    fuse_log(FUSE_LOG_WARNING, "%s: TODO! fd=%d\n", __func__, fd);
}

/* Callback from libvhost-user to panic */
static void fv_panic(VuDev *dev, const char *err)
{
    fuse_log(FUSE_LOG_ERR, "%s: libvhost-user: %s\n", __func__, err);
    /* TODO: Allow reconnects?? */
    exit(EXIT_FAILURE);
}

static bool fv_queue_order(VuDev *dev, int qidx)
{
    return false;
}

static const VuDevIface fv_iface = {
    .get_features = fv_get_features,
    .set_features = fv_set_features,

    .queue_is_processed_in_order = fv_queue_order,
};

/*
 * Main loop; this mostly deals with events on the vhost-user
 * socket itself, and not actual fuse data.
 */
int virtio_loop(struct fuse_session *se)
{
    fuse_log(FUSE_LOG_INFO, "%s: Entry\n", __func__);

    while (!fuse_session_exited(se)) {
        struct pollfd pf[1];
        pf[0].fd = se->vu_socketfd;
        pf[0].events = POLLIN;
        pf[0].revents = 0;

        fuse_log(FUSE_LOG_DEBUG, "%s: Waiting for VU event\n", __func__);
        int poll_res = ppoll(pf, 1, NULL, NULL);

        if (poll_res == -1) {
            if (errno == EINTR) {
                fuse_log(FUSE_LOG_INFO, "%s: ppoll interrupted, going around\n",
                         __func__);
                continue;
            }
            fuse_log(FUSE_LOG_ERR, "virtio_loop ppoll: %m\n");
            break;
        }
        assert(poll_res == 1);
        if (pf[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            fuse_log(FUSE_LOG_ERR, "%s: Unexpected poll revents %x\n", __func__,
                     pf[0].revents);
            break;
        }
        assert(pf[0].revents & POLLIN);
        fuse_log(FUSE_LOG_DEBUG, "%s: Got VU event\n", __func__);
        if (!vu_dispatch(&se->virtio_dev->dev)) {
            fuse_log(FUSE_LOG_ERR, "%s: vu_dispatch failed\n", __func__);
            break;
        }
    }

    fuse_log(FUSE_LOG_INFO, "%s: Exit\n", __func__);

    return 0;
}

int virtio_session_mount(struct fuse_session *se)
{
    struct sockaddr_un un;
    mode_t old_umask;

    if (strlen(se->vu_socket_path) >= sizeof(un.sun_path)) {
        fuse_log(FUSE_LOG_ERR, "Socket path too long\n");
        return -1;
    }

    se->fd = -1;

    /*
     * Create the Unix socket to communicate with qemu
     * based on QEMU's vhost-user-bridge
     */
    unlink(se->vu_socket_path);
    strcpy(un.sun_path, se->vu_socket_path);
    size_t addr_len = sizeof(un);

    int listen_sock = socket(AF_UNIX, SOCK_STREAM, 0);
    if (listen_sock == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket creation: %m\n");
        return -1;
    }
    un.sun_family = AF_UNIX;

    /*
     * Unfortunately bind doesn't let you set the mask on the socket,
     * so set umask to 077 and restore it later.
     */
    old_umask = umask(0077);
    if (bind(listen_sock, (struct sockaddr *)&un, addr_len) == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket bind: %m\n");
        umask(old_umask);
        return -1;
    }
    umask(old_umask);

    if (listen(listen_sock, 1) == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket listen: %m\n");
        return -1;
    }

    fuse_log(FUSE_LOG_INFO, "%s: Waiting for vhost-user socket connection...\n",
             __func__);
    int data_sock = accept(listen_sock, NULL, NULL);
    if (data_sock == -1) {
        fuse_log(FUSE_LOG_ERR, "vhost socket accept: %m\n");
        close(listen_sock);
        return -1;
    }
    close(listen_sock);
    fuse_log(FUSE_LOG_INFO, "%s: Received vhost-user socket connection\n",
             __func__);

    /* TODO: Some cleanup/deallocation! */
    se->virtio_dev = calloc(sizeof(struct fv_VuDev), 1);
    if (!se->virtio_dev) {
        fuse_log(FUSE_LOG_ERR, "%s: virtio_dev calloc failed\n", __func__);
        close(data_sock);
        return -1;
    }

    se->vu_socketfd = data_sock;
    se->virtio_dev->se = se;
    vu_init(&se->virtio_dev->dev, 2, se->vu_socketfd, fv_panic, fv_set_watch,
            fv_remove_watch, &fv_iface);

    return 0;
}
