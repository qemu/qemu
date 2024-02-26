/*
 * Raspberry Pi 4B emulation
 *
 * Copyright (C) 2022 Ovchinnikov Vitalii <vitalii.ovchinnikov@auriga.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "qemu/cutils.h"
#include "qapi/error.h"
#include "qapi/visitor.h"
#include "hw/arm/raspi_platform.h"
#include "hw/display/bcm2835_fb.h"
#include "hw/registerfields.h"
#include "qemu/error-report.h"
#include "sysemu/device_tree.h"
#include "hw/boards.h"
#include "hw/loader.h"
#include "hw/arm/boot.h"
#include "qom/object.h"
#include "hw/arm/bcm2838.h"

#define TYPE_RASPI4B_MACHINE MACHINE_TYPE_NAME("raspi4b")
OBJECT_DECLARE_SIMPLE_TYPE(Raspi4bMachineState, RASPI4B_MACHINE)

struct Raspi4bMachineState {
    RaspiBaseMachineState parent_obj;
    BCM2838State soc;
};

static void raspi4b_machine_init(MachineState *machine)
{
    Raspi4bMachineState *s = RASPI4B_MACHINE(machine);
    RaspiBaseMachineState *s_base = RASPI_BASE_MACHINE(machine);
    RaspiBaseMachineClass *mc = RASPI_BASE_MACHINE_GET_CLASS(machine);
    BCM2838State *soc = &s->soc;

    s_base->binfo.board_id = mc->board_rev;

    object_initialize_child(OBJECT(machine), "soc", soc,
                            board_soc_type(mc->board_rev));

    raspi_base_machine_init(machine, &soc->parent_obj);
}

static void raspi4b_machine_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    RaspiBaseMachineClass *rmc = RASPI_BASE_MACHINE_CLASS(oc);

    rmc->board_rev = 0xb03115; /* Revision 1.5, 2 Gb RAM */
    raspi_machine_class_common_init(mc, rmc->board_rev);
    mc->init = raspi4b_machine_init;
}

static const TypeInfo raspi4b_machine_type = {
    .name           = TYPE_RASPI4B_MACHINE,
    .parent         = TYPE_RASPI_BASE_MACHINE,
    .instance_size  = sizeof(Raspi4bMachineState),
    .class_init     = raspi4b_machine_class_init,
};

static void raspi4b_machine_register_type(void)
{
    type_register_static(&raspi4b_machine_type);
}

type_init(raspi4b_machine_register_type)
