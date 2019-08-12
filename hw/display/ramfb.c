/*
 * early boot framebuffer in guest ram
 * configured using fw_cfg
 *
 * Copyright Red Hat, Inc. 2017
 *
 * Author:
 *     Gerd Hoffmann <kraxel@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "qemu/option.h"
#include "hw/loader.h"
#include "hw/display/ramfb.h"
#include "ui/console.h"
#include "sysemu/reset.h"

struct QEMU_PACKED RAMFBCfg {
    uint64_t addr;
    uint32_t fourcc;
    uint32_t flags;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
};

struct RAMFBState {
    DisplaySurface *ds;
    uint32_t width, height;
    uint32_t starting_width, starting_height;
    struct RAMFBCfg cfg;
    bool locked;
};

static void ramfb_unmap_display_surface(pixman_image_t *image, void *unused)
{
    void *data = pixman_image_get_data(image);
    uint32_t size = pixman_image_get_stride(image) *
        pixman_image_get_height(image);
    cpu_physical_memory_unmap(data, size, 0, 0);
}

static DisplaySurface *ramfb_create_display_surface(int width, int height,
                                                    pixman_format_code_t format,
                                                    int linesize, uint64_t addr)
{
    DisplaySurface *surface;
    hwaddr size;
    void *data;

    if (linesize == 0) {
        linesize = width * PIXMAN_FORMAT_BPP(format) / 8;
    }

    size = (hwaddr)linesize * height;
    data = cpu_physical_memory_map(addr, &size, 0);
    if (size != (hwaddr)linesize * height) {
        cpu_physical_memory_unmap(data, size, 0, 0);
        return NULL;
    }

    surface = qemu_create_displaysurface_from(width, height,
                                              format, linesize, data);
    pixman_image_set_destroy_function(surface->image,
                                      ramfb_unmap_display_surface, NULL);

    return surface;
}

static void ramfb_fw_cfg_write(void *dev, off_t offset, size_t len)
{
    RAMFBState *s = dev;
    uint32_t fourcc, format, width, height;
    hwaddr stride, addr;

    width     = be32_to_cpu(s->cfg.width);
    height    = be32_to_cpu(s->cfg.height);
    stride    = be32_to_cpu(s->cfg.stride);
    fourcc    = be32_to_cpu(s->cfg.fourcc);
    addr      = be64_to_cpu(s->cfg.addr);
    format    = qemu_drm_format_to_pixman(fourcc);

    fprintf(stderr, "%s: %dx%d @ 0x%" PRIx64 "\n", __func__,
            width, height, addr);
    if (s->locked) {
        fprintf(stderr, "%s: resolution locked, change rejected\n", __func__);
        return;
    }
    s->locked = true;
    s->width = width;
    s->height = height;
    s->ds = ramfb_create_display_surface(s->width, s->height,
                                         format, stride, addr);
}

void ramfb_display_update(QemuConsole *con, RAMFBState *s)
{
    if (!s->width || !s->height) {
        return;
    }

    if (s->ds) {
        dpy_gfx_replace_surface(con, s->ds);
        s->ds = NULL;
    }

    /* simple full screen update */
    dpy_gfx_update_full(con);
}

static void ramfb_reset(void *opaque)
{
    RAMFBState *s = (RAMFBState *)opaque;
    s->locked = false;
    memset(&s->cfg, 0, sizeof(s->cfg));
    s->cfg.width = s->starting_width;
    s->cfg.height = s->starting_height;
}

RAMFBState *ramfb_setup(DeviceState* dev, Error **errp)
{
    FWCfgState *fw_cfg = fw_cfg_find();
    RAMFBState *s;

    if (!fw_cfg || !fw_cfg->dma_enabled) {
        error_setg(errp, "ramfb device requires fw_cfg with DMA");
        return NULL;
    }

    s = g_new0(RAMFBState, 1);

    const char *s_fb_width = qemu_opt_get(dev->opts, "xres");
    const char *s_fb_height = qemu_opt_get(dev->opts, "yres");
    if (s_fb_width) {
        s->cfg.width = atoi(s_fb_width);
        s->starting_width = s->cfg.width;
    }
    if (s_fb_height) {
        s->cfg.height = atoi(s_fb_height);
        s->starting_height = s->cfg.height;
    }
    s->locked = false;

    rom_add_vga("vgabios-ramfb.bin");
    fw_cfg_add_file_callback(fw_cfg, "etc/ramfb",
                             NULL, ramfb_fw_cfg_write, s,
                             &s->cfg, sizeof(s->cfg), false);
    qemu_register_reset(ramfb_reset, s);
    return s;
}
