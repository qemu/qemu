/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Virt system Controller
 */

#include "qemu/osdep.h"
#include "hw/qdev-properties.h"
#include "hw/sysbus.h"
#include "migration/vmstate.h"
#include "qemu/log.h"
#include "trace.h"
#include "system/runstate.h"
#include "hw/misc/virt_ctrl.h"

enum {
    REG_FEATURES = 0x00,
    REG_CMD      = 0x04,
};

#define FEAT_POWER_CTRL 0x00000001

enum {
    CMD_NOOP,
    CMD_RESET,
    CMD_HALT,
    CMD_PANIC,
};

static uint64_t virt_ctrl_read(void *opaque, hwaddr addr, unsigned size)
{
    VirtCtrlState *s = opaque;
    uint64_t value = 0;

    switch (addr) {
    case REG_FEATURES:
        value = FEAT_POWER_CTRL;
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register read 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }

    trace_virt_ctrl_write(s, addr, size, value);

    return value;
}

static void virt_ctrl_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned size)
{
    VirtCtrlState *s = opaque;

    trace_virt_ctrl_write(s, addr, size, value);

    switch (addr) {
    case REG_CMD:
        switch (value) {
        case CMD_NOOP:
            break;
        case CMD_RESET:
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
            break;
        case CMD_HALT:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
            break;
        case CMD_PANIC:
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_PANIC);
            break;
        }
        break;
    default:
        qemu_log_mask(LOG_UNIMP,
                      "%s: unimplemented register write 0x%02"HWADDR_PRIx"\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps virt_ctrl_ops = {
    .read = virt_ctrl_read,
    .write = virt_ctrl_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
    .valid.max_access_size = 4,
    .impl.max_access_size = 4,
};

static void virt_ctrl_reset(DeviceState *dev)
{
    VirtCtrlState *s = VIRT_CTRL(dev);

    trace_virt_ctrl_reset(s);
}

static void virt_ctrl_realize(DeviceState *dev, Error **errp)
{
    VirtCtrlState *s = VIRT_CTRL(dev);

    trace_virt_ctrl_instance_init(s);

    memory_region_init_io(&s->iomem, OBJECT(s), &virt_ctrl_ops, s,
                          "virt-ctrl", 0x100);
}

static const VMStateDescription vmstate_virt_ctrl = {
    .name = "virt-ctrl",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(irq_enabled, VirtCtrlState),
        VMSTATE_END_OF_LIST()
    }
};

static void virt_ctrl_instance_init(Object *obj)
{
    SysBusDevice *dev = SYS_BUS_DEVICE(obj);
    VirtCtrlState *s = VIRT_CTRL(obj);

    trace_virt_ctrl_instance_init(s);

    sysbus_init_mmio(dev, &s->iomem);
    sysbus_init_irq(dev, &s->irq);
}

static void virt_ctrl_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);

    device_class_set_legacy_reset(dc, virt_ctrl_reset);
    dc->realize = virt_ctrl_realize;
    dc->vmsd = &vmstate_virt_ctrl;
}

static const TypeInfo virt_ctrl_info = {
    .name = TYPE_VIRT_CTRL,
    .parent = TYPE_SYS_BUS_DEVICE,
    .class_init = virt_ctrl_class_init,
    .instance_init = virt_ctrl_instance_init,
    .instance_size = sizeof(VirtCtrlState),
};

static void virt_ctrl_register_types(void)
{
    type_register_static(&virt_ctrl_info);
}

type_init(virt_ctrl_register_types)
