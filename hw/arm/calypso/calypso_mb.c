#include "qemu/osdep.h"
#include "hw/boards.h"
#include "qemu/module.h"
#include "target/arm/cpu.h"
#include "hw/sysbus.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "hw/qdev-properties.h"

#include "hw/arm/calypso/calypso_soc.h"

#define TYPE_CALYPSO_MB MACHINE_TYPE_NAME("calypso-mb")

OBJECT_DECLARE_SIMPLE_TYPE(CalypsoMBState, CALYPSO_MB)

typedef struct CalypsoMBState {
    MachineState parent;
    ARMCPU *cpu;
    DeviceState *soc;
} CalypsoMBState;

static void calypso_mb_init(MachineState *machine)
{
    CalypsoMBState *s = CALYPSO_MB(machine);

    /* ---- CPU ---- */
    Object *cpuobj = object_new(machine->cpu_type);
    s->cpu = ARM_CPU(cpuobj);
    qdev_realize(DEVICE(cpuobj), NULL, &error_abort);

    /* ---- SoC ---- */
    DeviceState *soc = qdev_new(TYPE_CALYPSO_SOC);
    SysBusDevice *sbd = SYS_BUS_DEVICE(soc);

    /* propriété exposée par calypso_soc.c */
    qdev_prop_set_string(soc, "socket-path", "/tmp/calypso-socket");

    sysbus_realize_and_unref(sbd, &error_abort);
    sysbus_mmio_map(sbd, 0, 0x00000000);

    s->soc = soc;
}

static void calypso_mb_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Calypso Motherboard (CPU + Calypso SoC)";
    mc->init = calypso_mb_init;
    mc->max_cpus = 1;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm946");
}

static const TypeInfo calypso_mb_info = {
    .name          = TYPE_CALYPSO_MB,
    .parent        = TYPE_MACHINE,
    .instance_size = sizeof(CalypsoMBState),
    .class_init    = calypso_mb_class_init,
};

static void calypso_mb_register_types(void)
{
    type_register_static(&calypso_mb_info);
}

type_init(calypso_mb_register_types);

