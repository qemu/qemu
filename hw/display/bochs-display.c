/*
 * QEMU PCI bochs display adapter.
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "hw/hw.h"
#include "hw/pci/pci.h"
#include "hw/display/bochs-vbe.h"
#include "hw/display/edid.h"

#include "qapi/error.h"

#include "ui/console.h"
#include "ui/qemu-pixman.h"

typedef struct BochsDisplayMode {
    pixman_format_code_t format;
    uint32_t             bytepp;
    uint32_t             width;
    uint32_t             height;
    uint32_t             stride;
    uint64_t             offset;
    uint64_t             size;
} BochsDisplayMode;

typedef struct BochsDisplayState {
    /* parent */
    PCIDevice        pci;

    /* device elements */
    QemuConsole      *con;
    MemoryRegion     vram;
    MemoryRegion     mmio;
    MemoryRegion     vbe;
    MemoryRegion     qext;
    MemoryRegion     edid;

    /* device config */
    uint64_t         vgamem;
    bool             enable_edid;
    qemu_edid_info   edid_info;
    uint8_t          edid_blob[256];

    /* device registers */
    uint16_t         vbe_regs[VBE_DISPI_INDEX_NB];
    bool             big_endian_fb;

    /* device state */
    BochsDisplayMode mode;
} BochsDisplayState;

#define TYPE_BOCHS_DISPLAY "bochs-display"
#define BOCHS_DISPLAY(obj) OBJECT_CHECK(BochsDisplayState, (obj), \
                                        TYPE_BOCHS_DISPLAY)

static const VMStateDescription vmstate_bochs_display = {
    .name = "bochs-display",
    .fields = (VMStateField[]) {
        VMSTATE_PCI_DEVICE(pci, BochsDisplayState),
        VMSTATE_UINT16_ARRAY(vbe_regs, BochsDisplayState, VBE_DISPI_INDEX_NB),
        VMSTATE_BOOL(big_endian_fb, BochsDisplayState),
        VMSTATE_END_OF_LIST()
    }
};

static uint64_t bochs_display_vbe_read(void *ptr, hwaddr addr,
                                       unsigned size)
{
    BochsDisplayState *s = ptr;
    unsigned int index = addr >> 1;

    switch (index) {
    case VBE_DISPI_INDEX_ID:
        return VBE_DISPI_ID5;
    case VBE_DISPI_INDEX_VIDEO_MEMORY_64K:
        return s->vgamem / (64 * KiB);
    }

    if (index >= ARRAY_SIZE(s->vbe_regs)) {
        return -1;
    }
    return s->vbe_regs[index];
}

static void bochs_display_vbe_write(void *ptr, hwaddr addr,
                                    uint64_t val, unsigned size)
{
    BochsDisplayState *s = ptr;
    unsigned int index = addr >> 1;

    if (index >= ARRAY_SIZE(s->vbe_regs)) {
        return;
    }
    s->vbe_regs[index] = val;
}

static const MemoryRegionOps bochs_display_vbe_ops = {
    .read = bochs_display_vbe_read,
    .write = bochs_display_vbe_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 2,
    .impl.max_access_size = 2,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t bochs_display_qext_read(void *ptr, hwaddr addr,
                                        unsigned size)
{
    BochsDisplayState *s = ptr;

    switch (addr) {
    case PCI_VGA_QEXT_REG_SIZE:
        return PCI_VGA_QEXT_SIZE;
    case PCI_VGA_QEXT_REG_BYTEORDER:
        return s->big_endian_fb ?
            PCI_VGA_QEXT_BIG_ENDIAN : PCI_VGA_QEXT_LITTLE_ENDIAN;
    default:
        return 0;
    }
}

static void bochs_display_qext_write(void *ptr, hwaddr addr,
                                     uint64_t val, unsigned size)
{
    BochsDisplayState *s = ptr;

    switch (addr) {
    case PCI_VGA_QEXT_REG_BYTEORDER:
        if (val == PCI_VGA_QEXT_BIG_ENDIAN) {
            s->big_endian_fb = true;
        }
        if (val == PCI_VGA_QEXT_LITTLE_ENDIAN) {
            s->big_endian_fb = false;
        }
        break;
    }
}

static const MemoryRegionOps bochs_display_qext_ops = {
    .read = bochs_display_qext_read,
    .write = bochs_display_qext_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static int bochs_display_get_mode(BochsDisplayState *s,
                                   BochsDisplayMode *mode)
{
    uint16_t *vbe = s->vbe_regs;
    uint32_t virt_width;

    if (!(vbe[VBE_DISPI_INDEX_ENABLE] & VBE_DISPI_ENABLED)) {
        return -1;
    }

    memset(mode, 0, sizeof(*mode));
    switch (vbe[VBE_DISPI_INDEX_BPP]) {
    case 16:
        /* best effort: support native endianess only */
        mode->format = PIXMAN_r5g6b5;
        mode->bytepp = 2;
        break;
    case 32:
        mode->format = s->big_endian_fb
            ? PIXMAN_BE_x8r8g8b8
            : PIXMAN_LE_x8r8g8b8;
        mode->bytepp = 4;
        break;
    default:
        return -1;
    }

    mode->width  = vbe[VBE_DISPI_INDEX_XRES];
    mode->height = vbe[VBE_DISPI_INDEX_YRES];
    virt_width  = vbe[VBE_DISPI_INDEX_VIRT_WIDTH];
    if (virt_width < mode->width) {
        virt_width = mode->width;
    }
    mode->stride = virt_width * mode->bytepp;
    mode->size   = (uint64_t)mode->stride * mode->height;
    mode->offset = ((uint64_t)vbe[VBE_DISPI_INDEX_X_OFFSET] * mode->bytepp +
                    (uint64_t)vbe[VBE_DISPI_INDEX_Y_OFFSET] * mode->stride);

    if (mode->width < 64 || mode->height < 64) {
        return -1;
    }
    if (mode->offset + mode->size > s->vgamem) {
        return -1;
    }
    return 0;
}

static void bochs_display_update(void *opaque)
{
    BochsDisplayState *s = opaque;
    DirtyBitmapSnapshot *snap = NULL;
    bool full_update = false;
    BochsDisplayMode mode;
    DisplaySurface *ds;
    uint8_t *ptr;
    bool dirty;
    int y, ys, ret;

    ret = bochs_display_get_mode(s, &mode);
    if (ret < 0) {
        /* no (valid) video mode */
        return;
    }

    if (memcmp(&s->mode, &mode, sizeof(mode)) != 0) {
        /* video mode switch */
        s->mode = mode;
        ptr = memory_region_get_ram_ptr(&s->vram);
        ds = qemu_create_displaysurface_from(mode.width,
                                             mode.height,
                                             mode.format,
                                             mode.stride,
                                             ptr + mode.offset);
        dpy_gfx_replace_surface(s->con, ds);
        full_update = true;
    }

    if (full_update) {
        dpy_gfx_update_full(s->con);
    } else {
        snap = memory_region_snapshot_and_clear_dirty(&s->vram,
                                                      mode.offset, mode.size,
                                                      DIRTY_MEMORY_VGA);
        ys = -1;
        for (y = 0; y < mode.height; y++) {
            dirty = memory_region_snapshot_get_dirty(&s->vram, snap,
                                                     mode.offset + mode.stride * y,
                                                     mode.stride);
            if (dirty && ys < 0) {
                ys = y;
            }
            if (!dirty && ys >= 0) {
                dpy_gfx_update(s->con, 0, ys,
                               mode.width, y - ys);
                ys = -1;
            }
        }
        if (ys >= 0) {
            dpy_gfx_update(s->con, 0, ys,
                           mode.width, y - ys);
        }
    }
}

static const GraphicHwOps bochs_display_gfx_ops = {
    .gfx_update = bochs_display_update,
};

static void bochs_display_realize(PCIDevice *dev, Error **errp)
{
    BochsDisplayState *s = BOCHS_DISPLAY(dev);
    Object *obj = OBJECT(dev);
    int ret;

    s->con = graphic_console_init(DEVICE(dev), 0, &bochs_display_gfx_ops, s);

    if (s->vgamem < 4 * MiB) {
        error_setg(errp, "bochs-display: video memory too small");
    }
    if (s->vgamem > 256 * MiB) {
        error_setg(errp, "bochs-display: video memory too big");
    }
    s->vgamem = pow2ceil(s->vgamem);

    memory_region_init_ram(&s->vram, obj, "bochs-display-vram", s->vgamem,
                           &error_fatal);
    memory_region_init_io(&s->vbe, obj, &bochs_display_vbe_ops, s,
                          "bochs dispi interface", PCI_VGA_BOCHS_SIZE);
    memory_region_init_io(&s->qext, obj, &bochs_display_qext_ops, s,
                          "qemu extended regs", PCI_VGA_QEXT_SIZE);

    memory_region_init(&s->mmio, obj, "bochs-display-mmio",
                       PCI_VGA_MMIO_SIZE);
    memory_region_add_subregion(&s->mmio, PCI_VGA_BOCHS_OFFSET, &s->vbe);
    memory_region_add_subregion(&s->mmio, PCI_VGA_QEXT_OFFSET, &s->qext);

    pci_set_byte(&s->pci.config[PCI_REVISION_ID], 2);
    pci_register_bar(&s->pci, 0, PCI_BASE_ADDRESS_MEM_PREFETCH, &s->vram);
    pci_register_bar(&s->pci, 2, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);

    if (s->enable_edid) {
        qemu_edid_generate(s->edid_blob, sizeof(s->edid_blob), &s->edid_info);
        qemu_edid_region_io(&s->edid, obj, s->edid_blob, sizeof(s->edid_blob));
        memory_region_add_subregion(&s->mmio, 0, &s->edid);
    }

    if (pci_bus_is_express(pci_get_bus(dev))) {
        dev->cap_present |= QEMU_PCI_CAP_EXPRESS;
        ret = pcie_endpoint_cap_init(dev, 0x80);
        assert(ret > 0);
    }

    memory_region_set_log(&s->vram, true, DIRTY_MEMORY_VGA);
}

static bool bochs_display_get_big_endian_fb(Object *obj, Error **errp)
{
    BochsDisplayState *s = BOCHS_DISPLAY(obj);

    return s->big_endian_fb;
}

static void bochs_display_set_big_endian_fb(Object *obj, bool value,
                                            Error **errp)
{
    BochsDisplayState *s = BOCHS_DISPLAY(obj);

    s->big_endian_fb = value;
}

static void bochs_display_init(Object *obj)
{
    /* Expose framebuffer byteorder via QOM */
    object_property_add_bool(obj, "big-endian-framebuffer",
                             bochs_display_get_big_endian_fb,
                             bochs_display_set_big_endian_fb,
                             NULL);
}

static void bochs_display_exit(PCIDevice *dev)
{
    BochsDisplayState *s = BOCHS_DISPLAY(dev);

    graphic_console_close(s->con);
}

static Property bochs_display_properties[] = {
    DEFINE_PROP_SIZE("vgamem", BochsDisplayState, vgamem, 16 * MiB),
    DEFINE_PROP_BOOL("edid", BochsDisplayState, enable_edid, false),
    DEFINE_EDID_PROPERTIES(BochsDisplayState, edid_info),
    DEFINE_PROP_END_OF_LIST(),
};

static void bochs_display_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *k = PCI_DEVICE_CLASS(klass);

    k->class_id  = PCI_CLASS_DISPLAY_OTHER;
    k->vendor_id = PCI_VENDOR_ID_QEMU;
    k->device_id = PCI_DEVICE_ID_QEMU_VGA;

    k->realize   = bochs_display_realize;
    k->romfile   = "vgabios-bochs-display.bin";
    k->exit      = bochs_display_exit;
    dc->vmsd     = &vmstate_bochs_display;
    dc->props    = bochs_display_properties;
    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
}

static const TypeInfo bochs_display_type_info = {
    .name           = TYPE_BOCHS_DISPLAY,
    .parent         = TYPE_PCI_DEVICE,
    .instance_size  = sizeof(BochsDisplayState),
    .instance_init  = bochs_display_init,
    .class_init     = bochs_display_class_init,
    .interfaces     = (InterfaceInfo[]) {
        { INTERFACE_PCIE_DEVICE },
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void bochs_display_register_types(void)
{
    type_register_static(&bochs_display_type_info);
}

type_init(bochs_display_register_types)
