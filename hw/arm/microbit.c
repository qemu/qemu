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
#include "hw/arm/arm.h"
#include "sysemu/sysemu.h"
#include "exec/address-spaces.h"

#include "hw/arm/nrf51_soc.h"

typedef struct {
    MachineState parent;

    NRF51State nrf51;
} MicrobitMachineState;

#define TYPE_MICROBIT_MACHINE MACHINE_TYPE_NAME("microbit")

#define MICROBIT_MACHINE(obj) \
    OBJECT_CHECK(MicrobitMachineState, obj, TYPE_MICROBIT_MACHINE)

static void microbit_init(MachineState *machine)
{
    MicrobitMachineState *s = MICROBIT_MACHINE(machine);
    MemoryRegion *system_memory = get_system_memory();
    Object *soc = OBJECT(&s->nrf51);

    sysbus_init_child_obj(OBJECT(machine), "nrf51", soc, sizeof(s->nrf51),
                          TYPE_NRF51_SOC);
    qdev_prop_set_chr(DEVICE(&s->nrf51), "serial0", serial_hd(0));
    object_property_set_link(soc, OBJECT(system_memory), "memory",
                             &error_fatal);
    object_property_set_bool(soc, true, "realized", &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename,
                       NRF51_SOC(soc)->flash_size);
}

static void microbit_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "BBC micro:bit";
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
