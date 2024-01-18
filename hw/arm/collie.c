/*
 * SA-1110-based Sharp Zaurus SL-5500 platform.
 *
 * Copyright (C) 2011 Dmitry Eremin-Solenikov
 *
 * This code is licensed under GNU GPL v2.
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "hw/sysbus.h"
#include "hw/boards.h"
#include "strongarm.h"
#include "hw/arm/boot.h"
#include "hw/block/flash.h"
#include "exec/address-spaces.h"
#include "qom/object.h"
#include "qemu/error-report.h"


#define RAM_SIZE            (512 * MiB)
#define FLASH_SIZE          (32 * MiB)
#define FLASH_SECTOR_SIZE   (64 * KiB)

struct CollieMachineState {
    MachineState parent;

    StrongARMState *sa1110;
};

#define TYPE_COLLIE_MACHINE MACHINE_TYPE_NAME("collie")
OBJECT_DECLARE_SIMPLE_TYPE(CollieMachineState, COLLIE_MACHINE)

static struct arm_boot_info collie_binfo = {
    .loader_start = SA_SDCS0,
    .ram_size = RAM_SIZE,
};

static void collie_init(MachineState *machine)
{
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    CollieMachineState *cms = COLLIE_MACHINE(machine);

    if (machine->ram_size != mc->default_ram_size) {
        char *sz = size_to_str(mc->default_ram_size);
        error_report("Invalid RAM size, should be %s", sz);
        g_free(sz);
        exit(EXIT_FAILURE);
    }

    cms->sa1110 = sa1110_init(machine->cpu_type);

    memory_region_add_subregion(get_system_memory(), SA_SDCS0, machine->ram);

    for (unsigned i = 0; i < 2; i++) {
        DriveInfo *dinfo = drive_get(IF_PFLASH, 0, i);
        pflash_cfi01_register(i ? SA_CS1 : SA_CS0,
                              i ? "collie.fl2" : "collie.fl1", FLASH_SIZE,
                              dinfo ? blk_by_legacy_dinfo(dinfo) : NULL,
                              FLASH_SECTOR_SIZE, 4, 0x00, 0x00, 0x00, 0x00, 0);
    }

    sysbus_create_simple("scoop", 0x40800000, NULL);

    collie_binfo.board_id = 0x208;
    arm_load_kernel(cms->sa1110->cpu, machine, &collie_binfo);
}

static void collie_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->desc = "Sharp SL-5500 (Collie) PDA (SA-1110)";
    mc->init = collie_init;
    mc->ignore_memory_transaction_failures = true;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("sa1110");
    mc->default_ram_size = RAM_SIZE;
    mc->default_ram_id = "strongarm.sdram";
}

static const TypeInfo collie_machine_typeinfo = {
    .name = TYPE_COLLIE_MACHINE,
    .parent = TYPE_MACHINE,
    .class_init = collie_machine_class_init,
    .instance_size = sizeof(CollieMachineState),
};

static void collie_machine_register_types(void)
{
    type_register_static(&collie_machine_typeinfo);
}
type_init(collie_machine_register_types);
