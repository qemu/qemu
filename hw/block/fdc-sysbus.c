/*
 * QEMU Floppy disk emulator (Intel 82078)
 *
 * Copyright (c) 2003, 2007 Jocelyn Mayer
 * Copyright (c) 2008 Hervé Poussineau
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
#include "qapi/error.h"
#include "qom/object.h"
#include "system/memory.h"
#include "hw/sysbus.h"
#include "hw/block/fdc.h"
#include "migration/vmstate.h"
#include "fdc-internal.h"
#include "trace.h"

#define TYPE_SYSBUS_FDC "base-sysbus-fdc"
typedef struct FDCtrlSysBusClass FDCtrlSysBusClass;
typedef struct FDCtrlSysBus FDCtrlSysBus;
DECLARE_OBJ_CHECKERS(FDCtrlSysBus, FDCtrlSysBusClass,
                     SYSBUS_FDC, TYPE_SYSBUS_FDC)

struct FDCtrlSysBusClass {
    /*< private >*/
    SysBusDeviceClass parent_class;
    /*< public >*/

    bool use_strict_io;
};

struct FDCtrlSysBus {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    struct FDCtrl state;
    MemoryRegion iomem;
};

static uint64_t fdctrl_read_mem(void *opaque, hwaddr reg, unsigned ize)
{
    return fdctrl_read(opaque, (uint32_t)reg);
}

static void fdctrl_write_mem(void *opaque, hwaddr reg,
                             uint64_t value, unsigned size)
{
    fdctrl_write(opaque, (uint32_t)reg, value);
}

static const MemoryRegionOps fdctrl_mem_ops = {
    .read = fdctrl_read_mem,
    .write = fdctrl_write_mem,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const MemoryRegionOps fdctrl_mem_strict_ops = {
    .read = fdctrl_read_mem,
    .write = fdctrl_write_mem,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void fdctrl_external_reset_sysbus(DeviceState *d)
{
    FDCtrlSysBus *sys = SYSBUS_FDC(d);
    FDCtrl *s = &sys->state;

    fdctrl_reset(s, 0);
}

static void fdctrl_handle_tc(void *opaque, int irq, int level)
{
    trace_fdctrl_tc_pulse(level);
}

void fdctrl_init_sysbus(qemu_irq irq, hwaddr mmio_base, DriveInfo **fds)
{
    DeviceState *dev;
    SysBusDevice *sbd;
    FDCtrlSysBus *sys;

    dev = qdev_new("sysbus-fdc");
    sys = SYSBUS_FDC(dev);
    sbd = SYS_BUS_DEVICE(dev);
    sysbus_realize_and_unref(sbd, &error_fatal);
    sysbus_connect_irq(sbd, 0, irq);
    sysbus_mmio_map(sbd, 0, mmio_base);

    fdctrl_init_drives(&sys->state.bus, fds);
}

void sun4m_fdctrl_init(qemu_irq irq, hwaddr io_base,
                       DriveInfo **fds, qemu_irq *fdc_tc)
{
    DeviceState *dev;
    FDCtrlSysBus *sys;

    dev = qdev_new("sun-fdtwo");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sys = SYSBUS_FDC(dev);
    sysbus_connect_irq(SYS_BUS_DEVICE(sys), 0, irq);
    sysbus_mmio_map(SYS_BUS_DEVICE(sys), 0, io_base);
    *fdc_tc = qdev_get_gpio_in(dev, 0);

    fdctrl_init_drives(&sys->state.bus, fds);
}

static void sysbus_fdc_common_instance_init(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    FDCtrlSysBusClass *sbdc = SYSBUS_FDC_GET_CLASS(obj);
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    FDCtrlSysBus *sys = SYSBUS_FDC(obj);
    FDCtrl *fdctrl = &sys->state;

    /*
     * DMA is not currently supported for sysbus floppy controllers.
     * If we wanted to add support then probably the best approach is
     * to have a QOM link property 'dma-controller' which the board
     * code can set to an instance of IsaDmaClass, and an integer
     * property 'dma-channel', so that we can set fdctrl->dma and
     * fdctrl->dma_chann accordingly.
     */
    fdctrl->dma_chann = -1;

    qdev_set_legacy_instance_id(dev, 0 /* io */, 2); /* FIXME */

    memory_region_init_io(&sys->iomem, obj,
                          sbdc->use_strict_io ? &fdctrl_mem_strict_ops
                                              : &fdctrl_mem_ops,
                          fdctrl, "fdc", 0x08);
    sysbus_init_mmio(sbd, &sys->iomem);

    sysbus_init_irq(sbd, &fdctrl->irq);
    qdev_init_gpio_in(dev, fdctrl_handle_tc, 1);
}

static void sysbus_fdc_realize(DeviceState *dev, Error **errp)
{
    FDCtrlSysBus *sys = SYSBUS_FDC(dev);
    FDCtrl *fdctrl = &sys->state;

    fdctrl_realize_common(dev, fdctrl, errp);
}

static const VMStateDescription vmstate_sysbus_fdc = {
    .name = "fdc",
    .version_id = 2,
    .minimum_version_id = 2,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(state, FDCtrlSysBus, 0, vmstate_fdc, FDCtrl),
        VMSTATE_END_OF_LIST()
    }
};

static void sysbus_fdc_common_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = sysbus_fdc_realize;
    device_class_set_legacy_reset(dc, fdctrl_external_reset_sysbus);
    dc->vmsd = &vmstate_sysbus_fdc;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo sysbus_fdc_common_typeinfo = {
    .name          = TYPE_SYSBUS_FDC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(FDCtrlSysBus),
    .instance_init = sysbus_fdc_common_instance_init,
    .abstract      = true,
    .class_init    = sysbus_fdc_common_class_init,
    .class_size    = sizeof(FDCtrlSysBusClass),
};

static const Property sysbus_fdc_properties[] = {
    DEFINE_PROP_SIGNED("fdtypeA", FDCtrlSysBus, state.qdev_for_drives[0].type,
                        FLOPPY_DRIVE_TYPE_AUTO, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_SIGNED("fdtypeB", FDCtrlSysBus, state.qdev_for_drives[1].type,
                        FLOPPY_DRIVE_TYPE_AUTO, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_SIGNED("fallback", FDCtrlSysBus, state.fallback,
                        FLOPPY_DRIVE_TYPE_144, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
};

static void sysbus_fdc_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "virtual floppy controller";
    device_class_set_props(dc, sysbus_fdc_properties);
}

static const TypeInfo sysbus_fdc_typeinfo = {
    .name          = "sysbus-fdc",
    .parent        = TYPE_SYSBUS_FDC,
    .class_init    = sysbus_fdc_class_init,
};

static const Property sun4m_fdc_properties[] = {
    DEFINE_PROP_SIGNED("fdtype", FDCtrlSysBus, state.qdev_for_drives[0].type,
                        FLOPPY_DRIVE_TYPE_AUTO, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
    DEFINE_PROP_SIGNED("fallback", FDCtrlSysBus, state.fallback,
                        FLOPPY_DRIVE_TYPE_144, qdev_prop_fdc_drive_type,
                        FloppyDriveType),
};

static void sun4m_fdc_class_init(ObjectClass *klass, void *data)
{
    FDCtrlSysBusClass *sbdc = SYSBUS_FDC_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    sbdc->use_strict_io = true;
    dc->desc = "virtual floppy controller";
    device_class_set_props(dc, sun4m_fdc_properties);
}

static const TypeInfo sun4m_fdc_typeinfo = {
    .name          = "sun-fdtwo",
    .parent        = TYPE_SYSBUS_FDC,
    .class_init    = sun4m_fdc_class_init,
};

static void sysbus_fdc_register_types(void)
{
    type_register_static(&sysbus_fdc_common_typeinfo);
    type_register_static(&sysbus_fdc_typeinfo);
    type_register_static(&sun4m_fdc_typeinfo);
}

type_init(sysbus_fdc_register_types)
