#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"

typedef struct egl_dpy {
    DisplayChangeListener dcl;
    DisplaySurface *ds;
    egl_fb guest_fb;
    egl_fb blit_fb;
    bool y_0_top;
} egl_dpy;

/* ------------------------------------------------------------------ */

static void egl_refresh(DisplayChangeListener *dcl)
{
    graphic_hw_update(dcl->con);
}

static void egl_gfx_update(DisplayChangeListener *dcl,
                           int x, int y, int w, int h)
{
}

static void egl_gfx_switch(DisplayChangeListener *dcl,
                           struct DisplaySurface *new_surface)
{
    egl_dpy *edpy = container_of(dcl, egl_dpy, dcl);

    edpy->ds = new_surface;
}

static void egl_scanout_disable(DisplayChangeListener *dcl)
{
    egl_dpy *edpy = container_of(dcl, egl_dpy, dcl);

    egl_fb_destroy(&edpy->guest_fb);
    egl_fb_destroy(&edpy->blit_fb);
}

static void egl_scanout_texture(DisplayChangeListener *dcl,
                                uint32_t backing_id,
                                bool backing_y_0_top,
                                uint32_t backing_width,
                                uint32_t backing_height,
                                uint32_t x, uint32_t y,
                                uint32_t w, uint32_t h)
{
    egl_dpy *edpy = container_of(dcl, egl_dpy, dcl);

    edpy->y_0_top = backing_y_0_top;

    /* source framebuffer */
    egl_fb_create_for_tex(&edpy->guest_fb,
                          backing_width, backing_height, backing_id);

    /* dest framebuffer */
    if (edpy->blit_fb.width  != backing_width ||
        edpy->blit_fb.height != backing_height) {
        egl_fb_destroy(&edpy->blit_fb);
        egl_fb_create_new_tex(&edpy->blit_fb, backing_width, backing_height);
    }
}

static void egl_scanout_flush(DisplayChangeListener *dcl,
                              uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h)
{
    egl_dpy *edpy = container_of(dcl, egl_dpy, dcl);

    if (!edpy->guest_fb.texture || !edpy->ds) {
        return;
    }
    assert(surface_width(edpy->ds)  == edpy->guest_fb.width);
    assert(surface_height(edpy->ds) == edpy->guest_fb.height);
    assert(surface_format(edpy->ds) == PIXMAN_x8r8g8b8);

    egl_fb_blit(&edpy->blit_fb, &edpy->guest_fb, edpy->y_0_top);
    egl_fb_read(surface_data(edpy->ds), &edpy->blit_fb);

    dpy_gfx_update(edpy->dcl.con, x, y, w, h);
}

static const DisplayChangeListenerOps egl_ops = {
    .dpy_name                = "egl-headless",
    .dpy_refresh             = egl_refresh,
    .dpy_gfx_update          = egl_gfx_update,
    .dpy_gfx_switch          = egl_gfx_switch,

    .dpy_gl_ctx_create       = qemu_egl_create_context,
    .dpy_gl_ctx_destroy      = qemu_egl_destroy_context,
    .dpy_gl_ctx_make_current = qemu_egl_make_context_current,
    .dpy_gl_ctx_get_current  = qemu_egl_get_current_context,

    .dpy_gl_scanout_disable  = egl_scanout_disable,
    .dpy_gl_scanout_texture  = egl_scanout_texture,
    .dpy_gl_update           = egl_scanout_flush,
};

void egl_headless_init(void)
{
    QemuConsole *con;
    egl_dpy *edpy;
    int idx;

    if (egl_rendernode_init(NULL) < 0) {
        error_report("egl: render node init failed");
        exit(1);
    }

    for (idx = 0;; idx++) {
        con = qemu_console_lookup_by_index(idx);
        if (!con || !qemu_console_is_graphic(con)) {
            break;
        }

        edpy = g_new0(egl_dpy, 1);
        edpy->dcl.con = con;
        edpy->dcl.ops = &egl_ops;
        register_displaychangelistener(&edpy->dcl);
    }
}
