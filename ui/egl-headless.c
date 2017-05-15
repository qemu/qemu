#include "qemu/osdep.h"
#include "qemu-common.h"
#include "sysemu/sysemu.h"
#include "ui/console.h"
#include "ui/egl-helpers.h"
#include "ui/egl-context.h"

typedef struct egl_dpy {
    DisplayChangeListener dcl;
    DisplaySurface *ds;
    int width, height;
    GLuint texture;
    GLuint framebuffer;
    GLuint blit_texture;
    GLuint blit_framebuffer;
    bool y_0_top;
} egl_dpy;

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

    edpy->texture = 0;
    /* XXX: delete framebuffers here ??? */
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

    edpy->texture = backing_id;
    edpy->y_0_top = backing_y_0_top;

    /* source framebuffer */
    if (!edpy->framebuffer) {
        glGenFramebuffers(1, &edpy->framebuffer);
    }
    glBindFramebuffer(GL_FRAMEBUFFER_EXT, edpy->framebuffer);
    glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                              GL_TEXTURE_2D, edpy->texture, 0);

    /* dest framebuffer */
    if (!edpy->blit_framebuffer) {
        glGenFramebuffers(1, &edpy->blit_framebuffer);
        glGenTextures(1, &edpy->blit_texture);
        edpy->width = 0;
        edpy->height = 0;
    }
    if (edpy->width != backing_width || edpy->height != backing_height) {
        edpy->width   = backing_width;
        edpy->height  = backing_height;
        glBindTexture(GL_TEXTURE_2D, edpy->blit_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB,
                     edpy->width, edpy->height,
                     0, GL_BGRA, GL_UNSIGNED_BYTE, 0);
        glBindFramebuffer(GL_FRAMEBUFFER_EXT, edpy->blit_framebuffer);
        glFramebufferTexture2DEXT(GL_FRAMEBUFFER_EXT, GL_COLOR_ATTACHMENT0_EXT,
                                  GL_TEXTURE_2D, edpy->blit_texture, 0);
    }
}

static void egl_scanout_flush(DisplayChangeListener *dcl,
                              uint32_t x, uint32_t y,
                              uint32_t w, uint32_t h)
{
    egl_dpy *edpy = container_of(dcl, egl_dpy, dcl);
    GLuint y1, y2;

    if (!edpy->texture || !edpy->ds) {
        return;
    }
    assert(surface_width(edpy->ds)  == edpy->width);
    assert(surface_height(edpy->ds) == edpy->height);
    assert(surface_format(edpy->ds) == PIXMAN_x8r8g8b8);

    /* blit framebuffer, flip if needed */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, edpy->framebuffer);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, edpy->blit_framebuffer);
    glViewport(0, 0, edpy->width, edpy->height);
    y1 = edpy->y_0_top ? edpy->height : 0;
    y2 = edpy->y_0_top ? 0 : edpy->height;
    glBlitFramebuffer(0, y1, edpy->width, y2,
                      0, 0, edpy->width, edpy->height,
                      GL_COLOR_BUFFER_BIT, GL_NEAREST);

    /* read pixels to surface */
    glBindFramebuffer(GL_READ_FRAMEBUFFER, edpy->blit_framebuffer);
    glReadBuffer(GL_COLOR_ATTACHMENT0_EXT);
    glReadPixels(0, 0, edpy->width, edpy->height,
                 GL_BGRA, GL_UNSIGNED_BYTE, surface_data(edpy->ds));

    /* notify about updates */
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
