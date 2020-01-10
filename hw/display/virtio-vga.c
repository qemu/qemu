#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/qdev-properties.h"
#include "hw/virtio/virtio-gpu.h"
#include "qapi/error.h"
#include "qemu/module.h"
#include "virtio-vga.h"

static void virtio_vga_base_invalidate_display(void *opaque)
{
    VirtIOVGABase *vvga = opaque;
    VirtIOGPUBase *g = vvga->vgpu;

    if (g->enable) {
        virtio_gpu_ops.invalidate(g);
    } else {
        vvga->vga.hw_ops->invalidate(&vvga->vga);
    }
}

static void virtio_vga_base_update_display(void *opaque)
{
    VirtIOVGABase *vvga = opaque;
    VirtIOGPUBase *g = vvga->vgpu;

    if (g->enable) {
        virtio_gpu_ops.gfx_update(g);
    } else {
        vvga->vga.hw_ops->gfx_update(&vvga->vga);
    }
}

static void virtio_vga_base_text_update(void *opaque, console_ch_t *chardata)
{
    VirtIOVGABase *vvga = opaque;
    VirtIOGPUBase *g = vvga->vgpu;

    if (g->enable) {
        if (virtio_gpu_ops.text_update) {
            virtio_gpu_ops.text_update(g, chardata);
        }
    } else {
        if (vvga->vga.hw_ops->text_update) {
            vvga->vga.hw_ops->text_update(&vvga->vga, chardata);
        }
    }
}

static int virtio_vga_base_ui_info(void *opaque, uint32_t idx, QemuUIInfo *info)
{
    VirtIOVGABase *vvga = opaque;
    VirtIOGPUBase *g = vvga->vgpu;

    if (virtio_gpu_ops.ui_info) {
        return virtio_gpu_ops.ui_info(g, idx, info);
    }
    return -1;
}

static void virtio_vga_base_gl_block(void *opaque, bool block)
{
    VirtIOVGABase *vvga = opaque;
    VirtIOGPUBase *g = vvga->vgpu;

    if (virtio_gpu_ops.gl_block) {
        virtio_gpu_ops.gl_block(g, block);
    }
}

static const GraphicHwOps virtio_vga_base_ops = {
    .invalidate = virtio_vga_base_invalidate_display,
    .gfx_update = virtio_vga_base_update_display,
    .text_update = virtio_vga_base_text_update,
    .ui_info = virtio_vga_base_ui_info,
    .gl_block = virtio_vga_base_gl_block,
};

static const VMStateDescription vmstate_virtio_vga_base = {
    .name = "virtio-vga",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (VMStateField[]) {
        /* no pci stuff here, saving the virtio device will handle that */
        VMSTATE_STRUCT(vga, VirtIOVGABase, 0,
                       vmstate_vga_common, VGACommonState),
        VMSTATE_END_OF_LIST()
    }
};

/* VGA device wrapper around PCI device around virtio GPU */
static void virtio_vga_base_realize(VirtIOPCIProxy *vpci_dev, Error **errp)
{
    VirtIOVGABase *vvga = VIRTIO_VGA_BASE(vpci_dev);
    VirtIOGPUBase *g = vvga->vgpu;
    VGACommonState *vga = &vvga->vga;
    Error *err = NULL;
    uint32_t offset;
    int i;

    /* init vga compat bits */
    vga->vram_size_mb = 8;
    vga_common_init(vga, OBJECT(vpci_dev));
    vga_init(vga, OBJECT(vpci_dev), pci_address_space(&vpci_dev->pci_dev),
             pci_address_space_io(&vpci_dev->pci_dev), true);
    pci_register_bar(&vpci_dev->pci_dev, 0,
                     PCI_BASE_ADDRESS_MEM_PREFETCH, &vga->vram);

    /*
     * Configure virtio bar and regions
     *
     * We use bar #2 for the mmio regions, to be compatible with stdvga.
     * virtio regions are moved to the end of bar #2, to make room for
     * the stdvga mmio registers at the start of bar #2.
     */
    vpci_dev->modern_mem_bar_idx = 2;
    vpci_dev->msix_bar_idx = 4;

    if (!(vpci_dev->flags & VIRTIO_PCI_FLAG_PAGE_PER_VQ)) {
        /*
         * with page-per-vq=off there is no padding space we can use
         * for the stdvga registers.  Make the common and isr regions
         * smaller then.
         */
        vpci_dev->common.size /= 2;
        vpci_dev->isr.size /= 2;
    }

    offset = memory_region_size(&vpci_dev->modern_bar);
    offset -= vpci_dev->notify.size;
    vpci_dev->notify.offset = offset;
    offset -= vpci_dev->device.size;
    vpci_dev->device.offset = offset;
    offset -= vpci_dev->isr.size;
    vpci_dev->isr.offset = offset;
    offset -= vpci_dev->common.size;
    vpci_dev->common.offset = offset;

    /* init virtio bits */
    qdev_set_parent_bus(DEVICE(g), BUS(&vpci_dev->bus));
    virtio_pci_force_virtio_1(vpci_dev);
    object_property_set_bool(OBJECT(g), true, "realized", &err);
    if (err) {
        error_propagate(errp, err);
        return;
    }

    /* add stdvga mmio regions */
    pci_std_vga_mmio_region_init(vga, OBJECT(vvga), &vpci_dev->modern_bar,
                                 vvga->vga_mrs, true, false);

    vga->con = g->scanout[0].con;
    graphic_console_set_hwops(vga->con, &virtio_vga_base_ops, vvga);

    for (i = 0; i < g->conf.max_outputs; i++) {
        object_property_set_link(OBJECT(g->scanout[i].con),
                                 OBJECT(vpci_dev),
                                 "device", errp);
    }
}

static void virtio_vga_base_reset(DeviceState *dev)
{
    VirtIOVGABaseClass *klass = VIRTIO_VGA_BASE_GET_CLASS(dev);
    VirtIOVGABase *vvga = VIRTIO_VGA_BASE(dev);

    /* reset virtio-gpu */
    klass->parent_reset(dev);

    /* reset vga */
    vga_common_reset(&vvga->vga);
    vga_dirty_log_start(&vvga->vga);
}

static Property virtio_vga_base_properties[] = {
    DEFINE_VIRTIO_GPU_PCI_PROPERTIES(VirtIOPCIProxy),
    DEFINE_PROP_END_OF_LIST(),
};

static void virtio_vga_base_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    VirtioPCIClass *k = VIRTIO_PCI_CLASS(klass);
    VirtIOVGABaseClass *v = VIRTIO_VGA_BASE_CLASS(klass);
    PCIDeviceClass *pcidev_k = PCI_DEVICE_CLASS(klass);

    set_bit(DEVICE_CATEGORY_DISPLAY, dc->categories);
    device_class_set_props(dc, virtio_vga_base_properties);
    dc->vmsd = &vmstate_virtio_vga_base;
    dc->hotpluggable = false;
    device_class_set_parent_reset(dc, virtio_vga_base_reset,
                                  &v->parent_reset);

    k->realize = virtio_vga_base_realize;
    pcidev_k->romfile = "vgabios-virtio.bin";
    pcidev_k->class_id = PCI_CLASS_DISPLAY_VGA;
}

static TypeInfo virtio_vga_base_info = {
    .name          = TYPE_VIRTIO_VGA_BASE,
    .parent        = TYPE_VIRTIO_PCI,
    .instance_size = sizeof(struct VirtIOVGABase),
    .class_size    = sizeof(struct VirtIOVGABaseClass),
    .class_init    = virtio_vga_base_class_init,
    .abstract      = true,
};

#define TYPE_VIRTIO_VGA "virtio-vga"

#define VIRTIO_VGA(obj)                             \
    OBJECT_CHECK(VirtIOVGA, (obj), TYPE_VIRTIO_VGA)

typedef struct VirtIOVGA {
    VirtIOVGABase parent_obj;

    VirtIOGPU     vdev;
} VirtIOVGA;

static void virtio_vga_inst_initfn(Object *obj)
{
    VirtIOVGA *dev = VIRTIO_VGA(obj);

    virtio_instance_init_common(obj, &dev->vdev, sizeof(dev->vdev),
                                TYPE_VIRTIO_GPU);
    VIRTIO_VGA_BASE(dev)->vgpu = VIRTIO_GPU_BASE(&dev->vdev);
}


static VirtioPCIDeviceTypeInfo virtio_vga_info = {
    .generic_name  = TYPE_VIRTIO_VGA,
    .parent        = TYPE_VIRTIO_VGA_BASE,
    .instance_size = sizeof(struct VirtIOVGA),
    .instance_init = virtio_vga_inst_initfn,
};

static void virtio_vga_register_types(void)
{
    type_register_static(&virtio_vga_base_info);
    virtio_pci_types_register(&virtio_vga_info);
}

type_init(virtio_vga_register_types)
