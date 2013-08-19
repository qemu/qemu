/*
 *  QEMU model of the Milkymist texture mapping unit.
 *
 *  Copyright (c) 2010 Michael Walle <michael@walle.cc>
 *  Copyright (c) 2010 Sebastien Bourdeauducq
 *                       <sebastien.bourdeauducq@lekernel.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 *
 *
 * Specification available at:
 *   http://www.milkymist.org/socdoc/tmu2.pdf
 *
 */

#include "hw/hw.h"
#include "hw/sysbus.h"
#include "trace.h"
#include "qemu/error-report.h"

#include <X11/Xlib.h>
#include <GL/gl.h>
#include <GL/glx.h>

enum {
    R_CTL = 0,
    R_HMESHLAST,
    R_VMESHLAST,
    R_BRIGHTNESS,
    R_CHROMAKEY,
    R_VERTICESADDR,
    R_TEXFBUF,
    R_TEXHRES,
    R_TEXVRES,
    R_TEXHMASK,
    R_TEXVMASK,
    R_DSTFBUF,
    R_DSTHRES,
    R_DSTVRES,
    R_DSTHOFFSET,
    R_DSTVOFFSET,
    R_DSTSQUAREW,
    R_DSTSQUAREH,
    R_ALPHA,
    R_MAX
};

enum {
    CTL_START_BUSY  = (1<<0),
    CTL_CHROMAKEY   = (1<<1),
};

enum {
    MAX_BRIGHTNESS = 63,
    MAX_ALPHA      = 63,
};

enum {
    MESH_MAXSIZE = 128,
};

struct vertex {
    int x;
    int y;
} QEMU_PACKED;

#define TYPE_MILKYMIST_TMU2 "milkymist-tmu2"
#define MILKYMIST_TMU2(obj) \
    OBJECT_CHECK(MilkymistTMU2State, (obj), TYPE_MILKYMIST_TMU2)

struct MilkymistTMU2State {
    SysBusDevice parent_obj;

    MemoryRegion regs_region;
    CharDriverState *chr;
    qemu_irq irq;

    uint32_t regs[R_MAX];

    Display *dpy;
    GLXFBConfig glx_fb_config;
    GLXContext glx_context;
};
typedef struct MilkymistTMU2State MilkymistTMU2State;

static const int glx_fbconfig_attr[] = {
    GLX_GREEN_SIZE, 5,
    GLX_GREEN_SIZE, 6,
    GLX_BLUE_SIZE, 5,
    None
};

static int tmu2_glx_init(MilkymistTMU2State *s)
{
    GLXFBConfig *configs;
    int nelements;

    s->dpy = XOpenDisplay(NULL); /* FIXME: call XCloseDisplay() */
    if (s->dpy == NULL) {
        return 1;
    }

    configs = glXChooseFBConfig(s->dpy, 0, glx_fbconfig_attr, &nelements);
    if (configs == NULL) {
        return 1;
    }

    s->glx_fb_config = *configs;
    XFree(configs);

    /* FIXME: call glXDestroyContext() */
    s->glx_context = glXCreateNewContext(s->dpy, s->glx_fb_config,
            GLX_RGBA_TYPE, NULL, 1);
    if (s->glx_context == NULL) {
        return 1;
    }

    return 0;
}

static void tmu2_gl_map(struct vertex *mesh, int texhres, int texvres,
        int hmeshlast, int vmeshlast, int ho, int vo, int sw, int sh)
{
    int x, y;
    int x0, y0, x1, y1;
    int u0, v0, u1, v1, u2, v2, u3, v3;
    double xscale = 1.0 / ((double)(64 * texhres));
    double yscale = 1.0 / ((double)(64 * texvres));

    glLoadIdentity();
    glTranslatef(ho, vo, 0);
    glEnable(GL_TEXTURE_2D);
    glBegin(GL_QUADS);

    for (y = 0; y < vmeshlast; y++) {
        y0 = y * sh;
        y1 = y0 + sh;
        for (x = 0; x < hmeshlast; x++) {
            x0 = x * sw;
            x1 = x0 + sw;

            u0 = be32_to_cpu(mesh[MESH_MAXSIZE * y + x].x);
            v0 = be32_to_cpu(mesh[MESH_MAXSIZE * y + x].y);
            u1 = be32_to_cpu(mesh[MESH_MAXSIZE * y + x + 1].x);
            v1 = be32_to_cpu(mesh[MESH_MAXSIZE * y + x + 1].y);
            u2 = be32_to_cpu(mesh[MESH_MAXSIZE * (y + 1) + x + 1].x);
            v2 = be32_to_cpu(mesh[MESH_MAXSIZE * (y + 1) + x + 1].y);
            u3 = be32_to_cpu(mesh[MESH_MAXSIZE * (y + 1) + x].x);
            v3 = be32_to_cpu(mesh[MESH_MAXSIZE * (y + 1) + x].y);

            glTexCoord2d(((double)u0) * xscale, ((double)v0) * yscale);
            glVertex3i(x0, y0, 0);
            glTexCoord2d(((double)u1) * xscale, ((double)v1) * yscale);
            glVertex3i(x1, y0, 0);
            glTexCoord2d(((double)u2) * xscale, ((double)v2) * yscale);
            glVertex3i(x1, y1, 0);
            glTexCoord2d(((double)u3) * xscale, ((double)v3) * yscale);
            glVertex3i(x0, y1, 0);
        }
    }

    glEnd();
}

static void tmu2_start(MilkymistTMU2State *s)
{
    int pbuffer_attrib[6] = {
        GLX_PBUFFER_WIDTH,
        0,
        GLX_PBUFFER_HEIGHT,
        0,
        GLX_PRESERVED_CONTENTS,
        True
    };

    GLXPbuffer pbuffer;
    GLuint texture;
    void *fb;
    hwaddr fb_len;
    void *mesh;
    hwaddr mesh_len;
    float m;

    trace_milkymist_tmu2_start();

    /* Create and set up a suitable OpenGL context */
    pbuffer_attrib[1] = s->regs[R_DSTHRES];
    pbuffer_attrib[3] = s->regs[R_DSTVRES];
    pbuffer = glXCreatePbuffer(s->dpy, s->glx_fb_config, pbuffer_attrib);
    glXMakeContextCurrent(s->dpy, pbuffer, pbuffer, s->glx_context);

    /* Fixup endianness. TODO: would it work on BE hosts? */
    glPixelStorei(GL_UNPACK_SWAP_BYTES, 1);
    glPixelStorei(GL_PACK_SWAP_BYTES, 1);

    /* Row alignment */
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_PACK_ALIGNMENT, 2);

    /* Read the QEMU source framebuffer into an OpenGL texture */
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    fb_len = 2*s->regs[R_TEXHRES]*s->regs[R_TEXVRES];
    fb = cpu_physical_memory_map(s->regs[R_TEXFBUF], &fb_len, 0);
    if (fb == NULL) {
        glDeleteTextures(1, &texture);
        glXMakeContextCurrent(s->dpy, None, None, NULL);
        glXDestroyPbuffer(s->dpy, pbuffer);
        return;
    }
    glTexImage2D(GL_TEXTURE_2D, 0, 3, s->regs[R_TEXHRES], s->regs[R_TEXVRES],
            0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, fb);
    cpu_physical_memory_unmap(fb, fb_len, 0, fb_len);

    /* Set up texturing options */
    /* WARNING:
     * Many cases of TMU2 masking are not supported by OpenGL.
     * We only implement the most common ones:
     *  - full bilinear filtering vs. nearest texel
     *  - texture clamping vs. texture wrapping
     */
    if ((s->regs[R_TEXHMASK] & 0x3f) > 0x20) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    }
    if ((s->regs[R_TEXHMASK] >> 6) & s->regs[R_TEXHRES]) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    }
    if ((s->regs[R_TEXVMASK] >> 6) & s->regs[R_TEXVRES]) {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    } else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    }

    /* Translucency and decay */
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m = (float)(s->regs[R_BRIGHTNESS] + 1) / 64.0f;
    glColor4f(m, m, m, (float)(s->regs[R_ALPHA] + 1) / 64.0f);

    /* Read the QEMU dest. framebuffer into the OpenGL framebuffer */
    fb_len = 2 * s->regs[R_DSTHRES] * s->regs[R_DSTVRES];
    fb = cpu_physical_memory_map(s->regs[R_DSTFBUF], &fb_len, 0);
    if (fb == NULL) {
        glDeleteTextures(1, &texture);
        glXMakeContextCurrent(s->dpy, None, None, NULL);
        glXDestroyPbuffer(s->dpy, pbuffer);
        return;
    }

    glDrawPixels(s->regs[R_DSTHRES], s->regs[R_DSTVRES], GL_RGB,
            GL_UNSIGNED_SHORT_5_6_5, fb);
    cpu_physical_memory_unmap(fb, fb_len, 0, fb_len);
    glViewport(0, 0, s->regs[R_DSTHRES], s->regs[R_DSTVRES]);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0.0, s->regs[R_DSTHRES], 0.0, s->regs[R_DSTVRES], -1.0, 1.0);
    glMatrixMode(GL_MODELVIEW);

    /* Map the texture */
    mesh_len = MESH_MAXSIZE*MESH_MAXSIZE*sizeof(struct vertex);
    mesh = cpu_physical_memory_map(s->regs[R_VERTICESADDR], &mesh_len, 0);
    if (mesh == NULL) {
        glDeleteTextures(1, &texture);
        glXMakeContextCurrent(s->dpy, None, None, NULL);
        glXDestroyPbuffer(s->dpy, pbuffer);
        return;
    }

    tmu2_gl_map((struct vertex *)mesh,
        s->regs[R_TEXHRES], s->regs[R_TEXVRES],
        s->regs[R_HMESHLAST], s->regs[R_VMESHLAST],
        s->regs[R_DSTHOFFSET], s->regs[R_DSTVOFFSET],
        s->regs[R_DSTSQUAREW], s->regs[R_DSTSQUAREH]);
    cpu_physical_memory_unmap(mesh, mesh_len, 0, mesh_len);

    /* Write back the OpenGL framebuffer to the QEMU framebuffer */
    fb_len = 2 * s->regs[R_DSTHRES] * s->regs[R_DSTVRES];
    fb = cpu_physical_memory_map(s->regs[R_DSTFBUF], &fb_len, 1);
    if (fb == NULL) {
        glDeleteTextures(1, &texture);
        glXMakeContextCurrent(s->dpy, None, None, NULL);
        glXDestroyPbuffer(s->dpy, pbuffer);
        return;
    }

    glReadPixels(0, 0, s->regs[R_DSTHRES], s->regs[R_DSTVRES], GL_RGB,
            GL_UNSIGNED_SHORT_5_6_5, fb);
    cpu_physical_memory_unmap(fb, fb_len, 1, fb_len);

    /* Free OpenGL allocs */
    glDeleteTextures(1, &texture);
    glXMakeContextCurrent(s->dpy, None, None, NULL);
    glXDestroyPbuffer(s->dpy, pbuffer);

    s->regs[R_CTL] &= ~CTL_START_BUSY;

    trace_milkymist_tmu2_pulse_irq();
    qemu_irq_pulse(s->irq);
}

static uint64_t tmu2_read(void *opaque, hwaddr addr,
                          unsigned size)
{
    MilkymistTMU2State *s = opaque;
    uint32_t r = 0;

    addr >>= 2;
    switch (addr) {
    case R_CTL:
    case R_HMESHLAST:
    case R_VMESHLAST:
    case R_BRIGHTNESS:
    case R_CHROMAKEY:
    case R_VERTICESADDR:
    case R_TEXFBUF:
    case R_TEXHRES:
    case R_TEXVRES:
    case R_TEXHMASK:
    case R_TEXVMASK:
    case R_DSTFBUF:
    case R_DSTHRES:
    case R_DSTVRES:
    case R_DSTHOFFSET:
    case R_DSTVOFFSET:
    case R_DSTSQUAREW:
    case R_DSTSQUAREH:
    case R_ALPHA:
        r = s->regs[addr];
        break;

    default:
        error_report("milkymist_tmu2: read access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    trace_milkymist_tmu2_memory_read(addr << 2, r);

    return r;
}

static void tmu2_check_registers(MilkymistTMU2State *s)
{
    if (s->regs[R_BRIGHTNESS] > MAX_BRIGHTNESS) {
        error_report("milkymist_tmu2: max brightness is %d", MAX_BRIGHTNESS);
    }

    if (s->regs[R_ALPHA] > MAX_ALPHA) {
        error_report("milkymist_tmu2: max alpha is %d", MAX_ALPHA);
    }

    if (s->regs[R_VERTICESADDR] & 0x07) {
        error_report("milkymist_tmu2: vertex mesh address has to be 64-bit "
                "aligned");
    }

    if (s->regs[R_TEXFBUF] & 0x01) {
        error_report("milkymist_tmu2: texture buffer address has to be "
                "16-bit aligned");
    }
}

static void tmu2_write(void *opaque, hwaddr addr, uint64_t value,
                       unsigned size)
{
    MilkymistTMU2State *s = opaque;

    trace_milkymist_tmu2_memory_write(addr, value);

    addr >>= 2;
    switch (addr) {
    case R_CTL:
        s->regs[addr] = value;
        if (value & CTL_START_BUSY) {
            tmu2_start(s);
        }
        break;
    case R_BRIGHTNESS:
    case R_HMESHLAST:
    case R_VMESHLAST:
    case R_CHROMAKEY:
    case R_VERTICESADDR:
    case R_TEXFBUF:
    case R_TEXHRES:
    case R_TEXVRES:
    case R_TEXHMASK:
    case R_TEXVMASK:
    case R_DSTFBUF:
    case R_DSTHRES:
    case R_DSTVRES:
    case R_DSTHOFFSET:
    case R_DSTVOFFSET:
    case R_DSTSQUAREW:
    case R_DSTSQUAREH:
    case R_ALPHA:
        s->regs[addr] = value;
        break;

    default:
        error_report("milkymist_tmu2: write access to unknown register 0x"
                TARGET_FMT_plx, addr << 2);
        break;
    }

    tmu2_check_registers(s);
}

static const MemoryRegionOps tmu2_mmio_ops = {
    .read = tmu2_read,
    .write = tmu2_write,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void milkymist_tmu2_reset(DeviceState *d)
{
    MilkymistTMU2State *s = MILKYMIST_TMU2(d);
    int i;

    for (i = 0; i < R_MAX; i++) {
        s->regs[i] = 0;
    }
}

static int milkymist_tmu2_init(SysBusDevice *dev)
{
    MilkymistTMU2State *s = MILKYMIST_TMU2(dev);

    if (tmu2_glx_init(s)) {
        return 1;
    }

    sysbus_init_irq(dev, &s->irq);

    memory_region_init_io(&s->regs_region, OBJECT(s), &tmu2_mmio_ops, s,
            "milkymist-tmu2", R_MAX * 4);
    sysbus_init_mmio(dev, &s->regs_region);

    return 0;
}

static const VMStateDescription vmstate_milkymist_tmu2 = {
    .name = "milkymist-tmu2",
    .version_id = 1,
    .minimum_version_id = 1,
    .minimum_version_id_old = 1,
    .fields      = (VMStateField[]) {
        VMSTATE_UINT32_ARRAY(regs, MilkymistTMU2State, R_MAX),
        VMSTATE_END_OF_LIST()
    }
};

static void milkymist_tmu2_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SysBusDeviceClass *k = SYS_BUS_DEVICE_CLASS(klass);

    k->init = milkymist_tmu2_init;
    dc->reset = milkymist_tmu2_reset;
    dc->vmsd = &vmstate_milkymist_tmu2;
}

static const TypeInfo milkymist_tmu2_info = {
    .name          = TYPE_MILKYMIST_TMU2,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MilkymistTMU2State),
    .class_init    = milkymist_tmu2_class_init,
};

static void milkymist_tmu2_register_types(void)
{
    type_register_static(&milkymist_tmu2_info);
}

type_init(milkymist_tmu2_register_types)
