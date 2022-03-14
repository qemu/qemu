/*
 * vhost shadow virtqueue
 *
 * SPDX-FileCopyrightText: Red Hat, Inc. 2021
 * SPDX-FileContributor: Author: Eugenio Pérez <eperezma@redhat.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/virtio/vhost-shadow-virtqueue.h"

#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "linux-headers/linux/vhost.h"

/**
 * Forward guest notifications.
 *
 * @n: guest kick event notifier, the one that guest set to notify svq.
 */
static void vhost_handle_guest_kick(EventNotifier *n)
{
    VhostShadowVirtqueue *svq = container_of(n, VhostShadowVirtqueue, svq_kick);
    event_notifier_test_and_clear(n);
    event_notifier_set(&svq->hdev_kick);
}

/**
 * Set a new file descriptor for the guest to kick the SVQ and notify for avail
 *
 * @svq: The svq
 * @svq_kick_fd: The svq kick fd
 *
 * Note that the SVQ will never close the old file descriptor.
 */
void vhost_svq_set_svq_kick_fd(VhostShadowVirtqueue *svq, int svq_kick_fd)
{
    EventNotifier *svq_kick = &svq->svq_kick;
    bool poll_stop = VHOST_FILE_UNBIND != event_notifier_get_fd(svq_kick);
    bool poll_start = svq_kick_fd != VHOST_FILE_UNBIND;

    if (poll_stop) {
        event_notifier_set_handler(svq_kick, NULL);
    }

    /*
     * event_notifier_set_handler already checks for guest's notifications if
     * they arrive at the new file descriptor in the switch, so there is no
     * need to explicitly check for them.
     */
    if (poll_start) {
        event_notifier_init_fd(svq_kick, svq_kick_fd);
        event_notifier_set(svq_kick);
        event_notifier_set_handler(svq_kick, vhost_handle_guest_kick);
    }
}

/**
 * Stop the shadow virtqueue operation.
 * @svq: Shadow Virtqueue
 */
void vhost_svq_stop(VhostShadowVirtqueue *svq)
{
    event_notifier_set_handler(&svq->svq_kick, NULL);
}

/**
 * Creates vhost shadow virtqueue, and instructs the vhost device to use the
 * shadow methods and file descriptors.
 *
 * Returns the new virtqueue or NULL.
 *
 * In case of error, reason is reported through error_report.
 */
VhostShadowVirtqueue *vhost_svq_new(void)
{
    g_autofree VhostShadowVirtqueue *svq = g_new0(VhostShadowVirtqueue, 1);
    int r;

    r = event_notifier_init(&svq->hdev_kick, 0);
    if (r != 0) {
        error_report("Couldn't create kick event notifier: %s (%d)",
                     g_strerror(errno), errno);
        goto err_init_hdev_kick;
    }

    r = event_notifier_init(&svq->hdev_call, 0);
    if (r != 0) {
        error_report("Couldn't create call event notifier: %s (%d)",
                     g_strerror(errno), errno);
        goto err_init_hdev_call;
    }

    event_notifier_init_fd(&svq->svq_kick, VHOST_FILE_UNBIND);
    return g_steal_pointer(&svq);

err_init_hdev_call:
    event_notifier_cleanup(&svq->hdev_kick);

err_init_hdev_kick:
    return NULL;
}

/**
 * Free the resources of the shadow virtqueue.
 *
 * @pvq: gpointer to SVQ so it can be used by autofree functions.
 */
void vhost_svq_free(gpointer pvq)
{
    VhostShadowVirtqueue *vq = pvq;
    vhost_svq_stop(vq);
    event_notifier_cleanup(&vq->hdev_kick);
    event_notifier_cleanup(&vq->hdev_call);
    g_free(vq);
}
