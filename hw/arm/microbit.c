/*
 * BBC micro:bit machine
 * http://tech.microbit.org/hardware/
 *
 * Copyright 2018 Joel Stanley <joel@jms.id.au>
 *
 * This code is licensed under the GPL version 2 or later.  See
 * the COPYING file in the top-level directory.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/arm/boot.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include "hw/arm/nrf51_soc.h"
#include "hw/i2c/microbit_i2c.h"
#include "hw/qdev-properties.h"
#include "qom/object.h"

struct MicrobitMachineState {
    MachineState parent;

    NRF51State nrf51;
    MicrobitI2CState i2c;
};

#define TYPE_MICROBIT_MACHINE MACHINE_TYPE_NAME("microbit")

OBJECT_DECLARE_SIMPLE_TYPE(MicrobitMachineState, MICROBIT_MACHINE)

static void microbit_init(MachineState *machine)
{
    MicrobitMachineState *s = MICROBIT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    MemoryRegion *mr;

    object_initialize_child(OBJECT(machine), "nrf51", &s->nrf51,
                            TYPE_NRF51_SOC);
    qdev_prop_set_chr(DEVICE(&s->nrf51), "serial0", serial_hd(0));
    object_property_set_link(OBJECT(&s->nrf51), "memory",
                             OBJECT(system_memory), &error_fatal);
    sysbus_realize(SYS_BUS_DEVICE(&s->nrf51), &error_fatal);

    /*
     * Overlap the TWI stub device into the SoC.  This is a microbit-specific
     * hack until we implement the nRF51 TWI controller properly and the
     * magnetometer/accelerometer devices.
     */
    object_initialize_child(OBJECT(machine), "microbit.twi", &s->i2c,
                            TYPE_MICROBIT_I2C);
    sysbus_realize(SYS_BUS_DEVICE(&s->i2c), &error_fatal);
    mr = sysbus_mmio_get_region(SYS_BUS_DEVICE(&s->i2c), 0);
    memory_region_add_subregion_overlap(&s->nrf51.container, NRF51_TWI_BASE,
                                        mr, -1);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       0, s->nrf51.flash_size);
}

static void microbit_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "BBC micro:bit (Cortex-M0)";
    mc->init = microbit_init;
    mc->max_cpus = 1;
}

static const TypeInfo microbit_info = {
    .name = TYPE_MICROBIT_MACHINE,
    .parent = TYPE_MACHINE,
    .instance_size = sizeof(MicrobitMachineState),
    .class_init = microbit_machine_class_init,
};

static void microbit_machine_init(void)
{
    type_register_static(&microbit_info);
}

type_init(microbit_machine_init);
