/*
 * Arm Musca-B1 test chip board emulation
 *
 * Copyright (c) 2019 Linaro Limited
 * Written by Peter Maydell
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 or
 *  (at your option) any later version.
 */

/*
 * The Musca boards are a reference implementation of a system using
 * the SSE-200 subsystem for embedded:
 * https://developer.arm.com/products/system-design/development-boards/iot-test-chips-and-boards/musca-a-test-chip-board
 * https://developer.arm.com/products/system-design/development-boards/iot-test-chips-and-boards/musca-b-test-chip-board
 * We model the A and B1 variants of this board, as described in the TRMs:
 * http://infocenter.arm.com/help/topic/com.arm.doc.101107_0000_00_en/index.html
 * http://infocenter.arm.com/help/topic/com.arm.doc.101312_0000_00_en/index.html
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qapi/error.h"
#include "exec/address-spaces.h"
#include "hw/arm/arm.h"
#include "hw/arm/armsse.h"
#include "hw/boards.h"
#include "hw/core/split-irq.h"

#define MUSCA_NUMIRQ_MAX 96

typedef enum MuscaType {
    MUSCA_A,
    MUSCA_B1,
} MuscaType;

typedef struct {
    MachineClass parent;
    MuscaType type;
    uint32_t init_svtor;
    int sram_addr_width;
    int num_irqs;
} MuscaMachineClass;

typedef struct {
    MachineState parent;

    ARMSSE sse;
    SplitIRQ cpu_irq_splitter[MUSCA_NUMIRQ_MAX];
} MuscaMachineState;

#define TYPE_MUSCA_MACHINE "musca"
#define TYPE_MUSCA_A_MACHINE MACHINE_TYPE_NAME("musca-a")
#define TYPE_MUSCA_B1_MACHINE MACHINE_TYPE_NAME("musca-b1")

#define MUSCA_MACHINE(obj) \
    OBJECT_CHECK(MuscaMachineState, obj, TYPE_MUSCA_MACHINE)
#define MUSCA_MACHINE_GET_CLASS(obj) \
    OBJECT_GET_CLASS(MuscaMachineClass, obj, TYPE_MUSCA_MACHINE)
#define MUSCA_MACHINE_CLASS(klass) \
    OBJECT_CLASS_CHECK(MuscaMachineClass, klass, TYPE_MUSCA_MACHINE)

/*
 * Main SYSCLK frequency in Hz
 * TODO this should really be different for the two cores, but we
 * don't model that in our SSE-200 model yet.
 */
#define SYSCLK_FRQ 40000000

static void musca_init(MachineState *machine)
{
    MuscaMachineState *mms = MUSCA_MACHINE(machine);
    MuscaMachineClass *mmc = MUSCA_MACHINE_GET_CLASS(mms);
    MachineClass *mc = MACHINE_GET_CLASS(machine);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *ssedev;
    int i;

    assert(mmc->num_irqs <= MUSCA_NUMIRQ_MAX);

    if (strcmp(machine->cpu_type, mc->default_cpu_type) != 0) {
        error_report("This board can only be used with CPU %s",
                     mc->default_cpu_type);
        exit(1);
    }

    sysbus_init_child_obj(OBJECT(machine), "sse-200", &mms->sse,
                          sizeof(mms->sse), TYPE_SSE200);
    ssedev = DEVICE(&mms->sse);
    object_property_set_link(OBJECT(&mms->sse), OBJECT(system_memory),
                             "memory", &error_fatal);
    qdev_prop_set_uint32(ssedev, "EXP_NUMIRQ", mmc->num_irqs);
    qdev_prop_set_uint32(ssedev, "init-svtor", mmc->init_svtor);
    qdev_prop_set_uint32(ssedev, "SRAM_ADDR_WIDTH", mmc->sram_addr_width);
    qdev_prop_set_uint32(ssedev, "MAINCLK", SYSCLK_FRQ);
    object_property_set_bool(OBJECT(&mms->sse), true, "realized",
                             &error_fatal);

    /*
     * We need to create splitters to feed the IRQ inputs
     * for each CPU in the SSE-200 from each device in the board.
     */
    for (i = 0; i < mmc->num_irqs; i++) {
        char *name = g_strdup_printf("musca-irq-splitter%d", i);
        SplitIRQ *splitter = &mms->cpu_irq_splitter[i];

        object_initialize_child(OBJECT(machine), name,
                                splitter, sizeof(*splitter),
                                TYPE_SPLIT_IRQ, &error_fatal, NULL);
        g_free(name);

        object_property_set_int(OBJECT(splitter), 2, "num-lines",
                                &error_fatal);
        object_property_set_bool(OBJECT(splitter), true, "realized",
                                 &error_fatal);
        qdev_connect_gpio_out(DEVICE(splitter), 0,
                              qdev_get_gpio_in_named(ssedev, "EXP_IRQ", i));
        qdev_connect_gpio_out(DEVICE(splitter), 1,
                              qdev_get_gpio_in_named(ssedev,
                                                     "EXP_CPU1_IRQ", i));
    }

    armv7m_load_kernel(ARM_CPU(first_cpu), machine->kernel_filename, 0x2000000);
}

static void musca_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);

    mc->default_cpus = 2;
    mc->min_cpus = mc->default_cpus;
    mc->max_cpus = mc->default_cpus;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-m33");
    mc->init = musca_init;
}

static void musca_a_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MuscaMachineClass *mmc = MUSCA_MACHINE_CLASS(oc);

    mc->desc = "ARM Musca-A board (dual Cortex-M33)";
    mmc->type = MUSCA_A;
    mmc->init_svtor = 0x10200000;
    mmc->sram_addr_width = 15;
    mmc->num_irqs = 64;
}

static void musca_b1_class_init(ObjectClass *oc, void *data)
{
    MachineClass *mc = MACHINE_CLASS(oc);
    MuscaMachineClass *mmc = MUSCA_MACHINE_CLASS(oc);

    mc->desc = "ARM Musca-B1 board (dual Cortex-M33)";
    mmc->type = MUSCA_B1;
    /*
     * This matches the DAPlink firmware which boots from QSPI. There
     * is also a firmware blob which boots from the eFlash, which
     * uses init_svtor = 0x1A000000. QEMU doesn't currently support that,
     * though we could in theory expose a machine property on the command
     * line to allow the user to request eFlash boot.
     */
    mmc->init_svtor = 0x10000000;
    mmc->sram_addr_width = 17;
    mmc->num_irqs = 96;
}

static const TypeInfo musca_info = {
    .name = TYPE_MUSCA_MACHINE,
    .parent = TYPE_MACHINE,
    .abstract = true,
    .instance_size = sizeof(MuscaMachineState),
    .class_size = sizeof(MuscaMachineClass),
    .class_init = musca_class_init,
};

static const TypeInfo musca_a_info = {
    .name = TYPE_MUSCA_A_MACHINE,
    .parent = TYPE_MUSCA_MACHINE,
    .class_init = musca_a_class_init,
};

static const TypeInfo musca_b1_info = {
    .name = TYPE_MUSCA_B1_MACHINE,
    .parent = TYPE_MUSCA_MACHINE,
    .class_init = musca_b1_class_init,
};

static void musca_machine_init(void)
{
    type_register_static(&musca_info);
    type_register_static(&musca_a_info);
    type_register_static(&musca_b1_info);
}

type_init(musca_machine_init);
