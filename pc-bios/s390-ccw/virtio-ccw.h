/*
 * Virtio definitions for CCW devices
 *
 * Copyright 2025 IBM Corp.
 * Author(s): Jared Rossi <jrossi@linux.ibm.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef VIRTIO_CCW_H
#define VIRTIO_CCW_H

/* main.c */
extern SubChannelId blk_schid;

/* virtio-ccw.c */
int drain_irqs_ccw(SubChannelId schid);
bool virtio_ccw_is_supported(VDev *vdev);
int virtio_ccw_run(VDev *vdev, int vqid, VirtioCmd *cmd);
long virtio_ccw_notify(SubChannelId schid, int vq_idx, long cookie);
int virtio_ccw_setup(VDev *vdev);
int virtio_ccw_reset(VDev *vdev);

#endif
