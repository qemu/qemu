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
#include "hw/loader.h"
#include "hw/display/ramfb.h"
#include "ui/console.h"
#include "sysemu/sysemu.h"

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
    struct RAMFBCfg cfg;
};

static void ramfb_fw_cfg_write(void *dev, off_t offset, size_t len)
{
    RAMFBState *s = dev;
    void *framebuffer;
    uint32_t fourcc, format;
    hwaddr stride, addr, length;

    s->width  = be32_to_cpu(s->cfg.width);
    s->height = be32_to_cpu(s->cfg.height);
    stride    = be32_to_cpu(s->cfg.stride);
    fourcc    = be32_to_cpu(s->cfg.fourcc);
    addr      = be64_to_cpu(s->cfg.addr);
    length    = stride * s->height;
    format    = qemu_drm_format_to_pixman(fourcc);

    fprintf(stderr, "%s: %dx%d @ 0x%" PRIx64 "\n", __func__,
            s->width, s->height, addr);
    framebuffer = address_space_map(&address_space_memory,
                                    addr, &length, false,
                                    MEMTXATTRS_UNSPECIFIED);
    if (!framebuffer || length < stride * s->height) {
        s->width = 0;
        s->height = 0;
        return;
    }
    s->ds = qemu_create_displaysurface_from(s->width, s->height,
                                            format, stride, framebuffer);
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

RAMFBState *ramfb_setup(Error **errp)
{
    FWCfgState *fw_cfg = fw_cfg_find();
    RAMFBState *s;

    if (!fw_cfg || !fw_cfg->dma_enabled) {
        error_setg(errp, "ramfb device requires fw_cfg with DMA");
        return NULL;
    }

    s = g_new0(RAMFBState, 1);

    rom_add_vga("vgabios-ramfb.bin");
    fw_cfg_add_file_callback(fw_cfg, "etc/ramfb",
                             NULL, ramfb_fw_cfg_write, s,
                             &s->cfg, sizeof(s->cfg), false);
    return s;
}
