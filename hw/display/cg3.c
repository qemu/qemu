/*
 * QEMU CG3 Frame buffer
 *
 * Copyright (c) 2012 Bob Breuer
 * Copyright (c) 2013 Mark Cave-Ayland
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "qemu-common.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "ui/console.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/qdev-properties.h"
#include "qemu/log.h"
#include "qemu/module.h"

/* Change to 1 to enable debugging */
#define DEBUG_CG3 0

#define CG3_ROM_FILE  "QEMU,cgthree.bin"
#define FCODE_MAX_ROM_SIZE 0x10000

#define CG3_REG_SIZE            0x20

#define CG3_REG_BT458_ADDR      0x0
#define CG3_REG_BT458_COLMAP    0x4
#define CG3_REG_FBC_CTRL        0x10
#define CG3_REG_FBC_STATUS      0x11
#define CG3_REG_FBC_CURSTART    0x12
#define CG3_REG_FBC_CUREND      0x13
#define CG3_REG_FBC_VCTRL       0x14

/* Control register flags */
#define CG3_CR_ENABLE_INTS      0x80

/* Status register flags */
#define CG3_SR_PENDING_INT      0x80
#define CG3_SR_1152_900_76_B    0x60
#define CG3_SR_ID_COLOR         0x01

#define CG3_VRAM_SIZE 0x100000
#define CG3_VRAM_OFFSET 0x800000

#define DPRINTF(fmt, ...) do { \
    if (DEBUG_CG3) { \
        printf("CG3: " fmt , ## __VA_ARGS__); \
    } \
} while (0)

#define TYPE_CG3 "cgthree"
#define CG3(obj) OBJECT_CHECK(CG3State, (obj), TYPE_CG3)

typedef struct CG3State {
    SysBusDevice parent_obj;

    QemuConsole *con;
    qemu_irq irq;
    hwaddr prom_addr;
    MemoryRegion vram_mem;
    MemoryRegion rom;
    MemoryRegion reg;
    uint32_t vram_size;
    int full_update;
    uint8_t regs[16];
    uint8_t r[256], g[256], b[256];
    uint16_t width, height, depth;
    uint8_t dac_index, dac_state;
} CG3State;

static void cg3_update_display(void *opaque)
{
    CG3State *s = opaque;
    DisplaySurface *surface = qemu_console_surface(s->con);
    const uint8_t *pix;
    uint32_t *data;
    uint32_t dval;
    int x, y, y_start;
    unsigned int width, height;
    ram_addr_t page;
    DirtyBitmapSnapshot *snap = NULL;

    if (surface_bits_per_pixel(surface) != 32) {
        return;
    }
    width = s->width;
    height = s->height;

    y_start = -1;
    pix = memory_region_get_ram_ptr(&s->vram_mem);
    data = (uint32_t *)surface_data(surface);

    if (!s->full_update) {
        snap = memory_region_snapshot_and_clear_dirty(&s->vram_mem, 0x0,
                                              memory_region_size(&s->vram_mem),
                                              DIRTY_MEMORY_VGA);
    }

    for (y = 0; y < height; y++) {
        int update;

        page = (ram_addr_t)y * width;

        if (s->full_update) {
            update = 1;
        } else {
            update = memory_region_snapshot_get_dirty(&s->vram_mem, snap, page,
                                                      width);
        }

        if (update) {
            if (y_start < 0) {
                y_start = y;
            }

            for (x = 0; x < width; x++) {
                dval = *pix++;
                dval = (s->r[dval] << 16) | (s->g[dval] << 8) | s->b[dval];
                *data++ = dval;
            }
        } else {
            if (y_start >= 0) {
                dpy_gfx_update(s->con, 0, y_start, width, y - y_start);
                y_start = -1;
            }
            pix += width;
            data += width;
        }
    }
    s->full_update = 0;
    if (y_start >= 0) {
        dpy_gfx_update(s->con, 0, y_start, width, y - y_start);
    }
    /* vsync interrupt? */
    if (s->regs[0] & CG3_CR_ENABLE_INTS) {
        s->regs[1] |= CG3_SR_PENDING_INT;
        qemu_irq_raise(s->irq);
    }
    g_free(snap);
}

static void cg3_invalidate_display(void *opaque)
{
    CG3State *s = opaque;

    memory_region_set_dirty(&s->vram_mem, 0, CG3_VRAM_SIZE);
}

static uint64_t cg3_reg_read(void *opaque, hwaddr addr, unsigned size)
{
    CG3State *s = opaque;
    int val;

    switch (addr) {
    case CG3_REG_BT458_ADDR:
    case CG3_REG_BT458_COLMAP:
        val = 0;
        break;
    case CG3_REG_FBC_CTRL:
        val = s->regs[0];
        break;
    case CG3_REG_FBC_STATUS:
        /* monitor ID 6, board type = 1 (color) */
        val = s->regs[1] | CG3_SR_1152_900_76_B | CG3_SR_ID_COLOR;
        break;
    case CG3_REG_FBC_CURSTART ... CG3_REG_SIZE - 1:
        val = s->regs[addr - 0x10];
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                  "cg3: Unimplemented register read "
                  "reg 0x%" HWADDR_PRIx " size 0x%x\n",
                  addr, size);
        val = 0;
        break;
    }
    DPRINTF("read %02x from reg %" HWADDR_PRIx "\n", val, addr);
    return val;
}

static void cg3_reg_write(void *opaque, hwaddr addr, uint64_t val,
                          unsigned size)
{
    CG3State *s = opaque;
    uint8_t regval;
    int i;

    DPRINTF("write %" PRIx64 " to reg %" HWADDR_PRIx " size %d\n",
            val, addr, size);

    switch (addr) {
    case CG3_REG_BT458_ADDR:
        s->dac_index = val;
        s->dac_state = 0;
        break;
    case CG3_REG_BT458_COLMAP:
        /* This register can be written to as either a long word or a byte */
        if (size == 1) {
            val <<= 24;
        }

        for (i = 0; i < size; i++) {
            regval = val >> 24;

            switch (s->dac_state) {
            case 0:
                s->r[s->dac_index] = regval;
                s->dac_state++;
                break;
            case 1:
                s->g[s->dac_index] = regval;
                s->dac_state++;
                break;
            case 2:
                s->b[s->dac_index] = regval;
                /* Index autoincrement */
                s->dac_index = (s->dac_index + 1) & 0xff;
                /* fall through */
            default:
                s->dac_state = 0;
                break;
            }
            val <<= 8;
        }
        s->full_update = 1;
        break;
    case CG3_REG_FBC_CTRL:
        s->regs[0] = val;
        break;
    case CG3_REG_FBC_STATUS:
        if (s->regs[1] & CG3_SR_PENDING_INT) {
            /* clear interrupt */
            s->regs[1] &= ~CG3_SR_PENDING_INT;
            qemu_irq_lower(s->irq);
        }
        break;
    case CG3_REG_FBC_CURSTART ... CG3_REG_SIZE - 1:
        s->regs[addr - 0x10] = val;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                  "cg3: Unimplemented register write "
                  "reg 0x%" HWADDR_PRIx " size 0x%x value 0x%" PRIx64 "\n",
                  addr, size, val);
        break;
    }
}

static const MemoryRegionOps cg3_reg_ops = {
    .read = cg3_reg_read,
    .write = cg3_reg_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
    },
};

static const GraphicHwOps cg3_ops = {
    .invalidate = cg3_invalidate_display,
    .gfx_update = cg3_update_display,
};

static void cg3_initfn(Object *obj)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    CG3State *s = CG3(obj);

    memory_region_init_ram_nomigrate(&s->rom, obj, "cg3.prom", FCODE_MAX_ROM_SIZE,
                           &error_fatal);
    memory_region_set_readonly(&s->rom, true);
    sysbus_init_mmio(sbd, &s->rom);

    memory_region_init_io(&s->reg, obj, &cg3_reg_ops, s, "cg3.reg",
                          CG3_REG_SIZE);
    sysbus_init_mmio(sbd, &s->reg);
}

static void cg3_realizefn(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    CG3State *s = CG3(dev);
    int ret;
    char *fcode_filename;

    /* FCode ROM */
    vmstate_register_ram_global(&s->rom);
    fcode_filename = qemu_find_file(QEMU_FILE_TYPE_BIOS, CG3_ROM_FILE);
    if (fcode_filename) {
        ret = load_image_mr(fcode_filename, &s->rom);
        g_free(fcode_filename);
        if (ret < 0 || ret > FCODE_MAX_ROM_SIZE) {
            warn_report("cg3: could not load prom '%s'", CG3_ROM_FILE);
        }
    }

    memory_region_init_ram(&s->vram_mem, NULL, "cg3.vram", s->vram_size,
                           &error_fatal);
    memory_region_set_log(&s->vram_mem, true, DIRTY_MEMORY_VGA);
    sysbus_init_mmio(sbd, &s->vram_mem);

    sysbus_init_irq(sbd, &s->irq);

    s->con = graphic_console_init(DEVICE(dev), 0, &cg3_ops, s);
    qemu_console_resize(s->con, s->width, s->height);
}

static int vmstate_cg3_post_load(void *opaque, int version_id)
{
    CG3State *s = opaque;

    cg3_invalidate_display(s);

    return 0;
}

static const VMStateDescription vmstate_cg3 = {
    .name = "cg3",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = vmstate_cg3_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(height, CG3State),
        VMSTATE_UINT16(width, CG3State),
        VMSTATE_UINT16(depth, CG3State),
        VMSTATE_BUFFER(r, CG3State),
        VMSTATE_BUFFER(g, CG3State),
        VMSTATE_BUFFER(b, CG3State),
        VMSTATE_UINT8(dac_index, CG3State),
        VMSTATE_UINT8(dac_state, CG3State),
        VMSTATE_END_OF_LIST()
    }
};

static void cg3_reset(DeviceState *d)
{
    CG3State *s = CG3(d);

    /* Initialize palette */
    memset(s->r, 0, 256);
    memset(s->g, 0, 256);
    memset(s->b, 0, 256);

    s->dac_state = 0;
    s->full_update = 1;
    qemu_irq_lower(s->irq);
}

static Property cg3_properties[] = {
    DEFINE_PROP_UINT32("vram-size",    CG3State, vram_size, -1),
    DEFINE_PROP_UINT16("width",        CG3State, width,     -1),
    DEFINE_PROP_UINT16("height",       CG3State, height,    -1),
    DEFINE_PROP_UINT16("depth",        CG3State, depth,     -1),
    DEFINE_PROP_END_OF_LIST(),
};

static void cg3_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = cg3_realizefn;
    dc->reset = cg3_reset;
    dc->vmsd = &vmstate_cg3;
    dc->props = cg3_properties;
}

static const TypeInfo cg3_info = {
    .name          = TYPE_CG3,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CG3State),
    .instance_init = cg3_initfn,
    .class_init    = cg3_class_init,
};

static void cg3_register_types(void)
{
    type_register_static(&cg3_info);
}

type_init(cg3_register_types)
