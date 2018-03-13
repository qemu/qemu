/*
 * display support for mdev based vgpu devices
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Authors:
 *    Gerd Hoffmann
 *
 * This work is licensed under the terms of the GNU GPL, version 2.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include <linux/vfio.h>
#include <sys/ioctl.h>

#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "qapi/error.h"
#include "pci.h"

int vfio_display_probe(VFIOPCIDevice *vdev, Error **errp)
{
    struct vfio_device_gfx_plane_info probe;
    int ret;

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_GFX_PLANE_TYPE_PROBE | VFIO_GFX_PLANE_TYPE_DMABUF;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &probe);
    if (ret == 0) {
        error_setg(errp, "vfio-display: dmabuf support not implemented yet");
        return -1;
    }

    memset(&probe, 0, sizeof(probe));
    probe.argsz = sizeof(probe);
    probe.flags = VFIO_GFX_PLANE_TYPE_PROBE | VFIO_GFX_PLANE_TYPE_REGION;
    ret = ioctl(vdev->vbasedev.fd, VFIO_DEVICE_QUERY_GFX_PLANE, &probe);
    if (ret == 0) {
        error_setg(errp, "vfio-display: region support not implemented yet");
        return -1;
    }

    if (vdev->display == ON_OFF_AUTO_AUTO) {
        /* not an error in automatic mode */
        return 0;
    }

    error_setg(errp, "vfio: device doesn't support any (known) display method");
    return -1;
}

void vfio_display_finalize(VFIOPCIDevice *vdev)
{
}
