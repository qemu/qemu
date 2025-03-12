/*
 * Data structures and functions shared between variants of the macOS
 * ParavirtualizedGraphics.framework based apple-gfx display adapter.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_APPLE_GFX_H
#define QEMU_APPLE_GFX_H

#include "qemu/queue.h"
#include "system/memory.h"
#include "hw/qdev-properties.h"
#include "ui/surface.h"

#define TYPE_APPLE_GFX_MMIO         "apple-gfx-mmio"
#define TYPE_APPLE_GFX_PCI          "apple-gfx-pci"

@class PGDeviceDescriptor;
@protocol PGDevice;
@protocol PGDisplay;
@protocol MTLDevice;
@protocol MTLTexture;
@protocol MTLCommandQueue;

typedef QTAILQ_HEAD(, PGTask_s) PGTaskList;

typedef struct AppleGFXDisplayMode {
    uint16_t width_px;
    uint16_t height_px;
    uint16_t refresh_rate_hz;
} AppleGFXDisplayMode;

typedef struct AppleGFXState {
    /* Initialised on init/realize() */
    MemoryRegion iomem_gfx;
    id<PGDevice> pgdev;
    id<PGDisplay> pgdisp;
    QemuConsole *con;
    id<MTLDevice> mtl;
    id<MTLCommandQueue> mtl_queue;
    AppleGFXDisplayMode *display_modes;
    uint32_t num_display_modes;

    /* List `tasks` is protected by task_mutex */
    QemuMutex task_mutex;
    PGTaskList tasks;

    /* Mutable state (BQL protected) */
    QEMUCursor *cursor;
    DisplaySurface *surface;
    id<MTLTexture> texture;
    int8_t pending_frames; /* # guest frames in the rendering pipeline */
    bool gfx_update_requested; /* QEMU display system wants a new frame */
    bool new_frame_ready; /* Guest has rendered a frame, ready to be used */
    bool using_managed_texture_storage;
    uint32_t rendering_frame_width;
    uint32_t rendering_frame_height;

    /* Mutable state (atomic) */
    bool cursor_show;
} AppleGFXState;

void apple_gfx_common_init(Object *obj, AppleGFXState *s, const char* obj_name);
bool apple_gfx_common_realize(AppleGFXState *s, DeviceState *dev,
                              PGDeviceDescriptor *desc, Error **errp);
void *apple_gfx_host_ptr_for_gpa_range(uint64_t guest_physical,
                                       uint64_t length, bool read_only,
                                       MemoryRegion **mapping_in_region);

extern const PropertyInfo qdev_prop_apple_gfx_display_mode;

#endif

