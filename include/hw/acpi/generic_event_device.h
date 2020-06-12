/*
 *
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Huawei Technologies R & D (UK) Ltd
 * Written by Samuel Ortiz, Shameer Kolothum
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 *
 * The ACPI Generic Event Device (GED) is a hardware-reduced specific
 * device[ACPI v6.1 Section 5.6.9] that handles all platform events,
 * including the hotplug ones. Generic Event Device allows platforms
 * to handle interrupts in ACPI ASL statements. It follows a very
 * similar approach like the _EVT method from GPIO events. All
 * interrupts are listed in  _CRS and the handler is written in _EVT
 * method. Here, we use a single interrupt for the GED device, relying
 * on IO memory region to communicate the type of device affected by
 * the interrupt. This way, we can support up to 32 events with a
 * unique interrupt.
 *
 * Here is an example.
 *
 * Device (\_SB.GED)
 * {
 *     Name (_HID, "ACPI0013")
 *     Name (_UID, Zero)
 *     Name (_CRS, ResourceTemplate ()
 *     {
 *         Interrupt (ResourceConsumer, Edge, ActiveHigh, Exclusive, ,, )
 *         {
 *              0x00000029,
 *         }
 *     })
 *     OperationRegion (EREG, SystemMemory, 0x09080000, 0x04)
 *     Field (EREG, DWordAcc, NoLock, WriteAsZeros)
 *     {
 *         ESEL,   32
 *     }
 *
 *     Method (_EVT, 1, Serialized)  // _EVT: Event
 *     {
 *         Local0 = ESEL // ESEL = IO memory region which specifies the
 *                       // device type.
 *         If (((Local0 & One) == One))
 *         {
 *             MethodEvent1()
 *         }
 *         If ((Local0 & 0x2) == 0x2)
 *         {
 *             MethodEvent2()
 *         }
 *         ...
 *     }
 * }
 *
 */

#ifndef HW_ACPI_GED_H
#define HW_ACPI_GED_H

#include "hw/sysbus.h"
#include "hw/acpi/memory_hotplug.h"
#include "hw/acpi/ghes.h"

#define ACPI_POWER_BUTTON_DEVICE "PWRB"

#define TYPE_ACPI_GED "acpi-ged"
#define ACPI_GED(obj) \
    OBJECT_CHECK(AcpiGedState, (obj), TYPE_ACPI_GED)

#define ACPI_GED_EVT_SEL_OFFSET    0x0
#define ACPI_GED_EVT_SEL_LEN       0x4

#define GED_DEVICE      "GED"
#define AML_GED_EVT_REG "EREG"
#define AML_GED_EVT_SEL "ESEL"

/*
 * Platforms need to specify the GED event bitmap
 * to describe what kind of events they want to support
 * through GED.
 */
#define ACPI_GED_MEM_HOTPLUG_EVT   0x1
#define ACPI_GED_PWR_DOWN_EVT      0x2
#define ACPI_GED_NVDIMM_HOTPLUG_EVT 0x4

typedef struct GEDState {
    MemoryRegion evt;
    uint32_t     sel;
} GEDState;

typedef struct AcpiGedState {
    SysBusDevice parent_obj;
    MemHotplugState memhp_state;
    MemoryRegion container_memhp;
    GEDState ged_state;
    uint32_t ged_event_bitmap;
    qemu_irq irq;
    AcpiGhesState ghes_state;
} AcpiGedState;

void build_ged_aml(Aml *table, const char* name, HotplugHandler *hotplug_dev,
                   uint32_t ged_irq, AmlRegionSpace rs, hwaddr ged_base);

#endif
