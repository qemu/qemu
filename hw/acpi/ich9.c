/*
 * ACPI implementation
 *
 * Copyright (c) 2006 Fabrice Bellard
 * Copyright (c) 2009 Isaku Yamahata <yamahata at valinux co jp>
 *                    VA Linux Systems Japan K.K.
 * Copyright (C) 2012 Jason Baron <jbaron@redhat.com>
 *
 * This is based on acpi.c.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>
 *
 * Contributions after 2012-01-13 are licensed under the terms of the
 * GNU GPL, version 2 or (at your option) any later version.
 */
#include "hw/hw.h"
#include "qapi/visitor.h"
#include "hw/i386/pc.h"
#include "hw/pci/pci.h"
#include "qemu/timer.h"
#include "sysemu/sysemu.h"
#include "hw/acpi/acpi.h"
#include "sysemu/kvm.h"
#include "exec/address-spaces.h"

#include "hw/i386/ich9.h"
#include "hw/mem/pc-dimm.h"

//#define DEBUG

#ifdef DEBUG
#define ICH9_DEBUG(fmt, ...) \
do { printf("%s "fmt, __func__, ## __VA_ARGS__); } while (0)
#else
#define ICH9_DEBUG(fmt, ...)    do { } while (0)
#endif

static void ich9_pm_update_sci_fn(ACPIREGS *regs)
{
    ICH9LPCPMRegs *pm = container_of(regs, ICH9LPCPMRegs, acpi_regs);
    acpi_update_sci(&pm->acpi_regs, pm->irq);
}

static uint64_t ich9_gpe_readb(void *opaque, hwaddr addr, unsigned width)
{
    ICH9LPCPMRegs *pm = opaque;
    return acpi_gpe_ioport_readb(&pm->acpi_regs, addr);
}

static void ich9_gpe_writeb(void *opaque, hwaddr addr, uint64_t val,
                            unsigned width)
{
    ICH9LPCPMRegs *pm = opaque;
    acpi_gpe_ioport_writeb(&pm->acpi_regs, addr, val);
    acpi_update_sci(&pm->acpi_regs, pm->irq);
}

static const MemoryRegionOps ich9_gpe_ops = {
    .read = ich9_gpe_readb,
    .write = ich9_gpe_writeb,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 1,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static uint64_t ich9_smi_readl(void *opaque, hwaddr addr, unsigned width)
{
    ICH9LPCPMRegs *pm = opaque;
    switch (addr) {
    case 0:
        return pm->smi_en;
    case 4:
        return pm->smi_sts;
    default:
        return 0;
    }
}

static void ich9_smi_writel(void *opaque, hwaddr addr, uint64_t val,
                            unsigned width)
{
    ICH9LPCPMRegs *pm = opaque;
    switch (addr) {
    case 0:
        pm->smi_en = val;
        break;
    }
}

static const MemoryRegionOps ich9_smi_ops = {
    .read = ich9_smi_readl,
    .write = ich9_smi_writel,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

void ich9_pm_iospace_update(ICH9LPCPMRegs *pm, uint32_t pm_io_base)
{
    ICH9_DEBUG("to 0x%x\n", pm_io_base);

    assert((pm_io_base & ICH9_PMIO_MASK) == 0);

    pm->pm_io_base = pm_io_base;
    memory_region_transaction_begin();
    memory_region_set_enabled(&pm->io, pm->pm_io_base != 0);
    memory_region_set_address(&pm->io, pm->pm_io_base);
    memory_region_transaction_commit();
}

static int ich9_pm_post_load(void *opaque, int version_id)
{
    ICH9LPCPMRegs *pm = opaque;
    uint32_t pm_io_base = pm->pm_io_base;
    pm->pm_io_base = 0;
    ich9_pm_iospace_update(pm, pm_io_base);
    return 0;
}

#define VMSTATE_GPE_ARRAY(_field, _state)                            \
 {                                                                   \
     .name       = (stringify(_field)),                              \
     .version_id = 0,                                                \
     .num        = ICH9_PMIO_GPE0_LEN,                               \
     .info       = &vmstate_info_uint8,                              \
     .size       = sizeof(uint8_t),                                  \
     .flags      = VMS_ARRAY | VMS_POINTER,                          \
     .offset     = vmstate_offset_pointer(_state, _field, uint8_t),  \
 }

const VMStateDescription vmstate_ich9_pm = {
    .name = "ich9_pm",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ich9_pm_post_load,
    .fields = (VMStateField[]) {
        VMSTATE_UINT16(acpi_regs.pm1.evt.sts, ICH9LPCPMRegs),
        VMSTATE_UINT16(acpi_regs.pm1.evt.en, ICH9LPCPMRegs),
        VMSTATE_UINT16(acpi_regs.pm1.cnt.cnt, ICH9LPCPMRegs),
        VMSTATE_TIMER(acpi_regs.tmr.timer, ICH9LPCPMRegs),
        VMSTATE_INT64(acpi_regs.tmr.overflow_time, ICH9LPCPMRegs),
        VMSTATE_GPE_ARRAY(acpi_regs.gpe.sts, ICH9LPCPMRegs),
        VMSTATE_GPE_ARRAY(acpi_regs.gpe.en, ICH9LPCPMRegs),
        VMSTATE_UINT32(smi_en, ICH9LPCPMRegs),
        VMSTATE_UINT32(smi_sts, ICH9LPCPMRegs),
        VMSTATE_END_OF_LIST()
    }
};

static void pm_reset(void *opaque)
{
    ICH9LPCPMRegs *pm = opaque;
    ich9_pm_iospace_update(pm, 0);

    acpi_pm1_evt_reset(&pm->acpi_regs);
    acpi_pm1_cnt_reset(&pm->acpi_regs);
    acpi_pm_tmr_reset(&pm->acpi_regs);
    acpi_gpe_reset(&pm->acpi_regs);

    if (kvm_enabled()) {
        /* Mark SMM as already inited to prevent SMM from running. KVM does not
         * support SMM mode. */
        pm->smi_en |= ICH9_PMIO_SMI_EN_APMC_EN;
    }

    acpi_update_sci(&pm->acpi_regs, pm->irq);
}

static void pm_powerdown_req(Notifier *n, void *opaque)
{
    ICH9LPCPMRegs *pm = container_of(n, ICH9LPCPMRegs, powerdown_notifier);

    acpi_pm1_evt_power_down(&pm->acpi_regs);
}

static void ich9_cpu_added_req(Notifier *n, void *opaque)
{
    ICH9LPCPMRegs *pm = container_of(n, ICH9LPCPMRegs, cpu_added_notifier);

    assert(pm != NULL);
    AcpiCpuHotplug_add(&pm->acpi_regs.gpe, &pm->gpe_cpu, CPU(opaque));
    acpi_update_sci(&pm->acpi_regs, pm->irq);
}

void ich9_pm_init(PCIDevice *lpc_pci, ICH9LPCPMRegs *pm,
                  qemu_irq sci_irq)
{
    memory_region_init(&pm->io, OBJECT(lpc_pci), "ich9-pm", ICH9_PMIO_SIZE);
    memory_region_set_enabled(&pm->io, false);
    memory_region_add_subregion(pci_address_space_io(lpc_pci),
                                0, &pm->io);

    acpi_pm_tmr_init(&pm->acpi_regs, ich9_pm_update_sci_fn, &pm->io);
    acpi_pm1_evt_init(&pm->acpi_regs, ich9_pm_update_sci_fn, &pm->io);
    acpi_pm1_cnt_init(&pm->acpi_regs, &pm->io, 2);

    acpi_gpe_init(&pm->acpi_regs, ICH9_PMIO_GPE0_LEN);
    memory_region_init_io(&pm->io_gpe, OBJECT(lpc_pci), &ich9_gpe_ops, pm,
                          "apci-gpe0", ICH9_PMIO_GPE0_LEN);
    memory_region_add_subregion(&pm->io, ICH9_PMIO_GPE0_STS, &pm->io_gpe);

    memory_region_init_io(&pm->io_smi, OBJECT(lpc_pci), &ich9_smi_ops, pm,
                          "apci-smi", 8);
    memory_region_add_subregion(&pm->io, ICH9_PMIO_SMI_EN, &pm->io_smi);

    pm->irq = sci_irq;
    qemu_register_reset(pm_reset, pm);
    pm->powerdown_notifier.notify = pm_powerdown_req;
    qemu_register_powerdown_notifier(&pm->powerdown_notifier);

    AcpiCpuHotplug_init(pci_address_space_io(lpc_pci), OBJECT(lpc_pci),
                        &pm->gpe_cpu, ICH9_CPU_HOTPLUG_IO_BASE);
    pm->cpu_added_notifier.notify = ich9_cpu_added_req;
    qemu_register_cpu_added_notifier(&pm->cpu_added_notifier);

    if (pm->acpi_memory_hotplug.is_enabled) {
        acpi_memory_hotplug_init(pci_address_space_io(lpc_pci), OBJECT(lpc_pci),
                                 &pm->acpi_memory_hotplug);
    }
}

static void ich9_pm_get_gpe0_blk(Object *obj, Visitor *v,
                                 void *opaque, const char *name,
                                 Error **errp)
{
    ICH9LPCPMRegs *pm = opaque;
    uint32_t value = pm->pm_io_base + ICH9_PMIO_GPE0_STS;

    visit_type_uint32(v, &value, name, errp);
}

static bool ich9_pm_get_memory_hotplug_support(Object *obj, Error **errp)
{
    ICH9LPCState *s = ICH9_LPC_DEVICE(obj);

    return s->pm.acpi_memory_hotplug.is_enabled;
}

static void ich9_pm_set_memory_hotplug_support(Object *obj, bool value,
                                               Error **errp)
{
    ICH9LPCState *s = ICH9_LPC_DEVICE(obj);

    s->pm.acpi_memory_hotplug.is_enabled = value;
}

void ich9_pm_add_properties(Object *obj, ICH9LPCPMRegs *pm, Error **errp)
{
    static const uint32_t gpe0_len = ICH9_PMIO_GPE0_LEN;
    pm->acpi_memory_hotplug.is_enabled = true;

    object_property_add_uint32_ptr(obj, ACPI_PM_PROP_PM_IO_BASE,
                                   &pm->pm_io_base, errp);
    object_property_add(obj, ACPI_PM_PROP_GPE0_BLK, "uint32",
                        ich9_pm_get_gpe0_blk,
                        NULL, NULL, pm, NULL);
    object_property_add_uint32_ptr(obj, ACPI_PM_PROP_GPE0_BLK_LEN,
                                   &gpe0_len, errp);
    object_property_add_bool(obj, "memory-hotplug-support",
                             ich9_pm_get_memory_hotplug_support,
                             ich9_pm_set_memory_hotplug_support,
                             NULL);
}

void ich9_pm_device_plug_cb(ICH9LPCPMRegs *pm, DeviceState *dev, Error **errp)
{
    if (pm->acpi_memory_hotplug.is_enabled &&
        object_dynamic_cast(OBJECT(dev), TYPE_PC_DIMM)) {
        acpi_memory_plug_cb(&pm->acpi_regs, pm->irq, &pm->acpi_memory_hotplug,
                            dev, errp);
    } else {
        error_setg(errp, "acpi: device plug request for not supported device"
                   " type: %s", object_get_typename(OBJECT(dev)));
    }
}
