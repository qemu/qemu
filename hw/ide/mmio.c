/*
 * QEMU IDE Emulation: mmio support (for embedded).
 *
 * Copyright (c) 2003 Fabrice Bellard
 * Copyright (c) 2006 Openedhand Ltd.
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
#include "hw/hw.h"
#include "hw/sysbus.h"
#include "block/block.h"
#include "sysemu/dma.h"

#include <hw/ide/internal.h>

/***********************************************************/
/* MMIO based ide port
 * This emulates IDE device connected directly to the CPU bus without
 * dedicated ide controller, which is often seen on embedded boards.
 */

#define TYPE_MMIO_IDE "mmio-ide"
#define MMIO_IDE(obj) OBJECT_CHECK(MMIOState, (obj), TYPE_MMIO_IDE)

typedef struct MMIOIDEState {
    /*< private >*/
    SysBusDevice parent_obj;
    /*< public >*/

    IDEBus bus;

    uint32_t shift;
    qemu_irq irq;
    MemoryRegion iomem1, iomem2;
} MMIOState;

static void mmio_ide_reset(DeviceState *dev)
{
    MMIOState *s = MMIO_IDE(dev);

    ide_bus_reset(&s->bus);
}

static uint64_t mmio_ide_read(void *opaque, hwaddr addr,
                              unsigned size)
{
    MMIOState *s = opaque;
    addr >>= s->shift;
    if (addr & 7)
        return ide_ioport_read(&s->bus, addr);
    else
        return ide_data_readw(&s->bus, 0);
}

static void mmio_ide_write(void *opaque, hwaddr addr,
                           uint64_t val, unsigned size)
{
    MMIOState *s = opaque;
    addr >>= s->shift;
    if (addr & 7)
        ide_ioport_write(&s->bus, addr, val);
    else
        ide_data_writew(&s->bus, 0, val);
}

static const MemoryRegionOps mmio_ide_ops = {
    .read = mmio_ide_read,
    .write = mmio_ide_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static uint64_t mmio_ide_status_read(void *opaque, hwaddr addr,
                                     unsigned size)
{
    MMIOState *s= opaque;
    return ide_status_read(&s->bus, 0);
}

static void mmio_ide_cmd_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    MMIOState *s = opaque;
    ide_cmd_write(&s->bus, 0, val);
}

static const MemoryRegionOps mmio_ide_cs_ops = {
    .read = mmio_ide_status_read,
    .write = mmio_ide_cmd_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const VMStateDescription vmstate_ide_mmio = {
    .name = "mmio-ide",
    .version_id = 3,
    .minimum_version_id = 0,
    .fields = (VMStateField[]) {
        VMSTATE_IDE_BUS(bus, MMIOState),
        VMSTATE_IDE_DRIVES(bus.ifs, MMIOState),
        VMSTATE_END_OF_LIST()
    }
};

static void mmio_ide_realizefn(DeviceState *dev, Error **errp)
{
    SysBusDevice *d = SYS_BUS_DEVICE(dev);
    MMIOState *s = MMIO_IDE(dev);

    ide_init2(&s->bus, s->irq);

    memory_region_init_io(&s->iomem1, OBJECT(s), &mmio_ide_ops, s,
                          "ide-mmio.1", 16 << s->shift);
    memory_region_init_io(&s->iomem2, OBJECT(s), &mmio_ide_cs_ops, s,
                          "ide-mmio.2", 2 << s->shift);
    sysbus_init_mmio(d, &s->iomem1);
    sysbus_init_mmio(d, &s->iomem2);
}

static void mmio_ide_initfn(Object *obj)
{
    SysBusDevice *d = SYS_BUS_DEVICE(obj);
    MMIOState *s = MMIO_IDE(obj);

    ide_bus_new(&s->bus, sizeof(s->bus), DEVICE(obj), 0, 2);
    sysbus_init_irq(d, &s->irq);
}

static Property mmio_ide_properties[] = {
    DEFINE_PROP_UINT32("shift", MMIOState, shift, 0),
    DEFINE_PROP_END_OF_LIST()
};

static void mmio_ide_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    dc->realize = mmio_ide_realizefn;
    dc->reset = mmio_ide_reset;
    dc->props = mmio_ide_properties;
    dc->vmsd = &vmstate_ide_mmio;
}

static const TypeInfo mmio_ide_type_info = {
    .name = TYPE_MMIO_IDE,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MMIOState),
    .instance_init = mmio_ide_initfn,
    .class_init = mmio_ide_class_init,
};

static void mmio_ide_register_types(void)
{
    type_register_static(&mmio_ide_type_info);
}

void mmio_ide_init_drives(DeviceState *dev, DriveInfo *hd0, DriveInfo *hd1)
{
    MMIOState *s = MMIO_IDE(dev);

    if (hd0 != NULL) {
        ide_create_drive(&s->bus, 0, hd0);
    }
    if (hd1 != NULL) {
        ide_create_drive(&s->bus, 1, hd1);
    }
}

type_init(mmio_ide_register_types)
