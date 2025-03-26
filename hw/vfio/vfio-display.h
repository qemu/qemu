/*
 * VFIO display
 *
 * Copyright Red Hat, Inc. 2025
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_VFIO_VFIO_DISPLAY_H
#define HW_VFIO_VFIO_DISPLAY_H

#include "ui/console.h"
#include "hw/display/ramfb.h"
#include "hw/vfio/vfio-region.h"

typedef struct VFIODMABuf {
    QemuDmaBuf *buf;
    uint32_t pos_x, pos_y, pos_updates;
    uint32_t hot_x, hot_y, hot_updates;
    int dmabuf_id;
    QTAILQ_ENTRY(VFIODMABuf) next;
} VFIODMABuf;

typedef struct VFIODisplay {
    QemuConsole *con;
    RAMFBState *ramfb;
    struct vfio_region_info *edid_info;
    struct vfio_region_gfx_edid *edid_regs;
    uint8_t *edid_blob;
    QEMUTimer *edid_link_timer;
    struct {
        VFIORegion buffer;
        DisplaySurface *surface;
    } region;
    struct {
        QTAILQ_HEAD(, VFIODMABuf) bufs;
        VFIODMABuf *primary;
        VFIODMABuf *cursor;
    } dmabuf;
} VFIODisplay;

#endif /* HW_VFIO_VFIO_DISPLAY_H */
