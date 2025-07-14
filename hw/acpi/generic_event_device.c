/*
 *
 * Copyright (c) 2018 Intel Corporation
 * Copyright (c) 2019 Huawei Technologies R & D (UK) Ltd
 * Written by Samuel Ortiz, Shameer Kolothum
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2 or later, as published by the Free Software Foundation.
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/acpi/acpi.h"
#include "hw/acpi/pcihp.h"
#include "hw/acpi/generic_event_device.h"
#include "hw/pci/pci.h"
#include "hw/irq.h"
#include "hw/mem/pc-dimm.h"
#include "hw/mem/nvdimm.h"
#include "hw/pci/pci_device.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "qemu/error-report.h"
#include "system/runstate.h"

static const uint32_t ged_supported_events[] = {
    ACPI_GED_MEM_HOTPLUG_EVT,
    ACPI_GED_PWR_DOWN_EVT,
    ACPI_GED_NVDIMM_HOTPLUG_EVT,
    ACPI_GED_CPU_HOTPLUG_EVT,
    ACPI_GED_PCI_HOTPLUG_EVT,
};

/*
 * The ACPI Generic Event Device (GED) is a hardware-reduced specific
 * device[ACPI v6.1 Section 5.6.9] that handles all platform events,
 * including the hotplug ones. Platforms need to specify their own
 * GED Event bitmap to describe what kind of events they want to support
 * through GED. This routine uses a single interrupt for the GED device,
 * relying on IO memory region to communicate the type of device
 * affected by the interrupt. This way, we can support up to 32 events
 * with a unique interrupt.
 */
void build_ged_aml(Aml *table, const char *name, HotplugHandler *hotplug_dev,
                   uint32_t ged_irq, AmlRegionSpace rs, hwaddr ged_base)
{
    AcpiGedState *s = ACPI_GED(hotplug_dev);
    Aml *crs = aml_resource_template();
    Aml *evt, *field;
    Aml *dev = aml_device("%s", name);
    Aml *evt_sel = aml_local(0);
    Aml *esel = aml_name(AML_GED_EVT_SEL);

    /* _CRS interrupt */
    aml_append(crs, aml_interrupt(AML_CONSUMER, AML_EDGE, AML_ACTIVE_HIGH,
                                  AML_EXCLUSIVE, &ged_irq, 1));

    aml_append(dev, aml_name_decl("_HID", aml_string("ACPI0013")));
    aml_append(dev, aml_name_decl("_UID", aml_string(GED_DEVICE)));
    aml_append(dev, aml_name_decl("_CRS", crs));

    /* Append IO region */
    aml_append(dev, aml_operation_region(AML_GED_EVT_REG, rs,
               aml_int(ged_base + ACPI_GED_EVT_SEL_OFFSET),
               ACPI_GED_EVT_SEL_LEN));
    field = aml_field(AML_GED_EVT_REG, AML_DWORD_ACC, AML_NOLOCK,
                      AML_WRITE_AS_ZEROS);
    aml_append(field, aml_named_field(AML_GED_EVT_SEL,
                                      ACPI_GED_EVT_SEL_LEN * BITS_PER_BYTE));
    aml_append(dev, field);

    /*
     * For each GED event we:
     * - Add a conditional block for each event, inside a loop.
     * - Call a method for each supported GED event type.
     *
     * The resulting ASL code looks like:
     *
     * Local0 = ESEL
     * If ((Local0 & One) == One)
     * {
     *     MethodEvent0()
     * }
     *
     * If ((Local0 & 0x2) == 0x2)
     * {
     *     MethodEvent1()
     * }
     * ...
     */
    evt = aml_method("_EVT", 1, AML_SERIALIZED);
    {
        Aml *if_ctx;
        uint32_t i;
        uint32_t ged_events = ctpop32(s->ged_event_bitmap);

        /* Local0 = ESEL */
        aml_append(evt, aml_store(esel, evt_sel));

        for (i = 0; i < ARRAY_SIZE(ged_supported_events) && ged_events; i++) {
            uint32_t event = s->ged_event_bitmap & ged_supported_events[i];

            if (!event) {
                continue;
            }

            if_ctx = aml_if(aml_equal(aml_and(evt_sel, aml_int(event), NULL),
                                      aml_int(event)));
            switch (event) {
            case ACPI_GED_MEM_HOTPLUG_EVT:
                aml_append(if_ctx, aml_call0(MEMORY_DEVICES_CONTAINER "."
                                             MEMORY_SLOT_SCAN_METHOD));
                break;
            case ACPI_GED_CPU_HOTPLUG_EVT:
                aml_append(if_ctx, aml_call0(AML_GED_EVT_CPU_SCAN_METHOD));
                break;
            case ACPI_GED_PWR_DOWN_EVT:
                aml_append(if_ctx,
                           aml_notify(aml_name(ACPI_POWER_BUTTON_DEVICE),
                                      aml_int(0x80)));
                break;
            case ACPI_GED_NVDIMM_HOTPLUG_EVT:
                aml_append(if_ctx,
                           aml_notify(aml_name("\\_SB.NVDR"),
                                      aml_int(0x80)));
                break;
            case ACPI_GED_PCI_HOTPLUG_EVT:
                aml_append(if_ctx,
                           aml_acquire(aml_name("\\_SB.PCI0.BLCK"), 0xFFFF));
                aml_append(if_ctx, aml_call0("\\_SB.PCI0.PCNT"));
                aml_append(if_ctx, aml_release(aml_name("\\_SB.PCI0.BLCK")));
                break;
            default:
                /*
                 * Please make sure all the events in ged_supported_events[]
                 * are handled above.
                 */
                g_assert_not_reached();
            }

            aml_append(evt, if_ctx);
            ged_events--;
        }

        if (ged_events) {
            error_report("Unsupported events specified");
            abort();
        }
    }

    /* Append _EVT method */
    aml_append(dev, evt);

    aml_append(table, dev);
}

void acpi_dsdt_add_power_button(Aml *scope)
{
    Aml *dev = aml_device(ACPI_POWER_BUTTON_DEVICE);
    aml_append(dev, aml_name_decl("_HID", aml_string("PNP0C0C")));
    aml_append(dev, aml_name_decl("_UID", aml_int(0)));
    aml_append(scope, dev);
}

/* Memory read by the GED _EVT AML dynamic method */
static uint64_t ged_evt_read(void *opaque, hwaddr addr, unsigned size)
{
    uint64_t val = 0;
    GEDState *ged_st = opaque;

    switch (addr) {
    case ACPI_GED_EVT_SEL_OFFSET:
        /* Read the selector value and reset it */
        val = ged_st->sel;
        ged_st->sel = 0;
        break;
    default:
        break;
    }

    return val;
}

/* Nothing is expected to be written to the GED memory region */
static void ged_evt_write(void *opaque, hwaddr addr, uint64_t data,
                          unsigned int size)
{
}

static const MemoryRegionOps ged_evt_ops = {
    .read = ged_evt_read,
    .write = ged_evt_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 4,
        .max_access_size = 4,
    },
};

static uint64_t ged_regs_read(void *opaque, hwaddr addr, unsigned size)
{
    return 0;
}

static void ged_regs_write(void *opaque, hwaddr addr, uint64_t data,
                           unsigned int size)
{
    bool slp_en;
    int slp_typ;

    switch (addr) {
    case ACPI_GED_REG_SLEEP_CTL:
        slp_typ = (data >> ACPI_GED_SLP_TYP_POS) & ACPI_GED_SLP_TYP_MASK;
        slp_en  = !!(data & ACPI_GED_SLP_EN);
        if (slp_en && slp_typ == ACPI_GED_SLP_TYP_S5) {
            qemu_system_shutdown_request(SHUTDOWN_CAUSE_GUEST_SHUTDOWN);
        }
        return;
    case ACPI_GED_REG_SLEEP_STS:
        return;
    case ACPI_GED_REG_RESET:
        if (data == ACPI_GED_RESET_VALUE) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        return;
    }
}

static const MemoryRegionOps ged_regs_ops = {
    .read = ged_regs_read,
    .write = ged_regs_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 1,
    },
};

static void acpi_ged_device_pre_plug_cb(HotplugHandler *hotplug_dev,
                                        DeviceState *dev, Error **errp)
{
    if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_pre_plug_cb(hotplug_dev, dev, errp);
    }
}

static void acpi_ged_device_plug_cb(HotplugHandler *hotplug_dev,
                                    DeviceState *dev, Error **errp)
{
    AcpiGedState *s = ACPI_GED(hotplug_dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        if (object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM)) {
            nvdimm_acpi_plug_cb(hotplug_dev, dev);
        } else {
            acpi_memory_plug_cb(hotplug_dev, &s->memhp_state, dev, errp);
        }
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        acpi_cpu_plug_cb(hotplug_dev, &s->cpuhp_state, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_plug_cb(hotplug_dev, &s->pcihp_state, dev, errp);
    } else {
        error_setg(errp, "virt: device plug request for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void acpi_ged_unplug_request_cb(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp)
{
    AcpiGedState *s = ACPI_GED(hotplug_dev);

    if ((object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM) &&
                       !(object_dynamic_cast(OBJECT(dev), TYPE_NVDIMM)))) {
        acpi_memory_unplug_request_cb(hotplug_dev, &s->memhp_state, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        acpi_cpu_unplug_request_cb(hotplug_dev, &s->cpuhp_state, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_unplug_request_cb(hotplug_dev, &s->pcihp_state,
                                            dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug request for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void acpi_ged_unplug_cb(HotplugHandler *hotplug_dev,
                               DeviceState *dev, Error **errp)
{
    AcpiGedState *s = ACPI_GED(hotplug_dev);

    if (object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        acpi_memory_unplug_cb(&s->memhp_state, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_CPU)) {
        acpi_cpu_unplug_cb(&s->cpuhp_state, dev, errp);
    } else if (object_dynamic_cast(OBJECT(dev), TYPE_PCI_DEVICE)) {
        acpi_pcihp_device_unplug_cb(hotplug_dev, &s->pcihp_state, dev, errp);
    } else {
        error_setg(errp, "acpi: device unplug for unsupported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}

static void acpi_ged_ospm_status(AcpiDeviceIf *adev, ACPIOSTInfoList ***list)
{
    AcpiGedState *s = ACPI_GED(adev);

    acpi_memory_ospm_status(&s->memhp_state, list);
    acpi_cpu_ospm_status(&s->cpuhp_state, list);
}

static void acpi_ged_send_event(AcpiDeviceIf *adev, AcpiEventStatusBits ev)
{
    AcpiGedState *s = ACPI_GED(adev);
    GEDState *ged_st = &s->ged_state;
    uint32_t sel;

    if (ev & ACPI_MEMORY_HOTPLUG_STATUS) {
        sel = ACPI_GED_MEM_HOTPLUG_EVT;
    } else if (ev & ACPI_POWER_DOWN_STATUS) {
        sel = ACPI_GED_PWR_DOWN_EVT;
    } else if (ev & ACPI_NVDIMM_HOTPLUG_STATUS) {
        sel = ACPI_GED_NVDIMM_HOTPLUG_EVT;
    } else if (ev & ACPI_CPU_HOTPLUG_STATUS) {
        sel = ACPI_GED_CPU_HOTPLUG_EVT;
    } else if (ev & ACPI_PCI_HOTPLUG_STATUS) {
        sel = ACPI_GED_PCI_HOTPLUG_EVT;
    } else {
        /* Unknown event. Return without generating interrupt. */
        warn_report("GED: Unsupported event %d. No irq injected", ev);
        return;
    }

    /*
     * Set the GED selector field to communicate the event type.
     * This will be read by GED aml code to select the appropriate
     * event method.
     */
    ged_st->sel |= sel;

    /* Trigger the event by sending an interrupt to the guest. */
    qemu_irq_pulse(s->irq);
}

static const Property acpi_ged_properties[] = {
    DEFINE_PROP_UINT32("ged-event", AcpiGedState, ged_event_bitmap, 0),
    DEFINE_PROP_BOOL(ACPI_PM_PROP_ACPI_PCIHP_BRIDGE, AcpiGedState,
                     pcihp_state.use_acpi_hotplug_bridge, 0),
    DEFINE_PROP_LINK("bus", AcpiGedState, pcihp_state.root,
                     TYPE_PCI_BUS, PCIBus *),
};

static const VMStateDescription vmstate_memhp_state = {
    .name = "acpi-ged/memhp",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_MEMORY_HOTPLUG(memhp_state, AcpiGedState),
        VMSTATE_END_OF_LIST()
    }
};

static bool cpuhp_needed(void *opaque)
{
    MachineClass *mc = MACHINE_GET_CLASS(qdev_get_machine());

    return mc->has_hotpluggable_cpus;
}

static const VMStateDescription vmstate_cpuhp_state = {
    .name = "acpi-ged/cpuhp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = cpuhp_needed,
    .fields      = (VMStateField[]) {
        VMSTATE_CPU_HOTPLUG(cpuhp_state, AcpiGedState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ged_state = {
    .name = "acpi-ged-state",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(sel, GEDState),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_ghes = {
    .name = "acpi-ghes",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT64(hw_error_le, AcpiGhesState),
        VMSTATE_END_OF_LIST()
    },
};

static bool ghes_needed(void *opaque)
{
    AcpiGedState *s = opaque;
    return s->ghes_state.hw_error_le;
}

static const VMStateDescription vmstate_ghes_state = {
    .name = "acpi-ged/ghes",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = ghes_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ghes_state, AcpiGedState, 1,
                       vmstate_ghes, AcpiGhesState),
        VMSTATE_END_OF_LIST()
    }
};

static bool pcihp_needed(void *opaque)
{
    AcpiGedState *s = opaque;
    return s->pcihp_state.use_acpi_hotplug_bridge;
}

static const VMStateDescription vmstate_pcihp_state = {
    .name = "acpi-ged/pcihp",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = pcihp_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_HOTPLUG(pcihp_state,
                            AcpiGedState,
                            NULL, NULL),
        VMSTATE_END_OF_LIST()
    }
};

static const VMStateDescription vmstate_acpi_ged = {
    .name = "acpi-ged",
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_STRUCT(ged_state, AcpiGedState, 1, vmstate_ged_state, GEDState),
        VMSTATE_END_OF_LIST(),
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_memhp_state,
        &vmstate_cpuhp_state,
        &vmstate_ghes_state,
        &vmstate_pcihp_state,
        NULL
    }
};

static void acpi_ged_realize(DeviceState *dev, Error **errp)
{
    SysBusDevice *sbd = SYS_BUS_DEVICE(dev);
    AcpiGedState *s = ACPI_GED(dev);
    AcpiPciHpState *pcihp_state = &s->pcihp_state;
    uint32_t ged_events;
    int i;

    if (pcihp_state->use_acpi_hotplug_bridge) {
        s->ged_event_bitmap |= ACPI_GED_PCI_HOTPLUG_EVT;
    }
    ged_events = ctpop32(s->ged_event_bitmap);

    for (i = 0; i < ARRAY_SIZE(ged_supported_events) && ged_events; i++) {
        uint32_t event = s->ged_event_bitmap & ged_supported_events[i];

        if (!event) {
            continue;
        }

        switch (event) {
        case ACPI_GED_CPU_HOTPLUG_EVT:
            /* initialize CPU Hotplug related regions */
            memory_region_init(&s->container_cpuhp, OBJECT(dev),
                                "cpuhp container",
                                ACPI_CPU_HOTPLUG_REG_LEN);
            sysbus_init_mmio(sbd, &s->container_cpuhp);
            cpu_hotplug_hw_init(&s->container_cpuhp, OBJECT(dev),
                                &s->cpuhp_state, 0);
            break;
        case ACPI_GED_PCI_HOTPLUG_EVT:
            memory_region_init(&s->container_pcihp, OBJECT(dev),
                               ACPI_PCIHP_REGION_NAME, ACPI_PCIHP_SIZE);
            sysbus_init_mmio(sbd, &s->container_pcihp);
            acpi_pcihp_init(OBJECT(s), &s->pcihp_state,
                            &s->container_pcihp, 0);
            qbus_set_hotplug_handler(BUS(s->pcihp_state.root), OBJECT(dev));
        }
        ged_events--;
    }

    if (ged_events) {
        error_report("Unsupported events specified");
        abort();
    }
}

static void acpi_ged_initfn(Object *obj)
{
    DeviceState *dev = DEVICE(obj);
    AcpiGedState *s = ACPI_GED(dev);
    SysBusDevice *sbd = SYS_BUS_DEVICE(obj);
    GEDState *ged_st = &s->ged_state;

    memory_region_init_io(&ged_st->evt, obj, &ged_evt_ops, ged_st,
                          TYPE_ACPI_GED, ACPI_GED_EVT_SEL_LEN);
    sysbus_init_mmio(sbd, &ged_st->evt);

    sysbus_init_irq(sbd, &s->irq);

    s->memhp_state.is_enabled = true;
    /*
     * GED handles memory hotplug event and acpi-mem-hotplug
     * memory region gets initialized here. Create an exclusive
     * container for memory hotplug IO and expose it as GED sysbus
     * MMIO so that boards can map it separately.
     */
    memory_region_init(&s->container_memhp, OBJECT(dev), "memhp container",
                       MEMORY_HOTPLUG_IO_LEN);
    sysbus_init_mmio(sbd, &s->container_memhp);
    acpi_memory_hotplug_init(&s->container_memhp, OBJECT(dev),
                             &s->memhp_state, 0);

    memory_region_init_io(&ged_st->regs, obj, &ged_regs_ops, ged_st,
                          TYPE_ACPI_GED "-regs", ACPI_GED_REG_COUNT);
    sysbus_init_mmio(sbd, &ged_st->regs);
}

static void ged_reset_hold(Object *obj, ResetType type)
{
    AcpiGedState *s = ACPI_GED(obj);

    if (s->pcihp_state.use_acpi_hotplug_bridge) {
        acpi_pcihp_reset(&s->pcihp_state);
    }
}

static void acpi_ged_class_init(ObjectClass *class, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(class);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(class);
    AcpiDeviceIfClass *adevc = ACPI_DEVICE_IF_CLASS(class);
    ResettableClass *rc = RESETTABLE_CLASS(class);
    AcpiGedClass *gedc = ACPI_GED_CLASS(class);

    dc->desc = "ACPI Generic Event Device";
    device_class_set_props(dc, acpi_ged_properties);
    dc->vmsd = &vmstate_acpi_ged;
    dc->realize = acpi_ged_realize;

    hc->pre_plug = acpi_ged_device_pre_plug_cb;
    hc->plug = acpi_ged_device_plug_cb;
    hc->unplug_request = acpi_ged_unplug_request_cb;
    hc->unplug = acpi_ged_unplug_cb;
    resettable_class_set_parent_phases(rc, NULL, ged_reset_hold, NULL,
                                       &gedc->parent_phases);

    adevc->ospm_status = acpi_ged_ospm_status;
    adevc->send_event = acpi_ged_send_event;
}

static const TypeInfo acpi_ged_info = {
    .name          = TYPE_ACPI_GED,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(AcpiGedState),
    .instance_init  = acpi_ged_initfn,
    .class_init    = acpi_ged_class_init,
    .class_size    = sizeof(AcpiGedClass),
    .interfaces = (const InterfaceInfo[]) {
        { TYPE_HOTPLUG_HANDLER },
        { TYPE_ACPI_DEVICE_IF },
        { }
    }
};

static void acpi_ged_register_types(void)
{
    type_register_static(&acpi_ged_info);
}

type_init(acpi_ged_register_types)
