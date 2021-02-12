/*
 * LED, Switch and Debug control registers for ARM Integrator Boards
 *
 * This is currently a stub for this functionality but at least
 * ensures something other than unassigned_mem_read() handles access
 * to this area.
 *
 * The real h/w is described at:
 *  https://developer.arm.com/documentation/dui0159/b/peripherals-and-interfaces/debug-leds-and-dip-switch-interface
 *
 * Copyright (c) 2013 Alex Bennée <alex@bennee.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2 or later.
 * See the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "hw/sysbus.h"
#include "hw/misc/arm_integrator_debug.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(IntegratorDebugState, INTEGRATOR_DEBUG)

struct IntegratorDebugState {
    SysBusDevice parent_obj;

    MemoryRegion iomem;
};

static uint64_t intdbg_control_read(void *opaque, hwaddr offset,
                                    unsigned size)
{
    switch (offset >> 2) {
    case 0: /* ALPHA */
    case 1: /* LEDS */
    case 2: /* SWITCHES */
        qemu_log_mask(LOG_UNIMP,
                      "%s: returning zero from %" HWADDR_PRIx ":%u\n",
                      __func__, offset, size);
        return 0;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset %" HWADDR_PRIx,
                      __func__, offset);
        return 0;
    }
}

static void intdbg_control_write(void *opaque, hwaddr offset,
                                 uint64_t value, unsigned size)
{
    switch (offset >> 2) {
    case 1: /* ALPHA */
    case 2: /* LEDS */
    case 3: /* SWITCHES */
        /* Nothing interesting implemented yet.  */
        qemu_log_mask(LOG_UNIMP,
                      "%s: ignoring write of %" PRIu64
                      " to %" HWADDR_PRIx ":%u\n",
                      __func__, value, offset, size);
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: write of %" PRIu64
                      " to bad offset %" HWADDR_PRIx "\n",
                      __func__, value, offset);
    }
}

static const MemoryRegionOps intdbg_control_ops = {
    .read = intdbg_control_read,
    .write = intdbg_control_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void intdbg_control_init(Object *obj)
{
    SysBusDevice *sd = SYS_BUS_DEVICE(obj);
    IntegratorDebugState *s = INTEGRATOR_DEBUG(obj);

    memory_region_init_io(&s->iomem, obj, &intdbg_control_ops,
                          NULL, "dbg-leds", 0x1000000);
    sysbus_init_mmio(sd, &s->iomem);
}

static const TypeInfo intdbg_info = {
    .name          = TYPE_INTEGRATOR_DEBUG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(IntegratorDebugState),
    .instance_init = intdbg_control_init,
};

static void intdbg_register_types(void)
{
    type_register_static(&intdbg_info);
}

type_init(intdbg_register_types)
